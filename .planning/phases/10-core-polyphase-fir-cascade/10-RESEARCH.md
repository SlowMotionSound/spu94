# Phase 10: Core Polyphase FIR Cascade - Research

**Researched:** 2026-04-30
**Domain:** Naive 8x zero-stuff FIR interpolation cascade in Q15 fixed-point C
**Confidence:** HIGH

## Summary

Phase 10 replaces the v1.2 DAC FIR (three half-band stages all running at 44.1kHz -- a passband-equivalent approximation) with the real AK4309 operation: zero-stuff the input, run each stage at its true operating rate (88.2 / 176.4 / 352.8kHz), and decimate the 8x oversampled output back to 44.1kHz. Per D-01 and D-02, this is the **naive zero-stuff approach** -- literally insert 7 zeros and call the existing `dac_fir_stage_apply` at 8x rate. Polyphase decomposition is explicitly deferred.

The existing codebase is well-structured for this change. `dac_fir_stage_apply` is a generic folded-form half-band applier parameterized by delay line, coefficients, and pair table -- it works unchanged at any rate. The coefficient tables in `spu94_dac_fir_coef.c` were designed for the correct operating rates and need zero modification. The `spu94_dac_fir_state` struct's delay lines (55/11/7 entries) are sized correctly for samples at the elevated rates. The change is in the *control flow*: how many times each stage is clocked per 44.1kHz input sample, and what feeds into each stage.

The golden file strategy is the main process risk. 271 files exist (135 .wav + 136 sidecars). The 80 non-DAC golden .wav files (50 reverb + 30 ADPCM) must be bit-identical after the change. The 55 DAC golden .wav files will change and need regeneration. Archiving v1.2 DAC goldens before any code change is a hard prerequisite.

**Primary recommendation:** Prototype the 8x cascade in scipy first (D-04), validate against `build_composite()`, then port to C as `spu94_dac_fir_step_8x` alongside the preserved `spu94_dac_fir_step` (D-03). Prove zero blast radius on DAC-off goldens before touching any DAC-on goldens.

<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Naive 8x zero-stuff implementation -- literally insert 7 zeros between each input sample and run the existing `dac_fir_stage_apply` function at 8x rate through the cascade. This is what the AK4309 hardware does. ~176 multiplies per output sample vs 22 in v1.2, but well under the ~22,000ns per-sample budget on desktop. If MCU cost matters later, polyphase decomposition is a pure optimization that can be added without changing output.
- **D-02:** Do NOT pursue polyphase decomposition in this phase. The naive approach is simpler, faithful to hardware behavior, and fast enough. Polyphase is deferred as a future optimization if profiling shows it's needed.
- **D-03:** Keep the existing `spu94_dac_fir_step` function intact. Add `spu94_dac_fir_step_8x` as the new true-oversampled path. `spu94_process.c` switches to calling the 8x version by default. Both functions coexist for Phase 11's A/B mode toggle (CMP-01).
- **D-04:** Prototype the 8x cascade in Python/scipy first (extend `tools/dac_filter_design.py`). Verify frequency response and impulse response match expectations at the elevated rate. Then port to C with a known-good reference to diff against. This catches inter-stage buffer ordering and decimation phase bugs before they hit C.

### Claude's Discretion
- Accumulator overflow proof re-derivation for the 8x path (same coefficients, zero-stuffed inputs reduce worst-case)
- Delay line dimensioning for 8x state (trivial: 8 samples x 2 channels x 2 bytes = 32 bytes intermediate buffer)
- Decimation sample selection (which of 8 outputs to keep -- resolve with impulse test during prototype)
- Whether to add the 8x state to the existing `spu94_dac_fir_state` struct or create a new struct

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.

</user_constraints>

<phase_requirements>

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| DSP-01 | Zero-stuff input to 352.8kHz (insert 7 zeros between each 44.1kHz sample) | Naive zero-stuff per D-01; `dac_fir_push` helper reused for pushing zeros; Section "Architecture Patterns" details the loop |
| DSP-02 | Run Stage 1 interpolation FIR at 88.2kHz using v1.2 coefficients verbatim | `dac_fir_stage_apply` reused unchanged; 55-tap coefficients from `spu94_dac_fir_coef.c` unchanged; 2 evaluations per input sample |
| DSP-03 | Run Stage 2 interpolation FIR at 176.4kHz using v1.2 coefficients verbatim | Same `dac_fir_stage_apply`; 11-tap coefficients; 4 evaluations per input sample |
| DSP-04 | Run Stage 3 interpolation FIR at 352.8kHz using v1.2 coefficients verbatim | Same `dac_fir_stage_apply`; 7-tap coefficients; 8 evaluations per input sample |
| DSP-06 | Decimate 352.8kHz output to 44.1kHz (pick every 8th sample after full cascade) | Simple selection -- no decimation filter needed; Section "Decimation Strategy" explains why |
| DSP-08 | Real-time safety preserved -- no heap, no locks, no syscalls in 8x processing path | All intermediate storage is stack-local (16 bytes) or existing struct-embedded delay lines; Section "Real-Time Safety" |
| INT-03 | DAC-off golden files are bit-identical before and after (zero blast radius on non-DAC paths) | 80 non-DAC goldens (50 reverb + 30 ADPCM) must SHA-256-match; the 8x code is inside a `dac_enabled` guard; Section "Golden File Strategy" |
| INT-04 | Audio-band frequency response matches v1.2 within 0.01dB (same coefficients, same passband) | Same coefficients produce same passband ripple; scipy prototype verifies this before C port; Section "Frequency Response Validation" |

