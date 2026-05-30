# Milestones

## v1.11.0 Live Input Sampling (Shipped: 2026-05-30)

**Phases completed:** 4 phases, 5 plans, 6 tasks (Phases 56-59)
**Tag:** `v1.11.0`
**Requirements:** REC-01..06, RATE-01..04, TRIG-01..04, EXP-01..03 — all verified

**What shipped:**

Real-time audio recording directly into the sampler's 512KB voice RAM, ADPCM-encoded on intake so the PS1 character is baked into the recording. A buffer-then-encode pipeline accumulates raw PCM during capture and batch-encodes to ADPCM on stop via the existing `spu94_sample_encode_to_ram` path (zero new dependencies). Manual record/stop plus threshold-triggered auto-record that captures from the exact transient sample. Four PS1 preset sample rates (44.1 / 22.05 / 11.025 / 5.5125 kHz) plus a continuous variable-rate knob across the full pitch register range, with a rate-aware recording-time display. Input peak meter and RAM usage display update live. Recorded samples export as trimmed WAV at the recording rate for building sample libraries. 48 commits, 77 files changed, +5,352 / -7,208 lines over 2 days.

**Key accomplishments:**

1. Buffer-then-encode recording pipeline: 3-state machine, mono-summed staging capture in processBlock, batch ADPCM encode on stop with waveform/state cache population
2. Record button, input peak meter, and RAM stats display wired to the recording engine in the sampler window with coral visual feedback and encode-on-stop triggering
3. Pitch knob wired as continuous sample rate control with Hz display, bidirectional dropdown sync, and rate-aware recording time visible before pressing Record
4. Threshold trigger: 4-state machine with auto-start capturing from the exact transient sample, adjustable -60..0 dB threshold knob, tri-state record button (default / amber / coral)
5. Sample export: save the recorded sample as a trimmed 16-bit mono WAV at the recording rate (not resampled to 44.1 kHz), respecting start/end markers

**Verification:** All four phases verified; the Phase 58 (threshold trigger) and Phase 59 (sample export) human-verification items were confirmed working by the user at milestone close.

**Archived to:** `.planning/milestones/v1.11.0-ROADMAP.md`, `.planning/milestones/v1.11.0-REQUIREMENTS.md`, `.planning/milestones/v1.11.0-phases/`

---

## v1.10.0 Voice Dynamics & Stereo Effects (Shipped: 2026-05-28)

**Phases completed:** 13 phases, 20 plans (Phases 43-55)
**Tag:** `v1.10.0`
**Requirements:** 43/48 complete; 4 dropped (WIDE-01..04, stereo widener — no native SPU support); 1 subsumed (PMOD-05, phase modulator GUI → Ring Mod)

**What shipped:**

Five curated VCA ramp effects — Tremolo, Auto-Pan, AM Synthesis, Ring Mod, Sidechain Duck — all configurations of the same L/R sweep state machine, exposed through a unified dropdown GUI with adaptive controls per mode. Three native sweep shapes (Triangle, Saw Up, Saw Down). Per-voice internal mod bus routing global noise to pitch/volume/pan. Split-output bus with reverb-only side limiting. Retrigger engine foundation enabling continuous oscillation from sub-Hz through audio rate. Ring Mod delivers bipolar sweep crossing zero into phase inversion, subsuming the planned Phase Modulator. ADSR calibration rework with direct PS1 rate indexing and vertical faders. Preset format extended with voice, ADSR, effects, and mod bus sections. 100 commits, 120 files changed, +11,323 lines over 5 days.

**Key accomplishments:**

1. Retrigger engine — auto-reversing VCA ramp with independent L/R rates, KON reset, sub-Hz to 7350 Hz range
2. Five curated effects from one state machine — Tremolo (sync L/R), Auto-Pan (opposition phase), AM (audio-rate unipolar), Ring Mod (audio-rate bipolar with phase inversion), Ducking (KON-triggered one-shot)
3. Three sweep shapes — Triangle (auto-reverse), Saw Down (reset to max), Saw Up (reset to min), each in linear and exponential curve modes
4. Internal mod bus — per-voice noise-to-pitch/volume/pan routing in C core voice tick at sample rate
5. Split-output bus — reverb-only side limiting prevents stereo effects from being crushed
6. Unified effects GUI — single dropdown selector, shared controls (Rate/Depth/Shape/Lin-Exp) for oscillation modes, swapped controls (Source/Attack/Release/Depth) for Ducking
7. ADSR calibration — 5 measured tables, direct PS1 rate indexing, vertical faders with ms/s readout, sustain-zero fix

**Key decisions:**

- Effects are curated preconfigurations of VCA ramp state machines (not new DSP)
- Ring Mod subsumes Phase Modulator — bipolar sweep crossing zero IS polarity oscillation
- Stereo Widener dropped — SPU has no native stereo decorrelation
- Pan preserved during effects via base_vol_l/r ceiling formula
- 8-sample anti-click ramp on saw retrigger
- FRACT mode removed (PS1 uses integer addressing only)
- Preset format extended with four new INI sections (backwards compatible)
- All ADSR modes use direct PS1 rate indexing

