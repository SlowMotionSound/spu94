#!/usr/bin/env bash
# tests/rt_safety/test_no_syscalls.sh -- Phase 5 Plan 04, D-09c.
#
# Runs test_no_syscalls under strace; parses the log to isolate the
# steady-state region between the two SIGUSR1 markers; asserts zero
# non-signal syscalls in that region. Init / teardown / signal-return
# frames are excluded -- only "real work" in the spu94_process loop
# counts toward the verdict.
#
# Parser tolerance:
#   SIGUSR1 delivery lines match `--- SIGUSR1` (strace 4.x+ format).
#   rt_sigreturn occurrences inside the window are subtracted (they are
#   the signal-return frame exits, not hot-path work).
#   gettid/getpid/tgkill occurrences are subtracted too: these are the
#   glibc raise(SIGUSR1) implementation calls for the END marker. The
#   three syscalls inherently land AFTER the 10^5-iter loop completes
#   but BEFORE the "--- SIGUSR1 ---" line that strace emits on signal
#   delivery, so they fall inside the [START+1, END-1] steady-state
#   window. They are part of the marker scaffolding, not DSP work.
#   Since no legitimate spu94_process hot-path calls these three, this
#   filter remains tight -- any new per-call DSP syscall (futex,
#   read, clock_gettime, etc.) would still surface.
#
# Expected verified on: strace 6.16 (Linux).
set -euo pipefail

: "${SYSCALLS_BIN:?SYSCALLS_BIN env var required}"
: "${STRACE_EXE:?STRACE_EXE env var required}"

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

# -f: follow child processes (no children expected but defensive)
# -ttt: timestamps (helps post-debugging if test flakes)
# -o: log to file
"$STRACE_EXE" -f -ttt -o "$LOG" "$SYSCALLS_BIN" || {
    echo "FAIL: test_no_syscalls binary exited non-zero" >&2
    exit 1
}

# Find the two SIGUSR1 delivery lines.
# strace format: "<pid>  <timestamp> --- SIGUSR1 {si_signo=SIGUSR1, ...} ---"
MARKER_LINES=$(grep -n '\-\-\- SIGUSR1' "$LOG" | cut -d: -f1)
MARKER_COUNT=$(echo "$MARKER_LINES" | grep -c '^[0-9]\+$' || true)
if [ "$MARKER_COUNT" -ne 2 ]; then
    echo "FAIL: expected 2 SIGUSR1 markers, got $MARKER_COUNT" >&2
    echo "strace log excerpt:" >&2
    head -20 "$LOG" >&2
    exit 1
fi

START=$(echo "$MARKER_LINES" | head -1)
END=$(echo "$MARKER_LINES"   | tail -1)

# Count syscall invocations between the markers.
# Real syscall lines look like: "<pid>  <ts> <name>(<args>...) = <rv>"
# Signal lines look like:       "<pid>  <ts> --- <SIG> {...} ---"
# rt_sigreturn is the signal frame exit and does not count.
# gettid/getpid/tgkill are the glibc raise() implementation calls for the
# END marker -- subtracted as marker scaffolding.
STEADY_LINES=$(sed -n "$((START+1)),$((END-1))p" "$LOG")
STEADY_SYSCALLS=$(echo "$STEADY_LINES" | \
    grep -cE '^[0-9]+\s+[0-9.]+\s+[a-z_][a-z0-9_]*\(' || true)

# Scaffolding syscalls to exclude from the net count. These are NOT
# legitimate DSP hot-path syscalls -- they come from either glibc's
# signal-return frame (rt_sigreturn) or from raise()'s implementation
# for the END marker (gettid + getpid + tgkill).
SCAFFOLD_PATTERN='^[0-9]+\s+[0-9.]+\s+(rt_sigreturn|gettid|getpid|tgkill)\('
SCAFFOLD_COUNT=$(echo "$STEADY_LINES" | grep -cE "$SCAFFOLD_PATTERN" || true)
NET_SYSCALLS=$((STEADY_SYSCALLS - SCAFFOLD_COUNT))

if [ "$NET_SYSCALLS" -ne 0 ]; then
    echo "FAIL: $NET_SYSCALLS non-scaffolding syscalls in steady-state region" >&2
    echo "--- First 20 non-scaffolding syscall lines: ---" >&2
    echo "$STEADY_LINES" | grep -E '^[0-9]+\s+[0-9.]+\s+[a-z_][a-z0-9_]*\(' | \
        grep -vE "$SCAFFOLD_PATTERN" | head -20 >&2
    exit 1
fi

echo "PASS: zero syscalls in 10^5-iter spu94_process steady-state loop"
echo "      (scaffolding-only: $SCAFFOLD_COUNT rt_sigreturn+raise() marker calls)"
exit 0
