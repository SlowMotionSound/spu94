# Stack Research — SPU-94 v1.3 True 8x Oversampled DAC

**Domain:** True 8x oversampling of existing AK4309 interpolation filter model in a C99 fixed-point DSP library.
**Researched:** 2026-04-30
**Confidence:** HIGH -- this is internal restructuring of existing validated code, not new algorithm design.

**Scope note:** This document covers only what changes for v1.3 true oversampling. The existing v1.2 stack (C11, CMake, pytest, ctypes, scipy, same filter coefficients) is validated and unchanged. See STACK.md commit history for prior decisions.

---

## 1. What v1.2 Does vs. What v1.3 Must Do

### v1.2 (current, shipped)

All three cascade stages run at 44.1kHz. One input sample produces one output sample through the cascade. The frequency response matches the AK4309 passband ripple character *as seen at 44.1kHz*, but does not actually perform interpolation -- it is a "passband-equivalent" coloration filter.

```
Input (44.1kHz) -> Stage1 (44.1kHz) -> Stage2 (44.1kHz) -> Stage3 (44.1kHz) -> Output (44.1kHz)
```

22 multiplies per sample. Delay lines hold 44.1kHz samples.

### v1.3 (target)

Zero-stuff between each stage. Each stage runs at its true operating rate. The cascade performs genuine 8x interpolation to 352.8kHz, then decimates back to 44.1kHz.

```
Input (44.1kHz)
  -> zero-stuff -> Stage1 (88.2kHz, 2 evals)
    -> zero-stuff -> Stage2 (176.4kHz, 4 evals)
      -> zero-stuff -> Stage3 (352.8kHz, 8 evals)
        -> decimate (pick 1 of 8) -> Output (44.1kHz)
```

70 multiplies per sample. Delay lines hold rate-appropriate samples (mix of real + zero-stuffed).

---

## 2. Stack Delta: Nothing New

### No New Dependencies

| Candidate | Why Not Needed |
|-----------|----------------|
| libsamplerate / libsoxr | We are modeling a specific DAC's fixed FIR cascade, not doing generic SRC |
| FFTW / KissFFT | FIR lengths (55/11/7 taps) are too short for FFT convolution; direct form wins |
| int64_t accumulators | Existing int32 overflow proofs still hold with zero-stuffed input (demonstrated below) |
| New filter coefficients | Same AK4309 half-band coefficients from v1.2; the math is identical |
| New scipy features | scipy 1.17.1's `remez` and `freqz` are sufficient; no new APIs needed |
| Intermediate heap buffers | Sample-by-sample cascade uses only stack locals (28 bytes max) |

### No Changed Dependencies

| Component | Version | Status |
|-----------|---------|--------|
| C99/C11 (gcc 14+) | unchanged | Same compiler, same flags |
| scipy | 1.17.1 | No new features needed |
| numpy | 2.2.4 | No new features needed |
| pytest | existing | New goldens use same harness |
| Unity | existing | New overflow proofs use same framework |
| matplotlib | existing | Comparison plots (v1.2 vs v1.3 response) |
| CMake | existing | No new source files beyond modifying existing ones |

---

## 3. Key Technical Decisions

### 3.1 Processing Architecture: Explicit Zero-Stuff (Not Polyphase)

**Use explicit zero-stuff-and-filter. Do not use polyphase decomposition.**

Polyphase decomposition would reduce multiplies from 70 to 35 per input sample (50% savings) by exploiting the half-band zero structure. But:

- The existing `dac_fir_stage_apply` folded-form function works unchanged with zero-stuffed input. Just push (sample, 0) pairs and call the same function twice per stage-transition.
- 70 multiplies at 44.1kHz takes ~160ns on modern x86 (the per-sample budget is 22,676ns). Over 100x headroom.
- Polyphase requires rewriting the filter evaluation into two separate branch functions per stage, doubling the code surface for no audible benefit.
- The explicit zero-stuff form directly corresponds to what the AK4309 hardware does: it literally inserts zeros and filters.

