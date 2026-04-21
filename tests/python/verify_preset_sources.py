#!/usr/bin/env python3
"""verify_preset_sources.py -- Phase 5 D-07 cell-equality check.

Loads the two audit CSVs (BIB-011 nocash, BIB-012 hitmen) and asserts they
agree byte-for-byte across all 350 cells (10 presets x 35 regs).

Exit codes:
  0 -- all 350 cells agree
  1 -- one or more cells disagree; first 20 disagreements printed
  2 -- CSV schema violation (missing rows, header mismatch, file missing, etc.)
"""
import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
NOCASH = ROOT / ".planning" / "research" / "05-preset-values-audit-nocash.csv"
HITMEN = ROOT / ".planning" / "research" / "05-preset-values-audit-hitmen.csv"


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


def main():
    try:
        nocash = load(NOCASH)
        hitmen = load(HITMEN)
    except (AssertionError, FileNotFoundError) as e:
        print(f"SCHEMA FAIL: {e}", file=sys.stderr)
        return 2
    disagreements = []
    for key in sorted(nocash):
        if nocash[key] != hitmen.get(key):
            disagreements.append((key, nocash[key], hitmen.get(key)))
    if disagreements:
        print(f"FAIL: {len(disagreements)} cell disagreements")
        for (preset, idx), nv, hv in disagreements[:20]:
            print(f"  {preset} reg[{idx}]: nocash={nv} hitmen={hv}")
        return 1
    print("PASS: 350/350 cells agree across BIB-011 and BIB-012")
    return 0


if __name__ == "__main__":
    sys.exit(main())
