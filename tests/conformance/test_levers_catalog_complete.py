"""tests/conformance/test_levers_catalog_complete.py — Phase 7 Plan 04.

Structural conformance gate: docs/LEVERS-CATALOG.md has exactly 35 data
rows in spu94_reg_t enum order, AUTO columns populated (not `—`) for all
35 registers, and every row references a real register name.
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "python"))

if "SPU94_LIB" not in os.environ:
    _candidate = _REPO_ROOT / "build" / "src" / "spu94" / "libspu94.so"
    if _candidate.exists():
        os.environ["SPU94_LIB"] = str(_candidate)

import spu94  # noqa: E402

CATALOG = _REPO_ROOT / "docs" / "LEVERS-CATALOG.md"

# Row pattern: `| \`name\` | ... |`
_ROW_RE = re.compile(r"^\|\s*`([a-zA-Z0-9_]+)`\s*\|", re.M)


def test_35_rows_present_in_enum_order():
    text = CATALOG.read_text()
    rows = _ROW_RE.findall(text)
    names = [r.name for r in spu94.Register]
    assert rows == names, (
        "LEVERS-CATALOG row order != spu94.Register enum order:\n"
        f"  got:      {rows}\n"
        f"  expected: {names}"
    )


def test_no_empty_auto_cells_after_writer_runs():
    """Every data row has non-`—` AUTO cells (modulation_cost + zipper_onset).

    Assumes ``scripts/write_levers_catalog.py`` has run since the scaffold
    was created (which the meta-test and the plan acceptance criteria
    both invoke).
    """
    text = CATALOG.read_text()
    # Parse table rows: | `reg` | hand1 | auto1 | auto2 | hand2 |
    row_re = re.compile(
        r"^\|\s*`([a-zA-Z0-9_]+)`\s*"
        r"\|([^|]*)"
        r"\|([^|]*)"
        r"\|([^|]*)"
        r"\|([^|]*)\|",
        re.M,
    )
    hits = row_re.findall(text)
    assert len(hits) == 35, f"expected 35 data rows, got {len(hits)}"
    for reg, _hand1, auto1, auto2, _hand2 in hits:
        auto1_s = auto1.strip()
        auto2_s = auto2.strip()
        assert auto1_s and auto1_s != "—", (
            f"{reg}: modulation_cost AUTO cell is empty / placeholder"
        )
        assert auto2_s and auto2_s != "—", (
            f"{reg}: zipper_onset AUTO cell is empty / placeholder"
        )
