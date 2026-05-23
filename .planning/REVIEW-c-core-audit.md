# C Core DSP Engine Audit: SPU-94

**Reviewed:** 2026-05-07
**Scope:** All headers (8 files) and source (27 files) in include/spu94/ and src/spu94/
**Reviewer:** Claude (deep audit)

---

## Summary

The C core is remarkably clean for a codebase that has been through 19+ phases of iterative development. The Q15 fixed-point arithmetic, register I/O layer, reverb network, FIR chains, and ADPCM codecs are well-structured and thoroughly documented. The main findings fall into four categories: (1) one correctness bug in slew cancel, (2) dead code from abandoned approaches, (3) stale comments that describe earlier states of the code, and (4) a vestigial unused function parameter. No security issues found -- the library is zero-heap, zero-syscall, and validates all inputs.

---

## BUG

### BUG-01: `spu94_slew_cancel` only snaps U16 registers, leaves I16 fractional positions stale

**File:** `src/spu94/spu94_slew.c:76-85`
**Severity:** BUG

The cancel function snaps `slew_frac[]` to current integer values for U16 registers only. I16 (v-prefix gain) registers are skipped entirely. After cancel, if `spu94_set_slew_targets` is called again, the I16 registers' fractional positions still hold the old mid-slew value (from line 31: `cur_f = state->slew_frac[r]` when `slew_active && slew_abs_delta[r] > 0` -- but `slew_active` is now 0 so it falls through to `(float)state->reg_values[r]`).

Wait -- re-reading the logic: after cancel, `slew_active = 0`, so the next `spu94_set_slew_targets` call at line 30-31 takes the `else` branch (`cur_f = (float)state->reg_values[r]`), so the stale `slew_frac[r]` is not actually read. The bug is more subtle: during the NEXT slew, the `get_reg_frac` helper in `spu94_reverb.c:115-123` reads `slew_frac[r]` when `slew_active && slew_abs_delta[r] > 0`. But after cancel, `slew_active = 0`, so `get_reg_frac` falls through to the integer path. And when a NEW slew starts, `spu94_set_slew_targets` re-initializes `slew_frac[r]` at line 42.

On closer analysis, this is NOT a runtime bug because both `get_reg_frac` and `set_slew_targets` guard on `slew_active`. However, it IS an asymmetry that makes the code fragile: U16 registers get their frac snapped defensively while I16 registers don't. If any future code reads `slew_frac` without checking `slew_active` first, the stale I16 values would cause incorrect behavior.

**Fix:**
```c
void spu94_slew_cancel(spu94_state *state) {
    if (!state) return;
    state->slew_active = 0;
    state->slew_max_delta = 0;
    /* Snap ALL fractional positions to current integer values */
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
            state->slew_frac[r] = (float)state->reg_values[r];
        else
            state->slew_frac[r] = (float)(uint16_t)state->reg_values[r];
    }
}
```

**Reclassified:** WARNING (not a live bug today, but a maintenance trap)

---

### BUG-02: `spu94_set_slip_attack/release/hold/threshold/depth` lack range clamping

**File:** `src/spu94/spu94_error_accum.c:33-47`
**Severity:** WARNING

The public header documents these as "All values 0.0-1.0" but the setter implementations do no clamping -- unlike `spu94_set_memory_slip`, `spu94_set_clock_drift`, and `spu94_set_sh_slew` which all explicitly clamp to [0.0, 1.0]. Passing values outside [0.0, 1.0] to the envelope followers in `spu94_reverb_body` could cause:
- Negative attack coefficients (line 754 in reverb.c: `0.01f + atk * 0.99f` goes negative if `atk < -0.0101...`)
- Release multiplier > 1.0 (line 750: `0.99f + rel * 0.0099f` exceeds 1.0 if `rel > 1.01...`), making the envelope grow unbounded

These would produce audio anomalies but not crashes.

