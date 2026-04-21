#!/usr/bin/env bash
# tests/rt_safety/verify-no-locks.sh -- Phase 5 Plan 04, D-09b.
#
# Asserts no pthread / sem / futex lock symbols are undefined-referenced
# by libspu94.so or by the Phase-5-public-symbol-exercising linksym binary.
# A lock primitive reachable from spu94_process would break API-08's
# no-locks guarantee and the DSP core's real-time-safety discipline
# (PROJECT.md constraint).
#
# Expanded symbol list (per 05-RESEARCH.md § D-09b):
#   pthread_mutex_{init,destroy,lock,unlock,trylock,timedlock}
#   pthread_rwlock_*, pthread_cond_*, pthread_spin_*, pthread_barrier_*
#   sem_{init,destroy,wait,trywait,timedwait,post,close,open,unlink}
#   futex
#
# Exit: 0 on success; 1 on any lock-symbol hit.
set -euo pipefail

: "${SPU94_LIB:?SPU94_LIB env var required}"
: "${PHASE5_BIN:?PHASE5_BIN env var required}"

LOCK_PATTERN='^\s*U\s+(pthread_mutex_(init|destroy|lock|unlock|trylock|timedlock)|pthread_rwlock_[a-z_]+|pthread_cond_[a-z_]+|pthread_spin_[a-z_]+|pthread_barrier_[a-z_]+|sem_(init|destroy|wait|trywait|timedwait|post|close|open|unlink)|futex)(\s|@|$)'

fail=0
for BIN in "$SPU94_LIB" "$PHASE5_BIN"; do
    if nm -u "$BIN" | grep -qE "$LOCK_PATTERN"; then
        echo "FAIL: lock symbols referenced by $BIN:" >&2
        nm -u "$BIN" | grep -E "$LOCK_PATTERN" >&2
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "PASS: no pthread/sem/futex lock symbols referenced"
fi
exit "$fail"
