# Requirements: SPU-94 — M2 ADPCM

**Updated:** 2026-04-26 (M2 milestone scoped — Sony 4-bit ADPCM encode/decode)
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Milestone:** M2 — ADPCM (version tag v1.1 upon completion and integration)

M2 adds bit-faithful Sony 4-bit ADPCM encode/decode to libspu94, wired into the reverb signal path as a toggleable coloration stage. When enabled, input PCM is encoded to ADPCM then decoded back, introducing the quantization noise and filter ringing that characterized every PS1 game's audio before it reached the reverb. Research complete 2026-04-26 (3 passes, 12 researcher agents, 12 documents in `.planning/research/m2-adpcm/`).

## Active — M2 ADPCM Requirements

### ADPCM — Core Codec

- [x] **ADPCM-01**: Decoder decodes a 16-byte ADPCM block into 28 int16 PCM samples using all 5 SPU filter coefficient pairs (f0={0,60,115,98,122}, f1={0,0,-52,-55,-60}), with round-to-nearest via `(old*f0 + older*f1 + 32) >> 6` arithmetic right shift, single clamp to int16 after the full expression, and two-sample state carry across blocks. *(Plan 01-01, commit 8bdd664)*
- [x] **ADPCM-02**: Decoder handles shift values 0-12 normally and maps shift 13-15 to shift=9 per nocash psx-spx. No undefined behavior from negative shift amounts. *(Plan 01-01, commit 8bdd664)*
- [x] **ADPCM-03**: Decoder parses nibbles in correct order (low nibble first within each data byte) and sign-extends 4-bit nibbles to signed integers before shifting. *(Plan 01-01, commit 8bdd664)*
- [x] **ADPCM-04**: Encoder selects optimal (filter, shift) pair per 28-sample block via brute-force search over all 65 combinations (5 filters x 13 shifts), using sum-of-squared-error in int64 as the metric, with deterministic tiebreaking (lower filter index, then lower shift). *(Plan 01-02, commit d91707a)*
- [x] **ADPCM-05**: Encoder uses reconstructed (decoded) sample values for prediction state, not original PCM. The encoder contains an internal copy of the decoder. *(Plan 01-02, commit d91707a)*
- [x] **ADPCM-06**: Encoder quantizes residuals to 4-bit signed range [-8, +7] with round-to-nearest, guarding against UB at shift=12 where the half-step rounding term would be `1 << -1`. *(Plan 01-02, commit d91707a)*
- [x] **ADPCM-07**: Both encode and decode are pure C functions with caller-allocated state (4 bytes: two int16 for old/older), zero heap, integer-only arithmetic. No dependency on `spu94_state`. *(Decoder half: Plan 01-01, commit 8bdd664; encoder half: Plan 01-02)*

### ADPCM-INT — Integration with Reverb Pipeline

- [x] **ADPCM-INT-01**: ADPCM encode+decode is wired into `spu94_process` as an optional stage upstream of the FIR decimator, matching PS1 hardware signal flow. Toggled via `spu94_set_adpcm_enabled()` / `spu94_get_adpcm_enabled()`.
- [x] **ADPCM-INT-02**: Block/frame boundary handled via double-buffer (input accumulates in one 28-sample buffer, output emits from the previous decoded block). Fixed 28-sample latency when enabled, zero when disabled.
- [x] **ADPCM-INT-03**: `spu94_get_total_latency_samples()` reports 58 (FIR) + 28 (ADPCM) when enabled, 58 when disabled.
- [x] **ADPCM-INT-04**: ADPCM state is zeroed by `spu94_init` and `spu94_reset`. Mid-stream toggle discards partial accumulation buffer (silence gap is inaudible at 28 samples).
- [x] **ADPCM-INT-05**: ADPCM is off by default. All existing tests pass unchanged. No modification to the reverb network, FIR chain, presets, or registers. `spu94_state` growth stays within `SPU94_STATE_SIZE_MAX` (16384 bytes).
- [x] **ADPCM-INT-06**: Existing rt_safety gates (no heap, no locks, no syscalls, bounded latency) pass with ADPCM code linked into `libspu94.so`.

### ADPCM-IO — CLI + Python + Standalone

- [x] **ADPCM-IO-01**: CLI gains `spu94 adpcm-encode` (WAV→VAG), `spu94 adpcm-decode` (VAG→WAV), and `spu94 adpcm-roundtrip` (WAV→ADPCM→WAV) subcommands. *(Plan 03-01)*
- [x] **ADPCM-IO-02**: CLI gains `--adpcm` flag for the existing reverb processing mode, enabling the ADPCM coloration stage before reverb. *(Plan 03-01)*
- [x] **ADPCM-IO-03**: VAG file reader parses the 48-byte big-endian header (magic, version, sample rate, data size) using explicit byte-order conversion (no `ntohl`). Accepts any version on read. Handles terminator blocks. *(Plan 03-01)*
- [x] **ADPCM-IO-04**: VAG file writer produces valid VAG v2 files (mono, big-endian header). Zero-pads final block to 28 samples and sets end flag. *(Plan 03-01)*
- [x] **ADPCM-IO-05**: Python ctypes bindings expose `spu94_adpcm_decode_block()`, `spu94_adpcm_encode_block()`, `spu94_set_adpcm_enabled()`, and `spu94_get_adpcm_enabled()`. *(Plan 03-03)*
- [x] **ADPCM-IO-06**: JUCE standalone gains an "ADPCM" toggle in the GUI that enables/disables the coloration stage during playback. *(Plan 03-02)*

### ADPCM-TEST — Verification