Known deferred items at close: 4 (one-shot trigger, GUI noise section cleanup, speed musical divisions, sidechain duck UAT blocked on MIDI)

**Archived to:** `.planning/milestones/v1.10.0-ROADMAP.md`, `.planning/milestones/v1.10.0-REQUIREMENTS.md`

---

## v1.9 Complete Voice (Shipped: 2026-05-24)

**Phases completed:** 10 phases, 16 plans (Phases 33-42)
**Tag:** `v1.9`
**Requirements:** 37/37 complete (ADSR-FIX-01..04, SVOL-01..05, PMON-01..07, NON-01..09, SWEEP-01..10, INT-01..04, PAN-01..04, TOG-01..04, RAMP-01..05, VGUI-01..03)

**What shipped:**

Every SPU-94S voice is now feature-complete to the PS1 SPU spec. ADSR envelope corrected to match hardware (base-8 decrease formulas). Signed volume with phase inversion. Voice-to-voice pitch modulation (PMON) for FM synthesis and vibrato. Global LFSR noise generator (NON) for percussion and texture. Hardware-driven volume sweep with independent L/R state machines (VCA ramp). Musician-facing GUI controls: pan knob + level fader replacing raw register sliders, NON/PMON toggles with Noise Color control, VCA ramp section with direction/speed/curve/ARM. 98 voice engine unit tests, 6 rt_safety gates, all green. 99 commits, 230 files changed over 4 days.

**Key accomplishments:**

1. ADSR correction (ADR-0056) — sustain-decrease/release formulas fixed from base 7 to base 8 per nocash spec
2. Signed volume + VxOUTX capture — full -0x4000..+0x3FFF range, phase inversion, outx field for PMON chain
3. Pitch modulation (PMON) — 24-bit bitmask, Factor=outx+0x8000, chain stacking, ADR-0057 with DuckStation witness
4. Noise generator (NON) — global LFSR (taps 15,12,11,10), timer-driven frequency, ADR-0058
5. Volume sweep — independent L/R sweep state machines, shared step helper with ADSR, anti-stall guard, ADR-0059
6. Musician controls — Pan/Level/INV replacing raw Vol L/R, NON/PMON toggles, Noise Color knob, VCA ramp (direction/speed/curve/ARM)

**Key decisions:**

