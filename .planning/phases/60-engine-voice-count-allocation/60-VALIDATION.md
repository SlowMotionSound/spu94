---
phase: 60
slug: engine-voice-count-allocation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-30
---

# Phase 60 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from 60-RESEARCH.md § Validation Architecture (HIGH confidence; observables verified in-tree).

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | JUCE console-app test (headless `SPU94AudioProcessor`) run via CTest. Modeled on `tests/plugin/test_mono_sum.cpp` + `test_state_roundtrip.cpp`. |
| **Config file** | `tests/plugin/CMakeLists.txt` — add a `juce_add_console_app(test_voice_alloc)` block (copy the `test_mono_sum` block; link `spu94_static`, the processor sources, `juce::juce_audio_utils`). |
| **Quick run command** | `ctest --test-dir <build> -R voice_alloc -V` |
| **Full suite command** | `ctest --test-dir <build> --output-on-failure` |
| **Estimated runtime** | < 1 second (5 allocation cases, no audio render); full suite a few seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir <build> -R voice_alloc -V`
- **After every plan wave:** Run `ctest --test-dir <build> --output-on-failure` (includes rt_safety + C unit)
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** ~1 second (quick), ~seconds (full)

---

## Per-Task Verification Map

> Task/Plan/Wave IDs assigned by the planner; the Requirement → Test mapping below is fixed by research.
> The observable for every case is the mixer's synchronously-set `pending_kon` / `pending_koff` bitmasks
> (`spu94_get_voice_mixer()`), read before any tick — no audio render needed.

| Task ID | Plan | Wave | Requirement | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------------|-----------|-------------------|-------------|--------|
| TBD (planner) | TBD | TBD | VCOUNT-02 | N/A | unit | `ctest -R voice_alloc_mono_vs_poly -V` | ❌ W0 | ⬜ pending |
| TBD (planner) | TBD | TBD | VALLOC-01 | N/A | unit | `ctest -R voice_alloc_only_active -V` | ❌ W0 | ⬜ pending |
| TBD (planner) | TBD | TBD | VALLOC-02 | N/A | unit | `ctest -R voice_alloc_steal_oldest -V` | ❌ W0 | ⬜ pending |
| TBD (planner) | TBD | TBD | VALLOC-03 | N/A | unit | `ctest -R voice_alloc_mono_takeover -V` | ❌ W0 | ⬜ pending |
| TBD (planner) | TBD | TBD | (regression: default-24) | N/A | unit | `ctest -R voice_alloc_default24_regression -V` | ❌ W0 | ⬜ pending |

**Behavior detail per case (from research):**
- **VCOUNT-02** — count=1 ⇒ all note-ons land on voice 0 (mono); count=N ⇒ allocations spread across voices `0..N-1` (poly).
- **VALLOC-01** — count=N, allocate 8 distinct notes; assert every keyed-on bit is in `[0,N)` (none ≥ N).
- **VALLOC-02** — count=N, play N+1 simultaneous notes; the (N+1)th sets `pending_kon` bit 0 AND `pending_koff` bit 0 (least-recently-allocated reused = round-robin steal).
- **VALLOC-03** — count=1; two successive note-ons both return voice 0; 2nd call sets `pending_koff` bit 0 (prior note taken over).
- **Regression** — count=24; 24 note-ons → voices `0..23` in order; 25th wraps to 0 and key-offs voice 0's note. Must be identical pre- and post-edit (there is no existing 24-voice guard — this case becomes it).

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/plugin/test_voice_alloc.cpp` — covers VCOUNT-02, VALLOC-01/02/03 + the default-24 regression case
- [ ] `tests/plugin/CMakeLists.txt` — add `juce_add_console_app(test_voice_alloc)` + `add_test(NAME voice_alloc ...)` (copy the `test_mono_sum` block and rename)
- [ ] Test seam in `src/plugin/PluginProcessor.h` — `friend struct VoiceAllocTest;` (or thin public accessors) so the test reads `allocateVoice` / `findVoiceForNote` / `noteForVoice` / `nextVoice`
- [ ] No framework install needed — JUCE console-app test infra already present and built (`tests/CMakeLists.txt:27` adds `plugin` unconditionally).

**Isolation note (Pitfall 2):** the voice mixer is a process-wide singleton (`s_mixer`). Each test case must call `spu94_voice_mixer_init(spu94_get_voice_mixer())` and reset `noteForVoice[]`/`nextVoice` at its start.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Click audibility on voice steal | VALLOC-02 (deferred fade decision) | Subjective audio quality — the bitmask test proves the *allocation* is correct, but whether the hard-cut takeover clicks audibly is an ear judgment | Load a sample, set a moderate count (e.g. 3), play overlapping notes that exceed the count so voices get stolen mid-sustain; listen for pops/clicks on takeover. Decide whether a ~1–2 ms fade is worth adding (currently deferred — hard cut). |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 1s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
