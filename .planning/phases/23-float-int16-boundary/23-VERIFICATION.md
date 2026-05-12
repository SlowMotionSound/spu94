# Phase 23: Float↔int16 Boundary — Verification

**Verified:** 2026-05-11
**Verifier:** gsd-verifier
**Verdict:** PASSED — phase goal achieved, all PLUG-17..21 satisfied

## Summary

Phase 23 lifts the inline float↔int16 conversion (formerly in `SrcChain.cpp:45-56`) into an explicit `BoundaryConverter` module and repositions Input Gain from a post-clamp engine register to a pre-clamp float multiply with extended drive range (0.0..16.0 = +24 dB ceiling). Standalone GUI slider widened in-phase so the new range is reachable in the testbed. PLUG-17..21 all satisfied; C core untouched; side-channel limiter byte-identical (PLUG-20).

## Requirements coverage

| Requirement | Description | Status | Evidence |
|---|---|---|---|
| PLUG-17 | Host float32 → int16 via clamp + truncation, no dither | SATISFIED | `BoundaryConverter.h:49-55` — branch+static_cast, no Random / rand / noise shaping |
| PLUG-18 | sat_s16 semantics, no integer wrap | SATISFIED | Runtime corners: `+1.5f → 32767`, `-2.0f → -32768`, `+0.5f → 16384`, `-1.0f → -32768` |
| PLUG-19 | Default Input Gain = 0.5 (-6 dB) | SATISFIED | `PluginProcessor.h:156` `{0.50f}` preserved byte-identically |
| PLUG-20 | Side-channel limiter preserved unchanged | SATISFIED | `git diff` shows zero hunks on `kSideKnee` / `kSideCeiling` / `std::tanh` math; constants byte-identical |
| PLUG-21 | int16 → float by `/32768.0f` | SATISFIED | `BoundaryConverter.h:61-64`; runtime check confirms asymmetric mapping (32767 → ~0.99997) |

## User UAT outcome

Anthony approved the audible behavior — verbatim: *"This sounds great btw. listening now."*

- Default knob position (-6 dB): dry/reverb mix is indistinguishable from pre-Phase-23 behavior.
- Top of knob (~+24 dB ceiling): signals saturate cleanly into the int16 ceiling, producing the saturator/overdrive character the North Star calls for ("fixed-point quirks ARE the product").

## Plan divergences (both documented, both justifiable)

1. **D-03 register policy uniformed across both paths.** Originally split (plugin-only pre-clamp, standalone via engine register). UAT exposed an `int16_t` overflow when the atomic exceeded 1.0 on the standalone path (`static_cast<int16_t>(16.0 × 0x7FFF)` wraps). Post-UAT correction (commit `7e9bc82`): both paths apply pre-clamp float gain and pin `spu94_set_input_gain` at `0x7FFF`. Standalone WAV-read loop converts int16 → toFloat → applyInputGain → toInt16 inline.

2. **Standalone GUI slider widened in-phase.** Originally deferred to Phase 24. UAT proved the +24 dB drive zone wasn't reachable from any UI without the slider widen. Standalone is internal dev-only per v1.7, so exposing the new internal range to its slider is in-scope. Phase 24 / PLUG-28 still owns the plugin-format host-automation parameter surface (`AudioProcessorParameter`, separate code path).

## Project gates

| Gate | Status | Notes |
|---|---|---|
| C core (`c_core/`) untouched | PASS | `git diff --stat` returns empty for c_core/ |
| North Star (no dither / soft-knee / auto-makeup) | PASS | None introduced |
| RT-safety (no allocations / locks / syscalls in processBlock) | PASS | `inputGainScratch_[0/1]` allocated in `prepareToPlay`; atomic loaded once per block, not per sample |
| PLUG-20 limiter math byte-identical | PASS | git diff shows zero hunks on limiter constants or `std::tanh` math |
| Builds green (VST3 / LV2 / CLAP / Standalone) | PASS | Artifacts present at `build_test/src/plugin/spu94_plugin_artefacts/Release/` |

## Deferred verifications (acceptable)

- **Phase 22 Ardour null-test residual reproduction** at default Input Gain — same deferral as Phase 22 itself (no automated runner; hands-on UAT confirmed audible parity at default).
- **`pluginval --strictness-level 7`** — CI advisory job from Phase 21, not run locally.

## Anti-patterns scan

- Zero debt markers (TBD / FIXME / XXX) in any Phase 23 file.
- Zero `TODO` / `HACK` / `placeholder` / `coming soon` strings.
- `UAT-ONLY` comment at `PluginEditor.cpp:92-95` is intentional and matches SUMMARY's deferred item (revert when Phase 24's host-param surface lands).

## Commits

- `c0e0e0c` feat(23-01): add BoundaryConverter.h with named float<->int16 free functions
- `dc5ce32` refactor(23-01): migrate SrcChain.cpp boundary calls to BoundaryConverter
- `6084833` build(23-01): list BoundaryConverter.h in spu94_plugin sources; ignore /build_test/
- `176d2f7` docs(23-01): SUMMARY for float<->int16 boundary lift plan
- `f52993e` chore: merge executor worktree (worktree-agent-a5aa7332d8587f297)
- `6bffcb1` feat(23-02): add applyInputGain helper + document inputLevel range
- `f7f9137` feat(23-02): apply pre-clamp Input Gain on plugin path; pin engine register at unity
- `7e9bc82` fix(23-02): extend pre-clamp gain to standalone path; expose new range in standalone slider
- `9705565` docs(23-02): SUMMARY for pre-clamp Input Gain + extended drive range
- `4960361` chore: merge executor worktree (worktree-agent-aea8a33fa0754155f)

---

*Phase 23 closed 2026-05-11. Next: Phase 24 — State & Automation Surface (binary-wrapped .spu94 state round-trip; 9 host-automatable AudioProcessorParameters).*