</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| 8x zero-stuff + cascade FIR | C core DSP (`spu94_dac_fir.c`) | -- | All DSP lives in C core; no DSP in wrappers |
| Decimation (pick every 8th) | C core DSP (`spu94_dac_fir.c`) | -- | Internal to the step function's return value |
| Process loop wiring | C core integration (`spu94_process.c`) | -- | Switches call from `_step` to `_step_8x` |
| Scipy prototype/validation | Python tooling (`tools/dac_filter_design.py`) | -- | Prototype-then-port strategy per D-04 |
| Golden file management | Test infrastructure (`scripts/regenerate_goldens.py`) | -- | Archive v1.2, regenerate v1.3 after code change |
| State struct extension | C core state (`spu94_state_internal.h`) | -- | Minimal -- delay lines unchanged, no new buffers in struct |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C11 (gcc 14) | 14.2.0 | DSP implementation | Project standard; `_Static_assert` and `static inline` used throughout [VERIFIED: codebase] |
| scipy | 1.17.1 | Prototype 8x cascade, `freqz` verification | Already in project; `remez`, `freqz`, `convolve` cover all needs [VERIFIED: codebase `tools/dac_filter_design.py`] |
| numpy | 2.2.4 | Array operations for prototype | Already in project [VERIFIED: codebase] |
| Unity test framework | existing | C unit tests | Already in project; all DAC FIR tests use it [VERIFIED: `tests/unit/dac_fir/CMakeLists.txt`] |
| pytest | existing | Python conformance + golden tests | Already in project [VERIFIED: `tests/conformance/`] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| matplotlib | existing | Frequency response comparison plots | Prototype verification (`--plot` mode) |

### No New Dependencies
No new libraries, tools, or build changes needed. [VERIFIED: codebase analysis]

## Architecture Patterns

### System Architecture Diagram

```
44.1kHz input sample (one per channel)
         |
         v
  +------------------+
  | spu94_dac_fir_   |
  | step_8x()        |    NEW FUNCTION (D-03)
  |                  |
  |  push input ->   |
  |  Stage 1 delay   |
  |  push 0 ->       |   <-- zero-stuff (DSP-01)
  |  Stage 1 delay   |
  |  evaluate x2     |   <-- 88.2kHz (DSP-02)
  |       |          |
  |  for each of 2:  |
  |    push -> S2    |
  |    push 0 -> S2  |   <-- zero-stuff
  |    evaluate x2   |   <-- 176.4kHz (DSP-03)
  |       |          |
  |  for each of 4:  |
  |    push -> S3    |
  |    push 0 -> S3  |   <-- zero-stuff
  |    evaluate x2   |   <-- 352.8kHz (DSP-04)
  |       |          |
  |  8 outputs       |
  |  return last     |   <-- decimate (DSP-06)
  +------------------+
         |
         v
  44.1kHz output sample
```

### Recommended Project Structure (no new files)

All changes are within existing files. No new source files are created.

```
src/spu94/
  spu94_dac_fir.c          # ADD spu94_dac_fir_step_8x (keep _step intact)
  spu94_process.c          # MODIFY DAC section to call _step_8x
  spu94_state_internal.h   # MINOR: no struct changes needed (see analysis)
include/spu94/
  spu94_dac_fir.h          # ADD declaration of _step_8x
tools/
  dac_filter_design.py     # ADD --verify-8x mode (scipy prototype)
tests/unit/dac_fir/
  test_dac_fir_overflow_proof.c  # ADD zero-stuffed overflow cases
  (potential new file for 8x-specific tests)
```

### Pattern: Naive 8x Zero-Stuff Cascade

**What:** For each 44.1kHz input sample, push the real sample then 7 zeros through the three-stage cascade. Each stage doubles the rate by interleaving one real sample and one zero. The existing `dac_fir_stage_apply` is called at each evaluation point. [VERIFIED: `dac_fir_stage_apply` in `spu94_dac_fir.c` is parameterized and reusable]

**When to use:** This is the only pattern for Phase 10. Per D-01, the naive approach is mandatory.

**Core loop structure (pseudocode from STACK.md, adapted for D-01):**

