---
phase: 61-coherent-controls
verified: 2026-05-31T00:00:00Z
status: human_needed
score: 4/4 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Voice-0/Trigger audition A/B at Level=100%"
    expected: "Trigger button sounds indistinguishable from a v1.11.0 build (bit-identity is auto-verified at 0x3FFE; this is the ear confirmation)"
    why_human: "Bit-identity is proven by voice_controls_default24_regression; subjective ear confirmation that no new artifact was introduced requires a listening session"
  - test: "PMON-chain character audible across the active set"
    expected: "With count>=3, PMON enabled, and a chord playing, every active voice shows the chained pitch-mod character (voice N bent by N-1)"
    why_human: "flag-set is auto-verified (voice_controls_non_pmon_all_active asserts pmon_flags bits); the audible chaining effect requires human listening to confirm"
---

# Phase 61: Coherent Controls — Verification Report

**Phase Goal:** The sampler's per-voice controls govern every active voice, so the whole rig sounds the way the controls are set — not just the first voice.
**Verified:** 2026-05-31
**Status:** human_needed
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Adjusting Level changes the loudness of every sounding voice, not just one (VCTRL-01) | VERIFIED | `voice_controls_level_all_active` PASSED: `applyContinuousVoiceControls()` loop over `[0,count)` writes `base_vol_l/r = combineVoiceVol(guiL, noteVelocity[v])` for every active voice. Old `voices[0].base_vol_l = guiVoiceVolL.load(...)` write is gone. Confirmed by grep: no voice-0-only base_vol write survives at processBlock. |
| 2 | Adjusting Pan places every sounding voice at the same stereo position (VCTRL-02) | VERIFIED | `voice_controls_pan_all_active` PASSED: loop writes L and R independently using the same signed `guiL`/`guiR` values for every voice, so all voices share the same L/R ratio. |
| 3 | The ADSR envelope applies to every triggered voice — all notes share the same envelope (VCTRL-03) | VERIFIED | `voice_controls_adsr_shared` PASSED (regression guard — ADSR shared via `buildAdsrConfig()` at every `key_on`). `voice_controls_non_pmon_all_active` PASSED: `set_non`/`set_pmon` called per voice `[0,count)` inside the loop. |
| 4 | Toggling NON, PMON, or INV changes every active voice consistently (VCTRL-03) | VERIFIED | `voice_controls_non_pmon_all_active` PASSED: `non_flags` and `pmon_flags` bits set for all voices in `[0,count)`. INV: `q15_mul_truncate(guiVol, velQ15)` preserves sign of first arg, so a negative `guiVol` (INV active) propagates to every voice's `base_vol`. Review confirmed Q15 sign handling correct (61-REVIEW.md). |

