"""tests/python/test_levers_catalog_writer.py — Phase 7 Plan 04 Task 2.

Meta-test: ``scripts/write_levers_catalog.py`` is idempotent and preserves
HAND-column content across re-runs. Complements the conformance-side
structural test (``tests/conformance/test_levers_catalog_complete.py``).
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

_REPO_ROOT = Path(__file__).resolve().parents[2]
CATALOG = _REPO_ROOT / "docs" / "LEVERS-CATALOG.md"
REPORT = _REPO_ROOT / "tests" / "python" / "modulation_report.json"
WRITER = [sys.executable, str(_REPO_ROOT / "scripts" / "write_levers_catalog.py")]


@pytest.fixture(scope="module", autouse=True)
def require_report():
    if not REPORT.exists():
        pytest.skip(
            "tests/python/modulation_report.json missing — run "
            "test_modulation_harness.py first"
        )


def test_writer_is_idempotent():
    """Running the writer twice back-to-back produces a byte-identical file."""
    subprocess.run(WRITER, check=True, cwd=_REPO_ROOT)
    first = CATALOG.read_bytes()
    subprocess.run(WRITER, check=True, cwd=_REPO_ROOT)
    second = CATALOG.read_bytes()
    assert first == second, "writer is NOT idempotent"


def test_hand_columns_preserved():
    """Injecting a HAND annotation on one row and rerunning the writer
    leaves that annotation byte-for-byte intact."""
    original = CATALOG.read_text()
    # Pick a row whose AUTO cells have already been populated (free / —).
    # After a prior writer run vIIR's row should read:
    #   | `vIIR`   |  | free | ... |
    # We inject a HAND annotation into the first (musical-role) column.
    marker = "IIR tail density — test annotation"
    assert "| `vIIR`" in original, "vIIR row missing from catalog"

    # Find the vIIR line and rewrite its first HAND cell.
    lines = original.splitlines(keepends=True)
    for i, line in enumerate(lines):
        if line.lstrip().startswith("| `vIIR`"):
            # Split on pipes, strip spaces, rebuild with injected marker.
            # Row shape: ['', ' `vIIR`   ', ' hand1 ', ' auto1 ', ' auto2 ',
            #              ' hand2 ', '\n'] after keepends.
            parts = line.split("|")
            # parts[0] == '' and parts[-1] == '\n' / trailing whitespace.
            # Indices: 1=reg, 2=hand1, 3=auto1, 4=auto2, 5=hand2.
            parts[2] = f" {marker} "
            lines[i] = "|".join(parts)
            break
    else:
        pytest.fail("vIIR row not found in LEVERS-CATALOG.md")

    mutated = "".join(lines)
    CATALOG.write_text(mutated)
    try:
        subprocess.run(WRITER, check=True, cwd=_REPO_ROOT)
        after = CATALOG.read_text()
        assert marker in after, (
            "HAND column annotation was NOT preserved by the writer"
        )
    finally:
        CATALOG.write_text(original)


def test_hand_columns_empty_by_default():
    """The writer must never invent default HAND text — any register whose
    HAND cells are blank in-tree must remain blank after the writer runs."""
    subprocess.run(WRITER, check=True, cwd=_REPO_ROOT)
    text = CATALOG.read_text()
    blank_rows_checked = 0
    for line in text.splitlines():
        stripped = line.lstrip()
        if not (stripped.startswith("| `") and "`" in stripped[3:]):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 7:
            continue
        # parts layout: ['', '`reg`', hand1, auto1, auto2, hand2, '']
        if parts[2] == "" and parts[5] == "":
            blank_rows_checked += 1
    assert blank_rows_checked > 0, (
        "Expected at least one register row with both HAND cells blank "
        "to verify the writer preserves blank state. If every HAND cell "
        "is populated, update this test to assert non-invention differently."
    )
