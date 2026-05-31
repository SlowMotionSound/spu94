---
phase: 61-coherent-controls
plan: 01
subsystem: testing
tags: [juce, ctest, friend-seam, tdd, voice-mixer, q15, headless-processor]

# Dependency graph
requires:
  - phase: 60-engine-voice-count-allocation
    provides: "std::atomic<int> activeVoiceCount{24} (the fan-out bound) + friend-seam headless-processor test pattern (test_voice_alloc.cpp)"
provides:
  - "friend struct VoiceControlsTest access seam in PluginProcessor.h"
  - "int16_t noteVelocity[24] audio-thread-only velocity-retention array (D-01 storage home)"
  - "void applyContinuousVoiceControls() private method signature + linkable no-op stub"
  - "tests/plugin/test_voice_controls.cpp — 8 headless processor cases (6 RED + 2 guards)"
  - "test_voice_controls CTest target wired in tests/plugin/CMakeLists.txt"
  - "The documented RED baseline (6 failing cases) that Plan 02 must flip to all-green"
affects: [61-02, plan-02-green-fan-out, coherent-controls]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Headless SPU94AudioProcessor test via friend struct forwarders observing spu94_get_voice_mixer() state directly (no audio render) — cloned 1:1 from Phase 60"
    - "RED half of a RED->GREEN TDD pair: a no-op stub resolves the symbol so the target LINKS while count-sensitive assertions FAIL"

key-files:
  created:
    - tests/plugin/test_voice_controls.cpp
    - .planning/phases/61-coherent-controls/deferred-items.md
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/plugin/CMakeLists.txt

key-decisions:
  - "adsr_shared guard asserts on pending_config[v].adsr (key_on copies *adsr_cfg there, spu94_voice.c:461) instead of running a mixer tick — keeps the 'no audio render' invariant"
  - "out_of_range_untouched made count-sensitive RED by also asserting in-range voices [0,3) DO scale to the new lower Level; the not-yet-applied sentinel is the 0x3FFF init ceiling, NOT zero"

patterns-established:
  - "Pattern: velocity-retention via non-atomic int16_t[24] (duckOrigLevel precedent) declared in Plan 01, filled in Plan 02"

requirements-completed: []  # RED scaffold only — VCTRL-01/02/03 are PINNED here but SATISFIED by Plan 02 (GREEN). Left Pending in REQUIREMENTS.md until the fan-out lands.

# Metrics
duration: 51min
completed: 2026-05-31
---

# Phase 61 Plan 01: Coherent Controls Test Scaffold (RED) Summary

**RED half of a RED->GREEN TDD pair: declares the VoiceControlsTest friend seam, the noteVelocity[24] velocity-retention array, and a no-op applyContinuousVoiceControls() stub, plus 8 headless processor cases whose 6 count-sensitive members FAIL against the stub — the documented baseline Plan 02 flips to green.**

## Performance

- **Duration:** 51 min (≈18 min of it the 1091 s full-suite regression run)
- **Started:** 2026-05-31T01:51:37Z
- **Completed:** 2026-05-31T02:43:32Z
- **Tasks:** 3
- **Files modified:** 5 (2 created, 3 modified)

## Accomplishments
- Declared the Phase 61 contracts Plan 02 implements against: `friend struct VoiceControlsTest`, `int16_t noteVelocity[24]`, and `void applyContinuousVoiceControls();` — with a linkable empty stub so the test target builds.
- Wrote `tests/plugin/test_voice_controls.cpp` (444 lines), an 8-case headless processor harness cloned 1:1 from Phase 60's `test_voice_alloc.cpp`, observing `spu94_get_voice_mixer()->voices[v]` state directly with no audio render.
- Registered the `test_voice_controls` CTest target with 8 `add_test` entries; confirmed the correct RED signature (6 count-sensitive RED, 2 guards pass) and verified no pre-existing test target regressed from the header change.

## The Documented RED Baseline (what Plan 02 must flip to green)

Run: `ctest --test-dir build -R voice_controls --output-on-failure` → **25% passed, 6 of 8 failed.**

