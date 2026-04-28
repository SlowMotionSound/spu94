# Pitfalls Research — DAC Modeling for libspu94

**Domain:** Adding DAC conversion modeling to an existing bit-faithful PS1 SPU reverb reimplementation (plain C99)
**Researched:** 2026-04-28
**Confidence:** MEDIUM (PS1 DAC chip identified as AKM AK4309AVM delta-sigma; datasheet unavailable; specific internal behavior inferred from topology class and measurements, not from manufacturer documentation; integration patterns are HIGH confidence based on shipped ADPCM precedent)

---

## Orientation

This document covers pitfalls specific to **adding DAC conversion modeling as a toggleable coloration stage to the existing libspu94 pipeline** (v1.1 shipped, ~6,300 LOC C core). It replaces the previous M2 ADPCM pitfalls document. The focus is:

1. Correctly scoping what "DAC modeling" means for a digital emulation (vs. analog output stage modeling)
2. Inserting the DAC stage at the correct point in the existing signal chain without breaking reverb or ADPCM
3. Choosing the right artifacts to model based on the actual PS1 converter topology (AKM AK4309AVM, 1-bit delta-sigma)
4. Avoiding performance regression in the per-sample hot path
5. Honest verification strategy given that the AK4309AVM datasheet is unavailable

Phase labels below reference the expected v1.2 DAC milestone structure:

- **P-RESEARCH** — identify PS1 DAC chip, converter topology, relevant artifacts
- **P-MODEL** — implement the digital DAC model (artifacts selection, fixed-point math)
- **P-INTEGRATE** — wire DAC model into the existing signal chain as a toggleable stage
- **P-VERIFY** — test infrastructure, golden files, witness comparison
- **P-DECISIONS** — document gray-area resolutions in DECISIONS.md

---

## Critical Pitfalls

Mistakes that cause rewrites, break existing correctness, or produce fundamentally wrong models.

### C1: Over-modeling — including analog output stage effects in the DAC model

**What goes wrong:**
The PS1 audio signal path after the SPU's digital domain is: DAC (AK4309AVM) -> analog reconstruction filter -> NJM2100 op-amp buffer -> coupling capacitors -> RCA output. A DAC model that includes op-amp coloring, output impedance, coupling-cap high-pass behavior, or power supply noise is modeling the analog output stage, not the DAC conversion.

The temptation is enormous because the audiophile PS1 community (SCPH-1001 vs SCPH-5501 discussions, Stereophile measurements, diyAudio mods) conflates "the DAC" with "the entire analog output path." Archimago's SCPH-5501 measurements show ~15-bit effective dynamic range, but much of that shortfall comes from the analog output stage, not the converter itself. The AK4309AVM's rated dynamic range is 90dB (~15 bits), but that is the chip-level spec including its own analog output, not a measure of digital conversion artifacts alone.

**Why it happens:**
- Web search results about PS1 audio quality overwhelmingly discuss the analog output path (audiophile modders bypassing the NJM2100 op-amps, soldering directly to DAC output pins)
- The AK4309AVM datasheet is unavailable, so there is no authoritative source separating digital-domain artifacts from analog-domain artifacts
- "DAC modeling" is colloquially used to mean "everything after the digital domain," not just the conversion step

**How to avoid:**
1. Scope the v1.2 DAC model to **digital-domain conversion artifacts only**: zero-order hold (ZOH) staircase effect, sinc rolloff from the ZOH, and the chip's internal digital interpolation filter behavior. These are the artifacts that exist in the digital conversion process itself.
2. Explicitly defer analog output stage modeling to a future milestone (already listed as out-of-scope in PROJECT.md: "DAC analog output stage (op-amps, coupling caps, output impedance) -- deferred; needs real hardware measurement").
3. Document the boundary in an ADR: "The DAC model covers conversion artifacts. Analog coloring is a separate concern requiring hardware measurement."
4. If the model "doesn't sound different enough," resist the temptation to smuggle in analog effects. The delta-sigma conversion artifacts of a well-designed 1990s DAC may genuinely be subtle at 16-bit/44.1kHz.

**Warning signs:**
- The model includes frequency-dependent gain curves that look like op-amp transfer functions
- Parameters reference ohms, capacitance, or supply voltage
- The model has more audible effect at low frequencies (coupling-cap behavior) than at high frequencies (ZOH rolloff)
- Someone says "it should sound warmer" — warmth is analog coloring, not DAC conversion

**Phase to address:** P-RESEARCH (scope definition), P-DECISIONS (boundary ADR)

---

### C2: Under-modeling — treating DAC as simple bit-depth reduction ("bitcrusher")

**What goes wrong:**
The naive DAC model is a bitcrusher: truncate to N bits, add quantization noise. This is how NOS R2R DAC artifacts work (resistor mismatches, monotonicity errors). But the PS1 uses a **1-bit delta-sigma DAC** (AK4309AVM), which has a completely different artifact profile:

