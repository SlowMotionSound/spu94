# Requirements: v1.5 Preset Interpolation Engine

**Created:** 2026-05-05
**Milestone:** v1.5

## Interpolation Engine (C core)

- [ ] **INTERP-01**: A morph position (0.0 to 1.0) maps to a specific pair of adjacent presets and a fractional distance between them
- [ ] **INTERP-02**: All 30 active registers linearly interpolate between the two adjacent presets at the current morph position
- [ ] **INTERP-03**: vLOUT/vROUT stay fixed at 0x7FFF, vLIN/vRIN stay fixed at 0x8000, mBASE stays fixed at 0x0000 regardless of morph position
- [ ] **INTERP-04**: At each of the 9 waypoint positions, the registers exactly match the corresponding Sony factory preset (no rounding drift)
- [ ] **INTERP-05**: Signed registers (v-prefix coefficients) interpolate through their signed range correctly (no unsigned wraparound)

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
| INTERP-01 | | | Pending |
| INTERP-02 | | | Pending |
| INTERP-03 | | | Pending |
| INTERP-04 | | | Pending |
| INTERP-05 | | | Pending |
| GUI-01 | | | Pending |
| GUI-02 | | | Pending |
| GUI-03 | | | Pending |
