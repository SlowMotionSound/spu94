# Architecture: True 8x DAC Oversampling Integration

**Domain:** PS1 AK4309 DAC true oversampling -- zero-stuff to 352.8kHz, interpolate, decimate
**Researched:** 2026-04-30
**Confidence:** HIGH on integration points and data flow (existing codebase fully understood, hardware signal chain documented). MEDIUM on optimal decimation filter design (needs Phase-specific research). HIGH on build order and dependency analysis.

---

## 1. Current State (v1.2 Baseline)

### 1.1 What v1.2 Does

The v1.2 DAC FIR (`spu94_dac_fir.c`) runs all three cascaded half-band stages **at 44.1kHz on every call**. It models the passband ripple character of the AK4309 interpolation filter without actually performing oversampling. The header comment is explicit:

> "All three stages operate at 44.1 kHz on every call (Pitfall 5 -- NOT at increasing rates). The cascade reproduces the passband ripple character at the audio rate."

This was a deliberate design choice for v1.2 -- model the audible artifact cheaply.

### 1.2 What v1.3 Changes

v1.3 replaces this with the real operation: zero-stuff the 44.1kHz signal to 352.8kHz, run the three half-band stages at their true rates (each stage doubles the rate), then decimate back to 44.1kHz for output. This captures inter-image aliasing, time-domain ringing at the true oversampled rate, and the interaction between the filter cascade and the noise model.

### 1.3 Files Affected (Existing)

| File | Current Role | v1.3 Change |
|------|-------------|-------------|
| `src/spu94/spu94_dac_fir.c` | 44.1kHz cascade, 22 multiplies/sample | **REWRITE**: polyphase 8x upsample + cascade at true rates |
| `src/spu94/spu94_dac_fir_coef.c` | Coefficient tables (unchanged data) | **UNCHANGED**: same coefficients, same tables |
| `src/spu94/spu94_dac_fir_internal.h` | Dimensions, pair tables, test wrapper | **MODIFY**: add oversampled state dimensions, new API |
| `include/spu94/spu94_dac_fir.h` | Public state struct + API | **MODIFY**: new state struct for oversampled delay lines |
| `src/spu94/spu94_dac_noise.c` | Noise at 44.1kHz | **MODIFY**: run at 352.8kHz (8 steps per output sample) |
| `include/spu94/spu94_dac_noise.h` | Noise state struct + API | **LIKELY UNCHANGED**: state struct is already minimal |
| `src/spu94/spu94_process.c` | Calls `dac_fir_step` then `dac_noise_step` per sample | **REWRITE DAC SECTION**: new call pattern for oversampled processing |
| `src/spu94/spu94_state_internal.h` | DAC state in `spu94_state` | **MODIFY**: swap old FIR state for new oversampled state |
| `src/spu94/spu94_io_chain.c` | DAC enable/disable, state reset | **MODIFY**: reset new state structs |

---

## 2. True 8x Oversampling Signal Flow

### 2.1 Hardware Reality (AK4309)

```
44.1kHz PCM input
    |
    v
Stage 1: 2x upsample (55-tap half-band)   -> 88.2kHz
    |
    v
Stage 2: 2x upsample (11-tap half-band)   -> 176.4kHz
    |
    v
Stage 3: 2x upsample (7-tap half-band)    -> 352.8kHz
    |
    v
Delta-sigma modulator (1-bit, runs at 352.8kHz or higher)
    |
    v
Analog reconstruction (SCF + CTF)
```

Each stage zero-stuffs (insert a zero between every sample), then filters with its half-band FIR. The half-band property means every other coefficient is zero, which is why these filters are efficient for interpolation.

### 2.2 v1.3 Software Model

```
44.1kHz PCM from master mixer
    |
    v
Stage 1: zero-stuff + 55-tap filter       -> 88.2kHz (2 samples out per 1 in)
    |
    v
Stage 2: zero-stuff + 11-tap filter       -> 176.4kHz (2 samples out per 1 in)
    |
    v
Stage 3: zero-stuff + 7-tap filter        -> 352.8kHz (2 samples out per 1 in)
    |
    v
Noise injection at 352.8kHz               -> 352.8kHz (8 noise samples per input)
    |
    v
Decimation (select every 8th sample)      -> 44.1kHz
    |
    v
44.1kHz output
```