- **Delta-sigma DACs do not produce R2R-style DNL/INL nonlinearity.** A 1-bit converter is inherently monotonic (there is only one resistor/current source). The "nonlinearities" of delta-sigma DACs come from noise shaping, idle tones, and the behavior of the internal modulator and analog reconstruction filter.
- **The dominant artifact of a delta-sigma DAC is its noise-shaping profile.** Quantization noise is pushed to ultrasonic frequencies by the modulator's feedback loop. The in-band noise floor is determined by the modulator order and the oversampling ratio.
- **Idle tones** (tonal artifacts near DC or at specific frequencies when the input is near zero or a simple fraction of full scale) are a real delta-sigma artifact, but they depend on the modulator order, dither implementation, and internal architecture — all unknown for the AK4309AVM.

A bitcrusher model sounds nothing like a delta-sigma DAC. It produces in-band quantization noise evenly distributed across the spectrum, which is the signature of R2R or NOS conversion, not oversampled delta-sigma.

**Why it happens:**
- "DAC emulation" plugins (chipcrusher, HoRNet ADDA) typically model NOS/R2R behavior because it is audibly dramatic and easy to implement
- Delta-sigma artifacts are subtle and hard to model without knowing the modulator architecture
- The AK4309AVM datasheet is lost, so its oversampling ratio and modulator order are unknown

**How to avoid:**
1. Start with what IS known and modelable: the ZOH (zero-order hold) staircase effect and its sinc rolloff, which applies to ANY DAC topology. At 44.1kHz, the ZOH sinc rolloff attenuates 20kHz by about 3.9dB — this is a real, measurable, topology-independent effect.
2. Do NOT implement bit-depth reduction, DNL/INL lookup tables, or R2R resistor mismatch simulation. These are wrong for a 1-bit delta-sigma converter.
3. If modeling noise shaping is desired, use a conservative generic model (second-order noise shaper, which matches the era — Philips used second-order for their 1-bit DACs in the mid-1990s), with the caveat that the AK4309AVM's actual order is unknown. Flag this as LOW confidence.
4. Document the topology mismatch risk in DECISIONS.md: "The AK4309AVM is a 1-bit delta-sigma converter. Bitcrusher-style modeling is incorrect for this topology."

**Warning signs:**
- The model has a "bit depth" parameter
- Quantization noise is flat-spectrum (characteristic of NOS/R2R, not delta-sigma)
- The model sounds like a bitcrusher plugin (harsh, obvious, evenly-noisy) instead of like a subtle high-frequency rolloff with possible idle tones near zero

**Phase to address:** P-RESEARCH (topology identification), P-MODEL (artifact selection), P-DECISIONS

---

### C3: Sample rate confusion — modeling DAC effects at the wrong point in the signal chain

**What goes wrong:**
The existing libspu94 signal chain operates at two rates:

```
44.1kHz input -> [ADPCM coloration] -> FIR decimation -> 22.05kHz reverb -> FIR interpolation -> 44.1kHz output
```

The DAC on the real PS1 sees the **final 44.1kHz output** after interpolation. It does NOT see the 22.05kHz internal reverb signal. Placing the DAC model before the FIR interpolator (at 22.05kHz) would apply DAC artifacts at the wrong rate:
- ZOH sinc rolloff at 22.05kHz has a different profile than at 44.1kHz
- Any noise-shaping model would operate at the wrong base rate
- The FIR interpolator would then filter some DAC artifacts, which doesn't happen in hardware

Conversely, placing the DAC model after the FIR interpolator is correct but must be done carefully: the model runs on every 44.1kHz sample in the hot path, doubling the per-sample computation.

**Why it happens:**
- The ADPCM stage was inserted BEFORE the FIR decimator (upstream), which is correct for ADPCM's place in the PS1 signal path (voice decode happens before reverb). A developer might pattern-match and insert DAC at the same point.
- The 22.05kHz internal rate is "where the interesting DSP happens," and there is a cognitive pull toward placing new processing there
- The io_chain.c structure invites adding stages inside chain_step_impl(), where the 22.05kHz reverb runs, rather than outside it

**How to avoid:**
1. The DAC model MUST be applied at the **44.1kHz output**, AFTER the FIR interpolator. In the current architecture, this means operating on the `lo`/`ro` output samples in `spu94_process()`, after the `spu94_fir_chain_step()` call returns.
2. Follow the same integration pattern as ADPCM but at the opposite end:
   ```
   [ADPCM] -> FIR dec -> reverb -> FIR interp -> [DAC model]
   ```
3. The DAC model should be the LAST processing stage before output, because on real hardware the DAC is the last digital-to-analog conversion step.
4. Document the signal chain position in an ADR with a clear diagram.

**Warning signs:**
- The DAC model code is inside `chain_step_impl()` alongside the reverb tick
- DAC-related state is updated on retained-phase (22.05kHz) ticks only
- ZOH rolloff measurements show -3.9dB at 11kHz instead of at 20kHz (wrong Nyquist reference)

**Phase to address:** P-INTEGRATE (chain position), P-DECISIONS (signal chain ADR)

---

