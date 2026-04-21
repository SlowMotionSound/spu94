#!/usr/bin/env bash
# tests/rt_safety/test_no_heap.sh -- Phase 5 Plan 04, D-09a.
#
# Asserts no heap-allocator symbols are undefined-referenced by
# libspu94.so or by the Phase-5-public-symbol-exercising linksym binary.
#
# Pattern: extends scripts/ci/verify-no-heap-symbols.sh (Phase 1) with
# the Phase 5 binary's static-link closure. A reachable-from-Phase-5
# helper that accidentally references malloc would surface here even if
# it doesn't show up in libspu94.so's dynamic undefined-symbol list.
#
# Heap-allocator list (superset of Phase 1's list; adds aligned_alloc +
# posix_memalign for completeness per D-09a):
#   malloc, calloc, realloc, free, aligned_alloc, posix_memalign
#
# Exit: 0 on success; 1 on any heap-symbol hit.
set -euo pipefail

: "${SPU94_LIB:?SPU94_LIB env var required (set by CMake add_test)}"
: "${PHASE5_BIN:?PHASE5_BIN env var required (set by CMake add_test)}"

HEAP_PATTERN='^\s*U\s+(malloc|calloc|realloc|free|aligned_alloc|posix_memalign)(\s|@|$)'

fail=0

# 1. libspu94.so itself -- dynamic undefined-symbol audit.
if nm -u "$SPU94_LIB" | grep -qE "$HEAP_PATTERN"; then
    echo "FAIL: heap symbols referenced by $SPU94_LIB:" >&2
    nm -u "$SPU94_LIB" | grep -E "$HEAP_PATTERN" >&2
    fail=1
fi

# 2. Phase 5 public-API link closure (via test_phase5_linksym).
# The binary references spu94_process + spu94_flush + spu94_load_preset;
# static-linking pulls all Phase 5 code paths into the closure. If a Phase 5
# TU accidentally introduces a malloc-referring helper reachable only from
# those symbols, it shows up here but not in libspu94.so's shared-lib dynamic
# undefined-symbol list.
if nm -u "$PHASE5_BIN" | grep -qE "$HEAP_PATTERN"; then
    echo "FAIL: heap symbols referenced by $PHASE5_BIN:" >&2
    nm -u "$PHASE5_BIN" | grep -E "$HEAP_PATTERN" >&2
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "PASS: no heap symbols referenced by libspu94.so or Phase 5 linksym binary"
fi
exit "$fail"