One input sample at 44.1kHz produces 8 samples at 352.8kHz through the cascade. Simple decimation selects back to 44.1kHz.

### 2.3 Per-Sample Processing Cost

**v1.2 cost:** 22 multiplies + 1 noise step = 23 operations per sample per channel.

**v1.3 cost per input sample per channel:**

| Stage | Operations | Rate | Total per 44.1kHz sample |
|-------|-----------|------|-------------------------|
| Stage 1 (55-tap) | 15 muls (phase 0) + 1 mul (phase 1) | 2 outputs | 16 multiplies |
| Stage 2 (11-tap) | 4 muls (phase 0) + 1 mul (phase 1) | 4 outputs | 10 multiplies |
| Stage 3 (7-tap) | 3 muls (phase 0) + 1 mul (phase 1) | 8 outputs | 16 multiplies |
| Noise | 1 LFSR step x 8 | 352.8kHz | 8 LFSR steps |
| Decimation | 0 (simple selection) | -- | 0 multiplies |
| **Total** | | | **~42 muls + 8 LFSR** |

Roughly 2x the v1.2 cost. Still trivial for any modern CPU at 44.1kHz (sub-microsecond per sample). For MCU targets, 42 multiply-accumulates at 44.1kHz on a 168MHz Cortex-M4 with single-cycle MAC is well under 1% CPU.

---

## 3. Polyphase Decomposition: The Efficient Implementation

### 3.1 Why Polyphase, Not Naive Zero-Stuff

A naive implementation would:
1. Insert a zero between each sample
2. Run all N taps of the filter on every output sample (including the zeros)

This wastes multiplies on zero-valued samples. The polyphase decomposition splits each filter into sub-filters that only operate on non-zero inputs.

For a half-band 2x interpolation filter, polyphase decomposition gives exactly two phases:
- **Phase 0 (even outputs):** operates on even-indexed coefficients, produces output aligned with original samples
- **Phase 1 (odd outputs):** operates on odd-indexed coefficients, produces the interpolated sample between originals

This is **exactly what the existing `spu94_fir_interpolate` already does** for the reverb FIR -- `fir_interp_phase0_apply` and `fir_interp_phase1_apply` in `spu94_fir.c` are the polyphase decomposition of the 39-tap half-band FIR. The DAC FIR stages get the same treatment.

### 3.2 Half-Band Polyphase Efficiency

For a half-band filter, Phase 1 reduces to a **single multiply** (center tap passthrough with scaling). All other odd-indexed coefficients are zero by half-band construction. Phase 0 does the real work via the folded-form apply.

For the DAC cascade:

| Stage | N taps | Phase 0 muls (folded) | Phase 1 muls | Total per input sample |
|-------|--------|----------------------|-------------|------------------------|
| Stage 1 (55-tap) | 55 | 15 (14 pairs + center) | 1 (center tap) | 16 |
| Stage 2 (11-tap) | 11 | 4 (3 pairs + center) | 1 | 5 x 2 inputs = 10 |
| Stage 3 (7-tap) | 7 | 3 (2 pairs + center) | 1 | 4 x 4 inputs = 16 |
| **Cascade total** | | | | **42 multiplies** |

### 3.3 Delay Line Sizing (Polyphase)

In polyphase form, each stage's delay line holds **input-rate samples only** (not the zero-stuffed stream). The delay line length for a 2x polyphase interpolator with N taps is `ceil(N/2)`:

| Stage | Input rate | Delay line length | Bytes per channel |
|-------|-----------|-------------------|-------------------|
| Stage 1 | 44.1kHz | ceil(55/2) = 28 int16 | 56 + 1 idx = 57 |
| Stage 2 | 88.2kHz | ceil(11/2) = 6 int16 | 12 + 1 idx = 13 |
| Stage 3 | 176.4kHz | ceil(7/2) = 4 int16 | 8 + 1 idx = 9 |
| **Total** | | | **79 bytes/channel** |

Both channels: **158 bytes.**

v1.2 was 149 bytes/channel (73 int16 entries + 3 indices), 298 bytes for stereo. So v1.3 polyphase state is actually **smaller** (158 < 298) because polyphase delay lines store only input-rate samples, not the full filter span at a single rate.

---