### C4: Breaking existing ADPCM/reverb behavior when inserting a new stage

**What goes wrong:**
The existing `spu94_process()` loop is tight: ADPCM coloration (if enabled) -> `spu94_fir_chain_step()` -> output. Adding a DAC stage requires modifying this loop. Possible breakage modes:

1. **Latency accounting error:** `spu94_get_total_latency_samples()` currently returns `SPU94_LATENCY_SAMPLES + (adpcm ? 28 : 0)`. Adding DAC latency (if the model has any, e.g., from a reconstruction filter) requires updating this function. If the DAC model adds latency but the function is not updated, golden-file regression tests will fail because audio is shifted.

2. **In-place buffer aliasing:** `spu94_process()` supports `L_out == L_in` (in-place). If the DAC model needs to read the current output and modify it in-place, the read-modify-write must not corrupt unprocessed samples. The current ADPCM stage avoids this because it reads from its internal buffer, not from L_in.

3. **Flush path divergence:** `spu94_flush()` delegates to `spu94_process(NULL, NULL, ...)`. If the DAC model has state that needs draining (e.g., a filter delay line), the flush path must drain it too. The current architecture's "flush = process with silence" works only if all stages are memoryless or correctly drain when fed zeros.

4. **Golden file invalidation:** If the DAC model is always-on (or if its default changes), ALL existing golden files will need regeneration. The existing `regenerate_goldens.py --check` gate will fail.

**Why it happens:**
- The process loop looks simple and modification-safe, but its simplicity is load-bearing — multiple contracts (in-place, flush, latency) depend on the loop structure
- The ADPCM integration succeeded without breaking these contracts, creating false confidence that "any new stage is easy"

**How to avoid:**
1. DAC model is **default-off**, exactly like ADPCM. Toggle with `spu94_set_dac_enabled()`. This preserves all existing golden files when DAC is disabled.
2. DAC model is **zero-latency** (memoryless) if possible. ZOH and sinc rolloff compensation can be implemented as a per-sample transfer function with no state beyond the current sample. Avoid FIR filters in the DAC model unless they are essential.
3. If the DAC model does have state (e.g., a short FIR), update `spu94_get_total_latency_samples()` and verify the flush path drains correctly.
4. Run ALL existing tests (82+ ctest, golden files, witness diffs) with DAC disabled before attempting DAC-enabled tests.
5. New golden files for DAC-enabled mode are a SEPARATE set, not replacements.

**Warning signs:**
- Existing ctest failures after DAC code is added (even before DAC is enabled)
- `spu94_flush()` produces different-length tails with DAC enabled
- Witness diff thresholds suddenly exceeded with DAC disabled

**Phase to address:** P-INTEGRATE (primary), P-VERIFY (regression)

---

### C5: Historical accuracy trap — using modern AKM datasheets for a discontinued 1990s chip

**What goes wrong:**
The AK4309AVM datasheet is not publicly available (confirmed by dogbreath.de, the primary PS1 DAC documentation site: "the datasheet of the AK4309 AVM seems not to be available anymore"). The AK4309B datasheet IS available and describes a "1-bit stereo DAC for multimedia" with SCF (switched-capacitor filter) output and 256fs/384fs master clock options. But the AK4309B is a different chip:

- The AK4309B has 24 pins vs. the AK4309AVM's 24 pins (same count but different pinout)
- dogbreath.de explicitly notes the AK4309B is "incompatible" with the AK4309AVM
- Mid-1990s manufacturing tolerances, noise floors, and modulator designs differ from the "B" revision

Using the AK4309B datasheet as a proxy for the AK4309AVM is reasonable for high-level topology (both are 1-bit delta-sigma) but dangerous for specifics (oversampling ratio, noise-shaper order, SCF cutoff frequency, idle tone behavior).

Modern AKM chips (AK4490, AK4499) are irrelevant — they use completely different multi-bit delta-sigma architectures with digital post-processing that did not exist in the 1990s.

**Why it happens:**
- The AK4309AVM datasheet is genuinely lost
- The AK4309B datasheet appears in search results for "AK4309 datasheet" and looks authoritative
- Modern AKM datasheets are well-documented and tempting to extrapolate from

