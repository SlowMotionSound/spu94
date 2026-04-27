# Roadmap: SPU-94 — M2 ADPCM Encode/Decode

**Updated:** 2026-04-26
**Milestone:** M2 — Sony 4-bit ADPCM (version tag v1.1 upon completion)
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## Overview

M2 adds bit-faithful Sony 4-bit ADPCM encode/decode to libspu94 and wires it into the reverb signal path as a toggleable coloration stage. When enabled, input PCM is encoded to ADPCM then decoded back before reaching the reverb, introducing the quantization noise and filter ringing that characterized every PS1 game's audio. The codec is ~250 lines of C, zero heap, integer-only, block-at-a-time — a peer module alongside the reverb engine. 23 requirements across 4 categories. Research complete (12 documents, HIGH confidence). Previous milestone: v1.0 product (8 phases / 37 plans shipped 2026-04-26, 82 ctest green).

## Phases

- [x] **Phase 1: Core Codec** - Standalone ADPCM decode + encode functions: 5 filters, brute-force encoder, caller-allocated state, pure C
- [x] **Phase 2: Pipeline Integration** - Wire ADPCM into spu94_process as toggleable upstream stage with latency reporting and state management (completed 2026-04-27)
- [ ] **Phase 3: I/O Layer** - CLI subcommands (encode/decode/roundtrip), VAG file format, Python bindings, JUCE GUI toggle
- [ ] **Phase 4: Verification + Documentation** - Known-vector tests, round-trip gates, golden files, gray-area ADRs

## Phase Details

### Phase 1: Core Codec
**Goal**: ADPCM decode and encode exist as standalone, tested C functions that any caller can use without touching spu94_state
**Depends on**: Nothing (builds on existing libspu94 infrastructure but does not modify it)
**Requirements**: ADPCM-01, ADPCM-02, ADPCM-03, ADPCM-04, ADPCM-05, ADPCM-06, ADPCM-07
**Success Criteria** (what must be TRUE):
  1. A caller can decode a 16-byte ADPCM block into 28 int16 samples using any of the 5 SPU filter pairs, with correct rounding (`(old*f0 + older*f1 + 32) >> 6`), correct nibble ordering (low first), correct sign extension, and shift 13-15 mapped to 9
  2. A caller can encode 28 int16 samples into a 16-byte ADPCM block where the encoder selects the optimal (filter, shift) pair via brute-force search over all 65 combinations, using reconstructed (not original) samples for prediction state
  3. Both functions use caller-allocated 4-byte state (two int16 for old/older), zero heap, integer-only arithmetic, and have no dependency on spu94_state — they compile and link independently
  4. Existing 82 ctest all pass unchanged; new codec unit tests cover each filter, shift extremes, clamp triggering, and state carry across blocks
**Plans**: 2 plans
Plans:
- [x] 01-01-PLAN.md — ADPCM decoder + filter tables + known-vector tests
- [x] 01-02-PLAN.md — ADPCM encoder with brute-force search + round-trip tests

### Phase 2: Pipeline Integration
**Goal**: Users can toggle ADPCM coloration on/off in the reverb pipeline and hear the authentic PS1 signal path character
**Depends on**: Phase 1
**Requirements**: ADPCM-INT-01, ADPCM-INT-02, ADPCM-INT-03, ADPCM-INT-04, ADPCM-INT-05, ADPCM-INT-06
**Success Criteria** (what must be TRUE):
  1. Calling `spu94_set_adpcm_enabled(state, true)` causes `spu94_process` to run input PCM through encode+decode before the FIR decimator, and `spu94_set_adpcm_enabled(state, false)` restores the original M1 signal path with zero behavioral change
  2. `spu94_get_total_latency_samples()` reports 86 (58 FIR + 28 ADPCM) when enabled and 58 when disabled
  3. ADPCM is off by default — all 84 existing tests pass with zero modification to reverb network, FIR, presets, or registers, and spu94_state stays within SPU94_STATE_SIZE_MAX (16384 bytes)
  4. All 4 rt_safety gates (no heap, no locks, no syscalls, bounded latency) pass with ADPCM code linked into libspu94.so
**Plans**: 2 plans
Plans:
- [x] 02-01-PLAN.md — ADPCM state fields, public API, process-loop integration with double-buffer
- [x] 02-02-PLAN.md — Integration tests covering toggle, latency, state management, default-off