**Fix:** Add clamping to each setter:
```c
void spu94_set_slip_attack(spu94_state *state, float v) {
    if (!state) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    state->slip_attack = v;
}
// ... same for release, hold, threshold, depth
```

---

### BUG-03: `spu94_slew_tick` does not null-check state

**File:** `src/spu94/spu94_slew.c:100`
**Severity:** WARNING

`spu94_slew_tick` dereferences `state->slew_active` on line 101 without a NULL check. It's called from `spu94_tick` (which does NULL-check at line 44 of spu94_tick.c), so in practice the NULL state is already guarded. But this is inconsistent with the null-safety convention used everywhere else in the codebase (every other function that touches state checks `!state` first). If the call site ever changes, this becomes a null-pointer dereference.

**Fix:**
```c
void spu94_slew_tick(spu94_state *state) {
    if (!state || !state->slew_active) return;
    // ...
}
```

---

## DEAD_CODE

### DEAD-01: `spu94_dac_noise_step_8x` is declared, defined, but never called

**File:** `src/spu94/spu94_dac_noise.c:89-101` (definition), `include/spu94/spu94_dac_noise.h:53` (declaration)
**Severity:** DEAD_CODE

The function `spu94_dac_noise_step_8x` was created for Phase 11's 352.8kHz noise injection path. However, `spu94_dac_fir_step_8x_with_noise` was revised to call `spu94_dac_noise_step` (the 44.1kHz version) instead, as documented by the comment at `spu94_dac_fir.c:338-341`. The `_8x` variant is never called from any production code, test code, or Python binding. It has zero callers anywhere in the project.

Additionally, the comment block at `spu94_dac_fir.c:30-47` still references `spu94_dac_noise_step_8x` and describes its `SHIFT_8X=10` calibration as if it were in use, even though it isn't.

**Fix:** Remove `spu94_dac_noise_step_8x` from source, header, and the stale comment block in spu94_dac_fir.c. Also remove the `DAC_NOISE_SHIFT_8X` constant from spu94_dac_noise.c if no other code references it.

---

### DEAD-02: `frac_offset` parameter in `feedback_tap_read` is accepted but never used

**File:** `src/spu94/spu94_reverb.c:135-168`
**Severity:** DEAD_CODE

The `frac_offset` parameter is declared on line 137, immediately silenced with `(void)frac_offset;` on line 142, and never referenced in the function body. This parameter was needed when feedback taps used fractional reads, but the "Phase 18 morph-crackle fix" switched feedback taps to integer-only reads. Now every call site (6 locations across same_iir, diff_iir, apf1, apf2) computes and passes a `frac_offset` value that is discarded.

