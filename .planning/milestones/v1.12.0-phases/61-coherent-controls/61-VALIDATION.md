---
phase: 61
slug: coherent-controls
status: complete
nyquist_compliant: true
wave_0_complete: true
created: 2026-05-30
completed: 2026-05-31
---

# Phase 61 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | JUCE console-app harness (plugin layer, CTest) — same harness as Phase 60 `test_voice_alloc` |
| **Config file** | `tests/plugin/CMakeLists.txt` (the `test_voice_controls` target is added in Plan 01, Wave 0) |
| **Quick run command** | `ctest --test-dir build -R voice_controls --output-on-failure` |
| **Full suite command** | `ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~20 seconds (8 headless processor cases; no audio render) |

> Build note: configure once with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`,
> then `cmake --build build --target test_voice_controls` before running ctest.
> The build dir is not pre-populated with this target until Plan 01 lands.

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build -R voice_controls --output-on-failure`
- **After every plan wave:** Run `ctest --test-dir build --output-on-failure` (full suite incl. `voice_alloc_*`, `voice_tick_unit`, `adsr_unit`, rt_safety)
- **Before `/gsd:verify-work`:** Full suite green AND `ctest --test-dir build -L rt_safety --output-on-failure` green
- **Max feedback latency:** 20 seconds (quick), ~90 seconds (full suite)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 61-01-01 | 01 | 1 | VCTRL-01/02/03, D-01, D-06 | — | N/A (host glue; no trust boundary) | unit (RED) | `ctest --test-dir build -R voice_controls --output-on-failure` (cases compile + FAIL pre-impl) | ✅ | ✅ green (RED baseline 6/8 as designed) |
| 61-02-01 | 02 | 2 | VCTRL-01, VCTRL-02, D-01 | — | N/A | unit | `ctest --test-dir build -R "voice_controls_(level_all_active|pan_all_active|velocity_rides_level)" --output-on-failure` | ✅ | ✅ green |
| 61-02-02 | 02 | 2 | VCTRL-03, D-07 | — | N/A | unit | `ctest --test-dir build -R "voice_controls_(non_pmon_all_active|adsr_shared)" --output-on-failure` | ✅ | ✅ green |
| 61-02-03 | 02 | 2 | D-06, Pitfall 4 | — | N/A | unit + regression | `ctest --test-dir build -R "voice_controls_(default24_regression|out_of_range_untouched|sweep_interaction)" --output-on-failure` then full suite | ✅ | ✅ green (8/8 + full suite 131/131 + rt_safety 6/6) |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Requirement → Case Map (8 cases, all in `tests/plugin/test_voice_controls.cpp`)

| Case (argv selector) | Requirement / Decision | Asserts |
|----------------------|------------------------|---------|
| `voice_controls_level_all_active` | VCTRL-01 | After `applyContinuousVoiceControls()`, every voice in `[0,count)` has scaled `base_vol_l/r` (non-zero, ≤ Level ceiling); not just voice 0 |
| `voice_controls_pan_all_active` | VCTRL-02 | A pan-left GUI value yields the SAME L≠R ratio on every active voice's `base_vol_l` vs `base_vol_r` |
| `voice_controls_non_pmon_all_active` | VCTRL-03 (toggles) | `non_flags` and `pmon_flags` have bits `[0,count)` set after apply (PMON bit 0 ignored per engine rule) |
| `voice_controls_adsr_shared` | VCTRL-03 (ADSR), D-07 | Two MIDI key-ons at different voices carry the same `buildAdsrConfig()` shape (regression guard — no new wiring) |
| `voice_controls_velocity_rides_level` | D-01 | Two voices keyed at vel 40 vs vel 120, same Level: `base_vol` differ AND scale with velocity; flat-overwrite would make them equal (the Pitfall-1 trap) |
| `voice_controls_default24_regression` | D-06 regression | At count=24, loop reaches all 24; voice-0 base_vol from a full-velocity Trigger seed is bit-identical to the v1.11.0 voice-0 path |
| `voice_controls_out_of_range_untouched` | D-06 bound | A voice keyed on, then count lowered below its index: apply leaves that voice's `base_vol` at its key-on value (no control update) |
| `voice_controls_sweep_interaction` | Pitfall 4 (A2) | A voice with an active L-sweep keeps the sweep `active` flag after Level moves; `base_vol` re-bases as the ceiling without resetting the sweep state machine |

---

## Wave 0 Requirements

- [ ] `tests/plugin/test_voice_controls.cpp` — NEW headless processor test, 8 cases above; cloned 1:1 from `tests/plugin/test_voice_alloc.cpp` structure (friend struct, `isolate()`, argv selector, `ScopedJuceInitialiser_GUI`)
- [ ] `tests/plugin/CMakeLists.txt` — add `test_voice_controls` target + one `add_test` per case (clone the `test_voice_alloc` block, lines 228-287)
- [ ] `src/plugin/PluginProcessor.h` — declare `friend struct VoiceControlsTest;` (alongside `friend struct VoiceAllocTest;`), `void applyContinuousVoiceControls();`, and `int16_t noteVelocity[24];`
- Framework install: NONE — all harness deps (JUCE console-app, libsamplerate, spu94_static) already build for `test_voice_alloc`.

*Wave 0 is delivered by Plan 01.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Subjective "no audible change at count 24 vs v1.11.0 for the voice-0/Trigger audition" | D-06 | Bit-identity is auto-checked; the *ear* confirmation that the Trigger button still sounds the same is a listening check | Load v1.11.0-style sample, set Level=100%, press on-screen Trigger; A/B against a v1.11.0 build — voice-0 audition should be indistinguishable |
| Subjective PMON-chain character across the active set | VCTRL-03 | The faithful voice-N-bent-by-N-1 chain is a *sound* consequence (CONTEXT specifics); flag-set is auto-checked, the sound is ear-confirmed | Set count≥3, enable PMON, play a chord — confirm the chained pitch-mod character (already surfaced to the user as a known consequence) |

> These are ear-confirmations of behaviors whose *mechanism* is already covered by automated mixer-state assertions. They do not block the phase gate; they are UAT items.

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references (test file + CMake + header seam — delivered by Plan 01)
- [x] No watch-mode flags
- [x] Feedback latency < 90s (quick voice_controls run; full suite ~18min due to plug15_null_passthrough_48k render)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** green — all automated cases passed (voice_controls 8/8, full suite 131/131, rt_safety 6/6). 2 manual ear-confirmations remain as non-blocking UAT.
