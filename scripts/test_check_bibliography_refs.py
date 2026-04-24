"""D-22: meta-tests for check_bibliography_refs.py.

Two cases:
  1. Positive: the checker exits 0 when run against the committed
     docs/DECISIONS.md + docs/BIBLIOGRAPHY.md (every `BIB-nnn` reference
     in DECISIONS resolves to a `### BIB-nnn:` header in BIBLIOGRAPHY).
  2. Negative (dangling reference): pass a tmp BIBLIOGRAPHY that only
     defines BIB-001 and a tmp DECISIONS that cites BIB-001 plus an
     invented BIB-999; assert the checker exits 1 and stderr names
     BIB-999.
"""
import subprocess
from pathlib import Path

CHECKER = ["python3", "scripts/check_bibliography_refs.py"]
REPO = Path(__file__).resolve().parent.parent


def test_passes_on_current_docs():
    """Committed DECISIONS.md + BIBLIOGRAPHY.md cross-ref clean."""
    r = subprocess.run(CHECKER, capture_output=True, cwd=REPO)
    assert r.returncode == 0, (
        f"checker failed on committed docs; stderr:\n{r.stderr.decode()}"
    )


def test_fails_on_dangling_ref(tmp_path):
    """Invented BIB-999 in DECISIONS with no matching BIBLIOGRAPHY entry
    must trip the checker (exit 1, BIB-999 named on stderr)."""
    dec = tmp_path / "DECISIONS.md"
    bib = tmp_path / "BIBLIOGRAPHY.md"
    dec.write_text("Cites BIB-001 and also BIB-999.\n")
    bib.write_text("### BIB-001: The one real entry\n\n**URL:** https://example\n")
    r = subprocess.run(
        CHECKER + ["--decisions", str(dec), "--bibliography", str(bib)],
        capture_output=True,
        cwd=REPO,
    )
    assert r.returncode == 1, (
        f"expected exit 1 on dangling ref; got {r.returncode}\n"
        f"stdout: {r.stdout.decode()}\nstderr: {r.stderr.decode()}"
    )
    assert b"BIB-999" in r.stderr, (
        f"stderr should name BIB-999; got: {r.stderr.decode()}"
    )