## 4. Decimation Back to 44.1kHz

### 4.1 Recommendation: Simple Decimation (Select Every 8th Sample)

**Use simple decimation (pick one sample from each group of 8).** Rationale:

1. **No decimation filter exists in the real hardware.** The AK4309's output is analog (1-bit delta-sigma -> SCF -> CTF). There is no digital decimation step. We are adding decimation purely because our model is digital. The simplest approach that preserves the audio-band content is correct.

2. **The interpolation cascade provides anti-aliasing.** With 41dB stopband rejection across the three-stage cascade, image energy that could alias during decimation is already 41dB down. Combined with the noise floor target of -90dB, aliased content lands at ~-131dB. Inaudible.

3. **Zero additional cost.** No filter, no state, no multiplies for the decimation step. Just select `out[0]` from the 8-sample buffer.

**If validation reveals audible aliasing (unlikely):** add a polyphase anti-alias FIR as a refinement. This is a fallback, not a prerequisite.

---

## 5. Changes to spu94_process.c

### 5.1 Current DAC Section (v1.2)

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

### 5.2 New DAC Section (v1.3)

```c
/* 7. DAC section: true 8x oversampled processing */
if (state->dac_enabled) {
    if (state->dac_fir_enabled) {
        /* Upsample 1x44.1kHz -> 8x352.8kHz through cascade */
        int16_t os_l[8], os_r[8];
        spu94_dac_fir_upsample8x(&state->dac_fir_l, out_l, os_l);
        spu94_dac_fir_upsample8x(&state->dac_fir_r, out_r, os_r);

        /* Inject noise at 352.8kHz if enabled */
        if (state->dac_noise_enabled) {
            for (int k = 0; k < 8; k++) {
                os_l[k] = q15_add_sat(os_l[k],
                    spu94_dac_noise_step(&state->dac_noise_l));
                os_r[k] = q15_add_sat(os_r[k],
                    spu94_dac_noise_step(&state->dac_noise_r));
            }
        }

        /* Decimate: select sample aligned with input */
        out_l = os_l[0];
        out_r = os_r[0];
    } else if (state->dac_noise_enabled) {
        /* FIR off, noise only: noise at 44.1kHz (v1.2 fallback) */
        out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
        out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
    }
}
```

### 5.3 Key Design Decisions

1. **Stack-local 8-sample buffers.** `int16_t os_l[8]` is 16 bytes on stack. Transient -- produced and consumed within one call. No persistent storage needed.

2. **Noise injection at 352.8kHz.** 8 LFSR steps per 44.1kHz sample. The shaped noise spectrum is faithful at the elevated rate. After decimation, in-band noise includes correctly-folded contributions from all 8 sub-samples.

3. **FIR-off + noise-on fallback.** Without the FIR cascade there is no 352.8kHz rate. Noise runs at 44.1kHz (v1.2 behavior). Graceful degradation, not a correctness issue.

4. **Sub-toggles preserved.** `dac_fir_enabled` and `dac_noise_enabled` remain independent. FIR-only = oversampled filter coloration without noise. Noise-only = shaped noise without filter. Both-on = faithful model.

---

## 6. Changes to spu94_dac_fir.c

### 6.1 New Public API

Replace `spu94_dac_fir_step` (1-in, 1-out at 44.1kHz) with `spu94_dac_fir_upsample8x` (1-in, 8-out):

```c
/* Process one 44.1kHz sample through the three-stage interpolation cascade.
 * Produces 8 samples at 352.8kHz in out[0..7].
 * out[0] = phase-0 of all three stages (aligned with input).
 * out[7] = phase-1-1-1 (last interpolated sub-sample).
 * State carries across calls. Mono API -- one call per channel. */
void spu94_dac_fir_upsample8x(spu94_dac_fir_state *state,
                               int16_t input,
                               int16_t out[8]);
```

### 6.2 Internal Structure

```
Stage 1: push input into stage1_delay
          -> phase0 = dac_fir_stage_apply(stage1, even coefficients)
          -> phase1 = sat_s16((coef[27] * input) >> 15)
          -> 2 samples: [phase0, phase1]

Stage 2: for each of 2 samples from Stage 1:
            push into stage2_delay
            -> phase0 = dac_fir_stage_apply(stage2, even coefficients)
            -> phase1 = sat_s16((coef[5] * sample) >> 15)
          -> 4 samples

Stage 3: for each of 4 samples from Stage 2:
            push into stage3_delay
            -> phase0 = dac_fir_stage_apply(stage3, even coefficients)
            -> phase1 = sat_s16((coef[3] * sample) >> 15)
          -> 8 samples
```