```c
// Source: Derived from D-01 decision + existing dac_fir_stage_apply signature
int16_t spu94_dac_fir_step_8x(spu94_dac_fir_state *state, int16_t input) {
    int16_t s1[2];  /* Stage 1 outputs at 88.2kHz */

    /* Stage 1: push real sample, evaluate; push zero, evaluate */
    dac_fir_push(state->stage1_delay, &state->stage1_idx,
                 input, DAC_FIR_STAGE1_NTAPS);
    s1[0] = dac_fir_stage_apply(state->stage1_delay, state->stage1_idx,
                                DAC_FIR_STAGE1_NTAPS,
                                dac_interp_stage1,
                                dac_fir_stage1_pairs,
                                DAC_FIR_STAGE1_NPAIRS);
    dac_fir_push(state->stage1_delay, &state->stage1_idx,
                 0, DAC_FIR_STAGE1_NTAPS);
    s1[1] = dac_fir_stage_apply(state->stage1_delay, state->stage1_idx,
                                DAC_FIR_STAGE1_NTAPS,
                                dac_interp_stage1,
                                dac_fir_stage1_pairs,
                                DAC_FIR_STAGE1_NPAIRS);

    int16_t s2[4];  /* Stage 2 outputs at 176.4kHz */
    for (int i = 0; i < 2; i++) {
        dac_fir_push(state->stage2_delay, &state->stage2_idx,
                     s1[i], DAC_FIR_STAGE2_NTAPS);
        s2[2*i] = dac_fir_stage_apply(/* stage 2 params */);
        dac_fir_push(state->stage2_delay, &state->stage2_idx,
                     0, DAC_FIR_STAGE2_NTAPS);
        s2[2*i+1] = dac_fir_stage_apply(/* stage 2 params */);
    }

    int16_t s3_last;  /* Only need the final Stage 3 output */
    for (int j = 0; j < 4; j++) {
        dac_fir_push(state->stage3_delay, &state->stage3_idx,
                     s2[j], DAC_FIR_STAGE3_NTAPS);
        int16_t tmp = dac_fir_stage_apply(/* stage 3 params */);
        dac_fir_push(state->stage3_delay, &state->stage3_idx,
                     0, DAC_FIR_STAGE3_NTAPS);
        s3_last = dac_fir_stage_apply(/* stage 3 params */);
        /* Only the very last evaluation survives decimation */
    }

    return s3_last;  /* Decimated output at 44.1kHz */
}
```

**Key insight -- D-01 means the delay lines receive the zero-stuffed stream.** Stage 1's 55-tap delay line holds a mix of real samples and zeros. Stage 2's 11-tap delay line holds Stage 1's outputs interspersed with zeros. The delay line *sizes* are unchanged -- they were already dimensioned for the elevated-rate sample count. [VERIFIED: `_Static_assert` guards in `spu94_dac_fir_internal.h` validate delay line vs coefficient count]

### Pattern: Preserve v1.2 Path Alongside (D-03)

**What:** The existing `spu94_dac_fir_step` function is kept completely intact. The new `spu94_dac_fir_step_8x` is added as a sibling. `spu94_process.c` calls `_step_8x` by default. Phase 11 adds a mode toggle for A/B comparison (CMP-01).

**Why:** Zero blast radius on v1.2 behavior. The v1.2 function remains callable, testable, and diffable against the new function. v1.2 golden files remain valid for the old path.

### Anti-Patterns to Avoid

- **Polyphase decomposition:** Explicitly forbidden by D-02. Do not split the filter into sub-filters even though the codebase has this pattern in `spu94_fir.c`. The naive zero-stuff-and-evaluate approach is simpler and produces identical output. [LOCKED DECISION]
- **Modifying `dac_fir_stage_apply`:** This function is generic and correct. The 8x path reuses it verbatim. No changes to the apply function, the push helper, or the read_tap helper. [VERIFIED: function is parameterized by all state]
- **Modifying coefficient tables:** The coefficients in `spu94_dac_fir_coef.c` are designed for the correct operating rates. They need zero modification. [LOCKED: out of scope per REQUIREMENTS.md]
- **Persistent 352.8kHz buffer in state struct:** The 8 intermediate samples from Stage 3 are transient -- produced and consumed within one call to `_step_8x`. Use stack-local arrays only. [DISCRETION: confirmed by analysis]
- **Computing all 8 Stage 3 outputs when only the last is needed:** For the decimation sample, only the 8th (last) output matters for the return value. However, all 8 evaluations must still execute because each pushes into the delay line and affects future calls. The loop cannot be short-circuited. [VERIFIED: delay line state is cumulative]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Folded-form FIR evaluation | New filter apply function | Existing `dac_fir_stage_apply` | Already handles half-band folding, zero-skip, symmetric pairs [VERIFIED: `spu94_dac_fir.c` line 108-129] |
| Circular buffer push/read | New delay line helpers | Existing `dac_fir_push` / `dac_fir_read_tap` | Parameterized by ntaps, same convention as `spu94_fir.c` [VERIFIED: lines 89-100] |
| Composite frequency response | Manual cascade verification | Existing `build_composite()` in `dac_filter_design.py` | Already models the 8x cascade at 352.8kHz [VERIFIED: lines 90-101] |
| Q15 arithmetic | Custom saturating math | Existing `sat_s16`, `q15_add_sat`, `q15_mul_truncate` | Project-wide Q15 primitives [VERIFIED: `spu94_q15.h`] |
| Golden file generation | Custom test scripts | Existing `scripts/regenerate_goldens.py --dac` and `--dac-isolated` | Handles determinism env vars, SHA-256 sidecars, WAV format [VERIFIED: `regenerate_goldens.py`] |

## Decimation Strategy

**Pick the last of 8 outputs. No decimation filter needed.** [VERIFIED: STACK.md + FEATURES.md analysis]

