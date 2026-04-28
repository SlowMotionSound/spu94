---
phase: 02-pipeline-integration
reviewed: 2026-04-26T21:45:00Z
depth: standard
files_reviewed: 6
files_reviewed_list:
  - include/spu94/spu94.h
  - src/spu94/spu94_state_internal.h
  - src/spu94/spu94_process.c
  - src/spu94/spu94_io_chain.c
  - tests/unit/process/test_process_adpcm.c
  - tests/unit/process/CMakeLists.txt
findings:
  critical: 0
  warning: 2
  info: 1
  total: 3
status: issues_found
---

# Phase 02-pipeline-integration: Code Review Report

**Reviewed:** 2026-04-26T21:45:00Z
**Depth:** standard
**Files Reviewed:** 6
**Status:** issues_found

## Summary

The ADPCM pipeline integration into `spu94_process` is well-structured. The double-buffer strategy (accumulate input into one buffer, emit from a previously decoded buffer) correctly introduces exactly 28 samples of latency. The shared `spu94_adpcm_state` between encode and decode is sound because the encoder's internal reconstruction mirrors the decoder exactly (ADPCM-05 invariant verified by tracing both code paths). The disable-cleanup path in `spu94_set_adpcm_enabled` correctly zeros output buffers, codec state, and buffer position.

The test suite covers the key contracts (default-off, toggle, NULL safety, latency reporting, state init/reset, mid-stream toggle). No critical issues found. Two warnings and one informational item are noted below.

## Warnings

### WR-01: Stale latency value in assembly-example comment

**File:** `src/spu94/spu94_io_chain.c:143`
**Issue:** The comment reads "mov eax, 38; ret" as an example of the LTO-eliminated constant, but `SPU94_LATENCY_SAMPLES` was corrected from 38u to 58u in Phase 4 Plan 03. The stale value could mislead future readers into thinking the latency is 38, contradicting the macro definition at `include/spu94/spu94.h:208`.
**Fix:**
```c
/* Value from 04-RESEARCH section Latency. One-line definition -- LTO
 * makes consumer call sites emit a constant return (e.g., mov eax, 58; ret). */
```

### WR-02: ADPCM input buffers not zeroed on disable -- stale data persists

**File:** `src/spu94/spu94_io_chain.c:151-164`
**Issue:** When ADPCM is disabled, `adpcm_out_buf_l/r`, `adpcm_buf_pos`, and codec state are all zeroed, but `adpcm_in_buf_l/r` retain stale data from the partial block that was being accumulated. While this is not a correctness bug today (re-enabling resets `buf_pos` to 0 so new input overwrites from index 0 before any encode is triggered), it leaves stale audio data in the struct. If a future change introduces any path that reads the in-buffer at indices beyond buf_pos (e.g., debug dump, fuzz harness), it would observe stale audio. Zeroing the in-buffers alongside the out-buffers costs negligible cycles and makes the disable path unconditionally clean.
**Fix:**
```c
if (!enabled && state->adpcm_enabled) {
    state->adpcm_buf_pos = 0;
    for (int j = 0; j < SPU94_ADPCM_BLOCK_SAMPLES; j++) {
        state->adpcm_in_buf_l[j] = 0;   /* added */
        state->adpcm_in_buf_r[j] = 0;   /* added */
        state->adpcm_out_buf_l[j] = 0;
        state->adpcm_out_buf_r[j] = 0;
    }
    state->adpcm_state_l.old = 0;
    state->adpcm_state_l.older = 0;
    state->adpcm_state_r.old = 0;
    state->adpcm_state_r.older = 0;
}
```

## Info

### IN-01: Test 5 relies on reverb chain producing nonzero output to prove ADPCM wiring

**File:** `tests/unit/process/test_process_adpcm.c:143-194`
**Issue:** `test_adpcm_enabled_differs_from_disabled` loads the Hall preset and feeds a pseudo-random signal, asserting that ADPCM-on and ADPCM-off outputs differ. This test has an implicit dependency on the reverb chain producing audible output (non-zero vLOUT/vROUT, sufficient reverb buildup within 512 samples). If a future preset-table change zeros the Hall output gains, this test would silently pass vacuously (both paths produce silence, `differ` stays 0, and the assertion fires). The test is correct today, but adding a guard assertion that the ADPCM-off output contains at least some nonzero samples would make the test self-diagnosing.
**Fix:** Add a guard before the difference check:
```c
/* Guard: ADPCM-off output must be nonzero (reverb actually produced audio) */
int has_signal = 0;
for (uint32_t i = 0; i < N; i++) {
    if (out_off_l[i] != 0 || out_off_r[i] != 0) { has_signal = 1; break; }
}
TEST_ASSERT_TRUE_MESSAGE(has_signal,
    "Hall preset must produce nonzero output for this test to be meaningful");
```

---

_Reviewed: 2026-04-26T21:45:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