The existing `dac_fir_stage_apply` function is reused for Phase 0 of each stage. A new inline function handles Phase 1 (single multiply).

### 6.3 What Happens to the Old `spu94_dac_fir_step`

**Remove it.** The v1.2 function ran all three stages at 44.1kHz in series. v1.3's polyphase cascade is structurally incompatible. The test-visible `spu94_dac_fir_test_stage_apply` can remain for per-stage unit testing.

### 6.4 New State Struct

```c
typedef struct {
    /* Stage 1: polyphase delay line at 44.1kHz input rate */
    int16_t stage1_delay[28];  /* ceil(55/2) */
    uint8_t stage1_idx;

    /* Stage 2: polyphase delay line at 88.2kHz */
    int16_t stage2_delay[6];   /* ceil(11/2) */
    uint8_t stage2_idx;

    /* Stage 3: polyphase delay line at 176.4kHz */
    int16_t stage3_delay[4];   /* ceil(7/2) */
    uint8_t stage3_idx;
} spu94_dac_fir_state;
```

---

## 7. Noise Model at 352.8kHz

### 7.1 What Changes

The noise module (`spu94_dac_noise.c`) is called 8x per 44.1kHz sample instead of 1x. The `spu94_dac_noise_step` function itself is unchanged.

### 7.2 Why It Matters

The NTF `(1 - z^-1)^2` shapes noise relative to the sample rate. At 352.8kHz, most shaped noise energy is above 22.05kHz (audio band edge), exactly like the real delta-sigma modulator. After decimation, only the small in-band fraction survives.

Running at 44.1kHz (v1.2) artificially concentrates all shaped noise within the audio band. Running at 352.8kHz correctly distributes it across 8x the bandwidth.

### 7.3 Amplitude Recalibration

`DAC_NOISE_SHIFT` (currently 14) was tuned for 44.1kHz. At 352.8kHz, the 2nd-order NTF pushes noise upward more aggressively, reducing in-band energy by roughly 30dB. To maintain -90dB in-band SNR after decimation, the shift value decreases (more raw noise amplitude). Exact value: empirical tuning during Phase 3.

### 7.4 Code and State Impact

- `spu94_dac_noise_step`: unchanged
- `spu94_dac_noise_state`: unchanged
- `DAC_NOISE_SHIFT`: retuned (compile-time constant change)
- Caller in `spu94_process.c`: 8 calls per sample instead of 1

---

## 8. Interaction with Existing Architecture

### 8.1 Zero Blast Radius When Disabled

`dac_enabled == 0`: no DAC code runs. All DAC-off goldens are bit-identical.

`dac_enabled == 1` with v1.3: output differs from v1.2 goldens. Expected and desired. New golden files generated.

### 8.2 Components NOT Touched

| Component | Why Unchanged |
|-----------|--------------|
| Reverb core (`spu94_reverb.c`, `spu94_tick.c`) | DAC is terminal post-processor, no feedback |
| SPU half-band FIR (`spu94_fir.c`) | Different filter, different purpose |
| ADPCM (`spu94_adpcm.c`) | Upstream stage, no interaction with DAC |
| Send/return mixer (in `spu94_process.c`) | DAC operates on mixer output, no changes to mixer logic |
| Buffer arithmetic (`spu94_buffer.c`) | Reverb-only |
| Register I/O, presets | No new registers for DAC oversampling |

### 8.3 Latency

The polyphase FIR cascade has group delay. For linear-phase half-band FIRs, the total cascade delay referenced to the 44.1kHz input:
- Stage 1: (55-1)/2 / 1 = 27 input-rate samples
- Stage 2: (11-1)/2 / 2 = 2.5 input-rate samples
- Stage 3: (7-1)/2 / 4 = 0.75 input-rate samples
- **Total: ~30 samples at 44.1kHz**

This same latency existed in v1.2 (same filters), but was conceptually collapsed into a single-rate cascade. `spu94_get_total_latency_samples` should add this term when DAC FIR is enabled.