Rationale:
1. The input signal is bandlimited to 22.05kHz (arrives at 44.1kHz). It has no energy that would alias during 8:1 decimation.
2. The interpolation cascade provides 41dB stopband rejection. [VERIFIED: `dac_filter_design.py --verify` output]
3. Residual image energy above 22.05kHz after the cascade is at least 41dB below passband. After decimation, aliased content lands at approximately -131dB (41dB + 90dB noise floor). Inaudible.
4. On real AK4309 hardware, there is no digital decimation -- the output is analog. The decimation is an artifact of our digital model. The simplest correct approach is to just select one sample.

**Which sample to keep:** The last sample (index 7 in a 0-indexed 8-sample buffer). This corresponds to the "most recently processed" sample after the full zero-stuff cascade, maintaining proper time alignment with the next input sample. The exact choice affects group delay by fractions of a 352.8kHz sample period -- irrelevant. The scipy prototype (D-04) will confirm the correct choice via impulse response alignment. [DISCRETION]

## Frequency Response Validation

**INT-04 requires the audio-band response to match v1.2 within 0.01dB across 20Hz-20kHz.**

This is guaranteed by design because:
1. The same Q15 coefficients are used -- `spu94_dac_fir_coef.c` is untouched. [LOCKED]
2. The same `dac_fir_stage_apply` folded-form evaluation is used -- same arithmetic, same truncation. [VERIFIED]
3. The passband behavior of the cascade is determined by the coefficients, not the rate at which they are evaluated. Zero-stuffing creates spectral images outside the passband; the cascade suppresses them. The passband itself is unchanged. [VERIFIED: DSP theory + `build_composite()` already validates this]

**Validation approach (per D-04):**
1. Scipy prototype runs the 8x cascade on a swept-sine input
2. Measure frequency response at 44.1kHz output
3. Compare against v1.2's frequency response (obtainable from the same script running the single-rate cascade)
4. Assert max deviation in 20Hz-20kHz band is less than 0.01dB
5. C port is then verified against the scipy reference output

## Accumulator Overflow Analysis (8x Path)

The existing overflow proofs in `spu94_dac_fir.c` bound the worst case for each stage when all delay line entries are at maximum magnitude (all `INT16_MIN` or `INT16_MAX`). [VERIFIED: source lines 30-80]

**With naive zero-stuffing, every other delay line entry is 0.** This strictly reduces the accumulator worst case because half of the folded-pair additions contribute zero. The worst case for the 8x path is bounded by the v1.2 proof for each individual stage.

However, the interaction is subtle: a zero-stuffed delay line does not have *alternating* zeros in a fixed pattern across calls -- the pattern shifts as the circular buffer wraps. The worst case occurs when all non-zero entries happen to be the same sign and maximum magnitude, which can only happen if consecutive real inputs are all `INT16_MIN`. In that case, the non-zero entries in the delay line are all `INT16_MIN`, and the zero entries contribute nothing.

**For each stage, the 8x worst-case accumulator sum equals or is less than the v1.2 worst case:**

| Stage | v1.2 Worst Case | 8x Worst Case | Reason |
|-------|-----------------|---------------|--------|
| Stage 1 | 1,904,643,762 (0x71868EB2) | <= 1,904,643,762 | Same or fewer non-zero delay entries [VERIFIED: proof in source] |
| Stage 2 | 1,336,455,240 (0x4FA8B048) | <= 1,336,455,240 | Same reasoning [VERIFIED] |
| Stage 3 | 1,221,048,126 (0x48C7B73E) | <= 1,221,048,126 | Same reasoning [VERIFIED] |

**int32 accumulators remain sufficient.** No promotion to int64 needed. [DISCRETION: re-derive formal proof as comment block in the new function, extending the existing pattern]

**Validation:** Extend `test_dac_fir_overflow_proof.c` with test cases that exercise the delay line with alternating (real, 0) patterns at maximum magnitude. [DISCRETION]

## Real-Time Safety (DSP-08)

The 8x path satisfies real-time safety by construction: [VERIFIED: source analysis]

| Constraint | Status | Evidence |
|-----------|--------|----------|
| No heap allocation | PASS | All intermediate storage is stack-local `int16_t s1[2], s2[4]` (12 bytes) or existing struct-embedded delay lines |
| No locks/mutexes | PASS | No shared state -- mono API per channel, caller owns the state struct |
| No syscalls | PASS | Pure arithmetic: integer multiply, add, shift, circular buffer indexing |
| Stack budget | 12-16 bytes for intermediates + existing `dac_fir_stage_apply` stack (one `int32_t acc` + loop vars) | Well under any reasonable stack limit |

**Compute budget:**

| Metric | v1.2 | v1.3 (8x naive) | Budget (44.1kHz) |
|--------|------|-----------------|------------------|
| `dac_fir_stage_apply` calls per sample | 3 | 14 (2+4+8) | -- |
| Total multiplies per sample | 22 | ~176 | -- |
| Estimated time per sample | ~50ns | ~400ns | 22,676ns |
| CPU headroom | ~450x | ~56x | Real-time safe |

Even at 176 multiplies, the per-sample cost is under 2% of the 22.7us budget at 44.1kHz on modern x86. [VERIFIED: STACK.md section 4]

