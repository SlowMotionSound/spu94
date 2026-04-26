---
phase: 8
slug: m4-juce-plugin-product-v1-0
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-26
---

# Phase 8 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Test framework + per-requirement map are derived from `08-RESEARCH.md § Validation Architecture`. This file is the planner-consumed contract; per-task rows fill in once plans exist.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Pre-existing **Unity** (C unit) + **pytest** (Python) + **ctest** (umbrella). Phase 8 adds: lightweight **JUCE `UnitTest`** framework (built into `juce_core`, no extra deps; integrates with ctest via a tiny test-runner binary) for JUCE-side glue; **strace-based shell tests** (`tests/rt_safety/` pattern from Phase 5) for audio-thread allocation gates. |
| **Config file** | Existing root `tests/CMakeLists.txt` adds new `tests/standalone/` subdirectory; new `tests/standalone/CMakeLists.txt` registers the JUCE UnitTest runner + the strace + Python harnesses. |
| **Quick run command** | `ctest --test-dir build -R "^standalone_" --output-on-failure` |
| **Full suite command** | `ctest --test-dir build --output-on-failure` (existing 82+ tests + new Phase 8 tests) |
| **Estimated runtime** | ~15-30 s for `^standalone_` subset; ~60-90 s for full suite (existing 82 tests pass in ~60 s today). |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build -R "^standalone_" --output-on-failure`
- **After every plan wave:** Run `ctest --test-dir build --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 30 s for the per-task quick run; 90 s for the full suite

---

## Per-Task Verification Map

> Filled by the planner once plans exist. Each task row links plan ID → wave → STANDALONE-* requirement → automated command → file-exists status. Template:

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 08-01-XX | 01 | 1 | STANDALONE-08 | (none) | Build succeeds; libspu94 unmodified | build | `cmake -B build && cmake --build build --target spu94_standalone` | ❌ W0 | ⬜ pending |
| (further rows added by planner) | | | | | | | | | |

**Requirement → Test Type → Automated Command (pre-derived from RESEARCH.md):**

| Req ID | Behavior | Test Type | Automated Command (proposed) |
|--------|----------|-----------|------------------------------|
| STANDALONE-01 | Standalone binary launches, single-window, no DAW required | smoke | run binary; observe window appears (manual + ctest run-and-exit shell wrapper that asserts non-zero process lifetime) |
| STANDALONE-02 | WAV load: any-SR / any-BD / mono-stereo → 44.1 kHz int16 stereo | unit (golden round-trip) | `ctest -R "^standalone_wav_loader_unit$"` — feed N synthetic WAVs (8/16/24/32-int/32-float bit; mono/stereo; 8/22.05/44.1/48/96 kHz), assert output exactly matches pre-computed expected int16 stereo at 44.1 kHz |
| STANDALONE-03a | Audio callback rt-safety (no allocations in `processBlock`) | rt-safety (strace) | `tests/rt_safety/test_no_syscalls_standalone.sh` — strace the standalone for N seconds of playback; assert zero `mmap` / `brk` between two audio-callback markers |
| STANDALONE-03b | Real-time playback bit-exactness vs CLI (offline render) | golden (audio compare) | `pytest tests/standalone/test_playback_matches_cli.py` — offline-render N WAVs via standalone-headless and via CLI; SHA-256 compare |
| STANDALONE-04a | All 10 presets selectable from dropdown | unit (combo-box population) | JUCE UnitTest: `combo.getNumItems() == SPU94_PRESET__COUNT && getItemText(i) == spu94_presets[i].name` for all i |
| STANDALONE-04b | Each preset produces audibly correct output vs CLI | golden | covered by STANDALONE-03b sub-cases |
| STANDALONE-04c | Preset switch during playback survives without crash | smoke (driven) | `pytest tests/standalone/test_preset_switch_robustness.py` — programmatically switch preset 100× while audio thread runs |
| STANDALONE-05a | 18 sliders present with raw register names | unit (UI structure) | JUCE UnitTest: assert slider count == 18; assert each label matches `spu94_reg_name(reg)` for the 18 viable regs from the cost table |
| STANDALONE-05b | Free-class smoothness vs sample-quantized stepping (subjective character) | manual UAT | deferred to Phase 8 `verify-work` UAT checklist |
| STANDALONE-06a | Wet/Dry extremes: 0% wet = identity (dry); 100% wet = SPU output | unit (audio compare) | `pytest tests/standalone/test_wet_dry_extremes.py` — render with knob at 0.0 / 1.0; assert output matches expected |
| STANDALONE-06b | Smooth equal-power transition between Wet/Dry midpoints | manual UAT | deferred to UAT |
| STANDALONE-07 | JUCE stock look-and-feel (no custom skin) | manual UAT (visual) | deferred to UAT |
| STANDALONE-08 | Builds reproducibly via root CMake | build | `cmake -B build && cmake --build build --target spu94_standalone` (exit 0 = pass) |
| STANDALONE-09 | App metadata reads "SPU-94" not "PSX Reverb" | unit (binary string) | `tests/standalone/test_metadata.sh` — `strings <binary> \| grep "SPU-94"` exit 0 AND `strings <binary> \| grep -i "psx reverb"` exit 1 |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

