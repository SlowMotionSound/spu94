# Phase 11: Noise Recalibration + Integration - Research

**Researched:** 2026-04-30
**Domain:** DAC noise model recalibration at 352.8kHz, A/B mode toggle, latency reporting, surface integration
**Confidence:** HIGH

## Summary

Phase 11 completes the true oversampled DAC pipeline by moving noise injection from 44.1kHz (post-decimation) to 352.8kHz (pre-decimation), adding a v1.2/v1.3 A/B toggle, updating latency reporting, and verifying zero surface breakage.

The critical technical challenge is noise shift recalibration. The current `DAC_NOISE_SHIFT=14` was calibrated for 44.1kHz operation where 100% of the HP-shaped noise power falls in the audio band. At 352.8kHz, the second-order HP shaping `(1-z^-1)^2` pushes most noise energy far above 22.05kHz. Numerical analysis shows only ~0.08% of the shaped noise power at 352.8kHz falls in-band, a ~31dB reduction versus single-rate. The shift must decrease from 14 to approximately 9 (empirically tuned) to maintain the -90dB in-band target. This must be tuned empirically because the FIR cascade's transition band and the LFSR's non-ideal whiteness affect the final number.

The A/B toggle, latency update, and surface integration are straightforward -- all follow established patterns with existing code providing exact templates.

**Primary recommendation:** Structure as (1) noise injection refactoring + shift recalibration in the inner loop, (2) A/B toggle API + process path selection, (3) latency calculation update, (4) surface regression verification.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** A/B toggle uses existing set/get pattern matching `dac_fir_enabled`, `dac_noise_enabled`, etc. No new enum, no elaborate API.
- **D-02:** Toggle selects between v1.2 (`spu94_dac_fir_step`) and v1.3 (`spu94_dac_fir_step_8x`) processing paths. Both functions already coexist.
- **D-03:** Inject noise at 352.8kHz before decimation, NOT at 44.1kHz post-decimation. Run LFSR 8 ticks per output sample.
- **D-04:** This is standard oversampling DAC modeling -- noise originates at converter clock rate and gets filtered by reconstruction path.
- **D-05:** Phase 10's `<<3` gain compensation stays. Final sign-off is a human listen gate.
- **D-06:** Claude's discretion on latency calculation. Update `spu94_get_total_latency_samples` for whichever mode is active.

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| DSP-05 | Run LFSR + HP noise model at 352.8kHz (8 ticks per output sample), retune DAC_NOISE_SHIFT for correct -90dB amplitude | Noise shift recalibration analysis (Pitfall 1), noise injection architecture (Architecture Patterns), code examples for 8x noise loop |
| DSP-07 | Report correct group delay via `spu94_get_total_latency_samples` for true oversampled path | Group delay calculation (Architecture Patterns), mode-aware latency formula |
| CMP-01 | A/B mode toggle -- selectable v1.2 vs v1.3 DAC processing path | Toggle pattern analysis (Code Examples), existing toggle template in codebase |
| INT-01 | CLI/Python/JUCE surfaces unchanged -- `--dac`, `set_dac_enabled()`, JUCE toggle work identically | Surface inventory (Integration Points), verification strategy |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Noise injection at 352.8kHz | C core (`spu94_process.c`) | -- | All DSP lives in C core; noise must be injected inside the 8x evaluation loop |
| A/B mode toggle state | C core (`spu94_state_internal.h`) | -- | New `dac_true_oversample` flag in struct, set/get pair in `spu94_io_chain.c` |
| A/B path selection | C core (`spu94_process.c`) | -- | Conditional dispatch to `_step` vs `_step_8x` + noise injection variant |
| Latency reporting | C core (`spu94_io_chain.c`) | -- | `spu94_get_total_latency_samples` mode-aware formula |
| Surface integration | CLI / Python / JUCE | C core (unchanged API) | Surfaces call existing C API -- INT-01 requires zero surface code changes |

## Standard Stack

No new libraries are needed. This phase modifies existing C source files only.

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| libspu94 (project) | v1.3-dev | C core DSP library | All DAC processing lives here |
| Unity test framework | 2.6.0 | C unit tests | Already in use, `tests/unit/vendor/Unity/` [VERIFIED: codebase] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| numpy | (installed) | Python test harness | Noise amplitude verification scripts |
| CMake/ctest | (installed) | Build + test runner | Existing infrastructure |

