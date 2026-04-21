#!/usr/bin/env python3
"""verify_preset_sources.py -- Phase 5 D-07 cell-equality check.

Loads the two audit CSVs (BIB-011 nocash, BIB-012 hitmen) and asserts they
agree byte-for-byte across all 350 cells (10 presets x 35 regs).

Disagreements documented in 05-preset-values-audit-resolutions.md are
acknowledged and skipped: the CSVs preserve per-source provenance verbatim,
and the resolutions file is the paired audit record of how each
disagreement was decided. The verifier is green iff every remaining
disagreement (i.e. undocumented) is zero. This matches the Task 4
acceptance criterion "the verifier is re-run to confirm only resolved
cells remain red".

Exit codes:
  0 -- all unresolved cells agree (any residual disagreements are
       covered by an entry in resolutions.md)
  1 -- one or more UNDOCUMENTED cell disagreements; first 20 printed
  2 -- CSV schema violation (missing rows, header mismatch, file missing,
       etc.) or resolutions.md parse failure
"""
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
NOCASH = ROOT / ".planning" / "research" / "05-preset-values-audit-nocash.csv"
HITMEN = ROOT / ".planning" / "research" / "05-preset-values-audit-hitmen.csv"
RESOLUTIONS = ROOT / ".planning" / "research" / "05-preset-values-audit-resolutions.md"


def load(path):
    with open(path) as f:
        r = csv.DictReader(f)
        if r.fieldnames != ["preset_name", "reg_idx", "reg_name", "hex_value"]:
            raise AssertionError(f"schema mismatch in {path}: {r.fieldnames}")
        rows = {}
        for row in r:
            key = (row["preset_name"], int(row["reg_idx"]))
            rows[key] = row["hex_value"].lower().strip()
    if len(rows) != 350:
        raise AssertionError(f"{path}: expected 350 cells, got {len(rows)}")
    return rows


def load_resolutions(path):
    """Return the set of (preset_name, reg_idx) keys that are documented in
    resolutions.md. Missing file = empty set (strict-audit mode). Parse
    failure = raise to trigger exit code 2."""
    if not path.exists():
        return set()
    keys = set()
    row_re = re.compile(
        r"^\s*\|\s*([^|]+?)\s*\|\s*(\d+)\s*\|\s*\S+\s*\|\s*0x[0-9a-fA-F]+\s*\|"
        r"\s*0x[0-9a-fA-F]+\s*\|\s*0x[0-9a-fA-F]+\s*\|\s*BIB-\d+\s*\|\s*$"
    )
    with open(path) as f:
        for line in f:
            m = row_re.match(line)
            if m:
                preset = m.group(1).strip()
                reg_idx = int(m.group(2))
                keys.add((preset, reg_idx))
    return keys


def main():
    try:
        nocash = load(NOCASH)
        hitmen = load(HITMEN)
    except (AssertionError, FileNotFoundError) as e:
        print(f"SCHEMA FAIL: {e}", file=sys.stderr)
        return 2
    try:
        resolved = load_resolutions(RESOLUTIONS)
    except Exception as e:
        print(f"RESOLUTIONS PARSE FAIL: {e}", file=sys.stderr)
        return 2

    disagreements = []
    resolved_hits = []
    for key in sorted(nocash):
        if nocash[key] != hitmen.get(key):
            if key in resolved:
                resolved_hits.append((key, nocash[key], hitmen.get(key)))
            else:
                disagreements.append((key, nocash[key], hitmen.get(key)))

    if disagreements:
        print(f"FAIL: {len(disagreements)} undocumented cell disagreements")
        for (preset, idx), nv, hv in disagreements[:20]:
            print(f"  {preset} reg[{idx}]: nocash={nv} hitmen={hv}")
        if resolved_hits:
            print(f"(also {len(resolved_hits)} documented disagreements -- see resolutions.md)")
        return 1

    total_agreeing = 350 - len(resolved_hits)
    if resolved_hits:
        print(f"PASS: {total_agreeing}/350 cells agree; "
              f"{len(resolved_hits)} documented disagreements in resolutions.md")
    else:
        print("PASS: 350/350 cells agree across BIB-011 and BIB-012")
    return 0


if __name__ == "__main__":
    sys.exit(main())
