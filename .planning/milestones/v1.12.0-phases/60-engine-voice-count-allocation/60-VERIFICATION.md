---
phase: 60-engine-voice-count-allocation
verified: 2026-05-30T00:30:00Z
status: passed
score: 7/7 must-haves verified
overrides_applied: 0
re_verification: null
gaps: []
deferred: []
human_verification:
  - test: "Steal-click audibility on voice steal (VALLOC-02)"
    expected: "No audible pop or click when a voice is stolen mid-sustain; or click is acceptable without a fade"
    why_human: "Ear judgment only — allocation correctness is proven by the bitmask tests; whether a ~1-2 ms anti-click fade is worth adding is a listening-session decision. Explicitly deferred in 60-CONTEXT.md and 60-VALIDATION.md. Not a gate."
---

# Phase 60: Engine Voice-Count & Allocation Verification Report

**Phase Goal:** The sampler engine knows how many voices are active and allocates played notes only among them, stealing the oldest-sounding (round-robin least-recently-allocated) voice when more notes play than the count allows.
**Verified:** 2026-05-30
**Status:** passed
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                                                                               | Status     | Evidence                                                                                                                                                      |
|-----|-----------------------------------------------------------------------------------------------------------------------------------------------------|------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1   | With active count = 1, every played note lands on voice 0 (last-note priority); each new note takes over voice 0.                                  | VERIFIED   | `allocateVoice`: `voice = nextVoice % count` with count=1 always yields 0. `voice_alloc_mono_vs_poly` (monoNotes loop) and `voice_alloc_mono_takeover` both PASSED (CTest run: 5/5). |
| 2   | With count = N (1 < N < 24), up to N notes allocate to distinct voices in [0, N); no note ever allocated to index >= N.                           | VERIFIED   | Lazy modulo `nextVoice % count` bounds the index. `voice_alloc_only_active` (N=5, 8 allocations, asserts `kon() & ~mask == 0`) PASSED. `voice_alloc_mono_vs_poly` poly leg (count=6, 6 distinct notes at 0..5) PASSED. |
| 3   | The (N+1)th simultaneous note steals the least-recently-allocated voice (key-offs prior, keys on new) instead of dropping or using a voice >= N.  | VERIFIED   | `allocateVoice` key-offs `noteForVoice[voice] >= 0`; `stoleKoff` snapshot taken between `alloc` and `key_on`. `voice_alloc_steal_oldest` (N=4, 5th note returns voice 0, `stoleKoff & 0x1` set) PASSED. |
| 4   | Raising count adds polyphony; lowering reduces it, up to 24 max.                                                                                   | VERIFIED   | `setActiveVoiceCount` clamps via `juce::jlimit(1, 24, n)` then `activeVoiceCount.store(n, std::memory_order_release)`. Verified in source and by the mono/poly truth above exercising count=1 and count=6/24. |
| 5   | Default count = 24 reproduces the exact pre-change 0..23 round-robin (regression guard).                                                          | VERIFIED   | `voice_alloc_default24_regression` (24 notes → voices 0..23, 25th wraps to 0 with steal) PASSED. `activeVoiceCount{24}` default confirmed in header. |
| 6   | The active count is realtime-safe (`std::atomic<int>`) and clamped to [1, 24].                                                                     | VERIFIED   | `std::atomic<int> activeVoiceCount{24}` at h:437. Setter: `n = juce::jlimit(1, 24, n); activeVoiceCount.store(n, std::memory_order_release)`. Allocator: `activeVoiceCount.load(std::memory_order_acquire)`. |
| 7   | C core (src/spu94/, include/spu94/) has no diff from phase-start commit (c299b12).                                                                 | VERIFIED   | `git diff --name-only c299b12..HEAD -- src/spu94 include/spu94` produced empty output (exit 0).                                                                |

