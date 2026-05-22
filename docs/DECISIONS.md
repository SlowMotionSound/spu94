# SPU-94 — Architecture Decision Records

This log records gray-area resolutions for the SPU-94 project. It is a first-class
deliverable per PROJECT.md: the value of a bit-faithful PS1 SPU reverb reimplementation
is in part the decisions themselves — what was ambiguous, what was chosen, and why.

## Format

Each entry is an ADR in the Michael Nygard style, with an added **Sources** section:

- **Status**: Proposed / Accepted (date, phase) / Superseded-by (ADR-NNNN).
- **Context**: What ambiguity existed and why it had to be resolved.
- **Decision**: What SPU-94 does.
- **Consequences**: Tradeoffs, test obligations, known revision paths.
- **Sources**: Standards, documentation, witness emulators, internal refs
  (paraphrased; see `docs/BIBLIOGRAPHY.md` once Phase 7 creates it).

## Discipline

- Accepted ADRs are **not edited in place** to change the decision. A decision
  reversal requires a new ADR with `Status: Accepted` that references the
  superseded ADR via `Status: Superseded-by ADR-NNNN` on the old entry.
- Prose in ADRs is original SPU-94 wording. Facts (register names, shift
  semantics, coefficient values) are cited via `BIB-NNN` keys pointing to
  `docs/BIBLIOGRAPHY.md` entries (Phase 7 deliverable; placeholder refs are
  acceptable in earlier phases).
- New entries are prepended at the top of this file. Phase 1's seed entries
  (ADR-0001, ADR-0002, ADR-0003) are presented in numerical order below for
  readability of this initial commit.

---

## ADR-0057: VxOUTX capture point for PMON pitch modulation

**Status:** Accepted
**Date:** 2026-05-22
**Phase:** 35 (Pitch Modulation PMON)
**Requirement:** PMON-02, PMON-07

**Context:**

The nocash psx-spx spec states that PMON reads "the previous voice's amplitude"
(VxOUTX) as the pitch modulation factor, but does not explicitly define which stage
in the voice pipeline this amplitude is captured from. There are three possible tap
points in the per-voice processing chain, each producing different modulation behavior:

1. **Post-Gauss, pre-ADSR:** The raw Gaussian interpolation output before the ADSR
   envelope is applied. This would make the modulation factor independent of the
   modulator voice's envelope -- a modulator with a slow ADSR attack would produce
   full-depth FM from tick one, which contradicts observed hardware behavior.

2. **Post-ADSR, pre-volume:** The mono sample after ADSR scaling but before the
   per-voice left/right volume multiply. This makes the modulator's ADSR directly
   control the FM modulation depth: an attack ramp produces increasing modulation
   depth, a release ramp produces fading modulation. The value is mono (no stereo
   panning injected into the modulation chain).

3. **Post-volume:** The stereo output after left/right volume multiplication. This
   would inject stereo panning into the modulation factor, causing the FM depth to
   depend on the modulator's pan position -- behavior not observed on real hardware.

**Decision:**

SPU-94 captures VxOUTX at **post-ADSR, pre-volume** (Step 2.75 in voice_tick). The
stored value is `v->outx = gauss_out` after the ADSR multiply and before the vol_l/vol_r
multiply.

This matches DuckStation's implementation: `voice.last_volume = ApplyVolume(sample,
voice.regs.adsr_volume)` -- this value is then used as the PMON factor for the next
voice in the sequential 0..23 voice processing loop. The volume multiply happens
afterward and is not captured into the PMON factor.

Confidence: **HIGH** (nocash + DuckStation consensus; Phase 34 implementation
confirmed via regression tests that outx is identical regardless of volume sign).

Additionally, the PMON post-modulation pitch clamp uses **0x4000**, not the base pitch
maximum of 0x3FFF. The nocash spec states: "if Step > 3FFFh then Step = 4000h". This
means FM modulation can push the effective pitch step one increment beyond the maximum
base pitch register value. This is intentional hardware behavior -- the clamp at 0x4000
is a distinct value from the 0x3FFF maximum that key_on enforces on the base pitch
register. To accommodate this, the voice_tick Step 4 pitch re-clamp was relaxed from
0x3FFF to 0x4000 in Phase 35 Plan 01.

**Consequences:**

- **ADSR shapes FM depth:** A modulator voice's ADSR envelope directly controls the
  FM modulation depth over time. An attack phase produces increasing modulation depth;
  a sustain phase produces stable modulation; a release phase fades the modulation.
  This is the musically expected behavior and the foundation of PS1 FM synthesis.

- **Volume does NOT affect PMON factor:** Per-voice volume (including negative/phase-
  inverted volumes per SVOL-04, confirmed in Phase 34) does not alter the PMON factor.
  The modulation depth is determined solely by the modulator's ADPCM content and ADSR
  envelope, not by its stereo positioning or volume level.

- **PMON chain stacking reads post-ADSR output:** When voices are chained (e.g.,
  voice 0 -> 1 -> 2), each voice's outx is its own post-ADSR output, not the final
  stereo output. This keeps the modulation chain mono and ADSR-controlled at every link.

- **PMON pitch clamp 0x4000:** A PMON-modulated voice can play at a step rate of 0x4000,
  which is one increment above the normal 0x3FFF maximum. This produces a playback rate
  approximately 0.006% above the maximum base pitch -- negligible musically but
  architecturally significant for spec compliance.

**Sources:**

- nocash psx-spx "SPU ADPCM Pitch" page: PMON formula `Factor = VxOUTX(x-1) + 8000h`,
  pitch clamp `if Step > 3FFFh then Step = 4000h`
- DuckStation spu.cpp: `voice.last_volume = ApplyVolume(sample, voice.regs.adsr_volume)`
  confirms post-ADSR, pre-volume capture point for PMON factor
- Phase 34 implementation (commit 2528d2c): `v->outx = gauss_out` at Step 2.75, with
  regression test proving outx is independent of volume sign (SVOL-04)

---

## ADR-0056: ADSR sustain-decrease and release off-by-one correction

**Status:** Accepted  
**Date:** 2026-05-22  
**Phase:** 33 (ADSR Correction)  
**Requirement:** ADSR-FIX-01, ADSR-FIX-02

**Context:**

The v1.8 ADSR implementation (Phase 28) used base 7 for both decrease formulas:
sustain-decrease computed `-(7 - step)` producing steps -7, -6, -5, -4 for step
values 0..3, and release computed `-(7 - 0) = -7`. The nocash psx-spx specification
states the decrease formula uses base 8, not 7. The decay phase already used -8
correctly (matching the spec), making the sustain-decrease and release formulas
inconsistent with both the spec and with decay.

The error made sustain decay approximately 12.5% slower and release approximately
14% slower than real PS1 hardware. The distinction is that increase formulas
(attack, sustain-increase) correctly use base 7 per spec: `+(7 - step)`, while
decrease formulas (decay, sustain-decrease, release) use base 8: `-(8 - step)`.

This correction was prioritized before Phase 37 (Volume Sweep), which reuses the
same decrease formula for its own step calculation. Fixing now prevents the
off-by-one from propagating into sweep.

**Decision:**

Changed both decrease formulas to use base 8:
- Sustain-decrease step = `-(8 - step)` producing -8, -7, -6, -5 for step values 0..3
- Release step = `-(8 - 0) = -8`

This aligns sustain-decrease and release with the decay phase (which already used -8)
and with the nocash spec. Attack and sustain-increase continue to use base 7, as the
increase formula is correctly `+(7 - step)` per spec.

The two affected lines in `spu94_adsr.c`:
- ADSR_SUSTAIN decrease branch: `(7 - a->sustain_step)` changed to `(8 - a->sustain_step)`
- ADSR_RELEASE branch: `(int32_t)7` changed to `(int32_t)8`

**Consequences:**

- Sustain decay is approximately 12.5% faster, matching hardware behavior.
- Release is approximately 14% faster, matching hardware behavior.
- Two new regression tests (`test_sustain_decrease_step_magnitudes`,
  `test_release_step_base_is_8`) assert the corrected magnitudes at all 4 step values.
- All 10 pre-existing ADSR tests continue to pass unchanged.
- Volume Sweep (Phase 37) will reuse the same `-(8 - step)` pattern for its decrease path,
  inheriting the corrected formula.
- No golden file changes required (no ADSR-specific golden files exist; reverb pipeline
  goldens exercise the reverb network only, not the voice engine).

**Sources:**

- nocash psx-spx "SPU ADSR Generator" section, specifically the AdsrStep table for
  decrease modes (base value 8 for decrease, 7 for increase)
- Phase 28 implementation review (identified the inconsistency with the decay branch
  which already used -8 correctly)

---

## ADR-0055: True oversampled DAC (v1.3) -- audible difference characterization

**Status:** Accepted  
**Date:** 2026-05-01  
**Phase:** 12 (Verification + Characterization)  
**Requirement:** CMP-03

**Context:**

Phase 10-11 replaced the v1.2 44.1kHz FIR approximation with genuine 8x oversampling at 352.8kHz. The central question: does running the AK4309 interpolation cascade at the correct operating rates produce audibly different output compared to the single-rate approximation?

Human listen gate (Phase 11 Plan 02): Anthony confirmed "the character is very subtle."

**Decision:**

The true oversampled DAC (v1.3) is retained as the default path. The differences are measurable and significant in objective terms, though subjectively subtle on typical music material:

- **Frequency response:** 91.83 dB max deviation in 20Hz-20kHz band. The v1.2 path runs all three half-band FIR stages at 44.1kHz, causing severe high-frequency rolloff (the cascaded stopband rejection compounds near Nyquist). The v1.3 path runs each stage at its correct oversampling rate (88.2/176.4/352.8 kHz), preserving the intended passband up to 20kHz. This is the dominant measurable difference.
- **Impulse response:** Peak sample difference = 12078, RMS difference = 748.97. The v1.2 impulse peaks at 6535 while v1.3 peaks at 12062 -- the true oversampled path preserves nearly twice the impulse energy because the correctly-rated FIR stages pass more signal through.
- **Noise floor:** v1.2 = -84.9 dBFS, v1.3 = -84.9 dBFS. Identical -- both modes use the same HP-shaped noise model (spu94_dac_noise_step at 44.1kHz). This confirms the noise path is shared and only the FIR cascade differs.
- **Time-domain (full signal):** Max sample difference = 32042, RMS difference = -8.6 dBFS on a 20Hz-20kHz log chirp. The large RMS difference reflects the v1.2 path's aggressive high-frequency attenuation on wideband content.

The v1.2 path is preserved via `--no-dac-true-oversample` (CLI) and `spu94_set_dac_true_oversample(state, 0)` (API) for A/B comparison and backward compatibility.

**Measurement methodology:**

All measurements performed by `tools/dac_compare.py` using the spu94 CLI with Off preset (no reverb), --dac enabled, --input-gain 1.0, --dry 1.0. Signals: white noise (frequency response), impulse (IR), silence (noise floor), log chirp 20-20kHz (time-domain). 2 seconds at 44100 Hz, int16 stereo. Fixed random seed 0xDAC_F1B for reproducibility.

**Consequences:**

- *Default behavior:* v1.3 (true oversampling) is ON by default. Users get the physically correct model without opting in.
- *CPU cost:* The 8x path processes 8 FIR evaluations per sample versus 1 in v1.2. This is acceptable for desktop real-time (measured well within budget in Phase 10).
- *Audibility:* The objective measurements show large differences (91 dB frequency response, -8.6 dBFS RMS on chirp), but these concentrate at high frequencies where music content is sparse. On typical program material, the difference manifests as subtle high-frequency presence -- consistent with Anthony's listen gate assessment.
- *Characterization evidence:* `tools/dac_compare.py --plot` generates `tools/dac_compare.png` with 4-panel visual comparison.

**Sources:**

- Phase 11 listen gate confirmation (Anthony: "the character is very subtle")
- `tools/dac_compare.py` measurement script output (reproducible with fixed seed)
- `tools/dac_compare.png` visual comparison plots

---

## ADR-0054: AK4309 interpolation filter passband ripple -- datasheet is authoritative

**Status:** Accepted (2026-04-28, v1.2 Phase 5)

**Context:**

The AK4309B datasheet specifies +/-0.05 dB passband ripple for the digital interpolation filter. Stereophile's 1995 PS1 review measured "ripple in the top three octaves" which they attributed to an "underspecified digital filter." This raised a question: does the AK4309's digital filter have more ripple than the datasheet claims, or was Stereophile measuring the composite output of the entire analog chain (digital filter + switched-capacitor filter + continuous-time filter + op-amps + cabling)?

The AK4309 output path is: 8x FIR interpolator -> switched-capacitor filter (SCF) -> continuous-time filter (CTF) -> analog output. The datasheet's +/-0.05 dB spec applies to the digital FIR stage. The SCF and CTF contribute their own passband ripple and rolloff. The -0.2 dB at 20 kHz composite spec covers all stages combined.

Investigation per D-13: no AKM-specific documentation (application notes, AK43xx family datasheets, academic papers) was found confirming the internal architecture of the AK4309's digital interpolation filter. The cascaded half-band architecture used in this implementation is an era-typical engineering assumption supported by universal industry practice for 8x interpolation in 1990s consumer audio silicon, the AK4309's cost-optimized positioning (80mW, 20-pin SSOP), and the implausibility of a single-stage 8x FIR (would require 400+ taps) in mid-90s gate budgets.

**Decision:**

The AK4309B datasheet is authoritative for the digital interpolation filter's passband ripple specification (+/-0.05 dB). Stereophile's measured ripple is attributed to the composite analog output chain, not the digital filter alone. The SPU-94 DAC model reproduces the digital interpolation stage only and makes no claim about reproducing the full PS1 analog output "sound."

The filter is implemented as three cascaded 2x half-band FIR stages (55+11+7 taps = 73 total, 41 non-zero coefficients) designed with the Parks-McClellan (remez) algorithm. The composite cascade achieves 0.078 dB peak-to-peak passband ripple, 49.3 dB stopband attenuation, and -0.016 dB at 20 kHz -- all within datasheet limits with margin.

**Confidence:**
- Digital filter specs (passband ripple, stopband attenuation): HIGH -- the datasheet is explicit and the design meets it
- Cascaded half-band architecture: MEDIUM-HIGH -- industry-standard assumption, not confirmed by AKM documentation
- Full-chain PS1 output reproduction: LOW -- the analog stages (SCF, CTF, op-amps) are not modeled in v1.2

**Consequences:**

The v1.2 DAC model adds the digital interpolation filter's passband ripple character as a toggleable coloration. Users should understand this reproduces the DAC's digital conversion artifacts, not the full PS1 output chain. Hardware calibration (deferred to M5 / DAC-HW-01 through DAC-HW-03) may later refine the model against captures from real PS1 hardware, which would include the analog stages.

The Q15 fixed-point port in Phase 6 must verify that coefficient quantization does not push the composite ripple outside the +/-0.05 dB datasheet spec. The scipy design script verifies this for the designed coefficients; Phase 6 must re-verify after any implementation-specific coefficient adjustments.

**Sources:**
- AK4309B datasheet (AllDatasheet): +/-0.05 dB passband ripple, 41 dB stopband attenuation, -0.2 dB at 20 kHz, "8 times FIR Interpolator"
- Stereophile PS1 review (1995): "ripple in the top three octaves" -- composite measurement, not isolated digital filter
- DSPRelated (Neil Robertson): cascaded half-band as standard 8x interpolation architecture
- Analog Devices MT-017 (Walt Kester): cascaded interpolation as standard for oversampling DACs
- Phase 5 scipy design exploration: empirical verification of 55+11+7 minimum-order cascade

## ADR-0053: ADPCM tail block padding — caller zero-pads to 28 samples

**Status:** Accepted (2026-04-26, M2 Phase 1)

**Context:**

The ADPCM encoder API (`spu94_adpcm_encode_block`) accepts exactly 28 PCM samples per call, matching the fixed block size of the PS1 ADPCM format. The final audio segment in a stream may have fewer than 28 samples remaining. Two design options exist: (a) the encoder handles short blocks internally (accepting a sample count parameter, zero-padding internally), or (b) the caller is responsible for zero-padding the final input array to 28 samples before calling the encoder.

Option (a) adds complexity to the encoder (variable-length input, internal padding logic, potential off-by-one surface). Option (b) keeps the encoder simple (fixed-size I/O) and pushes the padding responsibility to the caller, which already knows the stream length.

**Decision:**

Caller is responsible for zero-padding the input array to 28 samples. The encoder always processes exactly `SPU94_ADPCM_BLOCK_SAMPLES` (28) samples per call. The VAG writer implementation (`spu94_vag_write_mono` in `vag.c`) demonstrates this pattern: it zero-pads the final block before calling the encoder.

**Consequences:**

Simpler encoder API with fixed-size I/O — no variable-length edge cases, no internal buffer management. The VAG end-flag (`block[1]` bit 0) signals the decoder to stop playback; trailing zero-padded samples in the final block are discarded by the reader because the decoder stops processing when the end flag is encountered. This matches PS1 hardware behavior, where the SPU reads fixed-size 16-byte blocks and the end flag terminates voice playback. Callers writing custom stream formats (not VAG) must implement their own zero-padding, but this is a trivial `memset` before the final `encode_block` call.

**Sources:**

- `src/spu94/spu94_adpcm_encode.c` (fixed 28-sample input contract).
- `src/spu94/vag.c` (`spu94_vag_write_mono` — zero-pads final block).
- PS1 SPU hardware behavior (fixed-size block reads, end-flag termination).

---

## ADR-0052: ADPCM encoder tiebreaking — strict less-than with iteration order

**Status:** Accepted (2026-04-26, M2 Phase 1)

**Context:**

The brute-force ADPCM encoder evaluates 65 (filter, shift) combinations per block and selects the one with the lowest sum-of-squared-error (see ADR-0051). When two or more combinations produce identical SSE, the encoder must pick one deterministically to ensure the same input always produces the same output.

Two approaches: (a) explicit tiebreak rules (e.g., prefer lower filter, then lower shift), implemented as secondary comparison logic; (b) rely on iteration order combined with strict `<` comparison, where the first combination found with a given error value wins and subsequent ties are rejected because `error < best_error` is false when `error == best_error`.

**Decision:**

`if (error < best_error)` (strict less-than). The outer loop iterates filter 0-4, the inner loop iterates shift 0-12. With strict `<`, the first combination to achieve the minimum error wins. This means: on ties, the lowest filter index wins; within the same filter, the lowest shift wins. No explicit tiebreak logic is needed — iteration order is the tiebreak.

**Consequences:**

Deterministic output across all compilers and platforms, because the result depends only on the comparison operator and loop order, not on floating-point rounding or compiler optimization choices. The implicit bias favors lower filter indices (simpler prediction, fewer ringing artifacts) and lower shifts (coarser quantization steps) on ties. Test coverage: `test_encode_decode_roundtrip_deterministic` verifies identical output across two runs with the same input.

**Sources:**

- `src/spu94/spu94_adpcm_encode.c` (strict `<` with outer=filter, inner=shift loop order).

---

## ADR-0051: ADPCM encoder error metric — sum of squared error (L2) in int64

**Status:** Accepted (2026-04-26, M2 Phase 1)

**Context:**

The brute-force ADPCM encoder evaluates 65 (filter, shift) combinations per block and must choose the combination that best reconstructs the original PCM. The error metric determines this choice. Three standard options exist:

- **L1 (SAD):** Sum of absolute differences. Linear penalty; treats all errors equally.
- **L2 (SSE):** Sum of squared differences. Quadratic penalty; large deviations are penalized more than small ones, concentrating residual error into many small deviations rather than a few large ones.
- **Linf (max absolute):** Worst-case single-sample error. Minimizes peak distortion but ignores the distribution of errors across the block.

L2 produces perceptually better results than L1 because human hearing is more sensitive to isolated loud artifacts (which L2 penalizes heavily) than to distributed low-level noise (which L2 tolerates). L2 is also the standard metric used by most ADPCM encoders in practice.

The accumulator must handle worst-case overflow: 28 samples x max error^2 = 28 x 65535^2 = approximately 1.2 x 10^11, which exceeds int32 range (2.1 x 10^9) but fits comfortably in int64.

**Decision:**

`int64_t error += (int64_t)diff * (int64_t)diff;` — sum-of-squared-error accumulated in int64. Integer-only, no floating-point. The int64 cast on both operands before multiplication prevents intermediate overflow.

**Consequences:**

Perceptually good results without psychoacoustic modeling complexity. The int64 accumulator is conservative (a 32-bit accumulator would overflow on pathological blocks with large residuals at high shifts), but the cost is negligible — one widening multiply per sample. No floating-point dependencies in the encoder hot path, consistent with the project-wide integer-only DSP discipline. Test coverage: `test_encode_decode_roundtrip_deterministic` confirms consistent winner selection across runs.

**Sources:**

- `src/spu94/spu94_adpcm_encode.c` (L2 metric with int64 accumulation).
- Standard ADPCM encoder practice (L2/SSE is the conventional choice).

---

## ADR-0050: ADPCM division semantics — arithmetic right shift (`>>6`), not division (`/64`)

**Status:** Accepted (2026-04-26, M2 Phase 1)

**Relates:** ADR-0001 (Q15 multiply semantics — truncation direction); ADR-0047 (ADPCM rounding bias, which rides on top of this shift).

**Context:**

The ADPCM prediction formula divides by 64: `(old * f0 + older * f1) / 64`. In C, `>> 6` on a negative signed integer is implementation-defined behavior: arithmetic right shift (ASR) rounds toward negative infinity, while `/64` rounds toward zero. These produce different results for negative predictions. For example, with prediction = -65: ASR gives `(-65) >> 6 = -2`, while truncation-toward-zero gives `(-65) / 64 = -1`. The 1-LSB difference compounds across a 28-sample block and is audible on bass-heavy material.

This is the same fundamental ambiguity as ADR-0001 (Q15 multiply uses `>> 15`, not `/ 32768`), applied to the ADPCM filter domain. The PS1 hardware uses a barrel shifter, which is inherently ASR.

**Decision:**

Use `>> 6` (arithmetic right shift) throughout the ADPCM decoder and encoder prediction paths. This is consistent with the project-wide ADR-0001 discipline that all fixed-point divisions are implemented as ASR, not C division. The existing `_Static_assert((-1 >> 1) == -1, ...)` in `spu94_q15.h` covers this assumption at compile time for all target platforms.

Implementation: `int32_t predicted = (old * f0 + older * f1 + 32) >> 6;` in both `spu94_adpcm.c` (decoder) and `spu94_adpcm_encode.c` (encoder internal decoder).

**Consequences:**

All compilers targeting this project (gcc 11+, clang 14+, arm-none-eabi-gcc) emit ASR for signed right-shift. The ADR-0001 static assert protects this assumption at compile time, so a platform where signed right-shift is not arithmetic will fail to build rather than silently produce wrong audio. No additional test obligation beyond the existing ADPCM round-trip and vector tests.

**Sources:**

- ADR-0001 (Q15 multiply semantics — establishes the project-wide ASR discipline).
- `spu94_q15.h` `_Static_assert` (compile-time guarantee).
- psx-spx SPU ADPCM section (barrel shifter semantics).

---

## ADR-0049: ADPCM filter index 5-7 policy — clamp to filter 4

**Status:** Accepted (2026-04-26, M2 Phase 1)

**Context:**

The ADPCM block header byte encodes the filter index in 3 bits (values 0-7), but only 5 coefficient pairs are defined (indices 0-4). Filter indices 5, 6, and 7 have no documented coefficient values in any Sony SDK reference or psx-spx. A real PS1 game is unlikely to produce these values, but a corrupted or hand-crafted VAG stream could.

Three approaches exist: (a) clamp to the highest defined filter (4), making the decoder deterministic for any input; (b) treat undefined indices as filter 0 (zero coefficients, disabling prediction); (c) leave behavior undefined (risk out-of-bounds array access).

Emulator consensus (DuckStation, Mednafen behavioral output) uses clamping to filter 4. No emulator uses option (b) or (c).

**Decision:**

`if (filter > 4) filter = 4;` in `spu94_adpcm_decode_block()`. Out-of-range filter indices are silently clamped to the highest defined filter. The encoder searches only filters 0-4, so it never produces out-of-range values; this clamp protects the decoder against arbitrary input.

**Consequences:**

Array access into `spu94_adpcm_f0[]` and `spu94_adpcm_f1[]` (5-element arrays) is always in-bounds. Decoder behavior is deterministic for any 16-byte input block, including malformed ones. Test coverage: `test_decode_filter_clamp_5to4` verifies filter 5 produces the same output as filter 4 with identical input.

**Sources:**

- psx-spx SPU ADPCM section (defines only 5 coefficient pairs).
- DuckStation and Mednafen behavioral output (consensus: clamp to 4).

---

## ADR-0048: ADPCM shift 13-15 policy — map to shift 9

**Status:** Accepted (2026-04-26, M2 Phase 1)

**Context:**

The ADPCM block header byte encodes the shift value in 4 bits (values 0-15). The decode formula `nibble << (12 - shift)` is only meaningful for shift 0-12; shift 13, 14, and 15 would produce negative left-shift amounts, which is undefined behavior in C. A real PS1 game should never produce these values (the encoder searches only 0-12), but corrupted or hand-crafted streams could contain them.

Two documented approaches exist: (a) map out-of-range shifts to shift 9 (documented in psx-spx as the hardware behavior), or (b) leave behavior undefined. psx-spx specifically states that shift values 13-15 map to shift 9 on the actual PS1 hardware.

**Decision:**

`if (shift > 12) shift = 9;` in `spu94_adpcm_decode_block()`. All out-of-range shift values (13, 14, 15) are mapped to shift 9, matching the psx-spx documentation of PS1 hardware behavior. The encoder searches only shifts 0-12, so it never produces out-of-range values; this mapping protects the decoder against arbitrary input.

**Consequences:**

The decoder handles any block a PS1 game might have produced, including edge cases that commercial games never exercised. The `12 - shift` expression in the decode loop always produces a non-negative shift amount, avoiding C undefined behavior. Test coverage: `test_decode_shift13_maps_to_9`, `test_decode_shift14_maps_to_9`, `test_decode_shift15_maps_to_9` each verify that the out-of-range shift produces identical output to an equivalent block with shift 9.

**Sources:**

- psx-spx SPU ADPCM section (shift 13-15 map to 9).

---

## ADR-0047: ADPCM prediction rounding — round-to-nearest via +32 bias before ASR

**Status:** Accepted (2026-04-26, M2 Phase 1)

**Relates:** ADR-0050 (division semantics — this ADR adds a rounding bias on top of the ASR that ADR-0050 establishes).

**Context:**

The ADPCM prediction formula computes `(old * f0 + older * f1) >> 6`. The PS1 hardware adds +32 (half of 64) before the right shift, implementing round-to-nearest with 0.5 ULP bias. The alternative is pure truncation (no bias), which rounds toward negative infinity for all values when using ASR.

The difference is 1 LSB in many samples. For example, with filter 1 and old=1000: `(60000 + 32) >> 6 = 938` (with bias) vs `60000 >> 6 = 937` (without bias). This 1-LSB difference accumulates across blocks and causes audible drift on sustained tones, particularly noticeable on bass and pad material where the prediction filter carries most of the signal energy.

The psx-spx documentation includes the +32 bias in its formula. Behavioral witness testing during Phase 1 confirmed that the bias-inclusive formula matches emulator output.

**Decision:**

`int32_t predicted = (old * f0 + older * f1 + 32) >> 6;` in both the decoder (`spu94_adpcm.c`) and the encoder's internal decoder (`spu94_adpcm_encode.c`). The +32 rounding bias is always applied before the arithmetic right shift.

**Consequences:**

Matches every behavioral witness tested (DuckStation, Mednafen output). The bias adds one integer addition per sample (negligible cost). Without the bias, filter 1 predictions on a 1kHz sine wave drift by approximately 4 LSB per block, producing an audible low-frequency warble after several blocks. Test coverage: the existing decode vector tests implicitly depend on the +32 bias (the expected output values were computed with the bias present).

**Sources:**

- psx-spx SPU ADPCM section (formula includes +32 bias).
- Phase 1 behavioral witness testing (M2 development).

---

## ADR-0024: Witness-diff per-preset tolerance gate — `config/witness_diff_thresholds.json`

**Status:** Accepted (2026-04-24, M1 close-out)

**Relates:** Phase 7 Plan 03 D-06 (witness-diff harness is measurement-only; this ADR is the deferred follow-up that lands the gate on top of the numbers). ADR-Phase-4-I (lv2-psx-reverb omits the half-band FIR by design — high-band divergence is informational only). ADR-Phase-6-I (reverb input wiring fix changed the witness-diff numerical baseline; the regen on commit 4fcad49 is the post-Step-2 reference these thresholds are calibrated against).

**Context:**

`scripts/ci/witness_diff.py` renders 50 audio pairs (10 presets × 5 standard inputs) through SPU-94 vs lv2-psx-reverb and writes per-pair split-band aligned-RMS divergence in dBFS to `.artifacts/witness_report.json`. The harness is deliberately measurement-only: it prints numbers, never returns non-zero on magnitude. That was the right shape for Phase 7 — calibrating tolerance numbers in the same step that landed the harness would have either over-tightened (false positives during normal development) or under-tightened (silently passed regressions).

By M1 close-out, the harness has produced a stable post-fix baseline (low-band max divergence ~+1.4 dBFS on the echo preset, ~+0.25 dBFS or below on the other eight non-silent presets, exactly -inf / -360 dBFS on the silent `off` preset). A future regression — e.g. a bad Q15 saturation change, a new buffer-arithmetic bug, a register-write-policy change that drifts apply ordering — would manifest as a bump in these numbers. Without a gate, that regression ships silently to the next milestone.

The gate must satisfy three constraints to be useful rather than annoying:

1. **Tight enough to catch a 2× regression.** A change that doubles echo's +1.4 dBFS to +2.8 dBFS should fail CI on the next push.
2. **Loose enough to absorb measurement noise.** Successive harness runs are byte-identical (witness_determinism asserts this), so noise is zero on the same machine, but the lv2 build is rebuilt fresh every CI run; small numerical jitter from compiler-version differences is plausible.
3. **Algorithmic-difference-aware.** lv2 omits the half-band FIR; high-band divergence is huge by design (~+30 to +50 dBFS at the worst). Gating the high band would gate a known-and-documented difference. Only the low band (≤ 10 kHz) is a valid witness axis.

A separate concern: the `off` preset has both sides go silent, producing low-band divergence of -inf / -360 dBFS. Asserting against any positive ceiling on that pair is meaningless; the gate must skip it explicitly.

**Decision:**

Adopt a per-preset low-band divergence ceiling table at `config/witness_diff_thresholds.json` and a CI gate at `tests/python/test_witness_thresholds.py` (ctest target `witness_thresholds`, label `witness`, depends on `witness_determinism` so a fresh report is on disk before the gate fires).

Initial thresholds:

- echo: `+4.0 dBFS` (covers the current +1.4 dBFS baseline with ~2.5 dB headroom).
- delay, half_echo, hall, room, space_echo, studio_a, studio_b, studio_c: `+3.0 dBFS` (covers the current ≤ +0.25 dBFS baseline with > 2.5 dB headroom; also covers the -1.0 dBFS delay floor from the other side).
- off: explicitly listed in `skip_presets` — silence-vs-silence baseline.

The gate asserts every non-skipped (preset, input) pair satisfies `low_band_diff_dbfs <= preset_threshold`. High-band values are written to the report and printed by the harness but not gated — they remain a measurement-only output for human review per ADR-Phase-4-I.

**Consequences:**

Tradeoffs:

- The thresholds are deliberately liberal at M1 close-out. They are a regression gate, not a correctness gate. M2 (when the lv2 baseline calibration plan lands and the half-band-FIR-equivalence gap is quantified more tightly) will tighten these numbers; the new thresholds will be a normal config-file change without an ADR follow-up unless the policy itself changes (per the "accepted ADRs not edited in place" rule).
- A real divergence regression that stays below the threshold (e.g. a 0.5 dB shift on an arm currently sitting at +0.1 dBFS) will not fire the gate. That is acceptable for the M1 gate's stated purpose: catch the kind of regressions that matter (silent doubling, structural breaks). Smaller drifts will surface in normal `witness_diff.py` print output during development.
- The skip-list for `off` is principled (silence-vs-silence is degenerate), not a fudge. Adding a real signal path to the `off` preset (which is unlikely — it's the documented bypass case) would warrant removing the skip and picking a real threshold; that revision path is a one-line config change.

Test obligations:

- `witness_thresholds` ctest target asserts every non-skipped pair stays at-or-below its threshold.
- `test_witness_thresholds_table_well_formed` (a sub-test in the same file) asserts the threshold table itself is well-formed: nine non-skipped presets each with a numeric threshold, `off` listed in `skip_presets`. Catches typos in the JSON before they ship as silently-disabled gates.

Known revision paths:

- M2 lv2 calibration → tighten thresholds; commit-message references this ADR but does not modify it.
- New witness sources beyond lv2 → may require a per-source threshold dimension; would warrant a new ADR superseding this one.
- `off` becomes non-silent → remove from `skip_presets`, pick a low-band threshold; one-line config change.

**Sources:**

- `scripts/ci/witness_diff.py` D-06 measurement-only contract (this ADR is its deferred-follow-up gate).
- ADR-Phase-4-I (high-band omission; gate excludes high-band as a result).
- ADR-Phase-6-I (Step 2 reverb-input fix and goldens regen; sets the numerical baseline these thresholds are calibrated against).
- `.planning/v1.0-GOLDENS-REGEN.md` (event log for the Step 2 regen, which was the reference point for picking initial threshold values).

---

## ADR-0023: Observable error counters — spu94_get_error_counters + oob_tap_count

**Status:** Accepted (2026-04-24, M1 close-out)

**Relates:** ADR-0022 (work-buf sizing contract — rejects undersized buffers at load_preset time; this ADR adds a runtime observable for the residual cases where callers bypass load_preset by hand-writing m-prefix/d-prefix registers directly); D-23 (read-only observability seams); ADR-Phase-6-I (input anti-aliasing — downstream measurements like the modulation harness now need a correctness invariant stronger than "output is non-silent").

**Context:**

The reverb body's `reverb_buf_read` and `reverb_buf_write` helpers silently return 0 / discard writes when a tap's computed byte offset falls outside `[0, work_buf_size)`. That fail-safe is correct (audio stays bit-deterministic; the 0x7FFFE mask already guarantees the access pattern is hardware-correct modulo buffer-size truncation), but it is INVISIBLE. Three classes of caller bug went unsurfaced as a result:

- A caller that ran `spu94_process` on a state with a hand-configured undersized work buffer could produce audio that sounded reasonable but reflected silent read-zero substitutions at every tap past the buffer end — the reverb character degraded without any runtime signal.
- The modulation harness (`tests/python/modulation_harness.py`) ran with an 8192-byte work buffer for months pre-M1-close-out. Every Hall modulation case was silently under-buffered; the recorded zipper-onset measurements in `docs/LEVERS-CATALOG.md` and `tests/python/modulation_report.json` were taken on corrupted reverb output. ADR-0022 (Step 3) fixed this for preset-based callers but not for callers that hand-write registers.
- Future M4-plugin modulation paths (real-time parameter drive with arbitrary m-prefix/d-prefix values) cannot rely on preset-load validation at all; they need a live signal that says "the last tick read past my buffer".

A secondary motivation: Step 6 of the M1 close-out plan wants to tighten `self_test` and the modulation harness with an "OOB count must equal zero" assertion. That assertion requires the counter to exist first.

**Decision:**

1. **`struct spu94_state` grows one field:** `uint64_t oob_tap_count`, appended at the tail (keeps fuzz-harness byte-offset reprobes out of Step 4 scope per the same D-17 concession used by `reverb_out_l/r`). Zeroed by `spu94_reset` via the existing byte-loop fill.

2. **`reverb_buf_read` and `reverb_buf_write` increment the counter** on the existing `byte_off + 1 >= work_buf_size` bounds check before returning 0 or discarding the write. The helper signature for `reverb_buf_read` drops its `const` on `spu94_state *` (private static-inline in `spu94_reverb.c`; all callers already hold non-const). Audio behavior is unchanged — the fail-safe path is identical, only now observable.

3. **Public snapshot accessor:** `spu94_error_counters_t spu94_get_error_counters(const spu94_state *state)` in `include/spu94/spu94.h`. Returns by value. NULL state → zeroed snapshot (matches the `spu94_get_buffer_address` null-safety convention). The struct is declared in the public header so callers can declare locals; ABI bump is allowed when future counters append at the tail.

4. **Python binding surfaces the snapshot:** `python/spu94/_binding.py` declares the prototype with a `ctypes.Structure` matching the C layout; `python/spu94/api.py::get_error_counters` returns a Python `dict` keyed by field name (`{"oob_tap_count": int}`). The dict shape (rather than a Structure object) absorbs future C struct growth without breaking Python callers that unpack by key.

5. **Unit tests under `tests/unit/state/test_error_counters.c`:** fresh-state zero; NULL-safety; OOB trigger via hand-written m-prefix register past a tight 1024-byte work buffer (bypasses ADR-0022's preset-loader check intentionally — this test proves the counter is wired; the "legitimate configurations produce zero OOB" invariant is owned by Step 6's modulation-harness run with a full-size buffer).

**Consequences:**

- No public API break. `spu94_reset` already cleared the new field via the wholesale byte-loop zero-fill; no lifecycle code touched. Hot path: one uint64 increment on the existing failure branch in `reverb_buf_read` / `reverb_buf_write` — zero overhead on the happy path, negligible on the fail-safe path.
- `spu94_error_counters_t` is first-release ABI as of this ADR. Future counters append AT THE TAIL. Callers that decode the struct by offset (Python binding via ctypes.Structure; C callers via field access) are forward-compatible.
- The "clean operation produces zero OOB" invariant is not directly asserted by the Step-4 unit test because the reverb body reads at `m* - 2` (wraps to halfword 0xFFFE when m* = 0), which produces unavoidable OOB on any work buffer smaller than ~128 KB regardless of preset state. That invariant is Step 6's responsibility — the modulation harness + self_test with `SPU94_WORK_BUF_MAX_BYTES` buffers observe `oob_tap_count == 0` across every preset × input combination as the correctness gate.
- `.planning/v1.0-MILESTONE-AUDIT.md`: the observability-gap axis flips from "gaps_found" toward "passed" once this ADR lands; Step 6 closes the remaining tightening.

**Revision triggers:**

- If multiple error classes appear (e.g., the Q15-accumulator saturation events tracked by the existing `err_*` fields) that deserve counter-style surfacing, add sibling fields to `spu94_error_counters_t` at the tail. Prefer counters that monotonically count *events*, not magnitudes — the `err_*` accumulators are already exposed through a separate M1-deferred observability seam.
- If the M4 plugin wants per-tick counter deltas (not lifetime totals), add `spu94_error_counters_t spu94_diff_error_counters(const spu94_state *, const spu94_error_counters_t *previous)` rather than mutating the existing snapshot accessor. Keep the field semantics monotonic-non-decreasing.
- If a real-time-safety audit ever flags the `oob_tap_count++` as a hot-path regression, guard the increment with a compile-time macro (`SPU94_OBSERVABILITY_COUNTERS` default-on). Current measurement: single uint64 add on an already-unlikely branch — dwarfed by the stage multiply-accumulate costs.

**Sources:**

- `.planning/ARCHITECTURAL-AUDIT.md` Part 6, Step 4 ("observable error counters") — the enumeration driving this ADR.
- `.planning/REVIEW-c-core.md` (observability gaps at reverb-body boundary).
- D-23 (read-only observability seams — the `err_*` and `overflow_magnitude` siblings of `oob_tap_count`).
- ADR-0022 (why the preset-loader gate does not fully subsume this observability — hand-written register callers stay uncovered there).

---

## ADR-0022: Work-buf sizing contract + load_preset argument validation

**Status:** Accepted (2026-04-24, M1 close-out)

**Relates:** D-07 (result-code enum is append-only; existing numeric values are stable); D-14 (`spu94_init` contract); D-13 (caller-owned reverb work buffer); ADR-Phase-6-G (non-Off factory presets carry `vLOUT = vROUT = 0x7FFF` so a correctly loaded preset emits signal out of the box — this ADR extends the "correctly loaded" guarantee to cover work-buf adequacy).

**Context:**

`spu94_load_preset(state, id)` was originally specified to follow the lifecycle-null-safe convention (D-12 / D-14): passing `NULL` for `state` returned `SPU94_OK` as a quiet no-op, and passing an out-of-range `id` returned `SPU94_UNKNOWN_REG`. The tacit assumption was that callers who cared would check the return value and callers who didn't would be working with a freshly `spu94_init`-returned handle that never went NULL in practice.

Two M1 close-out findings broke the assumption:

- **Finding A (ARCHITECTURAL-AUDIT.md Part 6, REVIEW-c-core.md):** The reverb body's tap formulas can read arbitrary byte offsets into the work buffer — bounded by the preset's m-prefix register values, which can reach `0x1ED6` (Space Echo `mLSAME` = 7894 halfwords = 15,790 bytes). The C API required the caller to size the work buffer correctly, but offered no way to query the required size and no runtime check. An under-sized work buffer produced out-of-bounds reads on the first `spu94_tick` after `spu94_load_preset`; the symptoms were quiet distortion or zeroed output, not a loud failure.

- **Finding B (REVIEW-cli-python.md):** `SPU94_UNKNOWN_REG` was overloaded across two semantically distinct error classes — "register id out of range" (the original meaning, emitted by `spu94_set_reg_*` on a bad reg id) and "preset id out of range" (a load-time argument error that happens to be unrelated to register-level I/O). A caller dispatching on `SPU94_UNKNOWN_REG` could not tell which mistake they had made. Similarly, `SPU94_OK` on `NULL` state meant "load_preset silently did nothing"; a caller who missed the NULL check silently produced a state where no preset was loaded at all.

The tightening must preserve D-07's append-only numeric stability: existing codes `0..3` are live ABI, and callers that pattern-match on `if (rc != SPU94_OK)` must continue to work.

**Decision:**

1. **Three new `spu94_result_t` codes**, appended at `4`, `5`, `6`. Existing names and numeric values for `SPU94_OK = 0`, `SPU94_CLAMPED = 1`, `SPU94_UNKNOWN_REG = 2`, `SPU94_TYPE_MISMATCH = 3` are preserved.
   - `SPU94_INVALID_STATE = 4` — a mutation API received `state == NULL`.
   - `SPU94_WORK_BUF_TOO_SMALL = 5` — the caller's `work_buf_size` is smaller than the preset requires.
   - `SPU94_INVALID_ARG = 6` — a mutation API received an argument out of range (e.g., preset id). Distinct from `SPU94_UNKNOWN_REG` which stays scoped to register-id validation on `spu94_set_reg_*` / `spu94_get_reg_*`.

2. **New sizing surface** in the public header (`include/spu94/spu94.h`):
   - `#define SPU94_WORK_BUF_MAX_BYTES 0x80000` — the PS1 SPU's full 512 KiB RAM, guaranteed to fit every factory preset. Callers that don't want to size per-preset can size once against this constant and forget about it. Matches the CLI's existing 512 KB default, so the CLI's work buffer is already at the max.
   - `size_t spu94_preset_min_work_buf_size(spu94_preset_id_t id)` — scans the preset's u16-family register values and returns `(max_halfword_value + 1) * 2` bytes as a conservative upper bound on the highest work-buf byte the reverb network will access. Returns `0` for an out-of-range `id`. Deterministic; `O(SPU94_REG__COUNT)`.