**State struct impact:** None. The delay lines (`stage1_delay[55]`, `stage2_delay[11]`, `stage3_delay[7]`) hold 88.2kHz / 176.4kHz / 352.8kHz samples respectively -- a mix of real values and zeros. Same array sizes, same circular buffer indices.

### 3.2 Accumulator Width: int32 Remains Sufficient

**No promotion to int64 needed.**

The existing overflow proofs in `spu94_dac_fir.c` bound worst-case accumulator values:

| Stage | v1.2 Worst Case | v1.3 Worst Case | INT32_MAX |
|-------|-----------------|-----------------|-----------|
| Stage 1 | 1,904,643,762 | Lower (half the delay entries are zero) | 2,147,483,647 |
| Stage 2 | 1,336,455,240 | Lower | 2,147,483,647 |
| Stage 3 | 1,221,048,126 | Lower | 2,147,483,647 |

Zero-stuffing guarantees that every other delay-line entry is 0. The folded-form pairs where one partner is 0 contribute nothing to the accumulator. The worst case is strictly less than v1.2's proof, which already fits in int32 with >1dB headroom.

**Validation plan:** Extend `test_dac_fir_overflow_proof.c` to exercise the alternating (INT16_MIN, 0) pattern in delay lines. This is a new test case, not a new test file.

### 3.3 Decimation: Trivial, No Additional Filter

**Decimate by retaining the last of 8 output samples. No decimation filter.**

Why no filter:
1. The input signal arrives at 44.1kHz -- it has no energy above 22.05kHz.
2. The interpolation cascade suppresses interpolation images by >41dB (verified by `dac_filter_design.py --verify`).
3. When decimating 8:1, the images that would alias back are already suppressed.
4. The worst-case alias level is below -41dB, which is below the AK4309's own noise floor.

Which of the 8 samples to keep: the last one (index 7). This corresponds to the "most recently computed" sample and maintains proper time alignment. The exact choice affects only the group delay by a fraction of a 352.8kHz sample period (2.8us), which is inaudible.

### 3.4 API: New Function Alongside Existing

**Add `spu94_dac_fir_step_8x` alongside the existing `spu94_dac_fir_step`.**

```c
/* v1.2: runs cascade at 44.1kHz (passband-equivalent coloration) */
int16_t spu94_dac_fir_step(spu94_dac_fir_state *state, int16_t input);

/* v1.3: true 8x zero-stuff + cascade + decimate */
int16_t spu94_dac_fir_step_8x(spu94_dac_fir_state *state, int16_t input);
```

Same return type, same state struct, same signature. The `_step_8x` function internally:
1. Pushes `input` then `0` into stage 1 delay line, evaluates stage 1 twice -> 2 outputs
2. For each stage 1 output: pushes it then `0` into stage 2, evaluates twice -> 4 outputs total
3. For each stage 2 output: pushes it then `0` into stage 3, evaluates twice -> 8 outputs total
4. Returns the 8th output (decimated)

The v1.2 `_step` function is kept for A/B comparison and backward compatibility.

### 3.5 Noise Model: Clock at 352.8kHz When Oversampled

**The delta-sigma noise model must clock at 8x rate when true oversampling is active.**

In the real AK4309, delta-sigma quantization noise is generated at the oversampled rate and shaped by the noise transfer function (1 - z^-1)^2. When the signal is reconstructed (analog filtering), the in-band noise is determined by the noise shaping at 352.8kHz, not at 44.1kHz.

v1.3 approach:
- Call `spu94_dac_noise_step` 8 times per 44.1kHz input sample (once per 352.8kHz output)
- Add noise to each interpolated sample before decimation
- The LFSR and HP shaping coefficients are unchanged
- The in-band noise spectrum will be different from v1.2 because 8x more noise bandwidth folds into 0-22.05kHz during decimation

**Recalibration needed:** The amplitude constant in `spu94_dac_noise_step` may need adjustment. At 8x rate, 8x more noise bandwidth aliases into the audio band during decimation, raising the in-band noise floor by approximately 9dB (10*log10(8)). The target is still the AK4309's rated 90dB dynamic range. This is a scipy prototyping task (simulate 352.8kHz noise, decimate, measure in-band RMS, adjust amplitude).