---

## 9. Component Boundaries

| Component | Responsibility | Communicates With |
|-----------|---------------|-------------------|
| `spu94_dac_fir.c` | Polyphase 8x interpolation cascade | Called by `spu94_process.c` |
| `spu94_dac_fir_coef.c` | Coefficient tables (unchanged) | Read by `spu94_dac_fir.c` |
| `spu94_dac_fir_internal.h` | Internal dimensions, pair tables | Included by `spu94_dac_fir.c` |
| `spu94_dac_noise.c` | LFSR + HP noise shaping (unchanged logic) | Called 8x per sample by `spu94_process.c` |
| `spu94_process.c` | Orchestrates upsample -> noise inject -> decimate | Calls FIR and noise modules |
| `spu94_state_internal.h` | State struct with new delay line dimensions | Defines storage for FIR and noise |
| `spu94_io_chain.c` | Enable/disable, state reset on toggle | Resets FIR/noise state |

### New vs Modified Files

| Category | Files |
|----------|-------|
| **New files** | None -- all changes are to existing files |
| **Major rewrite** | `spu94_dac_fir.c` (upsample8x replaces step) |
| **Moderate changes** | `spu94_process.c` (DAC section), `include/spu94/spu94_dac_fir.h` (state struct + API) |
| **Minor changes** | `spu94_dac_fir_internal.h`, `spu94_state_internal.h`, `spu94_io_chain.c` |
| **Unchanged** | `spu94_dac_fir_coef.c`, `spu94_dac_noise.c`, `spu94_dac_noise.h`, all reverb/FIR/ADPCM/mixer code |

---

## 10. State Budget Impact

| Change | Bytes |
|--------|-------|
| Remove v1.2 FIR state (2 channels: 298 bytes) | -298 |
| Add v1.3 polyphase FIR state (2 channels: 158 bytes) | +158 |
| Noise state unchanged | 0 |
| **Net change** | **-140 bytes (smaller)** |

No bump to `SPU94_STATE_SIZE_MAX` needed.

---

## 11. Build Order

### Phase 1: Polyphase Stage 1

**Goal:** True 2x polyphase interpolation for Stage 1 (55-tap).

1. Redesign `spu94_dac_fir_state` with polyphase delay lines (Stage 1 only, Stages 2-3 stubs)
2. Implement `dac_fir_stage1_upsample2x`: push 1 sample, get 2 samples (phase 0 + phase 1)
3. Reuse existing `dac_fir_stage_apply` for Phase 0; add Phase 1 center-tap path
4. Unit tests: verify phase 0 matches v1.2 stage-apply output, verify phase 1 center-tap correctness
5. Unit test: frequency response of 2x upsampled output

**Dependency:** `spu94_dac_fir_coef.c` (coefficients), `spu94_dac_fir_internal.h` (dimensions).

### Phase 2: Full 8x Cascade

**Goal:** Wire Stage 1 -> Stage 2 -> Stage 3 into `spu94_dac_fir_upsample8x`.

1. Add polyphase delay lines for Stages 2 and 3
2. Implement `spu94_dac_fir_upsample8x`: cascade producing 8 outputs
3. Update state struct for all three stages
4. Unit tests: DC convergence (8 outputs should approach input value), impulse response, frequency response against AK4309 spec (+/-0.05dB passband, 41dB stopband)

**Dependency:** Phase 1.

### Phase 3: Noise at 352.8kHz + Integration

**Goal:** Wire noise at elevated rate and integrate into process loop.

1. Retune `DAC_NOISE_SHIFT` for 352.8kHz (empirical calibration)
2. Rewrite DAC section in `spu94_process.c` (upsample -> noise inject -> decimate)
3. Update `spu94_state_internal.h` with new FIR state struct
4. Update `spu94_io_chain.c` reset/disable logic
5. Unit tests: noise spectral shape at 352.8kHz, in-band noise after decimation
6. Integration test: full chain (reverb + ADPCM + DAC)

**Dependency:** Phase 2, existing noise module.

### Phase 4: Verification + Golden Files

**Goal:** Validate, regression-test, update all surfaces.

