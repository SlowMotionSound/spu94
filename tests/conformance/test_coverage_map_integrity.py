#!/usr/bin/env python3
"""tests/conformance/test_coverage_map_integrity.py — Phase 7 Plan 01 Task 3.

Structural meta-test for docs/COVERAGE.md. Verifies:

  1. The file exists and parses as Markdown.
  2. All three required section headers are present exactly once:
        `## Per-Register Coverage`
        `## Per-Behavior Coverage`
        `## Per-Spec-Paragraph Coverage`
  3. The pinned wayback snapshot URL appears verbatim.
  4. The Per-Register section contains >= 35 rows (one per spu94_reg_t
     enum entry, SPU94_REG__COUNT).
  5. Every non-Known-Gaps coverage row has a non-empty `test:` cell.
  6. All 12 v-prefix, 13 m-prefix, and 6 d-prefix register names are
     mentioned in the Per-Register section (mechanical sanity check).

This is complementary to scripts/ci/check_coverage.py (which verifies
every test: reference resolves to a passing ctest target). Integrity
failures here indicate COVERAGE.md has drifted structurally — the kind
of breakage a diff-review might miss but a parser catches cheaply.

Runs via ctest (coverage_map_integrity) or directly:
    pytest tests/conformance/test_coverage_map_integrity.py -q
"""
from __future__ import annotations

import re
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
COVERAGE_MD = REPO_ROOT / "docs" / "COVERAGE.md"
PINNED_URL = (
    "https://web.archive.org/web/20260114082525/"
    "https://psx-spx.consoledev.net/soundprocessingunitspu/"
)

REQUIRED_SECTIONS = (
    "## Per-Register Coverage",
    "## Per-Behavior Coverage",
    "## Per-Spec-Paragraph Coverage",
)

# All 35 register names from include/spu94/spu94_registers.h (spu94_reg_t).
V_PREFIX_REGS = (
    "vLOUT", "vROUT", "vIIR",
    "vCOMB1", "vCOMB2", "vCOMB3", "vCOMB4",
    "vWALL", "vAPF1", "vAPF2",
    "vLIN", "vRIN",
)  # 12
M_PREFIX_REGS = (
    "mBASE",
    "mLSAME", "mRSAME",
    "mLCOMB1", "mRCOMB1", "mLCOMB2", "mRCOMB2",
    "mLDIFF", "mRDIFF",
    "mLCOMB3", "mRCOMB3", "mLCOMB4", "mRCOMB4",
    "mLAPF1", "mRAPF1", "mLAPF2", "mRAPF2",
)  # 17 (mBASE + 16 reverb-block)
D_PREFIX_REGS = (
    "dAPF1", "dAPF2",
    "dLSAME", "dRSAME",
    "dLDIFF", "dRDIFF",
)  # 6


@pytest.fixture(scope="module")
def coverage_text() -> str:
    assert COVERAGE_MD.exists(), f"COVERAGE.md not found at {COVERAGE_MD}"
    return COVERAGE_MD.read_text()


def test_file_exists_and_nonempty(coverage_text: str):
    assert len(coverage_text) > 0, "COVERAGE.md is empty"


def test_all_three_sections_present(coverage_text: str):
    for section in REQUIRED_SECTIONS:
        # Use regex with ^ anchor to require it appears as a heading, not
        # just inside prose. The file-level prose may mention `## Known
        # Gaps` inside backticks; those won't match a start-of-line anchor.
        pattern = re.compile(r"^" + re.escape(section) + r"\s*$", re.M)
        matches = pattern.findall(coverage_text)
        assert len(matches) == 1, (
            f"expected exactly one `{section}` heading, got {len(matches)}"
        )


def test_pinned_wayback_url_present(coverage_text: str):
    assert PINNED_URL in coverage_text, (
        f"pinned wayback URL missing: {PINNED_URL}"
    )


def _section_body(text: str, start_heading: str) -> str:
    """Return the body of the section starting with `## <name>` up to
    the next `## ` heading (exclusive)."""
    pattern = re.compile(
        r"^" + re.escape(start_heading) + r"\s*$(.*?)(?=^## |\Z)",
        re.M | re.S,
    )
    m = pattern.search(text)
    assert m, f"section body not found: {start_heading}"
    return m.group(1)


def test_per_register_section_has_at_least_35_data_rows(coverage_text: str):
    body = _section_body(coverage_text, "## Per-Register Coverage")
    # Data rows: start with `|`, contain at least 3 pipes, exclude header
    # separators (| --- | --- | ...).
    data_rows = [
        ln
        for ln in body.splitlines()
        if re.match(r"^\s*\|.*\|.*\|.*\|", ln)
        and not re.match(r"^\s*\|\s*[-: ]+\s*\|", ln)
        and "`" in ln  # a real coverage row has backticks; header doesn't
    ]
    assert len(data_rows) >= 35, (
        f"Per-Register section must have >=35 rows (SPU94_REG__COUNT), "
        f"got {len(data_rows)}"
    )


def test_every_non_gaps_row_has_non_empty_test_cell(coverage_text: str):
    """Every row under the three recognized sections (not Known Gaps)
    must have at least one backticked cell of the form `<path>::<name>`.
    An empty `` cell outside Known Gaps is a structural regression."""
    violations = []
    in_known_gaps = False
    in_recognized = False
    for i, line in enumerate(coverage_text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("## "):
            if stripped == "## Known Gaps":
                in_known_gaps = True
                in_recognized = False
                continue
            in_known_gaps = False
            in_recognized = stripped in REQUIRED_SECTIONS
            continue
        if not in_recognized or in_known_gaps:
            continue
        if not re.match(r"^\s*\|.*\|.*\|.*\|", line):
            continue
        # Skip header separators.
        if re.match(r"^\s*\|\s*[-: ]+\s*\|", line):
            continue
        cells = re.findall(r"`([^`]*)`", line)
        if not cells:
            continue  # plain-text header row, ignored
        # Require at least one cell of the form `<path>::<name>`.
        has_ref = any("::" in c and c.strip() for c in cells)
        if not has_ref:
            violations.append(f"line {i}: {line.strip()}")
    assert not violations, (
        "rows under recognized sections must have a non-empty test: "
        f"cell; violations:\n" + "\n".join(violations)
    )


@pytest.mark.parametrize("reg", V_PREFIX_REGS)
def test_v_prefix_register_present(coverage_text: str, reg: str):
    body = _section_body(coverage_text, "## Per-Register Coverage")
    assert f"`{reg}`" in body, f"register `{reg}` missing from Per-Register section"


@pytest.mark.parametrize("reg", M_PREFIX_REGS)
def test_m_prefix_register_present(coverage_text: str, reg: str):
    body = _section_body(coverage_text, "## Per-Register Coverage")
    assert f"`{reg}`" in body, f"register `{reg}` missing from Per-Register section"


@pytest.mark.parametrize("reg", D_PREFIX_REGS)
def test_d_prefix_register_present(coverage_text: str, reg: str):
    body = _section_body(coverage_text, "## Per-Register Coverage")
    assert f"`{reg}`" in body, f"register `{reg}` missing from Per-Register section"


def test_total_register_count_is_35(coverage_text: str):
    total = len(V_PREFIX_REGS) + len(M_PREFIX_REGS) + len(D_PREFIX_REGS)
    assert total == 35, (
        f"fixture tuples must sum to 35 registers (SPU94_REG__COUNT), "
        f"got v={len(V_PREFIX_REGS)} + m={len(M_PREFIX_REGS)} + "
        f"d={len(D_PREFIX_REGS)} = {total}"
    )