> Wave 0 = scaffolding tasks that land before the first feature task. Phase 8 Wave 0 needs:

- [ ] **Install Linux build deps** (`sudo apt install libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev libfontconfig1-dev libwebkit2gtk-4.1-dev libglu1-mesa-dev mesa-common-dev`) — hands-on walkthrough per `~/.claude/CLAUDE.md` global preference. Not a code task; planner notes as a prerequisite manual step.
- [ ] **`tests/standalone/CMakeLists.txt`** — registers JUCE UnitTest runner binary + strace shell tests + pytest harness under ctest with `^standalone_` test-name prefix
- [ ] **`tests/standalone/conftest.py`** — pytest fixtures for offline-render against both standalone (headless mode) and CLI; SHA-256 helpers; synthetic WAV generators
- [ ] **`tests/standalone/test_unit_runner.cpp`** — minimal `JUCEApplication`-derived test runner that hosts `juce::UnitTest::runAllTests()` and exits with non-zero on failure (~15 lines)
- [ ] **`tests/rt_safety/test_no_syscalls_standalone.sh`** — extends existing `tests/rt_safety/` pattern; documents the audio-callback-marker mechanism (`SPU94_PRINTF_MARKER` env var triggers single `write()` per block, easily filterable)
- [ ] **Pre-computed golden WAV corpus** — synthetic WAVs at the SR/BD/channel matrix the I/O wrapper must handle, plus their pre-computed expected outputs after I/O wrapper round-trip (for STANDALONE-02 unit test)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Free-class slider smoothness vs sample-quantized stepping is audibly distinct (smooth vs character-stepping) | STANDALONE-05b | Subjective audio quality — "audible stepping is character" is a perceptual claim | Load Hall preset; play a sustained tone or noise; twist `vIIR` (free) — listen for smooth response; twist `dCOMB1` (sample-quantized) — listen for audible stepping. Both behaviors expected. |
| Wet/Dry knob crossfades smoothly (no audible click at midpoint) | STANDALONE-06b | Subjective — equal-power crossfade quality | Play sustained tone; sweep Wet/Dry from 0 → 1 over ~5 s; listen for smooth blend without midpoint dip / click. |
| GUI uses JUCE stock look-and-feel | STANDALONE-07 | Subjective visual / structural | Open the app; visually confirm: no custom-painted backgrounds, no bespoke widgets, JUCE-default sliders / combo-boxes / buttons. |
| Each preset is "audibly correct" vs CLI | STANDALONE-04b | Subjective audio character | Load same WAV in CLI and standalone; render same preset; A/B listen. Bit-exactness is mechanizable (STANDALONE-03b); subjective audio character confirmation is human-only. |
| User can drag-and-drop a WAV onto the window (if planner ships drag-drop instead of file-picker only) | STANDALONE-02 | Drag-drop UX is JUCE built-in but worth a manual smoke | Drag a WAV onto the window; confirm it loads. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30 s (per-task quick run)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending (flips to approved YYYY-MM-DD once planner fills the per-task map and the gsd-plan-checker confirms coverage)
