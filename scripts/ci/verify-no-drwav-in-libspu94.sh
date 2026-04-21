#!/usr/bin/env bash
# scripts/ci/verify-no-drwav-in-libspu94.sh — Phase 6 Plan 3 Task 1.
# CLI-03: assert libspu94.so contains zero drwav_ and zero jsmn_ symbols.
#
# The vendored single-header libs (dr_wav, jsmn) are CLI-linked only —
# leaking them into the shared library would violate the "WAV I/O is an
# application concern, not a library concern" discipline and pollute
# any consumer that links libspu94.
#
# Usage:
#   scripts/ci/verify-no-drwav-in-libspu94.sh /path/to/libspu94.so
# Exit codes: 0 = clean, 1 = forbidden symbol found, 2 = library missing.

set -euo pipefail

LIB="${1:-build/src/spu94/libspu94.so}"

if [ ! -f "$LIB" ]; then
    echo "FAIL: $LIB not found — build libspu94 before running this script." >&2
    exit 2
fi

# Capture nm output once so a tool failure (corrupt library, permission
# error) is distinguishable from "no forbidden symbols found."
NM_OUT=$(nm -D "$LIB" 2>&1) || {
    echo "FAIL: nm -D failed on $LIB" >&2
    echo "$NM_OUT" >&2
    exit 2
}

leak_drwav="$(echo "$NM_OUT" | grep -E 'drwav_' || true)"
leak_jsmn="$(echo "$NM_OUT" | grep -E 'jsmn_' || true)"

FAIL=0

if [ -n "$leak_drwav" ]; then
    echo "FAIL: libspu94.so contains dr_wav symbols (CLI-03 violated):" >&2
    echo "$leak_drwav" >&2
    FAIL=1
fi

if [ -n "$leak_jsmn" ]; then
    echo "FAIL: libspu94.so contains jsmn symbols (CLI-03 violated):" >&2
    echo "$leak_jsmn" >&2
    FAIL=1
fi

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi

echo "PASS: libspu94.so has no drwav_ or jsmn_ symbols"
exit 0