**Fix:** Remove the `frac_offset` parameter from `feedback_tap_read` and remove the computation at each of the 6 call sites (e.g., line 324's `mLSAME_f - 2.0f`).

---

### DEAD-03: `SPU94_FIR_CASCADE_CLAMP` ifdef block is test-only dead code in production

**File:** `src/spu94/spu94_fir.c:103-134`
**Severity:** DEAD_CODE

The `#ifdef SPU94_FIR_CASCADE_CLAMP` block (lines 103-134) is documented as "Test binary may build with this defined; production never does." This is 30+ lines of commented, guarded code that exists solely as a development artifact. The production path always takes the `#else` branch.

**Fix:** Consider extracting this to a test-only file if it's still used in test builds, or remove it if the test suite no longer exercises it.

---

### DEAD-04: Comment block at spu94_reverb.c:275-287 describes stubs that no longer exist

**File:** `src/spu94/spu94_reverb.c:275-287`
**Severity:** STALE

This 12-line comment block describes "Plan 02 / Plan 03 stage stubs" that are "intentionally a no-op: no state mutation, no buffer I/O." But the actual implementations for same_iir, diff_iir, comb, apf1, and apf2 follow immediately after. The stubs were replaced by real implementations but the comment header was never removed.

**Fix:** Delete lines 275-287.

---

## STALE

### STALE-01: Header comment for `spu94_dac_fir_step_8x_with_noise` says noise is injected at 352.8kHz

**File:** `include/spu94/spu94_dac_fir.h:49-56`
**Severity:** STALE

The header documentation says: "Process one 44.1kHz Q15 sample through the 8x cascade with noise injection at 352.8kHz (Phase 11, DSP-05). Noise is added to each of the 8 Stage 3 evaluations before decimation."

But the actual implementation (`spu94_dac_fir.c:342`) calls `spu94_dac_noise_step(noise)` once at the output rate (44.1kHz) and adds it to the decimated sum. Noise is NOT injected at 352.8kHz into each Stage 3 evaluation. The implementation was revised but the header docs were not updated.

**Fix:** Update the header comment to: "Noise is added once at the 44.1kHz output rate after decimation of the 8 Stage 3 evaluations."

---

### STALE-02: `spu94_dac_fir.c` lines 30-47 describe obsolete 352.8kHz noise injection calibration

**File:** `src/spu94/spu94_dac_fir.c:30-47`
**Severity:** STALE

This 17-line comment block describes "post-decimation noise scaling for 352.8kHz injection" using `spu94_dac_noise_step_8x` with `SHIFT_8X=10`, explains a scaling constant and right-shift by 8 bits, and references tests/unit/dac_noise/test_dac_noise_8x.c. None of this math applies to the current implementation which simply calls `spu94_dac_noise_step` and does a straight int32 add.

**Fix:** Replace with a brief comment explaining the current behavior.

---

### STALE-03: `spu94_registers.c` comment says "Snapshot stub body lives here too; Plan 03 will rewrite it"

**File:** `src/spu94/spu94_registers.c:9-10`
**Severity:** STALE

The file-level comment says "Snapshot stub body lives here too; Plan 03 will rewrite it to read the `reg_values[]` storage Plan 01 already reserved in struct spu94_state." Plan 03 was completed long ago -- the actual implementation at line 126 reads `reg_values[]`. The comment is historical cruft.

**Fix:** Remove the "Plan 03 will rewrite it" sentence.

---

### STALE-04: Multiple comment references to future plans as if they haven't shipped yet

**File:** Multiple files
**Severity:** STALE (cosmetic)

Several comments reference future plans/phases in the present/future tense when they've already shipped:
- `spu94_io_chain.c:42-44`: "Phase 5 will additionally plumb mix-bus inputs..."
- `spu94_io_chain.c:9`: "Phase 5's public spu94_process composes the non-bypass chain..."
- `spu94_buffer.c:8-9`: "called once per stereo tick from spu94_tick AFTER spu94_apply_pending_writes (Pitfall 4: still exactly one call site each)" -- the parenthetical implies ongoing monitoring
- `spu94.h:163`: "Plan 02 ships a no-op stub body" -- spu94_tick is fully implemented
- `spu94_fir_internal.h:19-20`: "Plan 01 (this plan) lands declarations" / "Plan 02 lands the folded-form..."

These are cosmetic and don't affect correctness, but they make the codebase read as though it's perpetually under construction. A batch find-replace changing "Plan XX will" to "Plan XX" (past tense) would clean this up. Not individually actionable.

---

### STALE-05: `spu94_dac_noise.h` header comment references `spu94_dac_fir_step_8x_with_noise` as calling `step_8x` at each Stage 3 eval

**File:** `include/spu94/spu94_dac_noise.h:8-11`
**Severity:** STALE

The header says: "spu94_dac_noise_step_8x() generates noise at the 352.8kHz oversampled rate... Called by spu94_dac_fir_step_8x_with_noise at each Stage 3 evaluation." This is no longer true -- `step_8x_with_noise` calls `spu94_dac_noise_step` once, not `step_8x` eight times.

**Fix:** Update or remove this comment to match the current code path.

---

## REDUNDANT

### REDUND-01: Stages 1 and 2 of `spu94_dac_fir_step_8x_with_noise` are copy-pasted from `spu94_dac_fir_step_8x`

**File:** `src/spu94/spu94_dac_fir.c:273-344` vs `src/spu94/spu94_dac_fir.c:206-271`
**Severity:** REDUNDANT

The Stage 1 and Stage 2 code blocks (push+evaluate for 2 and 4 sub-evaluations respectively) are byte-for-byte identical between `spu94_dac_fir_step_8x` and `spu94_dac_fir_step_8x_with_noise`, differing only in the variable name `state` vs `fir`. ~50 lines duplicated. Stage 3 also shares the same loop structure, differing only in what happens to the accumulated output.

**Fix:** Extract the Stage 1+2 computation into a shared static helper that returns the `s2[4]` intermediate array, then have both functions call it and diverge only for Stage 3 + output.

---

### REDUND-02: `same_iir` and `diff_iir` are structurally identical with only cross-side tap difference

**File:** `src/spu94/spu94_reverb.c:311-376` and `395-458`
**Severity:** REDUNDANT (cosmetic, acceptable)

The L and R sides of `same_iir` (lines 317-347 and 349-375) are nearly identical code blocks, as are both sides of `diff_iir`. The ONLY difference between `same_iir` and `diff_iir` is which d-register feeds the wall tap (same-side vs cross-side). Similarly, `apf1` and `apf2` are byte-identical except for register names.

This is noted but NOT recommended to refactor -- the explicit per-stage code makes it trivially auditable against the nocash pseudocode, which is the stated project goal. Deduplication would help maintainability but hurt auditability.

---

### REDUND-03: `spu94_dac_fir_step` (v1.2 single-rate) duplicates the same push+apply pattern as the 8x variant

**File:** `src/spu94/spu94_dac_fir.c:158-187`
**Severity:** REDUNDANT (minor, acceptable)

The v1.2 single-rate step function has a simpler straight-through cascade of push+apply for each stage. This code pattern is also present inside the 8x variant's Stage 1/2/3 loops. Given that v1.2 may be removed in the future, this duplication is tolerable.

---

## SIMPLIFY

### SIMPLIFY-01: `reverb_buf_read_interp` does float-to-int16 truncation without rounding

**File:** `src/spu94/spu94_reverb.c:98-112`
**Severity:** WARNING

Line 111: `return (int16_t)((float)a + frac * (float)(b - a));`

The `(int16_t)` cast truncates toward zero, not toward -infinity like the rest of the Q15 arithmetic. For negative interpolated values, this produces a +1 LSB bias compared to the ASR convention used everywhere else. Example: if `a = -100`, `b = -101`, `frac = 0.5`, result is `(int16_t)(-100.5f)` = -100 (truncates toward zero), whereas ASR-style would give -101.

Whether this matters depends on how you view the interpolation: it's a creative effect layer (morph transitions), not a bit-faithful PS1 operation. But the inconsistency with the project's ASR convention is worth noting.

**Fix (if desired):** Use `floorf` + cast:
```c
return (int16_t)(int32_t)floorf((float)a + frac * (float)(b - a));
```

---

### SIMPLIFY-02: `spu94_preset_io.c` includes `<stdio.h>` for `snprintf` only

**File:** `src/spu94/spu94_preset_io.c:16`
**Severity:** WARNING

The project enforces freestanding-C constraints and explicitly avoids hosted libc. `spu94_preset_io.c` is the ONLY source file in the C core that includes `<stdio.h>`, bringing in `snprintf`. The comment in `spu94_state.c` says "We do not include <string.h>; a hand-rolled loop keeps the library clear of libc imports (verify-no-heap-symbols.sh invariant)." But `spu94_preset_io.c` also includes `<string.h>` and `<stdio.h>`. If this file links into a freestanding build targeting MCU, `snprintf` won't be available.

This may be intentional (preset I/O is only used in hosted environments), but it's worth noting the discrepancy.

**Fix:** Document this file as hosted-only, or replace snprintf with hand-rolled formatting (as was done for hex parsing with `parse_hex_u16`).

---

### SIMPLIFY-03: `spu94_process.c` Clock Drift block at lines 52-67 writes registers directly, bypassing the engine layer

**File:** `src/spu94/spu94_process.c:52-67`
**Severity:** WARNING

The Clock Drift code directly writes `state->reg_values[r]`, `state->pending_values[r]`, and clears `state->pending_mask` bits -- bypassing `spu94_set_reg_i16/u16`. This skips:
1. The write-timing policy table dispatch
2. The mBASE snap-on-write side effect (if r == SPU94_REG_mBASE)
3. Type-safety validation

The same pattern appears in `spu94_slew_tick` (spu94_slew.c:107-113, 130-132), which is intentional for performance. But in the Clock Drift code, writing mBASE without triggering `spu94_mbase_on_write` could leave `buffer_address` out of sync if mBASE is slewing. In practice this likely doesn't occur (mBASE is fixed at 0x0000 in all presets), but it's an unsafe pattern.

**Fix:** At minimum, add a check: `if (r == SPU94_REG_mBASE) spu94_mbase_on_write(state, (uint16_t)int_val);` after the direct write. Or add a comment documenting why mBASE bypass is safe.

---

### SIMPLIFY-04: `spu94_interp_is_fixed` could be a lookup table instead of five comparisons

**File:** `src/spu94/spu94_interp.c:48-54`
**Severity:** SIMPLIFY (cosmetic)

Five == comparisons for something that could be a 35-bit bitmask checked in one AND operation. Not a correctness issue -- just noting for future cleanup if the function is called in a tight loop (it's inside a 35-iteration loop per morph call).

---

## INFO

### INFO-01: `spu94_dac_fir_test_stage_apply` is compiled into production but used only by tests

**File:** `src/spu94/spu94_dac_fir.c:355-379`
**Severity:** INFO

The test-visible per-stage apply wrapper is always compiled and linked into the production library (not behind any ifdef). Same pattern as `spu94_fir_decimate_literal_reference` and `spu94_fir_folded_reference` in spu94_fir.c. This is documented as intentional ("Always compiled -- Plan 03 body") but adds ~24 lines of dead code to the production binary.

---

### INFO-02: `spu94_preset_io.c` includes `<string.h>` while `spu94_state.c` explicitly avoids it

**File:** `src/spu94/spu94_preset_io.c:17` vs `src/spu94/spu94_state.c:8`
**Severity:** INFO

Inconsistent libc discipline. `spu94_state.c` has a comment explaining why it avoids `<string.h>` and uses a hand-rolled byte loop. `spu94_preset_io.c` freely uses `memcpy`, `memset`, `strcmp`, `strchr`, `strlen` from `<string.h>`. This is noted but acceptable if preset I/O is considered hosted-only code.

---

### INFO-03: Unused `<math.h>` include scope

**File:** `src/spu94/spu94_reverb.c:25`
**Severity:** INFO

`<math.h>` is included for `floorf()` used in `reverb_buf_read_interp` (line 101). This is the only `<math.h>` dependency in the entire C core. Like the `<stdio.h>` in preset_io, this constrains the reverb module to hosted environments with libm.

---

### INFO-04: `spu94_dac_noise_step` and `spu94_dac_noise_step_8x` duplicate the LFSR stepping code

**File:** `src/spu94/spu94_dac_noise.c:65-87` and `89-101`
**Severity:** INFO

The LFSR step (check bit, shift, conditionally XOR) is duplicated between the two functions. A shared `lfsr_step` helper could eliminate 5 lines. Minor since `step_8x` should be removed (DEAD-01).

---

_Reviewed: 2026-05-07_
_Reviewer: Claude (C core deep audit)_
_Files reviewed: 35 (8 headers + 27 source files)_
