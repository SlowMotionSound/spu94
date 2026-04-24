"""Structural conformance for docs/BIBLIOGRAPHY.md.

Two invariants:
  1. The cross-ref checker passes on the committed docs (every
     `BIB-nnn` in DECISIONS.md resolves to a `### BIB-nnn:` entry in
     BIBLIOGRAPHY.md). Wrapping scripts/check_bibliography_refs.py
     into ctest so any new ADR referencing an undefined BIB-nnn trips
     the build.
  2. Every `### BIB-nnn:` entry carries a `**URL:**` field. Matches
     the existing bibliography schema (BIB-001..BIB-013 all have it);
     keeps additive entries honest.
"""
import re
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
BIB = REPO / "docs" / "BIBLIOGRAPHY.md"
CHECKER = REPO / "scripts" / "check_bibliography_refs.py"


def test_cross_ref_checker_passes():
    r = subprocess.run(
        ["python3", str(CHECKER)],
        capture_output=True,
        cwd=REPO,
    )
    assert r.returncode == 0, (
        f"cross-ref checker failed; stderr:\n{r.stderr.decode()}"
    )


def test_every_entry_has_url_field():
    text = BIB.read_text()
    sections = re.split(r'(?=^### BIB-\d{3})', text, flags=re.MULTILINE)
    missing = []
    for section in sections:
        m = re.match(r'^### (BIB-\d{3})', section)
        if not m:
            continue
        bib_id = m.group(1)
        if "**URL:**" not in section and "**URL**" not in section:
            missing.append(bib_id)
    assert not missing, (
        f"BIBLIOGRAPHY entries missing **URL:** field: {missing}"
    )