## Golden File Strategy

### Current Inventory [VERIFIED: filesystem scan]

| Category | Count (.wav) | DAC Involved? | Impact |
|----------|-------------|---------------|--------|
| Reverb (base) | 50 | No (DAC off) | Must be BIT-IDENTICAL (INT-03) |
| ADPCM | 30 | No (DAC off) | Must be BIT-IDENTICAL (INT-03) |
| DAC full-pipeline | 50 | Yes | WILL CHANGE -- regenerate |
| DAC isolated | 5 | Yes | WILL CHANGE -- regenerate |
| SHA-256 sidecars | 135 | Mirrors above | Regenerate alongside .wav |
| **Total** | **135 .wav + 136 sidecars = 271 files** | | |

### Transition Plan

1. **Before any code change:** Archive v1.2 DAC goldens (git tag or copy to `tests/golden_v1.2/`). This is Success Criterion 5.
2. **After code change, before golden regeneration:** Assert all 80 non-DAC goldens (50 reverb + 30 ADPCM) produce identical SHA-256 hashes. This proves zero blast radius (INT-03).
3. **Regenerate DAC goldens:** Run `scripts/regenerate_goldens.py --dac --dac-isolated` after the new `_step_8x` is wired into `spu94_process.c`.
4. **The conformance test** (`tests/conformance/test_goldens_present.py`) validates structural completeness (135 .wav + 135 .sha256) and spot-checks SHA-256 matches. It does not need modification -- it will pass once new goldens are committed.

### Which Samples to Keep as Decimated Output

The golden regeneration script shells out to the `spu94` CLI binary which calls `spu94_process`. Once `spu94_process.c` calls `_step_8x` instead of `_step`, the regeneration script produces the new goldens automatically. No script changes needed. [VERIFIED: `regenerate_goldens.py` lines 209-253]

## State Struct Analysis

### Current Sizes [VERIFIED: compiled measurement]

| Item | Size (bytes) |
|------|-------------|
| `spu94_dac_fir_state` | 152 (per channel: 55*2 + 1 + 11*2 + 1 + 7*2 + 1 + padding) |
| `spu94_dac_noise_state` | 8 (per channel) |
| `spu94_state` total | 1,240 |
| `SPU94_STATE_SIZE_MAX` | 16,384 |
| Headroom | 15,144 bytes |

### Impact of Phase 10

**No state struct changes needed.** [DISCRETION: analysis below]

The naive zero-stuff approach (D-01) reuses the existing delay lines at the existing sizes. The delay lines hold samples at the elevated rate -- a mix of real values and zeros -- which is exactly what they were dimensioned for. The 55-tap Stage 1 delay line holds 55 samples at 88.2kHz (the real rate), just as the `_Static_assert` guards verify.

There is no intermediate buffer that needs to persist across calls. The 8 outputs from the cascade are either:
- Stack-local `int16_t s1[2], s2[4]` arrays (transient, 12 bytes on stack)
- Produced and consumed within the same `_step_8x` invocation

The `spu94_dac_fir_state` struct does not grow. The `_Static_assert` in `spu94_state_internal.h` will not fire. No `SPU94_STATE_SIZE_MAX` bump is needed.

**Future consideration (Phase 11):** Adding a `dac_oversampled` toggle (uint8_t, 1 byte) for A/B mode. This is Phase 11's scope, not Phase 10's.

## Scipy Prototype Design (D-04)

### What to Add to `tools/dac_filter_design.py`

A `--verify-8x` mode that simulates the naive zero-stuff cascade sample-by-sample in Python, matching the C implementation's exact arithmetic:

```python
# Source: Derived from D-04 + existing dac_filter_design.py patterns
def verify_8x_cascade(h1_q, h2_q, h3_q, input_signal):
    """Simulate the naive 8x zero-stuff + cascade + decimate in Python.

    Uses Q15 integer arithmetic matching the C implementation.
    Returns decimated output at 44.1kHz.
    """
    # Initialize delay lines as circular buffers (matching C convention)
    s1_delay = np.zeros(len(h1_q), dtype=np.int16)
    s2_delay = np.zeros(len(h2_q), dtype=np.int16)
    s3_delay = np.zeros(len(h3_q), dtype=np.int16)
    s1_idx, s2_idx, s3_idx = 0, 0, 0

    output = []
    for sample in input_signal:
        # Stage 1: push real sample, evaluate; push zero, evaluate
        s1_out = []
        for val in [int(sample), 0]:
            # push + evaluate (matching dac_fir_push + dac_fir_stage_apply)
            ...
        # Stage 2: for each s1 output, push real, evaluate; push zero, evaluate
        s2_out = []
        for val in s1_out:
            ...
        # Stage 3: same pattern
        s3_out = []
        for val in s2_out:
            ...
        # Decimate: keep last
        output.append(s3_out[-1])
    return np.array(output, dtype=np.int16)
```

**Verification checks:**
1. Frequency response of 8x output matches `build_composite()` within 0.01dB in 20Hz-20kHz
2. Impulse response matches expected cascade shape
3. DC gain matches v1.2 cascade DC gain (approximately -0.027dB) [VERIFIED: source comment in `spu94_dac_fir_coef.c`]
4. The 8x output at 44.1kHz can be compared directly against v1.2's `spu94_dac_fir_step` output to measure the actual delta

