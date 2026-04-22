#!/usr/bin/env bash
# Phase 6 Plan 5: assert README.md contains all 11 D-20 section headings
# in the locked order plus the required content spot-checks.
#
# Usage:
#   verify-readme-sections.sh [path/to/README.md]
#
# Default path is README.md relative to the current working directory.
# Exit code 0 on success (prints "PASS: ..."); exit code 1 on any failure
# with a single-line stderr message identifying the missing heading or
# missing required content.
set -euo pipefail

readme="${1:-README.md}"
if [ ! -f "$readme" ]; then
    echo "FAIL: $readme not found" >&2
    exit 1
fi

# The 10 canonical section headings in D-20 order. The hero / pitch
# paragraph before the first `## ` heading is NOT in this list — it
# lives between the `# SPU-94` title and the first subsection. The
# checker verifies that each of these 10 appears at least once AND
# that they appear in document order.
expected=(
    "## Current state"
    "## Quick install"
    "## Python walkthrough"
    "## CLI walkthrough"
    "## For the DSP-curious"
    "## Roadmap"
    "## Architecture overview"
    "## Licensing posture"
    "## Acknowledgments"
    "## Contributing"
)

# Sanity: README has any level-2 headings at all.
actual_list="$(grep -E '^## ' "$readme" || true)"
if [ -z "$actual_list" ]; then
    echo "FAIL: $readme contains no level-2 headings" >&2
    exit 1
fi

# Check each expected heading appears AND is in order.
prev_line=0
for expected_heading in "${expected[@]}"; do
    line="$(grep -n -F "$expected_heading" "$readme" | head -n1 | cut -d: -f1 || true)"
    if [ -z "$line" ]; then
        echo "FAIL: $readme missing section heading: '$expected_heading'" >&2
        exit 1
    fi
    if [ "$line" -le "$prev_line" ]; then
        echo "FAIL: $readme section '$expected_heading' appears at line $line but earlier sections sort later (order violation)" >&2
        exit 1
    fi
    prev_line="$line"
done

# Specific content spot-checks (D-20 content requirements).
for required in \
    "pip install spu94" \
    "cmake --build build" \
    "spu94 --preset hall" \
    "spu94.SPU94" \
    "import spu94" \
    "vIIR" \
    "39-tap" \
    "Q15" \
    "dr_wav" \
    "jsmn" \
    "LICENSE" ; do
    if ! grep -q -F "$required" "$readme"; then
        echo "FAIL: $readme missing required content: '$required'" >&2
        exit 1
    fi
done

echo "PASS: README.md has all 10 sections in order with required content"
exit 0