| # | Case (argv selector) | Req / Decision | Pre-impl result | Why |
|---|----------------------|----------------|-----------------|-----|
| 126 | `voice_controls_level_all_active` | VCTRL-01 | **RED** | base_vol stays at init ceiling 0x3FFF > Level 0x2000 — no-op never scaled |
| 127 | `voice_controls_pan_all_active` | VCTRL-02 | **RED** | base_vol_l == base_vol_r == 0x3FFF — pan asymmetry (L>R) never applied |
| 128 | `voice_controls_non_pmon_all_active` | VCTRL-03 (toggles) | **RED** | non_flags / pmon_flags stay 0x0 — no-op never set the per-voice bits |
| 129 | `voice_controls_adsr_shared` | VCTRL-03 (ADSR) / D-07 | **PASS (guard)** | ADSR already fans out via key_on cfg — regression guard, no new wiring |
| 130 | `voice_controls_velocity_rides_level` | D-01 | **RED** | both voices read 0x3FFF (flattened); harder note not louder; base_vol not recomputed as velocity x Level |
| 131 | `voice_controls_default24_regression` | D-06 | **RED** | voice 0 = 0x3FFF, expected full-vel x full-Level 0x3FFE; loop never reached voice 23 |
| 132 | `voice_controls_out_of_range_untouched` | D-06 (bound) | **RED** | in-range voices [0,3) stay 0x3FFF instead of scaling down to new Level 0x1000 |
| 133 | `voice_controls_sweep_interaction` | Pitfall 4 / A2 | **PASS (guard)** | no-op touches nothing, so sweep_l.active survives — meaningful once Plan 02 owns base_vol |

**RED set Plan 02 turns GREEN (6):** 126, 127, 128, 130, 131, 132.
**Guards that already pass (2):** 129 (`adsr_shared`), 133 (`sweep_interaction`).

The build/link is GREEN throughout — the no-op stub resolves `applyContinuousVoiceControls`; only the test ASSERTIONS are red. This is the intended RED signature, not a failure.

## Declarations Added (for Plan 02)

`src/plugin/PluginProcessor.h`:
- `friend struct VoiceControlsTest;` — immediately after `friend struct VoiceAllocTest;` (private section, ~line 288).
- `int16_t noteVelocity[24] = {};` — adjacent to `duckOrigLevel_l/r[24]` (~line 547); audio-thread-only, NOT atomic, zero-initialized.
- `void applyContinuousVoiceControls();` — near `allocateVoice` / `findVoiceForNote` (~line 571).

`src/plugin/PluginProcessor.cpp`:
- `void SPU94AudioProcessor::applyContinuousVoiceControls() { }` — empty no-op, placed just after `allocateVoice` (~line 2618), carrying `// TODO(Plan 02): replace this no-op with the real fan-out across [0, activeVoiceCount).`
- The existing voice-0-only apply block (now ~lines 866-885, `mx->voices[0].base_vol_l = ...`) is UNCHANGED — its relocation is Plan 02's job.

## Task Commits

Each task committed atomically (all `test` type — this is a test-scaffold plan):