**State struct impact on noise:** None. `spu94_dac_noise_state` (LFSR + two int16 history samples) is unchanged. It just gets called more often.

### 3.6 State Struct: One New Toggle

**Add `uint8_t dac_oversampled` to `spu94_state`.**

```c
/* In spu94_state_internal.h, DAC section (after dac_noise_enabled) */
uint8_t        dac_oversampled;    /* 0=v1.2 at-rate (default), 1=true 8x */
```

One byte. Current `sizeof(spu94_state)` is well under the 16,384-byte `SPU94_STATE_SIZE_MAX` ceiling. No bump needed.

`spu94_process.c` dispatch:
```c
if (state->dac_oversampled) {
    // True 8x path: FIR + noise interleaved at 352.8kHz
    out_l = spu94_dac_step_8x_with_noise(state, out_l, /*channel=*/0);
    out_r = spu94_dac_step_8x_with_noise(state, out_r, /*channel=*/1);
} else {
    // v1.2 path: at-rate FIR then noise (existing code)
    if (state->dac_fir_enabled) { ... }
    if (state->dac_noise_enabled) { ... }
}
```

---

## 4. Buffer and Memory Analysis

### No Intermediate Buffers Needed

The sample-by-sample cascade uses only stack-local variables:

```
Stack frame for spu94_dac_fir_step_8x:
  2 stage-1 outputs:  int16_t s1[2]     =  4 bytes
  4 stage-2 outputs:  int16_t s2[4]     =  8 bytes
  8 stage-3 outputs:  int16_t s3[8]     = 16 bytes  (or just 1 if only keeping last)
  Loop indices:       3 x uint8_t       =  3 bytes
  Total:              ~31 bytes on stack (or ~7 bytes with scalar decimation)
```

No heap allocation. No block-level intermediate buffers. The function processes one 44.1kHz input sample completely before returning.

### State Struct Size Impact

| Component | v1.2 Size | v1.3 Size | Delta |
|-----------|-----------|-----------|-------|
| `spu94_dac_fir_state` (x2 channels) | 2 x 149 bytes | 2 x 149 bytes | 0 |
| `spu94_dac_noise_state` (x2 channels) | 2 x 8 bytes | 2 x 8 bytes | 0 |
| `dac_oversampled` toggle | 0 | 1 byte | +1 byte |
| **Total DAC section delta** | | | **+1 byte** |

### Compute Budget

| Metric | v1.2 | v1.3 (8x) | Budget (44.1kHz) |
|--------|------|-----------|------------------|
| FIR multiplies per sample | 22 | 70 | n/a |
| FIR evaluations per sample | 3 | 14 | n/a |
| Noise steps per sample | 1 | 8 | n/a |
| Estimated time per sample | ~50ns | ~200ns | 22,676ns |
| CPU headroom | ~450x | ~113x | real-time safe |

Even on a Cortex-M4 at 168MHz (future MCU target), 70 Q15 MAC operations take ~2us -- well within the 22.7us per-sample budget.

---

## 5. Design Tool Changes (tools/dac_filter_design.py)

### Keep Everything, Add One Mode

**Existing functionality (unchanged):**
- `design_halfband_stage` -- same coefficients, same design
- `quantize_to_q15` -- same quantization
- `build_composite` -- already models the 8x composite response correctly
- `verify_cascade` -- already verifies at 352.8kHz
- `--export-c` -- same coefficient output
- `--verify` -- same pass/fail checks

**New functionality (add):**
- `--verify-8x` mode: simulate the actual zero-stuff + cascade + decimate processing in Python (sample-by-sample, matching the C implementation), measure frequency response, compare against the analytical composite. This validates that zero-stuffing + existing FIR + decimation produces the expected response.
- Noise recalibration helper: generate noise at 352.8kHz using the existing LFSR + HP shaping model, decimate to 44.1kHz, measure in-band RMS, report amplitude adjustment factor.

**No scipy version upgrade needed.** scipy 1.17.1's `remez` and `freqz` handle everything. The `upfirdn` function could simplify the verification script but is not required.

