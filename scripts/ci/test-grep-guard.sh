#!/usr/bin/env bash
# scripts/ci/test-grep-guard.sh
# Tests scripts/ci/grep-guard.sh on positive + negative fixtures across both tiers.
# Run AFTER grep-guard.sh passes on the real tree.
#
# Exit codes: 0 = all fixture cases pass, 1 = a fixture case's result differs from expected.

set -euo pipefail

GUARD="$(pwd)/scripts/ci/grep-guard.sh"
test -x "$GUARD" || { echo "ERROR: $GUARD not executable"; exit 2; }

fail=0

run_case() {
    local label="$1" expected="$2" seed_file="$3" seed_content="$4"
    local tmp actual
    tmp=$(mktemp -d)
    (
        cd "$tmp"
        mkdir -p "$(dirname "$seed_file")"
        printf '%s\n' "$seed_content" > "$seed_file"
        if "$GUARD" >/dev/null 2>&1; then
            actual=0
        else
            actual=$?
        fi
        if [ "$actual" -eq "$expected" ]; then
            echo "PASS: $label (expected exit $expected, got $actual)"
            exit 0
        else
            echo "FAIL: $label (expected exit $expected, got $actual)"
            exit 1
        fi
    )
    local rc=$?
    rm -rf "$tmp"
    if [ "$rc" -ne 0 ]; then
        fail=1
    fi
}

# --- Core-tier cases (src/spu94, include/spu94) ---
# CASE 1: clean core tree -> exit 0.
run_case "core: clean tree" 0 "src/spu94/a.c"        '/* clean */'$'\n''#include <stdint.h>'$'\n''int32_t x;'
# CASE 2: 'float' in core src -> exit 0 (allowed since v1.5 interpolation engine).
run_case "core: float in src"  0 "src/spu94/ok_float.c"   'float x;'
# CASE 3: 'malloc' in core include -> exit 1.
run_case "core: malloc in include" 1 "include/spu94/bad.h" 'void *p = malloc(1);'
# CASE 4: 'long long' in core -- allowed -> exit 0.
run_case "core: long long allowed" 0 "src/spu94/ok.c" 'long long ll;'
# CASE 5: unqualified 'long' in core -> exit 1.
run_case "core: unqualified long forbidden" 1 "src/spu94/bad2.c" 'long n;'
# CASE 6: empty tree -> exit 0 (nothing to scan).
(
    tmp=$(mktemp -d)
    cd "$tmp"
    if "$GUARD" >/dev/null 2>&1; then
        echo "PASS: empty tree (exit 0)"
        rc=0
    else
        rc=$?
        echo "FAIL: empty tree (expected exit 0, got $rc)"
    fi
    rm -rf "$tmp"
    exit $rc
) || fail=1
# CASE 7: KNOWN LIMITATION pin -- mixed long long + unqualified long on one line (core) -> exit 0.
# If a future contributor tightens grep-guard to per-token matching, this fails and
# forces an intentional review. See KNOWN LIMITATION block in grep-guard.sh.
run_case "core: known limitation (mixed long long + long on one line)" 0 "src/spu94/edge.c" 'long long ll; long n;'

# --- CLI-tier cases (src/cli) ---
# CASE 8: 'malloc' in CLI -> allowed -> exit 0.
run_case "cli: malloc allowed (userland work_buf + dr_wav)" 0 "src/cli/ok.c" 'void *p = malloc(1); free(p);'
# CASE 9: 'float' in CLI -> exit 0 (CLI tier no longer enforced).
run_case "cli: float allowed (boundary parsing)" 0 "src/cli/ok_float.c" 'float x;'
# CASE 10: unqualified 'long' in CLI -> allowed (stdlib boundary: strtol, ftell).
run_case "cli: unqualified long allowed (stdlib boundary)" 0 "src/cli/ok2.c" 'long fs = 0;'
# CASE 11: 'double' in CLI -> exit 0 (CLI tier no longer enforced).
run_case "cli: double allowed (boundary parsing)" 0 "src/cli/ok_double.c" 'double d;'

if [ "$fail" -ne 0 ]; then
    echo
    echo "test-grep-guard FAILED -- guard semantics are broken."
    exit 1
fi

echo "test-grep-guard: OK (all 11 fixture cases passed across both tiers)."
exit 0