**Score:** 4/4 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/plugin/PluginProcessor.cpp` | Real `applyContinuousVoiceControls()` fan-out loop, velocity capture, range helpers | VERIFIED | Loop at line 2699: `for (int v = 0; v < count; ++v)` writing `base_vol_l/r` + `set_non`/`set_pmon`. `velToQ15` at line 66, `combineVoiceVol` at line 79. Velocity capture at note-on line 1479. Trigger seed at line 867. +103/-20 lines per SUMMARY. |
| `src/plugin/PluginProcessor.h` | `friend struct VoiceControlsTest`, `noteVelocity[24]`, `applyContinuousVoiceControls()` decl | VERIFIED | Line 292: `friend struct VoiceControlsTest;`. Line 559: `int16_t noteVelocity[24] = {};`. Line 590: `void applyContinuousVoiceControls();`. All present, non-atomic, zero-initialized. |
| `tests/plugin/test_voice_controls.cpp` | 8-case headless processor test, 220+ lines, struct VoiceControlsTest | VERIFIED | 454 lines. `struct VoiceControlsTest` present (2 occurrences). All 8 argv selector names present 24 times total (3 per case across function, dispatch, and test body). |
| `tests/plugin/CMakeLists.txt` | `test_voice_controls` target + 8 add_test entries | VERIFIED | 26 lines contain `test_voice_controls`. 8 `add_test` entries confirmed by running `ctest --test-dir build -N -R voice_controls` (registers exactly 8 tests #126-#133). |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `PluginProcessor.cpp` MIDI note-on (~line 1472) | `noteVelocity[voice]` | `noteVelocity[voice] = velToQ15(vel)` after `allocateVoice` | WIRED | Confirmed at line 1479; `combineVoiceVol` used for both L and R key_on args (lines 1483-1484). Old `int16_t vol = (vel*0x7FFF)/127` write to key_on is removed. |
| `applyContinuousVoiceControls()` | `voices[v].base_vol_l/r` | `combineVoiceVol(guiL, noteVelocity[v])` across `[0,count)` | WIRED | Confirmed at lines 2702-2703 inside the `for (int v = 0; v < count; ++v)` loop. |
| `processBlock` (~line 932) | `applyContinuousVoiceControls()` | Direct call replacing old voice-0-only block | WIRED | Confirmed at line 932; old `voices[0].base_vol_l = guiVoiceVolL.load(...)` grep returns no results — that write is gone. |

---

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `applyContinuousVoiceControls()` | `noteVelocity[v]` | Written at MIDI note-on (line 1479) and Trigger (line 867) | Yes — per-note velocity retained between note-on and apply loop | FLOWING |
| `applyContinuousVoiceControls()` | `guiVoiceVolL/R`, `guiVoiceNon`, `guiVoicePmon` | `std::atomic` GUI state, written by parameter host/UI thread | Yes — live GUI values, not constants | FLOWING |
| Loop output: `mx->voices[v].base_vol_l/r` | Consumed by C-core `spu94_voice.c` STEP 0 each tick | Engine reads `base_vol` as ceiling on same tick | Yes — real mixer state driving audio output | FLOWING |

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| 8 voice_controls cases pass | `ctest --test-dir build -R voice_controls` | 8/8 passed, 0.03 sec | PASS |
| rt_safety gates pass | `ctest --test-dir build -L rt_safety` | 6/6 passed | PASS |
| Old voice-0-only base_vol write removed | `grep "voices\[0\]\.base_vol_l = guiVoiceVolL"` in PluginProcessor.cpp | No output (line removed) | PASS |
| Fan-out loop present | `grep "for (int v = 0; v < count"` in PluginProcessor.cpp | Matched at line 2699 | PASS |
| Velocity capture at note-on | `grep "noteVelocity\[voice\]"` in PluginProcessor.cpp | Matched at line 1479 | PASS |
| Trigger seeds full velocity | `grep "noteVelocity\[0\] = 0x7FFF"` in PluginProcessor.cpp | Matched at line 867 | PASS |

---

### Probe Execution

No probe scripts declared in PLAN or conventionally present for this phase. Step 7c: SKIPPED (no probes).

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| VCTRL-01 | 61-01, 61-02 | Level control applies to all active voices | SATISFIED | `voice_controls_level_all_active` PASSED; loop writes `base_vol` for all `[0,count)` voices |
| VCTRL-02 | 61-01, 61-02 | Pan control applies to all active voices | SATISFIED | `voice_controls_pan_all_active` PASSED; same L/R ratio applied to every voice's `base_vol` |
| VCTRL-03 | 61-01, 61-02 | ADSR and per-voice toggles apply to all active voices | SATISFIED | `voice_controls_non_pmon_all_active` PASSED (NON/PMON bits set for all voices); `voice_controls_adsr_shared` PASSED (ADSR guard held) |

**Note:** REQUIREMENTS.md traceability table still shows VCTRL-01/02/03 as `[ ]` Pending even though 61-02-SUMMARY.md declares `requirements-completed: [VCTRL-01, VCTRL-02, VCTRL-03]`. This is a bookkeeping gap — the implementation and tests fully satisfy all three requirements. The checkboxes and the traceability table need to be updated to mark these complete.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| None | — | No TBD/FIXME/XXX markers found in any phase-modified file | — | — |

Code review findings (61-REVIEW.md) are all advisory. Summary:
- **WR-01** (noteVelocity not reset on note-off): latent, not audible today, Phase 62 concern. Does not break any success criterion.
- **WR-02** (duck + Level move interaction comment overclaims): comment is stronger than the code supports when Level moves during an active duck. Not an audible bug today; duck degrades gracefully. Does not break the phase goal.
- **WR-03** (global-fader-sole-authority design constraint): intentional, consistent with "coherent controls" goal. Documented constraint, not a defect.
- **IN-01 through IN-04**: informational, no correctness impact.

None of these constitute a phase-goal failure.

---

### Human Verification Required

The following two items are ear-confirmations from VALIDATION.md whose *mechanism* is already verified by automated mixer-state assertions. They do not block the automated success criteria but are required before phase sign-off.

#### 1. Voice-0/Trigger Audition Bit-Identity

**Test:** Load a sample into the sampler, set Level=100%, press the on-screen Trigger button. A/B the result against a v1.11.0 build doing the same.
**Expected:** The Trigger audition sounds indistinguishable from v1.11.0. The automated `voice_controls_default24_regression` test already verifies bit-identity (`base_vol = 0x3FFE`); this is the subjective ear confirmation that no new artifact is present.
**Why human:** Bit-level correctness is proven. The listening check confirms no unintended perceptual change slipped through alongside the correctness fix.

#### 2. PMON Chain Character Across the Active Set

**Test:** Set voice count to 3 or higher, enable PMON, play a chord with at least 3 simultaneous notes.
**Expected:** The PMON pitch-modulation chain character (voice N bent by the output of voice N-1) is audible across the whole active set, not just voice 0.
**Why human:** `voice_controls_non_pmon_all_active` verifies that `pmon_flags` bits are set for all `[0,count)` voices. The actual pitch-modulation sound requires listening: the engine's PMON behavior (voice N modulated by N-1) is only perceivable in audio.

---

### Gaps Summary

No gaps. All automated success criteria are met:
- VCTRL-01: Level fans out to every active voice via the `[0,count)` loop.
- VCTRL-02: Pan fans out identically.
- VCTRL-03: NON/PMON fan out; ADSR shared via existing key_on path.
- 8/8 voice_controls tests PASS; 6/6 rt_safety PASS.
- The old voice-0-only control writes are removed.
- Per-note velocity is captured and retained; the Level fader rides on top.
- Range reconciliation is correct: velocity-127 converges with Trigger at Level=100%.

The two human_verification items above are ear-confirmations, not blockers to the automated goal.

One bookkeeping item to address before closing the phase: REQUIREMENTS.md checkbox and traceability table for VCTRL-01/02/03 should be marked complete.

---

_Verified: 2026-05-31_
_Verifier: Claude (gsd-verifier)_