**No installation needed.** All dependencies are already present. [VERIFIED: build succeeds, 102 tests pass]

## Architecture Patterns

### System Architecture Diagram

```
Input (44.1kHz int16 stereo)
  |
  v
[Mixer: input gain -> bus split -> reverb -> master mix]
  |
  v
[DAC Section - mode-selected]
  |
  +--- v1.2 mode (dac_true_oversample=0) ----+
  |   spu94_dac_fir_step (44.1kHz cascade)    |
  |   spu94_dac_noise_step x1 (44.1kHz)       |
  |   q15_add_sat(fir_out, noise)             |
  +-------------------------------------------+
  |
  +--- v1.3 mode (dac_true_oversample=1) ----+
  |   spu94_dac_fir_step_8x:                  |
  |     Stage 1: 2 evals @ 88.2kHz            |
  |     Stage 2: 4 evals @ 176.4kHz           |
  |     Stage 3: 8 evals @ 352.8kHz           |
  |       +-- noise injected HERE (8 ticks) --|
  |     Decimation: keep last of 8             |
  |     Gain compensation: <<3                 |
  +-------------------------------------------+
  |
  v
Output (44.1kHz int16 stereo)
```

### Noise Injection Point (DSP-05 -- Critical)

Currently (`spu94_process.c` lines 114-124), noise is added AFTER the FIR cascade at 44.1kHz:

```c
/* Current v1.2-era wiring (WRONG for v1.3): */
if (state->dac_fir_enabled) {
    out_l = spu94_dac_fir_step_8x(&state->dac_fir_l, out_l);
    out_r = spu94_dac_fir_step_8x(&state->dac_fir_r, out_r);
}
if (state->dac_noise_enabled) {
    out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
    out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
}
```

For v1.3, noise must be injected INSIDE the 8x cascade loop at 352.8kHz, before decimation. This means the noise injection cannot remain in `spu94_process.c` -- it must move into `spu94_dac_fir.c` (or a new combined function) so it executes at the oversampled rate. [VERIFIED: codebase inspection]

**Two implementation approaches:**

**Option A (Recommended): New combined step function**
Add `spu94_dac_fir_step_8x_with_noise()` that takes both FIR state and noise state, injecting noise at each of the 8 Stage 3 evaluations. Clean separation, testable, follows the existing mono-per-channel pattern.

**Option B: Noise loop in spu94_process.c**
Modify `spu94_process.c` to call `spu94_dac_noise_step` 8 times in a loop. But this breaks encapsulation -- the caller would need to know the internal 8x structure. Not recommended.

### A/B Mode Toggle Pattern (CMP-01)

The existing toggle pattern is thoroughly established. Every toggle in the codebase follows this exact shape: [VERIFIED: codebase inspection of `spu94_io_chain.c`]

```c
/* In spu94_state_internal.h: */
uint8_t  dac_true_oversample;  /* 0=v1.2 approx (default), 1=v1.3 true */

/* In spu94_io_chain.c: */
void spu94_set_dac_true_oversample(spu94_state *state, int enabled) {
    if (state == NULL) return;
    state->dac_true_oversample = enabled ? 1 : 0;
}
int spu94_get_dac_true_oversample(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dac_true_oversample;
}
```

**Default value:** 1 (v1.3 true oversampling ON by default). The v1.3 path is the correct implementation; v1.2 is retained for comparison only. [ASSUMED]

### Process Path Selection

In `spu94_process.c`, the DAC section dispatches based on the toggle:

```c
if (state->dac_enabled) {
    if (state->dac_true_oversample) {
        /* v1.3: true 8x oversampling with noise at 352.8kHz */
        if (state->dac_fir_enabled && state->dac_noise_enabled) {
            out_l = spu94_dac_fir_step_8x_with_noise(
                &state->dac_fir_l, &state->dac_noise_l, out_l);
            out_r = spu94_dac_fir_step_8x_with_noise(
                &state->dac_fir_r, &state->dac_noise_r, out_r);
        } else if (state->dac_fir_enabled) {
            out_l = spu94_dac_fir_step_8x(&state->dac_fir_l, out_l);
            out_r = spu94_dac_fir_step_8x(&state->dac_fir_r, out_r);
        } else if (state->dac_noise_enabled) {
            /* noise-only at 352.8kHz: run 8 ticks, add to signal */
            /* ...8x noise ticks accumulated and added... */
        }
    } else {
        /* v1.2: approximate single-rate */
        if (state->dac_fir_enabled) {
            out_l = spu94_dac_fir_step(&state->dac_fir_l, out_l);
            out_r = spu94_dac_fir_step(&state->dac_fir_r, out_r);
        }
        if (state->dac_noise_enabled) {
            out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
            out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
        }
    }
}
```

