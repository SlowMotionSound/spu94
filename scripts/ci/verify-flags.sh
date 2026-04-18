#!/usr/bin/env bash
# scripts/ci/verify-flags.sh
# BUILD-02: assert -ffp-contract=off, -fno-fast-math, -Werror are present on every
# core TU compile command in build/compile_commands.json.
# Requires jq for robust JSON parsing (ubuntu-latest ships it; apt-get on dev machines).
#
# Exit codes: 0 = all flags present on every core TU, 1 = missing, 2 = environment.

set -euo pipefail

CDB="${1:-build/compile_commands.json}"
REQUIRED=('-ffp-contract=off' '-fno-fast-math' '-Werror')

if [ ! -f "$CDB" ]; then
    echo "ERROR [verify-flags]: compile commands DB not found at $CDB"
    echo "Did you run 'cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON' first?"
    exit 2
fi

if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR [verify-flags]: jq not installed. Install with: sudo apt-get install -y jq"
    exit 2
fi

# Extract (file, command) pairs for core translation units only (src/spu94/*.c).
# For each, assert every REQUIRED flag appears as a standalone token in the command.
fail=0
while IFS=$'\t' read -r src_file cmd; do
    for flag in "${REQUIRED[@]}"; do
        # Check the flag appears as a space-delimited token (not embedded in a longer word).
        if ! printf '%s' "$cmd" | grep -qE "(^| )${flag}( |\$)"; then
            echo "ERROR [verify-flags]: $src_file missing required flag: $flag"
            fail=1
        fi
    done
done < <(jq -r '.[] | select(.file | test("src/spu94/.*\\.c$")) | [.file, .command] | @tsv' "$CDB")

if [ "$fail" -ne 0 ]; then
    echo
    echo "verify-flags FAILED. A core TU is missing a BUILD-02 determinism flag."
    echo "Check cmake/spu94_warnings.cmake and src/spu94/CMakeLists.txt."
    exit 1
fi

# Also assert at least one core TU was inspected (catches the "empty scan" false pass).
core_count=$(jq -r '.[] | select(.file | test("src/spu94/.*\\.c$")) | .file' "$CDB" | wc -l)
if [ "$core_count" -eq 0 ]; then
    echo "ERROR [verify-flags]: 0 core TUs found in $CDB -- scan matched nothing."
    echo "Expected at least src/spu94/spu94_placeholder.c (Phase 1 baseline)."
    exit 1
fi

echo "verify-flags: OK (inspected $core_count core TUs, all flags present)."
exit 0