- [x] **ADPCM-TEST-01**: Known-vector decode tests cover: all-zero block, single-impulse, each filter (0-4) with known state, shift 0/6/12, shift 13/14/15, clamp-triggering overflow, two consecutive blocks verifying state carry. *(Plan 04-01)*
- [x] **ADPCM-TEST-02**: Round-trip test: encode→decode is deterministic and produces bit-identical output across runs. Decode of the encode matches standalone decode sample-for-sample. *(Plan 04-01)*
- [x] **ADPCM-TEST-03**: ADPCM golden files committed (reverb output with ADPCM on vs off, for at least 3 presets × 2 inputs), with SHA-256 sidecars and regression gate. *(Plan 04-02)*
- [x] **ADPCM-TEST-04**: Gray-area resolutions documented in `docs/DECISIONS.md` as numbered ADRs: rounding vs truncation, shift 13-15 policy, filter 5-7 policy, division semantics (>>6 vs /64), encoder error metric, encoder tiebreaking, tail block padding. *(Plan 04-03)*

## Future Scope (documented in research, NOT built in M2)

### Creative Exploitation (post-M2, informed by M2 listening evidence)

- Filter mask / exclude / bias / lock as encoder parameter
- Continuous K0/K1 coefficient modulation at the decoder (hero feature — turns ADPCM predictor into resonant filter with unique character)
- Cross-codec encode/decode (PS1 ADPCM ↔ SNES BRR)
- Asymmetric half-codec processing (encode-only, decode-only)
- Real-time filter selection modulation via LFO / envelope / random
- Per-channel filter splitting (different filters L vs R)

### Digital Patina Engine (broader codec collection, post-M2)

- Tier 1 (days each): SNES BRR, G.711 mu-law/A-law, IMA-ADPCM
- Tier 2 (days each): CVSD, OKI arcade ADPCM, Comrex frequency extender, GSM 06.10
- Tier 3 (weeks): ATRAC1/MiniDisc, MP2
- Codec chaining and feedback loops
- `src/patina/<codec>/` module architecture with independent per-codec APIs

Research artifacts: `.planning/research/m2-adpcm/CODEC-SURVEY.md`, `CREATIVE-EXPLOITATION.md`, `COMREX-FREQUENCY-EXTENDER.md`, `MODULE-ARCHITECTURE.md`

## Validated — Previous Milestones

### v1.0 Standalone GUI (Shipped 2026-04-26, Phase 8)

STANDALONE-01..09 validated. See `.planning/phases/08-m4-juce-plugin-product-v1-0/08-VERIFICATION.md`.

### M1 Reverb Core (Shipped 2026-04-25, tag `m1-reverb-core`)

All 49 M1 requirements validated through phases 1-7. See PROJECT.md "Validated" section. The full archived REQUIREMENTS.md is at `.planning/milestones/v1.0-REQUIREMENTS.md`.

Categories shipped: CORE-01..10, API-01..09, PYBIND-01..06, CLI-01..04, TEST-01..08, BUILD-01..08, DOCS-01..05.

## Out of Scope (project-wide)

- SPU voice engine, envelope generation, pitch modulation, noise, ADSR (reverb-only reimplementation)
- Gaussian interpolation (voice pitch engine, different subsystem)
- XA-ADPCM variant (CD subsystem, different format)
- SPU RAM simulation (zero audio benefit)
- Reading GPL emulator source as primary development activity (licensing posture)
- Noise shaping / multi-pass encoding (exceeds PS1 quality — future creative scope, not M2)

## Traceability

| REQ-ID | Phase | Status |
|--------|-------|--------|
| CORE-01..10 | v1.0 Phases 1-5 | Validated |
| API-01..09 | v1.0 Phases 2-5 | Validated |
| PYBIND-01..06 | v1.0 Phase 6 | Validated |
| CLI-01..04 | v1.0 Phase 6 | Validated |
| TEST-01..08 | v1.0 Phase 7 | Validated |
| BUILD-01..08 | v1.0 Phases 1, 6, 7 | Validated |
| DOCS-01..05 | v1.0 Phases 1, 6, 7 | Validated |
| STANDALONE-01..09 | v1.0 Phase 8 | Validated |
| ADPCM-01 | M2 Phase 1 | Complete (01-01) |
| ADPCM-02 | M2 Phase 1 | Complete (01-01) |
| ADPCM-03 | M2 Phase 1 | Complete (01-01) |
| ADPCM-04 | M2 Phase 1 | Complete (01-02) |
| ADPCM-05 | M2 Phase 1 | Complete (01-02) |
| ADPCM-06 | M2 Phase 1 | Complete (01-02) |
| ADPCM-07 | M2 Phase 1 | Complete (01-01 + 01-02) |
| ADPCM-INT-01 | M2 Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-02 | M2 Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-03 | M2 Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-04 | M2 Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-05 | M2 Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-06 | M2 Phase 2 | Complete (02-01, 02-02) |
| ADPCM-IO-01 | M2 Phase 3 | Complete |
| ADPCM-IO-02 | M2 Phase 3 | Complete |
| ADPCM-IO-03 | M2 Phase 3 | Complete |
| ADPCM-IO-04 | M2 Phase 3 | Complete |
| ADPCM-IO-05 | M2 Phase 3 | Complete |
| ADPCM-IO-06 | M2 Phase 3 | Complete |
| ADPCM-TEST-01 | M2 Phase 4 | Complete (04-01) |
| ADPCM-TEST-02 | M2 Phase 4 | Complete (04-01) |
| ADPCM-TEST-03 | M2 Phase 4 | Complete (04-02) |
| ADPCM-TEST-04 | M2 Phase 4 | Complete (04-03) |

---
*Requirements scoped: 2026-04-26. 23 active requirements across 4 categories. Research basis: 12 documents in `.planning/research/m2-adpcm/`.*
