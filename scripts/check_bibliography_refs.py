#!/usr/bin/env python3
"""DOCS-03: Cross-reference validator for BIB-nnn citations.

Parses docs/DECISIONS.md for every `BIB-nnn` reference and parses
docs/BIBLIOGRAPHY.md for every `### BIB-nnn:` (or `### BIB-nnn —`)
entry header, then exits non-zero if any cited BIB-nnn has no matching
entry.

Threat T-07-06-A (citation scope creep): the regex rejects references
of the form `BIB-<non-three-digit>` — citations must be zero-padded to
exactly three digits — so the ID space stays narrow and auditable.

Usage:
    python3 scripts/check_bibliography_refs.py
    python3 scripts/check_bibliography_refs.py \\
        --decisions path/DECISIONS.md --bibliography path/BIBLIOGRAPHY.md

Exit codes:
    0  — every BIB-nnn in DECISIONS is defined in BIBLIOGRAPHY
    1  — at least one dangling reference (listed on stderr)
    2  — argparse / file-not-found errors (standard argparse behaviour)
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# \b keeps us off substrings inside longer alphanum tokens; {3} rejects
# BIB-1, BIB-42, BIB-0001. The bibliography header regex allows either
# `: ` or ` — ` (em dash) after the ID to match the existing BIB-001
# / BIB-002 style already in the committed file.
BIB_REF = re.compile(r"\bBIB-(\d{3})\b")
BIB_ENTRY = re.compile(r"^### BIB-(\d{3})(?:\s*:|\s*[—-])", re.MULTILINE)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Cross-check every BIB-nnn cited in DECISIONS.md "
                    "against the entry headers in BIBLIOGRAPHY.md."
    )
    ap.add_argument("--decisions", default="docs/DECISIONS.md",
                    help="Path to DECISIONS.md (default: docs/DECISIONS.md)")
    ap.add_argument("--bibliography", default="docs/BIBLIOGRAPHY.md",
                    help="Path to BIBLIOGRAPHY.md (default: docs/BIBLIOGRAPHY.md)")
    args = ap.parse_args(argv)

    dec_path = Path(args.decisions)
    bib_path = Path(args.bibliography)

    if not dec_path.is_file():
        print(f"FAIL: decisions file not found: {dec_path}", file=sys.stderr)
        return 2
    if not bib_path.is_file():
        print(f"FAIL: bibliography file not found: {bib_path}", file=sys.stderr)
        return 2

    dec_text = dec_path.read_text()
    bib_text = bib_path.read_text()

    cited = set(BIB_REF.findall(dec_text))
    defined = set(BIB_ENTRY.findall(bib_text))

    dangling = sorted(cited - defined)
    unused = sorted(defined - cited)  # informational only

    if dangling:
        print(
            f"FAIL: {len(dangling)} BIB-nnn reference(s) not defined in "
            f"{bib_path}:",
            file=sys.stderr,
        )
        for d in dangling:
            print(f"  BIB-{d}", file=sys.stderr)
        return 1

    print(
        f"PASS: {len(cited)} BIB-nnn reference(s) in {dec_path} all resolve "
        f"against {len(defined)} entries in {bib_path} "
        f"({len(unused)} defined but not yet cited — informational)"
    )
    if unused:
        for u in unused[:5]:
            print(f"  unused: BIB-{u}")
        if len(unused) > 5:
            print(f"  ... and {len(unused) - 5} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