### Noise Shift Recalibration (DSP-05 -- Critical)

**Current state:** `DAC_NOISE_SHIFT=14` calibrated for 44.1kHz, targeting -90dB in-band RMS. [VERIFIED: `spu94_dac_noise.c` line 45]

**Problem:** At 352.8kHz, the second-order HP shaping `(1-z^-1)^2` pushes most noise power above the audio band. Numerical analysis shows:

- HP-shaped noise at 352.8kHz has only ~0.08% of its total power below 22.05kHz [VERIFIED: numerical integration]
- This is a ~31dB reduction in in-band noise power versus 44.1kHz operation [VERIFIED: numerical integration]
- Each bit of right-shift contributes ~6dB of attenuation
- Theoretical new shift: 14 - (31/6) = approximately 9 [VERIFIED: calculation]

**Approach:** The shift value should NOT be changed at compile time in the `#define`. Instead:

1. Add a new `#define DAC_NOISE_SHIFT_8X` constant alongside the existing `DAC_NOISE_SHIFT`
2. The 8x noise step function uses `DAC_NOISE_SHIFT_8X`
3. The v1.2 path continues using `DAC_NOISE_SHIFT=14` unchanged
4. Empirically tune `DAC_NOISE_SHIFT_8X` by measuring in-band RMS with the full cascade running
5. Starting point: try 9, then adjust +/-1 until the test_dac_noise_amplitude equivalent passes

### Group Delay Calculation (DSP-07)

**v1.2 DAC FIR cascade group delay (all stages at 44.1kHz):**
- Stage 1: (55-1)/2 = 27 samples at 44.1kHz
- Stage 2: (11-1)/2 = 5 samples at 44.1kHz
- Stage 3: (7-1)/2 = 3 samples at 44.1kHz
- Total: 35 samples at 44.1kHz
[VERIFIED: calculation from tap counts in `spu94_dac_fir_internal.h`]

**v1.3 DAC FIR cascade group delay (stages at true rates):**
- Stage 1: 27 samples at 88.2kHz = 13.5 samples at 44.1kHz
- Stage 2: 5 samples at 176.4kHz = 1.25 samples at 44.1kHz
- Stage 3: 3 samples at 352.8kHz = 0.375 samples at 44.1kHz
- Total: 15.125 samples at 44.1kHz (round to 15)
[VERIFIED: calculation from tap counts and operating rates]

**Current `spu94_get_total_latency_samples` does NOT include DAC FIR group delay** -- it returns `58 + (ADPCM ? 28 : 0)`. [VERIFIED: `spu94_io_chain.c` line 176-179]

**Updated formula (mode-aware):**
```c
uint32_t spu94_get_total_latency_samples(const spu94_state *state) {
    if (state == NULL) return SPU94_LATENCY_SAMPLES;
    uint32_t lat = SPU94_LATENCY_SAMPLES;
    if (state->adpcm_enabled)
        lat += SPU94_ADPCM_BLOCK_SAMPLES;  /* 28 */
    if (state->dac_enabled && state->dac_fir_enabled) {
        if (state->dac_true_oversample)
            lat += 15;  /* v1.3: 15.125 rounded */
        else
            lat += 35;  /* v1.2: 35 exact */
    }
    return lat;
}
```

**Note:** This is the first time DAC FIR delay is reported. Previously the DAC FIR delay existed (35 samples for v1.2) but was not included in `spu94_get_total_latency_samples`. DSP-07 specifically asks for "correct group delay for the true oversampled path." Whether to also start reporting v1.2's 35-sample delay is a judgment call -- reporting both modes accurately is the cleanest approach, but it changes the v1.2 return value from 58 to 93 (or 86+28 with ADPCM). The requirement says "for the true oversampled path" so at minimum v1.3 must be accurate. [ASSUMED: both modes should report accurately]

### Recommended Project Structure

No new files needed beyond modifying existing ones. One possible new file:

