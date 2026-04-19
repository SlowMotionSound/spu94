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

# Pass 1: undefined symbols (nm -u). The (^|[^A-Za-z0-9_]) ... ([^A-Za-z0-9_]|$)
# anchors prevent false positives on symbols that contain the forbidden words
# as substrings. We deliberately do NOT use \b here: \b treats `_` as a word
# character, so a future widening of the list (e.g., `aligned_alloc`,
# `posix_memalign`) would silently miss symbols like `__aligned_alloc_hook` or
# `xmalloc`. The explicit non-[A-Za-z0-9_] boundaries match any underscore-
# adjacent variant of a forbidden symbol, future-proofing the gate.
#
# Capture the tool output ONCE so a tool failure (corrupt library, missing
# binary, permission error) is a distinct signal from "no forbidden symbols
# found." Under `set -euo pipefail`, the previous `if nm ... | grep -q ...`
# pattern masked tool failure entirely: the `if` swallows the pipeline exit
# status, and `2>/dev/null` hid the diagnostic. That defeated the SC-1
# guarantee — a silent `nm` failure looked like a clean library. Now any
# non-zero `nm` exit aborts with exit 2, distinct from a forbidden-symbol
# match (exit 1).
NM_OUT=$(nm -u "$LIB" 2>&1) || {
    echo "FAIL: nm failed on $LIB" >&2
    echo "$NM_OUT" >&2
    exit 2
}
if echo "$NM_OUT" | grep -qE '(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)([^A-Za-z0-9_]|$)'; then
    echo "FAIL: $LIB references heap functions via undefined symbols:" >&2
    echo "$NM_OUT" | grep -E '(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)([^A-Za-z0-9_]|$)' >&2
    FAIL=1
fi

# Pass 2: dynamic relocations (readelf -r). Catches the stripped-library case
# where nm may not show all imports. Same capture-then-grep discipline as
# Pass 1: tool failure exits 2; forbidden-symbol match sets FAIL=1. Uses the
# same non-[A-Za-z0-9_] boundary pattern as Pass 1 so the two checks share a
# regex contract (see the Pass 1 comment block above).
READELF_OUT=$(readelf -r "$LIB" 2>&1) || {
    echo "FAIL: readelf failed on $LIB" >&2
    echo "$READELF_OUT" >&2
    exit 2
}
if echo "$READELF_OUT" | grep -qE '(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)([^A-Za-z0-9_]|$)'; then
    echo "FAIL: $LIB dynamic relocations reference heap functions:" >&2
    echo "$READELF_OUT" | grep -E '(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)([^A-Za-z0-9_]|$)' >&2
    FAIL=1
fi

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi

echo "OK: $LIB is heap-free (no malloc/calloc/realloc/free imports)."
exit 0