### Phase 3: I/O Layer
**Goal**: Users can encode/decode ADPCM via CLI, Python, and JUCE standalone — making the codec accessible through every existing interface
**Depends on**: Phase 2
**Requirements**: ADPCM-IO-01, ADPCM-IO-02, ADPCM-IO-03, ADPCM-IO-04, ADPCM-IO-05, ADPCM-IO-06
**Success Criteria** (what must be TRUE):
  1. Running `spu94 adpcm-encode input.wav output.vag` produces a valid VAG v2 file, and `spu94 adpcm-decode output.vag roundtrip.wav` produces a WAV that decodes to the same samples as calling the C API directly
  2. Running `spu94 --adpcm --preset hall input.wav output.wav` processes the input through ADPCM coloration before reverb, producing audibly different (grainier) output than the same command without `--adpcm`
  3. Python callers can call `spu94_adpcm_decode_block()`, `spu94_adpcm_encode_block()`, `spu94_set_adpcm_enabled()`, and `spu94_get_adpcm_enabled()` via ctypes bindings
  4. The JUCE standalone shows an "ADPCM" toggle that enables/disables the coloration stage during real-time playback
  5. VAG reader handles big-endian headers with explicit byte-order conversion (no ntohl) and respects terminator blocks
**Plans**: TBD
Plans:
- [ ] (to be planned)
**UI hint**: yes

### Phase 4: Verification + Documentation
**Goal**: The ADPCM implementation is provably correct against known vectors, deterministic across runs, regression-gated by golden files, and every gray-area resolution is documented
**Depends on**: Phase 3
**Requirements**: ADPCM-TEST-01, ADPCM-TEST-02, ADPCM-TEST-03, ADPCM-TEST-04
**Success Criteria** (what must be TRUE):
  1. Known-vector decode tests pass for: all-zero block, single-impulse, each filter 0-4 with known state, shift 0/6/12, shift 13/14/15, clamp-triggering overflow, and two consecutive blocks verifying state carry
  2. Encode-then-decode produces bit-identical output across runs, and decode of the encode matches standalone decode sample-for-sample
  3. Golden files exist for reverb output with ADPCM on vs off (at least 3 presets x 2 inputs), with SHA-256 sidecars and a ctest regression gate that fails on any drift
  4. docs/DECISIONS.md contains numbered ADRs for: rounding vs truncation, shift 13-15 policy, filter 5-7 policy, division semantics (>>6 vs /64), encoder error metric, encoder tiebreaking, tail block padding
**Plans**: TBD
Plans:
- [ ] (to be planned)

---

## Coverage Audit

| REQ-ID | Phase | Status |
|--------|-------|--------|
| ADPCM-01 | Phase 1 | Complete (01-01) |
| ADPCM-02 | Phase 1 | Complete (01-01) |
| ADPCM-03 | Phase 1 | Complete (01-01) |
| ADPCM-04 | Phase 1 | Complete (01-02) |
| ADPCM-05 | Phase 1 | Complete (01-02) |
| ADPCM-06 | Phase 1 | Complete (01-02) |
| ADPCM-07 | Phase 1 | Complete (01-01, 01-02) |
| ADPCM-INT-01 | Phase 2 | Pending |
| ADPCM-INT-02 | Phase 2 | Pending |
| ADPCM-INT-03 | Phase 2 | Pending |
| ADPCM-INT-04 | Phase 2 | Pending |
| ADPCM-INT-05 | Phase 2 | Pending |
| ADPCM-INT-06 | Phase 2 | Pending |
| ADPCM-IO-01 | Phase 3 | Pending |
| ADPCM-IO-02 | Phase 3 | Pending |
| ADPCM-IO-03 | Phase 3 | Pending |
| ADPCM-IO-04 | Phase 3 | Pending |
| ADPCM-IO-05 | Phase 3 | Pending |
| ADPCM-IO-06 | Phase 3 | Pending |
| ADPCM-TEST-01 | Phase 4 | Pending |
| ADPCM-TEST-02 | Phase 4 | Pending |
| ADPCM-TEST-03 | Phase 4 | Pending |
| ADPCM-TEST-04 | Phase 4 | Pending |

**Mapped:** 23/23
**Unmapped:** 0
**Duplicates:** 0

---

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Core Codec | 2/2 | Complete | 2026-04-26 |
| 2. Pipeline Integration | 2/2 | Complete   | 2026-04-27 |
| 3. I/O Layer | 0/TBD | Not started | - |
| 4. Verification + Documentation | 0/TBD | Not started | - |

---

## Previous Milestones

- **v1.0 Product** (8 phases / 37 plans, shipped 2026-04-26): M1 reverb core + standalone GUI. Archived to `.planning/milestones/v1.0-product-ROADMAP.md`.
- **M1 Reverb Core** (7 phases / 33 plans, shipped 2026-04-25, tag `m1-reverb-core`): 49 requirements validated. Archived to `.planning/milestones/v1.0-ROADMAP.md`.

---
*Roadmap created: 2026-04-26. 23 requirements across 4 categories mapped to 4 phases. Granularity: standard. Research basis: 12 documents in `.planning/research/m2-adpcm/`.*
