#!/usr/bin/env bash
# scripts/ci/verify-no-heap-symbols.sh
# Phase 2 SC 1 + API-01: proves that libspu94.so does not import heap
# functions. Complements scripts/ci/grep-guard.sh (which checks source
# text only -- the linker can still pull in heap functions via a
# toolchain surprise).
#
# Exit codes: 0 = clean, 1 = forbidden symbol found, 2 = library not found.
#
# -----------------------------------------------------------------------------
# WHAT THIS CHECKS
# -----------------------------------------------------------------------------
# 1. `nm -u libspu94.so` lists undefined (= imported) symbols -- what the
#    library asks the dynamic linker to resolve at load time.
# 2. `readelf -r libspu94.so` lists dynamic relocations including references
#    to external functions.
# Grepping both outputs for the forbidden-word-list catches both the "library
# imports heap" case and the "library has dynamic relocations to heap" case.
# 02-RESEARCH.md Pitfall 8 documents why both are needed.
#
# Forbidden word list matches BUILD-07 grep-guard on purpose: one source of
# truth for "no heap" across source text and linker symbols. Kept inline (not
# exported) so this script stays self-contained.
# -----------------------------------------------------------------------------

set -euo pipefail

LIB="${1:-build/src/spu94/libspu94.so}"

if [ ! -f "$LIB" ]; then
    echo "FAIL: $LIB not found -- build libspu94 before running this script." >&2
    exit 2
fi

FAIL=0

# Pass 1: undefined symbols (nm -u). Word boundaries prevent false positives
# on symbols that happen to contain the forbidden words as substrings.
if nm -u "$LIB" 2>/dev/null | grep -qE '\b(malloc|calloc|realloc|free)\b'; then
    echo "FAIL: $LIB references heap functions via undefined symbols:" >&2
    nm -u "$LIB" | grep -E '\b(malloc|calloc|realloc|free)\b' >&2 || true
    FAIL=1
fi

# Pass 2: dynamic relocations (readelf -r). Catches the stripped-library case
# where nm may not show all imports.
if readelf -r "$LIB" 2>/dev/null | grep -qE '\b(malloc|calloc|realloc|free)\b'; then
    echo "FAIL: $LIB dynamic relocations reference heap functions:" >&2
    readelf -r "$LIB" | grep -E '\b(malloc|calloc|realloc|free)\b' >&2 || true
    FAIL=1
fi

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi

echo "OK: $LIB is heap-free (no malloc/calloc/realloc/free imports)."
exit 0
