# Phase 6: DAC Core Implementation - Research

**Researched:** 2026-04-28
**Domain:** Q15 fixed-point FIR filter implementation + LFSR noise shaping in C
**Confidence:** HIGH

## Summary

Phase 6 ports the Phase 5 scipy-verified AK4309 interpolation filter coefficients to C as three cascaded half-band FIR stages, and implements a 2nd-order highpass-shaped noise model for the delta-sigma quantization noise floor. Both modules are standalone (no `spu94_state` dependency) following the ADPCM precedent.

The critical technical finding is that **int32 accumulators are sufficient for all three FIR stages** -- Stage 1 (55 taps) has 1.04 dB headroom, Stage 2 (11 taps) has 4.12 dB, Stage 3 (7 taps) has 4.90 dB. The folded-form + zero-skip optimization yields 15 + 4 + 3 = 22 multiplies total (one more than the 21 estimated in CONTEXT.md D-01 because Stage 1 has 14 pairs + 1 center = 15, not the estimated 14). The existing `spu94_fir.c` pattern -- circular buffer delay line, `fir_read_tap`/`fir_push` helpers, accumulator width proof block comment, `sat_s16` at the end -- transfers directly to the new stages with only dimension changes.

The noise model uses a maximal-length LFSR for white noise and a 2nd-order difference equation (`y[n] = x[n] - 2*x[n-1] + x[n-2]`) for the highpass shaping, producing the +12 dB/octave spectral slope specified in D-06. The amplitude is scaled to place the noise floor at approximately -90 dB relative to full scale, matching the AK4309B datasheet dynamic range.

**Primary recommendation:** Follow `spu94_fir.c` and `spu94_adpcm.c` patterns exactly. The three FIR stages share a single process function that cascades them internally. The noise module is a separate file with its own state struct. Both modules are stateless from the caller's perspective except for their opaque state structs.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Skip zeros AND fold symmetry, matching the existing `spu94_fir.c` folded-form discipline. ~21 multiplies total across all three stages. Proven pattern, best cycle count for future MCU/FPGA port.
- **D-02:** Each stage needs its own accumulator width proof (pre-added pairs change worst-case bounds). Follow the same analytic+empirical validation pattern as `spu94_fir.c`'s D-02 proof.
- **D-03:** Coefficients come directly from Phase 5's `python3 tools/dac_filter_design.py --export-c` output. No manual transcription -- copy the Q15 hex values verbatim.
- **D-04:** Noise amplitude calibrated to produce ~90dB SNR per the AK4309B datasheet spec. This is a paper target -- the datasheet number includes analog stages we don't model. Documented as a placeholder value.
- **D-05:** M5 hardware captures (DAC-HW-01 through DAC-HW-03) will refine the amplitude against real PS1 measurements. The current value is a reasonable starting point, not a final calibration.
- **D-06:** LFSR + 2nd-order HP shaping, +12dB/octave slope. The shaping character matters more than the absolute amplitude -- get the spectral shape right, treat the level as tunable later.
- **D-07:** Two separate files: `spu94_dac_fir.c` (interpolation filter + coefficient tables) and `spu94_dac_noise.c` (LFSR + 2nd-order shaping). Each has its own state struct, header, and unit tests.
- **D-08:** Phase 7 composes them (no compile-time dependency).
- **D-09:** Follows ADPCM precedent (separate files, independent state).

### Claude's Discretion
- LFSR polynomial choice (any maximal-length, documented in source)
- Internal organization of the three FIR stages within `spu94_dac_fir.c`
- Test structure and specific test cases beyond what CONTEXT.md specifies

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| DAC-FILT-02 | Interpolation filter implemented in C as Q15 fixed-point FIR at 44.1kHz, reproducing the top-octave passband ripple character of the AK4309's cascaded half-band interpolator | Accumulator width proofs confirm int32 sufficiency for all three stages; coefficient tables verified from Phase 5's --export-c; folded-form pattern from spu94_fir.c transfers directly |
| DAC-NOISE-01 | 2nd-order shaped noise model matching AK4309's delta-sigma modulator characteristics -- LFSR source with 2nd-order highpass shaping producing +12dB/octave spectral slope, calibrated to ~90dB dynamic range at 384x OSR | LFSR polynomial selected (maximal-length 32-bit); 2nd-order difference equation produces correct spectral slope; amplitude derivation from datasheet DR spec documented |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Interpolation filter (FIR) | C library (`libspu94`) | -- | Pure DSP computation, same tier as existing `spu94_fir.c` |
| Noise model (LFSR + shaping) | C library (`libspu94`) | -- | Pure DSP computation, stateful but standalone |
| Coefficient generation | Python tooling (`tools/`) | -- | Already exists in Phase 5; C consumes verbatim output |
| Bit-identity verification | Python + C test | -- | Python scipy reference compared against C output |
| Accumulator width proofs | C unit test + source comment | -- | Analytic proof in comment block, empirical proof in test |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C11 (project standard) | gcc/clang | Filter and noise implementation | Project constraint from PROJECT.md |
| Unity | vendored | Unit test framework | Already used for all existing C tests |
| Q15 primitives (`spu94_q15.h`) | project | Fixed-point arithmetic | `sat_s16`, `q15_mul_truncate` -- established project infrastructure |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| scipy.signal | system | Reference filter implementation for bit-identity tests | Test verification only, not production |
| numpy | system | Test data generation and comparison | Test verification only |

