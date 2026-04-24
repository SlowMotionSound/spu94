#!/usr/bin/env bash
# tests/rt_safety/hotpath_alloc_gate.sh -- BUILD-06 D-20 hard gate.
#
# Runs $SYSCALLS_BIN under strace with a NARROW heap-syscall filter
# (brk, mmap, mmap2, munmap, mremap) and asserts zero hits between
# the two SIGUSR1 markers raised by the target. Extends the Phase 5
# test_no_syscalls pattern (broad zero-syscall check on the rt-safety
# path) with a targeted filter so Phase 7 gets an independently
# diagnosable heap-allocation gate.
#
# mmap2 is included belt-and-suspenders per RESEARCH.md Pitfall 5:
# strictly a 32-bit-linux syscall; M1 targets are all 64-bit Linux so
# this is a no-op there, but keeps the filter robust if a future 32-bit
# CI runner joins the matrix.
#
# Exit 0 = zero heap syscalls in steady state  (PASS, CI green).
# Exit 1 = at least one heap syscall in steady state OR parser
#          anomaly (missing markers, non-zero target exit) (FAIL,
#          CI hard red).
#
# Parser structure intentionally mirrors Phase 5's test_no_syscalls.sh
# so someone reading either script can translate between them.
# Expected strace version: 4.x+ (tested on strace 6.16 on dev host).
#
# Env contract:
#   SYSCALLS_BIN  Absolute path to the target binary to run under strace.
#                 Required. ctest passes $<TARGET_FILE:...>.
#   STRACE_EXE    Path to strace executable. Defaults to "strace" on $PATH.

set -euo pipefail

: "${SYSCALLS_BIN:?SYSCALLS_BIN env var required}"
: "${STRACE_EXE:=strace}"

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

# -f        : follow child processes (defensive; target does not fork)
# -e trace=... : narrow filter to just the heap-mutation syscalls
# -o LOG    : strace writes to LOG (separate from target stdout/stderr)
#
# Disambiguate failure modes. `set -e` is already in effect from line 30,
# but we need the return code of the strace invocation without triggering
# -e exit, so wrap in `|| rc=$?` and branch on log-file state:
#   (a) rc != 0 AND log is empty/absent → strace itself failed (not on
#       PATH, YAMA ptrace_scope=1, no CAP_SYS_PTRACE, seccomp denial,
#       inaccessible log path). Distinct from target-exit failure.
#   (b) rc != 0 AND log has content → strace ran but the target exited
#       non-zero. Print log head so the target crash is legible.
#   (c) rc == 0 → fall through to marker parsing.
# Preserves WILL_FAIL semantics for hotpath_alloc_gate_negative: the
# poisoned target exits 0 but the heap-hit check further down raises
# exit 1, which ctest translates to "expected failure".
rc=0
"$STRACE_EXE" -f -e trace=brk,mmap,mmap2,munmap,mremap \
    -o "$LOG" "$SYSCALLS_BIN" || rc=$?
if [ "$rc" -ne 0 ]; then
    if [ ! -s "$LOG" ]; then
        echo "FAIL: strace failed to produce a log (rc=$rc)" >&2
        echo "  possible causes: strace missing, YAMA ptrace_scope blocks" \
             "attach, no CAP_SYS_PTRACE, seccomp denial, or unwritable" \
             "log path ($LOG). Containerized CI often requires" \
             "--cap-add=SYS_PTRACE or --security-opt seccomp=unconfined." >&2
        exit 1
    fi
    echo "FAIL: target binary exited non-zero (rc=$rc)" >&2
    head -20 "$LOG" >&2 || true
    exit 1
fi

# Locate the two SIGUSR1 delivery lines the target raised.
MARKER_LINES=$(grep -n '\-\-\- SIGUSR1' "$LOG" | cut -d: -f1 || true)
MARKER_COUNT=$(echo "$MARKER_LINES" | grep -c '^[0-9]\+$' || true)
if [ "$MARKER_COUNT" -ne 2 ]; then
    echo "FAIL: expected 2 SIGUSR1 markers, got $MARKER_COUNT" >&2
    head -20 "$LOG" >&2
    exit 1
fi

START=$(echo "$MARKER_LINES" | head -1)
END=$(echo   "$MARKER_LINES" | tail -1)

# Count heap syscalls in the strictly-interior window [START+1, END-1].
# Two matchers are tried in sequence so the parser tolerates both strace
# line layouts we might see:
#   -f layout:    "<pid>  <name>(...) = <rv>"
#   no-f layout:  "<name>(...) = <rv>"
STEADY=$(sed -n "$((START+1)),$((END-1))p" "$LOG")

HEAP_HITS=$(echo "$STEADY" | \
    grep -cE '^[0-9]+[[:space:]]+(brk|mmap|mmap2|munmap|mremap)\(' \
      2>/dev/null || true)
if [ -z "$HEAP_HITS" ] || [ "$HEAP_HITS" = "0" ]; then
    HEAP_HITS=$(echo "$STEADY" | \
        grep -cE '^(brk|mmap|mmap2|munmap|mremap)\(' 2>/dev/null || true)
fi
HEAP_HITS=${HEAP_HITS:-0}

if [ "$HEAP_HITS" -ne 0 ]; then
    echo "FAIL: $HEAP_HITS heap syscall(s) in spu94_process steady state" >&2
    echo "--- First 20 heap-syscall lines inside marker window: ---" >&2
    echo "$STEADY" | grep -E '(brk|mmap|mmap2|munmap|mremap)\(' | head -20 >&2
    exit 1
fi

echo "PASS: zero heap syscalls in spu94_process hot path"
exit 0