## Integration Point (spu94_process.c)

### Current Code [VERIFIED: `spu94_process.c` lines 114-124]

```c
/* 7. DAC section (D-09 through D-12): master output only */
if (state->dac_enabled) {
    if (state->dac_fir_enabled) {
        out_l = spu94_dac_fir_step(&state->dac_fir_l, out_l);
        out_r = spu94_dac_fir_step(&state->dac_fir_r, out_r);
    }
    if (state->dac_noise_enabled) {
        out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
        out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
    }
}
```

### Phase 10 Change

Replace `spu94_dac_fir_step` with `spu94_dac_fir_step_8x`:

```c
/* 7. DAC section: true 8x oversampled interpolation */
if (state->dac_enabled) {
    if (state->dac_fir_enabled) {
        out_l = spu94_dac_fir_step_8x(&state->dac_fir_l, out_l);
        out_r = spu94_dac_fir_step_8x(&state->dac_fir_r, out_r);
    }
    if (state->dac_noise_enabled) {
        out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
        out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
    }
}
```

**Noise model stays at 44.1kHz for Phase 10.** Noise at 352.8kHz is Phase 11's scope (DSP-05). The `spu94_dac_noise_step` is called once per sample, same as v1.2. This is a deliberate architectural choice -- the FIR cascade is the focus of this phase, and mixing in noise rate changes would complicate both the implementation and the golden file transition. [VERIFIED: DSP-05 is assigned to Phase 11 in REQUIREMENTS.md traceability table]

### Reset Logic

`spu94_io_chain.c` lines 282-293 handle `spu94_set_dac_enabled` disable/re-enable. The reset calls `spu94_dac_fir_init(&state->dac_fir_l)` which `memset`s the state to zero. This works identically for the 8x path because the state struct is unchanged -- same delay lines, same indices. No modification to `spu94_io_chain.c` needed. [VERIFIED: source analysis]

## Common Pitfalls

### Pitfall 1: Push-Then-Evaluate Ordering
**What goes wrong:** Reversing the order of push and evaluate, or pushing the zero before the real sample. The AK4309 processes the real sample first, then the interpolated (zero-stuffed) position.
**Why it happens:** The zero-stuff convention is not universally standardized -- some implementations push zero first, then real.
**How to avoid:** The scipy prototype (D-04) establishes the correct ordering. The C port must match exactly. Impulse response test confirms: a single impulse at input should produce the same time-domain shape as v1.2's cascade.
**Warning signs:** DC gain deviating from v1.2; impulse response shifted by one sub-sample.

### Pitfall 2: Delay Line State Accumulation Across Calls
**What goes wrong:** Assuming each call to `_step_8x` is independent. The delay lines carry state across calls -- each push advances the circular buffer index, and the zeros pushed in one call affect the filter output in subsequent calls.
**Why it happens:** The function looks "stateless" because it returns one sample per call, but the delay lines in `spu94_dac_fir_state` are persistent state.
**How to avoid:** The existing v1.2 code has the same property. The 8x version pushes 2 samples per stage per input sample (real + zero) instead of 1. After N input samples, each stage's delay line has seen 2N pushes. The indices wrap correctly because `dac_fir_push` uses modular arithmetic. [VERIFIED: `dac_fir_push` line 98: `*idx = (uint8_t)(((unsigned)(*idx) + 1u) % ntaps)`]
**Warning signs:** Audio glitches at the delay line wrap boundary (every 55/2 = 27.5 input samples for Stage 1).

### Pitfall 3: Golden File Mass Invalidation
**What goes wrong:** Regenerating goldens before verifying zero blast radius. This destroys the ability to detect bugs that leak into non-DAC paths.
**Why it happens:** Eagerness to "make the tests pass" after the code change.
**How to avoid:** Strict ordering: (1) archive v1.2 DAC goldens, (2) make code change, (3) assert non-DAC goldens bit-identical, (4) only then regenerate DAC goldens.
**Warning signs:** Any non-DAC golden SHA-256 mismatch is a critical bug.

### Pitfall 4: Incorrect Decimation Phase
**What goes wrong:** Keeping the wrong one of the 8 output samples, resulting in a subtle time offset in the output.
**Why it happens:** Eight possible choices, only one is correct for time alignment.
**How to avoid:** The scipy prototype determines the correct phase via impulse response comparison against the analytical composite. The C implementation matches.
**Warning signs:** v1.2 vs v1.3 impulse response peaks at different sample indices (should align).

### Pitfall 5: Compute Budget Surprise
**What goes wrong:** 176 multiplies per sample per channel (352 for stereo) is 8x the v1.2 cost. If the developer expects v1.2 performance, benchmark tests may alarm.
**Why it happens:** D-01 explicitly chooses the 8x cost path, trading compute for simplicity.
**How to avoid:** Document the expected cost increase in commit messages and code comments. The 56x headroom over real-time budget makes this a non-issue for desktop. [VERIFIED: STACK.md compute budget analysis]
**Warning signs:** Benchmark regression tests flagging a slowdown -- this is expected and should be annotated.

