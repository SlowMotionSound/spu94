---
gsd_state_version: 1.0
milestone: v1.7
milestone_name: DAW Plugin Port
status: verified
stopped_at: "Phase 22 PLUG-15 closed via headless null-test harness; latency_comp Stage B fix lands in C core (commit fd50b1d) -- all 8 PLUG-09..16 requirements PASS"
last_updated: "2026-05-11T21:00:00Z"
last_activity: "2026-05-11 -- PLUG-15 UAT surfaced core latency-comp bug (dry path not delayed to match FIR group delay). Fix: Stage B 58-sample delay on dry+patina at master mixer, gated on latency_comp (commit fd50b1d). Headless test nulls -85 dBFS @ 44.1k and passes alignment+residual gates @ 48k."
progress:
  total_phases: 7
  completed_phases: 2
  total_plans: 2
  completed_plans: 2
  percent: 100
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-10)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** Phase 22 — src-latency-reporting

## Current Position

Phase: 22 (src-latency-reporting) — COMPLETE
Plan: 1 of 1 (executed; all 8 PLUG-09..16 requirements PASS)
Status: PLUG-15 closed via headless null-test (passes at both 44.1 kHz fast-path strict null and 48 kHz alignment-tolerance gate). Core latency-comp bug discovered during UAT and fixed (Stage B FIR-match delay on dry+ADPCM buses).
Last activity: 2026-05-11 -- core fix commits 6a99676, fd50b1d landed; ready to advance to phase 23 (bit-depth conversion)

Progress: [██████████] 100%

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
v1.7 locked decisions (as of requirements draft, 2026-05-11):

- 4 plugin formats (VST3 + AU + LV2 + CLAP) across 3 OSes (Linux + macOS + Windows); AU is macOS-only and LV2 is dropped on Windows -- 11 unique binaries total
- Bit-faithful C core (libspu94) stays untouched -- SR and bit-depth compatibility is wrapper-only
- SRC library: libsamplerate (BSD-2), Sinc Medium quality preset
- Dither at float->int16 boundary: truncate (no dither) -- period-faithful per North Star
- Source folder split: src/standalone/ -> src/plugin/; new small src/standalone/ holds testbed-only files (WavLoader)
- Channel buses: mono->mono, mono->stereo, stereo->stereo; mono duplicated into both reverb inputs (engine always stereo internally)
- 9 host-automatable parameters: Input Gain, ADPCM Send, Dry Send, Morph Position, Morph Speed, Morph Grit, Dry Level, ADPCM Level, Reverb Level
- Standalone reframed: internal dev/test tool, no longer a user deliverable from v1.7 onward
- Code signing deferred for beta -- testers click through Gatekeeper / SmartScreen; reactive re-evaluation only
- Custom plugin UI redesign out of scope -- current standalone GUI reused inside plugin window
- Beta-tester preset return-loop mechanism out of scope -- handled out-of-band
- UAT (User Acceptance Testing) DAW matrix deferred to packaging-and-testing phase

### Blockers/Concerns

None.

### Pending Todos

None.

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| Cleanup | REVIEW-cli-python.md M/L/N findings | Carried from v1.0 | 2026-04-26 |
| Paperwork | Phase 6/7 Nyquist validation | Carried from v1.0 | 2026-04-26 |
| License | MIT vs Apache-2.0 pick | Carried from M1 | 2026-04-25 |
| Archive cleanup | `.planning/milestones/v1.6-phases/` currently contains the abandoned master-branch macro work, not the live v1.6 user-waypoint phases; live phases 10-20 (v1.3 through v1.6) sit in `.planning/phases/` rather than archived. Separate cleanup pass. | Flagged | 2026-05-10 |
| UI gate | Hide preset Save/Load buttons in plugin formats — wrap `addAndMakeVisible(savePresetButton/loadPresetButton)` in PluginEditor.cpp in the existing `wrapperType == wrapperType_Standalone` check (same pattern as PLUG-49 WAV-loader gate). | Flagged during phase 22 UAT | 2026-05-11 |
| Bug | Plugin GUI loses parameter state when window is closed and reopened (LV2 in Ardour 8.12). Params live in the processor; editor should reflect existing state on construction, not reset. Likely missing APVTS attachments or editor reads defaults instead of current values. Repro: open plugin → change Dry/Reverb/DAC → close plugin window → reopen → values reverted to defaults. Audio thread state may or may not match GUI; needs investigation. | Flagged during phase 22 UAT | 2026-05-11 |

## Deferred Ideas

| Category | Item | Deferred At |
|----------|------|-------------|
| UI Enhancement | ADPCM filter pair LED indicators | 2026-04-29 |
| Performance | Memory Flush button -- instant spu94_reset | 2026-04-29 |
| Performance | Smooth Memory Drain -- gradual buffer decay | 2026-04-29 |
| Performance | Parameter Slew Control | 2026-04-29 |
| UI Enhancement | Stereo Link toggle -- lock/unlock L/R values | 2026-04-29 |
| ADPCM Creative | ADPCM filter pair manual override | 2026-04-29 |
| Distribution | Visual signal flow diagram as GUI | 2026-04-29 |
| Feature | Continuous oversampling sweep (luxury) | 2026-04-29 |
| Performance | Buffer Base step-lock | 2026-05-01 |
| Performance | Resistance to Feedback meta-control | 2026-05-01 |
| Creative | Codec re-sync effect | 2026-05-01 |
| Experiment | Independent clamping (ratio drift) | 2026-05-03 |
| Visualization | Real-time room geometry visualizer | 2026-05-03 |
| Visualization | Polytope room shapes | 2026-05-03 |
| Documentation | Register reference manual | 2026-05-03 |
| Idea | Unified Morph Control -- merge Morph Speed + Morph Grit into one encoder | 2026-05-10 |

## Performance Metrics

| Phase | Plan | Duration | Tasks | Files |
|-------|------|----------|-------|-------|
| 18 | 01 | retroactive | 4 | 4 |
| 19 | 01 | retroactive | 4 | 4 |
| 19 | 02 | retroactive | 8 | 9 |
| 20 | 01 | retroactive | 4 | 3 |
| 17 | 02 | (v1.5)   | -   | -   |
| 16 | 01 | 50m 19s | 2 | 6 |

## Session Continuity

Last session: 2026-05-11T20:25:00Z
Stopped at: Phase 22 execute complete. SrcChain + libsamplerate FetchContent + ScopedNoDenormals + setLatencySamples landed on main; all 4 Linux plugin targets build green; verifier confirmed 7/8 PLUG requirements PASS. PLUG-15 (Ardour null-test residual ≤-60 dBFS RMS at 48 kHz host SR) requires Anthony to drive Ardour interactively — procedure documented in 22-PLAN-SUMMARY.md Verification section. After UAT clears PLUG-15, run state.complete-phase to close phase 22 and advance to phase 23.
Resume file: .planning/phases/22-src-latency-reporting/22-VERIFICATION.md (verdict + PLUG-15 procedure)