---

## 6. What NOT to Change

| Component | Why Leave Alone |
|-----------|-----------------|
| Filter coefficient values | Same AK4309 half-band design; filters are correct |
| `spu94_fir.c` (39-tap reverb FIR) | Unrelated to DAC; different filter at different point in signal chain |
| `spu94_dac_fir_state` struct layout | Same delay lines, same indices, same sizeof |
| `spu94_dac_noise_state` struct layout | Same LFSR, same HP shaping; called more often, not differently |
| `spu94_dac_fir_coef.c` | Verbatim reuse of v1.2 coefficients |
| `dac_fir_stage_apply` static function | Reused as-is for the zero-stuffed evaluation |
| `dac_fir_push` / `dac_fir_read_tap` helpers | Reused as-is |
| `SPU94_STATE_SIZE_MAX` | 1-byte addition is negligible |
| `spu94_dac_fir_step` (v1.2 function) | Keep for A/B comparison; do not remove or modify |
| Python ctypes binding structure | Add one toggle accessor; no structural change |
| JUCE GUI layout | Add toggle/radio for oversampled mode; fits existing DAC control zone |

---

## 7. Files That Change

| File | Change | Scope |
|------|--------|-------|
| `src/spu94/spu94_dac_fir.c` | Add `spu94_dac_fir_step_8x` function | ~30-40 new LOC |
| `include/spu94/spu94_dac_fir.h` | Declare `spu94_dac_fir_step_8x` | 3 lines |
| `src/spu94/spu94_state_internal.h` | Add `dac_oversampled` field | 1 line |
| `src/spu94/spu94_process.c` | Branch on `dac_oversampled` in DAC section | ~15 LOC |
| `tools/dac_filter_design.py` | Add `--verify-8x` mode | ~60 LOC |
| `tools/dac_measure.py` | Add 8x measurement path | ~40 LOC |
| `tests/unit/dac_fir/test_dac_fir_overflow_proof.c` | Add zero-stuffed overflow proof cases | ~30 LOC |
| `python/spu94/*.py` | Add `dac_oversampled` toggle accessor | ~5 LOC |
| `src/standalone/*.cpp` | Add toggle in DAC GUI zone | ~10 LOC |
| **Total estimated new/changed LOC** | | **~200 LOC** |

---

## 8. Confidence Summary

| Claim | Confidence | Basis |
|-------|------------|-------|
| No new dependencies | HIGH | All operations are existing Q15 MAC + existing LFSR |
| int32 accumulator sufficient | HIGH | Zero-stuffing reduces worst case below existing proof bounds |
| Same coefficients work at elevated rate | HIGH | `dac_filter_design.py --verify` already validates composite at 352.8kHz |
| Naive zero-stuff over polyphase | HIGH | Correctness-equivalent; 70 muls is trivially fast; code reuse |
| No decimation filter needed | HIGH | Input bandlimited; cascade provides >41dB image rejection |
| Noise recalibration needed | MEDIUM | Math is straightforward but exact amplitude needs measurement |
| ~200 LOC total delta | MEDIUM | Depends on test depth and how much A/B tooling is added |

---

## Sources

- `src/spu94/spu94_dac_fir.c` -- existing implementation, accumulator width proofs (lines 30-80)
- `src/spu94/spu94_dac_fir_internal.h` -- stage dimensions, pair tables
- `src/spu94/spu94_dac_fir_coef.c` -- Q15 coefficients, symmetric pair indices
- `src/spu94/spu94_process.c` -- DAC section integration point (lines 115-128)
- `src/spu94/spu94_state_internal.h` -- state struct layout, size ceiling
- `tools/dac_filter_design.py` -- filter design, composite verification at 352.8kHz
- `tools/dac_measure.py` -- frequency response characterization
- `include/spu94/spu94_dac_noise.h` -- noise model state, API
- AK4309B datasheet specs embedded in `dac_filter_design.py` (passband ripple, stopband rejection)

---

*Stack research for: true 8x oversampling of AK4309 interpolation filter in libspu94.*
*Researched: 2026-04-30*