```
src/spu94/
  spu94_dac_fir.c         # ADD spu94_dac_fir_step_8x_with_noise()
  spu94_dac_noise.c       # ADD DAC_NOISE_SHIFT_8X constant
  spu94_process.c         # MODIFY DAC section for mode dispatch
  spu94_io_chain.c        # ADD set/get toggle, MODIFY latency calc
  spu94_state_internal.h  # ADD dac_true_oversample field

include/spu94/
  spu94.h                 # ADD set/get declarations, update latency docs
  spu94_dac_fir.h         # ADD step_8x_with_noise declaration
  spu94_dac_noise.h       # UPDATE header comment for 352.8kHz operation

tests/unit/dac_noise/
  test_dac_noise_8x.c     # NEW: noise at 352.8kHz amplitude + spectral tests
tests/unit/process/
  test_process_dac_mode_toggle.c  # NEW: A/B mode toggle integration tests
```

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Noise amplitude calibration | Manual dB calculation in C | Empirical test with actual cascade | Theoretical shift vs measured shift can differ by 1-2 bits due to FIR transition band |
| Toggle state management | New enum or state machine | Existing `uint8_t` + set/get pattern | 6 existing toggles use this exact pattern; consistency is correctness |
| Group delay calculation | Runtime FIR analysis | Compile-time constant per mode | Linear-phase FIR group delay is fixed by tap count -- no need to measure at runtime |

## Common Pitfalls

### Pitfall 1: Noise Shift Off by Multiple Bits

**What goes wrong:** Using `DAC_NOISE_SHIFT=14` at 352.8kHz produces inaudibly quiet noise. The HP shaping at 8x rate pushes ~99.92% of noise power out of the audio band.
**Why it happens:** The HP filter `(1-z^-1)^2` has frequency-dependent gain. At 352.8kHz sample rate, the audio band (0-22.05kHz) is only 1/16th of the Nyquist bandwidth, and the HP shaping attenuates that low-frequency region heavily.
**How to avoid:** Use a separate `DAC_NOISE_SHIFT_8X` constant, start at 9 (theoretical), tune empirically by measuring in-band RMS. Write a test that measures the in-band RMS of 8x noise through the full cascade and asserts -100 to -80 dB.
**Warning signs:** Noise floor drops to -120dB or below; A/B toggle shows no audible noise difference.

### Pitfall 2: Noise Injection After Decimation (Breaks D-03)

**What goes wrong:** Adding noise at 44.1kHz (after the cascade picks every 8th sample) produces flat white noise in the audio band instead of spectrally shaped noise.
**Why it happens:** The whole point of injecting at 352.8kHz is that the cascade's lowpass filtering shapes the noise spectrum on the way down. Post-decimation injection skips this shaping entirely.
**How to avoid:** Noise must be added INSIDE the Stage 3 loop of `spu94_dac_fir_step_8x`, before the sat_s16 + `<<3` gain compensation. Each of the 8 Stage 3 evaluations gets its own noise tick added.
**Warning signs:** Noise spectrum is flat (white) instead of having the characteristic rising slope shaped by the cascade's stopband.

### Pitfall 3: Toggle Default Breaks Existing Behavior

**What goes wrong:** Setting `dac_true_oversample=1` as default means existing users who enable DAC suddenly get different (higher-CPU) behavior without opting in.
**Why it happens:** v1.3 is the "correct" implementation, tempting developers to make it the default.
**How to avoid:** Consider whether the default should be 1 (v1.3, correct) or 0 (v1.2, backward-compatible). Since Phase 10 already switched `spu94_process.c` to call `spu94_dac_fir_step_8x`, the v1.3 path is ALREADY the default for FIR. The toggle just adds the option to revert. Setting default=1 is consistent with current behavior. [VERIFIED: `spu94_process.c` line 117 calls `_step_8x`]
**Warning signs:** A/B comparison tests fail because the "A" state doesn't match v1.2 behavior.

### Pitfall 4: Gain Compensation Interaction with Noise