### Pitfall 6: Noise and FIR Interaction Change
**What goes wrong:** The noise model at 44.1kHz adds noise *after* the 8x FIR cascade + decimation (per Phase 10 design). In v1.2, noise adds after the 44.1kHz FIR. The spectral interaction is slightly different because the FIR now produces a different time-domain signal at each sample.
**Why it happens:** The FIR output is mathematically different (true interpolation vs passband approximation).
**How to avoid:** This is expected and documented. INT-04 only requires passband *frequency response* to match, not time-domain identity. The noise addition point is the same (post-FIR, pre-output), just the FIR output feeding it is different.
**Warning signs:** DAC goldens change more than expected. Measure the delta between v1.2 and v1.3 outputs with noise disabled first, then with noise enabled, to separate FIR-only and noise-interaction contributions.

## Code Examples

### Existing `dac_fir_stage_apply` -- Reused Verbatim

```c
// Source: src/spu94/spu94_dac_fir.c lines 108-129 [VERIFIED]
static int16_t dac_fir_stage_apply(const int16_t *delay, uint8_t idx,
                                   unsigned ntaps,
                                   const int16_t *coef,
                                   const unsigned (*pairs)[2],
                                   unsigned n_pairs) {
    unsigned center = ntaps / 2;
    int32_t acc = (int32_t)coef[center]
                * (int32_t)dac_fir_read_tap(delay, idx, center, ntaps);

    for (unsigned i = 0; i < n_pairs; ++i) {
        int16_t c = coef[pairs[i][0]];
        int32_t pair = (int32_t)dac_fir_read_tap(delay, idx, pairs[i][0], ntaps)
                     + (int32_t)dac_fir_read_tap(delay, idx, pairs[i][1], ntaps);
        acc += (int32_t)c * pair;
    }

    return sat_s16(acc >> 15);
}
```

### Existing Polyphase Pattern (Reverb FIR) -- Reference Only (NOT used per D-02)

```c
// Source: src/spu94/spu94_fir.c lines 257-270 [VERIFIED]
// Phase 1 of the reverb half-band interpolator: single center-tap multiply.
// This pattern demonstrates what polyphase Phase 1 looks like for half-band
// filters -- trivial, just center_tap * input. Phase 10 does NOT use this
// pattern per D-02, but it shows the established convention.
static int16_t fir_interp_phase1_apply(const int16_t delay[39], uint8_t idx,
                                       int32_t *err_acc_inout,
                                       int32_t *overflow_acc_inout) {
    int32_t acc = (int32_t)spu94_fir_coef[19] *
                  (int32_t)fir_read_tap(delay, idx, 19u);
    int32_t shifted = acc >> 15;
    // ... err tracking, overflow tracking, sat_s16 ...
    return sat_s16(shifted);
}
```

### Existing Push/Read Helpers -- Reused Verbatim

```c
// Source: src/spu94/spu94_dac_fir.c lines 89-100 [VERIFIED]
static inline int16_t dac_fir_read_tap(const int16_t *delay, uint8_t idx,
                                       unsigned k, unsigned ntaps) {
    unsigned pos = ((unsigned)idx + ntaps - 1u - k) % ntaps;
    return delay[pos];
}

static inline void dac_fir_push(int16_t *delay, uint8_t *idx,
                                int16_t sample, unsigned ntaps) {
    delay[*idx] = sample;
    *idx = (uint8_t)(((unsigned)(*idx) + 1u) % ntaps);
}
```

## State of the Art

| Old Approach (v1.2) | New Approach (v1.3) | When Changed | Impact |
|---------------------|---------------------|--------------|--------|
| All stages at 44.1kHz (passband approximation) | Stages at 88.2/176.4/352.8kHz (true interpolation) | v1.3 Phase 10 | Correct inter-sample behavior; 8x compute cost; new golden files |
| 22 multiplies per sample | 176 multiplies per sample | v1.3 Phase 10 | Still 56x real-time headroom on desktop |
| Single `spu94_dac_fir_step` | Two paths: `_step` (v1.2) + `_step_8x` (v1.3) | v1.3 Phase 10 | A/B comparison capability for Phase 11 |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Decimating by keeping the last (index 7) of 8 outputs is the correct phase alignment | Decimation Strategy | Subtle time offset in output -- detectable by impulse response test; scipy prototype resolves this before C port |
| A2 | Zero-stuffing reduces the accumulator worst case below v1.2's proven bounds | Accumulator Overflow Analysis | If wrong, int32 overflow produces corrupt audio -- mitigated by extending overflow proof test cases |
| A3 | The delay line sizes (55/11/7) are correct for elevated-rate samples without modification | State Struct Analysis | If wrong, buffer overrun -- but `_Static_assert` guards catch dimension mismatches at compile time |

**Note on A1:** This will be resolved during the scipy prototype phase (D-04). The prototype's impulse response test determines the correct decimation index empirically. Risk is LOW because the choice only affects sub-sample time alignment (fractions of a 352.8kHz period = 2.8us).

## Open Questions