No new dependencies. Phase 6 uses only what the project already has.

## Architecture Patterns

### System Architecture Diagram

```
Phase 5 output                Phase 6 modules                   Phase 7 (future)
                                                                 
tools/dac_filter_design.py    spu94_dac_fir.c                   spu94_process()
  |                             |                                  |
  | --export-c (Q15 hex)       [Stage 1: 55-tap half-band]        reads 44.1kHz
  |                             | 14 pairs + 1 center = 15 mul    sample from
  v                             v                                  fir_chain_step
spu94_dac_fir_coef.c          [Stage 2: 11-tap half-band]          |
  (static const tables)         | 3 pairs + 1 center = 4 mul      v
                                v                                calls
                              [Stage 3: 7-tap half-band]         spu94_dac_fir_step()
                                | 2 pairs + 1 center = 3 mul      |
                                v                                  v
                              output (filtered sample)           calls
                                                                 spu94_dac_noise_step()
                                                                   |
spu94_dac_noise.c                                                  v
  |                                                              output
  [32-bit LFSR] -> white noise
  |
  [2nd-order HP: y = x - 2*x1 + x2]
  |
  [amplitude scale to -90dB]
  |
  v
  shaped noise sample (added to signal in Phase 7)
```

### Recommended Project Structure

```
src/spu94/
  spu94_dac_fir.c          # Three cascaded half-band FIR stages + process function
  spu94_dac_fir_coef.c     # Coefficient tables (verbatim from --export-c)
  spu94_dac_fir_internal.h # Internal header: stage functions, coef externs, state
include/spu94/
  spu94_dac_fir.h          # Public: state struct, spu94_dac_fir_init, spu94_dac_fir_step
  spu94_dac_noise.h        # Public: state struct, spu94_dac_noise_init, spu94_dac_noise_step
src/spu94/
  spu94_dac_noise.c        # LFSR + 2nd-order HP shaping
tests/unit/dac_fir/
  test_dac_fir_coef_table.c    # SHA-256 pinning of coefficient tables
  test_dac_fir_bit_identity.c  # Folded == literal under clamp-once
  test_dac_fir_overflow_proof.c # int32 accumulator width proof (3 stages)
  test_dac_fir_dc_gain.c       # DC gain verification per stage
  test_dac_fir_impulse.c       # Impulse response vs scipy reference
tests/unit/dac_noise/
  test_dac_noise_spectral.c    # +12 dB/octave slope verification
  test_dac_noise_amplitude.c   # RMS level vs -90dB target
  test_dac_noise_lfsr.c        # LFSR period and distribution tests
```

### Pattern 1: Folded-Form Half-Band FIR (from `spu94_fir.c`)

**What:** Exploit half-band zero structure and coefficient symmetry to minimize multiplies.
**When to use:** Every half-band FIR stage in Phase 6.
**Example:**

```c
// Source: src/spu94/spu94_fir.c lines 98-170 (existing production pattern)
// Adapted for a generic N-tap half-band stage

static int16_t dac_fir_folded_apply(const int16_t *delay, uint8_t idx,
                                     unsigned ntaps,
                                     const int16_t *coef,
                                     const unsigned (*pairs)[2],
                                     unsigned n_pairs) {
    unsigned center = ntaps / 2;
    int32_t acc = (int32_t)coef[center] * (int32_t)dac_fir_read_tap(delay, idx, center, ntaps);
    
    for (unsigned i = 0; i < n_pairs; ++i) {
        int16_t c = coef[pairs[i][0]];
        /* Pre-add symmetric pair before multiply (D-01 fold) */
        int32_t pair = (int32_t)dac_fir_read_tap(delay, idx, pairs[i][0], ntaps)
                     + (int32_t)dac_fir_read_tap(delay, idx, pairs[i][1], ntaps);
        acc += (int32_t)c * pair;
    }
    
    /* D-03 clamp-once: single shift + sat_s16 at the end */
    return sat_s16(acc >> 15);
}
```