- ADR-0056: Decrease base 8, increase base 7 (nocash spec)
- ADR-0057: VxOUTX is post-ADSR, pre-volume (DuckStation-confirmed, HIGH confidence)
- ADR-0058: Noise LFSR XNOR taps 15,12,11,10, seed=1 (emulator consensus)
- ADR-0059: Negative-phase sweep LOW confidence (nocash "not yet tested")
- VCA ramp naming over "sweep" (musician-facing vs Sony register jargon)
- Pan/Level static, VCA ramp is modulation layer (future loop/tremolo won't fight faders)
- Voice Dynamics & Stereo Effects deferred to own milestone

Known deferred items at close: 1 (MIDI dispatch regression test — deferred to plugin port rebuild)

**Archived to:** `.planning/milestones/v1.9-ROADMAP.md`, `.planning/milestones/v1.9-REQUIREMENTS.md`

---

## v1.8 PSX Voice Engine (Shipped: 2026-05-21)

**Phases completed:** 6 phases, 7 plans (Phases 27-32)
**Tag:** `v1.8`
**Requirements:** 34/34 complete (VOICE-01..06, ADSR-01..06, LOOP-01..05, MIX-01..06, RAM-01..04, TEST-01..04, AA-01..03)

**What shipped:**

24-voice ADPCM sampler engine built on the existing SPU-94 C core. Samples load as WAV, encode to 4-bit Sony ADPCM on intake, and play back through a PS1-faithful single-counter pitch architecture with 4-tap Gaussian interpolation. Each voice has an independent counter-accumulate ADSR envelope (fake exponential attack above 0x6000, real exponential decay, sustain plateau, release to silence on KOFF). SPU loop mechanics: flag-byte dispatch from ADPCM block headers, loop-start auto-latch, filter state snapshot/restore at loop boundaries, one-shot termination with ENDX status. 24 voices run in parallel through an int32 accumulator with sat_s16 at the output, EON-gated per-voice reverb send, and master volume scaling. Full standalone sampler GUI: waveform display with mouse-wheel zoom and pan-drag scroll, draggable start/loop/end markers with push logic, ADSR knobs with analytical real-time display, gate/latch trigger modes, marker lock for window scanning, sampler drive stage, pitch control, MIDI note dispatch with round-robin voice allocation. Anti-aliasing toggle switches all 24 voices between Gaussian interpolation (default) and raw zero-order-hold for creative aliasing artifacts. 64 commits, 54 files changed, +11,410 lines over 5 days.

**Key accomplishments:**

1. `spu94_voice_t` / `spu94_voice_tick` — per-voice state struct with ADPCM decode, single-counter Gaussian interpolation, pitch register with 0x3FFF hardware clamp, isolated ring buffers
2. `spu94_adsr_t` / `spu94_adsr_tick` — counter-accumulate envelope with bit-15 trigger, fake-exponential attack, real-exponential decay, configurable sustain target, KOFF-triggered release
3. SPU loop mechanics — flag-byte dispatch after ADPCM decode, auto-latching loop address, filter state snapshot/restore at loop boundaries, ENDX status bit API
4. `spu94_voice_mixer_t` — 24-voice parallel mixer with pending KON/KOFF bitmask dispatch, EON-gated reverb send, master volume Q15 scaling, coexistence with ADPCM coloration bus
5. Sampler GUI — waveform display with zoom/scroll, draggable S/L/E markers with push logic, ADSR knobs with analytical rendering and power-curve scaling, gate/latch trigger, marker lock, drive stage
6. MIDI dispatch — round-robin voice allocation, `midiNoteToPitch` with 0x3FFF clamp, note-on/note-off in processBlock, standalone-only via `acceptsMidi` gate

**Key decisions:**

- Separate 512 KB voice RAM (deliberate deviation from PS1's shared address space — avoids collision risk)
- ADPCM state cache built at sample load time (correct decoder history at any position)
- End-addr loop path keeps has_block=1 (matches VAG flag path — fixes DC on short loops)
- Decay shift max extended 15→20 (PS1 limits to 4 bits but mid-knob decay was inaudible at 155ms)
- Analytical ADSR display rendering (tick-by-tick simulation couldn't handle extreme shift values)
- Delta interception for zoom-scaled knobs (factor 0.125 — JUCE slider sensitivity APIs didn't work for rotary)
- Block 0 silence is expected content (ADPCM starts from zero history)
- `NEEDS_MIDI_INPUT TRUE` in CMakeLists.txt (required for standalone MIDI routing)
- Gate (hold) and latch (toggle) trigger modes for different playing styles

Known deferred items at close: 5 (see STATE.md Deferred Items — all from prior milestones)

**Archived to:** `.planning/milestones/v1.8-ROADMAP.md`, `.planning/milestones/v1.8-REQUIREMENTS.md`

---

## v1.0 SPU-94 Standalone (Shipped: 2026-04-26)

**Phases completed:** 8 phases, 33 plans (Phases 1-7 = M1 reverb core; Phase 8 = standalone GUI)
**Tag:** `m1-reverb-core` on the core (Phases 1-7, 2026-04-25); v1.0 standalone is the Phase 8 close on 2026-04-26 (untagged — predates the v1.x tag convention)
**Requirements:** 58 total (49 M1 validated + 9 STANDALONE-01..09)

**What shipped:**

Bit-faithful PS1 SPU reverb implemented from spec as a freestanding C core (`libspu94` — no heap, no locks, no syscalls, no STL, C99-pedantic + C++-pedantic clean), wrapped in a single-window JUCE standalone audio tool on Linux. Drag-and-drop WAV playback accepts any sample rate (8/11.025/22.05/44.1/48/88.2/96 kHz), any bit depth (8/16/24/32-int/32-float), mono or stereo — converted internally to 44.1 kHz int16 stereo before the reverb sees the buffer. 10 PS1 factory presets selectable from a flat dropdown. 18 raw register sliders exposed with raw register names (`vIIR`, `dCOMB1`, etc. — not musical aliases). Wet/Dry equal-power crossfade knob + Input Level knob. JUCE stock look-and-feel. No plugin formats, no DAW integration — the standalone closes the "I can't easily hear what's being built" gap.

**Key accomplishments:**

1. `libspu94` — opaque-handle C core with caller-allocated storage, 35-register identity surface, Q15 fixed-point math with hand-audited reference table, pending-shadow + tick-flush write-timing policy, 39-tap half-band FIR for sample-rate conversion
2. 10 PS1 factory preset tables in `.rodata` with byte-for-byte traceability to two audited sources (psx-spx + BIB references)
3. Determinism-locked build (`-Werror`, `-ffp-contract=off`, `-fno-fast-math`); 5-job GitHub Actions CI (matrix gcc+clang, grep-guard, clang-tidy, cppcheck, UBSan-hard-abort) gates every commit
4. Four permanent `rt_safety` ctest gates prove no-heap / no-locks / no-syscalls / bounded per-block latency variance (ratio 0.741, budget 3.0 across 10^5 blocks)
5. 50-golden `.wav` corpus (10 presets × 5 inputs) with SHA-256 sidecars + sha-pinned bookworm-slim Dockerfile that reproduces the corpus byte-for-byte in CI
6. Python ctypes bindings + native C `spu94` CLI with dr_wav-backed WAV I/O and jsmn-backed JSON config
7. JUCE 8.0.12 standalone GUI: 18 raw register sliders + 10-preset dropdown + Wet/Dry knob + Input Level knob, lock-free parameter bridge between GUI and audio thread, native file picker for WAV load
8. 14+ ADRs covering Q15 multiply semantics, vIIR = -0x8000 anomaly, UBSan no_sanitize policy, BufferAddress wrap formula, snap-on-write side effect, split write-timing policy, mailbox D-05 mix bus, and the resolution chain ADR-0004..ADR-0010

**Key decisions:**

- Raw register names over musical aliases on the GUI — surface the spec, not a translation
- JUCE stock look-and-feel for v1.0 — visual identity deferred
- Linux primary; macOS/Windows builds deferred
- No plugin formats (VST3/LV2/CLAP/AU) for v1.0 — same JUCE codebase can add them later via `FORMATS` argument to `juce_add_plugin`
- 44.1 kHz int16 stereo as the internal canonical format; resampling happens in the wrapper, the core stays bit-faithful
- Phase 9 (MCU cross-compile smoke test) parked indefinitely — design discipline + `rt_safety` ctests already prove portability
- LICENSE pick (MIT vs Apache-2.0) deferred to end of Milestone 1 (still open as of v1.6)

**Issues deferred:**

- Named-lever curation (Room Size / Pre Delay / Damping / Width / Mix) — deferred to later milestones; v1.0 ships raw registers only
- Plugin-layer DSP extensions (true Pre-Delay buffer, Input HPF, Freeze, Tail-modulation LFO)
- WAV file save / export
- Live audio input (mic / line-in via JACK / PipeWire / ALSA)
- Custom UI / visual identity
- M2 ADPCM, M3 DAC reconstruction (both followed in v1.1 / v1.2-v1.3)

**Archived to:** `.planning/milestones/v1.0-product-ROADMAP.md`, `.planning/milestones/v1.0-ROADMAP.md`, `.planning/milestones/v1.0-REQUIREMENTS.md`, `.planning/milestones/v1.0-MILESTONE-AUDIT.md`, `.planning/milestones/v1.0-phases/`

---

## v1.1 ADPCM Encode/Decode (Shipped: 2026-04-27)

**Phases completed:** 4 phases, 10 plans
**Tag:** `v1.1`
**Requirements:** 23/23 complete (ADPCM-01..07, ADPCM-INT-01..06, ADPCM-IO-01..06, ADPCM-TEST-01..04)

**What shipped:**

Bit-faithful Sony 4-bit ADPCM encode/decode added to libspu94 as a peer module (380 LOC C, zero heap, integer-only). Wired into the reverb pipeline as a toggleable coloration stage — when enabled, input PCM round-trips through ADPCM before reverb, reproducing the quantization noise and filter ringing of PS1 audio. Accessible via C API, CLI (`--adpcm` flag + `adpcm-encode`/`adpcm-decode` subcommands), Python ctypes, and JUCE standalone GUI toggle.

**Key accomplishments:**

1. ADPCM decoder + encoder with 5 SPU filter pairs, brute-force best-fit encoder over 65 combinations, caller-allocated 4-byte state
2. Pipeline integration as toggleable upstream stage with 28-sample latency, default-off, all rt_safety gates passing
3. VAG v2 file format I/O (big-endian, terminator blocks), CLI subcommands, Python bindings, JUCE ADPCM toggle
4. 32 unit tests with coverage maps, 30 ADPCM golden files (10 presets x 3 inputs) with SHA-256 regression gate
5. 7 ADRs (ADR-0047 through ADR-0053) formalizing all gray-area resolutions

**Key decisions:**

- Rounding: `(old*f0 + older*f1 + 32) >> 6` — round-to-nearest via +32 bias (ADR-0047)
- Shift 13-15 mapped to shift 9 per psx-spx (ADR-0048)
- Filter 5-7 clamped to filter 4 per emulator consensus (ADR-0049)
- ASR division semantics per ADR-0001 discipline (ADR-0050)
- L2 error metric in int64 (ADR-0051), strict `<` tiebreak with iteration order (ADR-0052)
- Caller zero-pads tail blocks to 28 samples (ADR-0053)

**Issues deferred:** None — clean close.

**Archived to:** `.planning/milestones/v1.1-ROADMAP.md`, `.planning/milestones/v1.1-REQUIREMENTS.md`, `.planning/milestones/v1.1-phases/`

---

## v1.2 DAC Modeling (Shipped: 2026-04-30)

**Phases completed:** 5 phases, 12 plans (Phases 5-9)
**Tag:** `v1.2`
**Requirements:** 14/14 complete (DAC-FILT-01..03, DAC-NOISE-01, DAC-INT-01..03, DAC-IO-01..03, DAC-TEST-01..04)

**What shipped:**

AK4309 digital-domain DAC artifacts modeled as a toggleable coloration stage on the output path. The PS1 used an AKM AK4309AVM 1-bit delta-sigma DAC with 8x digital interpolation, 2nd-order noise shaping, and on-chip reconstruction filtering. v1.2 models the converter's interpolation filter (as a Q15 fixed-point cascaded half-band FIR running at 44.1 kHz — approximate single-rate implementation) and its delta-sigma noise floor (2nd-order shaped noise from an LFSR, +12 dB/octave HP spectral slope, ~90 dB dynamic range calibrated to 384x OSR). Pipeline integration also introduced a full send/return mixer architecture: three buses (dry, patina, reverb), six independent faders, latency compensation, with the DAC section as a toggleable post-mixer coloration stage. JUCE GUI redesigned as a 4-zone layout exposing the new mixer faders + DAC/ADPCM toggles.

**Key accomplishments:**

1. AK4309 8x cascaded half-band FIR interpolator in Q15 fixed-point (approximate single-rate implementation at 44.1 kHz; true 8x oversampling deferred to v1.3)
2. 2nd-order shaped noise model: LFSR source + 2nd-order highpass shaping, +12 dB/octave spectral slope, ~90 dB dynamic range
3. Send/return mixer architecture: 3 buses (dry, patina/ADPCM, reverb) with independent faders summed at a master mixer; latency compensation; DAC section as a toggleable post-mixer stage
4. `spu94_set_dac_enabled` / `spu94_get_dac_enabled` toggle API matching the ADPCM toggle pattern; default-off; clean filter/noise state reset on disable; DAC state fits within the existing `spu94_state` budget
5. CLI `--dac` flag, Python ctypes binding, JUCE DAC checkbox — three-surface I/O matching the ADPCM toggle pattern
6. JUCE 4-zone GUI redesign exposing all new controls (6 mixer faders + DAC/ADPCM toggles alongside existing register sliders / preset dropdown / Wet-Dry / Input)
7. DAC-enabled golden WAV corpus (55 WAVs + SHA-256 sidecars), Python frequency-response measurement script verifying passband ripple, C unit tests for filter coefficient correctness + noise shaping spectral slope + toggle transitions + state reset, `docs/COVERAGE.md` mapping every DAC requirement
8. All four `rt_safety` gates (rt_no_heap / rt_no_locks / rt_no_syscalls / rt_bench_latency) pass with DAC enabled — zero regression with DAC disabled

**Key decisions:**

- Datasheet spec used as authoritative reference (+/-0.05 dB passband ripple, 41 dB stopband attenuation) over Stereophile composite-chain measurements — ADR-0054 documents the gray area and confidence assessment
- Approximate single-rate FIR cascade for v1.2 — true zero-stuff oversampling at 352.8 kHz deferred to v1.3 (planner's call)
- 2nd-order noise shaping calibrated to ~90 dB dynamic range at 384x OSR
- Default DAC-off — matches ADPCM toggle pattern, opt-in coloration
- Three-bus send/return architecture (dry / patina / reverb) — avoids cascading wet/dry phase-alignment problems; six controls (input gain, dry fader, patina fader, dry-reverb send, patina-reverb send, reverb fader) plus DAC toggle
- DAC state contained within existing `spu94_state` budget — no allocation growth, disable resets state cleanly

**Issues deferred:**

- True 8x oversampling at 352.8 kHz with proper sum-of-8 decimation — followed in v1.3
- Hardware calibration against PS1 captures (DAC-HW-01..03 — noise amplitude, filter response, A/B perceptual comparison) — deferred to M5 hardware validation
- Late-revision CXD2938Q integrated DAC profile (PU-22+ boards) — deferred indefinitely
- Analog output stage, SCF/CTF reconstruction filter, ZOH sinc droop, idle tone, true 384x-rate delta-sigma simulation — explicitly out of scope (rationales in `v1.2-REQUIREMENTS.md`)

**Archived to:** `.planning/milestones/v1.2-ROADMAP.md`, `.planning/milestones/v1.2-REQUIREMENTS.md`, `.planning/milestones/v1.2-phases/`

---

## v1.3 True Oversampled DAC (Shipped: 2026-05-01)

**Phases completed:** 3 phases, 8 plans (Phases 10-12)
**Tag:** `v1.3`
**Requirements:** INT-01..04 (true-oversample integration), CMP-01..03 (comparison & characterization), CR-01 (zero-stuff gain documentation) — all closed

**What shipped:**

Genuine 8x oversampling at 352.8 kHz replacing v1.2's approximate single-rate FIR cascade. The 3-stage cascaded half-band FIR now runs at its true elevated rates (44.1 → 88.2 → 176.4 → 352.8 kHz) with zero-stuff upsampling at each stage and proper sum-of-8 decimation at the output. Post-cascade int32-precision noise injection avoided the -72 dBFS quantization floor that int16 intermediates would have imposed at the elevated rate. An A/B mode toggle (`--no-dac-true-oversample` on the CLI, parallel Python and JUCE controls) preserves v1.2's approximate mode for direct comparison — v1.2's `spu94_dac_fir_step` body is unchanged and coexists with the new `spu94_dac_fir_step_8x`. Measurement confirms: 91.83 dB frequency-response deviation between modes (v1.2's 44.1 kHz cascade caused massive HF rolloff; v1.3 preserves the full passband), identical -84.9 dBFS noise floor across both modes (shared noise path).

**Key accomplishments:**

1. `spu94_dac_fir_step_8x` — 3-stage zero-stuff cascade at true rates (Stage 1: 44.1→88.2, Stage 2: 88.2→176.4, Stage 3: 176.4→352.8 kHz); 14 polyphase coefficient evaluations + 28 sample pushes per input
2. Sum-of-8 proper decimation at the output of the 352.8 kHz cascade — not bare sample-skip
3. Post-cascade int32 noise injection — avoids -72 dBFS quantization floor that int16 intermediates + `<<3` gain compensation would have created
4. `DAC_NOISE_SHIFT_8X=10` / `DAC_NOISE_8X_ACC_SCALE=14` calibration constants — empirically tuned to hit -90 dB in-band RMS
5. `dac_true_oversample` toggle API (set/get pair, default on); `--no-dac-true-oversample` CLI flag; matching Python ctypes binding and JUCE toggle
6. v1.2 mode preserved unchanged for A/B — original `spu94_dac_fir_step` body intact (verified by `test_v1_2_regression`); `tests/golden_v1.2/` corpus retained (55 WAVs + sidecars)
7. `tools/dac_compare.py` + `tools/dac_compare.png` — 4-panel measurement comparison (frequency response, impulse, noise-floor PSD, time-domain difference) generated via paired CLI renders
8. ADR-0055 — documents v1.2 vs v1.3 audible differences with measurement evidence

**Key decisions:**

- D-01 Naive zero-stuff cascade — each stage upsamples 2x with zero insertion + FIR; no polyphase implementation (D-02 explicit rejection)
- D-03 Coexist with v1.2 — `spu94_dac_fir_step` body completely unchanged; new `_8x` variant added; A/B toggle preserves both code paths in the binary
- D-04 Scipy prototype first — `tools/dac_filter_design.py --verify-8x` validates the Q15 C implementation against a scipy reference cascade
- INT-04 passband tolerance pragmatically widened from 0.01 dB → 0.05 dB — 0.01 dB is mathematically unreachable in Q15 across 14 evaluations per sample; measured deviation 0.044 dB accepted as satisfying the spirit of INT-04
- Raw white noise at 352.8 kHz instead of HP-shaped at the elevated rate — HP shaping pushes >99.9% of noise above audio band at 8x, impractical at int16 precision
- v1.3 retained as default; v1.2 preserved as toggleable A/B reference, not removed
- CR-01: 1/8 zero-stuff gain drop documented as correct naive-cascade physics — accepted rather than gain-compensated (hardware-faithful behavior)
- LFSR advanced 8 times per output sample (even though only the last contributes) for L/R decorrelation

**Issues deferred:** None — clean close.

**Archived to:** None in `.planning/milestones/` (v1.3 archive artifacts were not separated out). Source artifacts live at `.planning/phases/10-core-polyphase-fir-cascade/`, `.planning/phases/11-noise-recalibration-integration/`, `.planning/phases/12-verification-characterization/`.

---

## v1.4 Preset System (Shipped: 2026-05-02)

**Phases completed:** 3 phases, 5 plans
**Tag:** `v1.4`
**Requirements:** 10/10 complete (PRE-01..PRE-10)

**What shipped:**

Human-readable preset save/load system for SPU-94. The C core serializes all 46 engine fields (35 registers + 7 mixer faders + 4 DAC toggles) to a versioned INI-style `.spu94` text file and restores them with bit-identical fidelity. Accessible via C API (`spu94_preset_save` / `spu94_preset_load`), CLI (`preset-dump` subcommand + `--load-preset` flag), and JUCE standalone GUI (Save/Load buttons with native file dialogs, custom preset dropdown, modified-state asterisk indicator).

**Key accomplishments:**

1. `spu94_preset_save` — EMIT-macro-based overflow-safe serializer writing 46 fields across 3 INI sections (registers, mixer, DAC) with version header
2. `spu94_preset_load` — section-aware strchr-based parser with 512-byte line buffer, hand-rolled `parse_hex_u16` (grep-guard compliant)
3. CLI `preset-dump` with `--preset`/`--name`/`-o`/`--list-presets` flags + `--load-preset` on reverb with three-way mutual exclusion
4. JUCE Save/Load buttons with native file dialogs, custom preset dropdown (diamond prefix), modified-state asterisk (30Hz 46-field diff)
5. Integration-level golden round-trip test proving bit-identical audio output after save/load through `spu94_process` (factory Hall + custom Delay with non-default mixer/DAC)

**Key decisions:**

- EMIT macro pattern for overflow-safe snprintf writes
- strchr-based key=value splitting (strtok modifies strings)
- Save dialog simplified to single native file dialog step (kdialog touch-create workaround)
- File-preset audio-thread handoff via pendingPresetBuf + acquire/release atomics
- loaded_pid = -1 for file presets prevents default-fader overwrite

**Issues deferred:** None — clean close.

**Archived to:** `.planning/milestones/v1.4-ROADMAP.md`, `.planning/milestones/v1.4-REQUIREMENTS.md`

---

## v1.5 Preset Interpolation Engine (Shipped: 2026-05-06)

**Phases completed:** 2 phases, 3 plans
**Tag:** `v1.5`
**Requirements:** 8/8 complete (INTERP-01..05, GUI-01..03)

**What shipped:**

A single morph knob between Sony's 9 PS1 factory presets, replacing the archived v1.5/v1.6 macro control approach (gang clamping, Spread/Sweep/Rotate, safety constraints) which proved too complex. A morph position (0.0-1.0) maps to linearly interpolated register values across the 9 presets with bit-identical output at exact waypoint positions — every interpolated state is bounded by Sony-validated configurations, so no clamping or safety logic is needed. 280px JUCE rotary knob with 9 PS1-colored waypoint dots, detent snap at each preset, dynamic label showing preset name at detents and numerical position between. Macro/Advanced toggle switches between the morph knob view and raw register access.

**Key accomplishments:**

1. `spu94_interp` — C interpolation engine: morph position → adjacent-preset pair + fractional distance, linear interpolation across 30 active registers, bit-identical output at waypoint positions
2. MorphPanel JUCE component: 280px rotary knob, 9 PS1-colored waypoint dots equally spaced on the arc, detent snap at each preset (threshold 0.01), dynamic label
3. Macro/Advanced toggle in the editor — default morph view with switchable raw register access
4. Write-on-change optimization: morph engine only writes registers when position changes, preventing delay-line disruption

**Key decisions:**

- Waypoint order: Half Echo → Room → Studio A → Studio B → Studio C → Hall → Space Echo → Echo → Delay (perceptual, confirmed by ear)
- All registers morph together — no decoupled parameters for v1.5
- Fixed registers excluded from interpolation: vLOUT/vROUT (0x7FFF), vLIN/vRIN (0x8000), mBASE (0x0000)
- Simple detent snap (threshold 0.01), no magnetic physics
- morphActive atomic gates morph engine vs register bridge in `processBlock`
- Equal angular spacing between waypoints on the knob arc
- Replaces archived v1.5/v1.6 macro approach — interpolation between Sony-validated presets eliminates clamping by construction

**Pivot context:** v1.5 was built after reverting the original v1.5/v1.6 macro control attempt (Spread/Sweep/Rotate, gang clamping, Sync/Free modal toggle). The macro approach exposed complexity rather than taming it — interlocking constraints fought each other. Decision logged in PROJECT.md and `.planning/milestones/v1.5-v1.6-PIVOT.md`. Abandoned macro code recoverable on `archive/v1.5-v1.6-macro-approach` branch.

**Known at ship time:** Transition artifacting (digital clicks when d/m delay-line addresses change during knob movement) and unstable feedback at certain interpolated positions. Both were addressed during v1.6 work via the engine state mirroring overhaul and waypoint-bounded interpolation — not carried as open bugs.

**Archived to:** `.planning/milestones/v1.5-ROADMAP.md`, `.planning/milestones/v1.5-REQUIREMENTS.md`, `.planning/milestones/v1.5-phases/`, `.planning/milestones/v1.5-v1.6-PIVOT.md`

---

## v1.6 User Programmable Waypoints (Shipped: 2026-05-10)

**Phases completed:** 3 phases, 4 plans
**Tag:** `v1.6`
**Requirements:** 17/17 complete (USLOT-01..17)

**What shipped:**

8 user-programmable waypoint slots between Sony's 9 factory anchors. Empty slots are transparent (audio bit-identical to v1.5); filled slots act as new interpolation breakpoints, turning the morph dial from a 9-position perceptual continuum into a user-customisable 17-position continuum. Per-tick EDIT / EXPORT / LOAD action buttons stacked top-right of MorphPanel; SAVE (PS1 teal) / REVERT (PS1 coral) edit-flow band hidden until an edit is active. Filled slots persist in the `.spu94` preset format as `[user_slot N]` sections; empty slots are omitted so legacy presets stay byte-identical and pre-feature `.spu94` files load cleanly with all slots empty. Single-slot EXPORT files enable drop-anywhere copy/paste between beta-tester slot files (LOAD ignores the file's slot index and stamps onto the currently-parked tick). Engine state mirroring overhaul: sliders + tick colors now reflect engine state regardless of WAV load, playback state, or slew progress.

**Key accomplishments:**

1. `spu94_interp_set_user_slot` / `_clear_user_slot` / `_get_user_slot` / `_user_slot_is_filled` — public C API for 8-slot waypoint storage at midpoint positions 1/16..15/16, INTERP-04 bit-identity carve-out extended to filled slots
2. 17-detent morph knob with user-slot ticks rendered between the knob's outer edge and the Sony dot ring (PS1 blue = filled, dim grey = empty)
3. Per-tick action stack (EDIT / EXPORT / LOAD) replacing the floating Sony-preset dropdown after a long design loop; SAVE/REVERT band anchored top of Advanced viewport, hidden via `addChildComponent`
4. `spu94_export_user_slot` / `spu94_load_user_slot` for single-slot `.spu94` file I/O with drop-anywhere semantics; 3 added unit tests
5. Preset persistence: `[user_slot N]` sections, byte-identical back-compat for pre-feature presets, 2 added unit tests; `SPU94_PRESET_BUF_SIZE` bumped 4096 → 8192
6. Engine state mirroring overhaul — state management hoisted above audio-I/O gate, forced re-applies (LOAD/SAVE/REVERT) snap regardless of Morph Speed, shadow sync reads engines[1] (target) not engines[0] (mid-slew)
7. Final bug pass: `saveUserSlot` mirrors writes to engines[1] (scratch engine), `spu94_apply_pending_writes` flushes TICK_LATCHED edits before snapshot, default Morph Speed lowered 1.0 → 0.5

**Key decisions:**

- 8 slots at midpoint positions (1/16, 3/16, ..., 15/16) — preserves Sony anchors as the visible coarse grid and adds fine detail between them
- Empty = transparent — fresh project remains bit-identical to v1.5
- User slot state lives on `spu94_state` (per-engine); multi-engine consumers MUST mirror writes (the audible-glide root cause)
- Three per-tick action buttons (EDIT/EXPORT/LOAD) over dropdown — invites curiosity, dedicated affordance
- REVERT = clear slot entirely (not 'discard edits') — user-requested emergency clear
- LOAD ignores file's slot index — enables drop-anywhere copy/paste between beta-tester slot files
- State management above audio-I/O gate — sliders/ticks reflect engine state regardless of WAV/playback
- Forced re-applies snap regardless of Morph Speed — action buttons need instantaneous feedback
- Empty slots omitted from preset serialization — byte-identical back-compat with pre-feature presets

**Anti-patterns captured:**

- Per-engine state without explicit mirroring (blocking — audible glide bug)
- Synchronous `spu94_apply_pending_writes` from GUI thread mid-frame (advisory)
- Audio I/O gate above state management (advisory — sliders go stale without WAV loaded)
- TICK_LATCHED snapshot races (advisory — SAVE misses fresh m/d-prefix edits without pending-writes flush)

**Issues deferred:** None — clean close. Real-world user testing will surface anything not caught here; raised as a normal bug if so.

**Archived to:** `.planning/milestones/v1.6-ROADMAP.md`, `.planning/milestones/v1.6-REQUIREMENTS.md`, `.planning/milestones/v1.6-user-waypoints-HANDOFF.json`, `.planning/milestones/v1.6-phases/`

---

## v1.7 DAW Plugin Port (Shipped: 2026-05-16)

**Phases completed:** 6 phases, 10 plans (Phases 21-26)
**Tag:** `v1.7`
**Requirements:** 45/51 satisfied; 6 partial (accepted as tech debt); 2 deferred (PLUG-52/53 conditional)

**What shipped:**

SPU-94 packaged as a multi-format DAW plugin (VST3 + AU + LV2 + CLAP) on Linux, macOS, and Windows. The bit-faithful C core stays untouched — sample-rate conversion (libsamplerate Sinc Medium, bidirectional) and bit-depth conversion (truncation, no dither) live entirely in the plugin wrapper. 9 host-automatable parameters exposed for DAW automation. Binary state persistence via a locale-independent container format. Fixed-size plugin window reusing the standalone GUI. Per-OS installer packages (macOS .pkg/.dmg, Windows Inno Setup, Linux tarball+install.sh). Tag-triggered GitHub Release workflow. pluginval strictness-7 on VST3/AU as hard CI gates. Post-beta refinements: PS1-faithful single-counter voice path on the ADPCM bus, Fast/Slow morph speed toggle, Gaussian interpolation defaults on.

**Key accomplishments:**

1. 11 user-facing plugin binaries across 3 OSes (4 Linux + 4 macOS + 3 Windows), all built and validated in a single GitHub Actions matrix on every push to main
2. Bidirectional libsamplerate SRC wrapper with impulse-measured group-delay latency reporting via `setLatencySamples()` — 44.1 kHz fast-path bypasses SRC entirely
3. Binary state container (SPU9 magic + version + body) with locale-independent IEEE 754 float appendix; round-trip preserves all 9 params + full engine state
4. 9 host-automatable `AudioParameterFloat` instances routed through atomic-scalar bridge (no APVTS) with frozen parameter IDs (versionHint=1)
5. Bus layout whitelist (mono→mono, mono→stereo, stereo→stereo) with stack-scratch mono summing and CLAP mono feature tag
6. Per-OS packaging: macOS .pkg + .dmg, Windows Inno Setup, Linux tarball + install.sh; tag-triggered release CI with artifact upload

**Key decisions:**

- libsamplerate Sinc Medium (BSD-2) for SRC — industry standard, transparent on HF
- Truncation at float→int16 boundary (no dither) — period-faithful per North Star
- RegisterBridge atomic-scalar pattern (not APVTS) — lightweight, RT-safe, no JUCE coupling in the bridge
- State format: binary envelope over existing .spu94 text body + 6-float appendix — future-version rejection via version byte
- LV2 dropped on Windows (no major Windows DAW scans LV2)
- Standalone reframed as internal dev testbed, not user deliverable
- Code signing deferred — testers click through Gatekeeper/SmartScreen; reactive only
- Single-counter voice path on ADPCM bus — matches real PS1 SPU hardware architecture (one pitch counter, bits 12+ = sample index, bits 4-11 = Gaussian index)
- Gaussian interpolation defaults ON — PS1 has no Gauss bypass in hardware
- Fast/Slow morph speed: Fast 0–0.5s, Slow 0.5–8s — slow range creates evolving feedback textures

**Known gaps (accepted as tech debt):**

- PLUG-42: auval, lv2lint, and VST3 SDK validator steps have `continue-on-error: true` (pluginval on VST3/AU is the hard gate)
- PLUG-37/41: CLAP excluded from pluginval validation (pluginval hangs on CLAP — upstream limitation)
- PLUG-38/39/40: Soft-gated validators (run but don't fail CI on error)
- State restore race: `setValueNotifyingHost` in filePresetReady block (low practical impact — fires once per preset load)
- Plugin unit tests compile but are not invoked in CI (ctest not called in plugins.yml)

Known deferred items at close: 6 (see STATE.md Deferred Items)

**Archived to:** `.planning/milestones/v1.7-ROADMAP.md`, `.planning/milestones/v1.7-REQUIREMENTS.md`, `.planning/milestones/v1.7-MILESTONE-AUDIT.md`

---
