#!/usr/bin/env bash
# scripts/ci/test-grep-guard.sh
# Tests scripts/ci/grep-guard.sh on positive + negative fixtures.
# Run AFTER grep-guard.sh passes on the real tree (so we know the current tree is clean).
# This script creates a tempdir, seeds fixture files, invokes grep-guard.sh against it,
# and asserts the expected outcomes.
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

# CASE 1: clean tree (only a trivial placeholder) -> exit 0.
run_case "clean tree" 0 "src/a.c"        '/* clean */'$'\n''#include <stdint.h>'$'\n''int32_t x;'
# CASE 2: 'float' in a src file -> exit 1.
run_case "float in src"  1 "src/bad.c"   'float x;'
# CASE 3: 'malloc' in include -> exit 1.
run_case "malloc in include" 1 "include/bad.h" 'void *p = malloc(1);'
# CASE 4: 'long long' -- allowed -> exit 0.
run_case "long long allowed" 0 "src/ok.c" 'long long ll;'
# CASE 5: unqualified 'long' -> exit 1.
run_case "unqualified long forbidden" 1 "src/bad2.c" 'long n;'
# CASE 6: no src/ or include/ dirs -> exit 0 (nothing to scan).
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

if [ "$fail" -ne 0 ]; then
    echo
    echo "test-grep-guard FAILED -- guard semantics are broken."
    exit 1
fi

echo "test-grep-guard: OK (all fixture cases passed)."
exit 0