### Pattern 2: Standalone Module (from `spu94_adpcm.c`)

**What:** Self-contained state struct, no `spu94_state` dependency, public header in `include/spu94/`.
**When to use:** Both `spu94_dac_fir` and `spu94_dac_noise` modules.
**Example:**

```c
// Source: include/spu94/spu94_adpcm.h (existing pattern)

// Public header: include/spu94/spu94_dac_fir.h
typedef struct {
    int16_t stage1_delay[55];
    uint8_t stage1_idx;
    int16_t stage2_delay[11];
    uint8_t stage2_idx;
    int16_t stage3_delay[7];
    uint8_t stage3_idx;
} spu94_dac_fir_state;

/* Process one 44.1kHz sample through the three-stage cascade.
 * Returns the filtered sample. State carries across calls. */
int16_t spu94_dac_fir_step(spu94_dac_fir_state *state, int16_t input);

/* Zero-initialize state. Equivalent to memset(state, 0, sizeof(*state)). */
void spu94_dac_fir_init(spu94_dac_fir_state *state);
```

### Pattern 3: LFSR + 2nd-Order HP Noise Shaping

**What:** White noise from LFSR, shaped by a 2nd-order difference equation to produce +12 dB/octave rising spectrum.
**When to use:** `spu94_dac_noise.c`.
**Example:**

```c
// 2nd-order highpass shaping: y[n] = x[n] - 2*x[n-1] + x[n-2]
// This is the discrete derivative of a 1st-order HP, producing +12 dB/oct slope.
// Equivalent to NTF(z) = (1 - z^-1)^2 from delta-sigma theory.
// Source: .planning/research/DEEP-DELTA-SIGMA.md Section 1

typedef struct {
    uint32_t lfsr;      /* 32-bit LFSR state */
    int16_t  x_prev;    /* x[n-1] for HP shaping */
    int16_t  x_prev2;   /* x[n-2] for HP shaping */
} spu94_dac_noise_state;

int16_t spu94_dac_noise_step(spu94_dac_noise_state *state) {
    /* Generate white noise sample from LFSR */
    uint32_t lfsr = state->lfsr;
    /* Galois form: x^32 + x^22 + x^2 + x^1 + 1 (maximal-length) */
    uint32_t bit = lfsr & 1u;
    lfsr >>= 1;
    if (bit) lfsr ^= 0x80200003u;  /* taps at 32, 22, 2, 1 */
    state->lfsr = lfsr;
    
    /* Scale LFSR output to noise amplitude (~-90 dB RMS) */
    int16_t x = (int16_t)((int32_t)(lfsr & 0xFFFF) - 32768) >> NOISE_SHIFT;
    
    /* 2nd-order HP: y = x - 2*x1 + x2 */
    int32_t y = (int32_t)x - 2 * (int32_t)state->x_prev + (int32_t)state->x_prev2;
    state->x_prev2 = state->x_prev;
    state->x_prev = x;
    
    return sat_s16(y);
}
```

### Anti-Patterns to Avoid
- **Simulating the full delta-sigma modulator at 384x OSR:** Would require 16.9 MHz processing for zero audible benefit. Model the EFFECT (noise spectrum shape), not the MECHANISM. [CITED: .planning/research/DEEP-DELTA-SIGMA.md Section 9]
- **Using float in the hot path:** Breaks the float-free CI gate and is inconsistent with the project's C99/no-heap discipline.
- **Manual coefficient transcription:** Always use `--export-c` output verbatim. Manual hex typing invites transcription errors.
- **Inserting the filter at 22.05 kHz:** The DAC sees the 44.1 kHz output. The filter operates at 44.1 kHz. [CITED: .planning/research/PITFALLS-v1.2.md C3]

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Half-band FIR | New filter algorithm | Adapt existing `spu94_fir.c` folded-form pattern | Proven pattern with accumulator width proof discipline, bit-identity test infrastructure |
| Q15 arithmetic | Custom fixed-point | `spu94_q15.h` primitives (`sat_s16`, `q15_mul_truncate`) | Established project infrastructure with compile-time ASR assertion |
| Coefficient generation | Hand-calculated Q15 values | `tools/dac_filter_design.py --export-c` | Already verified against datasheet specs in Phase 5 |
| Random number generation | Custom RNG | Well-known maximal-length LFSR polynomial | Decades of established theory; any maximal-length polynomial is correct |
| Test framework | Custom test runner | Unity (vendored) + ctest | Already used for 100+ existing tests |

