# Phase 4 Witness Captures — Mednafen / DuckStation empirical FIR classification

Per 04-RESEARCH § Witness Analysis + ROADMAP Phase 4 SC-4 mandate + CONTEXT D-14.

## Status

**Empirical classification: DEFERRED.** Neither Mednafen nor DuckStation is installed on the Plan 04 executor's machine (`which mednafen` and `which duckstation-nogui / duckstation-qt / duckstation` returned no match on 2026-04-20), and no PSX test ROM exists under `tests/fixtures/roms/` or `tests/fixtures/witness-rom/`. The protocol below is documented for a later session (or Phase 7 TEST-03 harness work) to pick up. Phase 4 closure is NOT blocked: lv2-psx-reverb OUT-OF-AXIS is already primary-source-attested (README self-attestation) and is sufficient for the ADR-0012 SC-4 landing.

## Purpose

Determine whether Mednafen and DuckStation implement the 39-tap half-band FIR that the hardware SPU applies at both I/O boundaries of the reverb ring (IN-AXIS — usable as a Phase 7 TEST-03 oracle on frequency-response claims) or skip it like lv2-psx-reverb (OUT-OF-AXIS — not usable on frequency-response axis, still usable on reverb-network-behavior axis).

## Licensing posture

Per `.planning/PROJECT.md`:

- Mednafen is **GPLv2**.
- DuckStation is **CC-BY-NC-ND** as of 2024-09.

Their OUTPUT audio is consumed as witness material (uncopyrightable facts: frequency-response numbers extracted from captured WAVs). Their SOURCE code is never read as a primary input to SPU-94 design. This directory holds only captured audio output and analysis scripts; no emulator source code is vendored.

## Protocol (04-RESEARCH § Witness Analysis)

### Probe signals (four)

Driven into each emulator's reverb path (preset = Hall or equivalent; dry mix = 0; wet mix = full):

1. **Unit impulse** — single sample at +0x7FFF at t=0, zeros thereafter.
2. **Near-Nyquist sine** — 20 kHz continuous sine at 44.1 kHz input rate.
3. **White-noise burst** — uniform int16 random noise for ~1 s.
4. **Band-limited sweep** — logarithmic sine sweep 20 Hz → 22 kHz, 44100 samples, Hann-windowed at both ends.

### Capture

- **Mednafen**:
  ```
  mednafen -sound.driver file \
           -sound.wav witness-captures/captures/mednafen/${PROBE}_${PRESET}.wav \
           ${ROM_PATH}
  ```
  (Exact flag may differ across Mednafen versions — consult `mednafen --help` for the current file-sound-driver syntax.)

- **DuckStation**:
  ```
  duckstation-nogui --dump-audio witness-captures/captures/duckstation/${PROBE}_${PRESET}.wav \
                    ${ROM_PATH}
  ```
  (Or equivalent `--dump-wav` / `--audio-dump` flag; consult `duckstation-nogui --help`.)

Save captured WAV files to `witness-captures/captures/{mednafen,duckstation}/{impulse,sine20khz,whitenoise,sweep}_{preset}.wav`.

### Analysis

See `analyze_witness_capture.py` (TBD — add when captures are collected) or the numpy pseudocode in `04-RESEARCH.md § Witness Analysis`. Key metrics per capture:

- **20 kHz stopband attenuation**: `< −40 dB` → IN-AXIS; `within ±6 dB of input level` → OUT-OF-AXIS.
- **White-noise output spectrum**: lowpass shape with transition ~11 kHz → IN-AXIS; flat → OUT-OF-AXIS.
- **Impulse response group delay** (reverb-off preset, if the emulator exposes one): ~58 samples → IN-AXIS; ~0 samples → OUT-OF-AXIS.

Require majority agreement across ≥ 3 of the 4 probe signals for classification.

## Classification Table

| Emulator         | Version | OS/arch        | Test ROM   | IN-AXIS / OUT-OF-AXIS | Confidence | Evidence                                                                                  | Captured by                 | Date       |
|------------------|---------|----------------|------------|-----------------------|------------|-------------------------------------------------------------------------------------------|-----------------------------|------------|
| lv2-psx-reverb   | N/A (LV2 plugin) | Linux x86_64 | N/A (README self-attestation) | **OUT-OF-AXIS** | HIGH | Primary-source README self-attestation quoted verbatim in 04-RESEARCH § Witness Analysis | 04-RESEARCH pass 2026-04-20 | 2026-04-20 |
| Mednafen         | `{pending}` | `{pending}` | `{pending}` | `{pending — empirical pass required}` | `{pending}` | `{pending — see Protocol above}`                                                           | `{pending}`                 | `{pending}` |
| DuckStation      | `{pending}` | `{pending}` | `{pending}` | `{pending — empirical pass required}` | `{pending}` | `{pending — see Protocol above}`                                                           | `{pending}`                 | `{pending}` |

## Prerequisites for future execution

- [ ] Mednafen binary installed (`apt install mednafen` on Debian/Ubuntu; `brew install mednafen` on macOS; or build from source).
- [ ] DuckStation binary installed (AppImage / flatpak / build from source).
- [ ] PSX test ROM with a known reverb signal path (homebrew test ROM or commercial game with a legally-acquired backup image; purpose-built test ROM preferred for reproducibility).
- [ ] `numpy` + `scipy` + `matplotlib` for capture analysis.

## Deferral tracking

Deferred empirical pass is tracked for Phase 7 TEST-03 (witness-diff harness). ADR-0012 lands with "classification pending — protocol documented" wording for Mednafen and DuckStation; lv2-psx-reverb OUT-OF-AXIS is HIGH-confidence and alone satisfies the ROADMAP Phase 4 SC-4 "DECISIONS.md ADR for half-rate + lv2-psx-reverb exclusion" mandate. When the empirical pass is eventually run, update:

1. This classification table (fill in the `{pending}` cells).
2. `docs/DECISIONS.md` — either supersede ADR-0012 with a revised ADR carrying the classifications, OR add a follow-up ADR that references ADR-0012 and records the new data.
3. `04-04-SUMMARY.md` Witness Analysis section (or create a Phase 7 summary that covers this delta).

## Analysis artifacts

When empirical captures are collected, add analysis scripts to this directory:

- `analyze_witness_capture.py` — numpy FFT per capture; prints 20 kHz bin magnitude + spectral-shape summary + classification per metric.
- PNG spectrogram / magnitude-response plots per capture — referenced from the classification-table Evidence column.
- Per-probe WAV files under `captures/mednafen/` and `captures/duckstation/`.

## Why deferral is OK

The Phase 4 ROADMAP SC-4 asks for "DECISIONS.md ADR for half-rate architecture + lv2-psx-reverb frequency-axis exclusion". lv2-psx-reverb is the explicit named emulator in the success-criterion text, and its OUT-OF-AXIS classification comes from a primary-source README self-attestation (not an empirical capture), so the ADR lands on solid ground without Mednafen/DuckStation captures. The two GPL/CC-BY-NC-ND emulators are *additional* witnesses to strengthen the Phase 7 witness-diff tolerance calibration; they are not on the critical path for Phase 4 closure.

Phase 7 TEST-03 (witness-diff harness) formally consumes this deferral.