1. Generate new DAC-enabled golden files
2. Verify all DAC-off goldens are bit-identical (zero blast radius)
3. Update `tools/dac_measure.py` frequency response characterization
4. Verify CLI `--dac`, Python binding, JUCE GUI (no API changes needed)
5. Coverage map update
6. ADR: document oversampling architecture decision and decimation choice

**Dependency:** Phase 3.

### Dependency Graph

```
Phase 1 (Polyphase Stage 1)
    |
    v
Phase 2 (Full 8x Cascade)
    |
    v
Phase 3 (Noise + Decimation + Integration)
    |
    v
Phase 4 (Verification + Golden Files)
```

Linear chain. Each phase builds on the previous.

---

## 12. Patterns to Follow

### Polyphase Half-Band (Existing Precedent)

`spu94_fir.c` already implements polyphase half-band interpolation (`fir_interp_phase0_apply` + `fir_interp_phase1_apply`). The DAC FIR stages follow the same decomposition. Proven pattern in the codebase.

### Folded-Form + Zero-Skip (Existing Precedent)

`dac_fir_stage_apply` already implements folded-form symmetric pair multiplication. Reused unchanged as the Phase 0 subfilter.

### Stack-Local Intermediate Buffers

The 8-sample oversampled buffer is stack-local in `spu94_process.c` -- produced and consumed within one call. No persistent storage wastes state budget.

### Toggleable Sub-Stages (Existing Precedent)

`dac_fir_enabled` and `dac_noise_enabled` sub-toggles preserved. FIR-off + noise-on falls back to 44.1kHz noise (v1.2 behavior). No API changes.

---

## 13. Anti-Patterns to Avoid

### Naive Zero-Stuff Without Polyphase

Do NOT insert zeros and run full N-tap filters at the elevated rate. Use polyphase decomposition (halves the work, codebase already has the pattern).

### Persistent 352.8kHz Circular Buffer

Do NOT store the 8-sample oversampled stream in the state struct. It is transient. Only the polyphase delay lines (input-rate samples per stage) persist.

### Changing the Public API

The enable/disable API is unchanged. `spu94_dac_fir_upsample8x` replaces `spu94_dac_fir_step` internally. No new public functions in `spu94.h`.

### Float Intermediates

All intermediates remain int16 Q15. The accumulator width proofs for each stage (documented in `spu94_dac_fir.c`) remain valid because polyphase sub-filters see the same worst-case input range.

---

## 14. Open Questions for Phase-Specific Research

### Q1: Polyphase Delay Line Indexing

Both Phase 0 and Phase 1 share the same input-rate delay line (one push per input, two reads with different coefficient subsets). This matches the reverb FIR pattern. Needs implementation-level verification during Phase 1.

### Q2: Phase 1 Center Tap Values

Stage 1 center: `0x4000` (index 27). Stage 2 center: `0x3FFE` (index 5). Stage 3 center: `0x4000` (index 3). The Stage 2 asymmetry (`0x3FFE` vs `0x4000`) must be preserved exactly.

### Q3: Decimation Phase Selection

Which of the 8 output samples is the "correct" one for decimation? The natural choice is sample[0] (Phase 0 of all three stages, aligned with input). Verify with impulse response test during Phase 2.

### Q4: Golden File Strategy

Replace v1.2 DAC-enabled goldens with v1.3 goldens. v1.3 is the more faithful model; v1.2 was an approximation. Document in commit message.

---

## Sources

- Existing codebase: `spu94_dac_fir.c`, `spu94_dac_noise.c`, `spu94_process.c`, `spu94_io_chain.c`, `spu94_fir.c` (all read directly, HIGH confidence)
- `.planning/research/ARCHITECTURE-v1.2.md` (prior architecture research, HIGH confidence)
- `.planning/research/DEEP-AK4309-FAMILY.md` (AK4309 datasheet extraction, MEDIUM confidence on internal filter architecture)
- `docs/DECISIONS.md` ADR-Phase-6-* series (v1.2 DAC design decisions, HIGH confidence)
- DSP theory: polyphase filter decomposition for multirate signal processing (textbook, HIGH confidence)
- AK4309B datasheet summary: 8x FIR interpolator, cascaded half-band (MEDIUM confidence -- era-typical assumption)

---

*Architecture research for: SPU-94, v1.3 True Oversampled DAC milestone*
*Researched: 2026-04-30*