## Common Pitfalls

### Pitfall 1: Stage 1 Accumulator Headroom is Tight (1.04 dB)

**What goes wrong:** Stage 1's 55-tap half-band FIR has only 1.04 dB of int32 accumulator headroom under worst-case adversarial input. If a future modification adds any additional accumulation (e.g., dither, error feedback), the margin could be exceeded.
**Why it happens:** Stage 1 has 29 non-zero coefficients with large magnitudes (center tap 0x4000 = 16384, largest pair coefficient 0x28A9 = 10409). The sum of absolute coefficient values is 58,126, which times 32,768 (|INT16_MIN|) = 1,904,672,768, only 1.04 dB below INT32_MAX.
**How to avoid:** Document the tight margin in the accumulator width proof comment block. If any future modification requires additional accumulation headroom for Stage 1, promote to int64 accumulator (per the existing D-02 seam in `spu94_fir.c`'s comment block). For Phase 6 as scoped, int32 is sufficient.
**Warning signs:** UBSan signed-overflow trap in Stage 1 during adversarial testing.

### Pitfall 2: Half-Band Zero Enforcement Must Be Structural

**What goes wrong:** The scipy remez optimizer leaves residuals of ~1e-6 at positions that should be exactly zero. After Q15 quantization, these become non-zero integers (typically +/-1). If the coefficient table includes these residual values, the filter is no longer half-band and the zero-skip optimization produces wrong results.
**Why it happens:** Phase 5 discovered this (05-01-SUMMARY.md deviation #2): remez residuals at ~1e-6 were above the original 1e-10 threshold. The fix was structural zeroing: explicitly zero all odd-indexed coefficients except center.
**How to avoid:** The `--export-c` output already has structural zeros enforced. Verify the coefficient table by asserting all odd-indexed entries (except center) are exactly 0x0000.
**Warning signs:** Non-zero values at odd indices in the coefficient table; multiply count exceeds expected 22 total.

### Pitfall 3: Circular Buffer Dimension Must Match Filter Length

**What goes wrong:** Each stage's delay line must be exactly `ntaps` entries long. The existing 39-tap FIR uses `delay[39]` everywhere. If a new stage accidentally uses the wrong dimension (e.g., copy-paste `delay[39]` instead of `delay[55]`), the circular buffer wraps at the wrong point, corrupting the filter output silently.
**Why it happens:** Copy-paste from `spu94_fir.c` without updating all dimension constants.
**How to avoid:** Use `#define` constants for each stage's tap count. Use `_Static_assert` to verify delay line dimensions match coefficient table dimensions.
**Warning signs:** Filter output does not match scipy reference for inputs longer than the shortest tap count.

### Pitfall 4: Noise LFSR Must Be Non-Zero Initialized

**What goes wrong:** An LFSR initialized to all zeros stays at zero forever (zero is an absorbing state for Galois-form LFSRs). The noise generator produces silence.
**Why it happens:** Zero-initialization (memset to 0) is the standard pattern for state structs in this project.
**How to avoid:** The `spu94_dac_noise_init()` function must set the LFSR state to a non-zero seed value. Document that `memset(state, 0, sizeof(*state))` is NOT equivalent to `spu94_dac_noise_init()` -- this is the one exception to the ADPCM precedent where zero-init is correct.
**Warning signs:** Noise output is all zeros; spectral analysis shows no noise floor.

### Pitfall 5: The Three Stages Operate at 44.1 kHz, Not at Increasing Rates

**What goes wrong:** In the real AK4309, each stage upsamples: Stage 1 operates at 88.2 kHz, Stage 2 at 176.4 kHz, Stage 3 at 352.8 kHz. But the Phase 6 implementation operates at **44.1 kHz only** -- we are modeling the filter's passband ripple character at the audio rate, not simulating the actual upsampling process. If someone tries to implement the upsampling stages, they would need to process 8x as many samples per audio sample, which is unnecessary and contradicts the Phase 5/6 design intent.
**Why it happens:** Confusion between "modeling the AK4309's filter effect" and "simulating the AK4309's internal signal processing."
**How to avoid:** The three stages cascade at 44.1 kHz. Each processes one sample per call. The upsampling is already accounted for in the coefficient design (Phase 5 designed each stage's coefficients at its operating rate, and the composite response was verified at the final rate). At 44.1 kHz, the cascade reproduces the same passband ripple character. [VERIFIED: Phase 5 --verify confirms composite response meets specs]
**Warning signs:** Filter processing takes 8x longer than expected; sample rate changes between stages.

### Pitfall 6: DC Gain Normalization

**What goes wrong:** Each stage's Q15 coefficients sum to slightly less than 0x8000 (1.0 in Q15):
- Stage 1: sum = 0x7F8E (DC gain = 0.9965, -0.030 dB)
- Stage 2: sum = 0x801A (DC gain = 1.0008, +0.007 dB)
- Stage 3: sum = 0x7FF4 (DC gain = 0.9996, -0.003 dB)

Cascaded: approximately -0.027 dB total DC gain. This matches the existing project convention -- the v1.0 SPU half-band FIR's DC gain of ~0.9999 is also not compensated (documented in `spu94_fir.c` comment block: "DC gain per 04-RESEARCH section 5... Not compensated (bit-faithful)").
**Why it happens:** Q15 quantization of floating-point coefficients introduces rounding error in the DC gain.
**How to avoid:** Do NOT add gain compensation. The existing project convention is to not compensate DC gain in filters. Document the per-stage DC gain in the source comment block. The composite -0.027 dB is inaudible.
**Warning signs:** Someone adds a gain normalization step that changes the filter's bit-exact behavior.

## Code Examples

### Coefficient Table Layout (from `--export-c` output)

```c
// Source: python3 tools/dac_filter_design.py --export-c [VERIFIED: run on 2026-04-28]

/* 55-tap half-band FIR, Q15 (29 non-zero) */
static const int16_t dac_interp_stage1[55] = {
    0xFFA4, 0x0000, 0x0056, 0x0000, 0xFF82, 0x0000, 0x00B0, 0x0000,
    0xFF10, 0x0000, 0x0141, 0x0000, 0xFE59, 0x0000, 0x022C, 0x0000,
    0xFD25, 0x0000, 0x03D0, 0x0000, 0xFAC0, 0x0000, 0x07BB, 0x0000,
    0xF2AC, 0x0000, 0x28A9, 0x4000, 0x28A9, 0x0000, 0xF2AC, 0x0000,
    0x07BB, 0x0000, 0xFAC0, 0x0000, 0x03D0, 0x0000, 0xFD25, 0x0000,
    0x022C, 0x0000, 0xFE59, 0x0000, 0x0141, 0x0000, 0xFF10, 0x0000,
    0x00B0, 0x0000, 0xFF82, 0x0000, 0x0056, 0x0000, 0xFFA4,
};

/* 11-tap half-band FIR, Q15 (7 non-zero) */
static const int16_t dac_interp_stage2[11] = {
    0x0171, 0x0000, 0xF832, 0x0000, 0x266B, 0x3FFE, 0x266B, 0x0000,
    0xF832, 0x0000, 0x0171,
};

/* 7-tap half-band FIR, Q15 (5 non-zero) */
static const int16_t dac_interp_stage3[7] = {
    0xFB99, 0x0000, 0x2461, 0x4000, 0x2461, 0x0000, 0xFB99,
};
```

### Accumulator Width Proof Comment Block (template for each stage)

```c
// Source: src/spu94/spu94_fir.c lines 32-67 (existing pattern, adapted for Stage 1)

/* ========================================================================
 * Accumulator Width Proof -- Stage 1 (55-tap half-band)
 *
 * Folded form: center tap + 14 symmetric pairs. Pre-added pairs change
 * the worst-case input range from int16 to int17 (sum of two int16 values).
 *
 * Center tap: |0x4000| * |INT16_MIN| = 16384 * 32768 = 536,870,912
 * 14 pairs: sum(|coef[k]| * max(|x[k]+x[N-1-k]|)) for non-zero k
 *   = sum(|coef[k]| * 65536) for negative coefs
 *   + sum(|coef[k]| * 65534) for positive coefs
 *   = 1,367,772,850  (computed by tools/dac_filter_design.py)
 *
 * Total worst-case: 536,870,912 + 1,367,772,850 = 1,904,643,762 = 0x71868EB2
 * INT32_MAX: 2,147,483,647 = 0x7FFFFFFF
 * Headroom: 1.04 dB (0.17 bits)
 *
 * int32 is sufficient. If a future modification tightens the margin below
 * zero, promote to int64 per the D-02 seam.
 *
 * Validated empirically by tests/unit/dac_fir/test_dac_fir_overflow_proof.c.
 * ========================================================================
 */
```

### CMakeLists.txt Pattern for Adding New Source Files

```cmake
# Source: src/spu94/CMakeLists.txt (add after spu94_adpcm_encode.c)

add_library(spu94_obj OBJECT
    # ... existing sources ...
    spu94_adpcm_encode.c
    vag.c
    spu94_dac_fir.c        # Phase 6: DAC interpolation filter
    spu94_dac_fir_coef.c   # Phase 6: DAC filter coefficient tables
    spu94_dac_noise.c      # Phase 6: DAC noise model
)
```

### Test CMakeLists.txt Pattern

```cmake
# Source: tests/unit/fir/CMakeLists.txt (pattern for new test subdirectory)

# tests/unit/dac_fir/CMakeLists.txt
add_executable(test_dac_fir_coef_table test_dac_fir_coef_table.c)
target_link_libraries(test_dac_fir_coef_table PRIVATE unity spu94_static)
add_test(NAME dac_fir_coef_table COMMAND test_dac_fir_coef_table)
set_tests_properties(dac_fir_coef_table PROPERTIES LABELS "dac_fir")

# tests/unit/dac_noise/CMakeLists.txt
add_executable(test_dac_noise_spectral test_dac_noise_spectral.c)
target_link_libraries(test_dac_noise_spectral PRIVATE unity spu94_static)
add_test(NAME dac_noise_spectral COMMAND test_dac_noise_spectral)
set_tests_properties(dac_noise_spectral PROPERTIES LABELS "dac_noise")
```

### Bit-Identity Test Pattern (from `test_fir_bit_identity.c`)

```c
// Source: tests/unit/fir/test_fir_bit_identity.c (existing pattern)
// Phase 6 adapts this for the DAC FIR: run scipy and C on identical
// input sequences, compare output sample-by-sample.

// The Python side: extend tools/dac_filter_design.py with a
// --dump-reference-output mode that:
//   1. Reads a test vector from stdin or a fixed file
//   2. Applies all three stages in scipy (Q15-quantized coefficients)
//   3. Writes the output as a C header with int16_t array literal
//
// The C side: test_dac_fir_bit_identity.c includes the generated header
// and asserts C output == scipy output for every sample.
//
// This matches the derive_fir_reference.py -> test_fir_bit_identity.c
// pipeline from Phase 4.
```

## Accumulator Width Proofs (Verified)

All proofs computed analytically and verified empirically.

### Stage 1: 55-tap half-band (14 pairs + 1 center = 15 multiplies)

| Property | Value |
|----------|-------|
| Non-zero coefficients | 29 |
| Sum of absolute Q15 values | 58,126 |
| Center tap | 0x4000 (16,384) |
| Worst-case folded accumulator | 1,904,643,762 (0x71868EB2) |
| INT32_MAX | 2,147,483,647 (0x7FFFFFFF) |
| Headroom | **1.04 dB** |
| Accumulator type | **int32 sufficient** |

[VERIFIED: python3 accumulator width analysis, 2026-04-28]

### Stage 2: 11-tap half-band (3 pairs + 1 center = 4 multiplies)

| Property | Value |
|----------|-------|
| Non-zero coefficients | 7 |
| Sum of absolute Q15 values | 40,786 |
| Center tap | 0x3FFE (16,382) |
| Worst-case folded accumulator | 1,336,455,240 (0x4FA8B048) |
| INT32_MAX | 2,147,483,647 (0x7FFFFFFF) |
| Headroom | **4.12 dB** |
| Accumulator type | **int32 sufficient** |

[VERIFIED: python3 accumulator width analysis, 2026-04-28]

### Stage 3: 7-tap half-band (2 pairs + 1 center = 3 multiplies)

| Property | Value |
|----------|-------|
| Non-zero coefficients | 5 |
| Sum of absolute Q15 values | 37,264 |
| Center tap | 0x4000 (16,384) |
| Worst-case folded accumulator | 1,221,048,126 (0x48C7B73E) |
| INT32_MAX | 2,147,483,647 (0x7FFFFFFF) |
| Headroom | **4.90 dB** |
| Accumulator type | **int32 sufficient** |

[VERIFIED: python3 accumulator width analysis, 2026-04-28]

### Total Multiply Count

15 + 4 + 3 = **22 multiplies** per sample through the cascade (one more than the ~21 estimate in D-01 because Stage 1 has 14 pairs + 1 center = 15, not 14).

## LFSR Polynomial Selection

**Recommendation:** 32-bit Galois LFSR with polynomial x^32 + x^22 + x^2 + x^1 + 1 (feedback mask 0x80200003).

| Property | Value |
|----------|-------|
| Bit width | 32 |
| Form | Galois (single XOR per step) |
| Polynomial | x^32 + x^22 + x^2 + x^1 + 1 |
| Feedback mask | 0x80200003 |
| Period | 2^32 - 1 = 4,294,967,295 |
| At 44.1 kHz | Period = ~97,391 seconds (~27 hours) |

[ASSUMED] This specific polynomial is a well-known maximal-length 32-bit LFSR. The exact polynomial is Claude's discretion per CONTEXT.md. Alternative: x^32 + x^7 + x^5 + x^3 + x^2 + x + 1 (Xilinx XAPP052) -- either is correct as long as it is maximal-length. The choice has no audible effect.

**Why 32-bit, not 16-bit:** A 16-bit LFSR has period 65,535 which at 44.1 kHz repeats every 1.49 seconds. This is audible as a periodic pattern in the noise floor. 32-bit repeats every 27 hours -- effectively infinite for audio purposes. [VERIFIED: arithmetic from known LFSR period formula]

## Noise Amplitude Derivation

**Target:** ~90 dB SNR per AK4309B datasheet (D-04).

The noise model produces shaped noise whose RMS level, integrated over the audio band (0-22.05 kHz), should be approximately 90 dB below full scale (32,767 for Q15).

**Approach:** The LFSR produces uniformly distributed white noise. The 2nd-order HP shaping boosts high-frequency energy. The pre-shaping amplitude must be set so that the post-shaping RMS in the audio band hits the target.

For a 2nd-order HP with transfer function H(z) = (1 - z^-1)^2, the in-band noise power gain relative to white noise at 44.1 kHz is:

```
G = integral(0 to 0.5) |H(f)|^2 df  (normalized frequency)
  = integral(0 to 0.5) [2*sin(pi*f)]^4 df
  = 3/8 = 0.375
```

So the shaped noise RMS is sqrt(0.375) = 0.612 times the white noise RMS.

For -90 dB target: white noise RMS = 10^(-90/20) * 32768 / 0.612 = 0.001038 * 32768 = **approximately 55 LSBs RMS pre-shaping**.

This translates to a right-shift of approximately 9 bits on the raw LFSR output (scaling from ~16384 RMS to ~32 RMS, then HP shaping boosts back up). The exact shift value should be tuned empirically to hit the -90 dB target in the spectral test.

[ASSUMED] The exact shift value (NOISE_SHIFT) needs empirical tuning. The derivation above is approximate because it assumes ideal shaping at DC sampling rate. The planner should make the amplitude a compile-time constant that the spectral slope test validates.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| ZOH sinc droop as primary DAC artifact | Passband ripple + noise shaping (no ZOH) | Phase 5 research | ZOH at 384x OSR is 0.000009 dB at 20 kHz -- negligible. Passband ripple (+/-0.05 dB) and noise shaping are the actual artifacts. |
| 1st-order HP noise shaping | 2nd-order HP noise shaping (+12 dB/oct) | DEEP-DELTA-SIGMA.md | Matches the likely 2nd-order modulator in the AK4309 |
| Bitcrusher-style DAC modeling | Delta-sigma noise spectrum modeling | Phase 5 research | AK4309 is 1-bit delta-sigma; flat quantization noise is wrong topology |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | 32-bit Galois LFSR polynomial 0x80200003 is maximal-length | LFSR Polynomial Selection | LFSR would have shorter period; noise would repeat audibly. Easily verified by running 2^32 steps and checking return to seed. |
| A2 | NOISE_SHIFT of approximately 9 bits produces -90 dB RMS in audio band | Noise Amplitude Derivation | Noise would be too loud or too quiet. The spectral test catches this; the exact value is tuned empirically. |
| A3 | The three-stage cascade at 44.1 kHz reproduces the correct passband ripple character | Pitfall 5 | The composite response would differ from the Phase 5 verified design. Mitigated by the bit-identity test against scipy. |
| A4 | 2nd-order difference equation (1 - z^-1)^2 correctly models +12 dB/octave slope | Pattern 3 | The noise spectral shape would be wrong. This is textbook delta-sigma NTF theory but the spectral test verifies it empirically. |

## Open Questions

1. **Exact NOISE_SHIFT value for -90 dB target**
   - What we know: Approximately 9 bits of right-shift on raw LFSR output
   - What's unclear: The exact value depends on the interaction between LFSR output distribution, 2nd-order HP gain, and audio-band integration
   - Recommendation: Make it a compile-time constant. Write the spectral amplitude test first, then tune the constant until the test passes.

2. **Whether the DAC FIR operates on both L and R channels independently**
   - What we know: The existing SPU FIR has separate delay lines for L and R (4 rings total). The DAC FIR should follow the same pattern.
   - What's unclear: Whether Phase 7 integration will call `spu94_dac_fir_step` once per channel or once per stereo pair
   - Recommendation: Design the API as mono (one sample in, one sample out). Phase 7 calls it twice per sample -- once for L, once for R. Each channel gets its own state struct instance. This matches the ADPCM pattern where `spu94_adpcm_state` is per-channel.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (vendored in tests/unit/vendor/Unity/) |
| Config file | tests/unit/CMakeLists.txt (add_subdirectory for new test dirs) |
| Quick run command | `cd build && ctest -L dac_fir --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DAC-FILT-02 | Filter coefficients match --export-c | unit | `ctest -R dac_fir_coef_table --output-on-failure` | Wave 0 |
| DAC-FILT-02 | Folded form == literal form | unit | `ctest -R dac_fir_bit_identity --output-on-failure` | Wave 0 |
| DAC-FILT-02 | int32 accumulator no overflow | unit | `ctest -R dac_fir_overflow_proof --output-on-failure` | Wave 0 |
| DAC-FILT-02 | DC gain per stage matches expected | unit | `ctest -R dac_fir_dc_gain --output-on-failure` | Wave 0 |
| DAC-FILT-02 | Impulse response matches scipy | unit | `ctest -R dac_fir_impulse --output-on-failure` | Wave 0 |
| DAC-FILT-02 | Bit-identity with scipy reference | integration | `ctest -R dac_fir_bit_identity --output-on-failure` | Wave 0 |
| DAC-NOISE-01 | +12 dB/octave spectral slope | unit | `ctest -R dac_noise_spectral --output-on-failure` | Wave 0 |
| DAC-NOISE-01 | Noise RMS at ~-90 dB | unit | `ctest -R dac_noise_amplitude --output-on-failure` | Wave 0 |
| DAC-NOISE-01 | LFSR non-zero output, full period | unit | `ctest -R dac_noise_lfsr --output-on-failure` | Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build && ctest -L dac_fir -L dac_noise --output-on-failure`
- **Per wave merge:** `cd build && ctest --output-on-failure` (full suite)
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/dac_fir/` directory and CMakeLists.txt
- [ ] `tests/unit/dac_noise/` directory and CMakeLists.txt
- [ ] `tests/unit/CMakeLists.txt` -- add `add_subdirectory(dac_fir)` and `add_subdirectory(dac_noise)`
- [ ] Python reference output generator (extend `tools/dac_filter_design.py` or new script)

## Sources

### Primary (HIGH confidence)
- `src/spu94/spu94_fir.c` -- Folded-form FIR pattern, accumulator width proof, delay-line conventions
- `src/spu94/spu94_fir_coef.c` -- Coefficient table layout pattern
- `src/spu94/spu94_adpcm.c` + `include/spu94/spu94_adpcm.h` -- Standalone module pattern
- `src/spu94/spu94_state_internal.h` -- State struct layout, ADPCM field pattern
- `tools/dac_filter_design.py --export-c` -- Verified Q15 coefficient output
- `.planning/phases/05-interpolation-filter-design/05-01-SUMMARY.md` -- Phase 5 achieved specs
- `tests/unit/fir/test_fir_bit_identity.c` -- Bit-identity test pattern
- `tests/unit/fir/test_fir_overflow_proof.c` -- Overflow proof test pattern
- `CMakeLists.txt` + `src/spu94/CMakeLists.txt` -- Build system pattern for adding sources
- `tests/unit/fir/CMakeLists.txt` -- Test registration pattern

### Secondary (MEDIUM confidence)
- `.planning/research/DEEP-DELTA-SIGMA.md` -- 2nd-order NTF, noise shaping theory, +12 dB/octave slope
- `.planning/research/DEEP-AK4309-FAMILY.md` -- AK4309B specs, 90 dB DR, 384x OSR
- `.planning/research/PITFALLS-v1.2.md` -- C1-C6 pitfalls, especially C3 (chain position) and C6 (fixed-point)

### Tertiary (LOW confidence)
- LFSR polynomial 0x80200003 -- from training knowledge, needs period verification
- Noise amplitude shift value -- approximate derivation, needs empirical tuning

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all tools and libraries already exist in the project
- Architecture: HIGH -- direct adaptation of proven patterns (`spu94_fir.c`, `spu94_adpcm.c`)
- Accumulator width proofs: HIGH -- computed analytically and verified with scipy
- Noise model: MEDIUM -- theory is sound, exact amplitude needs empirical tuning
- Pitfalls: HIGH -- well-documented from Phase 5 experience and existing FIR work

**Research date:** 2026-04-28
**Valid until:** 2026-05-28 (stable -- no external dependency changes expected)