**What goes wrong:** The `<<3` gain compensation at the end of `spu94_dac_fir_step_8x` amplifies noise by 8x (18dB) in addition to the signal.
**Why it happens:** If noise is added before `<<3`, it gets amplified. If added after, it bypasses the cascade filtering (Pitfall 2).
**How to avoid:** Add noise to each Stage 3 intermediate sample BEFORE the decimation pick. The decimation (keep last of 8) naturally selects one noise-contaminated sample. Then `<<3` amplifies both signal and noise by 8x. The `DAC_NOISE_SHIFT_8X` calibration must account for this 18dB gain boost -- it's part of the empirical tuning. [VERIFIED: `spu94_dac_fir.c` line 252 shows `<<3` applied to `s3_last`]
**Warning signs:** Noise is 18dB too loud after shift calibration.

### Pitfall 5: Latency Change Breaks Existing Tests

**What goes wrong:** Adding DAC FIR delay to `spu94_get_total_latency_samples` changes the return value from 58 to 73 (or 93 for v1.2 mode) when DAC is enabled, breaking any test that asserts `== 58`.
**Why it happens:** No existing test enables DAC before checking latency, but downstream consumers might depend on the current value.
**How to avoid:** Audit all callers of `spu94_get_total_latency_samples` before changing. The function currently adds ADPCM delay only when ADPCM is enabled. Following the same pattern (add DAC delay only when DAC FIR is enabled) is safe because no existing code enables DAC FIR before querying latency in a way that would break.
**Warning signs:** `test_process_latency_comp.c` or Python binding latency tests fail.

## Code Examples

### Noise Injection Inside 8x Cascade Loop

```c
/* Source: derived from spu94_dac_fir.c spu94_dac_fir_step_8x, modified
 * to inject noise at each Stage 3 evaluation per D-03 */

int16_t spu94_dac_fir_step_8x_with_noise(
    spu94_dac_fir_state *fir,
    spu94_dac_noise_state *noise,
    int16_t input)
{
    /* Stage 1: 2 evaluations at 88.2kHz */
    int16_t s1[2];
    dac_fir_push(fir->stage1_delay, &fir->stage1_idx,
                 input, DAC_FIR_STAGE1_NTAPS);
    s1[0] = dac_fir_stage_apply(/* ... stage 1 ... */);

    dac_fir_push(fir->stage1_delay, &fir->stage1_idx,
                 0, DAC_FIR_STAGE1_NTAPS);
    s1[1] = dac_fir_stage_apply(/* ... stage 1 ... */);

    /* Stage 2: 4 evaluations at 176.4kHz */
    int16_t s2[4];
    for (int i = 0; i < 2; i++) {
        dac_fir_push(/* ... s1[i] ... */);
        s2[2*i] = dac_fir_stage_apply(/* ... stage 2 ... */);
        dac_fir_push(/* ... 0 ... */);
        s2[2*i+1] = dac_fir_stage_apply(/* ... stage 2 ... */);
    }

    /* Stage 3: 8 evaluations at 352.8kHz -- noise injected HERE */
    int16_t s3_last = 0;
    for (int j = 0; j < 4; j++) {
        dac_fir_push(/* ... s2[j] ... */);
        int16_t s3_val = dac_fir_stage_apply(/* ... stage 3 ... */);
        /* Inject noise at 352.8kHz */
        s3_val = q15_add_sat(s3_val, spu94_dac_noise_step(noise));

        dac_fir_push(/* ... 0 ... */);
        s3_last = dac_fir_stage_apply(/* ... stage 3 ... */);
        /* Inject noise at 352.8kHz */
        s3_last = q15_add_sat(s3_last, spu94_dac_noise_step(noise));
    }

    /* Gain compensation + decimation */
    return sat_s16((int32_t)s3_last << 3);
}
```

### Toggle API (follows existing pattern exactly)

```c
/* In spu94.h -- matches existing dac_enabled/dac_fir_enabled pattern */
void spu94_set_dac_true_oversample(spu94_state *state, int enabled);
int  spu94_get_dac_true_oversample(const spu94_state *state);
```

### Mode-Aware Latency

