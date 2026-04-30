#!/usr/bin/env python3
"""scripts/ci/check_coverage.py — D-02 CI-enforced coverage validator.

Parses docs/COVERAGE.md's three sections (Per-Register Coverage,
Per-Behavior Coverage, Per-Spec-Paragraph Coverage) plus the optional
Known Gaps section, extracts every backticked `test:` reference of the
form `<source-path>::<ctest-name>`, and verifies:

  1. `<source-path>` exists on disk.
  2. The named ctest target exists and passes.

Exit 0 on success (prints 'PASS: <N> COVERAGE.md rows all green' to
stdout); exit 1 on any failure (prints one or more 'FAIL: ...' lines
to stderr followed by a summary).

Every source-path-ends-in-.py or .c or .sh row is dispatched via ctest
(`ctest -R "^<name>$" --output-on-failure`) — the project registers
every test, including its python fuzz harnesses and shell-based rt-safety
checks, as a ctest target. This keeps the dispatch single-path, which
also keeps the T-07-01-A threat surface single-path.

THREAT MITIGATION — T-07-01-A (CI script injection via test names):
  - Every subprocess invocation uses argv-split form with shell=False.
  - Before any subprocess call, the test_name is scanned for shell
    metacharacters in the REJECT set: `;`, `&`, `|`, `>`, `<`, `$`,
    backtick, newline, and whitespace. Any hit exits 1 with
    'metacharacter rejected'. The positive allowlist is
    [A-Za-z0-9_].

Empty `test:` cells are permitted only inside the `## Known Gaps`
section; empty cells anywhere else trigger 'FAIL: empty test: field
outside Known Gaps'.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------
# Threat mitigation — shell metacharacter allowlist (T-07-01-A).
# --------------------------------------------------------------------------

# Regex for a valid ctest registration name: alnum + underscore only.
_VALID_TEST_NAME = re.compile(r"^[A-Za-z0-9_]+$")

# Explicit reject set for clarity / grep. Not used directly by the check
# (the allowlist regex above is authoritative) but documented here so
# `grep -E '[\$;&|><\`\n]' scripts/ci/check_coverage.py` finds the intent.
_REJECT_METACHARS = r"[;&|><$`\n\s]"  # noqa: E501,W605


def _reject_if_metachars(test_name: str) -> str | None:
    """Return an error string if test_name contains shell metacharacters,
    None otherwise. Uses positive allowlist to mitigate T-07-01-A."""
    if not _VALID_TEST_NAME.match(test_name):
        return (
            f"metacharacter rejected: test-name {test_name!r} contains "
            f"a character outside the [A-Za-z0-9_] allowlist"
        )
    return None


# --------------------------------------------------------------------------
# COVERAGE.md parser.
# --------------------------------------------------------------------------

# Match any backticked cell; we filter for `<path>::<name>` shape below.
_BACKTICK_CELL_RE = re.compile(r"`([^`]*)`")

# Cells of interest: contain `::` and the path starts with `tests/`
# or `tools/` (project convention — coverage sources live under tests/
# or tools/; tools/ entries are validated for file existence only, not
# via ctest, since they are standalone measurement scripts).
_TEST_REF_RE = re.compile(r"^((?:tests|tools)/[^\s`]+)::([^\s`]+)$")

# Row starts with a pipe and contains at least three pipes (header separator
# rows are filtered by requiring at least one backtick in the row).
_TABLE_ROW_RE = re.compile(r"^\s*\|.*\|.*\|.*\|")


def parse_coverage_md(path: Path):
    """Parse COVERAGE.md; return (entries, empty_outside_gaps) where:
       entries: list of (line_no, ctest_name, source_path, row_text)
       empty_outside_gaps: list of (line_no, row_text) for empty test:
                           cells outside the Known Gaps section.
    """
    text = path.read_text()
    lines = text.splitlines()
    entries = []
    empty_outside = []
    in_known_gaps = False
    in_table = False
    # Track which columns are inside a table — we only look at rows that
    # start with `|` and that live beneath one of the four recognized
    # section headers. We DON'T try to identify which column is the
    # `test:` column; we take every backticked `<path>::<name>` cell as
    # a test reference and every purely empty backtick pair as a potential
    # empty-test-cell signal.

    # Sections in order; rows under any of them are coverage rows.
    recognized_sections = {
        "## Per-Register Coverage",
        "## Per-Behavior Coverage",
        "## Per-Spec-Paragraph Coverage",
        "## ADPCM Coverage",
        "## DAC Model Coverage",
    }
    in_recognized = False

    for i, line in enumerate(lines, start=1):
        stripped = line.strip()
        # Section tracking.
        if stripped.startswith("## "):
            if stripped == "## Known Gaps":
                in_known_gaps = True
                in_recognized = False
                continue
            in_known_gaps = False
            in_recognized = stripped in recognized_sections
            continue
        # Skip non-table, non-recognized lines.
        if not (in_recognized or in_known_gaps):
            continue
        if not _TABLE_ROW_RE.match(line):
            continue
        # Skip header separators (| --- | --- | ...).
        if re.match(r"^\s*\|\s*[-: ]+\s*\|", line):
            continue
        # Extract backticked cells.
        cells = _BACKTICK_CELL_RE.findall(line)
        if not cells:
            # Row is table-shaped but has no backticks — it's a header row
            # (| Register | Type | test: | Notes |) or similar. Skip.
            continue
        found_test_ref = False
        for cell in cells:
            # Any backticked cell containing `::` is treated as a coverage
            # reference — even if it fails the `tests/…` allowlist shape —
            # so the validator can flag malformed or metacharacter-bearing
            # cells rather than silently ignore them. The _TEST_REF_RE
            # shape check happens inside validation, not here.
            if "::" not in cell:
                continue
            m = _TEST_REF_RE.match(cell)
            if m:
                path_part, name_part = m.group(1), m.group(2)
                entries.append((i, name_part, path_part, line))
            else:
                # Malformed cell (bad path shape, bad name shape, or
                # contains shell metacharacters between path and name).
                # Split on the first `::` and let validation flag it.
                path_part, _, name_part = cell.partition("::")
                entries.append((i, name_part, path_part, line))
            found_test_ref = True
        # Empty-test-cell detection: look for `` (backtick immediately
        # followed by backtick) which yields an empty string in cells.
        if not found_test_ref and "``" in line and not in_known_gaps:
            # Only flag if this row is under a recognized section.
            if in_recognized:
                empty_outside.append((i, line))
    return entries, empty_outside


# --------------------------------------------------------------------------
# Test runner — single-path ctest dispatch.
# --------------------------------------------------------------------------


def run_ctest(test_name: str, build_dir: Path) -> tuple[int, str]:
    """Run `ctest -R "^<test_name>$"` inside build_dir. Return (rc, combined_output)."""
    meta_err = _reject_if_metachars(test_name)
    if meta_err:
        return 1, meta_err
    argv = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "-R",
        f"^{test_name}$",
        "--output-on-failure",
    ]
    # Pin LC_ALL/LANG=C so the "No tests were found" / "No tests to run"
    # prose matcher below is locale-stable. A non-English parent shell would
    # otherwise localize those strings and turn the matcher into a
    # false-pass.
    env = {**os.environ, "LC_ALL": "C", "LANG": "C"}
    # 1800 s (30 min) is comfortably longer than any individual ctest target
    # in this repo (longest is ~14 s for hotpath_alloc_gate) but well under
    # GitHub Actions' 360-min job cap, so a hung ctest fails fast with an
    # actionable error rather than burning the whole job slot.
    CTEST_TIMEOUT_SEC = 1800
    try:
        proc = subprocess.run(
            argv,
            capture_output=True,
            text=True,
            check=False,
            shell=False,
            env=env,
            timeout=CTEST_TIMEOUT_SEC,
        )
    except FileNotFoundError as exc:
        return 1, f"ctest not on PATH: {exc}"
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode("utf-8", errors="replace") if exc.stdout else ""
        stderr = exc.stderr.decode("utf-8", errors="replace") if exc.stderr else ""
        return 1, (
            f"ctest '-R ^{test_name}$' exceeded {CTEST_TIMEOUT_SEC}s timeout "
            f"(likely hung test):\n--- stdout ---\n{stdout[-400:]}\n"
            f"--- stderr ---\n{stderr[-400:]}"
        )
    combined = proc.stdout + proc.stderr
    # ctest returns 0 on any match-pass. If zero tests matched, ctest
    # still exits 0 but says 'No tests were found'. We treat that as a
    # failure (the row names a test that does not exist).
    if "No tests were found" in combined or "No tests to run" in combined:
        return 1, f"no tests matched by ctest -R '^{test_name}$':\n{combined}"
    return proc.returncode, combined


# --------------------------------------------------------------------------
# Main.
# --------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--file",
        type=Path,
        default=Path("docs/COVERAGE.md"),
        help="Path to COVERAGE.md (default: docs/COVERAGE.md).",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path("build"),
        help="CMake build dir containing the ctest registry (default: build).",
    )
    parser.add_argument(
        "--skip-ctest",
        action="store_true",
        help=(
            "Only verify file existence + metacharacter allowlist; skip "
            "the ctest pass check. Used by meta-tests that exercise the "
            "parsing and rejection paths without incurring the full "
            "ctest runtime."
        ),
    )
    args = parser.parse_args()

    if not args.file.exists():
        print(f"FAIL: COVERAGE.md not found at {args.file}", file=sys.stderr)
        return 1

    entries, empty_outside = parse_coverage_md(args.file)

    failures: list[str] = []

    # Fail on empty test: cells outside Known Gaps.
    for line_no, row_text in empty_outside:
        failures.append(
            f"FAIL: empty test: field outside Known Gaps at "
            f"{args.file}:{line_no}: {row_text.strip()}"
        )

    # Group by (path, test_name) to avoid running the same ctest
    # multiple times when multiple rows cite the same target.
    seen: set[tuple[str, str]] = set()
    ordered_unique: list[tuple[int, str, str]] = []
    for line_no, name, path, _ in entries:
        key = (path, name)
        if key in seen:
            continue
        seen.add(key)
        ordered_unique.append((line_no, name, path))

    for line_no, name, path in ordered_unique:
        # 1. File existence.
        if not Path(path).exists():
            failures.append(
                f"FAIL: {args.file.name}:{line_no}: file not found: {path}"
            )
            continue
        # 2. Metacharacter allowlist.
        meta_err = _reject_if_metachars(name)
        if meta_err:
            failures.append(
                f"FAIL: {args.file.name}:{line_no}: {meta_err}"
            )
            continue
        # 3. ctest pass.  tools/ paths are standalone measurement scripts
        #    that are not registered as ctest targets — file-existence is
        #    sufficient for them.
        if args.skip_ctest or path.startswith("tools/"):
            continue
        rc, combined = run_ctest(name, args.build_dir)
        if rc != 0:
            # Classify: "no tests matched" vs non-zero exit.
            if "no tests matched" in combined.lower():
                failures.append(
                    f"FAIL: {args.file.name}:{line_no}: ctest target "
                    f"'{name}' not found ({path})"
                )
            else:
                failures.append(
                    f"FAIL: {args.file.name}:{line_no}: ctest '{name}' "
                    f"returned non-zero ({rc}) for {path}"
                )

    if failures:
        for f in failures:
            print(f, file=sys.stderr)
        print(
            f"\nFAIL: {len(failures)} coverage row(s) failed out of "
            f"{len(entries)} total",
            file=sys.stderr,
        )
        return 1

    print(f"PASS: {len(entries)} COVERAGE.md rows all green")
    return 0


if __name__ == "__main__":
    sys.exit(main())
