#!/usr/bin/env bash
# Phase 6 Plan 4 Task 2 — PYBIND-06 regression gate.
# Parses the wheel's .dist-info/WHEEL file and asserts the Tag line matches
# the expected shape. Two modes:
#
#   STRICT  — full manylinux_2_28 tag required. Enabled by setting
#             SPU94_WHEEL_STRICT=1 (used by cibuildwheel-produced wheels).
#   RELAXED — accept py3-none-linux_x86_64 (local dev build). Default.
#
# Usage:
#   verify-wheel-tag.sh path/to/dist/*.whl
#   SPU94_WHEEL_STRICT=1 verify-wheel-tag.sh path/to/dist/*.whl
set -euo pipefail

wheel="${1:-}"
if [ -z "$wheel" ] || [ ! -f "$wheel" ]; then
    echo "FAIL: wheel not found: ${wheel:-<none>}" >&2
    exit 1
fi

# The wheel is a zip; .dist-info/WHEEL is a small text file.
wheel_meta="$(unzip -p "$wheel" '*.dist-info/WHEEL' 2>/dev/null || true)"
if [ -z "$wheel_meta" ]; then
    echo "FAIL: could not extract .dist-info/WHEEL from $wheel" >&2
    exit 1
fi

tag_line="$(echo "$wheel_meta" | grep -E '^Tag: ' | head -n1 || true)"
if [ -z "$tag_line" ]; then
    echo "FAIL: no Tag line in WHEEL metadata for $wheel" >&2
    echo "WHEEL contents:" >&2
    echo "$wheel_meta" >&2
    exit 1
fi

# The tag value after "Tag: "
tag="${tag_line#Tag: }"

# Check py3-none prefix first — this is D-23, always required.
if ! [[ "$tag" == py3-none-* ]]; then
    echo "FAIL: wheel tag '$tag' does not start with py3-none- (D-23 violation — Pitfall 1)" >&2
    echo "      Expected py3-none-manylinux_2_28_x86_64 (strict) or py3-none-linux_x86_64 (dev)" >&2
    exit 1
fi

if [ "${SPU94_WHEEL_STRICT:-0}" = "1" ]; then
    # cibuildwheel-produced wheel. Must be the full manylinux_2_28 tag.
    if [ "$tag" != "py3-none-manylinux_2_28_x86_64" ]; then
        echo "FAIL: strict mode — expected 'py3-none-manylinux_2_28_x86_64', got '$tag'" >&2
        exit 1
    fi
else
    # Dev build. Accept py3-none-linux_x86_64 OR py3-none-manylinux_2_28_x86_64
    # (the latter if the dev host happens to have auditwheel run the repair step).
    if [ "$tag" != "py3-none-linux_x86_64" ] && [ "$tag" != "py3-none-manylinux_2_28_x86_64" ]; then
        echo "FAIL: relaxed mode — expected 'py3-none-linux_x86_64' or 'py3-none-manylinux_2_28_x86_64', got '$tag'" >&2
        exit 1
    fi
fi

echo "PASS: wheel tag is '$tag'"
exit 0
