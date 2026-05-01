# Requirements: SPU-94

**Defined:** 2026-05-01
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## v1.4 Requirements

Requirements for the Preset System milestone. Each maps to roadmap phases.

### Core API

- [ ] **PRE-01**: `spu94_preset_save` writes all register + mixer + DAC state to a caller-provided buffer in key=value text format
- [ ] **PRE-02**: `spu94_preset_load` reads a key=value text buffer and restores all register + mixer + DAC state
- [ ] **PRE-03**: Preset format includes a version header so future additions don't break old files
- [ ] **PRE-04**: Round-trip fidelity -- save then load produces bit-identical SPU state

### File I/O

- [ ] **PRE-05**: .spu94 file extension, plain text, human-readable and hand-editable

### CLI Surface

- [ ] **PRE-06**: `spu94 preset-dump` subcommand writes current state to stdout or file
- [ ] **PRE-07**: `spu94 preset-load` subcommand reads .spu94 file and applies before processing

### JUCE GUI Surface

- [ ] **PRE-08**: Save button opens file dialog, writes .spu94 file
- [ ] **PRE-09**: Load button opens file dialog, reads .spu94 file, updates all GUI controls

### Verification

- [ ] **PRE-10**: Round-trip golden test -- save, load, process, compare output to pre-save output

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| PRE-01 | Phase 13 | Pending |
| PRE-02 | Phase 13 | Pending |
| PRE-03 | Phase 13 | Pending |
| PRE-04 | Phase 13 | Pending |
| PRE-05 | Phase 13 | Pending |
| PRE-06 | Phase 14 | Pending |
| PRE-07 | Phase 14 | Pending |
| PRE-08 | Phase 14 | Pending |
| PRE-09 | Phase 14 | Pending |
| PRE-10 | Phase 15 | Pending |

## Out of Scope

- **Preset bank management** (folders, categories, search) -- future, after basic save/load proves out
- **Binary preset format** -- plain text chosen for human-readability and hand-editability
- **Preset sharing/cloud sync** -- out of scope for all milestones
- **Python binding for preset save/load** -- defer to if/when needed for tooling