3. **`spu94_load_preset` contract tightened** — before touching any register:
   - `state == NULL` → `SPU94_INVALID_STATE`, no mutation.
   - `id >= SPU94_PRESET__COUNT` (or negative) → `SPU94_INVALID_ARG`, no mutation.
   - `state->work_buf_size < spu94_preset_min_work_buf_size(id)` → `SPU94_WORK_BUF_TOO_SMALL`, no mutation. The caller can `spu94_init` again with a larger work buffer and retry.
   Any of the three failures leaves state bit-identical to its pre-call value; the caller never has to clean up a half-applied preset.

4. **Python binding surfaces the new codes** (`python/spu94/_binding.py`, `python/spu94/__init__.py`) and the new constants (`SPU94_WORK_BUF_MAX_BYTES`). `python/spu94/api.py::load_preset` raises `RuntimeError` on `SPU94_WORK_BUF_TOO_SMALL` with a message naming the exact required size from `spu94_preset_min_work_buf_size` — a correctly-sized work buffer is a configuration prerequisite, not a runtime condition worth silent recovery. The other two new codes pass through as integer return values, matching the existing `SPU94_OK` / `SPU94_UNKNOWN_REG` pattern. `self_test()` now sizes its work buffer against `SPU94_WORK_BUF_MAX_BYTES` (it loads Hall, whose minimum is ~11 KB).

5. **CLI surfaces the new codes** (`src/cli/main.c`). The CLI's work buffer is already `SPU94_WORK_BUF_MAX_BYTES` so `SPU94_WORK_BUF_TOO_SMALL` is unreachable in normal operation, but the return value is now checked and surfaced as a one-line diagnostic on any failure path (discipline matches D-05 one-line-per-error).

**Consequences:**

- Behavior change for two `spu94_load_preset` input cases: `NULL` state flips from `SPU94_OK` → `SPU94_INVALID_STATE`; out-of-range `id` flips from `SPU94_UNKNOWN_REG` → `SPU94_INVALID_ARG`. Callers pattern-matching on `if (rc != SPU94_OK)` are unaffected. Two test files key on the old specific codes: `tests/unit/preset/test_preset_load_all.c` (Tests 1 and 2) and `tests/python/binding/test_binding_numpy_contract.py` (`test_load_preset_unknown_id_*`); both updated in the same commit.
- One test file loads Hall with an 8 KB work buffer (`tests/python/binding/test_binding_preset_table.py`) — that path now returns `SPU94_WORK_BUF_TOO_SMALL` and fails the equality assertion. Updated to `SPU94_WORK_BUF_MAX_BYTES` so the test exercises a fully loaded Hall preset.
- `python/spu94/api.py::init`'s default `work_buf_size=8192` remains a per-step-5 follow-up; Step 3 does not change it. End-user callers who load Hall through `spu94.init()` + `spu94.load_preset(state, "hall")` with the current default now get a clear `RuntimeError` instead of silent under-reads. Step 5 raises the default to `SPU94_WORK_BUF_MAX_BYTES` so the happy path is "just works" even with no `work_buf_size` argument.
- No ABI break. The enum is append-only; `sizeof(spu94_result_t)` is unchanged (still `int`-promoted); existing code paths for codes 0..3 are bit-identical.
- `.planning/v1.0-MILESTONE-AUDIT.md` flagged this gap under "validation missing at API boundaries"; the audit status flips to passed for this axis once Step 3 commits.

**Revision triggers:**

- If a future preset family introduces address registers that are NOT in the u16 signedness family (all current ones are), `spu94_preset_min_work_buf_size` needs to scan by write-policy table instead of by signedness. The current implementation is tight enough for the 10 PS1 factory presets and any forward-compatible variant that preserves the u16 address-register convention.
- If the `mBASE` immediate-policy semantics change (unlikely — locked by ADR-0006), the sizing bound may need to add a mBASE-offset term. Currently `mBASE = 0` for every factory preset, so the bound is computed as if the reverb network started at byte 0.
- If Phase-M4 plugin hosts set register values outside the factory tables (a deliberate design goal for real-time modulation), the sizing query becomes per-live-state rather than per-preset-id. A hypothetical `spu94_state_min_work_buf_size(state)` variant covers that case; out of M1 scope.

**Sources:**

- `.planning/ARCHITECTURAL-AUDIT.md` Part 6 ordered migration plan, Step 3 ("work-buf size contract") — the enumeration of required-vs-done items driving this ADR.
- `.planning/REVIEW-c-core.md` (argument-validation gaps at mutation-call boundaries).
- `.planning/REVIEW-cli-python.md` (CLI silent drop of `spu94_load_preset` return value; Python `load_preset` lacking work-buf diagnostic).
- D-07 (result-code enum append-only + numeric stability).
- D-14 (`spu94_init` contract — why it was OK there to be null-safe but not OK in `spu94_load_preset`).

---

## ADR-Phase-6-I: Reverb input wiring correction — chain_step_impl writes mix_bus_l/r with the 22.05 kHz decimator output, not raw 44.1 kHz samples

**Status:** Accepted (2026-04-24, M1 close-out)

**Amends:** ADR-Phase-5-B ("mix-bus mailbox — two int16 fields on spu94_state"). The mailbox contract and field layout are unchanged; the WRITER moves from `spu94_process` (previously writing raw 44.1 kHz `L_in[i]`) to `chain_step_impl` (now writing the decimator output `dec_l`/`dec_r` on the retained phase, just before `spu94_tick` runs). ADR-Phase-5-B's "spu94_process writes the current 44.1 kHz input sample into the mailbox" bullet is superseded by this ADR.

**Relates:** CORE-06 / CORE-07 (39-tap half-band FIR at the I/O boundaries); the Phase 4 goal of "closing the fidelity gap that lv2-psx-reverb explicitly leaves open"; ADR-0012 (half-rate architecture + lv2-psx-reverb OUT-OF-AXIS exclusion); ADR-Phase-6-G (symmetric sibling: wet-only 44.1 kHz OUTPUT wiring — unchanged; this ADR addresses the INPUT side).

**Context:**

Phase 4 landed the 39-tap linear-phase half-band FIR decimator (44.1 → 22.05 kHz) and interpolator (22.05 → 44.1 kHz) as a bit-faithful sample-rate boundary. The Phase 4 ROADMAP goal is explicit: "SPU-94 is bit-faithful at the I/O boundary — the 44.1 kHz host rate is converted to/from the internal 22.05 kHz reverb rate via nocash's documented 39-tap half-band FIR, closing the fidelity gap that lv2-psx-reverb explicitly leaves open." The decimator's purpose is to band-limit the input before the reverb network sees it at 22.05 kHz, so that HF content above 11.025 kHz does not alias into the reverb.

Phase 5 Plan 02 (ADR-Phase-5-B) added the `mix_bus_l`/`mix_bus_r` fields on `spu94_state` as the seam through which the block-based `spu94_process` entry point feeds per-sample input into the reverb body. At the time, the Phase-5 writer wrote `state->mix_bus_l = L_in[i]` on every 44.1 kHz call — raw int16 sample, no filtering. The intent was "get any input flowing to the reverb end-to-end"; the fidelity story at the boundary was deferred to Phase-5-integration work that never reconciled the seam against Phase 4's decimator.

M1 close-out verification (2026-04-24) surfaced the consequence: the raw `L_in[i]` written by `spu94_process` was the value `spu94_reverb_body` read on every 22.05 kHz tick. The 39-tap FIR ran every call (`chain_step_impl` invoked `spu94_fir_decimate`), but its `dec_l`/`dec_r` output was never threaded into the mailbox — so the reverb effectively saw every-other-44.1-kHz-sample nearest-neighbor downsampling of the raw input. Any HF content above 11 kHz aliased directly into the reverb network.

Two comments in the codebase disagreed about which behavior was intended:

- `src/spu94/spu94_io_chain.c:62-63` (pre-fix): "the dry decimator output (dec_l, dec_r) feeds the reverb INPUT via state->mix_bus_l/r." — the intended design.
- `src/spu94/spu94_reverb.c:606-608` (pre-fix): "spu94_process writes state->mix_bus_l/r with the current 44.1 kHz input sample." — the actual behavior.

Both comments were self-consistent; the code matched the second; the project's bit-faithful claim required the first.

**Decision:**

- `spu94_process` (and therefore `spu94_flush`, which delegates) no longer writes `state->mix_bus_l`/`mix_bus_r`. Those writes are removed.
- `chain_step_impl` (`src/spu94/spu94_io_chain.c`) now writes `state->mix_bus_l = dec_l; state->mix_bus_r = dec_r;` inside the retained-phase branch (`if (dec_valid)`), on the production code path (`reverb_active=1`), immediately before `spu94_tick(state)` fires. The value written is the FIR decimator's 22.05 kHz band-limited output, not the raw 44.1 kHz input.
- The test-only `reverb_active=0` bypass path in `chain_step_impl` is unchanged: it skips `spu94_tick` entirely and routes `dec_l`/`dec_r` straight into the interpolator. It does NOT write the mailbox (no tick will consume it). This preserves the "pure half-band round-trip" contract that the FIR unit tests (`tests/unit/fir/test_fir_impulse.c`, `test_fir_chain_latency.c`, `test_fir_dc.c`, `test_fir_round_trip_transparency.c`, `test_fir_err_overflow_taps.c`) depend on.
- The two contradicting comments in `spu94_reverb.c:606-608` and `spu94_io_chain.c:57-83` are reconciled. Both now describe the same (correct) behavior.

**Consequences:**

