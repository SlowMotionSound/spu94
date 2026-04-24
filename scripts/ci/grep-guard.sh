#!/usr/bin/env bash
# scripts/ci/grep-guard.sh -- BUILD-07 two-tier scope.
#
# Tier 1 (core library -- src/spu94, include/spu94):
#   Forbid float, double, malloc, calloc, realloc, free, unqualified long.
#   Rationale: bit-faithful integer arithmetic; freestanding-C / MCU portability.
#
# Tier 2 (CLI -- src/cli):
#   Forbid float, double only.
#   Permit malloc/free (CLI owns its work-buf allocation + wraps dr_wav I/O).
#   Permit unqualified long (stdlib boundary: strtol, ftell, printf %ld).
#
# Tests live under tests/ and are OUT of scope (may use any C as needed).
# Requires GNU grep (word-boundary syntax). macOS/BSD not supported in CI.
#
# Exit codes: 0 = clean, 1 = forbidden token found, 2 = environment problem.
#
# See docs/DECISIONS.md ADR-0022 (two-tier scope, amends ADR-0003).
#
# -----------------------------------------------------------------------------
# KNOWN LIMITATION (unqualified 'long' detection, Tier 1 only)
# -----------------------------------------------------------------------------
# The detection is line-granular, not token-granular. A single source line that
# contains BOTH `long long X;` AND unqualified `long Y;` will be filtered out
# by the `grep -v 'long long'` pass and will NOT be caught. SPU-94 core C body
# never mixes them. Pinned by fixture CASE 7 in test-grep-guard.sh -- a future
# tightening will fail that fixture and force an intentional review.
# -----------------------------------------------------------------------------

set -euo pipefail

FORBIDDEN_CORE='\b(float|double|malloc|calloc|realloc|free)\b'
FORBIDDEN_CLI='\b(float|double)\b'
LONG_PATTERN='\blong\b'

# Tier 1: core library files.
mapfile -t CORE_FILES < <(find src/spu94 include/spu94 -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null | sort)

# Tier 2: CLI files (optional subtree; absent in some configurations).
mapfile -t CLI_FILES < <(find src/cli -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null | sort)

total=$(( ${#CORE_FILES[@]} + ${#CLI_FILES[@]} ))
if [ "$total" -eq 0 ]; then
    echo "grep-guard: no source files found under src/spu94, include/spu94, or src/cli; nothing to scan."
    exit 0
fi

fail=0

# --- Tier 1: core library strict ---
if [ "${#CORE_FILES[@]}" -gt 0 ]; then
    if grep -nE "$FORBIDDEN_CORE" "${CORE_FILES[@]}"; then
        echo "ERROR [grep-guard/core]: forbidden token (float|double|malloc|calloc|realloc|free) in core sources above."
        fail=1
    fi
    if grep -nE "$LONG_PATTERN" "${CORE_FILES[@]}" | grep -v 'long long'; then
        echo "ERROR [grep-guard/core]: unqualified 'long' in core sources above. Use int32_t / int64_t."
        fail=1
    fi
fi

# --- Tier 2: CLI narrower ---
if [ "${#CLI_FILES[@]}" -gt 0 ]; then
    if grep -nE "$FORBIDDEN_CLI" "${CLI_FILES[@]}"; then
        echo "ERROR [grep-guard/cli]: forbidden token (float|double) in CLI sources above."
        fail=1
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo
    echo "grep-guard FAILED. See docs/DECISIONS.md ADR-0022 for tier rationale."
    exit 1
fi

echo "grep-guard: OK (scanned ${#CORE_FILES[@]} core + ${#CLI_FILES[@]} CLI files = $total total)."
exit 0