1. **Which of 8 decimation outputs to keep?**
   - What we know: Any of the 8 produces correct frequency response. The choice only affects time alignment by fractions of a 352.8kHz sample.
   - What's unclear: The exact sub-sample alignment convention that matches the AK4309's analog output timing.
   - Recommendation: Resolve empirically during scipy prototype (D-04). Use impulse response peak alignment as the criterion. Default to last (index 7) per STACK.md recommendation.

2. **Exact group delay of the 8x cascade at 44.1kHz?**
   - What we know: v1.2 single-rate cascade has group delay of 35 samples (27+5+3). The 8x cascade's effective delay is different because stages operate at elevated rates.
   - What's unclear: The precise 44.1kHz-equivalent group delay after the multi-rate cascade + decimation.
   - Recommendation: Measure empirically during prototype. This affects Phase 11's latency reporting (DSP-07), not Phase 10.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (C unit tests) + pytest (Python conformance) |
| Config file | `tests/unit/dac_fir/CMakeLists.txt` (existing) |
| Quick run command | `cd build && ctest -L dac_fir --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DSP-01 | Zero-stuff to 352.8kHz | unit | `ctest -R dac_fir_8x_impulse --output-on-failure` | Wave 0 |
| DSP-02 | Stage 1 at 88.2kHz | unit | `ctest -R dac_fir_8x_impulse --output-on-failure` | Wave 0 |
| DSP-03 | Stage 2 at 176.4kHz | unit | `ctest -R dac_fir_8x_impulse --output-on-failure` | Wave 0 |
| DSP-04 | Stage 3 at 352.8kHz | unit | `ctest -R dac_fir_8x_impulse --output-on-failure` | Wave 0 |
| DSP-06 | Decimate to 44.1kHz | unit | `ctest -R dac_fir_8x_impulse --output-on-failure` | Wave 0 |
| DSP-08 | Real-time safety | unit (rt_safety) | `ctest -L rt_safety --output-on-failure` | Existing infra covers |
| INT-03 | DAC-off goldens bit-identical | conformance | `pytest tests/conformance/test_goldens_present.py -x` | Existing (needs SHA-256 assertion step before regen) |
| INT-04 | Passband matches v1.2 within 0.01dB | integration | `python3 tools/dac_filter_design.py --verify-8x` | Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build && ctest -L dac_fir --output-on-failure`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/dac_fir/test_dac_fir_8x_impulse.c` -- impulse response through 8x cascade, compare against scipy reference
- [ ] `tests/unit/dac_fir/test_dac_fir_8x_overflow_proof.c` -- zero-stuffed adversarial patterns (or extend existing `test_dac_fir_overflow_proof.c`)
- [ ] `tools/dac_filter_design.py --verify-8x` mode -- scipy prototype of naive 8x cascade
- [ ] CMakeLists.txt registration for new test TU(s)

## Sources

### Primary (HIGH confidence)
- `src/spu94/spu94_dac_fir.c` -- v1.2 implementation, accumulator width proofs, delay line convention [VERIFIED]
- `src/spu94/spu94_dac_fir_internal.h` -- stage dimensions, `_Static_assert` guards, pair table externs [VERIFIED]
- `src/spu94/spu94_dac_fir_coef.c` -- Q15 coefficient tables, DC gain measurements, symmetric pair indices [VERIFIED]
- `include/spu94/spu94_dac_fir.h` -- public state struct (152 bytes), API signature [VERIFIED]
- `src/spu94/spu94_process.c` -- DAC section integration point (lines 114-124) [VERIFIED]
- `src/spu94/spu94_state_internal.h` -- state struct layout, 1240 bytes, 15144 headroom [VERIFIED: compiled measurement]
- `src/spu94/spu94_io_chain.c` -- DAC enable/disable reset logic (lines 282-327) [VERIFIED]
- `src/spu94/spu94_dac_noise.c` -- noise model implementation, LFSR, HP shaping [VERIFIED]
- `tools/dac_filter_design.py` -- `build_composite()`, `verify_cascade()`, coefficient design [VERIFIED]
- `scripts/regenerate_goldens.py` -- golden generation CLI, DAC flags, determinism env [VERIFIED]
- `tests/conformance/test_goldens_present.py` -- 135-golden structural conformance [VERIFIED]
- `.planning/research/STACK.md` -- v1.3 stack analysis, no new dependencies, compute budget [VERIFIED]
- `.planning/research/FEATURES.md` -- table stakes vs differentiators, expected audible differences [VERIFIED]
- `.planning/research/ARCHITECTURE-v1.3.md` -- integration architecture, signal flow [VERIFIED]
- `.planning/research/PITFALLS-v1.3.md` -- accumulator overflow, golden transition, cascade ordering [VERIFIED]

### Secondary (MEDIUM confidence)
- `src/spu94/spu94_fir.c` -- reverb FIR polyphase pattern (reference only, not used per D-02) [VERIFIED]

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new dependencies, all existing tools verified
- Architecture: HIGH -- existing code is well-documented, integration point is clear, pattern is mechanical
- Pitfalls: HIGH -- derived from codebase analysis + established DSP fundamentals + v1.3 milestone research
- Scipy prototype: HIGH -- `dac_filter_design.py` already has the infrastructure, `--verify-8x` is a straightforward extension

**Research date:** 2026-04-30
**Valid until:** Indefinite (stable codebase, locked decisions, no external dependency drift)
