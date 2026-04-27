# Roadmap: SPU-94 — M2 ADPCM Encode/Decode

**Updated:** 2026-04-27
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
**Plans**: 3 plans
Plans:
- [ ] 04-01-PLAN.md — Audit TEST-01/TEST-02 coverage, fill gaps, add coverage maps
- [ ] 04-02-PLAN.md — ADPCM golden files (30 WAV + 30 SHA-256) + regression gate
- [ ] 04-03-PLAN.md — Write ADR-0047 through ADR-0053 (7 gray-area ADRs)

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
| ADPCM-INT-01 | Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-02 | Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-03 | Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-04 | Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-05 | Phase 2 | Complete (02-01, 02-02) |
| ADPCM-INT-06 | Phase 2 | Complete (02-01, 02-02) |
| ADPCM-IO-01 | Phase 3 | Pending (03-01) |
| ADPCM-IO-02 | Phase 3 | Pending (03-01) |
| ADPCM-IO-03 | Phase 3 | Pending (03-01) |
| ADPCM-IO-04 | Phase 3 | Pending (03-01) |
| ADPCM-IO-05 | Phase 3 | Pending (03-03) |
| ADPCM-IO-06 | Phase 3 | Pending (03-02) |
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
| 3. I/O Layer | 3/3 | Complete | 2026-04-27 |
| 4. Verification + Documentation | 0/3 | Not started | - |

---

## Previous Milestones

- **v1.0 Product** (8 phases / 37 plans, shipped 2026-04-26): M1 reverb core + standalone GUI. Archived to `.planning/milestones/v1.0-product-ROADMAP.md`.
- **M1 Reverb Core** (7 phases / 33 plans, shipped 2026-04-25, tag `m1-reverb-core`): 49 requirements validated. Archived to `.planning/milestones/v1.0-ROADMAP.md`.

---
*Roadmap created: 2026-04-26. 23 requirements across 4 categories mapped to 4 phases. Granularity: standard. Research basis: 12 documents in `.planning/research/m2-adpcm/`.*
