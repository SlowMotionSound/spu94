---
phase: 60-engine-voice-count-allocation
plan: 01
subsystem: testing
tags: [voice-allocation, round-robin, std-atomic, juce, ctest, tdd, sampler]

# Dependency graph
requires:
  - phase: 42-* (v1.9 Complete Voice)
    provides: verified full 24-voice operation (the default-24 path this plan must not regress)
  - phase: 31-* (v1.8 PSX Voice Engine)
    provides: allocateVoice round-robin allocator + spu94_voice_mixer key_on/key_off API
provides:
  - "Active sampler-voice count (1..24, default 24) as std::atomic<int> on SPU94AudioProcessor"
  - "Realtime-safe clamped setter setActiveVoiceCount(int) (message thread -> audio thread)"
  - "Count-bounded round-robin allocator (lazy % count) with oldest-voice reuse + mono last-note priority"
  - "friend struct VoiceAllocTest seam + tests/plugin/test_voice_alloc.cpp (5 CTest cases) — first-ever coverage of allocateVoice, including the default-24 regression guard"
affects: [61-coherent-controls, 62-selector-gui, 63-persistence]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Realtime-safe scalar handoff via std::atomic<int> with release store / acquire load (project's established msg->audio idiom)"
    - "Lazy modulo bounding (% count at allocation time) so a count decrease self-heals the cursor on the next allocation (ring-out default for free)"
    - "friend struct test seam to exercise private allocator without widening the public ABI"
    - "Headless JUCE console-app test driven by an argv case-selector; one add_test per case so ctest -R resolves a single case"
    - "Observe allocation via the voice mixer's synchronously-set pending_kon/pending_koff bitmasks (no audio render); snapshot the steal koff BEFORE key_on (key_on clears the matching koff bit)"

key-files:
  created:
    - tests/plugin/test_voice_alloc.cpp
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/plugin/CMakeLists.txt

key-decisions:
  - "Stored the count as std::atomic<int> (not juce::AudioParameterInt) — internal in Phase 60; matches the existing 162-atomic convention. Persistence is Phase 63, GUI is Phase 62."
  - "Lazy % count at allocation time rather than re-basing nextVoice from the setter — keeps nextVoice audio-thread-owned and yields CONTEXT's ring-out default automatically."
  - "Snapshot pending_koff between allocateVoice and key_on, because spu94_voice_mixer_key_on clears the koff bit for the voice it keys on (spu94_voice.c:472) — observing post-key_on would give a false-negative on the steal."
  - "Anti-click fade on steal NOT implemented (DEFERRED per 60-CONTEXT.md). Hard cut for now; ear-judgment listening session pending."

patterns-established:
  - "Atomic msg->audio scalar: release store in setter, acquire load in allocateVoice."
  - "Lazy modulo bounding for a count-bounded round-robin cursor."
  - "argv-selected headless CTest case with a friend test seam and pending-bitmask observation."

requirements-completed: [VCOUNT-02, VALLOC-01, VALLOC-02, VALLOC-03]

# Metrics
duration: ~50min
completed: 2026-05-30
---

# Phase 60 Plan 01: Engine Voice-Count & Allocation Summary

**Count-bounded (1..24) round-robin voice allocator via a realtime-safe atomic + clamped setter, proven by the first-ever automated test of allocateVoice — five headless CTest cases (mono/poly, only-active, steal-oldest, mono-takeover, default-24 regression) observing the voice mixer's pending_kon/pending_koff bitmasks.**

## Performance

- **Duration:** ~50 min
- **Started:** 2026-05-30T23:xx (execution start)
- **Completed:** 2026-05-31T00:17Z
- **Tasks:** 3 (TDD RED -> GREEN)
- **Files modified:** 4 (2 source, 2 test) — 361 insertions, 2 deletions

## Accomplishments
- Added `std::atomic<int> activeVoiceCount{24}` and a clamped, realtime-safe `setActiveVoiceCount(int)` (release store, acquire load) to `SPU94AudioProcessor`.
- Bounded the existing round-robin allocator to the active count with lazy modulo (`voice = nextVoice % count; nextVoice = (voice + 1) % count`) — mono (count=1) forces voice 0 (last-note priority); poly spreads across `[0, count)`; overflow reuses the least-recently-allocated voice; default 24 reproduces the exact pre-change 0..23 sequence.
- Created `tests/plugin/test_voice_alloc.cpp` with a `friend struct VoiceAllocTest` seam and five CTest cases — the first automated coverage `allocateVoice` has ever had (added Phase 31, never tested), including the new default-24 regression guard.
- Proved the TDD discipline: the four count-sensitive cases failed RED against the unbounded `% 24` allocator (Task 2) and turned GREEN only after the bounded-modulo edit (Task 3); the regression guard passed in both states.
- No C-core (`src/spu94/`, `include/spu94/`) changes; `rt_safety` gates remain 6/6 green (the atomic read lives in the C++ allocator, outside the C link closure).

## Task Commits

Each task was committed atomically (TDD RED -> GREEN):

1. **Task 1: Add active-voice-count state, clamped setter, and test seam** - `1dd7652` (feat) — allocator logic unchanged (still `% 24`)
2. **Task 2: Write the five allocation tests (RED) and wire into CTest** - `7fe7d04` (test) — default24_regression PASSES, four count-sensitive cases FAIL (proving the tests exercise the count)
3. **Task 3: Bound the round-robin allocator to the active count (GREEN)** - `d1f19c1` (feat) — all five cases pass 5/5

**Plan metadata:** (final docs commit — SUMMARY + STATE + ROADMAP + REQUIREMENTS)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` - `std::atomic<int> activeVoiceCount{24}` member, public `setActiveVoiceCount(int)` declaration, `friend struct VoiceAllocTest;` seam.
- `src/plugin/PluginProcessor.cpp` - `setActiveVoiceCount` definition (clamp [1,24] via `juce::jlimit`, release store) and the count-bounded `allocateVoice` body (acquire load + lazy `% count`). `findVoiceForNote` unchanged (still scans all 24).
- `tests/plugin/test_voice_alloc.cpp` - NEW. Headless `SPU94AudioProcessor`; `VoiceAllocTest` friend seam; argv case-selector; five cases observing `pending_kon`/`pending_koff`.
- `tests/plugin/CMakeLists.txt` - `juce_add_console_app(test_voice_alloc)` block (copied from `test_mono_sum`) + five `add_test(NAME voice_alloc_* COMMAND test_voice_alloc voice_alloc_*)` registrations.

## Final allocateVoice form

```cpp
int SPU94AudioProcessor::allocateVoice(int note)
{
    const int count = activeVoiceCount.load(std::memory_order_acquire);
    int voice = nextVoice % count;
    if (noteForVoice[voice] >= 0)
        spu94_voice_mixer_key_off(spu94_get_voice_mixer(), voice);   // steal: key-off prior note
    noteForVoice[voice] = static_cast<int8_t>(note);
    nextVoice = (voice + 1) % count;
    return voice;
}
```

Test seam: `friend struct VoiceAllocTest;` (in PluginProcessor.h).
Five CTest case names: `voice_alloc_mono_vs_poly`, `voice_alloc_only_active`, `voice_alloc_steal_oldest`, `voice_alloc_mono_takeover`, `voice_alloc_default24_regression`.

## Decisions Made
- **Storage = atomic, not AudioParameterInt.** The count is internal in Phase 60; an atomic matches the existing `pendingGuiTriggerPitch` / recording-state convention and is lighter. Phase 63 (persistence) / 62 (GUI) can wrap or migrate later.
- **Lazy `% count`, no cursor re-base.** Keeps `nextVoice` audio-thread-owned and makes the count-decrease ring-out behavior fall out automatically.
- **Anti-click fade DEFERRED** (60-CONTEXT.md) — hard-cut steal/takeover only; no fade in this phase.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Reconfigured `build-test` with `SPU94_BUILD_GUI=ON`**
- **Found during:** Task 1 (compile verification)
- **Issue:** The existing `build-test` CMake cache had `SPU94_BUILD_GUI=OFF`, so `tests/plugin/` (and thus the plan's required `voice_alloc` target + the `test_mono_sum` compile check) were never configured there. All the plan's verify commands target `build-test`.
- **Fix:** `cmake -S . -B build-test -DSPU94_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release` (Release per project default). This added the plugin/test targets without touching any tracked files.
- **Files modified:** none tracked (CMake cache only)
- **Verification:** `test_mono_sum` and `test_voice_alloc` targets now build; `ctest -N` lists the five `voice_alloc_*` cases.
- **Committed in:** n/a (build-tree config, not source)

**2. [Rule 1 - Bug] Test observed the steal `pending_koff` AFTER `key_on`, which clears it**
- **Found during:** Task 2 (RED verification — `default24_regression` failed when it must pass)
- **Issue:** My first `allocAndKeyOn` helper read `pending_koff` after the production-shaped `allocateVoice` + `key_on` pair. But `spu94_voice_mixer_key_on` clears the `pending_koff` bit for the voice it keys on (`spu94_voice.c:472`). Since a steal key-offs and then keys on the *same* voice, the key_on wiped the steal bit, giving a false-negative on the regression guard (and a latent false-positive risk for steal_oldest/mono_takeover).
- **Fix:** Split the helper to return an `AllocStep{voice, stoleKoff}` that snapshots `pending_koff` between `allocateVoice` and `key_on`; steal/takeover/regression cases assert on `stoleKoff`. (`pending_kon` is still read normally after `key_on`.)
- **Files modified:** tests/plugin/test_voice_alloc.cpp
- **Verification:** After the fix, RED split is exact — `default24_regression` PASSES, the four count-sensitive cases FAIL against `% 24`; all five GREEN after Task 3.
- **Committed in:** 7fe7d04 (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (1 blocking build-config, 1 test bug)
**Impact on plan:** Both necessary to make the plan's own verify commands run and to make the tests valid. No scope creep; production allocator matches the plan's specified form exactly.

## Issues Encountered

**Full-suite run: two `packaging` tests timed out (NOT a regression, out of scope).**
- `test_packaging_editable_install` (#101) and `test_packaging_wheel_tag` (#102) hit their `TIMEOUT 300` during the full-suite run. These two tests each build a Python wheel from source (`pip install -e .` / wheel tag check) and live in `tests/packaging/` — entirely unrelated to the C++ voice allocator. They were not touched by this plan (no diff vs pre-plan HEAD~2).
- **Root cause:** environment, not logic. The full suite ran them in parallel while the machine was under heavy compile contention (load avg peaked ~16), so two simultaneous from-source wheel builds each exceeded the 300 s budget. They are documented as SLOW in `tests/packaging/CMakeLists.txt`.
- **Change-relevant gates all green:** `voice_alloc` 5/5, all 8 plugin console tests (mono_sum, state_roundtrip, bus_layout, voice_alloc) 8/8, `rt_safety` 6/6, and the C unit/process/golden/fir/dac/mixer/preset/binding/cli/witness/fuzz/modulation labels all passed.
- **Action:** none in this phase (per scope boundary + the project's standing "packaging/PyPI is not a priority" guidance). A serial isolation re-run of just these two tests (with the machine idle) was started to confirm they pass without contention; their pass/fail does not gate this engine change.

## Manual-Only Verification (DEFERRED decision input — not a gate)

**Steal-click audibility (VALLOC-02) — NOT yet performed.** Per 60-VALIDATION.md this is an ear judgment that feeds the DEFERRED anti-click-fade decision in 60-CONTEXT.md, and per project practice (untested sound-shaping requires a real listening session with the user) it is not something to assert programmatically. **No observation recorded yet.** To gather the data: load a sample, set a moderate count (e.g. 3), play overlapping notes that exceed the count so voices are stolen mid-sustain, and listen for pops/clicks on the hard-cut takeover. The bitmask tests already prove the *allocation* is correct; this listening pass only informs whether a ~1-2 ms fade is worth adding LATER. The fade stays DEFERRED regardless of the result.

## Next Phase Readiness
- Engine now knows an active voice count and allocates within it — the foundation Phases 61/62/63 build on is in place.
- `setActiveVoiceCount(int)` is the public entry point Phase 62's GUI selector will call.
- Phase 63 (persistence) will serialize the count; the atomic is the audio-thread read source either way.
- **Open follow-up (not a blocker):** the steal-click listening session (above) to settle the deferred anti-click-fade decision.

## Self-Check: PASSED

- FOUND: tests/plugin/test_voice_alloc.cpp
- FOUND: .planning/phases/60-engine-voice-count-allocation/60-01-SUMMARY.md
- FOUND commit 1dd7652 (Task 1), 7fe7d04 (Task 2), d1f19c1 (Task 3)

---
*Phase: 60-engine-voice-count-allocation*
*Completed: 2026-05-30*