**How to avoid:**
1. Use the AK4309B datasheet ONLY for topology-class identification (1-bit delta-sigma, SCF output, 256fs/384fs clock). Do NOT use it for performance specifications (SNR, THD, noise-shaper order).
2. Use the Archimago SCPH-5501 measurements (real PS1 hardware) as the primary reference for actual performance: ~90dB dynamic range, "slight deviance from flat above 3kHz," jitter sidebands below -100dB.
3. Flag ALL AK4309AVM-specific claims as LOW confidence in documentation.
4. Design the model to be parameterizable so that hardware measurements (M5, Anthony's PS1) can calibrate it later.
5. Document in DECISIONS.md: "AK4309AVM datasheet unavailable. Model is based on topology-class behavior (1-bit delta-sigma) calibrated against Archimago's SCPH-5501 measurements. Specific parameters are approximate and flagged for hardware validation."

**Warning signs:**
- An ADR that says "per the AK4309 datasheet" without specifying which variant
- A noise-shaper implementation tuned to specific dB targets from the B variant's datasheet
- Anyone claiming to know the exact oversampling ratio of the AK4309AVM

**Phase to address:** P-RESEARCH (data gathering), P-DECISIONS (confidence flagging)

---

### C6: Fixed-point arithmetic pitfalls when modeling DAC nonlinearities in integer math

**What goes wrong:**
The existing libspu94 core uses Q15 fixed-point (int16_t, multiply-then-shift-15, truncate). A DAC model that introduces new arithmetic — particularly division, non-power-of-two scaling, or small fractional corrections — can introduce subtle precision errors:

1. **ZOH sinc compensation in Q15:** The sinc function `sin(pi*f/fs) / (pi*f/fs)` requires either a lookup table or a polynomial approximation. In Q15, a polynomial approximation of `1/sinc(x)` for compensation needs careful range analysis to avoid overflow in intermediate products. Two Q15 values multiplied produce a Q30 intermediate before the shift-by-15; chaining three multiplications (as in a cubic polynomial) needs Q45, which overflows int32.

2. **Noise-shaper feedback in fixed-point:** A delta-sigma noise-shaper model feeds back quantization error through a filter. The feedback coefficients determine the noise-shaping profile. In floating-point, this is straightforward. In fixed-point, coefficient quantization changes the noise-shaping curve, potentially introducing limit cycles (oscillations at DC or Nyquist that do not decay). This is a well-known problem in fixed-point delta-sigma implementations.

3. **Division operations:** The existing codebase avoids division in the hot path (reverb uses shifts; ADPCM uses `>>6`). A DAC model that introduces divisions (e.g., for polynomial evaluation or normalization) breaks this pattern and may introduce rounding inconsistencies.

**Why it happens:**
- DAC modeling literature is almost exclusively in floating-point (MATLAB, Python)
- Converting a floating-point model to fixed-point is a separate engineering task that introduces its own error sources
- The existing Q15 infrastructure handles simple multiply-and-shift well but does not provide higher-precision helpers

**How to avoid:**
1. Keep the DAC model as simple as possible in the hot path. ZOH sinc rolloff can be modeled as a single-pole IIR filter (first-order approximation) or a short FIR, both of which are straightforward in Q15.
2. If polynomial approximation is needed, use Q15 with int64_t intermediates for chained multiplications. The existing `q15_mul_truncate` returns int16_t; a new `q15_mul_wide` returning int32_t may be needed for intermediate precision.
3. Do NOT attempt to model the full delta-sigma modulator in fixed-point. The modulator operates at a much higher internal rate (256fs = 11.29MHz for 44.1kHz audio) and simulating it sample-by-sample is both computationally prohibitive and unnecessary for the audible effect.
4. Pre-compute any frequency-dependent coefficients at initialization time (when the model is enabled or parameters change), not in the per-sample path.
5. Validate fixed-point model output against a floating-point reference implementation (Python/numpy) with a known tolerance budget (e.g., +/-1 LSB per sample).

**Warning signs:**
- int32_t overflow in intermediate products (ASAN/UBSAN will catch this)
- Limit cycles: output oscillates at a fixed pattern when input is constant zero
- Model output diverges from float reference by more than 1 LSB on average

**Phase to address:** P-MODEL (implementation), P-VERIFY (float-vs-fixed comparison)

---

## Significant Pitfalls

### S1: Performance regression — DAC model adding too much computation to the hot path

**What goes wrong:**
The DAC model runs at 44.1kHz on every output sample (not at the 22.05kHz half-rate where the reverb runs). If the model involves a multi-tap FIR, a polynomial evaluation, or — worst case — a full delta-sigma modulator simulation at 256x oversampling, the per-sample cost could dwarf the reverb computation.

Current benchmark context: the existing `spu94_process` at the `rt_bench_latency` ctest target shows (p99-median)/median ratio of 0.741 against a threshold of 2.0. There is headroom, but not infinite.

**Why it happens:**
- "Just a few multiplies per sample" compounds: at 44.1kHz stereo, that is 88,200 extra operations per second per multiply
- Modeling literature suggests complex filter structures that are overkill for the audible effect
- The temptation to "get it right" leads to over-engineering the model

**How to avoid:**
1. Target a DAC model that adds NO MORE than 2-3 multiplications per sample per channel in the hot path. This is achievable with a first-order IIR or a 3-tap FIR for sinc compensation.
2. Benchmark before and after with `pytest-benchmark` and the existing `rt_bench_latency` target.
3. If a more complex model is needed, make it optional (a "quality" flag) with a fast default.
4. The model must NOT simulate the delta-sigma modulator at its native oversampled rate. Model the EFFECT (noise shaping, sinc rolloff), not the MECHANISM.
5. Profile with `perf` on the hot loop to catch unexpected costs (branch mispredictions from conditional DAC enable, cache misses from new state fields).

**Warning signs:**
- `rt_bench_latency` p99/median ratio increases above 1.5
- Per-block processing time more than doubles with DAC enabled
- The model has a loop inside the per-sample function

**Phase to address:** P-MODEL (design constraint), P-VERIFY (benchmark regression)

---

### S2: Verification traps — what can and cannot be verified without real hardware

**What goes wrong:**
Unlike ADPCM (where the decode algorithm is fully specified by nocash and bit-exact verification is possible against multiple witnesses), DAC modeling has NO authoritative specification to verify against. The AK4309AVM datasheet is lost. Emulator witnesses (Mednafen, DuckStation) do NOT model the DAC at all — they output raw 16-bit PCM from the SPU and rely on the host audio system for D/A conversion. There is no "golden reference" for what the PS1 DAC does to the digital signal.

Available verification targets:
- **Archimago's SCPH-5501 measurements:** Frequency response, THD, jitter — captured AFTER the entire analog output chain (DAC + op-amps + cables + measurement ADC). These measurements include analog output stage effects that are out of scope for the DAC model.
- **Stereophile measurements:** Similar scope (full chain), similar limitations.
- **Anthony's PS1 (future M5):** Can produce real hardware output, but capturing the DAC's digital-domain behavior requires isolating the DAC from the analog output stage, which is non-trivial.

What CANNOT be verified without hardware:
- The exact noise-shaping profile of the AK4309AVM
- Idle tone frequencies and amplitudes
- The internal digital filter's frequency response
- Whether the chip has 8x or some other oversampling ratio

**Why it happens:**
- The ADPCM milestone's verification strategy (bit-exact witness comparison) creates expectations that DAC verification will be similarly rigorous
- The audiophile measurement community provides data, but it measures the wrong thing (the complete analog chain, not the DAC alone)

**How to avoid:**
1. Accept that DAC model verification is inherently **approximate**, not bit-exact. Document this honestly in the ADR.
2. Verification targets for v1.2:
   - **Topology correctness:** The model behaves like a delta-sigma converter, not an R2R converter (spectral analysis shows noise shaping, not flat quantization noise)
   - **ZOH rolloff:** The model's frequency response shows the expected sinc rolloff (-3.9dB at 20kHz for 44.1kHz ZOH)
   - **Transparency when bypassed:** DAC-disabled output is bit-identical to pre-v1.2 output
   - **Regression safety:** All existing tests pass with DAC disabled
3. Defer **calibration** to M5 (hardware validation). The v1.2 model is a structurally correct placeholder that hardware measurements will tune.
4. Do NOT claim bit-accuracy for the DAC model. The project's bit-accuracy claim applies to the reverb algorithm and ADPCM codec, not to the DAC model.

**Warning signs:**
- ADR or documentation claims "bit-faithful DAC emulation"
- Test infrastructure attempts sample-exact comparison against emulator witnesses (which do not model DAC)
- Hardware validation is described as "confirming" rather than "calibrating" the model

**Phase to address:** P-VERIFY (strategy), P-DECISIONS (confidence documentation)

---

### S3: Scope creep via "while we're at it" effects

**What goes wrong:**
Once a DAC modeling stage exists in the pipeline, it becomes a magnet for adjacent effects:
- "Add a gentle high-shelf to simulate the op-amp coloring"
- "Add 1-bit dither to model the modulator's quantization"
- "Add jitter simulation to model clock instability"
- "Add a low-cut at 20Hz to model the coupling capacitor"

Each of these is a separate analog-domain effect masquerading as part of "DAC modeling." Together, they turn the DAC stage into an unverifiable grab-bag of "sounds PS1-ish" processing.

**Why it happens:**
- The DAC model's audible effect may be subtle (ZOH sinc rolloff is -3.9dB at 20kHz, which is gentle)
- The desire to "hear a difference when I flip the toggle" drives feature addition
- The analog output stage is where the PS1's distinctive character lives, and it is tempting to model it under the DAC umbrella

**How to avoid:**
1. The v1.2 milestone models the CONVERSION STEP ONLY. Analog output stage is a separate future milestone.
2. Each proposed addition must answer: "Does this effect exist in the digital-to-analog conversion itself, or in the analog circuit after the DAC chip's output pins?" If the latter, defer.
3. The toggle is `spu94_set_dac_enabled()`. It enables DAC conversion modeling. A future `spu94_set_analog_enabled()` (or similar) enables analog output stage modeling.
4. If the v1.2 model is too subtle to hear, that is a CORRECT RESULT, not a failure. Document it.

**Warning signs:**
- The DAC model has more than 3-4 parameters
- Parameters include frequency values below 100Hz (coupling cap territory) or above 22kHz (analog filter territory)
- The model "sounds great" but cannot be related back to a specific conversion artifact

**Phase to address:** P-RESEARCH (scope boundary), P-DECISIONS (scope ADR)

---

### S4: Revision-dependent behavior — which PS1 model is "the" reference?

**What goes wrong:**
Different PS1 hardware revisions use different DAC chips and output stages:

| Model | DAC | Notes |
|-------|-----|-------|
| SCPH-1001 | AK4309AVM | RCA jacks, NJM2100 op-amp buffer |
| SCPH-5501 | AK4309AVM | Same DAC, different output path (A/V Multiport) |
| SCPH-7001+ | AK4309B or integrated | Different chip, potentially different conversion |
| SCPH-750x+ | DAC integrated in CD/DSP | Completely different architecture |

Anthony owns "an original PSX" but the specific model revision has not been established. The DAC model should target the AK4309AVM (SCPH-1001/5501 era), but if Anthony's unit is a later revision, hardware validation measurements will not match the model.

**Why it happens:**
- "PS1" is treated as a single target, but the audio path changed significantly across revisions
- The audiophile community focuses on early models (SCPH-1001 specifically) because they sound "better"

**How to avoid:**
1. Target the AK4309AVM (SCPH-1001/5501) as the reference. This is the most documented, most measured, and most discussed PS1 DAC.
2. Document the target revision in the ADR: "DAC model targets AK4309AVM as found in SCPH-1001 and SCPH-5501."
3. Before M5 hardware validation, confirm which model Anthony has. If it is a later revision, the model cannot be validated against that specific unit for DAC behavior (though reverb and ADPCM can still be validated, since those are in the SPU, not the DAC).
4. Consider making the model parameterizable enough that a second "profile" could represent the later integrated DAC, if measurements become available.

**Warning signs:**
- The model is described as "the PS1 DAC" without specifying revision
- Hardware validation measurements do not match model predictions but the discrepancy is attributed to "the model needs tuning" rather than "this is a different DAC chip"

**Phase to address:** P-RESEARCH (reference identification), P-DECISIONS (revision ADR)

---

### S5: Confusing ZOH with sample-rate reduction

**What goes wrong:**
The ZOH (zero-order hold) effect is the staircase waveform produced by holding each sample constant between clock edges. Its frequency-domain effect is multiplication by a sinc function: `H(f) = sinc(f / fs)`, which rolls off high frequencies. At 44.1kHz, 20kHz is attenuated by about -3.9dB.

This is NOT the same as downsampling to a lower rate and then upsampling. The SPU already handles the 22.05kHz <-> 44.1kHz conversion with its half-band FIR. The ZOH effect operates on the 44.1kHz output signal as it is presented to the DAC chip.

An implementation that "models ZOH" by inserting a sample-and-hold at a lower rate (e.g., holding every other sample to simulate 22.05kHz ZOH) is double-counting: the FIR decimator/interpolator already handles the half-rate processing.

**Why it happens:**
- ZOH, sample-and-hold, and sample-rate conversion are related concepts that are easy to conflate
- "The PS1 reverb runs at 22.05kHz" leads to thinking the ZOH should operate at 22.05kHz
- Bitcrusher plugins model ZOH as "hold every Nth sample," which is a sample-rate reduction, not a true ZOH at the DAC output rate

**How to avoid:**
1. The ZOH effect at 44.1kHz is a gentle high-frequency rolloff following the sinc envelope. It does NOT involve holding or repeating samples.
2. Implement as a frequency-domain compensation: either apply the sinc rolloff directly (a mild low-pass curve) or model the ZOH's impulse response (a rectangular pulse of width 1/fs).
3. In practice, a first-order IIR low-pass at approximately 20kHz cutoff provides a reasonable approximation of the ZOH's audible effect at 44.1kHz.
4. Do NOT repeat or hold samples. That is sample-rate reduction, which is already handled by the FIR.

**Warning signs:**
- The model holds or duplicates samples in the output
- The model has a "hold factor" or "decimation ratio" parameter
- Frequency response shows a brick-wall notch (sample-rate aliasing) instead of a smooth sinc rolloff

**Phase to address:** P-MODEL

---

## Minor Pitfalls

### M1: Forgetting to update the Python binding and CLI for the new toggle

**What goes wrong:** v1.1 added `spu94_set_adpcm_enabled()` with Python ctypes binding (`spu94.adpcm_enabled = True`) and CLI flag. v1.2 must add equivalent `spu94_set_dac_enabled()` with matching binding and CLI support. Forgetting the binding means Python test infrastructure cannot exercise the DAC model.

**How to avoid:** Checklist item: for every new C API function, add ctypes wrapper + CLI flag + pytest coverage.

**Phase to address:** P-INTEGRATE

---

### M2: State struct bloat from DAC model fields

**What goes wrong:** The `spu94_state` struct currently contains ADPCM buffers (28-sample input + output per channel = 112 int16_t). If the DAC model adds filter delay lines, noise-shaper state, or lookup tables to the struct, it grows the per-instance memory footprint. On MCU targets (future Daisy/Cortex-M port), every byte of state matters.

**How to avoid:**
1. Keep DAC model state minimal. A memoryless sinc-rolloff model needs zero additional state. A first-order IIR needs 2 int16_t (one per channel).
2. If lookup tables are needed (e.g., pre-computed sinc compensation coefficients), store them as `const` arrays outside the state struct.
3. Document the state struct size delta in the ADR.

**Phase to address:** P-MODEL, P-INTEGRATE

---

### M3: JUCE toggle checkbox wiring (future dependency)

**What goes wrong:** v1.1 shipped with a JUCE toggle for ADPCM coloration (confirmed in PROJECT.md: "JUCE toggle confirmed by user"). The JUCE plugin (future milestone) will need a matching DAC toggle. If the C API toggle semantics differ from ADPCM's (e.g., DAC enable requires re-initialization while ADPCM enable is instant), the JUCE integration will need special handling.

**How to avoid:** Match ADPCM toggle semantics exactly: `spu94_set_dac_enabled(state, 1)` enables immediately, `spu94_set_dac_enabled(state, 0)` disables and clears any model state. No re-initialization required.

**Phase to address:** P-INTEGRATE (API design)

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Implement bitcrusher instead of delta-sigma model | Easy to code, audibly dramatic | Wrong artifact profile for the actual PS1 DAC; misleads users | Never for "DAC model" label; acceptable as separate "lo-fi" effect |
| Use float in the DAC model hot path | Simpler math, no overflow risk | Breaks float-free CI gate; not RT-safe on all targets; inconsistent with core | Never in libspu94.so; acceptable in offline analysis tools |
| Model full delta-sigma modulator at 256fs | Most faithful to hardware | ~256x computation increase; unknowable without datasheet | Never in real-time path; acceptable as offline reference |
| Skip ZOH modeling ("too subtle to hear") | Less code | Misses the one topology-independent artifact that IS modelable | Acceptable for v1.2 MVP if documented as deferred |
| Use AK4309B datasheet specs directly | Concrete numbers to implement | Wrong chip; different revision | Only as starting hypothesis, flagged as LOW confidence |
| Include analog output stage in DAC model | More audible effect | Wrong scope boundary; unverifiable without hardware | Never in v1.2; separate future milestone |

---

## Integration Gotchas (DAC + existing libspu94 pipeline)

| Integration Point | Common Mistake | Correct Approach |
|-------------------|----------------|------------------|
| Signal chain position | Insert inside chain_step_impl at 22.05kHz | Insert AFTER spu94_fir_chain_step at 44.1kHz in spu94_process |
| Toggle API | Require re-init on enable/disable | Match ADPCM pattern: instant enable/disable with state cleanup |
| Latency accounting | Forget to update spu94_get_total_latency_samples | Add DAC model latency (ideally 0) to the accumulator |
| Flush path | DAC model state not drained during spu94_flush | Ensure flush feeds silence through DAC model too (automatic if model is in spu94_process loop) |
| Golden files | Replace existing goldens with DAC-enabled versions | Keep existing goldens (DAC-off); add new DAC-on golden set |
| RT safety gates | New state fields or operations violate rt_safety | Run all 4 rt_safety targets after integration; no heap, no locks, no syscalls |
| In-place processing | DAC model reads output buffer that is aliased to input | Read from chain_step output before modifying; or use temp variable |

---

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Per-sample branching on dac_enabled flag | Branch predictor trains on one path; toggling mid-stream causes mispredictions | Place branch outside tight loop (process DAC-on block or DAC-off block, not per-sample decision) | Rapid toggle during automation |
| Lookup table cache misses | Large coefficient tables evict hot cache lines from reverb work buffer | Keep tables under 64 bytes; prefer computed-on-init coefficients | Always, if tables are >L1 line |
| Full modulator simulation | Per-sample cost 256x expected | Model the EFFECT not the MECHANISM; use IIR/FIR approximation | Immediately; 256x oversampling at 44.1kHz = 11.3M ops/sec |
| Unnecessary per-sample division | Division is 10-40x slower than multiply on ARM | Pre-compute reciprocals at init; use shifts where possible | MCU targets (Cortex-M has no hardware divide) |

---

## "Looks Done But Isn't" Checklist

- [ ] **DAC model:** Often missing ZOH sinc rolloff (the one universal DAC artifact) while over-implementing topology-specific effects -- verify frequency response shows sinc shape
- [ ] **Toggle:** Often missing state cleanup on disable -- verify toggling on/off/on produces same output as always-on for the second segment
- [ ] **Latency:** Often missing from `spu94_get_total_latency_samples()` -- verify function returns correct value with DAC on and off
- [ ] **Flush:** Often not tested with DAC enabled -- verify `spu94_flush()` drains DAC model state
- [ ] **Python binding:** Often missing for new C API functions -- verify `spu94.dac_enabled = True` works from Python
- [ ] **CLI flag:** Often missing for new features -- verify `spu94 process --dac input.wav output.wav` works
- [ ] **ADR:** Often missing for scope boundary decisions -- verify DECISIONS.md has ADR for DAC model scope, topology choice, confidence level

---

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Over-modeled (analog stage in DAC model) | MEDIUM | Extract analog effects to separate stage; may need new ADRs; no data loss |
| Under-modeled (bitcrusher instead of delta-sigma) | MEDIUM | Replace model core; regenerate DAC-on goldens; existing DAC-off goldens unaffected |
| Wrong chain position (22.05kHz instead of 44.1kHz) | HIGH | Restructure integration point; all DAC-on goldens invalid; reverb behavior may have been subtly affected |
| Broke existing tests | LOW-MEDIUM | Revert DAC integration; fix; re-apply; existing goldens serve as regression gate |
| Performance regression | LOW | Simplify model (fewer taps, lower-order filter); profile and optimize hot path |
| Fixed-point overflow | LOW | Add int64_t intermediates; existing UBSAN/ASAN infrastructure catches these |

---

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| C1: Over-modeling | P-RESEARCH, P-DECISIONS | ADR defines scope boundary; no analog parameters in model |
| C2: Under-modeling | P-RESEARCH, P-MODEL | Spectral analysis shows noise shaping, not flat noise |
| C3: Wrong chain position | P-INTEGRATE | ZOH rolloff at correct frequency; 44.1kHz output path |
| C4: Breaking existing behavior | P-INTEGRATE, P-VERIFY | All pre-v1.2 tests pass with DAC disabled |
| C5: Historical accuracy | P-RESEARCH, P-DECISIONS | ADR documents AK4309AVM datasheet unavailability; LOW confidence flags |
| C6: Fixed-point pitfalls | P-MODEL, P-VERIFY | Float reference comparison; UBSAN clean |
| S1: Performance regression | P-MODEL, P-VERIFY | rt_bench_latency still under threshold |
| S2: Verification limits | P-VERIFY, P-DECISIONS | ADR documents what CAN and CANNOT be verified |
| S3: Scope creep | P-RESEARCH, P-DECISIONS | Feature additions require "digital conversion or analog?" test |
| S4: Revision-dependent | P-RESEARCH | ADR names target revision; Anthony's PS1 model identified |
| S5: ZOH confusion | P-MODEL | Frequency response measured; no sample holding/repeating |

---

## Sources

### Primary (HIGH confidence)
- [psx-spx SPU documentation](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- PS1 SPU signal path, 44.1kHz output rate
- [dogbreath.de PS1 DAC page](https://dogbreath.de/PS1/DAC/DAC.html) -- AK4309AVM identification, pin configuration, revision history
- Existing libspu94 codebase: `spu94_process.c`, `spu94_io_chain.c` -- current signal chain architecture

### PS1 DAC measurements (MEDIUM confidence — measure full analog chain, not DAC alone)
- [Archimago SCPH-5501 measurements](http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html) -- ~90dB dynamic range, frequency response, jitter
- [Stereophile PS1 measurements](https://www.stereophile.com/content/sony-playstation-1-cd-player-measurements) -- THD, dynamic range (measured through full output chain)

### AK4309 datasheet fragments (LOW-MEDIUM confidence — AK4309B, not AK4309AVM)
- [AK4309 datasheet on AllDatasheet](https://www.alldatasheet.com/datasheet-pdf/pdf/54935/AKM/AK4309.html) -- 1-bit delta-sigma, SCF output, 256fs/384fs clock
- [AK4309B description on datasheetq](https://www.datasheetq.com/en/AK4309-AKM) -- "16BIT SCF DAC FOR MULTIMEDIA"

### PS1 hardware revision info (MEDIUM-HIGH confidence)
- [ConsoleMods PS1 Model Differences](https://consolemods.org/wiki/PS1:PS1_Model_Differences) -- DAC chip per revision
- [RetroGameTalk PS1 audio](https://retrogametalk.com/threads/why-early-playstation-1-models-are-valued-in-the-audio-world.3598/) -- SCPH-1001 vs later models

### DAC modeling theory (HIGH confidence for theory, LOW for PS1-specific application)
- [DSP Related: DAC Zero-Order Hold Models](https://www.dsprelated.com/showarticle/1627.php) -- ZOH theory, sinc rolloff, MATLAB models
- [All About Circuits: DNL and INL](https://www.allaboutcircuits.com/technical-articles/understanding-dnl-and-inl-specifications-of-a-digital-to-analog-converter/) -- DAC nonlinearity (applies to R2R, NOT to 1-bit delta-sigma)
- [Analog Devices AN-283: Sigma-Delta ADCs and DACs](https://www.analog.com/media/en/technical-documentation/application-notes/292524291525717245054923680458171an283.pdf) -- Delta-sigma noise shaping theory

### Retro audio emulation plugins (MEDIUM confidence — commercial references)
- [Plogue Chipcrusher](https://www.plogue.com/products/chipcrusher.html) -- retro DAC emulation approach (ZOH, PWM, filtering)
- [TAL-Sampler](https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1016253-new-tal-sampler-emulator-ii-am6070-sample-hold-dac-emulation.html) -- DAC emulation in signal chain context

---

*Pitfalls research for: DAC modeling milestone (v1.2) for libspu94*
*Researched: 2026-04-28*