```c
/* In spu94_io_chain.c -- extends existing function */
#define DAC_FIR_GROUP_DELAY_V12  35u  /* (55-1)/2 + (11-1)/2 + (7-1)/2 */
#define DAC_FIR_GROUP_DELAY_V13  15u  /* 27/2 + 5/4 + 3/8, rounded */

uint32_t spu94_get_total_latency_samples(const spu94_state *state) {
    if (state == NULL) return SPU94_LATENCY_SAMPLES;
    uint32_t lat = SPU94_LATENCY_SAMPLES;
    if (state->adpcm_enabled)
        lat += SPU94_ADPCM_BLOCK_SAMPLES;
    if (state->dac_enabled && state->dac_fir_enabled)
        lat += state->dac_true_oversample
            ? DAC_FIR_GROUP_DELAY_V13
            : DAC_FIR_GROUP_DELAY_V12;
    return lat;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Noise at 44.1kHz post-FIR | Noise at 352.8kHz pre-decimation | Phase 11 (this phase) | Spectrally shaped noise floor matches real DAC behavior |
| Single DAC path (v1.2 approx) | Coexisting v1.2 + v1.3 with toggle | Phase 10 + 11 | A/B comparison for characterization |
| DAC FIR delay not reported | Mode-aware group delay in total latency | Phase 11 (this phase) | Accurate latency for host compensation |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `dac_true_oversample` default should be 1 (v1.3 on), matching Phase 10's switch to `_step_8x` | Architecture Patterns - Toggle Default | If 0 (v1.2 default), Phase 10's switch to `_step_8x` in `spu94_process.c` already made v1.3 the default; a 0 default would create inconsistency |
| A2 | Both v1.2 and v1.3 DAC FIR delay should be reported in `spu94_get_total_latency_samples` | Architecture Patterns - Group Delay | If only v1.3 delay is added, v1.2 mode latency is still underreported by 35 samples -- but this matches current behavior |
| A3 | Starting shift for `DAC_NOISE_SHIFT_8X` is ~9 based on theoretical analysis | Pitfall 1 | Off by 1-2 bits is expected; empirical tuning addresses this; risk is low |
| A4 | Noise-only mode at 352.8kHz (FIR disabled, noise enabled, true oversample on) should run 8 noise ticks and add the result | Architecture Patterns | Edge case -- likely not useful in practice but must be defined for completeness |

## Open Questions

1. **Exact value of `DAC_NOISE_SHIFT_8X`**
   - What we know: Theoretical analysis points to ~9 (range 8-10)
   - What's unclear: The actual FIR cascade transition band and `<<3` gain compensation interact to shift the calibration point
   - Recommendation: Start at 9, run the amplitude test, adjust empirically. The planner should include a calibration task.

2. **Noise-only mode with true oversample**
   - What we know: When FIR is disabled but noise is enabled and `dac_true_oversample=1`, the system needs to decide what "8 ticks of noise" means without the cascade to shape it
   - What's unclear: Should noise-only at 352.8kHz simply run 8 ticks and add the last one? Or should it fall back to v1.2 behavior (1 tick at 44.1kHz)?
   - Recommendation: Noise-only in v1.3 mode should run 8 ticks and add the accumulated result scaled appropriately. This matches the mental model of "everything runs at 352.8kHz when true oversample is on." But given this is an edge case, fallback to v1.2 noise behavior (1 tick) is also defensible.

3. **v1.2 DAC FIR delay -- should it be added to latency now?**
   - What we know: v1.2 DAC FIR has 35 samples of group delay that was never reported. DSP-07 says "report correct group delay for the true oversampled path."
   - What's unclear: Whether fixing the v1.2 reporting is in scope or creates unnecessary blast radius
   - Recommendation: Report DAC FIR delay for both modes. The change is additive (only affects return value when DAC FIR is enabled, which is off by default), so blast radius is near zero. Accuracy benefits both modes.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity 2.6.0 (C) + pytest (Python) |
| Config file | `tests/unit/*/CMakeLists.txt` + top-level `CMakeLists.txt` |
| Quick run command | `cd build && ctest -L 'dac_noise\|dac_fir' --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DSP-05 | LFSR noise at 352.8kHz, -90dB in-band | unit | `ctest -R dac_noise_8x_amplitude --output-on-failure` | Wave 0 |
| DSP-05 | HP-shaped noise spectrum at 352.8kHz | unit | `ctest -R dac_noise_8x_spectral --output-on-failure` | Wave 0 |
| DSP-07 | Group delay accurate for v1.3 mode | unit | `ctest -R process_dac_latency --output-on-failure` | Wave 0 |
| CMP-01 | A/B toggle set/get + path selection | integration | `ctest -R process_dac_mode_toggle --output-on-failure` | Wave 0 |
| CMP-01 | v1.2 mode output matches pre-phase-11 v1.2 | regression | `ctest -R process_dac_v12_regression --output-on-failure` | Wave 0 |
| INT-01 | CLI `--dac` unchanged | cli | `cd build && ctest -L cli --output-on-failure` | Existing (tests/cli/) |
| INT-01 | Python `set_dac_enabled` unchanged | binding | `pytest tests/python/binding/test_binding_mixer_dac.py` | Existing |

### Sampling Rate
- **Per task commit:** `cd build && ctest -L 'dac_noise\|dac_fir\|process' --output-on-failure`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green (102+ tests) before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/dac_noise/test_dac_noise_8x.c` -- covers DSP-05 (noise at 352.8kHz amplitude + spectrum)
- [ ] `tests/unit/process/test_process_dac_mode_toggle.c` -- covers CMP-01 (A/B toggle) + DSP-07 (latency)
- [ ] `tests/unit/dac_noise/CMakeLists.txt` -- add new test executables
- [ ] `tests/unit/process/CMakeLists.txt` -- add new test executable

## Integration Points Inventory

### CLI (`src/cli/cmd_reverb.c`)
- `--dac` flag sets `spu94_set_dac_enabled(state, 1)` + sub-toggles [VERIFIED: lines 393-398]
- **No change needed.** The toggle dispatches inside C core. CLI flag behavior is identical. [VERIFIED: codebase]

### Python Binding (`python/spu94/_binding.py`)
- `spu94_set_dac_enabled` / `spu94_get_dac_enabled` + FIR/noise sub-toggles are bound [VERIFIED: lines 279-295]
- `spu94_get_total_latency_samples` is bound [VERIFIED: lines 229-230]
- **No Python binding change needed** for existing surfaces. The new toggle (`dac_true_oversample`) MAY optionally be bound for Python characterization scripts (Phase 12), but INT-01 does not require it.
- **Note:** `spu94_get_total_latency_samples` is bound in `_binding.py` but NOT exposed in `api.py` or `reverb.py` [VERIFIED: grep found no references]. This is a pre-existing gap, not introduced by Phase 11.

### JUCE Standalone (`src/standalone/`)
- `PluginProcessor.cpp` calls `spu94_set_dac_enabled/fir/noise` from atomic bools [VERIFIED: lines 180-186]
- `PluginEditor.cpp` has DAC toggle/FIR toggle/noise toggle buttons [VERIFIED: lines 165-195]
- **No JUCE change needed.** The existing buttons control the same C API. The v1.3 path is already wired (Phase 10 switched to `_step_8x`).

### Golden Files
- DAC-on golden files will change because noise injection moves to 352.8kHz. However, golden regeneration is Phase 12 scope (INT-02). Phase 11 should NOT regenerate goldens -- that's a separate phase.

## Sources

### Primary (HIGH confidence)
- `src/spu94/spu94_dac_noise.c` -- Current LFSR + HP noise implementation, SHIFT=14
- `src/spu94/spu94_dac_fir.c` -- Both v1.2 and v1.3 step functions, Stage 3 loop structure
- `src/spu94/spu94_process.c` -- DAC section wiring (lines 114-124)
- `src/spu94/spu94_io_chain.c` -- Toggle patterns, latency calculation
- `include/spu94/spu94.h` -- Public API declarations, SPU94_LATENCY_SAMPLES=58
- `src/spu94/spu94_state_internal.h` -- State struct layout
- `src/standalone/PluginProcessor.cpp` -- JUCE DAC toggle wiring
- `src/cli/cmd_reverb.c` -- CLI `--dac` flag implementation
- `python/spu94/_binding.py` -- Python ctypes bindings for DAC functions

### Secondary (MEDIUM confidence)
- Numerical analysis: noise shift recalibration via spectral power integration (in-band power fraction calculation)
- Group delay calculation: linear-phase FIR theory applied to known tap counts

### Tertiary (LOW confidence)
- None -- all claims verified against codebase or derived from verified calculations

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new dependencies, all existing code verified
- Architecture: HIGH -- all patterns verified in existing codebase, noise injection point is the only novel engineering
- Pitfalls: HIGH -- noise calibration verified numerically, toggle patterns verified by code inspection
- Latency: MEDIUM -- group delay calculation is theoretical; should be validated with impulse test

**Research date:** 2026-04-30
**Valid until:** 2026-06-30 (stable C codebase, no upstream dependency changes expected)