**Score:** 7/7 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/plugin/PluginProcessor.h` | `std::atomic<int> activeVoiceCount{24}` member, `setActiveVoiceCount(int)` declaration, `friend struct VoiceAllocTest` seam | VERIFIED | All three confirmed at lines 437, 281, 287. One occurrence each (grep -c == 1). |
| `src/plugin/PluginProcessor.cpp` | `setActiveVoiceCount` definition (clamped store) and count-bounded `allocateVoice` | VERIFIED | Lines 2593-2616. `juce::jlimit(1, 24, n)` + release store at 2597-2598. Acquire load + lazy `% count` at 2609-2614. |
| `tests/plugin/test_voice_alloc.cpp` | Five CTest cases observing `pending_kon`/`pending_koff` via the friend seam | VERIFIED | 270-line file with `VoiceAllocTest` seam, `allocAndKeyOn` helper with pre-key_on `pending_koff` snapshot, five named boolean test functions, argv-selector `main()`. |
| `tests/plugin/CMakeLists.txt` | `juce_add_console_app(test_voice_alloc)` + five `add_test(NAME voice_alloc_* ...)` | VERIFIED | Lines 232-286 in CMakeLists.txt. `grep -c "add_test(NAME voice_alloc"` == 5. All five exact case names registered. |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `allocateVoice` (PluginProcessor.cpp) | `activeVoiceCount` atomic | Acquire load used as round-robin modulus | VERIFIED | `activeVoiceCount.load(std::memory_order_acquire)` at line 2609; result used as divisor in `% count` on 2610 and 2614. |
| `test_voice_alloc.cpp` | `pending_kon` / `pending_koff` | `stoleKoff = mx->pending_koff` between `alloc` and `key_on` | VERIFIED | `AllocStep` struct at lines 75-82; snapshot taken at line 92 before `key_on` at line 94-100. Pattern `pending_k(on|off)` found throughout the test. |
| `test_voice_alloc.cpp` | `allocateVoice` / `findVoiceForNote` / `setActiveVoiceCount` | `friend struct VoiceAllocTest` | VERIFIED | `struct VoiceAllocTest` at lines 45-62 calls `p.allocateVoice(note)`, `p.setActiveVoiceCount(n)`, reads `p.noteForVoice[i]` and `p.nextVoice` directly. |

---

### Data-Flow Trace (Level 4)

Not applicable — no rendering artifact. The allocator writes voice indexes to bitmasks (`pending_kon`, `pending_koff`) which the tests read directly. No component renders dynamic data from a store or fetch chain.

---

### Behavioral Spot-Checks (CTest)

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| count=1 → mono, all notes on voice 0 | `ctest --test-dir build-test -R voice_alloc_mono_vs_poly -V` | PASSED (0.00 sec) | PASS |
| count=N → no allocation at index >= N | `ctest --test-dir build-test -R voice_alloc_only_active -V` | PASSED (0.00 sec) | PASS |
| (N+1)th note steals oldest voice (koff bit 0 set) | `ctest --test-dir build-test -R voice_alloc_steal_oldest -V` | PASSED (0.00 sec) | PASS |
| count=1, note B takes over voice 0 from note A | `ctest --test-dir build-test -R voice_alloc_mono_takeover -V` | PASSED (0.00 sec) | PASS |
| Default-24 round-robin 0..23, 25th wraps to 0 | `ctest --test-dir build-test -R voice_alloc_default24_regression -V` | PASSED (0.00 sec) | PASS |

**All 5/5 cases passed. Total test time: 0.02 sec.**

---

### Probe Execution

No `probe-*.sh` scripts declared in PLAN or present in `scripts/*/tests/`. CTest cases above are the designated verification mechanism for this phase.

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| VCOUNT-02 | 60-01-PLAN.md | Setting count to 1 makes sampler monophonic; higher counts add polyphony up to 24 | SATISFIED | `voice_alloc_mono_vs_poly` PASSED; `[x]` in REQUIREMENTS.md; traceability row: Phase 60 Complete. |
| VALLOC-01 | 60-01-PLAN.md | Played notes allocated only among active voices | SATISFIED | `voice_alloc_only_active` PASSED; `[x]` in REQUIREMENTS.md; traceability row: Phase 60 Complete. |
| VALLOC-02 | 60-01-PLAN.md | More simultaneous notes than count steals oldest-sounding voice | SATISFIED | `voice_alloc_steal_oldest` PASSED; `[x]` in REQUIREMENTS.md; traceability row: Phase 60 Complete. |
| VALLOC-03 | 60-01-PLAN.md | Monophonic mode (count=1): each new note takes over the single active voice | SATISFIED | `voice_alloc_mono_takeover` PASSED; `[x]` in REQUIREMENTS.md; traceability row: Phase 60 Complete. |

No orphaned requirements for Phase 60: all four IDs declared in the PLAN are mapped in REQUIREMENTS.md with `Status: Complete`.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | — | — | No anti-patterns found in modified files. |

No `TBD`, `FIXME`, or `XXX` markers in the four modified files. No stub returns, placeholder implementations, or empty handlers. `findVoiceForNote` still scans all 24 (confirmed at cpp:2620) — correct by design (ring-out behavior for notes on now-out-of-range voices).

---

### Human Verification Required

#### 1. Steal-click audibility (VALLOC-02 ear judgment)

**Test:** Load a sample, set voice count to 3, play overlapping notes that exceed the count so voices are stolen mid-sustain.
**Expected:** Allocation is already proven correct by the bitmask tests. The question is purely whether the hard-cut takeover (key-off immediately followed by key-on on the same voice) produces an audible pop or click that would warrant a ~1–2 ms anti-click fade.
**Why human:** This is an ear judgment, not a correctness check. The anti-click fade is explicitly DEFERRED in 60-CONTEXT.md regardless of the result. The listening session only collects data for the deferred decision. Programmatic verification of perceptual audio quality is not possible.

*Note: This is a deferred decision input item, not a gate. The phase goal (allocation correctness) is fully verified. Status is `passed` because this item is explicitly documented as non-blocking in 60-CONTEXT.md and 60-VALIDATION.md.*

---

### Gaps Summary

No gaps. All seven must-have truths are VERIFIED by direct source inspection and live CTest execution (5/5 cases, 0.02 sec, exit 0). The C core is untouched. All four requirement IDs are satisfied and marked complete in REQUIREMENTS.md.

The one remaining item (steal-click listening session) is an explicitly deferred ear-judgment that feeds a future anti-click-fade decision. It is not a correctness gap and does not affect the phase goal status.

---

_Verified: 2026-05-30_
_Verifier: Claude (gsd-verifier)_
