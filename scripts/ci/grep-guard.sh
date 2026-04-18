#!/usr/bin/env bash
# scripts/ci/grep-guard.sh
# BUILD-07: forbid float, double, malloc, calloc, realloc, free, and unqualified long
# in core library sources. Allow 'long long'.
# Scope: src/**/*.c, src/**/*.h, include/**/*.c, include/**/*.h
# Tests live under tests/ and are OUT of scope (may use any C as needed).
# Requires GNU grep (word-boundary syntax). macOS/BSD not supported in CI per CONTEXT.md.
#
# Exit codes: 0 = clean, 1 = forbidden token found, 2 = environment problem.
#
# See docs/DECISIONS.md ADR-0003 area (future) for the rationale behind this guard.

set -euo pipefail

FORBIDDEN_CORE='\b(float|double|malloc|calloc|realloc|free)\b'
LONG_PATTERN='\blong\b'

# Collect core files. No file -> trivially clean.
mapfile -t FILES < <(find src include -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null | sort)

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "grep-guard: no core files found under src/ or include/; nothing to scan."
    exit 0
fi

fail=0

# Pass 1: simple forbidden tokens.
if grep -nE "$FORBIDDEN_CORE" "${FILES[@]}"; then
    echo "ERROR [grep-guard]: forbidden token (float|double|malloc|calloc|realloc|free) found in core sources above."
    fail=1
fi

# Pass 2: unqualified 'long' -- subtract 'long long'.
# Use `grep -v 'long long'` to filter out lines where the match is part of 'long long'.
# This is coarse (a line with BOTH 'long long' AND unqualified 'long' would be allowed),
# but SPU-94's core C body never mixes them. Good enough per RESEARCH.md Pitfall 5 (no -P).
if grep -nE "$LONG_PATTERN" "${FILES[@]}" | grep -v 'long long'; then
    echo "ERROR [grep-guard]: unqualified 'long' found in core sources above. Use int32_t / int64_t."
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo
    echo "grep-guard FAILED. See RESEARCH.md section Grep Guard and ADR (future) for the forbidden-token list rationale."
    exit 1
fi

echo "grep-guard: OK (scanned ${#FILES[@]} files)."
exit 0
