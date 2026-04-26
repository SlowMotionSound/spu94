# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-26)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** M2 Phase 1 — Core Codec (ADPCM decode + encode)

## Current Position

Phase: 1 of 4 (Core Codec)
Plan: 0 of TBD in current phase
Status: Ready to plan
Last activity: 2026-04-26 — Roadmap created (4 phases, 23 requirements mapped)

Progress: [░░░░░░░░░░] 0%

## Performance Metrics

**Velocity:**
- Total plans completed: 0 (M2)
- Prior milestone: v1.0 product shipped 37 plans across 8 phases

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- M2 roadmap: 4 phases derived from 4 requirement categories (codec, integration, I/O, verification)
- ADPCM codec is a peer module — independent state, independent functions, no spu94_state dependency for core encode/decode
- Decoder first, encoder embeds internal decoder copy (critical: use reconstructed samples, not original PCM, for prediction state)
- Arithmetic: `>> 6` ASR (not `/64`) per existing ADR-0001 discipline; `+32` rounding bias is hardware-faithful
- Shift 13-15 mapped to 9 per psx-spx; filter 5-7 clamped to 4 per emulator consensus

### Blockers/Concerns

None.

### Pending Todos

None yet.

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| Cleanup | REVIEW-cli-python.md M/L/N findings | Carried from v1.0 | 2026-04-26 |
| Paperwork | Phase 6/7 Nyquist validation | Carried from v1.0 | 2026-04-26 |
| License | MIT vs Apache-2.0 pick | Carried from M1 | 2026-04-25 |

## Session Continuity

Last session: 2026-04-26
Stopped at: M2 roadmap created, ready to plan Phase 1
Resume file: None
