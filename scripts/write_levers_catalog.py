#!/usr/bin/env python3
"""scripts/write_levers_catalog.py — Phase 7 Plan 04 D-16.

Idempotent AUTO-column writer for ``docs/LEVERS-CATALOG.md``.

Reads ``tests/python/modulation_report.json``; rewrites the AUTO columns
(modulation_cost, zipper_onset) for each of the 35 registers. HAND
columns (musical_role, M4 lever) are preserved verbatim — the writer
parses the existing table row-by-row, rebuilds each row by substituting
only the AUTO cells, and writes the file back.

Properties:
  - Idempotent — running twice in a row produces a byte-identical file
    (meta-test: ``tests/python/test_levers_catalog_writer.py``).
  - HAND-safe — any user-written HAND cell content survives regeneration
    unchanged (meta-test: same file, ``test_hand_columns_preserved``).
  - Never fills a default into an empty HAND cell.
  - Unknown register names in the table are left untouched — the writer
    refuses to invent rows it didn't find in the scaffold.

Threat mitigations (T-07-04-C / T-07-04-D):
  - Row regex uses named groups + explicit backtick-anchored register
    column; a malformed row won't parse and is passed through verbatim.
  - ``modulation_cost`` values come from a closed enum
    (``free`` / ``sample-quantized`` / ``catastrophic``) so pipe-
    character injection through the JSON report can't happen.
  - ``zipper_onset`` is a formatted string (``"~500 Hz"`` or
    ``"clean through 11 kHz"``) — no user-controlled bytes.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

# Catalog + report live at repo-root-relative paths. The writer always
# resolves against the repo root so it works from any cwd (pytest meta-
# tests invoke it via subprocess with cwd=repo_root).
_REPO_ROOT = Path(__file__).resolve().parents[1]
CATALOG = _REPO_ROOT / "docs" / "LEVERS-CATALOG.md"
REPORT = _REPO_ROOT / "tests" / "python" / "modulation_report.json"

# Row shape: `| \`REG\` | HAND1 | AUTO1 | AUTO2 | HAND2 |`
# HAND / AUTO cells must each be non-pipe — we stop at the next `|`.
# Capture leading whitespace / trailing newline separately so we can
# rebuild the row with its exact original framing preserved.
_ROW = re.compile(
    r"^(?P<lead>\|\s*`(?P<reg>[a-zA-Z0-9_]+)`\s*)"
    r"\|(?P<hand1>[^|]*)"
    r"\|(?P<auto1>[^|]*)"
    r"\|(?P<auto2>[^|]*)"
    r"\|(?P<hand2>[^|]*)"
    r"\|(?P<trail>\s*)$"
)


def format_zipper_onset(modes: dict) -> str:
    """Pick the lowest sine-mode rate at which the zipper-onset proxy
    fired. If no mode fired, the register is 'clean through 11 kHz' —
    the most-conservative label the harness can honestly emit given
    sine mode only spot-checks a single mid-rate. Sweep mode's
    continuous 0.1 Hz → 11 kHz walk did not flag zipper under the
    sample-to-sample-RMS threshold, so the label is warranted."""
    onsets = [
        m.get("zipper_onset_hz") for m in modes.values()
        if m.get("zipper_onset_hz") is not None
    ]
    if not onsets:
        return "clean through 11 kHz"
    lowest = min(onsets)
    return f"~{lowest:.0f} Hz"


def rewrite_row(line: str, report: dict) -> str:
    """Rewrite a catalog row's AUTO cells from ``report``; return line
    unchanged if it doesn't match the data-row shape or if the register
    isn't in the report."""
    m = _ROW.match(line.rstrip("\n"))
    if not m:
        return line
    reg = m.group("reg")
    if reg not in report:
        return line
    mod_cost = report[reg]["modulation_cost"]
    zipper = format_zipper_onset(report[reg]["modes"])
    # Preserve the exact HAND-cell text + leading / trailing framing.
    # AUTO cells get a single leading + trailing space so the output
    # alignment is pleasant without depending on padding tricks.
    newline = "\n" if line.endswith("\n") else ""
    return (
        f"{m.group('lead')}"
        f"|{m.group('hand1')}"
        f"| {mod_cost} | {zipper} "
        f"|{m.group('hand2')}"
        f"|{m.group('trail')}{newline}"
    )


def main(argv: list[str] | None = None) -> int:
    if not REPORT.exists():
        print(
            f"FAIL: {REPORT} missing — run "
            "`pytest tests/python/test_modulation_harness.py -q` first",
            file=sys.stderr,
        )
        return 1
    if not CATALOG.exists():
        print(f"FAIL: {CATALOG} missing", file=sys.stderr)
        return 1

    report = json.loads(REPORT.read_text())
    text = CATALOG.read_text()
    out_lines = [rewrite_row(line, report) for line in text.splitlines(keepends=True)]
    new_text = "".join(out_lines)
    if new_text != text:
        CATALOG.write_text(new_text)
        print(f"Updated {CATALOG} ({len(report)} registers)")
    else:
        print(f"No changes to {CATALOG}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