1. **Task 1: Declare seam + velocity array + apply-method signature + no-op stub** — `8ae4358` (test)
2. **Task 2: Write the 8-case headless test file (RED vs no-op stub)** — `3718f77` (test)
3. **Task 3: Wire the CMake target + 8 add_test entries; confirm RED** — `69d48fc` (test)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` (+21) — friend seam, noteVelocity[24], method signature.
- `src/plugin/PluginProcessor.cpp` (+8) — linkable no-op stub with TODO(Plan 02).
- `tests/plugin/test_voice_controls.cpp` (+444, new) — 8-case headless friend-seam harness.
- `tests/plugin/CMakeLists.txt` (+68) — test_voice_controls target + 8 add_test entries.
- `.planning/phases/61-coherent-controls/deferred-items.md` (+22, new) — logs pre-existing out-of-scope packaging-test timeouts.

## Decisions Made
- **adsr_shared guard reads pending_config[].adsr directly.** `spu94_voice_mixer_key_on` copies `*adsr_cfg` into `pending_config[voice_idx].adsr` (spu94_voice.c:461-462). Asserting on that staged copy proves the config fans to each voice without needing a mixer tick — honoring the plan's "no audio render" constraint.
- **out_of_range_untouched given a count-sensitive in-range assertion.** The plan's literal single assertion (`baseL(5) == before`) trivially passes against a no-op (which touches nothing), which would make it a guard, not RED. To satisfy the plan's "6 count-sensitive RED" contract, the case also asserts in-range voices [0,3) scale DOWN to the new lower Level (0x1000) — RED against the no-op, GREEN once Plan 02 fans out and skips out-of-range voices.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Corrected the not-yet-applied sentinel from 0 to the 0x3FFF init ceiling**
- **Found during:** Task 3 (first RED run showed 5 failed / 3 passed, not the required 6 / 2)
- **Issue:** `voice_controls_out_of_range_untouched` PASSED when it had to be RED. The plan's prose (and my first-draft assertions) assumed `spu94_voice_init` leaves `base_vol` at 0; I verified by reading `src/spu94/spu94_voice.c` that `spu94_voice_init` actually seeds `base_vol_l/r = 0x3FFF`. My in-range "was it updated?" check used `baseL(v) == 0` as the not-updated sentinel, which never fires against the real 0x3FFF init.
- **Fix:** Reworked the in-range assertion to `baseL(v) > 0x1000 + 1` (RED while voices sit at the 0x3FFF init ceiling; GREEN once Plan 02 scales them to the lower Level). Also corrected three misleading "mixer-init 0" comments in cases 1, 2, and 6 to state the real 0x3FFF init ceiling — the assertions in those three were already correctly RED against 0x3FFF; only the comments were inaccurate.
- **Files modified:** `tests/plugin/test_voice_controls.cpp`
- **Verification:** Re-ran `ctest -R voice_controls` → exactly 6 RED + 2 guards, matching the plan's contract. Ground truth confirmed by reading the engine init code rather than assuming.
- **Committed in:** `3718f77` correction folded into the Task 2/3 sequence (the fix landed before the Task 3 commit; the test file's final state in `3718f77` already reflects it via the in-flight edit cycle).

---

**Total deviations:** 1 auto-fixed (1 bug in my own new test logic, caught by the RED-signature check).
**Impact on plan:** Necessary to produce the exact RED signature the plan requires. No production code affected; no scope creep.

## Issues Encountered
- **Full-suite regression run is slow (1091 s) and the harness auto-backgrounds long commands.** Handled by running `ctest --test-dir build` in the background and waiting on completion notifications rather than polling.
- **Two pre-existing packaging tests time out** (`test_packaging_editable_install` #101, `test_packaging_wheel_tag` #102, both 600 s Timeout). Verified these live in `tests/packaging/` (Python wheel/pip tests) with zero reference to `PluginProcessor.h` — unrelated to this plan's header change, pre-existing. Logged to `deferred-items.md` per the SCOPE BOUNDARY rule; NOT fixed.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Plan 02 (GREEN) has an unambiguous target: implement `applyContinuousVoiceControls()` to fan Pan/Level/INV (via base_vol), NON, and PMON across `[0, activeVoiceCount)`, recomputing `base_vol = q15_mul_truncate(guiVol, noteVelocity[v])` so velocity rides under Level (D-01). Flipping cases 126, 127, 128, 130, 131, 132 to green (without breaking guards 129/133) is the GREEN gate.
- The seam, the velocity array, and the stub location are all in place; Plan 02 replaces the stub body and relocates the existing voice-0 apply block.
- No blockers. Pre-existing packaging timeouts are unrelated and tracked separately.

## Self-Check: PASSED

- Files exist: `tests/plugin/test_voice_controls.cpp`, `tests/plugin/CMakeLists.txt` (modified), `src/plugin/PluginProcessor.h` (modified), `src/plugin/PluginProcessor.cpp` (modified), `.planning/phases/61-coherent-controls/61-01-SUMMARY.md`, `.../deferred-items.md` — all present.
- Header declarations present: `friend struct VoiceControlsTest`, `int16_t noteVelocity[24]`, `applyContinuousVoiceControls() { }` stub.
- Commits exist on the branch: `8ae4358`, `3718f77`, `69d48fc`.
- Target builds + links; ctest registers exactly 8 `voice_controls` cases; the 6 count-sensitive cases are RED (the required baseline); no pre-existing test target regressed (the only non-`voice_controls` failures — packaging #101/#102 — are pre-existing Python-wheel timeouts, logged as out-of-scope).

> Note: the 6 RED `voice_controls` cases are the INTENDED RED baseline of this RED->GREEN pair, not a self-check failure. Self-Check is PASSED.

---
*Phase: 61-coherent-controls*
*Completed: 2026-05-31*
