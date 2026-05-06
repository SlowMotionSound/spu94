# Requirements: v1.5 Preset Interpolation Engine

**Created:** 2026-05-05
**Milestone:** v1.5

## Interpolation Engine (C core)

- [x] **INTERP-01**: A morph position (0.0 to 1.0) maps to a specific pair of adjacent presets and a fractional distance between them
- [x] **INTERP-02**: All 30 active registers linearly interpolate between the two adjacent presets at the current morph position
- [x] **INTERP-03**: vLOUT/vROUT stay fixed at 0x7FFF, vLIN/vRIN stay fixed at 0x8000, mBASE stays fixed at 0x0000 regardless of morph position
- [x] **INTERP-04**: At each of the 9 waypoint positions, the registers exactly match the corresponding Sony factory preset (no rounding drift)
- [x] **INTERP-05**: Signed registers (v-prefix coefficients) interpolate through their signed range correctly (no unsigned wraparound)

## GUI

- [ ] **GUI-01**: A single rotary knob (250-300px diameter) is the sole control on the macro panel
- [ ] **GUI-02**: 9 equally-spaced dot markers around the knob arc indicate exact preset waypoint positions
- [ ] **GUI-03**: Turning the knob continuously updates the interpolated register values in real time (audio-rate or timer-driven)

## Future Requirements (deferred)

- Decoupled coefficient controls (independent v-prefix knobs on top of interpolated base)
- Additional morph axes (2D pad, per-category interpolation)
- Non-equal waypoint spacing (perceptual spacing)
- Custom user presets as additional waypoints
- Tempo sync integration with interpolation engine

## Out of Scope

- Individual register controls (archived with v1.5/v1.6 macro approach)
- Gang clamping / safety constraints (unnecessary — interpolation bounded by Sony presets)
- Sync/Free modal toggle (archived with v1.5/v1.6 macro approach)
- Spread/Sweep/Rotate transforms (archived with v1.5/v1.6 macro approach)

## Traceability

| REQ-ID | Phase | Plan | Status |
|--------|-------|------|--------|
| INTERP-01 | Phase 16 | 16-01 | Complete |
| INTERP-02 | Phase 16 | 16-01 | Complete |
| INTERP-03 | Phase 16 | 16-01 | Complete |
| INTERP-04 | Phase 16 | 16-01 | Complete |
| INTERP-05 | Phase 16 | 16-01 | Complete |
| GUI-01 | Phase 17 | 17-01, 17-02 | Complete |
| GUI-02 | Phase 17 | 17-01 | Complete |
| GUI-03 | Phase 17 | 17-01, 17-02 | Complete |