- The reverb-input path is now anti-aliased per Phase 4's stated intent. HF content above 11.025 kHz is attenuated by the half-band FIR before reaching the reverb network. Cymbal, sibilance, and noise content stop aliasing into the reverb.
- All 50 golden WAVs will change bits on regeneration — the previous goldens captured the pre-fix aliased behavior. Goldens are regenerated via the CLI (which uses a 512 KB work buffer, unaffected by this wiring fix) in the same commit window; SHA-256 sidecars updated. `.planning/v1.0-GOLDENS-REGEN.md` records the event.
- Witness-diff numbers against lv2-psx-reverb shift (expected: improvement on HF content, since both libraries' reverb-input paths are now comparably band-limited; the out-of-axis exclusion of lv2-psx-reverb on the frequency-response axis per ADR-0012 remains intact).
- Modulation harness output changes; stability invariants hold (the decimator output stays within int16 bounds), but the per-preset acoustic character reflected in the harness now corresponds to real Hall / Space Echo / etc. behavior, not the aliased approximation.
- `test_process_reverb_audible` continues to pass — the audibility assertion is on non-silent Hall output, which is unchanged qualitatively.
- No public-API surface change: `spu94_process` signature, block-size contract, in-place-processing contract, and RT-safety properties are all preserved. `mix_bus_l`/`mix_bus_r` remain internal state fields.

**Revision triggers (future ADRs may amend):**

- If Phase-5 RT-safety measurements tighten to the point where `chain_step_impl`'s per-sample mailbox write shows up as a measurable hot-path cost, consider inlining `dec_l`/`dec_r` directly into the reverb-body input parameter list and retiring `mix_bus_l`/`mix_bus_r` as fields. Current measurement: trivial (two int16 stores per 44.1 kHz sample, dwarfed by the FIR multiply-accumulate).
- If a future MCU port's SPU RAM is addressable as a distinct memory region from the reverb state struct (e.g., backed by the on-die SPU RAM on the PS1), the mailbox indirection may need to move to a register-based ABI. Out of M1 scope.

**Sources:**

- `.planning/ROADMAP.md` Phase 4 goal ("closing the fidelity gap that lv2-psx-reverb explicitly leaves open").
- ADR-Phase-5-B (this one amends its writer-side behavior).
- ADR-Phase-6-G (symmetric sibling — output-path wiring correction).
- ADR-0012 (half-rate architecture posture).
- BIB-011 (nocash psx-spx) — the algorithm is defined at 22.05 kHz; the 44.1 kHz host I/O rate is SPU-94's architectural choice, and the decimator/interpolator are what make that choice bit-faithful.
- `.planning/ARCHITECTURAL-AUDIT.md` Part 3 (detailed resolution of the input-path ambiguity produced during M1 close-out review, 2026-04-24).
- `.planning/REVIEW-c-core.md` CR-01 (the review finding that prompted this fix).

---

## ADR-Phase-7-A: Spec-conformance coverage via three-section markdown + CI-enforced validator

**Status:** Accepted (2026-04-23, Phase 7)

**Resolves:** D-01 (COVERAGE.md single-file three-section structure); D-02 (CI-enforced validator); D-03 (existing tests count as coverage); D-04 (pinned wayback snapshot for spec anchor stability).

**Relates:** Milestone 1 success criterion SC-1 ("every nocash-documented reverb behavior has a passing test with a coverage map in the repo"); TEST-01 (spec-conformance coverage requirement); the 22 D-XX decisions recorded in `.planning/phases/07-verification-golden-files-witness-diff-modulation/07-CONTEXT.md`.

**Context:**

SC-1 requires a coverage map from documented SPU reverb behaviors to passing tests, but the shape of the map is discretionary. Three natural options presented themselves during Phase 7 context-gathering: a single markdown file with three sections, three separate files (one per view), or a behavior × register × paragraph matrix. The matrix option becomes unreadable past ~20 rows. Three separate files split concerns but multiply the maintenance surface when a test name changes. A single file with three sections keeps every view on one screen and keeps the CI validator's job shape narrow: parse every `test:` cell, verify the file exists and the ctest registration passes, fail the build on any gap.

A second dimension: should the coverage map cite the live `psx-spx.consoledev.net` URL or a pinned wayback snapshot? The live URL is a moving target — community renders reorganize, anchors drift, sites occasionally go offline. A pinned wayback snapshot is stable by construction at the cost of one-time URL ugliness.

**Decision:**

- `docs/COVERAGE.md` is a single file with three sections: Per-Register Coverage (35 rows, one per `spu94_reg_t` entry), Per-Behavior Coverage (~20 rows covering SAME/DIFF IIR, comb/APF, hard clip, FIR, write-timing policy, fuzz harnesses, RT-safety), Per-Spec-Paragraph Coverage (one row per psx-spx anchor cited so far).
- `scripts/ci/check_coverage.py` parses every backticked cell containing `::` as a `<path>::<ctest>` coverage reference; verifies the file exists, the ctest name matches the positive allowlist `[A-Za-z0-9_]` (T-07-01-A shell-injection mitigation), and `ctest -R <name>` passes. Build fails on any gap.
- Empty `test:` cells are allowed only inside a `## Known Gaps` section; the validator emits `FAIL: empty test: field outside Known Gaps` on violation.
- Existing tests count as coverage; no obligation to write a new test when an existing one already exercises the behavior. The obligation is that the row names the narrowest existing test, not the broadest.
- Spec citations point at `https://web.archive.org/web/20260114082525/https://psx-spx.consoledev.net/soundprocessingunitspu/` (BIB-015) for citation stability. Live-URL references continue to exist in prose, but the authoritative citation in the coverage map is the snapshot.
- A dedicated `coverage-map-check` GitHub CI job runs the validator on every push and pull request. SHA-pinned `actions/checkout` preserved.

**Consequences:**

- Coverage gaps surface at CI time rather than at release-audit time. Adding a new behavior to the library implies adding a row to COVERAGE.md, which implies wiring a ctest target — the forcing function runs in the right direction.
- The matrix approach was rejected for unreadability past ~20 rows; the single-file-three-sections structure scales to the M1 35-register × ~20-behavior surface without breaking on any one view.
- T-07-01-A (shell-metacharacter injection through a doctored `test:` cell in a PR) is mitigated at source level via the positive allowlist plus `subprocess.run([...], shell=False)` everywhere. Landed in Plan 07-01.

**Sources:**

- BIB-001 (nocash) — the primary spec whose paragraphs are anchored.
- BIB-015 (pinned wayback snapshot) — the citation-stable URL form.
- BIB-017 (strace) — cited elsewhere but not directly in COVERAGE.md.
- Internal: `docs/COVERAGE.md`, `scripts/ci/check_coverage.py`, `tests/conformance/test_coverage_map_integrity.py`, Phase 7 Plan 01 SUMMARY.

---

## ADR-Phase-7-B: Witness-diff split-band measurement-only harness; lv2-psx-reverb is a binary witness at a pinned SHA

**Status:** Accepted (2026-04-23, Phase 7)

**Resolves:** D-05 (build witness fresh each run at a pinned commit); D-06 (measurement-only; tolerance policy deferred); D-07 (split-band divergence honors ADR-Phase-4-I); D-08 (source code never read as primary material).

**Relates:** ADR-Phase-4-I (frequency-response axis exclusion); Milestone 1 SC-2 (divergence numbers against lv2-psx-reverb with per-preset tolerances); PROJECT.md licensing posture (GPL sources off-limits as primary input).

**Context:**

SC-2 asks for divergence numbers with per-preset tolerances against `lv2-psx-reverb`. Two problems present themselves. First, tolerance values cannot be chosen sight-unseen — they depend on what the actual divergence looks like, which only exists after the harness runs once. Second, `lv2-psx-reverb` is GPLv3, and PROJECT.md's licensing posture forbids reading its source as primary material; the harness must interact with it purely as a binary witness.

A third dimension emerges from ADR-Phase-4-I: the frequency-response axis above ~10 kHz is an explicit exclusion — `lv2-psx-reverb`'s own README acknowledges it skips the half-band FIR and produces additional brightness at high frequencies. A single broadband divergence metric would lump that known shift in with legitimate structural differences and produce uninterpretable numbers.

**Decision:**

- `scripts/ci/witness_diff_build.sh` clones `github.com/ipatix/lv2-psx-reverb` at commit SHA `424e1e8ee7f780106b005011b036386513c61db3` (BIB-014, re-verified at Phase 7 execute time via `git ls-remote`) and builds the plugin fresh each CI run. Commit SHA drift fails the build loudly; any witness shift requires a deliberate pin bump.
- `scripts/ci/witness_diff.py` runs SPU-94 and the `lv2-psx-reverb` binary against the same 50-pair standard input set (BIB-014 consumes the goldens corpus). A minimal in-process ctypes LV2 host provides the `urid:map` feature that `lv2apply` from `lilv-utils` does not, so no Python `lilv` bindings enter the dependency footprint. D-08 is preserved: the host is written against the public LV2 C API headers (BIB-018), not against lv2-psx-reverb's source.
- Divergence is measured in two bands: low (≤10 kHz, gated-candidate) and high (>10 kHz, informational). scipy's `sosfiltfilt` splits the band; aligned RMS (via FFT cross-correlation) gives the divergence number per band per pair. ADR-Phase-4-I's exclusion is honored algorithmically, not by convention.
- The harness writes `.artifacts/witness_report.json` and uploads it as a CI artifact. No pass/fail gate on divergence magnitude — Phase 7 ships numbers. The tolerance-policy ADR is a deferred follow-up that will land post-Phase-7 once the first report informs the thresholds.

**Consequences:**

- The first `witness_report.json` is evidence, not a gate — humans read it, propose tolerances, land a follow-up ADR. Premature gating on a speculative threshold is explicitly rejected.
- The pinned commit SHA is a supply-chain boundary: if the `lv2-psx-reverb` repo reorganizes, gets renamed, or goes offline, the pinned SHA keeps the reference stable as long as the object database does. A commit-SHA mismatch halts the build; silent witness drift cannot happen.
- SILENCE and OFF inputs produce degenerate numbers (reference RMS near zero → dBFS floor + saturated lag) in the first run; the tolerance-policy ADR will scope these rows out or use a different metric for bypass behavior. Documented in 07-03-SUMMARY.md.
- Landed in Plan 07-03.

**Sources:**

- BIB-014 (lv2-psx-reverb at the pinned SHA) — the witness binary itself.
- BIB-001 (nocash) — the spec the witness approximates.
- BIB-018 (LV2 plugin specification) — the host-side interface the ctypes harness speaks.
- ADR-Phase-4-I — the frequency-response exclusion this harness respects.
- Internal: `scripts/ci/witness_diff_build.sh`, `scripts/ci/witness_diff.py`, Phase 7 Plan 03 SUMMARY.

---

## ADR-Phase-7-C: Golden-file format, input set, and regeneration discipline

**Status:** Accepted (2026-04-23, Phase 7)

**Resolves:** D-09 (`.wav` + `.sha256` sidecar pair); D-10 (`tests/golden/<preset>/<input>.{wav,sha256}` layout); D-11 (five-input standard set with deterministic parameters); D-12 (regeneration requires an ADR).

**Relates:** Milestone 1 SC-3 (byte-identical goldens across Docker-pinned CI and host dev); TEST-07 (golden-file regression harness); ADR-Phase-7-D (which governs the reproducibility container the goldens are generated in).

**Context:**

SC-3 requires byte-identity between a CI-rendered golden corpus and a host-rendered one. Format, input set, layout, and regeneration discipline are all discretionary. Three natural formats: raw PCM plus metadata, `.wav` with embedded metadata, or `.wav` plus a separate sidecar. Three natural input-set shapes: exhaustive (every plausible test vector), minimal (impulse only), or a curated standard set. Regeneration cadence: automatic on every preset-table change, gated behind an ADR, or opt-in manual.

**Decision:**

- Each golden is a pair: `<input>.wav` plus `<input>.wav.sha256` sidecar. The sidecar carries the SHA-256 hash (BIB-019) of the WAV's exact bytes. Humans can double-click the `.wav` to audition; CI diffs the `.sha256` for byte-identity gating.
- Layout is `tests/golden/<preset>/<input>.{wav,sha256}`. Ten presets × five inputs = 50 pairs = 100 committed files (~14 MB — comfortably within git's sweet spot).
- Standard input set (locked in `scripts/regenerate_goldens.py` as module-level literals — T-07-02-D path-traversal mitigation): `impulse`, `white_noise` (deterministic seed `0x1094_DADA`), `sine_1khz`, `silence`, `sweep` (logarithmic 20 Hz → 20 kHz). All 2 seconds, 44100 Hz, stereo int16, amplitude 16000.
- Regeneration requires a follow-up ADR. The script has a `--check` mode that re-renders and diffs against the committed sidecars; CI's `reproducibility` job invokes `--check` inside the pinned Docker image. Silent mutations of committed `.wav` bytes trip the check; T-07-02-E mitigated by meta-test.
- Determinism environment (`LC_ALL=C`, `TZ=UTC`, `SOURCE_DATE_EPOCH=1704067200`) is set both in the generator and in the reproducibility Dockerfile so host and container operate in the same byte-level environment.

**Consequences:**

- 50 goldens × 2 files = 100 committed artifacts. Small enough to commit, large enough to meaningfully exercise the preset space.
- Sidecars give both byte-identity gating AND human audibility — a single format serves both CI and a human reviewer's ear.
- Regeneration friction lives in the ADR requirement, not the mechanism. Updating a single golden is a deliberate act with justification attached; accidental regeneration cannot happen because `--check` is what CI runs, not `--regenerate`.
- Landed in Plan 07-02.

**Sources:**

- BIB-019 (SHA-256 RFC 6234) — the hash spec the sidecars use.
- BIB-001, BIB-011 — the behavior and preset tables being rendered.
- BIB-014 (lv2 witness) — shares the same input set for witness-diff cross-referencing.
- Internal: `scripts/regenerate_goldens.py`, `tests/conformance/test_goldens_present.py`, Phase 7 Plan 02 SUMMARY.

---

## ADR-Phase-7-D: Reproducibility via base-image digest pin; no per-package apt pins

**Status:** Accepted (2026-04-23, Phase 7)

**Resolves:** D-13 (two-environment scope: host dev + GitHub CI); D-14 (base-image digest is the sole pin); D-15 (digest bump requires new ADR + golden regeneration).

**Relates:** TEST-08 + BUILD-08 (reproducible build environment); ADR-Phase-7-C (the goldens whose byte-identity depends on this digest); the `reproducibility` GitHub CI job.

**Context:**

A reproducible build environment has many axes to pin: OS version, apt package versions, compiler version, CMake version, Python package versions. Per-package apt pins are high-maintenance — a single `apt-get install pkg=version` line can rot within a month as Debian's apt index drops old versions. Base-image digest pins are low-maintenance: pin once, the digest is stable as long as the image manifest is stable.

**Decision:**

- `Dockerfile.repro` pins the base image by digest only:
  ```
  FROM debian:bookworm-slim@sha256:5a2a80d11944804c01b8619bc967e31801ec39bf3257ab80b91070eb23625644
  ```
  (Plan 07-02 execute-time re-verified digest; supersedes the researcher-time digest `sha256:f9c6a2fd2ddbc23e336b6257a5245e31f996953ef06cd13a59fa0a1df2d5c252` from 2026-04-22 per D-14's "whichever is live at plan-execute time" discipline.) BIB-020 describes the digest-pin Docker surface.
- No per-package apt pins inside the Dockerfile — the base-image digest transitively locks every apt-installed toolchain version. Reproducibility is guaranteed against the digest, not against individual package versions.
- Two environments in scope: host dev (the first environment; where SC-3 was first observed to hold) and GitHub CI (the second environment; where SC-3 is continuously verified). Any further environment is out of scope for M1.
- Bumping the digest requires a successor ADR plus full golden regeneration — the digest is the reproducibility anchor, and any change to it must re-establish the byte-identity claim.

**Consequences:**

- Image digest changes silently break reproducibility. This is acceptable because the digest is static until a deliberate D-15 bump, and the bump is an ADR-governed act, not a bot-driven update.
- apt-installed toolchain drift is bounded by the base-image digest's immutability — the image manifest never rewrites itself. If Debian Security backports a fix to `bookworm-slim`, the digest changes; SPU-94 adopts that change via an explicit D-15 bump + regeneration, not via transparent propagation.
- Landed in Plan 07-02; `.dockerignore` (post-hoc fix during smoke test) prevents host-clutter leak into the build context.

**Sources:**

- BIB-020 (Docker image-digest pinning).
- Internal: `Dockerfile.repro`, `.dockerignore`, `.github/workflows/ci.yml` (`reproducibility` job), Phase 7 Plan 02 SUMMARY.

---

## ADR-Phase-7-E: Modulation harness architecture + determinism/stability gate

**Status:** Accepted (2026-04-23, Phase 7)

**Resolves:** D-16 (LEVERS-CATALOG.md split between mechanical AUTO columns and hand-written HAND columns, preserved across regenerations); D-17 (stability + determinism as the gate; audio-rate zipper is character, not defect); D-19 (`list(spu94.Register)` × three modes = 105 parametrized cases).

**Relates:** Milestone 1 SC-4 (register modulation free of zipper noise — reinterpreted in ADR-Phase-7-F); ADR-0005 (Phase 2 split write-timing policy, continuously re-verified by this harness); PROJECT.md "living instrument" directive (Eurorack target treats audio-rate modulation as design point).

**Context:**

SC-4 requires evidence that every SPU register can be modulated without breaking the DSP core. The Eurorack future documented in `ps1-reverb-eurorack.md` treats audio-rate CV modulation as first-class — a parameter changing 11 kHz times per second is a design point, not an edge case. At the same time, "zipper noise" on gain-type registers is a real PS1 hardware phenomenon; the hardware has no parameter smoothing. Any gate that rejects audio-rate zipper is rejecting the PS1 signature.

The question is what the correct gate actually asserts. Two candidates: "the output is smooth" (fails — the core faithfully reproduces the hardware's lack of smoothing) or "the output is deterministic and bounded" (passes — bit-exact repeatability under modulation proves the D-08 split-write-policy works).

**Decision:**

- `tests/python/test_modulation_harness.py` parametrizes `list(spu94.Register)` × `{sine, sweep, random_walk}` = 105 cases. Each case runs a deterministic modulation sequence against one register while the other 34 are held at a preset value, then asserts:
  - Stability: output is bounded (no NaN, no crash, no buffer corruption), works at every rate up to audio-rate ~11 kHz.
  - Determinism: two runs of the same sequence produce bit-exact identical output (verifies ADR-0005's split write-timing policy holds under modulation stress).
- `tests/python/modulation_harness.py` writes `tests/python/modulation_report.json` — classifier + sample-to-sample RMS + SHA256. The report is committed (27 KB) so downstream consumers (the LEVERS-CATALOG writer, the conformance test) don't need to run the harness first.
- `scripts/write_levers_catalog.py` is the idempotent writer for `docs/LEVERS-CATALOG.md`. It populates four mechanical AUTO columns from the report and preserves four hand-written HAND columns across regenerations via a named-group regex that captures the row shape byte-for-byte. AUTO values are a closed set (`free` / `sample-quantized` / `catastrophic` / `~NNN Hz` / `clean through 11 kHz`) so no user-controlled bytes reach the markdown table (T-07-04-D pipe-injection mitigation).
- Zipper onset at ≥500 Hz sine modulation is reported as character, not gated. The catalog records it; SC-4 does not reject it.

**Consequences:**

- Any regression that causes non-deterministic output under audio-rate modulation (a D-08 split-write-policy violation, a concurrency bug, a numerical-instability nondeterminism) surfaces immediately in the determinism gate.
- The M4 macro-lever layer (Room Size, Pre Delay, CV input) consumes LEVERS-CATALOG.md as its empirical register-to-behavior map — no manual audit required at M4 time.
- 211 pytest cases run in ~4 s (105 stability + 105 determinism + 1 report-writer meta), vs. a ~180 s harness-time budget — 45× headroom for Phase 7 extensions.
- Landed in Plan 07-04.

**Sources:**

- BIB-001 (register list + write-timing semantics).
- ADR-0005 (the split write-timing policy the determinism gate verifies).
- Internal: `tests/python/modulation_harness.py`, `docs/LEVERS-CATALOG.md`, `scripts/write_levers_catalog.py`, Phase 7 Plan 04 SUMMARY.

---

## ADR-Phase-7-F: ROADMAP SC-4 reinterpretation — no internal-tick zipper, not smoothness-at-audio-rate

**Status:** Accepted (2026-04-23, Phase 7)

**Resolves:** D-18 (SC-4 reinterpretation for semantic clarity).

**Relates:** ADR-Phase-7-E (the determinism gate that SC-4 now targets); PROJECT.md core-value statement; Milestone 4 (smoothing + named-lever parameter abstraction); Milestone 5 (Eurorack CV).

**Context:**

ROADMAP SC-4's original phrasing — "free of zipper noise on gain-type registers" — was legible when SPU-94 was framed as a plugin-first product where smoothing is expected. With the living-instrument directive making every register CV-addressable at audio-rate and the PS1 hardware itself providing no parameter smoothing, SC-4 as literally written promises something the bit-faithful core cannot deliver without betraying the core value statement.

A musician consuming SPU-94 through a macro-lever UI at M4 should see smoothed parameters. A musician patching SPU-94's raw registers from a Eurorack CV at audio-rate should see the PS1 signature — including zipper, stepping, polarity-flip character. Both are correct; the question is where SC-4 lives on that axis.

**Decision:**

- SC-4 is reinterpreted as: "no internal-tick zipper arising from write-policy violations." The gate is the determinism test in ADR-Phase-7-E, not a smoothness-at-audio-rate promise.
- Zipper / stepping / polarity-flip at audio-rate modulation is catalogued in `docs/LEVERS-CATALOG.md` as character per register, not gated.
- Parameter smoothing is explicitly Milestone 4 (plugin macro-lever layer) and Milestone 5 (Eurorack CV conditioning) scope. The M1 core stays bit-faithful.
- The phrasing in ROADMAP.md is updated to match this reading; the SC-4 close-out artifact is ADR-Phase-7-E's `modulation_report.json` plus the LEVERS-CATALOG character classifications.

**Consequences:**

- SC-4 cannot later be misread as promising smoothness the core does not provide. The PS1-hardware-faithful interpretation is the committed one.
- Anthony's musician audience sees audio-rate zipper as the PS1 signature — character, not defect — consistent with the Eurorack-first framing of `ps1-reverb-eurorack.md`.
- M4 + M5 inherit the smoothing obligation with a clear hand-off: the mechanism is theirs to design, and the M1 core does not pre-solve it.
- Landed as part of the Phase 7 close-out; no code change, only a semantic lock.

**Sources:**

- PROJECT.md — core-value statement and "living instrument" directive.
- ADR-Phase-7-E — the determinism gate that SC-4 now operationally targets.
- Internal: `.planning/ROADMAP.md` SC-4 row; `docs/LEVERS-CATALOG.md` as the character register.

---

## ADR-Phase-7-G: Benchmark gate split — allocation hard-fail, timing report-only

**Status:** Accepted (2026-04-23, Phase 7)

**Resolves:** D-20 (two-signal split: allocations binary, timing noisy); D-21 (benchmark baseline is human-endorsed; CI never auto-updates it).

**Relates:** BUILD-06 (benchmark harness for pathological regressions); Phase 5's `test_no_syscalls.sh` (generalized no-syscalls gate, which this ADR narrows to heap syscalls only as a sibling signal); ADR-Phase-6-G (the self-test unlock dance the benchmark harness mirrors).

**Context:**

BUILD-06 asks for a benchmark harness that catches pathological regressions in `spu94_process`. Two distinct signals hide under that single ask: (1) heap allocations in the hot path — a binary yes/no question with zero tolerance for "yes," (2) timing — a noisy continuous quantity subject to CI runner jitter. A single combined gate forces a tolerance decision that satisfies neither signal: tight enough to catch allocations and it false-positives on timing noise; loose enough to tolerate timing noise and it lets allocations slip through.

**Decision:**

- `tests/rt_safety/hotpath_alloc_gate.sh` runs `strace` with filter `brk,mmap,mmap2,munmap,mremap` (Pitfall 5 belt-and-suspenders — `mmap2` kept even on 64-bit hosts) against a target that calls `spu94_init` + `spu94_load_preset(HALL)` + 100 000 `spu94_process` blocks inside a `[SIGUSR1 START, SIGUSR1 END]` window. Any heap syscall inside the steady-state window hard-fails the gate and the build. BIB-017 anchors the strace interface.
- A paired negative meta-test (`tests/rt_safety/hotpath_alloc_gate_target_with_malloc.c` + WILL_FAIL ctest inversion) allocates 1 MiB inside the hot window and asserts the gate catches it — the gate isn't just a silent pass.
- `tests/benchmarks/test_benchmark.py` uses pytest-benchmark (BIB-016) across 10 presets × 2 block sizes with warmup, GC disabled, and pinned min-rounds. `tests/benchmarks/benchmark_baselines.json` is committed (26 KB stripped); the CI job runs with `continue-on-error: true` and uploads the bench artifact without gating.
- Baseline refresh is a deliberate human act with justification in the commit message. CI never rewrites the baseline.

**Consequences:**

- Real hot-path regressions (allocation sneaking into the DSP) block the merge instantly. The negative meta-test proves the gate is wired live, not silently green.
- Timing noise never blocks the merge — runners jitter, that's the signal's nature, and a noisy signal is not a correctness signal.
- Humans diff the bench artifact against `benchmark_baselines.json` on demand; baseline drift is a conversation, not a bot-driven update.
- Landed in Plan 07-05.

**Sources:**

- BIB-016 (pytest-benchmark) — the timing harness.
- BIB-017 (strace) — the allocation gate's syscall filter.
- Internal: `tests/rt_safety/hotpath_alloc_gate.sh`, `tests/benchmarks/test_benchmark.py`, `tests/benchmarks/benchmark_baselines.json`, Phase 7 Plan 05 SUMMARY.

---

## ADR-Phase-7-H: BIBLIOGRAPHY.md additive-plus-cleanup posture with CI cross-reference gate

**Status:** Accepted (2026-04-23, Phase 7)

**Resolves:** D-22 (additive Phase 7 entries plus cluster-polish pass; not a full rewrite).

**Relates:** DOCS-03 (BIBLIOGRAPHY.md is a first-class M1 deliverable); PROJECT.md licensing posture (paraphrase discipline upheld); every ADR filed in this batch (Sources sections cite `BIB-nnn` entries that must resolve).

**Context:**

Phase 7 introduced new citations (the witness binary's pinned commit, the pinned wayback spec snapshot, pytest-benchmark, strace, LV2, SHA-256, Docker) that did not exist in the Phase-6-era BIBLIOGRAPHY. Three posture options: full rewrite (high effort, risks prose regressions), additive-only (low effort, leaves the file structurally stale), or additive plus targeted cluster-polish (middle path). A fourth option — deferring BIBLIOGRAPHY polish to Milestone 2 — was rejected as it leaves DOCS-03 incomplete at the M1 close-out boundary.

Separately, `BIB-003` and `BIB-004` had been cited as "future" since Phase 1 commit-time; the cross-reference checker shipping in Phase 7 would trip on them. Leaving orphan references in place is inconsistent with a first-class deliverable.

**Decision:**

- BIBLIOGRAPHY.md gains seven net-new Phase 7 entries: BIB-014 (lv2-psx-reverb witness binary, pinned SHA `424e1e8ee7f780106b005011b036386513c61db3`, GPLv3, witness-only posture per D-05 / D-08); BIB-015 (pinned psx-spx wayback snapshot for D-04 citation stability); BIB-016 (pytest-benchmark 5.2.3); BIB-017 (strace); BIB-018 (LV2 specification, host-side use only); BIB-019 (SHA-256 RFC 6234); BIB-020 (Docker image-digest pinning). BIB-003 and BIB-004 (Clang UBSan + GCC no_sanitize) are also promoted from Phase-1-era placeholder status to full entries.
- Entries are clustered into four tiers that reflect consumption role: Primary Sources (the spec + its direct anchors), Secondary Sources (community paraphrase + corroboration), Witness Binaries (independent implementations whose output is diffed but whose source is not read), Tooling References (CI / test / build machinery anchors).
- Tone is polished to match the README voice — confident, factual, no apologetic early-stage language. No factual content is deleted; cleanup is structural plus tone.
- `scripts/check_bibliography_refs.py` enforces that every `BIB-nnn` cited in DECISIONS.md has a matching `### BIB-nnn:` entry in BIBLIOGRAPHY.md. The checker exits non-zero on any dangling reference; paired meta-tests (positive + negative) and a conformance gate (`tests/conformance/test_bibliography_crossref.py`) wire the check into ctest under the label `bibliography`.
- Paraphrase discipline is upheld throughout — no verbatim transcription from source documentation into BIBLIOGRAPHY prose. T-07-06-B (transcribed-prose leak) is mitigated by reviewer attention, not by code; documented here so the obligation is legible.

**Consequences:**

- BIBLIOGRAPHY.md stays a live artifact synchronized with DECISIONS.md rather than a closing-time afterthought. Any future ADR that cites a `BIB-nnn` not yet defined trips CI at commit time.
- The four-tier clustering scales past M1 — M2's ADPCM work will add Primary-Sources entries; M3's DAC work will add Tooling References for analysis oscilloscope software; M5's hardware validation will add Witness Binaries for console recordings.
- This is the last Phase 7 ADR; subsequent Phase 7 close-out is verifier + state machinery, not new decisions.
- Landed in Plan 07-06 (this ADR).

**Sources:**

- All new BIB entries land in this ADR by definition: BIB-003, BIB-004, BIB-014, BIB-015, BIB-016, BIB-017, BIB-018, BIB-019, BIB-020.
- PROJECT.md — the paraphrase discipline this ADR upholds.
- Internal: `scripts/check_bibliography_refs.py`, `tests/conformance/test_bibliography_crossref.py`, `scripts/test_check_bibliography_refs.py`.

---

## ADR-Phase-6-H: Non-Off factory preset tables carry `vLOUT`/`vROUT` = 0x7FFF — master-mix default moves from CLI layer to preset tables

**Status:** Accepted (2026-04-22, Phase 6 close-out)

**Amends:** ADR-Phase-6-G (the "CLI default" Decision bullet 5, and its rejected Alternatives-Considered entry "Writing the CLI's vLOUT/vROUT default into the factory preset tables instead of the CLI layer"). ADR-Phase-6-G's other decisions — wet-only wiring, `reverb_out_l/r` mailbox, `chain_step_impl` routing, the `test_process_reverb_audible` regression gate — remain in force unchanged.

**Relates:** D-01 (two-surface Python binding — raw panel + class); M4 plugin roadmap (the send-knob UI that scales from this default); ADR-0005 (register write-policy routes vLOUT/vROUT as IMMEDIATE — unchanged).

**Context:**

ADR-Phase-6-G closed the M1 shipping bug (dry-passthrough output) by wiring the wet reverb signal into the final 44.1 kHz output. A second-order consequence: the factory preset tables' `vLOUT`/`vROUT` cells, previously 0x0000 "by convention, not algorithmic necessity," now gate the audible output. ADR-Phase-6-G placed the non-zero default at the CLI layer: `src/cli/main.c` wrote `vLOUT`/`vROUT` = 0x7FFF after `spu94_load_preset` for any non-Off preset. The alternative of populating the preset tables directly was considered and rejected, citing the three-source audit invariant (`tests/python/verify_preset_sources.py` cross-checks BIB-011 against BIB-012 cell-by-cell).

Phase 6 close-out verification (06-UAT.md, Gap 1) surfaced the downstream: the CLI `--config` JSON override shape (`{"base": "hall", "overrides": {...}}`) loads the base preset through the library, then applies the override patch — but the CLI-layer auto-unlock only fires on the `--preset` code path, so an override-shape config whose overrides don't explicitly set `vLOUT`/`vROUT` produces silent output. The same trap applies to every future `spu94_load_preset` consumer outside the CLI: Phase 7's witness-diff harness, the M4 plugin, Python raw-panel API users. Each would either stub its toe once or carry the boilerplate forever.

Re-examining the audit-invariant concern: BIB-011 and BIB-012 do not publish per-preset `vLOUT`/`vROUT` values at all. The audit CSVs list 0x0000 for those cells because the sources are silent, not because they agree on a positive value. The three-source audit guarantees that cells where the sources DO publish a value match the in-code table; cells the sources don't address are a separate category where SPU-94 convention fills the value, and that convention is ours to set.

**Decision:**

- The 9 non-Off entries in `src/spu94/spu94_presets.c` (`SPU94_PRESET_ROOM` through `SPU94_PRESET_DELAY`) carry `vLOUT` = `vROUT` = 0x7FFF.
- `SPU94_PRESET_OFF` keeps `vLOUT` = `vROUT` = 0x0000 — muted output is literally what "Off" means.
- `src/cli/main.c` no longer writes `vLOUT`/`vROUT` after `spu94_load_preset`. The post-load unlock block (ADR-Phase-6-G Decision bullet 5) is removed. Both `--preset` and `--config` (override + flat-map) code paths consume the library contract directly.
- `tests/python/fuzz_process.py` drops its HI-04 post-load writes; the non-Off "produces non-zero output within 256 process calls" invariant is now satisfied by the preset tables themselves.
- `tests/unit/process/test_process_block_size.c::fresh_state()` drops its explicit `vLOUT`/`vROUT` writes; preset load alone is sufficient.
- `spu94_presets.c`'s header comment block updates to document the new convention.
- `verify_preset_sources.py` is unchanged; the BIB-011 vs BIB-012 comparison is orthogonal to SPU-94 convention for cells the sources don't address.

**Consequences:**

- Every caller of `spu94_load_preset` (CLI `--preset`, CLI `--config`, Phase 7 harnesses, M4 plugin, Python raw-panel + class API) inherits audible-by-default for non-Off presets. No per-caller boilerplate.
- The `--config` override-shape silent-output trap closes. A JSON override based on `"hall"` that tweaks `vIIR` renders audibly at Hall's master-send level.
- `SPU94_PRESET_OFF`'s output-gating semantics are now a library-level contract, not a CLI-layer convention. Off is off for every consumer, automatically.
- The M4 plugin's send knob scales from unity on preset load — matches every reverb-box and reverb-plugin convention since hardware reverbs shipped.
- Zero DSP-numeric change. Register writes flow through the existing engine-layer setters; the write-policy table (ADR-0005) routes `vLOUT`/`vROUT` as IMMEDIATE; the reverb body's `vLOUT_snap`/`vROUT_snap` read is bit-identical. Every DSP-level test continues to pass on unchanged numerics.
- The preset tables are now self-contained "play me" units. A reader examining `spu94_presets.c` sees the complete state each preset intends — including the master send.

**Alternatives Considered:**

- **Keep ADR-Phase-6-G's CLI-layer default.** Rejected on the close-out evidence: the `--config` override trap is concrete, and every future non-CLI consumer of `spu94_load_preset` would replicate the same boilerplate. Documentation-only fixes shift the cost to every user surface; a one-layer table change pays the cost once.
- **Apply the default inside `spu94_load_preset` itself (preset-dispatched logic).** Rejected: the loader's contract is "copy the table into state." Adding preset-specific special-cases (Off → leave zero, non-Off → force 0x7FFF) hides the default from readers of the table and makes the loader a lookup-plus-rewrite instead of a pure copier.
- **Gate the change behind a CLI flag (`--auto-unlock-send` / `--raw-config`).** Rejected: adds a flag for a behavior the user virtually always wants and never asks for.
- **Per-preset master-send levels (Cathedral softer than Hall).** Deferred: if Phase 7 witness-diff or M4 plugin UX reveals preset-specific master-gain preferences, a future ADR can populate cells individually. 0x7FFF is the "no preference" default, per-row overridable without further ADR.

**Seam:**

- Future non-factory presets (user JSON presets, Phase 7 calibration presets, etc.) inherit the same convention — populate `vLOUT`/`vROUT` to a sensible default. `spu94_load_preset` copies whatever the caller puts in the table; no new seam.
- The M4 send-knob UI binds to `vLOUT`/`vROUT` directly through the Python register IntEnum. All three surfaces (CLI, Python API, plugin) emit the same default after load.
- If "raw panel mode" (load preset but leave master send at 0) becomes a real use case, the clean shape is a sibling `spu94_load_preset_no_mix` entry point that iterates 33 cells instead of 35. Not needed now; documented here for future reference.

**Sources:**

- ADR-Phase-6-G (the direct amendment target); specifically its "CLI default" Decision bullet and the rejected Alternatives-Considered entry about preset-table placement.
- `.planning/phases/06-python-binding-cli/06-UAT.md` Gap 1 (the close-out evidence: `--config {"base": "hall", "overrides": {...}}` produces silent output).
- `src/spu94/spu94_presets.c` (the preset tables amended).
- `src/cli/main.c` (the auto-unlock block removed).
- `tests/python/fuzz_process.py` (the HI-04 workaround removed).
- `tests/unit/process/test_process_block_size.c` (the `fresh_state()` helper simplified).

**Revision Path:**

- If per-preset master-gain preferences emerge (Phase 7 or M4 findings), populate the affected cells individually — one cell edit per preset, no ADR required.
- If a "load preset but hold master send at 0" shape becomes a real use case, land `spu94_load_preset_no_mix` as documented in the Seam section.

---

## ADR-Phase-6-G: Wet-only 44.1 kHz output — chain_step_impl feeds reverb wet into interpolator, not decimator output

**Status:** Accepted (2026-04-21, Phase 6)

**Resolves:** .planning/debug/resolved/reverb-not-in-audio-path.md (the M1 shipping bug where `LeftOutput`/`RightOutput` from `spu94_reverb_output_scale` were `(void)`-cast at `spu94_reverb.c:613-614` and the dry decimator samples fed the interpolator unchanged, producing a dry-passthrough CLI output that sounded identical to the input WAV regardless of preset).

**Relates:** ADR-Phase-5-A (public block-based `spu94_process` entry point); ADR-Phase-5-B (D-05 mix-bus mailbox for reverb INPUT); ADR-0005 (Phase 4 FIR chain composition — decimate → tick → interpolate); Pitfall 4 (ADR-0005: the FIR helper functions each have exactly one call site).

**Context:**

Phase 4 landed the internal 44.1 kHz FIR chain wrapper (`spu94_fir_chain_step`, Plan 03). Phase 5 landed the public block-based entry point (`spu94_process`) on top of it. Both phases left a seam that neither closed: `spu94_reverb_body` computes `int32 LeftOutput, RightOutput` values via `spu94_reverb_output_scale` at the end of every 22.05 kHz tick, but `chain_step_impl` in `src/spu94/spu94_io_chain.c` passed the dry decimator outputs (`dec_l`, `dec_r`) straight into `spu94_fir_interpolate`, and `spu94_reverb.c` closed out the body with `(void)LeftOutput; (void)RightOutput;` under a "Phase 4 FIR will read them when the 39-tap interpolator lands" comment. Phase 4 did not land that wiring; Phase 5's test suite `test_preset_nonzero_tail` documented the gap explicitly ("The 'Off gates non-silent input' interpretation requires the M4 output-bus-through-FIR rewiring which is out of M1 scope") and structured its Off-preset sub-test around silent input so the gap was not test-visible.

The M1 shipping binary therefore produced 44.1 kHz output that was the dry input convolved with the half-band FIR chain — approximately a -6 dB gain and a short FIR ring-down — regardless of which preset was loaded. A 440 Hz sine + Hall preset produced sine peak 2896 → output peak 1449 (exactly -6 dB, matching the half-band attenuation); the "tail" region decayed to silence after the 39-tap FIR ring-down (~40 samples) rather than the 2–3 second reverb decay Hall's preset values would produce if the wet output had actually been routed. A real piano WAV rendered with `--preset hall` sounded identical to the input. The reverb network ran correctly on every tick (state-machine advance, work-buffer writes, IIR/comb/APF numerics all verified by Phase 3's body-level tests); only the scalar output was lost.

The fix is trivial at the wiring level (swap the interpolator's input source). The policy question — what should the 44.1 kHz output signal be — was not. PS1 SPU hardware emitted a separate wet bus (via `vLOUT`/`vROUT`) that the console's main mixer blended with the dry channel at fixed gain. A bit-faithful reimplementation of that bus emits wet only; dry+wet mixing is the host application's job. This ADR records the decision.

**Decision:**

The 44.1 kHz output of `spu94_process` is the reverb body's WET output, scaled by `vLOUT`/`vROUT`, interpolated from 22.05 kHz to 44.1 kHz by the polyphase half-band FIR. No dry-signal blend. No dry-path routing inside libspu94.

Concretely:

- `struct spu94_state` gains two int16 fields `reverb_out_l` / `reverb_out_r` — a reverb wet-output mailbox symmetric with the existing D-05 `mix_bus_l` / `mix_bus_r` input mailbox. Fields are zeroed by `spu94_init` / `spu94_reset` (existing byte-loop covers) and placed at the END of the struct (not adjacent to `mix_bus_*` where they belong logically) so that hand-typed byte offsets in `tests/python/fuzz_process.py` (per D-17) stay valid. `sizeof(struct spu94_state)` grows from 544 to 552; still under `SPU94_STATE_SIZE_MAX`.
- `spu94_reverb_body` at the end of every 22.05 kHz tick writes the `(int16_t)LeftOutput` / `(int16_t)RightOutput` produced by `spu94_reverb_output_scale` into those fields, replacing the prior `(void)` casts.
- `chain_step_impl` on the production path (`spu94_fir_chain_step`, `reverb_active=1`) zeros the mailbox BEFORE calling `spu94_tick`, then feeds `state->reverb_out_l` / `state->reverb_out_r` into `spu94_fir_interpolate` as the 22.05 kHz source samples. `dec_l` / `dec_r` (the dry decimator outputs) are no longer passed to the interpolator — they continue to drive the decimator's delay-line state (`spu94_fir_decimate` writes its delay lines from every 44.1 kHz call) and feed the reverb INPUT via the existing `state->mix_bus_l` / `mix_bus_r` mailbox populated by `spu94_process` before each chain call.
- `chain_step_impl` on the test-only bypass path (`spu94_fir_chain_step_reverb_bypass`, `reverb_active=0`) skips `spu94_tick` AND routes `dec_l` / `dec_r` directly into the interpolator, preserving the pre-ADR "pure half-band decimate → interpolate round-trip" semantics that the DSP-level FIR tests (`test_fir_impulse`, `test_fir_chain_latency`, `test_fir_dc`, `test_fir_round_trip_transparency`, `test_fir_err_overflow_taps`, `fuzz_fir`) rely on for bit-faithful half-band FIR coverage. This bypass path is never reachable from production code: `spu94_process` calls only `spu94_fir_chain_step` (`reverb_active=1`). The `spu94_fir_internal.h` docstring for `spu94_fir_chain_step_reverb_bypass` is revised to document the dual semantics (production path is wet-only; test bypass stays dry-passthrough).
- CLI default: `src/cli/main.c` sets `vLOUT` / `vROUT` to `0x7FFF` after `spu94_load_preset` for any non-Off preset. The 10 factory preset tables intentionally leave those master-mix registers at `0x0000` (see `spu94_presets.c` lines 32–34: "neither nocash nor hitmen publishes per-preset values for those global registers (they are configured by the caller outside the preset surface)"), so without this CLI-level default the rendered audio would remain silent under wet-only wiring even though the user explicitly asked for a named preset. Off is the deliberate exception — its `vLOUT` / `vROUT` stay at 0.
- Regression gate: `tests/unit/process/test_process_reverb_audible.c` is the behavioral test that would have caught the original bug. Three sub-tests:
  1. `test_hall_preset_produces_non_dry_output` — Hall + deterministic noise → sum-of-absolute-values over the primed second half of the output must exceed a per-sample-average threshold of 100 LSB. The Off-preset reference (same input, different preset) must be identically silent.
  2. `test_off_preset_with_noise_input_is_silent` — Off + noise → every output sample is exactly zero. `vLOUT` / `vROUT` = 0 gates the wet path.
  3. `test_hall_preset_tail_decays` — Hall + 10000 primed samples + 4000-sample flush → at least half of the 2 × 4000 tail samples (L+R) are non-zero. FIR-only ring-down decays in ~40 samples; real reverb tail persists for thousands.
- Test surface adjustments:
  - `test_preset_nonzero_tail`: its Off sub-test is restored to the original plan premise "Off + non-silent input → silent output" (the invariant that vLOUT/vROUT = 0 gates the 44.1 kHz output, which is the wet path under wet-only wiring). Non-Off sub-test sets `vLOUT` / `vROUT` = 0x7FFF and bumps feed length to 15000 samples so every preset's reverb network primes within the measurement window (Hall's `dLSAME` delay tap is 4544 halfword ticks = 9088 44.1-kHz samples).
  - `test_process_basic::test_process_impulse_peak_near_latency` is retired to a pass-stub with an inline ADR-Phase-6-G rationale. Under wet-only wiring, a single impulse into `spu94_process` with all registers zero cannot propagate to the output (vLIN gates input to the reverb body, and vLOUT gates output regardless); the internal-FIR group-delay contract is still pinned by `tests/unit/fir/test_fir_chain_latency.c` via the test-only bypass.

**Consequences:**

- libspu94 and the CLI now ship audibly-correct reverb. `spu94 --preset hall input.wav output.wav` produces a wet reverb rendering whose character matches the preset; `spu94 --preset off` produces silence regardless of input. The M1 shipping bug (dry-passthrough CLI output) is closed.
- Dry+wet mixing is deferred to the host. CLI users who want a dry+wet blend can render twice (once with `--preset off` or skip the CLI for dry, once with `--preset hall` for wet) and mix in their DAW at their chosen ratio. This matches the PS1 hardware boundary where the SPU produced a wet bus and the console's main mixer blended.
- A future `--wet`/`--dry`/`--mix` CLI flag is a well-scoped addition if demand surfaces (would require buffering the dry input and blending at the WAV-writer stage). Not in scope for M1; deferred.
- The `reverb_out_l` / `reverb_out_r` mailbox is observable — tests and future host code can read it for metering, analysis, or soft-clip drive logic symmetric to the existing `state->overflow_magnitude` observable. No public accessor is added in this ADR; the fields are internal per the `spu94_state_internal.h` convention.
- The test-only bypass semantics split (dry-passthrough for FIR tests; wet-only for production) adds exactly one `if (reverb_active)` branch to `chain_step_impl`. Blast radius is bounded; the production hot path pays only a predictable branch.
- Future preset table: if a preset-definitive master-mix level becomes a desired feature (e.g., "Hall intentionally has a softer wet gain than Cathedral"), the per-preset `vLOUT` / `vROUT` cells in the preset table can be populated without further ADR — the CLI default would stack additively-correctly with populated preset values (set_vLOUT after load_preset overwrites). Current cells are zero by convention, not by algorithmic necessity.

**Alternatives Considered:**

- **Option B — Dry + wet additive mix inside libspu94.** Rejected: introduces a policy decision (which dry gain? fixed or parameter?) that the PS1 hardware did not make internally. Moving the mix to the host is cleaner, matches the hardware boundary, and keeps libspu94's output contract scalar-simple.
- **Option C — Output `LeftOutput` / `RightOutput` at 22.05 kHz and skip the interpolator.** Rejected: breaks the 44.1 kHz output-rate contract that `spu94_process` and the CLI WAV writer both assume. The polyphase half-band FIR is part of the bit-faithful PS1 reproduction; removing it for wet-output routing would be an architectural regression.
- **Leaving the `reverb_out_*` fields adjacent to `mix_bus_*` in the struct layout.** Rejected for this ADR: shifts the FIR-block byte offsets hand-typed into `tests/python/fuzz_process.py` (per D-17), forcing an offset reprobe for a layout change that is logically orthogonal to the wiring fix. Placing the new fields at the struct tail confines the byte-offset disruption; a future struct audit can relocate if the logical grouping is worth the reprobe cost.
- **Writing the CLI's `vLOUT` / `vROUT` default into the factory preset tables instead of the CLI layer.** Rejected: the preset tables' cell-level provenance is audited against BIB-011 / BIB-012 / BIB-013 (see Phase 5 Plan 01 audit); editing them to add a master-mix default that neither source publishes would break the three-source audit invariant. The CLI-layer default is the right place for a user-facing rendering convenience.

**Seam:**

- The reverb wet-output mailbox pattern (`reverb_out_l` / `reverb_out_r` written by `spu94_reverb_body`, read by `chain_step_impl`) is directly reusable for any future feature that needs the un-interpolated 22.05 kHz wet output — a parallel dry+wet capture test, a future tap-for-metering public accessor, or an M4 host-side CV-on-wet feature all slot in without further state-layout changes.
- The `test_process_reverb_audible` pattern (Off-reference vs. primed-preset output, sum-of-absolute-values over the second-half primed region, explicit priming-duration rationale tied to the longest delay tap) is the template for every future behavioral-audibility test.
- The dual-semantics split on `spu94_fir_chain_step_reverb_bypass` (production wet-only; test dry-passthrough) is the template for any future test helper that preserves old testable-isolation semantics after a production-level contract change.

**Sources:**

- `.planning/debug/resolved/reverb-not-in-audio-path.md` (full investigation, including empirical CLI repro on a 440 Hz sine and the `(void)LeftOutput` / `(void)RightOutput` smoking gun).
- `src/spu94/spu94_reverb.c` pre-fix lines 608–614 (the cast-to-void comment); post-fix lines 608–621 (mailbox write).
- `src/spu94/spu94_io_chain.c` pre-fix lines 45–73 (dry-into-interpolator wiring); post-fix lines 45–107 (wet-only production path with preserved test-only dry bypass).
- `tests/unit/process/test_process_reverb_audible.c` (regression gate born with this ADR).
- `src/spu94/spu94_presets.c` lines 32–34 (factory preset tables intentionally omit master-mix levels).
- Phase 5 Plan 03 Task 3 docstring in pre-fix `tests/unit/preset/test_preset_nonzero_tail.c` (which routed around the bug explicitly, punting the "Off gates non-silent input" behavior to M4).

**Revision Path:**

- A `--wet`/`--dry`/`--mix` CLI flag would amend this ADR (add a new section; status stays Accepted). The libspu94 contract stays wet-only — the flag routes the dry input buffer into the WAV writer as a separately tracked channel and blends at write time.
- If PS1-hardware research surfaces a documented master-mix default for the factory presets, a successor ADR may revise the CLI default to honor that value instead of `0x7FFF`. The default is a user-facing rendering convenience, not a DSP contract, so the revision is low-blast-radius.
- Moving the `reverb_out_l` / `reverb_out_r` fields back to their logical home adjacent to `mix_bus_l` / `mix_bus_r` after a future D-17 offset reprobe is fine; `fuzz_process.py` is the only consumer of the hand-typed offsets, and updating the four `FIR_IDX_*_OFFSET` constants plus the comment block is a one-commit change.

---

## ADR-Phase-6-A: Two-surface Python binding — raw-panel functions + SPU94 class

**Status:** Accepted (2026-04-21, Phase 6)

**Resolves:** D-01 (06-CONTEXT.md Area A) — expose both layers; D-02 (factory presets importable as Python data).

**Relates:** PYBIND-01; PYBIND-04; PROJECT.md "living instrument" directive (every parameter must be reachable from Python for Phase 7's modulation harness); ADR-0005 (Phase 2 split write-timing policy — Phase 6 is a consumer, not a modifier); ADR-Phase-5-D (preset-load atomicity honors the split policy).

**Context:**

Phase 6 exposes the Phase 1-5 C library to Python. Two idiomatic shapes present themselves: (1) a thin module-function surface that mirrors the C API one-to-one — explicit state handle, `spu94.process(state, ...)`; or (2) a class-based surface that owns the handle — `rev = spu94.SPU94(); rev.process(...)`. The discussion in 06-CONTEXT converged on "honest machine on the inside, polite panel on the outside": both, with the class being sugar over the raw layer, not a second implementation with its own state. The raw layer stays the single source of truth for behavior; the class is a forward-only wrapper.

**Decision:**

- `python/spu94/api.py` hosts the raw-panel public functions: `init`, `reset`, `destroy`, `tick`, `process`, `flush`, `load_preset`, `set_reg_i16`, `set_reg_u16`, `get_reg_i16`, `get_reg_u16`, `get_reg_i16_pending`, `get_reg_u16_pending`, `snapshot_registers`, `get_buffer_address`, `get_latency_samples`, `self_test`. Each function takes an explicit `state` handle as its first positional argument.
- `python/spu94/reverb.py` hosts `class SPU94` — the handle-owning wrapper. Every method on `SPU94` forwards 1:1 to `api.*`. The class supports `__enter__` / `__exit__` and tracks `self._state` so double-destroy is idempotent and any post-destroy access raises `RuntimeError("SPU94 instance has been destroyed")`.
- `python/spu94/presets.py` exposes `Preset` (IntEnum, 10 members) and `presets` (dict-like accessor keyed by lowercase-underscore name, by `Preset` enum, or by int id). The underlying data is read from the C `spu94_presets[]` `.rodata` symbol via `ctypes.in_dll`; no hand-typed parallel table.

**Consequences:**

- PYBIND-01 is satisfied by the full raw-panel + class surface; PYBIND-04 is satisfied by the preset accessor.
- Phase 7's modulation-harness (TEST-05) will drive each of the 35 registers via `rev.set_reg(...)` — which auto-dispatches by signedness — without needing Phase 6 code churn. The register IntEnum from Plan 1's runtime reflection is the driver's enum.
- "Two implementations to maintain" concern is mitigated by the discipline that the class is never the sole implementation of any behavior. A bug in `spu94.process` shows up identically in `rev.process()` and `api.process()` — there is one code path.
- `test_binding_numpy_contract.py` pins the surface shape: 17 Task-1 tests on the raw panel + 13 Task-2 tests on the class + shim. Any drift requires touching the test.

**Alternatives Considered:**

- **Class only (no raw panel).** Rejected: closes the door on callers who want explicit state threading (the forthcoming Phase 7 harnesses + future test utilities that juggle multiple states).
- **Raw panel only (no class).** Rejected: the context-manager pattern `with spu94.SPU94() as rev` is what most Python users reach for first; providing only the raw panel would feel pedantic.
- **Class owns state; raw panel is undocumented internal.** Rejected: "both are public" is explicit in D-01; hiding the raw panel would force modulation-style callers to reach into private names.

**Seam:**

- The SPU94 class wraps `api` entries; the `api` module wraps `_binding._lib` entries. Future additions to the public C API land by adding to `_binding.py` → `api.py` → `reverb.py` in that order. No new seams introduced; the three-layer stack matches the one-concern-per-TU grain of the rest of the project.

**Revision Path:**

- If a future phase introduces state that the class needs to track separately (e.g., a Python-side latency compensator), the new state lives on the class instance and the raw panel stays unchanged. A new ADR records the split.

**Sources:**

- 06-CONTEXT.md Area A (D-01, D-02).
- 06-RESEARCH.md § Pattern 5 (ctypes.Structure + in_dll for preset table import).
- 06-01-SUMMARY.md, 06-02-SUMMARY.md (landed API, class, preset accessor).

---

## ADR-Phase-6-B: Runtime reflection + import-time drift detection

**Status:** Accepted (2026-04-21, Phase 6)

**Resolves:** D-06 (runtime reflection builds the IntEnum); D-07 (import-time drift assertions); D-08 (struct-internal offsets stay hand-typed in fuzz scripts).

**Relates:** PYBIND-03; PYBIND-05; ADR-Phase-5-F § seam note ("if Phase 6's ctypes.Structure auto-derives the struct offsets, the hand-synced constants delete cleanly").

**Context:**

Python bindings routinely drift from their underlying C library: register enum added on the C side, Python script still uses the old list; struct grows, the Python side keeps byte-peek offsets that reach into reclaimed memory; preset count changes, array index becomes out-of-range. SPU-94's answer is to treat the live library as authoritative at import time — the Python side carries no parallel typed list of registers, no parallel integer constants, no parallel anything that could diverge silently. Any gap surfaces as a loud `RuntimeError` at `import spu94`.

The open question was which offsets to reflect vs leave hand-typed. The public API functions (register name, hardware offset, type) are reflection-friendly. Struct-internal offsets (pending_mask, fir_idx_*_*) have no public C accessor and are test-private knowledge.

**Decision:**

- `spu94.Register` is built by walking `spu94_reg_name(0..SPU94_REG__COUNT-1)` at import and using `IntEnum(name, members, module=__name__)` so the 35 members are generated from the live library rather than hard-coded. The sentinel `spu94_reg_name(35)` must return NULL; otherwise drift is flagged.
- Import-time drift assertions (all raise `RuntimeError("spu94 library mismatch: ...")` with enough detail to diagnose):
  - `spu94_state_size() <= SPU94_STATE_SIZE_MAX` — catches state growth past the public bound.
  - `len(Register) == SPU94_REG__COUNT == 35` — catches count drift in either direction.
  - `len(Preset) == SPU94_PRESET__COUNT == 10`.
  - Preset-name drift: `_EXPECTED_NAMES` tuple in `presets.py` is compared against the `.rodata`-resident `spu94_presets[].name` strings. Any rename or reorder forces a visible source edit + commit message linking to a DECISIONS.md update — matching the project's "no silent divergences" posture.
- Struct-internal offsets stay hand-typed in the fuzz scripts (`fuzz_process.py`'s `PENDING_MASK_OFFSET`, `FIR_IDX_L_IN_OFFSET`, `FIR_IDX_R_IN_OFFSET`, `FIR_IDX_L_OUT_OFFSET`, `FIR_IDX_R_OUT_OFFSET`) with a clearly-labeled warning block that names D-17, explains why the public binding does not expose them, and gives the `offsetof()` C-probe recipe for recomputing when layout shifts.

**Consequences:**

- A rebuilt library with an added register would: produce a non-NULL response at `spu94_reg_name(35)`, fail the sentinel check in `__init__.py::_reflect_registers`, and halt import with the exact diagnostic needed to upgrade the Python side. Symptomatic drift (mysterious garbage values from a missing enum member) is impossible.
- The import-time cost is ~22 ms on the dev workstation (dominated by the PresetInfo dataclass + 10×35 int16 tuple construction). Paid once per process; well within any sensible import-time budget.
- Struct-offset drift is only partially covered — a growth that moves `pending_mask` higher in the struct while keeping `sizeof` the same would not trip the drift gate. The fuzz invariants themselves catch this symptom: a misaligned peek would see nonsense values and fail the mask / FIR-index bounds. The warning block documents this residual risk.
- 4 drift tests in `test_binding_drift_detection.py` (state-size overflow, register count grown, register count shrunk, positive-case import) are the permanent regression gate.

**Alternatives Considered:**

- **Generate `Register` from a committed Python file instead of reflection.** Rejected: introduces a parallel source of truth exactly what D-06 forbids.
- **Expose struct-internal offsets via a `spu94_debug_offset(field_id)` accessor.** Considered; rejected as scope creep. The import-time `spu94_state_size()` check covers the overwhelming majority of drift-surface, and the fuzz invariants catch the residual. Adding a public accessor for test-private knowledge would conflict with the "no extra public seams" principle.
- **Use `ctypes.Structure` to auto-derive the offsets.** Considered; rejected for Plan 1 because the internal struct layout is not published and pinning it would create an ABI contract we explicitly didn't want. A future tests-only debug layer could lift the constraint if the pattern becomes painful.

**Seam:**

- If struct-internal drift becomes a real problem (unlikely; 5 fuzz scripts have been stable across 5 phases), the path is either (a) a tests-only `spu94_state_layout_t` accessor or (b) promoting the internal header to a tests-include. Both are append-only.

**Revision Path:**

- A future phase may tighten the preset-name drift check to include the preset register values too (paranoid mode: every cell validated). The `_EXPECTED_NAMES` seam accepts the addition without restructuring.

**Sources:**

- 06-CONTEXT.md Area C (D-06, D-07, D-08).
- 06-RESEARCH.md § Pattern 3 (runtime-reflection IntEnum), § Pattern 4 (import-time drift asserts).
- 06-01-SUMMARY.md (landed reflection + drift assertions + 4 drift tests).
- `python/spu94/__init__.py::_reflect_registers` — the single-file implementation.

---

## ADR-Phase-6-C: Strict numpy int16 contract — zero-copy when it holds

**Status:** Accepted (2026-04-21, Phase 6)

**Resolves:** D-09 (strict int16 C-contiguous required on `spu94.process` / `spu94.flush`); D-10 (zero-copy when the contract holds); D-11 (strict contract is more faithful to PS1 hardware, not less).

**Relates:** PYBIND-02; ADR-Phase-5-A (public block-based C entry point takes planar int16 buffers); Phase 5 D-01 (int16 planar contract at the C level).

**Context:**

The Phase 5 C API takes planar int16 buffers (`int16_t *L_in, *R_in, *L_out, *R_out`). Python bindings typically have a choice between strict ("reject anything that isn't int16 C-contiguous") and forgiving ("silently convert on your behalf"). The user raised this directly in Phase 6 discussion: does the PS1 auto-convert audio formats? No — the PS1 SPU is int16 end-to-end, every signal path, every buffer, every register. A Python binding that silently converted float32 to int16 would be adding a conversion layer the hardware never had. Strict is *more* faithful, not less.

The secondary question is implementation: bespoke validation (`isinstance(arr, np.ndarray) and arr.dtype == np.int16 and arr.flags.c_contiguous`) or `numpy.ctypeslib.ndpointer`. Both work; the latter is the idiomatic ctypes+numpy pattern and the error messages compose better.

**Decision:**

- `spu94.process` and `spu94.flush` accept numpy int16 C-contiguous 1-D arrays and nothing else. Enforcement is via `numpy.ctypeslib.ndpointer(dtype=np.int16, ndim=1, flags="C_CONTIGUOUS")` at the argtypes level — the check runs in ctypes C before the C function is called, and no raw pointer crosses the binding boundary for a malformed input.
- Upgrade wrapper `api._raise_upgraded` intercepts `ctypes.ArgumentError` from ndpointer and re-raises a `TypeError` with actionable guidance: the exact float32-to-int16 conversion recipe (`(arr * 32767).clip(-32768, 32767).astype(np.int16)`) for dtype failures; `np.ascontiguousarray(arr)` for C-contig failures; both with the original ndpointer message appended.
- Zero-copy is verified empirically: `test_process_is_zero_copy` writes sentinel values into `L_out` / `R_out` BEFORE calling `process()`, and asserts at least one element changes afterward. If the binding had copied the input to a private buffer and forgotten to copy the output back, the sentinel would survive.
- NULL inputs are supported via a silent-buffer substitution at the Python boundary (ndpointer rejects None). `api._silent_input(n)` caches a module-private zero-filled int16 array; smaller subsequent blocks slice off the front. From the caller's perspective the semantics match the C contract's NULL-substitutes-silence rule.

**Consequences:**

- Callers who pass float32 get a `TypeError` the first time and know exactly how to convert. No "why does my audio sound wrong?" mystery.
- Zero-copy is the default when the contract holds — no per-call allocation, no intermediate `np.ascontiguousarray` calls inside the binding. Phase 7's per-preset golden-file generation runs at full C speed.
- The strict contract is test-locked: 17 tests in `test_binding_numpy_contract.py` defend dtype / contig / length / zero-copy / None / register I/O / preset loading surfaces. A future phase that softens the contract fails that gate.
- The exact error messages are committed to the README. If they change, the README must change with them — the DOCS-04 regression gate does not assert the exact text but the user-facing contract is well-defined.

**Alternatives Considered:**

- **Forgiving binding (auto-convert float32 → int16).** Rejected: adds a conversion layer the hardware never had (D-11 rationale). Auto-conversion also hides bugs (someone writes float32 thinking they're modeling PS1 audio, never realizes the hidden conversion truncates their dynamic range).
- **Bespoke per-call Python validation instead of ndpointer.** Rejected: bespoke validation runs at Python level, adding per-call overhead on the hot path. Ndpointer runs in ctypes C.
- **Accept any dtype + auto-scale to int16 magnitude.** Same rejection as forgiving-binding, plus introduces an arbitrary convention (float32 in [-1,1]? in [-32768,32767]? both are plausible) the hardware does not endorse.

**Seam:**

- If a future phase admits float32 audio at the Python boundary (say, to support a DAW host that works in float32 natively), the path is a new `spu94.process_float32` entry wrapping the int16 contract plus explicit `astype` at the boundary. A new ADR records the addition; the existing int16 contract is not modified.

**Revision Path:**

- If the upgrade wrapper's message text proves insufficient (user reports confusion), iterate the wording without changing the contract. The wrapper is the single centralization point for `process` and `flush`.

**Sources:**

- 06-CONTEXT.md Area D (D-09, D-10, D-11).
- 06-RESEARCH.md § Pattern 2 (ndpointer).
- 06-02-SUMMARY.md (landed 17 numpy-contract tests + zero-copy sentinel pattern).
- 05-CONTEXT.md Area A D-01 (underlying C int16 contract).

---

## ADR-Phase-6-D: Native C CLI — vendored dr_wav + jsmn, polished error shape

**Status:** Accepted (2026-04-21, Phase 6)

**Resolves:** D-03 (native C binary via CMake; dr_wav vendored; CLI binary only); D-04 (Python entry_point shim via `os.execv`); D-05 (non-zero exit + one-line `spu94: error:` stderr on every error path).

**Relates:** CLI-01; CLI-02; CLI-03; CLI-04; PROJECT.md "shipped binary has no Python runtime requirement"; ADR-Phase-6-C (strict numpy contract does not apply to the CLI's internal int16 buffers — they are ctypes arrays handed to C directly).

**Context:**

The CLI exists for two audiences: users who want to render WAV files without touching Python, and users who install via `pip install spu94` and want `spu94 --preset hall in.wav out.wav` to work out of the box. Option (A) is a native C binary that links `libspu94.so` + vendored WAV / JSON libraries and runs with no Python dependency. Option (B) is a Python implementation that re-reads the WAV via `dr_wav` or stdlib `wave` and feeds the binding. The two can coexist via a Python entry-point shim that `exec`s the native binary.

The secondary question is library vendoring hygiene: dr_wav is a single-header WAV library under public-domain / MIT-0 choice license; jsmn is a tiny MIT JSON tokenizer. Both are linked into the CLI binary. Neither can leak into `libspu94.so` — the shared library must stay free of CLI-only dependencies so downstream consumers (Phase 7 witness-diff, Phase 8 MCU cross-compile, M4 JUCE plugin) do not transitively depend on WAV / JSON machinery.

**Decision:**

- `src/cli/main.c` (+ `wav_io.c`, `json_config.c`, `preset_names.c`) compiles to a standalone `spu94` executable via CMake. Argument parsing via `getopt_long`; WAV I/O via `vendor/dr_wav/dr_wav.h`; `--config` JSON parsing via `vendor/jsmn/jsmn.h`. Both vendored headers are included PRIVATE on the CLI target so they never leak into `libspu94.so`.
- Permanent regression gate: `scripts/ci/verify-no-drwav-in-libspu94.sh` runs `nm -D libspu94.so | grep -E 'drwav_|jsmn_'` and asserts zero matches. Wired as a ctest target under the `cli` label (CLI-03).
- Python entry-point shim: `python/spu94/cli.py::main` uses `os.execv` (not `subprocess.run`) to replace the Python interpreter with the compiled binary. `$?` on the command line is the binary's actual exit code — `subprocess.run` would spawn a child and indirect the exit code, breaking the CLI-04 one-line-error contract through the wheel-install path.
- Every error path flows through a single `SPU94_ERROR(...)` macro that centralizes the `spu94: error:` prefix and the newline so no copy-paste drift produces a misshaped message. Per-error text is specified in Plan 3 SUMMARY and quoted verbatim in the README.
- `--config` JSON schema is covered by a separate ADR (ADR-Phase-6-E); this ADR fixes only the CLI binary architecture.

**Consequences:**

- CLI-01 green: `spu94 --preset hall in.wav out.wav` works end-to-end on a compiled binary with no Python.
- CLI-03 green: `libspu94.so` stays free of CLI-only symbols; the regression gate catches any future drift.
- CLI-04 green: 12 tests in `test_cli_error_paths.py` pin the one-line contract byte-for-byte. The README quotes the exact messages.
- Wheel install works transparently: scikit-build-core ships `libspu94.so` + the `spu94` binary next to `__init__.py`; `[project.scripts] spu94 = "spu94.cli:main"` registers the shim; `os.execv` transfers control to the binary with no process-spawn overhead.
- CLI binary size: 117 KB stripped. Shippable in a Python wheel without meaningful impact on download size.

**Alternatives Considered:**

- **Python-side CLI re-implementation.** Rejected: duplicates WAV + JSON machinery at the Python level; makes the CLI slower (Python import overhead dominates for short files); creates a maintenance split between "CLI behavior in C" and "CLI behavior in Python."
- **`subprocess.run` instead of `os.execv` in the shim.** Rejected: adds a Python-to-child-process indirection that breaks the exit-code contract for pipeline users (`spu94 ... && next-step`).
- **Vendored `dr_wav` in `libspu94.so` rather than CLI-only.** Rejected: infects every downstream consumer with a WAV-I/O dependency they don't need. The Phase 5 library specifically ships planar int16 processing with no file-format concerns.
- **Write our own WAV reader instead of vendoring dr_wav.** Rejected: dr_wav is ~9000 lines of well-tested WAV handling covering dozens of format variants. Re-implementing it correctly is weeks of work for zero benefit over a public-domain header.

**Seam:**

- The error macro + nm-audit gate is a reusable pattern for any future vendored dependency. The plan calls out the generalization: "for every vendored lib X, add verify-no-X-in-libspu94.sh."

**Revision Path:**

- If a future phase needs a second CLI (say, a preset-differ), it lives alongside `spu94` in `src/cli/` and inherits the same warning-relaxation + PRIVATE include discipline.

**Sources:**

- 06-CONTEXT.md Area B (D-03, D-04, D-05).
- 06-03-SUMMARY.md (landed CLI binary + vendored libs + 35 behavioral tests + nm-audit gate).
- `src/cli/main.c`, `src/cli/CMakeLists.txt`, `scripts/ci/verify-no-drwav-in-libspu94.sh`.

---

## ADR-Phase-6-E: `--config` JSON — dual shape auto-detect + strict validation

**Status:** Accepted (2026-04-21, Phase 6)

**Resolves:** D-12 (dual shape with auto-detect by `"base"` key); D-13 (integer + hex-string values per register); D-14 (unknown register names are hard errors); D-15 (README showcases the override shape).

**Relates:** CLI-02; ADR-Phase-6-D (the CLI binary that parses the JSON); Phase 5 D-01 (35 canonical register names + signedness per register).

**Context:**

CLI users have two natural use cases for `--config`: everyday tweaking (a named preset with a handful of register overrides) and exact reproduction (every register specified explicitly, e.g., for a golden-file reference). A single flat-map schema forces the everyday case to list all 34 un-tweaked registers verbatim. Two separate flags (`--preset-override` vs `--config-flat`) multiplies CLI surface. The chosen compromise is one `--config` flag with auto-detection: presence of a `"base"` top-level key discriminates override-shape from flat-shape.

Secondary questions cluster around strictness: hex strings yes or no? Unknown register names silent-ignore or hard-error? Out-of-range values silent-clip or hard-error? Project posture across Phases 1-5 is "fail loudly" — config errors are caller bugs, not runtime conditions.

**Decision:**

- Shape auto-detect: top-level `"base"` key present → override shape `{ "base": "<preset>", "overrides": { "<reg>": <value>, ... } }`; absent → flat shape `{ "<reg>": <value>, ... }` with all 35 registers required.
- Value parsing: JSON integer literals (`-32768`, `65535`) and hex strings (`"0x3F00"`, `"-0x8000"`, `"-0x40"`) both accepted. Signed (v-prefix) registers accept negative values; unsigned (d-prefix / m-prefix + mBASE) registers reject negatives.
- Range check per register: signed int16 is `[-32768, 32767]`; unsigned uint16 is `[0, 65535]`. Out-of-range value → non-zero exit + one-line stderr naming the register and the out-of-range value.
- Unknown register name → non-zero exit + one-line stderr listing several valid canonical names (e.g., "vIIR, mBASE, mLCOMB1"). Case-insensitive matching against `spu94_reg_name` results.
- Strict shape check: jsmn's non-strict tokenizer accepts `{ not valid json }` as 3 primitive children; we explicitly reject any top-level key that is not `JSMN_STRING` with an `invalid JSON in '<path>' (non-string key at top level)` message.
- Flat shape requires all 35 registers; missing any → non-zero exit + one-line stderr naming the count found and the count required.

**Consequences:**

- CLI-02 green: 7 tests in `test_cli_config_and_list.py` pin the dual-shape behavior plus both schema edges.
- README users see the override shape first (D-15) — "everyday" entry point. Flat shape documented separately as "exact register specification for golden-file reproduction."
- Hex strings match how register documentation reads. `"0x3F00"` in a config file matches how the same value appears in nocash psx-spx, Sony SDK docs, and `docs/DECISIONS.md`.
- Silent-skip unknowns and silent-clip overflows are both impossible. Every config-related bug is a loud error at CLI parse time, before any audio processing starts.

**Alternatives Considered:**

- **Single flat-map schema only.** Rejected: forces callers into verbose everyday configs and makes typos (register-name dropped accidentally) silently turn into zero-valued register writes.
- **Separate `--preset-override` + `--config-flat` flags.** Rejected: two flags that accept the same file format on different code paths. Auto-detect is simpler.
- **Silent-skip unknown registers with a warning.** Rejected: warnings are easy to miss in a pipeline. Hard errors force the user to fix the typo.
- **Accept float values and auto-scale.** Rejected: same rationale as ADR-Phase-6-C's strict-int16 contract. PS1 registers are exactly 16-bit integers; config files that pretend otherwise are misleading.

**Seam:**

- jsmn tokenizes; our code validates. A future stricter JSON validator (e.g., JSON Schema) would slot in at the same layer without changing the schema.

**Revision Path:**

- A future phase may add a `"comment": "..."` allowed key (silently ignored) to let users annotate their configs. Either a new ADR or a seam addition to this one.

**Sources:**

- 06-CONTEXT.md Area E (D-12, D-13, D-14, D-15).
- 06-03-SUMMARY.md (landed jsmn parser + 7 config-shape tests + error-text pins).
- `src/cli/json_config.c`, `tests/fixtures/sample_override_hall.json`, `tests/fixtures/sample_flat_registermap.json`.

---

## ADR-Phase-6-F: Packaging, README scope, fuzz migration — closing Phase 6

**Status:** Accepted (2026-04-21, Phase 6)

**Resolves:** D-16 (fuzz scripts migrate to new binding); D-17 (struct-internal offsets stay hand-typed in fuzz scripts); D-18 (CMake test wiring unchanged); D-19 (README polished tone); D-20 (11-section README structure); D-21 (manylinux_2_28 Linux wheel); D-22 (Python 3.10+ minimum); D-23 (one wheel per platform, `py3-none-*` tag); D-24 (wheel layout — libspu94.so + spu94 binary inside spu94/ package dir); D-25 (pyproject.toml as single source of truth).

**Relates:** PYBIND-06; DOCS-04; PROJECT.md § Constraints (Python 3 + numpy + scipy + matplotlib + pytest, ctypes not pybind11/cffi, scikit-build-core for wheel, dr_wav vendored for CLI only); Phase 5 precedent (ADR-Phase-5-A..F's "one ADR per locked decision group" approach).

**Context:**

Phase 6's final plan bundles a cluster of smaller decisions that don't each warrant their own ADR but are substantive enough to record. Fuzz migration closes a three-phase-old loop ("Phase 6 will replace the hand-typed register tables" appears in every Phase 2-5 fuzz harness docstring). Packaging sets the precedent for every future wheel SPU-94 ships. README is the DOCS-04 deliverable and the document unfamiliar readers land on.

Per the Claude's Discretion line in 06-CONTEXT: "exact number and split of ADRs appended to `docs/DECISIONS.md`" is planner's call. This ADR groups the three related clusters into one record because each resolution is short and the three are intertwined (the README quotes the wheel filename from the packaging decision; the fuzz migration relies on the binding that the packaging wheel delivers).

**Decision:**

Fuzz migration (D-16, D-17, D-18):

- `fuzz_buffer.py`, `fuzz_reverb.py`, `fuzz_fir.py`, `fuzz_process.py` all drop their hand-typed register-enum tuples, state-size constants, type-classifier tables, and per-function argtype / restype declarations. Replaced by `from spu94 import Register, SPU94_REG__COUNT, SPU94_STATE_SIZE_MAX, ...` + `from spu94._binding import _lib`. Register partitions (I16_REGS / U16_REGS) derived at import time via `spu94_reg_type` over the reflected Register enum.
- Struct-internal offsets in `fuzz_process.py` (PENDING_MASK_OFFSET and four FIR_IDX_*_OFFSET constants) remain hand-typed per D-17. Warning block strengthened to name D-17 explicitly and to document the C-probe recipe for recomputing offsets when layout shifts.
- `tests/python/CMakeLists.txt` is unchanged (D-18) — same ctest topology, same `SPU94_LIB=$<TARGET_FILE:spu94_shared>` env wiring.

README (D-19, D-20):

- Polished product-doc tone throughout. No apologetic framing; status is communicated via a dedicated status block.
- 11 sections in the locked D-20 order: hero paragraph (before first `## `) → Current state → Quick install → Python walkthrough → CLI walkthrough → For the DSP-curious → Roadmap → Architecture overview → Licensing posture → Acknowledgments → Contributing.
- Content contract enforced by `scripts/ci/verify-readme-sections.sh` (permanent ctest regression gate under label `docs`): every required section heading in document order, plus 11 required content tokens (`pip install spu94`, `cmake --build build`, `spu94 --preset hall`, `spu94.SPU94`, `import spu94`, `vIIR`, `39-tap`, `Q15`, `dr_wav`, `jsmn`, `LICENSE`).

Packaging (D-21..D-25):

- `manylinux_2_28` Linux wheel pinned via `manylinux-x86_64-image = "manylinux_2_28"` in `[tool.cibuildwheel]`.
- `requires-python = ">=3.10"` in `[project]`; classifiers list 3.10 through 3.13.
- `wheel.py-api = "py3"` produces a single `py3-none-*` tag per platform (valid because SPU-94 is pure ctypes with no Python C API).
- SKBUILD-guarded `install(TARGETS ...)` rules in `src/spu94/CMakeLists.txt` + `src/cli/CMakeLists.txt` drop `libspu94.so` + `spu94` binary inside the `spu94/` package dir next to `__init__.py`. Plain `cmake -B build` is unaffected.
- `pyproject.toml` holds every piece of build configuration (build backend, project metadata, `[project.scripts]`, `[tool.scikit-build]`, `[tool.cibuildwheel]`). No setup.py, no setup.cfg, no separate config file.
- Wheel-tag regression gate: `scripts/ci/verify-wheel-tag.sh` enforces the `py3-none-*` tag shape in relaxed mode (dev builds) and `py3-none-manylinux_2_28_x86_64` in strict mode (`SPU94_WHEEL_STRICT=1`, CI mode).

**Consequences:**

- PYBIND-06 and DOCS-04 both close in this plan.
- Fuzz migration removes ~150 lines of hand-typed parallel truth across the four scripts; any future register addition lands in one place (the C enum) and every fuzz script picks it up at next `import spu94`.
- README is a permanent deliverable. Any future phase that drifts the CLI error text, the Python API shape, or the `vIIR` / `39-tap` / `Q15` DSP-curious content must update the README — or the docs gate fires.
- Wheel installs work on every Linux distribution with glibc >= 2.28 (Ubuntu 20.04+, Debian 11+, RHEL 8+, Fedora 30+) across Python 3.10 through 3.13+. Single wheel per platform; cibuildwheel CI produces it.
- manylinux2014 (CentOS 7 base) is NOT supported. A future ADR revises if a user demonstrates the need.
- `pyproject.toml` is the single-file edit surface for every build concern. Flipping MIT ↔ Apache-2.0 at the end of M1 is a LICENSE edit; no pyproject change needed. Adding a Windows wheel is a cibuildwheel matrix extension; no structural change.

**Alternatives Considered:**

- **Separate ADRs per decision cluster** (one for fuzz migration, one for README, one for packaging). Rejected: each cluster is short, and the three are interrelated enough that readers benefit from seeing them together.
- **Per-Python-minor wheels** (cp310-*, cp311-*, etc.). Rejected: pure-ctypes binding means every wheel would be byte-identical except the filename. `py3-none-*` ships exactly what's needed.
- **Windows wheels and macOS wheels in Phase 6.** Rejected for scope reasons. scikit-build-core + cibuildwheel both support them; a future ADR adds them once demand is demonstrated.
- **Handle-typed register offsets as tests-only debug accessors** (deferring D-17 to an ADR of its own). Rejected: the decision is compact enough to fold here; the offset-preservation discipline is already well-documented in the fuzz scripts themselves.

**Seam:**

- Fuzz scripts' sys.path prepend is reusable for any future tests-only Python script that needs the binding but doesn't want to require `pip install -e .`.
- `scripts/ci/verify-*.sh` pattern (one shell script per regression gate, wired as its own ctest target) is now the project-wide template — matches Phase 1's verify-no-heap-symbols.sh, Phase 6's verify-no-drwav-in-libspu94.sh, verify-readme-sections.sh, verify-wheel-tag.sh.

**Revision Path:**

- Future phase may tighten the strict-mode wheel-tag check to include a `python -c "import spu94; spu94.self_test()"` smoke test inside the manylinux container. The script is structured to extend.
- Future phase may migrate fuzz scripts to use the `SPU94` class instead of raw `_lib.*` calls once the class surface stabilizes further. The sys.path prepend accommodates this without structural change.

**Sources:**

- 06-CONTEXT.md Area F (D-16, D-17, D-18), Area G (D-19, D-20), Area H (D-21, D-22, D-23, D-24, D-25).
- 06-04-SUMMARY.md (landed pyproject.toml + SKBUILD install rules + wheel-tag gate).
- 06-05-SUMMARY.md (this plan — README + fuzz migration + the ADR you are currently reading).
- `pyproject.toml`, `README.md`, `scripts/ci/verify-wheel-tag.sh`, `scripts/ci/verify-readme-sections.sh`.
- Memory file `feedback_user_facing_docs_polished.md` (README tone guidance: polished confident, not apologetic).

---

## ADR-Phase-5-F: Mid-stream register writes are first-class at any granularity

**Status:** Accepted (2026-04-20, Phase 5)

**Resolves:** D-10 (05-CONTEXT.md Area F); D-10a (fuzz_process.py 10^6-step harness); D-10b (block-size sweep + in-place + preset-roundtrip + flush correctness).

**Relates:** API-06; PROJECT.md "living instrument" directive; ADR-0005 (per-register split write-timing policy + swappable table); ADR-Phase-5-A (public block-based entry point shape); ADR-Phase-5-D (preset-load atomicity honors the same split policy).

**Context:**

PROJECT.md's "SPU-94 is a living instrument, not a preset engine" Key Decision promises that every parameter that moves in the original PS1 reverb algorithm must be runtime-controllable, glitch-free, and ready for modulation or CV control. The Phase 2 ADR-0005 split write policy (IMMEDIATE for the 12 v-prefix + mBASE; TICK_LATCHED for the 22 d-prefix and m-prefix families) already resolves the mid-tick correctness question: a gain can change mid-multiply; an address latches at the next tick so the L/R address pair stays consistent. What Phase 5 has to prove is that the PUBLIC API level — the block-based spu94_process plus spu94_flush plus spu94_load_preset plus interleaved spu94_set_reg_* calls — does not crash, corrupt state, or emit out-of-range outputs under arbitrary orderings of these operations. A 10^6-step random-walk harness is the standard precedent (Phase 2 fuzz_buffer, Phase 3 fuzz_reverb, Phase 4 fuzz_fir).

**Decision:**

- `spu94_process` tolerates `spu94_set_reg_i16` / `spu94_set_reg_u16` calls interleaved at any frequency — any of the 35 registers, any block boundary, any sub-block granularity. The guarantee is enforced by the Phase 2 ADR-0005 machinery; Phase 5 adds no new write-policy surface.
- Proof at scale: `tests/python/fuzz_process.py` drives 1000000 random steps (seed 0x05F05EED). Each step is one of {write_i16_reg, write_u16_reg, process(random_block_size), flush(random_length), load_preset(random_id)}, uniformly sampled. Six per-step invariants must hold:
  1. No crash, no UBSan trip, no ASan trip (any ctypes SIGSEGV becomes a Python FatalError and fails the harness).
  2. Output samples within the int16 domain (bulk min/max slice check on every process and flush output).
  3. `spu94_get_buffer_address(state)` is even OR equals the current mBASE (halfword exception from the Phase 2 Plan 05 mBASE-snap-on-write resolution).
  4. FIR delay-line indices `fir_idx_{l,r}_{in,out}` in [0, 39) (hand-synced struct-offset peek mirrors the fuzz_fir.py CANARY_OFFSET pattern; struct-offset guard at startup via `spu94_state_size()` matches the WR-02 discipline).
  5. `pending_mask` top 29 bits zero (bits 0..34 cover `SPU94_REG__COUNT` = 35).
  6. After a non-Off preset load, at least one non-zero output sample appears within 256 contiguous process calls (patience amortizes the FIR group delay for small blocks).
- Test vectors beyond the fuzz (D-10b): `tests/unit/process/test_process_block_size.c` proves block-size invariance across `{1, 2, 3, 4, 7, 16, 64, 128, 441, 1024, 4096}` — all grouping sizes produce bit-identical output from a fresh+Hall-preset-loaded+one-ticked state. `tests/unit/process/test_process_in_place.c` proves in-place (`L_out == L_in`, `R_out == R_in`) output is bit-identical to out-of-place under matched initial state.
- Plan 03's `test_preset_nonzero_tail` pins the flush correctness axis: non-Off preset + deterministic noise input + 1000-sample flush produces a non-silent tail; Off preset + silent input produces silent output.

**Consequences:**

- API-06 is fully discharged. The 10^6-step harness covers ~200K operations in each of the five categories and exits clean in 595 seconds on the dev workstation (~1680 operations per second). The CTest TIMEOUT is 1200 seconds — 2x observed — to give slower CI runners headroom without masking a future per-step-cost regression.
- The "living instrument" directive is literal at the public-API level: every parameter modulatable at any granularity. The M4 named-lever layer + CV inputs can sit directly atop `spu94_set_reg_*` without Phase 5 rework.
- The fuzz harness is the single regression insurance that catches any future change to the write-timing machinery, the FIR chain, the preset loader, or the block-loop structure that violates any of the six invariants — all in one test target.

**Alternatives Considered:**

- **Property-based testing via `hypothesis` instead of the Phase 2/3/4 random-walk style.** Rejected: consistency with prior-phase fuzz harnesses is more valuable than fancier shrinking. The random-walk harness's reproducibility via the golden seed is enough for regression diagnosis.
- **Tighter invariant on Off preset: assert every post-load sample is exactly zero.** Rejected: the TICK_LATCHED commit window means the process call immediately after `spu94_load_preset(OFF)` may observe old d-prefix / m-prefix delays for one tick before they commit. The non-zero-output tolerance on the Off side would need at least one tick of patience. Simpler to skip the Off-silence check and let the non-Off-non-silence invariant carry the signal.
- **Run the harness at 10^5 steps instead of 10^6.** Rejected: 10^6 is the Phase 2/3/4 precedent; the extra 9x exposure is cheap insurance given each step is cheap.

**Seam:**

- If future work adds a new public-API entry point (e.g., a byte-level mid-stream seek), extend the five-op set in `fuzz_process.py` and add the corresponding invariant. No machinery change; it is an append-only op list.
- If Phase 6's ctypes.Structure auto-derives the struct offsets, the hand-synced `PENDING_MASK_OFFSET` / `FIR_IDX_*_OFFSET` constants + the startup guard block in `fuzz_process.py` delete cleanly. The invariants themselves stay.

**Revision Path:**

- A future ADR may tighten the output-bound invariant from "within int16" to "within a preset-specific amplitude envelope" once per-preset envelope characterization is done (M5 hardware validation).
- A future ADR may widen the op set to include `spu94_reset` injections mid-stream once a reset-timing semantics ADR resolves what happens to in-flight TICK_LATCHED writes.

**Sources:**

- `tests/python/fuzz_process.py` (this plan, Task 1).
- `tests/unit/process/test_process_block_size.c` + `tests/unit/process/test_process_in_place.c` (this plan, Task 2).
- `tests/unit/preset/test_preset_nonzero_tail.c` (Phase 5 Plan 03 — SC-2 behavioral proof).
- 05-RESEARCH § "Fuzz Harness Integration Notes (D-10a)" + § "Test Measure Additions".
- 05-CONTEXT.md Area F.
- ADR-0005 (the underlying split write-timing machinery).

---

## ADR-Phase-5-E: RT-safety audit methodology — per-axis CI gates + pinned latency threshold

**Status:** Accepted (2026-04-20, Phase 5)

**Resolves:** D-09a (no-heap linker-symbol check), D-09b (no-locks linker-symbol check), D-09c (no-syscalls strace-based steady-state test), D-09d (no-variable-latency ctypes timing benchmark), D-09e (four targets not one monolithic audit).

**Relates:** API-08; Phase 1 `scripts/ci/verify-no-heap-symbols.sh` (existing precedent); ADR-Phase-5-A (public API whose RT-safety contract this methodology verifies).

**Context:**

API-08 requires four distinct real-time-safety guarantees on `spu94_process` and the rest of the hot-path surface: no heap allocations, no locks, no syscalls, no variable-latency operations. ROADMAP Phase 5 SC-4 requires these verified across 100000 consecutive process blocks. Phase 1 already ships a linker-symbol no-heap audit (`scripts/ci/verify-no-heap-symbols.sh`); Phases 2–4 left the other three axes for Phase 5 to land. The methodology question is: one monolithic audit or four independent targets?

Four axes have four independent failure modes: a pthread-linkage regression, a syscall regression, and a cache-dependent-branch regression each want to fail at a specific ctest target so the diagnostic is unambiguous. A monolithic audit that fails with "RT-safety broke, check the log" is strictly worse.

The no-variable-latency axis (D-09d) also requires a measure-then-pin calibration: the `(p99 − median) / median` ratio bound must be wide enough to tolerate real OS noise on CI runners without flaking, tight enough to catch a cache-dependent branch or an accidental syscall. The plan's first-pass target was 3.0; the host measurement informs the pinned value.

**Decision:**

- Four independent ctest gates landed under `tests/rt_safety/`:
  - `rt_no_heap` (D-09a): `nm -u` plus `readelf -d` on both `libspu94.so` and `tests/rt_safety/test_phase5_linksym` (a static-linked harness that references `spu94_process`, `spu94_flush`, `spu94_load_preset` so the `nm -u` audit sees every Phase 5 code path's link closure). The forbidden-symbol list widens from Phase 1's `{malloc, calloc, realloc, free}` to also cover `aligned_alloc` and `posix_memalign`.
  - `rt_no_locks` (D-09b): `nm -u` on the same two binaries, pattern `pthread_mutex_*` / `pthread_rwlock_*` / `pthread_cond_*` / `pthread_spin_*` / `pthread_barrier_*` / `sem_*` / `futex`.
  - `rt_no_syscalls` (D-09c): C harness runs `spu94_init` → `spu94_load_preset(HALL)` → `raise(SIGUSR1)` marker → 100000 iterations of `spu94_process(state, L, R, Lout, Rout, 1024)` → `raise(SIGUSR1)` marker → `spu94_flush`. A shell wrapper runs the binary under `strace -f -ttt -o log`, locates the two `--- SIGUSR1 ---` lines, windows the log between them, subtracts a scaffolding-syscall filter (`rt_sigreturn`, `gettid`, `getpid`, `tgkill` — the four syscalls glibc's `raise()` expands into), and asserts zero remaining syscalls. Linux-only; skips gracefully if `strace` is unavailable.
  - `rt_bench_latency` (D-09d): Python ctypes benchmark. 1000-call warmup then 100000-call measurement of `spu94_process(state, L, R, Lout, Rout, 1024)` with `time.perf_counter_ns`. Asserts `(p99 − median) / median ≤ RT_LATENCY_THRESHOLD`.
- **Measured dev-host ratio**: **0.741** (median = 536389 ns per 1024-sample block, p99 = 933797 ns, max = 1350529 ns; Hall preset loaded; Ryzen-class Linux workstation; measurement window 100000 calls after 1000-call warmup).
- **Pinned `RT_LATENCY_THRESHOLD`**: **2.0** per the measure-then-pin protocol `max(2.0, 2 × observed)` = `max(2.0, 1.482)` = `2.0`. The default is wired into `tests/rt_safety/CMakeLists.txt` as the `RT_LATENCY_THRESHOLD` CMake cache variable; callers override via `-DRT_LATENCY_THRESHOLD=<value>` at configure time for host-specific recalibration.

**Consequences:**

- Per-axis diagnosis preserved: a heap-linkage regression fails `rt_no_heap` red; a syscall regression fails `rt_no_syscalls` red; a cache-dependent-branch regression fails `rt_bench_latency` red. The D-09e "four targets, one per property" discipline holds.
- The 2.0 threshold is 2.7x the observed 0.741 variance — tight enough to catch a regression that doubles per-block variance; loose enough that a noisy CI runner with moderate scheduling jitter still passes. A future ADR may tighten toward 1.5x if CI-runner noise proves smaller than feared.
- The `test_phase5_linksym` static-linked binary is the key link-closure audit hook: dynamic linking would only surface `libspu94.so`'s exported symbols; static linking pulls every reachable-from-public-symbol helper into one binary that `nm -u` can audit end-to-end.
- RT-safety ctest runtime is ~108 seconds (the `rt_no_syscalls` strace + 10^5 iterations dominate at ~54 s; `rt_bench_latency` adds another ~54 s). `ctest -E rt_safety` returns fast-path < 15 s for iterative dev.

**Alternatives Considered:**

- **Single monolithic audit target.** Rejected: per-axis diagnosis is strictly more useful; four cheap ctest entries are not a cost.
- **Use `cyclictest` for the latency axis.** Rejected: `cyclictest` measures the kernel, not the library; meaningful numbers require an RT kernel; commodity CI runners would produce noise. The `(p99 − median) / median` ratio on `spu94_process` directly probes the library's own behavior.
- **Pin `RT_LATENCY_THRESHOLD = 3.0`** (keep the first-pass target). Rejected: 3.0 leaves too much headroom; a regression doubling per-block cost would still fit. 2.0 is tight but still 2.7x observed.
- **Pin `RT_LATENCY_THRESHOLD = 1.5`.** Rejected for first-pass: 1.5x is 2x observed; plausibly too tight for CI-runner scheduling jitter without empirical CI-host evidence. Future tightening is an append-only ADR.
- **Drop the syscall axis on non-Linux hosts.** Accepted: graceful ctest skip when `strace` is unavailable. Linux is the primary dev + CI target; macOS/BSD would use `dtrace`-style tooling which is a separate port not justified in M1.

**Seam:**

- The `RT_LATENCY_THRESHOLD` CMake cache variable is the documented tightening path. An M5 hardware-capture pass on Cortex-M7 or Daisy silicon revises the pinned default based on embedded-target measurements; the seam supports that with zero code change.
- The scaffolding-syscall filter list in `tests/rt_safety/test_no_syscalls.sh` is an append-only extension point: if a future glibc version changes the `raise()` expansion or a new strace version renames a scaffolding syscall, the filter widens without touching the C harness.

**Revision Path:**

- If CI-host evidence shows the 2.0 threshold flakes under routine load, widen to 2.5 with an explicit ADR + a commit message justification.
- If embedded-target measurement (M5) shows Cortex-M7 per-block ratios materially different from desktop, fork the threshold into desktop and MCU variants via a new ADR.

**Sources:**

- `tests/rt_safety/` (full suite — 4 ctest targets per D-09a-e; created Phase 5 Plan 04).
- `tests/rt_safety/bench_latency.py` (D-09d measurement tool; prints the ratio + threshold + pass/fail on every run).
- 05-04-SUMMARY.md (Plan 04 ran bench_latency and recorded the measured 0.741 ratio).
- 05-RESEARCH § "RT-Safety Audit Methodology (D-09a-e)".
- 05-CONTEXT.md Area E.

---

## ADR-Phase-5-D: Preset-load atomicity honors the D-04 split write policy

**Status:** Accepted (2026-04-20, Phase 5)

**Resolves:** D-08 (05-CONTEXT.md Area D); API-05 (bulk preset-load function).

**Relates:** ADR-0005 (per-register split mid-stream write-timing policy); ADR-0006 (mBASE snap-on-write); ADR-Phase-5-C (preset representation + three-source sourcing).

**Context:**

`spu94_load_preset` has to write 35 register values atomically-enough that the caller does not see partial configuration. The question is whether to bypass the Phase 2 ADR-0005 split write policy — writing every register directly into the active `reg_values` slot in one pass — or to honor it by routing every write through the engine-layer setters and letting the split policy handle IMMEDIATE (12 v-prefix plus mBASE) versus TICK_LATCHED (22 d-prefix and m-prefix) registers per the existing machinery.

Bypassing the policy would produce "whole-preset commit at call return": all 35 values active immediately. Honoring it produces a single-tick "half-applied window" — new gains live immediately; new delays live after the next `spu94_tick` — approximately 45 us at 22.05 kHz. That window is inaudible and consistent with the mid-stream-write semantics the project is already committed to.

**Decision:**

- `spu94_load_preset(state, id)` iterates all `SPU94_REG__COUNT` = 35 register indices, dispatches to `spu94_set_reg_i16` or `spu94_set_reg_u16` via `spu94_reg_type`, and passes the preset table's `int16_t` value unchanged (with a `(uint16_t)` reinterpret for the 23 U16-family registers whose preset storage is a bit-pattern).
- The D-04 split write policy applies automatically: the 12 v-prefix registers plus mBASE (IMMEDIATE) become active immediately; the 22 d-prefix and m-prefix registers (TICK_LATCHED) stage into `pending_values[]` with the corresponding bit set in `pending_mask`, and commit at the next `spu94_tick`.
- The ~45-microsecond half-applied window (new v-prefix gains, old d-prefix and m-prefix delays) is accepted as inaudible and consistent with the mid-stream-write model.
- NULL state returns `SPU94_OK` (lifecycle-null-safe convention matching Phase 2's `spu94_tick`, `spu94_reset`, `spu94_destroy` precedent). Out-of-range id returns `SPU94_UNKNOWN_REG` with no register mutation (T-5-3 threat mitigation).

**Consequences:**

- One unified write path — no preset-specific bypass; no D-04 divergence. Every preset load is a sequence of engine-layer setter calls; the policy table decides timing per register.
- The D-11 swappable write-policy-table seam (Phase 2 ADR-0005) remains available. Future M4 Controllers that need atomic whole-preset commit (for example, to match a plugin framework's "recall program" semantics with zero half-applied window) can re-point the table without touching `spu94_load_preset`.
- Tests: `tests/unit/preset/test_preset_load_all.c` (Phase 5 Plan 03) pins six sub-tests covering D-08 split semantics per preset across all 10 presets × 35 registers. Plus `test_preset_nonzero_tail.c` validates SC-2 behavioral contract (non-Off preset produces non-silent tail; Off preset produces silent output).

**Alternatives Considered:**

- **Atomic whole-preset commit via a new Phase 5 bypass path.** Rejected: adds a new code path, a new surface for bugs, a new policy to document, and delivers zero audible benefit. The 45-microsecond window is below perception.
- **Preset-specific pending-to-active flush between v-prefix and d-prefix sections.** Rejected: same objection; net effect identical to single-tick-wait; added state machine complexity for nothing.
- **Load preset into a scratch struct then memcpy.** Rejected: violates Phase 2's engine-layer discipline; would require duplicating the 35-entry type-classifier + bit-pattern-preservation logic.

**Seam:**

- The D-11 swappable write-policy-table remains. M4 Controllers that need atomic commit can re-point the table via a static-linkage override of `spu94_write_policy_table`, matching the D-22 extensibility-seam discipline.

**Revision Path:**

- If M4 plugin authors report that the 45-microsecond half-applied window produces audible artifacts during rapid preset switching (unlikely — the window is 1 / 22050 s which is well below typical human-hearing temporal resolution), a new ADR adds a `spu94_load_preset_atomic` entry that bypasses the policy table via the D-11 seam.

**Sources:**

- `src/spu94/spu94_presets.c` (`spu94_load_preset` body; Phase 5 Plan 03 Task 1).
- `tests/unit/preset/test_preset_load_all.c` (Phase 5 Plan 03 Task 2 — split-policy verification).
- `tests/unit/preset/test_preset_nonzero_tail.c` (Phase 5 Plan 03 Task 3 — SC-2 behavioral proof).
- 05-RESEARCH § Architecture Patterns "Preset iteration via engine-layer setters".
- 05-CONTEXT.md Area D.
- ADR-0005 (the underlying split write-timing policy machinery).

---

## ADR-Phase-5-C: Preset representation + three-source value sourcing

**Status:** Accepted (2026-04-20, Phase 5)

**Resolves:** D-06 (preset table shape, .rodata placement, enum indexing); D-07 (three-source cross-reference for preset values); D-07a (per-preset arbitration policy if consensus cannot be reached).

**Relates:** CORE-09 (10 factory presets); BIB-011 (nocash SPU Reverb Examples); BIB-012 (hitmen c02 SPU documentation); BIB-013 (Sony Psy-Q LIBSND documentation); ADR-0020 (Phase 4 coefficient-sourcing three-source pattern precedent).

**Context:**

Phase 5 ships the 10 PS1 factory reverb presets (Off, Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay) as a 350-integer table (35 registers × 10 presets). Two questions: what shape does the table take in the codebase, and how are the integer values sourced defensibly?

On shape: flat `int16_t regs[SPU94_REG__COUNT]` indexed by the canonical register enum versus a typed-per-register struct with 35 named fields. Flat is data-centric (trivially iterable by Phase 6 Python bindings and M4 Controllers); typed-per-register is more readable at the definition site but adds 35 field-name maintenance burden for minimal gain.

On sourcing: PROJECT.md's licensing posture puts Mednafen, lv2-psx-reverb, DuckStation, and MiSTer source code OFF-LIMITS as primary references. The candidate primary sources are nocash psx-spx, hitmen c02, and the Sony Psy-Q SDK. A three-source cross-reference audit — mirroring Phase 4's ADR-0020 coefficient-sourcing discipline — resolves how much independence the three sources actually provide.

**Decision:**

- **Table shape (D-06):** one `const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT]` table in `.rodata`. Each row carries `const char *name` plus a 35-element `int16_t regs[SPU94_REG__COUNT]` indexed by the canonical register enum. The typedef lives in `include/spu94/spu94.h`; the table definition lives in `src/spu94/spu94_presets.c`; enum indexing (`SPU94_PRESET_OFF = 0` through `SPU94_PRESET_DELAY = 9`, with `SPU94_PRESET__COUNT = 10` as the iteration sentinel) matches the Sony LIBSND ordering per BIB-013.
- **Value sourcing (D-07):** values transcribed facts-only from BIB-011 (nocash psx-spx — primary modern publication, human-audited into `.planning/research/05-preset-values-audit-nocash.csv`) and cross-verified byte-for-byte against BIB-012 (hitmen c02 — archived 1990s demoscene transcription, `.planning/research/05-preset-values-audit-hitmen.csv`). Verification via `tests/python/verify_preset_sources.py` — a permanent ctest regression gate.
- **BIB-013 (Sony Psy-Q LIBSND documentation)** corroborates the preset-ID ordering (`SPU_REV_MODE_OFF = 0` through `SPU_REV_MODE_DELAY = 9`) and the preset-name mapping, but DOES NOT publish the per-register values — those live inside Sony's `libsnd.lib` binary which is off-limits under the project's licensing posture. This is disclosed honestly here: "three sources" means three independent documentation lineages (nocash modern, hitmen demoscene, Sony SDK), NOT three independent hardware readouts.
- **Disagreement resolution (D-07a)**: the audit found 16 cell disagreements — all on the Off preset, all at m-prefix buffer-address registers. BIB-011 publishes `0x0001` (defensive minimum halfword offset); BIB-012 publishes `0x0000`. Resolved in favor of BIB-011 per the priority chain BIB-013 > BIB-011 > BIB-012 (BIB-013 silent on these values, so BIB-011 wins against BIB-012). Rationale documented in `.planning/research/05-preset-values-audit-resolutions.md`. The Off preset ships with the BIB-011 values; `test_preset_table_integrity` pins the specific 16 nonzero cells as a cell-specific regression gate.

**Consequences:**

- The data-centric flat-table shape lets Phase 6 ctypes bindings iterate presets trivially, M4 Controllers dump preset values for A/B comparison, and Phase 7 golden-file tests drive each preset through `spu94_process` with standard inputs and snapshot outputs.
- The honest three-source disclosure matches Phase 4's ADR-0020 precedent: "three sources" documents three distinct lineages, not three independent hardware readouts. Byte-for-byte agreement across BIB-011 and BIB-012 is a transcription-fidelity check (does the audited-into-CSV integer match between two independent 1990s-and-2020s transcriptions?), not an independent-hardware-measurement check. A future M5 hardware capture supersedes this ADR's values for any preset where silicon disagrees.
- The 16 Off-preset disagreements are cell-specific — the audit-driven test (`test_preset_table_integrity` → `test_off_matches_audit`) pins the resolved nonzero cells rather than asserting blanket all-zero, catching any future regression that quietly changes the Off values.

**Alternatives Considered:**

- **Typed-per-register preset struct.** Rejected: 35-field-name maintenance burden; worse for programmatic iteration by Phase 6 Python.
- **Claim three independent sources where one is a mirror.** Rejected: ADR-0020 precedent establishes honest-lineage disclosure as project discipline. A future audit would catch the claim anyway; better to disclose now.
- **Resolve disagreements via "average" or "union" of sources.** Rejected: averaging integer register values is nonsensical; union would require shipping both variants behind a flag. The priority chain is cheap and defensible.
- **Drop the Off preset from the product** if its values cannot be agreed across sources. Rejected: D-07a's "ship with documented rationale + flag for M5 arbitration" policy handles this; losing one of 10 presets is a bigger audible loss than a documented defensive-value choice.

**Seam:**

- M5 hardware validation on original PSX silicon may revise specific register cells if the silicon disagrees with the BIB-011 / BIB-012 audit-resolved values. The supersede-with-new-ADR protocol updates the table in `src/spu94/spu94_presets.c` and revises the corresponding audit CSV.
- The `verify_preset_sources.py` gate is resolutions-aware: adding a new cell to `05-preset-values-audit-resolutions.md` acknowledges a new cell disagreement without failing CI. Undocumented disagreements continue to fail the gate.

**Revision Path:**

- M5 hardware capture on ≥ 2 independent PSX consoles (SCPH-5501, SCPH-7502, SCPH-9001, PAL revisions) validates or revises the per-preset register matrix. Any revision drives a superseding ADR with the new values.
- If a fourth documentation source emerges (e.g., a declassified Sony internal document or a new hardware-readout post), the bibliography extends and the audit runs on the three-way comparison.

**Sources:**

- `include/spu94/spu94.h` (`spu94_preset_id_t` enum + `spu94_preset_t` typedef + `spu94_presets[]` extern).
- `src/spu94/spu94_presets.c` (the `.rodata` preset table).
- `.planning/research/05-preset-values-audit-nocash.csv` (BIB-011 transcription).
- `.planning/research/05-preset-values-audit-hitmen.csv` (BIB-012 transcription).
- `.planning/research/05-preset-values-audit-resolutions.md` (16-cell disagreement resolution).
- `tests/python/verify_preset_sources.py` (resolutions-aware cell-equality gate).
- `tests/unit/preset/test_preset_table_integrity.c` (includes `test_off_matches_audit`).
- docs/BIBLIOGRAPHY.md BIB-011 / BIB-012 / BIB-013 entries.
- 05-RESEARCH § "Three-Source Preset-Value Cross-Reference (PRIMARY D-07 OUTPUT)".
- 05-CONTEXT.md Area C.

---

## ADR-Phase-5-B: Mix-bus mailbox — two int16 fields on spu94_state

**Status:** Accepted (2026-04-20, Phase 5)

**Resolves:** D-05 (05-CONTEXT.md Area B).

**Relates:** ADR-0005 (single-call-site discipline); Phase 3 reverb body (unchanged internal shape); ADR-Phase-5-A (public block-based entry point that writes the mailbox); ADR-Phase-5-F (fuzz harness that exercises the mailbox end-to-end).

**Context:**

`spu94_process` receives 44.1 kHz stereo samples block-at-a-time; `spu94_reverb_body` runs inside `spu94_tick` at 22.05 kHz and needs the "current tick's mix-bus input" to drive the reverb network. Phase 3 left that site hardcoded to `const int16_t left_in = 0, right_in = 0` at `src/spu94/spu94_reverb.c` line 580 because the reverb-body tests drove silent input and Phase 3 had no upstream public-API caller.

Phase 5 has to wire the public block loop's input samples into that site without disturbing Phase 3's body-level tests. The design options: thread `left_in` / `right_in` through every tick-call signature (large blast radius; touches `spu94_tick`, `spu94_fir_chain_step`, `spu94_reverb_body`), add a new "input scale" stage (large code surface; new err-tap), or use a mailbox-on-state mailslot (minimal change; default zero preserves every Phase 3 test that never writes the mailbox).

**Decision:**

- Two new fields on `struct spu94_state`: `int16_t mix_bus_l;` and `int16_t mix_bus_r;`. Grouped immediately before the Phase 4 FIR block so all I/O-boundary state is adjacent and audit-friendly. +4 bytes to `sizeof(struct spu94_state)` (now 544 bytes — well below `SPU94_STATE_SIZE_MAX` = 16384).
- `spu94_process` writes `state->mix_bus_l = L_in[i]` and `state->mix_bus_r = R_in[i]` (or zero if the caller passed NULL for that channel) before each call to `spu94_fir_chain_step`. Single mailbox write per 44.1 kHz sample; zero allocation, zero synchronization, zero pass-through through additional call signatures.
- `spu94_reverb_body` reads the mailbox where it previously hardcoded zero — `src/spu94/spu94_reverb.c:580`: `const int16_t left_in = state->mix_bus_l; const int16_t right_in = state->mix_bus_r;`. Default-zero on `spu94_init` / `spu94_reset` via the wholesale byte-loop zero-fill from Phase 2 Plan 01.
- **Zero blast radius on Phase 3 body-level tests:** those tests never write the mailbox, so the default-zero state produces the same `left_in = right_in = 0` behavior Phase 3 originally assumed. No Phase 3 test needs editing.

**Consequences:**

- The mailbox is an internal state surface (D-23): no public accessors, no setter. Callers write it indirectly by passing L_in / R_in to `spu94_process`. M4 Controllers could in principle write the mailbox for cross-feed tricks (deferred idea in 05-CONTEXT); Phase 5 does not expose that.
- 4 bytes of additional state cost. Still 15840 bytes headroom below `SPU94_STATE_SIZE_MAX`.
- Unit test `tests/unit/process/test_process_mix_bus.c` proves the mailbox field behavior in isolation: init-zero, reset-clears, tick-observes-mailbox-write (using `state->overflow_magnitude` as the mailbox-read-proof observable — the `err_input_scale` field is zero by construction because `spu94_reverb_input_scale` does plain `int16 * int16 -> int32` with no Q15 truncation, so `overflow_magnitude` on the hard-clip stage is the robust proof point).

**Alternatives Considered:**

- **Thread `int16_t left_in, right_in` through `spu94_tick` + `spu94_fir_chain_step` + `spu94_reverb_body`.** Rejected: large blast radius; every Phase 3 test + every Phase 4 test + every future internal-helper test would need the new parameters. The mailbox is the minimum-change answer.
- **Promote the mailbox to `int32_t`** (matching Phase 3's hard-clip stage input width). Rejected: Phase 3's `left_in` / `right_in` at reverb.c:580 are `int16_t`; widening would require rewiring the input-scale stage. The minimum-change answer is `int16_t`; input scaling stays in its existing Phase 3 stage.
- **Add a separate "input scale" stage public accessor** so the caller can pre-scale or attenuate mix-bus input. Rejected: out of Phase 5 scope; "Mix Gain" is an M4 named-lever concern, not a Phase 5 API shape concern.

**Seam:**

- The mailbox fields are an extensibility point. M4 Controllers that want pre-reverb input scaling can write the mailbox directly between `spu94_process` blocks (requires either a test-visible setter or direct struct access via the internal header — Phase 5 ships neither; M4 decides).
- If a future Phase needs 32-bit mix-bus precision (e.g., a dither or noise-shaping stage that runs pre-reverb), the field can widen with an ADR documenting the interaction with Phase 3's stage boundary.

**Revision Path:**

- M4's named-lever layer may expose a pre-reverb input-gain parameter that modulates the mailbox values; that would be an M4 ADR, not a Phase 5 revision.
- If the mailbox ever becomes bidirectional (e.g., a tap that lets M4 read the current-tick input), that reverses its D-23 observability posture and requires a new ADR.

**Sources:**

- `src/spu94/spu94_state_internal.h` (field definitions; lines 75-77 adjacent to the FIR block).
- `src/spu94/spu94_reverb.c:580` (the read-site; Phase 3 placeholder replaced).
- `src/spu94/spu94_process.c` (the write-site; one mailbox write per 44.1 kHz sample in the block loop).
- `tests/unit/process/test_process_mix_bus.c` (mailbox-field behavior in isolation).
- 05-RESEARCH § Architecture Patterns "Block-loop with mailbox writes".
- 05-CONTEXT.md Area B.

---

## ADR-Phase-5-A: Public block-based entry point shape — spu94_process + spu94_flush

**Status:** Accepted (2026-04-20, Phase 5)

**Resolves:** D-01 (planar stereo int16 pointers); D-02 (named `spu94_flush` drain function); D-03 (any block size N >= 1); D-04 (in-place processing allowed).

**Relates:** API-03; API-06; ADR-Phase-5-B (mix-bus mailbox written from the block loop); ADR-Phase-5-F (fuzz harness at scale).

**Context:**

The Phase 1–4 algorithm is bit-faithful inside `spu94_tick` and `spu94_fir_chain_step`. Phase 5 has to wrap that algorithm in a 2026-caller-friendly public entry point that serves DAWs, CLIs, Python bindings, and future JUCE / MCU shells. The API shape decisions are ergonomic (what shape serves modern callers best?), not authenticity-bearing (PS1 silicon had no caller concept). Four questions: stereo layout, tail-drain treatment, block-size flexibility, in-place aliasing.

Stereo options: planar (four separate pointers — `L_in`, `R_in`, `L_out`, `R_out`) versus interleaved (one pointer with `L, R, L, R, ...`). Planar matches JUCE / VST3 / AU / LV2 framework conventions at M4. Interleaved matches WAV file layout but forces M4 wrappers to deinterleave.

Tail-drain options: expose a separate `spu94_flush` function versus document "call `spu94_process` with silent input to drain" in a comment. Named API surface is discoverable; comment-only is folklore.

Block-size options: require even N (matches 22.05 kHz clock alternation internally), require power-of-2, or allow any N >= 1. The internal phase-tracker in Phase 4's `spu94_fir_chain_step` handles odd blocks across calls, so any N >= 1 is achievable.

In-place options: allow or forbid. The sample-at-a-time loop is alias-safe by construction (input consumed before output written); forbidding in-place would just be an arbitrary restriction.

**Decision:**

- **D-01:** `void spu94_process(spu94_state *state, const int16_t *L_in, const int16_t *R_in, int16_t *L_out, int16_t *R_out, uint32_t num_samples);` — four planar pointers; matches JUCE / VST3 / AU / LV2 conventions for M4 wrappers.
- **D-02:** `void spu94_flush(spu94_state *state, int16_t *L_out, int16_t *R_out, uint32_t num_samples);` — a named tail-drain entry point. Implementation delegates to `spu94_process(state, NULL, NULL, L_out, R_out, num_samples)`; NULL-L_in / R_in substitutes zeros for that channel's input. Single-body discipline (Pitfall 4 / ADR-0005): same sample-at-a-time math path as `spu94_process`.
- **D-03:** Any `num_samples` >= 1 is legal. `num_samples == 0` is a safe no-op (output buffers untouched). `spu94_fir_chain_step`'s internal `fir_interpolate_phase` state preserves the 22.05 kHz clock alternation across calls, so a sequence of 1-sample calls produces the same output as a single N-sample call. Proven by `tests/unit/process/test_process_block_size.c` across the block-size sweep `{1, 2, 3, 4, 7, 16, 64, 128, 441, 1024, 4096}`.
- **D-04:** In-place processing allowed. `L_out == L_in` and `R_out == R_in` are both legal aliases. The block loop's per-sample pattern `l = L_in[i]; ...; L_out[i] = lo` consumes each input sample before writing its output slot, so aliased buffers produce identical output to distinct buffers. Proven by `tests/unit/process/test_process_in_place.c`.
- NULL state is a no-op. NULL L_in or R_in substitutes zero for that channel. NULL L_out or R_out suppresses writes for that channel. The lifecycle-null-safe convention matches the other Phase 2–4 public entries.

**Consequences:**

- DAW hosts with planar internal buffers consume `spu94_process` directly; WAV callers (Phase 6 CLI) deinterleave in one adapter line (`for i in 0..N: L[i] = wav[2i]; R[i] = wav[2i+1]`).
- `spu94_flush` makes offline-render tail capture a discoverable API surface rather than comment-only folklore. CLI's `spu94 in.wav out.wav` invocation appends a tail-drain pass after the input WAV ends.
- The block-size-invariance contract lets callers pick their preferred block size (JUCE: whatever the host provides; CLI: whatever fits in L2 cache — 1024 is typical; embedded: whatever fits in SRAM — 64 or smaller). The fuzz harness exercises random block sizes in [1, 4096]; the unit-test sweep proves exact bit-identity across the specific plan-pinned sizes.
- In-place aliasing is essential for DAW hosts with single-buffer processing; forbidding it would have made SPU-94 a weird outlier in the plugin ecosystem.
- Zero new math introduced in Phase 5 — the public entry points are glue over the Phase 4 `spu94_fir_chain_step` building block, preserving the Phase 1–4 bit-faithfulness claim unchanged.

**Alternatives Considered:**

- **Interleaved buffer instead of planar.** Rejected: JUCE / VST3 / AU / LV2 are the M4 target framework set; three of four are planar-native. One adapter line at the CLI is cheaper than per-plugin deinterleave.
- **Require even `num_samples`.** Rejected: Phase 4 research already confirmed the internal phase tracker handles odd blocks; imposing the restriction would create a caller footgun for zero algorithmic benefit.
- **Document "flush via `spu94_process` with silent input" instead of a named entry.** Rejected: named API surface is the clearer shape; duplicate-body risk is zero because `spu94_flush` delegates to `spu94_process` internally.
- **Forbid in-place aliasing.** Rejected: arbitrary restriction; sample-at-a-time loop is alias-safe by construction; DAW single-buffer processing needs this.
- **`size_t num_samples` instead of `uint32_t`.** Rejected: `uint32_t` is portable and unambiguous on MCU; matches `SPU94_LATENCY_SAMPLES` convention; 4 billion samples is ~27 hours at 44.1 kHz — well above any realistic block size.

**Seam:**

- If a future phase needs a different-rate public entry (e.g., 48 kHz native instead of 44.1 kHz round-trip via the FIR boundaries), a new `spu94_process_48k` entry lives adjacent to `spu94_process` in the public header. The Phase 4 FIR chain is the rate-conversion stage; bypassing it requires a separate public path, not a modification of `spu94_process`.
- If a future phase needs variable-precision output (e.g., int32 or float32), new entries land alongside the int16 pair — ABI-stable extension pattern matching Phase 2 D-07's append-only result-code convention.

**Revision Path:**

- If an M4 plugin framework surfaces that requires a different block-API shape (e.g., single callback + pull-model rather than push-model block calls), a new adapter wraps `spu94_process` rather than revising it. The sample-at-a-time building block (`spu94_fir_chain_step`) is the stable primitive beneath any future adapter.

**Sources:**

- `include/spu94/spu94.h` (`spu94_process`, `spu94_flush`, `spu94_load_preset` prototypes).
- `src/spu94/spu94_process.c` (block-loop implementation shared by both entry points).
- `src/spu94/spu94_io_chain.c` (Phase 4 `spu94_fir_chain_step` — the per-sample building block).
- `tests/unit/process/test_process_basic.c` (block-loop correctness; Phase 5 Plan 02).
- `tests/unit/process/test_process_block_size.c` (D-03 block-size invariance sweep; Phase 5 Plan 05 Task 2).
- `tests/unit/process/test_process_in_place.c` (D-04 in-place bit-identity; Phase 5 Plan 05 Task 2).
- `tests/python/fuzz_process.py` (10^6-step random-walk fuzz; Phase 5 Plan 05 Task 1).
- 05-RESEARCH § Architecture Patterns "Public block-loop wraps internal per-sample step".
- 05-CONTEXT.md Area A.

---

## ADR-0020: Coefficient provenance — three-source audit + bibliography reconciliation

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** D-10, D-11, D-12 (04-CONTEXT.md); 04-RESEARCH § Coefficient provenance audit.

**Relates:** BIB-005 (psx-spx reverb coefficient page), BIB-006 (forums.bannister.org SCPH-5501 hardware readout), BIB-007 (jsgroth PS1 SPU Part 3 structural corroboration), BIB-008 (lv2-psx-reverb), ADR-0012 (half-rate + lv2-psx-reverb exclusion).

**Context:**

The 39-tap half-band FIR coefficient table is the one piece of Phase 4 data that cannot be derived from the spec prose; it must be transcribed from a real measurement. 04-CONTEXT.md D-10 called for a "three-source byte-for-byte cross-reference" and D-11/D-12 directed the source TU to carry only the integer values (no inline citation prose) with provenance routed through `docs/BIBLIOGRAPHY.md`. A pass-2 audit during 04-RESEARCH traced the actual provenance chain and found the landscape is narrower than D-10's framing implied: every published 39-tap coefficient table — whether on `psx-spx.consoledev.net`, in jsgroth's PS1 SPU series, or anywhere else the values are quoted — ultimately descends from one hardware reading posted to `forums.bannister.org` for a SCPH-5501. jsgroth's PS1 SPU Part 3 corroborates the STRUCTURE (39 taps, half-band, both I/O boundaries, independent L/R delay lines) but does not republish the values.

CONTEXT's D-10 bullet "NOCASH IS NOT A VALID SOURCE" was a separate, partially-incorrect framing: `psx-spx.consoledev.net` (the modern mirror of `nocash/psx-spx`) DOES publish the coefficient values — by way of transcribing the bannister readout — which is exactly the kind of community-mirrored primary reference the project needs.

**Decision:**

- The 39 coefficient values are transcribed independently into BOTH `src/spu94/spu94_fir_coef.c` (the canonical C table consumed by the production FIR) AND `tests/python/derive_fir_reference.py` (the Python oracle consumed by the Plan 01/02/03/04 bit-identity audits). These are two independent human transcriptions; the Plan 02 bit-identity test + Plan 04 fuzz_fir (loaded via ctypes) confirm byte-level agreement at build time.
- BIB-006 (forums.bannister.org SCPH-5501 readout) is cited as the **primary** source — one hardware reading, published openly, with the poster's methodology visible.
- BIB-005 (psx-spx) is cited as a **corroborating mirror**: it carries the values verbatim from the bannister readout. The "NOCASH IS NOT A VALID SOURCE" framing is superseded by: *"prefer community citations with explicit attribution to the bannister SCPH-5501 readout; psx-spx's render carries the values verbatim and is cited as a corroborating mirror."*
- BIB-007 (jsgroth's PS1 SPU Part 3) is cited as a **structural corroborator** — it confirms the 39-tap / half-band / dual-boundary / independent-L-R-delay-lines architecture without republishing the numeric values, which is the strongest kind of independent check (a second author, same measurement conclusion).
- Cross-console confirmation (SCPH-7502 / SCPH-9001 / PAL revisions) is NOT attempted in Phase 4. M5 (hardware validation milestone) captures additional readouts; if they disagree, the disagreement drives a follow-up ADR.

**Consequences:**

- The "three sources" of the bibliography (BIB-005 + BIB-006 + BIB-007) do NOT equal three independent hardware readings — they equal one hardware reading + two published mirrors (one byte-level, one structural). Disagreement across them is impossible by construction. The audit trail is legible on its face in `docs/BIBLIOGRAPHY.md`.
- Defense against a single-source coefficient-tampering attack is provided by the dual-independent-transcription (C + Python) + Plan 01's `test_fir_coef_table.c` (sum + L1 + symmetry + center-tap + half-band zero invariants) + Plan 02's bit-identity test + Plan 04's fuzz_fir runtime cross-check. Four independent check surfaces; any single-source attack fails.
- The bibliography's licensing tags (lv2-psx-reverb GPLv3, Mednafen GPLv2, DuckStation CC-BY-NC-ND) are not "three sources" either — they are witness emulators, usable as OUTPUT witnesses only (Phase 7 TEST-03), per PROJECT.md's source-not-read posture.

**Alternatives Considered:**

- **Count BIB-005 and BIB-006 as two independent sources.** Rejected: misleading, since BIB-005 transcribes BIB-006.
- **Require a second independent hardware readout as a Phase 4 gate.** Rejected: out of scope for M1. Deferred to M5.
- **Fold jsgroth's structural corroboration into BIB-005.** Rejected: jsgroth and psx-spx are distinct authors with distinct methodologies; treating them as one citation would lose the structural-vs-numeric distinction this audit makes visible.

**Seam:**

If an M5 hardware capture disagrees with the bannister-readout values, Plan 05 (or a new Phase 7 plan) lands a superseding ADR with the new values + a regenerated SHA-256 pin + updated test fixtures. The dual-transcription discipline (C + Python + test invariants) means the ADR alone doesn't need to decide "which source wins"; the superseding ADR simply records the new values and all four check surfaces update in lockstep.

**Revision Path:**

- M5 hardware validation runs a cross-console readout on ≥ 2 independent PSX units; disagreement drives a new ADR.
- Any published coefficient table that differs from the bannister values is a trigger to re-audit.

**Sources:**

- BIB-005 — `psx-spx.consoledev.net` Reverb Buffer Resampling coefficient table.
- BIB-006 — `forums.bannister.org` SCPH-5501 hardware readout (primary).
- BIB-007 — jsgroth PS1 SPU Part 3 (structural corroboration).
- 04-RESEARCH § Coefficient provenance audit (this plan's audit trail).
- `docs/BIBLIOGRAPHY.md` (Plan 01 — licensing tags + URLs).
- `src/spu94/spu94_fir_coef.c` + `tests/python/derive_fir_reference.py` (independent transcriptions).

---

## ADR-0019: FIR chain latency contract — `SPU94_LATENCY_SAMPLES = 58u` + public accessor

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** D-09 (04-CONTEXT.md); ROADMAP Phase 4 partial SC-1.

**Relates:** ADR-0018 (internal 44.1 kHz wrapper + per-channel FIR state), Phase 5 `spu94_process` block API (future consumer).

**Context:**

M4 JUCE / VST3 / AU hosts need a plugin-delay-compensation (PDC) value to align their mix bus when the plugin introduces latency. Phase 5's block-based `spu94_process` wraps Plan 03's 44.1 kHz `spu94_fir_chain_step`, which adds the FIR group delay. The value must be (a) a compile-time constant (so callers can size static arrays), (b) exposed via a runtime accessor (so Phase 6 Python bindings + M4 plugins that prefer accessor style both work without header-churn), and (c) derived from the FIR architecture (not a magic number).

The original 04-RESEARCH derivation claimed "19 decimator + 19 interpolator = 38 samples at 44.1 kHz." Plan 03's chain-level impulse test — driving a single-sample impulse through `spu94_fir_chain_step_reverb_bypass` and reading 80 44.1 kHz output samples — exposed an error in that derivation: the empirical impulse peak lies at 44.1 kHz output index 57 (phase-0 stream) tied with 59 (phase-1 stream), not 38.

**Decision:**

- `include/spu94/spu94.h` defines `#define SPU94_LATENCY_SAMPLES 58u` — the nominal round-trip FIR group delay at 44.1 kHz input-to-output.
- `uint32_t spu94_get_latency_samples(void)` is a public-API accessor returning `SPU94_LATENCY_SAMPLES`. Single-line body; LTO-eliminable at C consumer call sites.
- The corrected derivation: the decimator's 19-sample group delay is at its 44.1 kHz INPUT clock (impulse reaches `delay[19]`). The interpolator's 19-sample group delay is at its 22.05 kHz INPUT clock, which corresponds to **38 samples at the 44.1 kHz OUTPUT clock** (the interpolator emits two 44.1 kHz samples per 22.05 kHz sample consumed, so one 22.05 kHz delay step = two 44.1 kHz output clock ticks). Total: 19 + 38 = **57 samples**. The empirical impulse peak is tied at t=57 (phase-0, even indices) and t=59 (phase-1, odd indices) because the polyphase split produces two independent symmetric subsequences whose axes straddle the true continuous-time latency of 57.5. `SPU94_LATENCY_SAMPLES = 58u` is the midpoint of the two tied peaks; the `test_fir_chain_latency` TU asserts the empirical peak is within ±1 sample of the API value.
- The **ORIGINAL "19+19=38" rationale is wrong and must NOT be repeated.** It mixed clock domains on the interpolator contribution. This ADR is the canonical record of the corrected derivation.

**Consequences:**

- M4 plugin `prepareToPlay` calls `spu94_get_latency_samples()` and feeds the result to `AudioProcessor::setLatencySamples()` for DAW PDC. Zero caller-side math required.
- Phase 6 Python bindings expose the accessor; the macro is also available to C consumers that prefer compile-time constants for static array sizing.
- Tests: `tests/unit/fir/test_fir_chain_latency.c` pins macro = accessor = 58u; `test_fir_impulse.c` asserts the empirical peak is within ±1; Plan 04's `tests/python/fuzz_fir.py` asserts the accessor returns 58u at every step of a 10⁶-step random-input run.
- The ±1-sample tolerance in `test_fir_chain_latency` accommodates the tied-peak polyphase split without requiring a bumpy "±0 exact" contract that doesn't mean anything for a polyphase filter.

**Alternatives Considered:**

- **Make `SPU94_LATENCY_SAMPLES` a `size_t` instead of `u32`.** Rejected: `uint32_t` is portable + non-ambiguous; `size_t` varies across MCU/desktop and adds zero value.
- **Ship only the macro, no accessor.** Rejected: Phase 6 Python ctypes prefers accessors over header-parsed macros (keeps the binding surface stable across header reshuffles).
- **Leave the value at 38u and explain the discrepancy in a comment.** Rejected: the empirical peak at 57/59 is a fact; the macro must encode the fact honestly, and a ±20-sample lie would fail the `test_fir_chain_latency` ±1 check.

**Seam:**

If the FIR design ever changes (tap count changes from 39 or the filter becomes asymmetric), the macro + accessor return the new value; callers that use `spu94_get_latency_samples()` transparently pick up the change. The macro is an intentional extensibility point.

**Sources:**

- `include/spu94/spu94.h` (macro + accessor declaration).
- `src/spu94/spu94_io_chain.c` (accessor body).
- `tests/unit/fir/test_fir_chain_latency.c` (macro = accessor = empirical-peak ±1).
- `tests/unit/fir/test_fir_impulse.c` (polyphase-split impulse-response shape + tied peaks at 57/59).
- 04-03-SUMMARY.md § Deviations Rule 1 (the 38→58 correction + corrected derivation).
- 04-RESEARCH § 5 (half-band cascade latency — now known to be 57.5 continuous, 58u rounded for the nominal value).

---

## ADR-0018: Internal 44.1 kHz FIR wrapper + per-channel FIR state

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** D-07 (internal-only wrapper); D-08 (per-channel FIR state).

**Relates:** ADR-0019 (latency contract), Phase 5 `spu94_process` block API.

**Context:**

Plan 03 composes `spu94_fir_decimate` → `spu94_tick` → `spu94_fir_interpolate` into a single per-44.1-kHz-sample wrapper. The composition needs (a) a stable home that each of the three stage helpers calls exactly once (Pitfall 4 from 04-CONTEXT.md), (b) a test-only bypass variant so Phase 4 tests can exercise the FIR chain without reverb contamination (SC-1/SC-2 at chain level), and (c) a state layout that keeps L and R delay lines independent (D-08) so a future stereo-decorrelated workload doesn't introduce cross-channel artifacts.

The design choice is whether the wrapper is (i) public API (Phase 5 users call it directly) or (ii) internal plumbing (Phase 5 wraps it in a block API). Option (i) front-loads per-sample call overhead on every user; option (ii) preserves the flexibility to add block-level optimizations later (Phase 6 SIMD; Phase 7 witness-diff hooks).

**Decision:**

- `spu94_fir_chain_step(state, l_in_44k1, r_in_44k1, *l_out, *r_out)` and its test-bypass sibling `spu94_fir_chain_step_reverb_bypass(state, ...)` live in `src/spu94/spu94_io_chain.c`, declared in **`src/spu94/spu94_fir_internal.h`** (NOT on the public include path). Phase 5 composes them into the public block-based `spu94_process`.
- The two wrappers share a static `chain_step_impl(..., reverb_active)` helper parameterized on whether `spu94_tick` runs on the retained phase. Single source of the state-machine logic; the two public-internal wrappers differ only in the `reverb_active` flag.
- State fields in `struct spu94_state`:
  - Per-channel 39-sample int16 delay lines: `fir_delay_l_in[39]`, `fir_delay_r_in[39]`, `fir_delay_l_out[39]`, `fir_delay_r_out[39]`. Independent per channel per stage (D-08 — stereo-decorrelated workloads cannot cross-contaminate).
  - Per-channel uint8 indices: `fir_idx_l_in`, `fir_idx_r_in`, `fir_idx_l_out`, `fir_idx_r_out`. Circular-buffer position; modular arithmetic with `(idx + 38u - k) % 39u` for logical-tap reads.
  - Phase trackers: `fir_decimate_phase` (uint8 — 0/1, advances on every 44.1 kHz input), `fir_interpolate_phase` (uint8 — Pitfall 7 redundancy, kept in lockstep with `fir_decimate_phase` of opposite polarity so desync triggers a test failure rather than silent drift).
  - Phase-1 cache: `fir_pending_l_phase1`, `fir_pending_r_phase1` (int16 — the interpolator emits both phases on retained calls; phase-0 returns immediately, phase-1 is cached here and emitted on the next non-retained 44.1 kHz call).
- `spu94_reset` zeros the whole struct via its existing hand-rolled byte-loop; no additional FIR-specific reset code is needed (D-23 — read-only observability fields are already covered).

**Consequences:**

- Public API surface of Phase 4 is exactly ONE new symbol: `spu94_get_latency_samples()` (ADR-0019). Everything else is internal plumbing, consumed by Phase 5.
- Pitfall 4 single-call-site discipline is preserved: `grep -rE "spu94_fir_decimate\(" src/spu94/ --include='*.c'` returns 1 hit (in `chain_step_impl`); `grep -rE "spu94_fir_interpolate\("` returns 1 hit (also in `chain_step_impl`).
- Reverb-bypass wrapper is always-compiled (not `#ifdef`-gated), so the M4 plugin era can promote it to a user-facing "FIR bypass" toggle without CMake churn. Same primitive; different call site.
- `sizeof(struct spu94_state)` grew from 200 bytes (end of Phase 3) to 544 bytes (end of Phase 4 Plan 03) — +344 bytes for the FIR fields, still 30× headroom vs `SPU94_STATE_SIZE_MAX = 16384`.

**Alternatives Considered:**

- **Shared L/R delay line.** Rejected: cross-channel contamination is a future-proof cost; the ~156-byte savings is not worth the risk.
- **Public 44.1 kHz chain API.** Rejected: Phase 5's block API is the intended public surface; exposing the per-sample wrapper would commit to that signature as API.
- **Separate `chain_step` and `chain_step_reverb_bypass` implementations (no shared helper).** Rejected: duplicates the state-machine logic; a bug in one will not automatically show up in the other.

**Seam:**

The `reverb_active` parameter on `chain_step_impl` is the seam: Plan 05 or a later phase can add a third wrapper (e.g., a "reverb-muted-but-FIR-active" variant for an M4 FIR-monitor) without touching the core state-machine logic. The state-field layout (all `fir_*` fields grouped at the end of `struct spu94_state`) is also a seam — future FIR additions (taps, rates) append here without cascading struct-layout changes across other phases.

**Sources:**

- `src/spu94/spu94_io_chain.c` (chain-wrapper bodies + shared helper).
- `src/spu94/spu94_fir_internal.h` (internal-only declarations).
- `src/spu94/spu94_state_internal.h` (14 FIR state fields + 2 Pitfall-7 phase-1 cache fields).
- 04-CONTEXT.md D-07 + D-08.
- 04-03-SUMMARY.md § Function Layout.

---

## ADR-0017: FIR per-multiply err-tap — aggregate-post-shift-remainder under clamp-once

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** D-06 (04-CONTEXT.md); 04-RESEARCH § Pattern 1 reconciliation + Assumption A7.

**Relates:** ADR-0011 (Phase 3 per-multiply err-tap + overflow-magnitude observable), ADR-0015 (FIR clamp policy — clamp-once default), ADR-0014 (accumulator width).

**Context:**

Phase 3 ADR-0011 established the err-tap pattern: every Q15 multiply in the reverb network runs through `q15_mul_truncate_with_err` and writes its pre-saturation remainder to a per-stage `int32` field on `struct spu94_state`. CONTEXT D-06 asked to extend the pattern to the FIR — "every ~10 folded multiplies calls q15_mul_truncate_with_err".

A strict reading of that language implies per-multiply shift-and-saturate, i.e., the cascade-clamp regime (D-04). Under cascade-clamp, the FIR saturates intermediate sums and the numerical output diverges from the clamp-once default (ADR-0015). 04-RESEARCH § Pattern 1 surfaced the contradiction + proposed a reconciliation: the INTENT of D-06 is to observe the complete precision-loss surface at the FIR boundary, not to engage cascade-clamp's numerical divergence. Under clamp-once, the aggregate post-shift remainder `(acc - (shifted << 15))` in a single accumulate captures the same discarded low bits as a per-multiply sum — but without the per-multiply saturation that would change the bit-exact output.

Assumption A7 from 04-RESEARCH formalizes this: *under D-03 clamp-once, the bit-faithful interpretation of D-06 is the aggregate-post-shift-remainder tap.*

**Decision:**

SPU-94 adopts the **aggregate-post-shift-remainder interpretation** of D-06 under D-03 clamp-once:

- `state->err_fir_decimator` and `state->err_fir_interpolator` receive `(acc - (shifted << 15))` once per retained FIR output. `acc` is the full int32 sum-of-products; `shifted` is `acc >> 15` (arithmetic shift per ADR-0001); the remainder is the low 15 bits of `acc` interpreted as a signed int32 (negative if `acc` was negative).
- Per-multiply err wiring is **not** used in the FIR (unlike the reverb network, which uses `q15_mul_truncate_with_err` per multiply). The FIR accumulates in int32, shifts once, saturates once, and records the discarded-low-bits remainder once.
- This is bit-faithful to D-03 clamp-once: the recorded err value is the remainder of the same single shift that produced the FIR output sample.
- The strict per-multiply err reading — as a strict reading of D-06 would imply — is available as a compile-time option paired with `SPU94_FIR_CASCADE_CLAMP` (ADR-0015 seam). Engaging either the strict err-tap reading or the cascade-clamp mode crosses the same seam; they cannot be mixed.

**Consequences:**

- Runtime cost: one int32 subtract + one int32 shift + one int32 add per FIR output (decimator + interpolator each contribute). No branches; hot-path clean.
- The err-tap value for a full-scale adversarial input is bounded by `|acc|_max ≤ 0x5CD2632E` (ADR-0014), so the low-15-bits remainder is in `[-16384, 16383]`. Safely fits int32 even after many accumulations.
- Future M4 Controllers (drive meter, soft-clip warmth, err-stream envelopes) can consume `err_fir_decimator` + `err_fir_interpolator` identically to how they consume the Phase 3 `err_*` fields. The surface is uniform across reverb + FIR.
- Strict per-multiply err semantics are available if ever needed, via the D-04 seam in ADR-0015.

**Alternatives Considered:**

- **Strict per-multiply err-tap under clamp-once.** Rejected: engages cascade-clamp numerical divergence (ADR-0015 seam), which SPU-94 does not want as the FIR default.
- **Skip the FIR err-tap entirely.** Rejected: leaves a hole in the Controllers-consumption surface — half the precision-loss in the chain would be invisible.
- **Per-tap err (one int32 field per non-zero coefficient pair).** Rejected: 10× state-size increase for no consumer-visible benefit; aggregate suffices.

**Seam:**

D-04 cascade-clamp (ADR-0015) is the single seam that both changes numerical output AND enables strict per-multiply err-tap. If M5 hardware capture ever reveals cascade-clamp semantics in the PS1 silicon, engaging the `SPU94_FIR_CASCADE_CLAMP` compile-time switch also lights up strict per-multiply err — both changes cross together.

**Sources:**

- `src/spu94/spu94_fir.c` (aggregate err-tap implementation in `fir_folded_apply`).
- `src/spu94/spu94_state_internal.h` (`err_fir_decimator` + `err_fir_interpolator` fields).
- 04-RESEARCH § Pattern 1 reconciliation + Assumption A7.
- ADR-0011 (parent pattern; Phase 3).
- `tests/unit/fir/test_fir_err_overflow_taps.c` (zero-input/stress/reset invariants).

---

## ADR-0016: FIR overflow-magnitude tap on clamp boundary

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** D-05 (04-CONTEXT.md) for the FIR boundary.

**Relates:** ADR-0011 (Phase 3 hard-clip overflow-magnitude pattern — this ADR extends that pattern to the FIR).

**Context:**

Phase 3 ADR-0011 established the overflow-magnitude observable pattern: when the hard-clip stage saturates an int32 value to int16, `|input| - INT16_MAX` is added to `state->overflow_magnitude`. This gives future M4 Controllers (drive meter, warmth knob, overflow-modulated feedback) a direct observable for "how hard the reverb output hit the rails."

The FIR chain has two additional saturation boundaries: the decimator `sat_s16` (after the single clamp-once shift — ADR-0015) and the interpolator `sat_s16` (same structure, different subfilter). Without an overflow tap here, high-transient input samples that saturate at the FIR boundary would be invisible to the Controllers consumption surface.

**Decision:**

- `state->fir_overflow_decimator` and `state->fir_overflow_interpolator` are int32 accumulators that receive `|acc_shifted| - INT16_MAX` whenever the final `sat_s16` on the FIR output clamps, zero otherwise. Always-on, no branches on the hot path (the if/else-if selects the magnitude value; the add is unconditional).
- Read-only observability per D-23 — no public mutator API; `spu94_reset` zeros these fields via the existing byte-loop.
- Same contract as Phase 3's `overflow_magnitude` field: the accumulator grows monotonically over the session; consumers sample differences to detect "how much clamp happened in the last N samples."

**Consequences:**

- Runtime cost: one int32 conditional-select + one unconditional int32 add per FIR output. No branches (hot path pre-computes the magnitude value regardless of whether it'll be zero).
- `tests/unit/fir/test_fir_err_overflow_taps.c` pins the invariants: zero input → taps stay zero; adversarial stress → taps perturb and accumulate monotonically; `spu94_reset` zeros all four (two err + two overflow).
- Future M4 Controllers get a uniform surface: `overflow_magnitude` (reverb-body clip) + `fir_overflow_decimator` + `fir_overflow_interpolator` together describe the full "high-bits lost to saturation" surface across the reverb + FIR chain.
- The tap is conservative: the `sat_s16` clamp is rare in practice (the half-band FIR has DC gain ~0.5, so sustained |INT16_EXTREMA| input produces `|shifted| ≈ 16383 < INT16_MAX`, no saturation). Only adversarial-pattern inputs trigger the clamp.

**Alternatives Considered:**

- **Single shared overflow field across reverb + FIR.** Rejected: muddles the consumer surface; different consumers may care about "reverb clipping" vs "FIR clipping" independently.
- **Branch on the clamp condition (skip the add when no clamp).** Rejected: unconditional-add is branch-free and thus faster on MCU pipelines; the "if/else-if selects the magnitude" avoids any hot-path branch in the actual code.

**Seam:**

If M5 hardware capture reveals a different saturation semantics (e.g., wrap-around rather than clamp), ADR-0015's seam flips the clamp policy and the overflow tap meaning changes accordingly. The observable surface is stable; only the semantics of what "clamp" means updates.

**Sources:**

- `src/spu94/spu94_fir.c` (overflow-magnitude tap implementation in `fir_folded_apply`).
- `src/spu94/spu94_state_internal.h` (`fir_overflow_decimator` + `fir_overflow_interpolator` fields).
- ADR-0011 (parent pattern; Phase 3 hard-clip overflow).
- `tests/unit/fir/test_fir_err_overflow_taps.c` (invariants).
- 04-RESEARCH § Architecture Patterns (tap discipline).

---

## ADR-0015: FIR clamp policy — clamp-once default + cascade-clamp `#ifdef` seam

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** D-03 + D-04 (paired; 04-CONTEXT.md).

**Relates:** ADR-0007 (Phase 3 comb-sum cascading-clamp decision — this ADR INTENTIONALLY DIVERGES), ADR-0013 (FIR folded production + literal audit witness), ADR-0017 (err-tap interpretation).

**Context:**

The FIR accumulates 39 (or fewer, after folded-form optimization) products of Q15 coefficient × int16 input sample into an int32 accumulator, shifts right by 15, and saturates to int16. Two discipline choices appear:

- **Clamp-once (D-03):** one `sat_s16` at the final stage output. The intermediate accumulator holds the full-precision sum; only the output value is forced into int16 range.
- **Cascade-clamp (D-04):** one `sat_s16` per pair-sum (folded form) or per multiply-accumulate (literal form). The intermediate accumulator saturates at each step.

These two policies produce BIT-DIFFERENT outputs on adversarial inputs where an intermediate sum would exceed INT16. 04-RESEARCH § Folded-vs-Literal Bit-Identity Argument gives the worked example: for a crafted input pattern, the literal-form evaluates to −12 under clamp-once and −7 under folded-form cascade-clamp. The difference matters for bit-faithful reproduction against a hypothetical "real" PS1 reference.

Phase 3 ADR-0007 chose cascade-clamp for the comb-sum on musical grounds: the comb is a CHARACTER stage (its distortion is a feature). Phase 4 faces the opposite situation: the FIR is a TRANSPARENT resampling boundary (its distortion is an artifact, not a feature), so the policies diverge.

**Decision:**

- **Default: clamp-once (D-03).** The FIR computes the full int32 sum, shifts once, and saturates once. `src/spu94/spu94_fir.c::fir_folded_apply` implements this regime.
- **Seam: `SPU94_FIR_CASCADE_CLAMP` compile-time switch.** Defining this macro at build time engages per-pair `sat_s16` (cascade-clamp). The switch is undefined by default. When defined:
  - Numerical output diverges from the clamp-once default on adversarial inputs (per the 04-RESEARCH worked example).
  - The literal vs folded bit-identity audit (ADR-0013) is NO LONGER valid — `test_fir_bit_identity.c` has an `#error` guard that fires when `SPU94_FIR_CASCADE_CLAMP` is set, preventing a false-positive "audit passed" signal.
  - ADR-0017's err-tap interpretation changes: strict per-multiply err semantics light up under cascade-clamp.
- **This diverges from ADR-0007 intentionally.** The comb is a character stage; the FIR is a transparent boundary. Different audio context, same research method (taste + silicon inference).

**Consequences:**

- Hot-path `src/spu94/spu94_fir.c` is unbranched: one shift + one sat_s16 per FIR output. No per-step clamp overhead in the default build.
- Plan 02's `test_fir_bit_identity.c` asserts folded == literal under the default (clamp-once) build; cascade-clamp builds do not run this test, as guarded by the `#error`.
- M4 character-FX (plugin era) can promote `SPU94_FIR_CASCADE_CLAMP` from compile-time to runtime (function pointer) without caller-side churn — the function signatures are stable; only the body swaps. 04-RESEARCH § Decision Proposals covers the promotion path.
- If M5 hardware capture reveals the PS1 silicon actually implements cascade-clamp at the FIR boundary, engaging `SPU94_FIR_CASCADE_CLAMP` is a one-build-flag change and no source-file edit is needed. Supersede this ADR accordingly.

**Alternatives Considered:**

- **Always cascade-clamp (match ADR-0007).** Rejected: the FIR is transparent-boundary, not character stage; cascade-clamp introduces audible artifacts at adversarial inputs for no reproduction benefit.
- **Always clamp-once, no seam.** Rejected: M5 hardware capture may eventually demand cascade-clamp; bolting on a runtime switch after the fact is harder than leaving the seam now.
- **Runtime switch from day one (not compile-time).** Rejected: runtime switch adds a per-call branch; compile-time switch costs nothing at runtime. The M4 era upgrades compile → runtime if a user-facing character switch is wanted.

**Seam:**

`#ifdef SPU94_FIR_CASCADE_CLAMP` in `src/spu94/spu94_fir.c::fir_folded_apply`. The code above the seam is shared; the per-pair `sat_s16` insertion lives inside the `#ifdef`. Stable across the default-vs-cascade configurations at source-diff granularity.

**Revision Path:**

- M4 plugin user-facing "character" switch promotes compile-time → runtime.
- M5 hardware capture confirms or rejects cascade-clamp at FIR boundary; supersede this ADR.

**Sources:**

- `src/spu94/spu94_fir.c` (clamp-once default + `#ifdef` seam).
- `tests/unit/fir/test_fir_bit_identity.c` (`#error` guard under cascade-clamp).
- 04-RESEARCH § Folded-vs-Literal Bit-Identity Argument (worked −12 vs −7 example).
- ADR-0007 (Phase 3 — comb-sum cascade-clamp; explicitly diverged from).
- ADR-0013 (folded + literal audit witness; bit-identity contract under clamp-once).

---

## ADR-0014: FIR accumulator width — int32 with 0.46-bit decimator margin + int64 typedef seam

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** D-02 (04-CONTEXT.md § Area A); ROADMAP Phase 4 SC-3.

**Relates:** ADR-0001 (Q15 arithmetic-right-shift guarantee), ADR-0003 (UBSan no_sanitize policy), ADR-0015 (FIR clamp-once default).

**Context:**

The 39-tap decimator sums 39 products of (int16 coefficient × int16 input sample) into a local accumulator before a single arithmetic-right-shift by 15 and a final `sat_s16` to int16. The accumulator type must hold the worst-case sum without signed-integer overflow (UB under UBSan + defined-but-surprising under -O2). Choosing int32 vs int64 trades MCU cycle cost (int64 is more expensive on Cortex-M without hardware 64-bit MAC) against safety margin. CONTEXT D-02 specified int32 with a derived-from-coefficients no-overflow proof.

**Decision:**

The FIR accumulator is `int32_t` in production. The worst-case bound was derived analytically in 04-RESEARCH § 7 (Accumulator Width Proof) and validated empirically via `tests/unit/fir/test_fir_overflow_proof.c`.

Honest margin disclosure (verbatim from 04-RESEARCH § 7):

- **Decimator (full 39-tap FIR):**
  - Sum of |h[k]| over 39 taps: `47,526 = 0xB9A6`.
  - Analytic worst-case product `47,526 × 32,768 = 1,557,331,968 = 0x5CD30000` (requires |x|=32768 simultaneously on both signs — unreachable by int16).
  - Achievable int16 bound `x = +32767 if coef[k] ≥ 0 else -32768`: empirically `0x5CD2632E = 1,557,291,822`.
  - INT32_MAX = `0x7FFFFFFF = 2,147,483,647`. Headroom above achievable bound: `0x232CFFFF = 590,151,679`.
  - **Margin to INT32_MAX: 2.791 dB = 0.464 bits.** ← tight but sufficient.
- **Interpolator phase-0 (every-other-tap subfilter):**
  - Adversarial worst-case magnitude: `0x3CD30000 = 1,020,461,056`.
  - Margin: `6.463 dB = 1.074 bits`.
- **Interpolator phase-1 (center-tap only):**
  - Adversarial worst-case magnitude: `0x20000000 = 536,870,912`.
  - Margin: `12.04 dB = 2.0 bits`.

The 0.46-bit decimator margin is EXPLICITLY DISCLOSED here, not hidden behind a "fits in int32" claim. Any future composition — additional accumulate stages, cascading intermediate clamps (ADR-0015), Q30 coefficient promotion — that tightens this margin below zero bits triggers the promotion seam below.

**Consequences:**

- Hot path stays MCU-friendly (no 64-bit MAC on Cortex-M7). `src/spu94/spu94_fir.c` is int32 throughout.
- `tests/unit/fir/test_fir_overflow_proof.c` drives the accumulator to exactly `0x5CD2632E` under adversarial input + verifies the negative analog is UB-free (sign-symmetric magnitude). SC-3 closed at test-vector level; Plan 04's `fuzz_fir.py` adds 10⁶-step regression cover.
- UBSan CI flags any signed-integer overflow regression. ADR-0003's `no_sanitize` policy does NOT apply to the FIR — the accumulator is proved safe, so UBSan's catch-all covers it.
- The 0.46-bit margin is narrow. Plan 04 disclosed it verbatim in this ADR + in 04-04-SUMMARY.md; no future composition can silently burn through it.

**Alternatives Considered:**

- **int64 accumulator by default.** Rejected: extra cost on Cortex-M with no safety benefit for Phase 4 as scoped. Retained as a hedge seam (below).
- **Q30 coefficient promotion + int64.** Rejected: changes bit-faithful output; out of scope for Phase 4 authenticity posture.
- **Runtime overflow-detection + graceful degradation.** Rejected: SPU-94's bit-faithful posture doesn't permit graceful degradation — the output is the spec or it's not. If overflow were possible, promote the accumulator type.

**Seam (D-22):**

The accumulator type is effectively `int32_t` at `acc` variable granularity inside `fir_folded_apply` + `fir_interp_phase0_apply` + `fir_interp_phase1_apply`. Widening to int64 is a mechanical change (promote the `int32_t acc` local to `int64_t acc`; `sat_s16` accepts either width via the existing Phase 1 primitive). No caller change required. A future plan that needs the promotion can land a typedef `spu94_fir_acc_t` + search-and-replace, all in one commit.

**Revision Path:**

If any future composition — cascading intermediate clamps from ADR-0015 engaged, Q30 coefficient promotion (M5 hardware-capture-driven), or additional accumulate stages stacked into the FIR boundary — tightens the decimator margin to ≤ 0 bits, promote the accumulator to `int64_t` per the seam. Supersede this ADR.

**Sources:**

- 04-RESEARCH § 7 Accumulator Width Proof (primary derivation).
- 04-RESEARCH § Coefficient Table (coefficient values used in derivation).
- `src/spu94/spu94_fir.c` (in-source D-02 proof comment).
- `tests/unit/fir/test_fir_overflow_proof.c` (empirical validation at `0x5CD2632E`).
- `tests/python/fuzz_fir.py` (10⁶-step runtime cover).
- ADR-0001 (Q15 arithmetic-right-shift guarantee — basis of the `>> 15` reduction).

---

## ADR-0013: FIR math form — folded production + literal 39-multiply audit witness

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** D-01 (04-CONTEXT.md).

**Relates:** ADR-0015 (clamp-once default establishes the regime under which bit-identity holds), ADR-0017 (err-tap aggregate reading agrees under clamp-once).

**Context:**

The 39-tap half-band Type I FIR has structural redundancy: the coefficient table is symmetric about index 19 (`h[k] == h[38-k]`), and 18 off-center odd positions are zero. A literal 39-multiply implementation pays for every tap; a folded implementation exploits the symmetry to pay for ~11 multiplies. The folded form is ~3.5× faster on an MCU hot path without changing the numerical output — **under clamp-once (ADR-0015)**.

The catch is "under clamp-once." Cascade-clamp (ADR-0015 seam) breaks folded-vs-literal bit-identity on adversarial inputs. CONTEXT D-01 asked for a production math form + a deliberate audit trail so that reviewers can see the symmetry exploit is numerically-justified + regression-protected.

**Decision:**

- **Production:** `fir_folded_apply` in `src/spu94/spu94_fir.c` — center tap + 9 non-zero pairs (skipping the 18 zeros by construction). ~11 int32 multiplies per FIR output (vs 39 in the literal form).
- **Audit witness:** `spu94_fir_decimate_literal_reference` + `spu94_fir_folded_reference` — declared in `src/spu94/spu94_fir_internal.h` (test-visible, not public API). Literal-form performs all 39 multiplies, no folding. Permanently compiled into the test binary so a reviewer can read it alongside the production form.
- **Bit-identity contract:** `tests/unit/fir/test_fir_bit_identity.c` asserts `spu94_fir_folded_reference(x) == spu94_fir_decimate_literal_reference(x)` over 10⁵ random int16 inputs + the 04-RESEARCH adversarial-worst-case input. Contract holds under the clamp-once default build; the test TU has an `#error` guard that refuses to compile under `SPU94_FIR_CASCADE_CLAMP` (because bit-identity does not hold there by design).

**Consequences:**

- 3.5× reduction in production multiply count (~11 vs 39); measurable on MCU cross-compile smoke tests.
- The audit trail is legible: a reviewer who doesn't trust the folded implementation can read the literal reference in the same source file, run the bit-identity test, and verify the symmetry exploit is sound.
- The `#error` guard on the bit-identity TU under cascade-clamp prevents a false-positive "audit passed" signal in a build where the contract doesn't apply.
- Plan 04's `fuzz_fir.py` adds 10⁶-step runtime cross-check via the production wrapper (which composes the folded FIR).

**Alternatives Considered:**

- **Literal-only production (no folded form).** Rejected: 3.5× MCU cycle cost for no correctness benefit.
- **Folded-only, no audit witness.** Rejected: leaves the symmetry exploit unprotected against regression; a reviewer cannot cross-check without running the generator script on every audit.
- **Audit witness generated at test time via a Python pass.** Rejected: C-test-embedded audit witness is faster to audit (one open file vs a generator run) and checks a real C compilation path.

**Seam:**

ADR-0015's `SPU94_FIR_CASCADE_CLAMP` is the single seam across clamp policy. Under cascade-clamp, the bit-identity contract of this ADR is explicitly invalidated + the test is disabled (#error). No in-between state.

**Sources:**

- `src/spu94/spu94_fir.c` (folded production + literal + folded audit-reference functions).
- `src/spu94/spu94_fir_internal.h` (audit-witness declarations).
- `tests/unit/fir/test_fir_bit_identity.c` (contract assertion + `#error` guard).
- 04-RESEARCH § Folded-vs-Literal Bit-Identity Argument (worked example of where it breaks under cascade-clamp).
- 04-02-SUMMARY.md § Function Layout.

---

## ADR-0012: Half-rate architecture + lv2-psx-reverb OUT-OF-AXIS exclusion

**Status:** Accepted (2026-04-20, Phase 4)

**Resolves:** ROADMAP Phase 4 SC-4.

**Relates:** ADR-0018 (internal 44.1 kHz wrapper + per-channel state), ADR-0019 (latency contract), ADR-0020 (coefficient provenance), Phase 7 TEST-03 future work.

**Context:**

The PS1 SPU reverb operates internally at 22.05 kHz — half the 44.1 kHz system output rate — and applies a 39-tap half-band FIR at both I/O boundaries of the reverb ring to convert between the two rates. PROJECT.md's key decisions call this out explicitly: *"22.05 kHz internal reverb tick with 39-tap half-band FIR at both I/O boundaries (bit-faithful at the boundary, closing the lv2-psx-reverb gap)."* The ROADMAP Phase 4 SC-4 asks for a DECISIONS.md ADR formalizing this half-rate architecture AND excluding lv2-psx-reverb from the frequency-response witness axis because lv2-psx-reverb's own README self-attests that it skips the FIR.

Mednafen (GPLv2) and DuckStation (CC-BY-NC-ND as of 2024-09) are two additional witness emulators. Their classification (IN-AXIS — implements the FIR / OUT-OF-AXIS — skips the FIR) would strengthen Phase 7 TEST-03's witness-diff tolerance calibration, but they are not on the critical path for Phase 4 closure: SC-4 explicitly names lv2-psx-reverb, and that classification is attested by primary source (the project's own README).

**Decision:**

SPU-94 architecture (ratified):

- **Internal reverb tick rate:** 22.05 kHz (half of 44.1 kHz).
- **Boundary FIR:** 39-tap half-band, applied at BOTH the 44.1 kHz → 22.05 kHz input boundary (decimator) AND the 22.05 kHz → 44.1 kHz output boundary (interpolator).
- **Latency contract:** `SPU94_LATENCY_SAMPLES = 58u` at the 44.1 kHz input-to-output rate (ADR-0019 — the corrected derivation).

Witness classification for frequency-response axis:

- **lv2-psx-reverb: OUT-OF-AXIS (HIGH confidence).** Primary-source attested by its own README, quoted verbatim in 04-RESEARCH § Witness Analysis: the project chose not to implement the half-band FIR. Its reverb-network behavior remains a valid witness; its frequency-response cannot be used to validate SPU-94's FIR implementation.
- **Mednafen (GPLv2): classification pending.** Empirical pass deferred; protocol documented in `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/witness-captures/README.md`.
- **DuckStation (CC-BY-NC-ND as of 2024-09): classification pending.** Empirical pass deferred; same protocol.

Deferral basis: neither Mednafen nor DuckStation is installed on the Plan 04 executor's machine, and no PSX test ROM is available. Phase 7 TEST-03 (witness-diff harness) picks up the deferred captures + classifications. This is NOT a Phase-4 gap — SC-4's mandate is lv2-psx-reverb exclusion, which is primary-source attested.

PROJECT.md licensing posture (per PROJECT.md + `docs/BIBLIOGRAPHY.md`): Mednafen and DuckStation are consumed only as OUTPUT witnesses. Their SOURCE code is never read as a primary input to SPU-94 design.

**Consequences:**

- Phase 4 SC-4 GREEN: ADR landed + lv2-psx-reverb exclusion recorded + protocol for future empirical captures documented.
- Phase 7 TEST-03 harness work has a clear prerequisite: install Mednafen + DuckStation, acquire a PSX test ROM, follow the `witness-captures/README.md` protocol, fill in the classification table, and either supersede this ADR with the captured data or record a follow-up ADR carrying the deltas.
- The 58u latency contract (ADR-0019) is the downstream consumer. M4 plugin PDC + Phase 5 block API pre-roll/post-roll depend on this value.

**Alternatives Considered:**

- **Block Phase 4 closure on Mednafen + DuckStation empirical pass.** Rejected: SC-4 explicitly names lv2-psx-reverb; deferral is scoped by the plan document.
- **Land the ADR with lv2-psx-reverb only; no Mednafen/DuckStation mention.** Rejected: the mention + protocol commitment is the honest record of what Phase 4 did + did not do.
- **Skip the half-rate architecture ADR (treat it as implicit in PROJECT.md).** Rejected: PROJECT.md is a project-level framing; DECISIONS.md is the architecture-level ratification. SC-4 asked for both.

**Seam:**

The witness classification table in `witness-captures/README.md` is the seam: when Mednafen and DuckStation captures are collected, the table updates + either (a) this ADR is superseded by a new ADR carrying the classifications OR (b) a follow-up ADR records the delta and keeps this ADR as the architectural ratification. Either path is open.

**Sources:**

- `.planning/PROJECT.md` Key Decisions (half-rate architecture ratification).
- `.planning/ROADMAP.md` Phase 4 SC-4 (lv2-psx-reverb exclusion mandate).
- 04-RESEARCH § Witness Analysis (lv2-psx-reverb README self-attestation quoted verbatim).
- BIB-008 (lv2-psx-reverb project + README).
- `docs/BIBLIOGRAPHY.md` BIB-009 (Mednafen; GPLv2) + BIB-010 (DuckStation; CC-BY-NC-ND).
- `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/witness-captures/README.md` (empirical-pass protocol + deferred-classification tracking).
- ADR-0018 (wrapper + per-channel state), ADR-0019 (latency contract), ADR-0020 (coefficient provenance).

---

## ADR-0011: Per-multiply err-tap + overflow-magnitude observable

**Status:** Accepted (2026-04-19, Phase 3)

**Context:**

Every Q15 multiply in the reverb network discards the low 15 bits of the
full 32-bit product when it shifts right by 15 to re-scale. The hard-clip
stage additionally discards the high bits of any int32 input whose
magnitude exceeds INT16_MAX when it saturates to int16. Together, these
two forms of precision loss describe the reverb's complete discard
surface: low bits thrown away by the shift + high bits thrown away by
the clamp. CONTEXT.md D-11 and the Controllers-milestone seed in
Deferred Ideas both wanted this surface observable so that future
musical features (drive meter, soft-clip warmth, overflow-modulated
feedback, err-stream envelopes) can consume it without having to fake
the data after the fact.

Three scopes were on the table: (i) wire every multiply to a per-stage
err accumulator and add an overflow-magnitude out-param on the clip;
(ii) wire only the feedback-loop multiplies (the ones that amplify
drift across ticks); (iii) skip per-multiply wiring in Phase 3 and
revisit when a consumer exists.

**Decision:**

Scope (i). Every Q15 multiply in every reverb stage runs through
`q15_mul_truncate_with_err` and its truncation remainder accumulates
into a per-stage int32 field on `struct spu94_state`
(`err_input_scale`, `err_same_iir`, `err_diff_iir`, `err_comb`,
`err_apf1`, `err_apf2`, `err_output_scale`). The hard-clip stage
additionally emits an overflow-magnitude out-param — `|input| -
INT16_MAX` for int32 inputs outside the int16 range, zero otherwise —
which `spu94_reverb_body` accumulates into a sibling
`overflow_magnitude` state field.

Scope (ii) was rejected: limiting observability to the feedback
multiplies would have required a second multiply helper and a per-site
routing decision, and would have left the comb's + APF's non-feedback
discards invisible. Scope (iii) was rejected because retrofitting the
per-multiply wiring after Controllers consumers exist is strictly more
expensive than adding it day-one.

**Consequences:**

- Runtime cost: one int32 add per multiply + one int32 add in the
  clip. No runtime branches, no conditional logic, no allocations.
- Test obligation: Phase 3 Plan 04's `test_reverb_edges` and per-stage
  test TUs assert err is zero for non-saturating input, nonzero and
  monotonic under saturating input, and matches the Python reference
  script's remainder exactly. `test_reverb_body` asserts that the
  full-body path and the stage-by-stage path accumulate the same err
  and overflow totals.
- No public API surface in Phase 3. The err and overflow fields live
  in `struct spu94_state` and are read-only per D-23.
- Controllers (M4) forward-dependency: public read-only getters for
  each field are the one-line addition that turns the taps into a
  musical feature surface. No reverb-code change is needed for that
  step; all the wiring is already in place.

**Alternatives Considered:**

- Scope (ii) — feedback multiplies only. Rejected: partial coverage
  without a clean stopping principle.
- Scope (iii) — defer to Phase 4+. Rejected: refit cost.
- Widening `err_out` to int32. Considered; kept at int16 per ADR-0004
  because the pre-saturation remainder always fits in int16 by
  construction (`remainder = product - (shifted << 15)`).

**Seam (D-22):**

The accumulator fields are struct members. A future Controllers
milestone adds public read-only accessors without touching reverb code.
A future refactor that wants per-multiply stream observability (rather
than summed-per-stage) adds a callback parameter to
`q15_mul_truncate_with_err`'s existing signature (`err_out`) without
breaking the current API — that is the point of ADR-0004's extensibility
tap.

**Revision Path:**

- Plugin-era user testing reveals that one or more err fields are
  useless or misleading: demote or remove that field in a superseding
  ADR; the struct size shrinks.
- Hardware capture (M5) reveals an additional precision-loss surface
  (e.g., ADPCM decoder bit-churn): add a sibling int32 field in a new
  ADR; this ADR is not superseded but extended.

**Sources:**

- psx-spx.consoledev.net/soundprocessingunitspu/ — primary source for
  the reverb multiplies whose discards this ADR observes. Paraphrased.
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md`
  § Post-Research Decisions (D-11 — scope + Phase 3 expansion).
- Internal: `.planning/notes/2026-04-19-error-accumulator-concept.md`
  (the originating Controllers use-case note behind ADR-0004).
- Prior ADR: ADR-0004 (extensibility taps: `q15_mul_truncate_with_err`
  is the underlying mechanism this ADR consumes across every stage).
- Prior ADR: ADR-0001 (Q15 multiply semantics — the shift that
  produces the truncation remainder).
- Prior ADR: ADR-0009 (hard-clip stage placement — the site of the
  overflow-magnitude emit).

---

## ADR-0010: vIIR = INT16_MIN anomaly mechanism

**Status:** Accepted (2026-04-19, Phase 3) — implements the intent of
ADR-0002.

**Context:**

The nocash documentation describes a quirk of the reverb IIR stages:
when the vIIR coefficient register is written with exactly INT16_MIN
(`-0x8000`), the value the IIR stage stores to memory ends up negated
relative to what the straightforward arithmetic would have produced.
The documentation reports the observable effect; it does not describe a
hardware mechanism. ADR-0002 (Phase 1) committed SPU-94 to reproducing
the observable effect; Phase 3 had to pick an implementation pattern.

Two approaches were considered. The first: an explicit branch at the
point of the memory write that detects `vIIR == INT16_MIN` and negates
the final result. The second: search for a specific saturation-
arithmetic sequence that produces the observable effect as an emergent
consequence of the INT16_MIN operand running through the normal
arithmetic path.

**Decision:**

Explicit branch at the memory-write point of each IIR stage (SAME and
DIFF). Inside `spu94_reverb_same_iir` and `spu94_reverb_diff_iir`,
after the normal arithmetic has produced the saturated int16 `result`:

```c
if (vIIR_snap == INT16_MIN) {
    result = sat_s16(-(int32_t)result);
}
reverb_buf_write(state, mXSAME, result);
```

The `int32_t` widening before negation guards against INT16_MIN-
negation undefined behavior (Pitfall 1 from 03-RESEARCH.md); the
saturating cast back to int16 handles the `+0x8000` overflow that
appears when the pre-negation `result` was exactly INT16_MIN.

**Consequences:**

- Observable behavior matches the nocash description exactly.
- Mechanism is auditable: one explicit branch per IIR stage, greppable
  (`grep -c 'vIIR_snap == INT16_MIN' src/spu94/spu94_reverb.c` should
  return 4 — L+R per stage, 2 stages).
- No runtime cost when `vIIR != INT16_MIN`; a single compare + branch
  otherwise.
- Test obligation: `test_reverb_same_iir` / `test_reverb_diff_iir` and
  `test_reverb_edges` all assert the anomaly fires at `vIIR =
  INT16_MIN` and a control case at `vIIR = INT16_MIN + 1` does not.

**Alternatives Considered:**

- **Emergent-from-saturation variant.** Try to find an arithmetic
  sequence where the INT16_MIN operand produces the negation naturally
  (e.g., via a specific ordering of saturations, sign flips, and
  shifts). Rejected: the nocash documentation tells us what the
  observable effect is, not how the hardware arrives at it;
  speculating at an undocumented mechanism risks drifting away from
  observable correctness when the speculation is wrong at a different
  operand combination. Explicit is the honest option.
- **No guard** (native `-result` negation). Rejected immediately —
  undefined behavior at `result == INT16_MIN` (Pitfall 1).

**Seam (D-22):**

The branch itself is the seam. If hardware capture (M5) ever reveals
an emergent mechanism that produces the same observable at the
INT16_MIN edge but a different observable at (for example) INT16_MIN
combined with specific wall-tap values, replace the branch body with
the captured arithmetic in a superseding ADR. The call sites do not
change.

**Revision Path:**

- M5 hardware capture discloses a specific emergent pattern: replace
  the branch body with the captured arithmetic; ADR-0010 is superseded.
- M5 discloses that the anomaly does NOT fire at exactly INT16_MIN but
  at some other specific value: adjust the compare; supersede ADR-0010.
- Nocash updates to describe a different observable behavior: re-open
  both ADR-0002 and ADR-0010.

**Sources:**

- psx-spx.consoledev.net/soundprocessingunitspu/ — primary source for
  the observable description (paraphrased, not transcribed, per the
  project DOCS-03 paraphrase discipline).
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md`
  § Post-Research Decisions (D-10).
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-RESEARCH.md`
  § Pitfall 1 (INT16_MIN-negation UB).
- Witness (behavioral, license-tagged, not read as primary source):
  DuckStation's 2019 reverb commit uses a comparable explicit-branch
  pattern. DuckStation re-licensed to CC-BY-NC-ND in September 2024;
  SPU-94 does not consume DuckStation source as a primary input per
  the PROJECT.md licensing posture.
- Prior ADR: ADR-0002 (vIIR anomaly — reproduce-target commitment).
- Prior ADR: ADR-0001 (Q15 multiply semantics — the saturation the
  pre-negation `result` has already been through).

---

## ADR-0009: Hard-clip stage placement

**Status:** Accepted (2026-04-19, Phase 3)

**Context:**

The reverb algorithm has a mix-bus saturation step on its input path —
CORE-02 in ROADMAP. The stage fits naturally between input-scale
(which emits an int32 widened product) and same-iir (which expects an
int16). Two factoring options were on the table: (a) fold the
saturation into input-scale's tail as an implicit `sat_s16`, or (b)
factor it out as its own named function between input-scale and
same-iir.

CORE-02 explicitly requires that the hard-clip be "independently
testable" — it is one of the five Phase 3 success criteria. The
factoring decision therefore turns on test architecture as much as
implementation aesthetics.

**Decision:**

Hard-clip is its own stage function (`spu94_reverb_hard_clip`) between
`spu94_reverb_input_scale` and `spu94_reverb_same_iir`. The function
accepts int32 L and R inputs, emits int16 L and R outputs plus an
overflow-magnitude int32 out-param, and has no dependency on
`spu94_state` (it is a pure function over its arguments).

```c
void spu94_reverb_hard_clip(int32_t Lin_wide, int32_t Rin_wide,
                            int16_t *Lin_out, int16_t *Rin_out,
                            int32_t *overflow_out);
```

The overflow-magnitude out-param is the sibling observable to the
per-stage err accumulators — see ADR-0011 for the rationale behind
the complete-precision-loss surface.

**Consequences:**

- CORE-02 "independently testable" is satisfied with zero extra test
  scaffolding: `tests/unit/reverb/test_reverb_hard_clip.c` drives the
  function directly with int32 boundary inputs and asserts `sat_s16`
  behavior bit-for-bit.
- The function has no `spu94_state` dependency, so tests do not need
  to set up a state fixture. This is the cheapest possible test
  environment.
- One additional function call per tick, negligible cost (the body is
  two `sat_s16` calls + an overflow-magnitude computation; compiler
  inlines trivially through LTO).

**Alternatives Considered:**

- **Fold into input-scale with implicit `sat_s16`.** Rejected: makes
  CORE-02 testing require a test fixture that can observe the post-
  scale-pre-IIR value, which is the same level of observability
  indirection the D-04 decision explicitly avoided for stage outputs.
  Independently-testable means testable without fixture indirection.
- **Fold into same-iir's input read.** Rejected for the same reason,
  plus it would mix the saturation step with the IIR arithmetic in a
  way that makes the Pitfall-1 + Pitfall-7 analysis harder.

**Seam (D-22):**

The function slot in `spu94_reverb_body`. The body-caller can be
re-routed through a different clip (or no clip) by swapping one call,
keeping every other stage untouched. This is the hook a future
Controllers milestone uses to expose alternative clipping shapes (soft
clip, asymmetric clip, bypass) without touching the stage functions
themselves.

**Revision Path:**

- Plugin-era user testing wants a soft-clip or bypass mode:
  Controllers layer adds a policy pointer or stage-function slot;
  additive ADR; ADR-0009 is not superseded.
- M5 hardware capture reveals that the clip has richer per-sample
  behavior than `sat_s16` (e.g., a soft knee, asymmetric roll-off):
  supersede ADR-0009 with the captured arithmetic; the function
  signature is stable so call sites do not change.

**Sources:**

- psx-spx.consoledev.net/soundprocessingunitspu/ — primary source for
  the mix-bus saturation behavior (paraphrased).
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md`
  § Post-Research Decisions (D-09).
- Internal: `.planning/REQUIREMENTS.md` CORE-02.
- Prior ADR: ADR-0001 (Q15 semantics — `sat_s16` is the underlying
  saturation primitive).
- Prior ADR: ADR-0011 (overflow-magnitude observable — the clip is
  the site of its emit).

---

## ADR-0008: L/R register-write timing within a 22.05 kHz tick

**Status:** Accepted (2026-04-19, Phase 3)

**Context:**

The reverb algorithm processes one stereo sample pair per 22.05 kHz
tick. Internally, the pair comprises an L half-step and an R
half-step. The nocash documentation is silent on whether coefficient
registers (the `v*` family in particular) are read once per pair or
re-read fresh for each half-step. Both readings are consistent with
the written documentation; behavioral witnesses SPU-94 consulted
(witness: Mednafen, DuckStation, lv2-psx-reverb — all license-tagged
as GPL and therefore not read as primary sources per PROJECT.md)
appear to freeze once per pair, but those witnesses are consulted as
observable behavior, not as source-read primary material.

SPU-94 must pick a defensible default that preserves bit-faithfulness
for the M1 milestone (plain stereo reverb on realistic preset material)
while leaving a seam for the M4 Controllers milestone to expose
half-step modulation if users want it.

**Decision:**

Freeze-once-per-pair. At the top of `spu94_reverb_body`, every v-
register that the body's stages consume is read into a const local
snapshot (`vLIN_snap`, `vRIN_snap`, `vLOUT_snap`, `vROUT_snap`,
`vIIR_snap`, `vWALL_snap`, `vCOMB1..4_snap`, `vAPF1_snap`,
`vAPF2_snap`). The snapshots are passed down to the stage functions
by value. Stages never re-read the v-registers from state. A
coefficient written mid-tick by the host lands in the register file
on write (v-registers are IMMEDIATE per ADR-0005), but its effect on
the reverb math is deferred until the next pair — both L and R halves
of the current pair observe the pre-write value.

**Consequences:**

- Atomic L/R behavior within a tick. The pair acts as a single
  indivisible processing unit from the arithmetic's perspective,
  matching the tick-atomicity principle ADR-0005 pinned for mid-tick
  writes.
- Bit-faithful default for M1 preset material. If real PS1 silicon
  freezes once per pair (the behavioral-witness reading supports
  this), SPU-94 matches; if it re-reads per half-step, the M4
  Controllers toggle (see Seam below) exposes the other shape.
- Test obligation: `test_reverb_body` re-reads the same snapshots
  when reproducing the stage-by-stage path. The equivalence assertion
  implicitly pins the snapshot-once discipline.
- Mid-tick v-register writes are visible via the `_pending` accessor
  from the time they are written until the next pair begins; they are
  not invisible, just deferred.

**Alternatives Considered:**

- **Re-read v-registers fresh for the R half-step.** Equally valid
  under the nocash silence. Rejected as default for M1: bit-
  faithfulness to the behavioral-witness consensus is the safer
  default when the primary source is silent, AND the semantics
  (pair-rate modulation vs half-step modulation) differ audibly at
  high modulation rates, so shipping both as alternate modes is the
  right long-term shape.

**Seam (D-22):**

Body-caller level. Stage functions take v-register values as
parameters; swapping "snapshot once" vs "re-read for R" is a change
localized to `spu94_reverb_body`. No stage function changes.

**M4 Controllers seed — Extended Modulation Mode:**

See the Deferred Ideas section of 03-CONTEXT.md. The M4 Controllers
milestone exposes re-read-fresh-for-R as an opt-in "Extended
Modulation Mode" toggle on top of the default freeze-once-per-pair
behavior. Use cases: audio-rate LFOs, fast envelopes, FM-style
parameter modulation, cross-rate tricks that pair-rate snapshots
cannot reach. Doubles the modulation temporal resolution from
22.05 kHz (pair rate) to 44.1 kHz (half-step rate) for users who
want that expressiveness, while M1's default preserves the bit-
faithful behavior for users who want that.

**Revision Path:**

- M5 hardware capture reveals the real chip re-reads per half-step:
  flip the default in a superseding ADR; the M4 toggle still exposes
  both modes.
- M5 reveals a third option (e.g., a specific register is re-read but
  others are not): add a per-register policy column in a new ADR.

**Sources:**

- psx-spx.consoledev.net/soundprocessingunitspu/ — primary source for
  the 22.05 kHz tick rate and the L-then-R processing order. Silent
  on per-half-step v-register timing, which is the absence-of-
  evidence this ADR addresses.
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md`
  § Post-Research Decisions (D-08).
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-RESEARCH.md`
  § Test Strategy and § Pitfall 4 (mid-tick re-read hazard).
- Prior ADR: ADR-0005 (IMMEDIATE vs TICK_LATCHED write policy — this
  ADR extends the IMMEDIATE policy's effective visibility).

---

## ADR-0007: comb-sum accumulation precision — cascading `sat_s16` after each add

**Status:** Accepted (2026-04-19, Phase 3)

**User lock date:** 2026-04-19 (supplemental post-research discuss pass)

**Context:**

The reverb's 4-tap comb sums four Q15 products per side per tick. The
comb-sum precision question (ROADMAP Phase 3 SC-5a) is whether the
intermediate accumulator behaves as a widening int32 value clamped
once at the end, or as an int16 saturator clamping after every add.
The nocash documentation describes the formula as an addition of four
terms but is silent on the intermediate comb-sum precision. Both
shapes give identical output whenever the sum fits in int16 range;
they diverge at operand combinations that force intermediate
saturation.

Two variants were on the table:

- **Variant A (int32 accumulate):** widen each product to int32, sum
  all four, call `sat_s16` once on the final sum. This is the
  mathematically cleanest form and the one the behavioral-witness
  consensus (witness: DuckStation and Mednafen-PSX, both GPL and not
  read as primary witness-sources) appears to use.
- **Variant B (cascading `sat_s16`):** treat the accumulator as int16
  and call `q15_add_sat` (which widens internally to int32 and then
  saturates) after each add. Three saturation points per side, six
  across L+R per tick.

The two variants produce markedly different sums under saturation:
Plan 03's distinguishing-test case (`v=(0x7FFF,0x7FFF,-0x7FFF,
-0x7FFF)` with all taps `0x7FFF`) evaluates to `-0x7FFF` under
Variant B and `-2` under Variant A, a 32765-point gap.

**Decision:**

Variant B — cascading `sat_s16` after each add. SPU-94's
`spu94_reverb_comb` implements three cascading `q15_add_sat` calls
per side (no `int32_t sum*` local). The grep guard
`! grep -E 'int32_t[[:space:]]+(sumL|sumR|sum_L|sum_R|comb_acc|acc32)' src/spu94/spu94_reverb.c`
is part of the Phase 3 SUMMARY.md acceptance and must continue to
pass across plans. The distinguishing test in
`tests/unit/reverb/test_reverb_comb.c` pins the chosen variant against
the rejected one at the 32765-point gap.

**Rationale (taste-driven, user-locked):**

The user's decision (Anthony, 2026-04-19) favored Variant B for two
reasons:

1. **Distortion character at input extremes.** Cascading saturation
   produces a richer clipping flavor when tap combinations push the
   accumulator past the int16 boundary. For musical material this
   manifests as a perceptual texture in the reverb tail on loud
   transients.
2. **Richer overflow signal feeding the D-11 err accumulator (see
   ADR-0011).** Each cascading saturation contributes precision-loss
   material to `err_comb`. Variant A produces at most one clamp event
   per side per tick; Variant B produces up to three. More clamp
   events mean more material for the future Controllers-era drive
   meter, soft-clip warmth lever, and overflow-modulated feedback
   features (see the Deferred Ideas seeds in 03-CONTEXT.md).

This ADR diverges from the behavioral-witness consensus deliberately.
The primary source (nocash) is silent on the question; the
behavioral witnesses are license-tagged as GPL and are not read as
primary sources per PROJECT.md; the decision therefore falls to
taste until M5 hardware capture provides primary evidence. The
divergence is documented for audit — the witness consensus is a
legitimate future revision trigger but not the current authority.

**Consequences:**

- Divergence from behavioral-witness consensus (witness: DuckStation,
  witness: Mednafen-PSX) on intermediate comb behavior per the witness
  survey in 03-RESEARCH.md. M4 plugin-era A/B testing will be the
  first musical judgment data point.
- The `err_comb` stream is richer under this variant than under
  Variant A. Controllers consumers (M4) that use `err_comb` as a
  musical modulation source get more material.
- Three saturating adds per side (`q15_add_sat` widens internally to
  int32 and calls `sat_s16`). Runtime cost: six total per tick across
  L+R. Negligible.

**Alternatives Considered:**

- **Variant A (int32 accumulate + single final `sat_s16`).** Rejected
  per the rationale above. Mathematically cleanest; matches the
  behavioral-witness consensus; preserves more precision on loud
  material. Kept as the revert-lever target if the revision triggers
  below fire.

**Seam (D-22):**

`spu94_reverb_comb` body. Variant A is a one-TU swap: replace the
three `q15_add_sat` calls with a widening int32 accumulator and a
single final `sat_s16`. The function signature is stable. Test
reference tables in `tests/python/derive_reverb_reference.py::ref_comb`
would need to re-derive; the distinguishing test would either flip
its expectation or be replaced by a documented witness-matching test.

**Revert lever:**

If M4 plugin-era user testing on realistic preset material finds the
cascading distortion too aggressive or unmusical, flip to Variant A.
The mechanism is a one-TU swap (see Seam above); the user lock stands
until plugin-era evidence contradicts it.

**Revision Path:**

- **M4 plugin-era user testing** finds the cascading distortion
  unmusical on realistic preset material: flip to Variant A with a
  superseding ADR.
- **M5 hardware capture** provides primary evidence for whichever
  variant real PS1 silicon implements. Hardware capture is the
  ultimate authority and overrides both this ADR and the revert
  lever.

**Sources:**

- psx-spx.consoledev.net/soundprocessingunitspu/ — primary source for
  the comb formula. Silent on intermediate precision. Paraphrased.
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md`
  § Post-Research Decisions (D-07 — user lock rationale).
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-RESEARCH.md`
  § Comb-sum precision — full evidence table.
- Internal: `.planning/phases/03-core-reverb-algorithm-hard-clip/03-DISCUSSION-LOG.md`
  — plain-language user decision record for the 2026-04-19 lock.
- Sources — Witness (behavioral, license-tagged, not read as primary
  source): witness DuckStation and witness Mednafen-PSX source code
  appears to use Variant A per third-party reports; SPU-94 does not
  read those GPL-licensed sources per PROJECT.md licensing posture.
- Prior ADR: ADR-0001 (Q15 multiply semantics — `sat_s16` is the
  saturation primitive each cascading step uses).
- Prior ADR: ADR-0011 (per-multiply err-tap + overflow-magnitude
  observable — this ADR's user-lock rationale cites that surface
  directly).

---

## ADR-0006: mBASE write side effect — snap-on-write

**Status:** Accepted (2026-04-19, Phase 2)

**Context:**

Phase 2's discussion identified the mBASE-write side-effect question as a
gray area worth resolving with primary-source research before locking a
behavior. The preliminary CONTEXT.md lean (D-09) was "floor-only — writing
mBASE updates the wrap floor but does not reset BufferAddress." The
research step (see `02-RESEARCH.md` § "mBASE Side-Effect Evidence")
contradicted the preliminary lean: the nocash psx-spx SPU documentation
states in plain language that an mBASE write additionally sets the current
buffer address to the written value. The wrap formula itself (see this
ADR's Decision section) uses `max(mBASE, ...)` so mBASE already acts as a
floor on every subsequent tick — the write-time snap is an additional,
stateful side effect that the formula alone does not produce.

Secondary and tertiary sources (hitmen c02 SPU doc, jsgroth PS1 SPU Part 3
writeup, findable PSX homebrew) are silent on the mid-stream mBASE-write
case, consistent with the Sony BIOS reverb-setup procedure that disables
reverb before changing registers. GPL witnesses (Mednafen, lv2-psx-reverb,
DuckStation) were not read per PROJECT.md licensing posture; their output
audio remains a candidate witness for Milestone 5 hardware-comparison
work, but their source code is not a primary input to this resolution.

**Decision:**

Writing `mBASE` snaps the reverb work-buffer pointer:

```
On any write to mBASE with value N:
    state->reg_values[SPU94_REG_mBASE] = (int16_t)N      (engine layer)
    state->buffer_address = (uint32_t)N                  (this ADR)
```

No implicit work-buffer clear. No crossfade. No tick-alignment delay. The
jump can produce an audible discontinuity in the reverb tail; that is
hardware-accurate behavior and SPU-94's default.

BufferAddress advance (once per stereo tick, from `spu94_buffer_advance`
called inside `spu94_tick` after `spu94_apply_pending_writes`):

```
buffer_address = MAX(mBASE, (buffer_address + 2) AND 0x7FFFE)
```

(Mathematical `MAX` — the implementation in `spu94_buffer.c` uses an
inline ternary, not a `max()` macro, to keep the operation visible at
the call site.)

- Byte addressing. The `+2` advances by one 16-bit halfword per stereo
  tick.
- `0x7FFFE` masks to the 512 KB-2 SPU RAM region with halfword alignment
  (bit 0 always clear after an advance).
- The `max(mBASE, ...)` clause floors the advance to mBASE even without
  the write-time snap; the snap is for the mid-stream-write case
  specifically.

Implemented in `src/spu94/spu94_buffer.c`:
- `spu94_buffer_advance(state)` — the wrap formula.
- `spu94_mbase_on_write(state, new_mbase)` — the snap side effect.
- `spu94_get_buffer_address(state)` — read-only observability accessor
  (D-23) on the running address.

`spu94_mbase_on_write` is the D-11 seam. SPU-94 keeps it internal (not
runtime-swappable) in Phase 2. The future Controllers milestone may
re-point it at alternative behaviors (floor-only, crossfade-on-write,
clear-and-snap) by re-linking an alternative translation unit, or by
promoting it to a runtime function pointer via an additive ADR.

**Bit-faithfulness note (T-02-18):** The snap passes the written `u16`
value through verbatim. An odd mBASE produces an odd `buffer_address`
for exactly one step, until the next `spu94_buffer_advance` clears bit 0
through the `AND 0x7FFFE` mask. The primary source is silent on bit-0
masking at write time; SPU-94 defaults to verbatim pass-through and
documents this exception in the threat register and in Plan 05's
T-02-28 fuzz invariant. The snap MUST NOT be patched to add `& ~1u` —
that would diverge from the bit-faithful interpretation.

**Consequences:**

- **Bit-faithful to primary source.** SPU-94's mBASE behavior matches the
  plain-language nocash statement. No invented side effect (buffer clear,
  crossfade, tick-alignment) that the spec does not describe.

- **D-09 revised.** The preliminary D-09 "floor-only" lean is superseded
  by this ADR. The D-11 seam structure is preserved so a future reversal
  is cheap if hardware-witness evidence (Milestone 5) contradicts the
  plain-language reading.

- **Audible discontinuity accepted.** Because the snap is instantaneous,
  writing mBASE during active reverb jumps the reverb's work-buffer
  read pointer, which manifests as a click or phase jump in the audio
  tail. This is accepted as hardware-accurate. Controllers may later
  add a smoothing layer for musical use cases — but that is the
  Controllers layer, not core SPU-94.

- **Test obligations:**
  - Plan 04 ships `tests/unit/buffer/test_buffer_basic.c` with eleven
    Unity cases covering null-safety, init/reset zeroing, single-tick
    advance, 100-tick cumulative advance, the wrap corner at the top of
    the address window, the mBASE-floor case, the snap-on-write
    immediate effect, the odd-mBASE pass-through, the work-buffer
    untouched invariant, and the tick-order observability check
    (apply_pending_writes runs before buffer_advance).
  - Plan 05 will add `tests/unit/buffer/test_buffer_wrap.c` for the
    formula corners at finer resolution and `tests/unit/buffer/test_buffer_mbase.c`
    with a full sentinel-pattern check that the snap leaves work_buf
    bytewise unchanged.
  - Plan 05 will add the Python ctypes fuzz harness
    (`tests/python/fuzz_buffer.py`) running 10^6 random operations,
    asserting after each step that
    `buffer_address >= mBASE && buffer_address <= 0x7FFFE` and that
    `(buffer_address & 1) == 0` holds *unless* the most recent op was
    a snap with an odd value (the T-02-28 exception).

- **Revision paths:**
  - Milestone 5 hardware witness contradicts "snap exactly on write" —
    a new ADR supersedes with the observed behavior; the D-11 seam
    makes the change a one-file edit.
  - Controllers needs a runtime-swappable handler — additive ADR that
    promotes the internal handler to a function-pointer slot in
    `spu94_state`. No break to existing callers.
  - Hardware witness shows bit 0 IS masked at snap time — flip the
    snap body to `state->buffer_address = (uint32_t)new_mbase & ~1u;`
    in a new ADR; the T-02-28 invariant relaxes accordingly.

**Sources:**

- External (paraphrased, cite-only): nocash psx-spx, "Reverb Volume and
  Address Registers (R/W)" subsection, which in plain language describes
  that an mBASE write additionally sets the current buffer address to
  the written value. URL:
  https://psx-spx.consoledev.net/soundprocessingunitspu/
  (extracted via WebFetch on 2026-04-19; paraphrased here per
  PROJECT.md licensing posture — no prose or tables transcribed).
- External (paraphrased): nocash psx-spx, "SPU Reverb Formula" section,
  which defines the `max(mBASE, (addr+2) AND 0x7FFFE)` wrap formula.
  Same URL. Used verbatim for the arithmetic (uncopyrightable facts,
  not prose).
- External (absence-of-evidence): hitmen c02 SPU documentation, jsgroth
  PS1 SPU Part 3 blog series — neither documents a different mBASE
  side effect; no contradiction.
- Internal: `.planning/phases/02-buffer-register-infrastructure/02-RESEARCH.md`
  § "mBASE Side-Effect Evidence" — full evidence table, secondary-
  source survey, and contradiction-with-D-09 flag.
- Internal: `src/spu94/spu94_buffer.c` — the implementation this ADR
  documents.
- Prior ADR: ADR-0005 (write-timing policy table) — mBASE's IMMEDIATE
  policy is established there; the snap side effect documented here
  fires AFTER the register-value update. ADR-0005's text references
  the Plan-03 location of the handler in `spu94_write_policy.c`; the
  handler was lifted to `spu94_buffer.c` in Plan 04 (ODR preserved).

---

## ADR-0005: Per-register mid-stream write-timing policy — split policy with swappable table

**Status:** Accepted (2026-04-19, Phase 2)

**Context:**

The nocash psx-spx documentation is silent on what happens when an SPU
reverb register is written mid-tick — i.e., during the 22.05 kHz stereo
tick in which the reverb network is computing. The Sony BIOS procedure for
setting up a new reverb effect is to disable reverb, write every register,
then re-enable, which strongly implies that mid-stream writes are not a
spec'd operation. SPU-94 nonetheless has to pick a defensible behavior
because (a) PROJECT.md treats real-time modulation of every register as a
first-class use case (reverb-as-living-instrument), and (b) the algorithm
must not crash, glitch, or corrupt memory on a mid-stream write.

The 35 reverb-affecting registers fall into two structural families:

1. **Gain-type registers** (`v*`-prefix: vLOUT, vROUT, vIIR, vCOMB1..4,
   vWALL, vAPF1, vAPF2, vLIN, vRIN — 12 total) participate in per-sample
   multiplies. A faithful model of the multiplier reads each register at
   the multiply site; a mid-tick write is naturally visible to the next
   multiply that reads it.

2. **Address/delay-type registers** (`d*` / `m*`-prefix — 22 total) index
   into the reverb work buffer. The reverb algorithm's correctness depends
   on a consistent pair of L and R addresses across a stereo tick. A
   mid-tick change here would corrupt the L/R address relationship and
   produce phase/buffer artifacts that have nothing to do with the
   musical intent.

`mBASE` is a third case: the register itself is `u16` and structurally
belongs with the address family, but its update timing is IMMEDIATE so the
config value is observable instantly. Its stateful side effect — snapping
the running BufferAddress — is resolved separately by ADR-0006.

**Decision:**

SPU-94 ships a split mid-stream write policy:

- **IMMEDIATE** for all 12 `v*` gain registers AND for `mBASE`.
  Writes become visible to the next register read (and to the next
  multiply, for `v*`) within the same tick. For `mBASE` the additional
  side effect — `state->buffer_address := mBASE` — is invoked through
  the `spu94_mbase_on_write` handler defined in
  `src/spu94/spu94_write_policy.c`.

- **TICK_LATCHED** for the remaining 22 `d*` / `m*` address/delay
  registers. Writes stage into a shadow slot
  (`state->pending_values[reg]`, with the corresponding bit set in
  `state->pending_mask`) and are applied atomically at the start of
  the next `spu94_tick()` call, before any buffer-address advance or
  reverb computation.

The policy is implemented as a `static const spu94_write_policy_t`
35-entry array keyed by `spu94_reg_t`, defined in
`src/spu94/spu94_write_policy.c`. **This array IS the swappable seam
(D-05).** The future SPU-94 Controllers milestone (D-22, D-24) re-points
the table at an alternative policy by linking its own translation unit
that defines `spu94_write_policy_table` differently — without touching
core engine code. SPU-94 itself ships the table pinned to the
PS1-faithful split above.

The pending-value shadow is observable to callers via
`spu94_get_reg_i16_pending` and `spu94_get_reg_u16_pending`, which
return what will be applied at the next tick. For IMMEDIATE-policy
registers the pending and active readings always match — the engine
mirrors IMMEDIATE writes into the pending slot specifically so that
callers polling the `_pending` accessor never see a stale value.

**Consequences:**

- **Assumption flagged.** The exact per-register assignment (every `v*`
  IMMEDIATE, every `d*` / `m*` TICK_LATCHED) is structurally defensible
  but not spec-backed. It could be revised if hardware-witness evidence
  in Phase 7 contradicts a specific entry. The seam exists precisely so
  that revision is a one-line edit, not an architectural change.

- **Test obligation.** Plan 05 will add per-register policy tests under
  `tests/unit/registers/test_register_policy.c` that exercise:
  - For every `v*` register: write -> `get == get_pending == value`.
  - For every `d*` / `m*` register: write -> `get != get_pending`
    immediately, then `spu94_tick()` -> `get == get_pending == value`.
  - For `mBASE` specifically: the IMMEDIATE update of the config value
    (the snap side effect itself is in ADR-0006's scope, exercised in
    `tests/unit/buffer/test_buffer_mbase.c` per Plan 04).

- **Pitfall 4 protection.** `spu94_apply_pending_writes()` is called
  from EXACTLY one location — the first line of `spu94_tick()`. Any
  future change that invokes it from a second site violates the
  contract. A grep-based CI guard could be added if drift becomes a
  concern; the call-site-uniqueness is currently enforced by code
  review.

- **Revision paths.**
  - **Hardware witness (Milestone 5):** behavioral capture from an
    original PS1 may show that some `v*` registers are actually
    TICK_LATCHED on the real chip (e.g., if the multiplier reads at
    tick start rather than per-sample). Resolution is a new ADR
    superseding the relevant rows of this one.
  - **Controllers milestone:** may want to add a third policy
    (CROSSFADE — for zipper-free real-time gain modulation) by adding
    a new `spu94_write_policy_t` enum value, new rows in the table,
    and an additional case in the engine setter switch. This is an
    additive ADR; the IMMEDIATE/TICK_LATCHED entries above remain
    pinned for SPU-94's own consumers.
  - **Per-tick observation:** if Phase 3 reveals that the reverb
    algorithm needs a "did this tick flush any pending writes?"
    signal for diagnostics, the apply function can return the mask
    that was flushed. Pure-additive change; existing callers ignore
    the return value.

**Sources:**

- Internal: `.planning/phases/02-buffer-register-infrastructure/02-RESEARCH.md`
  § "Write-Timing Policy" and § "Per-Register Policy Table" — the research
  notes that built the structural-family argument and the L/R consistency
  requirement.
- External (paraphrased, not transcribed): nocash psx-spx, SPU reverb
  section. https://psx-spx.consoledev.net/soundprocessingunitspu/ —
  facts used: the 22.05 kHz internal tick rate with L/R half-cycle
  alternation; the BIOS "disable -> rewrite -> enable" reverb-setup
  convention.
- Prior ADR: ADR-0004 (extensibility taps) — same swappable-seam
  architectural principle (D-22, D-23, D-24) applied to a different
  piece of the API.

---

## ADR-0004: Extensibility taps — `q15_mul_truncate_with_err` and `spu94_tick`

**Status:** Accepted (2026-04-19, Phase 2)

**Context:**

Phase 2 lands two intentional API-surface commitments that the phase-context
discussion designated as "extensibility taps": `q15_mul_truncate_with_err` in
the Q15 fixed-point surface, and the public `spu94_tick` per-stereo-tick
processing entry point. Both exist to serve future consumers — specifically, a
planned SPU-94 Controllers milestone (an exploration/modulation layer that
consumes SPU-94's public API as-is) and a separate Error Accumulator project
(a performance-oriented audio effect that reuses the Q15 primitives) — without
forcing those consumers to re-implement anything in SPU-94's bit-faithful core.

Neither tap is load-bearing for the reverb algorithm itself. Both are
observability and composition hooks. Landing them in Phase 2 avoids the cost
of retrofitting them later, when consumers exist and their API expectations
have become sticky. This ADR records them so they read as deliberate seams
rather than accidental public surface.

**Decision:**

SPU-94 exposes two extensibility taps as public, stable API:

1. `q15_mul_truncate_with_err(int16_t a, int16_t b, int16_t *err_out)` — the
   Q15 multiply primitive whose math is pinned by ADR-0001. Additionally
   writes the truncation remainder (the bits discarded by the `>>15` shift)
   via `err_out` when non-NULL. Passing `NULL` is permitted and makes the
   function behaviorally identical to `q15_mul_truncate`, which is now a thin
   wrapper. The remainder is *pre-saturation*: for the `INT16_MIN * INT16_MIN`
   edge case, the saturated result is `INT16_MAX` (per ADR-0001) while the
   reported remainder is zero (the product `+2^30` is exactly divisible by
   `2^15`). Callers that need the additional saturation discard can infer it
   from the difference between the pre-saturation shifted value and
   `INT16_MAX`.

2. `spu94_tick(spu94_state *state)` — the per-22.05 kHz-stereo-tick processing
   entry point. In Phase 2 it is a no-op stub; Phase 3 implements the reverb
   algorithm inside it; Phase 5's `spu94_process` wraps it as a loop.
   Observers (Controllers, Error Accumulator telemetry, test harnesses)
   interleave reads of public accessors between ticks with the guarantee that
   the observed state is instantaneous and consistent. The function is
   null-safe: `spu94_tick(NULL)` is a no-op.

**Consequences:**

- *Tradeoff accepted:* Two functions in the public API surface that the core
  reverb algorithm does not strictly need. Offset: both are committed by
  CONTEXT.md decisions D-18 and D-19, so the marginal cost is essentially
  zero — we were going to add them anyway, and adding them later would cost
  more because downstream consumers would have adapted to their absence.

- *Bit-faithfulness preserved:* Neither tap alters the reverb data path. The
  `err_out` parameter is a side channel (a write-only observation hook); the
  `spu94_tick` body in Phase 2 is empty and will be filled in Phase 3 with
  the reverb algorithm, whose correctness is orthogonal to the existence of
  the function name. ADR-0001's Q15 semantics are preserved bit-exactly:
  `q15_mul_truncate` is now a wrapper that passes `err_out = NULL`; the
  Phase 1 Q15 reference table continues to pass unchanged.

- *Observer ergonomics:* External projects (Error Accumulator consuming
  `_with_err`; Controllers milestone consuming the tick-boundary observer
  contract) now have a stable target. If either consumer discovers a need
  not covered here, the response is either "add a new seam" (cheap, additive
  ADR) or "reshape this one" (requires a new ADR superseding this one).

- *Test obligations:* Phase 1's Q15 test table must continue to pass against
  the refactored `q15_mul_truncate` — verified in Plan 02 Task 2 alongside a
  new remainder-verification table for `q15_mul_truncate_with_err` and two
  `spu94_tick` null-safety tests. Plan 05's per-register battery will
  re-exercise the remainder observation in the larger integration context.

- *Revision paths:*
  - If a future use case shows the remainder signedness convention is wrong
    for the Error Accumulator's needs, a new ADR supersedes this one and
    every `_with_err` call site is revisited.
  - If `spu94_tick` ever needs to change signature (e.g., returning a status
    code), a new ADR records the break and the migration story.
  - The `err_out` parameter type (`int16_t *`) may need widening to
    `int32_t *` if future callers need the full pre-shift product; that is
    an additive ADR (a second accessor), not a break.

**Sources:**

- Internal: `.planning/notes/2026-04-19-error-accumulator-concept.md` —
  algorithm + hardware brief motivating the per-multiply remainder tap.
- Internal: `.planning/notes/2026-04-19-spu94-controllers-seed.md` — future
  exploration-layer milestone; drives the tick-boundary observer contract.
- Internal: `.planning/phases/02-buffer-register-infrastructure/02-CONTEXT.md`
  § D-18, D-19, D-22, D-23, D-24 — discussion-time locked decisions
  (extensibility taps, seams principle, observability principle, Controllers
  as future consumer).
- Prior ADR: ADR-0001 (Q15 multiply semantics) — `_with_err` preserves the
  ASR + saturation semantics pinned there.

---

## ADR-0001: Q15 multiply semantics — truncation direction and INT16_MIN² edge case

**Status:** Accepted (2026-04-18, Phase 1)

**Context:**

The PS1 SPU reverb algorithm applies Q15 fixed-point multiplication throughout: every
gain-type register (vWALL, vIIR, vCOMB1..4, vAPF1..2, vLOUT, vROUT, and the other
volume coefficients that affect the reverb path) participates in a `sample * register`
multiplication whose intermediate must be right-shifted by 15 bits to re-scale into the
int16 sample range before being written back to the work buffer or the output mix.

Two ambiguities exist:

1. The rounding direction for negative intermediate products. In C17 (§6.5.7/5) and
   C23, the expression `E1 >> E2` when `E1` is a negative signed integer is
   implementation-defined. On two's-complement hardware (now mandated by C23 via WG14
   N2412), every mainstream compiler — gcc, clang, arm-none-eabi-gcc, MSVC — emits
   arithmetic shift right (ASR), which rounds toward negative infinity. The alternative
   rounding direction in C is integer division (`/`), which rounds toward zero. These
   are different operations on negative results: `-1 >> 15` is `-1` under ASR but `0`
   under C division.

2. The INT16_MIN × INT16_MIN edge case. The mathematically-correct product
   `(-32768) × (-32768) = +2^30`. Right-shifted by 15 bits, that is `+2^15 = +32768`,
   which does not fit in the int16 range `[-32768, +32767]`. The naive cast to int16
   aliases to `-32768`, which has the wrong sign.

nocash's SPU documentation describes the reverb multiply-shift chain and states the
result is saturated to the int16 range, but does not explicitly specify the rounding
direction for negative products. Behavioral witnesses (public emulators, jsgroth.dev
PS1 SPU series — consulted output-only, not source-read, per PROJECT.md licensing
posture) consistently reflect ASR semantics. DSP hardware convention in commercial
fixed-function multiplier units is ASR.

**Decision:**

SPU-94's Q15 multiply helper is:

```c
static inline int16_t q15_mul_truncate(int16_t a, int16_t b) {
    int32_t product = (int32_t)a * (int32_t)b;
    int32_t shifted = product >> 15; /* ASR, verified by _Static_assert */
    return sat_s16(shifted);
}
```

- **Rounding direction:** arithmetic shift right (toward negative infinity).
- **INT16_MIN × INT16_MIN:** saturate to `INT16_MAX` via `sat_s16`.
- **Compiler assumption:** the target compiler emits ASR for signed negative shifts.
  Enforced at compile time by a `_Static_assert((-1 >> 1) == -1, ...)` in
  `include/spu94/spu94_q15.h`.

The function is `static inline`, header-only, and lives in the PUBLIC API at
`include/spu94/spu94_q15.h` (CONTEXT.md D-05, D-06, D-08).

**Consequences:**

- *Easier:* All target compilers (gcc 11+, clang 14+, arm-none-eabi-gcc) agree;
  zero runtime cost; idiomatic portable code.
- *Harder:* A future target compiler that emits logical-shift-right for signed
  negative values would fail the `_Static_assert` at compile time. The port is
  blocked until either a compiler swap or an explicit branchless ASR helper is
  introduced (not worth the complexity until that day).
- *Test obligation:* `tests/unit/q15/test_q15.c` asserts the ASR-vs-division
  distinguisher (`-1 * 1` returning `-1` under ASR, not `0` under division)
  and the INT16_MIN² saturation case as explicit table entries.
- *Future revision path:* If Milestone 5 hardware capture reveals the real SPU
  rounds toward zero, this ADR is superseded by a new ADR reopening the choice
  and every Q15 multiply site is revisited. The likelihood is low (industry DSP
  convention + emulator witness consensus favor ASR), but the revision path is
  acknowledged.

**Sources:**

- ISO/IEC 9899:2018 (C17) §6.5.7/5 — implementation-defined signed right shift.
- ISO/IEC 9899:2023 (C23) via WG14 N2412 — two's complement mandated; shift
  semantics unchanged.
- `BIB-001` (future bibliography entry) — nocash PSX SPU documentation,
  reverb formula section. Facts paraphrased; prose is SPU-94's own.
- `BIB-002` (future) — jsgroth.dev PS1 SPU series (behavioral witness).
- Internal: `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-RESEARCH.md`
  §Q15 Semantics Deep Dive, and RESEARCH.md §Common Pitfalls — Pitfall 1 + Pitfall 2.

---

## ADR-0002: vIIR = -0x8000 anomaly — reproduce faithfully

**Status:** Accepted (2026-04-18, Phase 1) — implementation deferred to Phase 3.

**Context:**

The nocash SPU documentation describes a quirk in the reverb vIIR coefficient: the
register is nominally expected in the range `-0x7FFF..+0x7FFF`, but when a caller
writes exactly `-0x8000` the final computed reverb value at that stage is negated
rather than simply clamped. nocash explicitly describes this as a documented quirk
and NOT a simple overflow bug, and notes the effect also touches the `+[mLSAME-2]`
addition term that normally should not be perturbed. Similar negation effects may
occur on other volume registers when written with exactly `-0x8000`.

The question: does SPU-94 reproduce this anomaly, or treat `-0x8000` as either
clamped to `-0x7FFF` or as an input error?

**Decision:**

Reproduce the anomaly faithfully. When the vIIR coefficient register holds exactly
`-0x8000`, the final computed value at the vIIR application site is negated. This
matches the documented hardware behavior and preserves bit-faithfulness for any
preset or modulation sequence that historically exercised this code path.

Implementation lands in **Phase 3** (at the register-application site inside the
reverb tick), **not in `q15_mul_truncate` itself**. `q15_mul_truncate` remains a
clean generic Q15 multiply; the vIIR anomaly is register-specific and is applied
as a post-step at the site where vIIR is consumed.

Phase 3 is also responsible for enumerating which *other* volume registers (if any)
exhibit the same `-0x8000` negation behavior per the nocash note on "similar
effects." That enumeration is a Phase 3 task and may update this ADR with a
`Follow-up (Phase 3)` sub-entry.

**Consequences:**

- *Easier:* Golden-file regression tests (Phase 7, TEST-04) and witness-diff
  comparisons against hardware captures (Milestone 5) align with any preset or
  test input that writes `-0x8000` to vIIR.
- *Harder:* Every register subject to the negation behavior must carry the
  anomaly logic; increases test surface. Phase 3 TEST-06 will assert the
  anomaly fires under `vIIR = -0x8000` and does NOT fire under `vIIR = -0x7FFF`
  against a hand-derived reference.
- *Semantics note:* The anomaly is not a general signed-overflow pattern. It is
  a named hardware quirk. Do NOT attempt to generalize it via a UBSan `no_sanitize`
  annotation on `q15_mul_truncate` — the multiply itself is well-defined; only
  the vIIR register application site applies the negation.

**Sources:**

- `BIB-001` (future) — nocash PSX SPU documentation, SPU Reverb Formula section,
  vIIR quirk description. Facts paraphrased; prose is SPU-94's own.
- Internal: `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-RESEARCH.md`
  §Q15 Semantics Deep Dive — The vIIR = −0x8000 Anomaly (ADR-0002 resolution).

---

## ADR-0003: UBSan `no_sanitize` policy — surgical, function-scoped, enumerated

**Status:** Accepted (2026-04-18, Phase 1) — first use deferred to Phase 3.

**Context:**

CI runs a UBSan build (`-fsanitize=undefined -fno-sanitize-recover=undefined`) to
catch undefined behavior in the core library. The PS1 SPU reverb algorithm relies
on documented hardware behaviors (saturation, specific overflow semantics at the
mix-bus hard clip, ADR-0002's vIIR negation site) that UBSan would otherwise flag
as `signed-integer-overflow` errors. Without a policy, the temptation is to disable
UBSan globally or to annotate broad swaths of code.

The question: under what narrow conditions may a function be exempted from UBSan's
integer checks, and how is the exemption recorded?

**Decision:**

1. **Surgical annotation only.** Individual functions that model documented SPU
   hardware wraparound or saturation may be annotated with
   `SPU94_NO_SANITIZE_INTEGER` (defined below). File-level or project-level
   disables are prohibited.

2. **Enumerated registry.** Every annotated function gains a row in the table
   below with columns *Function*, *File*, *ADR-introducing-it*, *Rationale*.
   A function with `SPU94_NO_SANITIZE_INTEGER` but no matching row in this table
   is a compliance failure. (CI enforcement of this registry check is a possible
   future enhancement; Phase 1 documents the discipline.)

3. **Phase 1 has zero entries.** No core-library function in Plan 01 wraps,
   saturates intentionally, or has any reason for annotation. The UBSan job
   passes clean on the empty-reverb-core baseline.

4. **Macro definition** (copy into `include/spu94/spu94_internal.h` or an
   equivalent project header when Phase 3 needs it):

    ```c
    #if defined(__clang__)
    #  define SPU94_NO_SANITIZE_INTEGER __attribute__((no_sanitize("integer")))
    #elif defined(__GNUC__) && __GNUC__ >= 8
    #  define SPU94_NO_SANITIZE_INTEGER __attribute__((no_sanitize_undefined))
       /* GCC's no_sanitize("integer") exists but is less granular; the
          _undefined flavor is the pragmatic cross-compiler fallback.
          See ADR-0003 Consequences for the audit approach. */
    #else
    #  define SPU94_NO_SANITIZE_INTEGER /* empty */
    #endif
    ```

**Annotated Functions registry** (Phase 1: empty; later phases append rows):

| Function | File | ADR | Rationale |
|----------|------|-----|-----------|
| *(none in Phase 1)* | — | — | — |

**Consequences:**

- *Easier:* Intentional SPU wraparound / saturation has a pre-authorized path;
  no ad-hoc discussion when Phase 3 adds the mix-bus hard clip (CORE-02) or the
  vIIR negation (ADR-0002 implementation).
- *Harder:* Every new annotated function forces a row-add in this ADR's registry
  table. Forgetting the row is a discipline failure; future CI enhancement may
  check programmatically by grepping for the macro and matching against the table.
- *Portability note:* Under GCC the annotation uses `no_sanitize_undefined`
  which is broader than Clang's `no_sanitize("integer")`. The audit obligation
  (row in the registry + explicit rationale) compensates for the broader scope.
- *Revision trigger:* If a Phase 3+ author wants to disable sanitization at
  file level or for a non-SPU-hardware reason, the path is "new ADR that
  updates ADR-0003," not an in-place edit.

**Sources:**

- Clang UBSan reference — group name `integer` covers signed-overflow,
  unsigned-overflow, shift, integer-divide-by-zero, implicit-truncation,
  and sign-change (`BIB-003` future).
- GCC documentation on `no_sanitize_undefined` attribute (`BIB-004` future).
- Internal: `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-RESEARCH.md`
  §CI Wiring Details — UBSan CI job; §Architecture Patterns — Pattern 4.
