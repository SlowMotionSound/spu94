# Phase 60: Engine Voice-Count & Allocation - Research

**Researched:** 2026-05-30
**Domain:** Real-time voice allocation in a JUCE audio plugin (C++ processor over a C DSP core)
**Confidence:** HIGH (all findings verified by direct codebase inspection; no external libraries introduced)

## Summary

Phase 60 is a small, localized, **logic-only** change with **no new DSP and no new dependencies**. The
allocator (`SPU94AudioProcessor::allocateVoice`) is a pure index-bookkeeping method: it picks
`nextVoice`, key-offs whatever note was already on that voice, records `noteForVoice[voice]=note`, and
advances `nextVoice = (nextVoice + 1) % 24`. The only substantive edits are (1) introduce an
active-voice-count member, (2) replace the two hardcoded `% 24` / `< 24` literals with the count, and
(3) add a realtime-safe setter. The steal/takeover behavior the requirements ask for (VALLOC-02,
VALLOC-03) **already exists** as the key-off-then-key-on on a reused voice — bounding the round-robin
modulus is the entire feature.

The single highest-value research output is the **test strategy**, because every success criterion is
behavioral and there is currently **zero test coverage** of `allocateVoice` (added in Phase 31, never
touched since — there is no existing regression guard for the default-24 path; Phase 60 must create it).
The decisive finding: `spu94_voice_mixer_key_on` immediately sets two directly-inspectable bitmasks —
`pending_kon` and `pending_koff` — **before any audio tick runs**. A test can drive note-on/off and read
`spu94_get_voice_mixer()->pending_kon` / `->pending_koff` to assert *exactly which voice index* was
allocated or stolen, with no audio rendering, no WAV fixture, and no settling. This makes all four
criteria fully deterministic.

**Primary recommendation:** Store the active count as `std::atomic<int> activeVoiceCount{24}` (the
codebase's overwhelming msg→audio convention — 162 atomics, acquire/release ordering), bound the
round-robin with lazy `% count` at allocation time (no cursor re-base needed — CONTEXT's ring-out
default falls out for free), and add a JUCE console-app test in `tests/plugin/` modeled on
`test_mono_sum.cpp` that asserts allocation via the mixer's pending bitmasks. Expose `allocateVoice` /
`findVoiceForNote` / count + cursor to the test via a friend declaration or a thin public accessor
(they are currently `private`).

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Active-voice-count state + setter | C++ PluginProcessor | — | Allocation is a host-side scheduling concern; the C core has no notion of MIDI notes or voice assignment. `allocateVoice` already lives here. |
| Round-robin allocation / steal | C++ PluginProcessor | C voice mixer (executes the key-off/key-on) | Index logic is processor-side; the mixer just receives `key_on(voice_idx)` / `key_off(voice_idx)` commands. |
| Envelope restart on steal | C voice mixer | — | `spu94_voice_mixer_key_on` → `spu94_voice_key_on` resets ADSR to attack. Already PS1-faithful; no change needed. |
| Realtime-safe count handoff (msg→audio) | C++ PluginProcessor | — | `std::atomic` per the existing project convention. The C core stays untouched, so RT-safety gates (which audit only the C link closure) are unaffected. |

**Key boundary:** Phase 60 touches **only `src/plugin/PluginProcessor.{h,cpp}`** plus a new test file. The
C core (`spu94_voice.c`, `spu94_process.c`) needs **no changes** — its `key_on(voice_idx)` /
`key_off(voice_idx)` API already does everything. This keeps the change off the hardware-port-sensitive C
core entirely.

## Standard Stack

No new libraries. This phase uses only what is already in the build.

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `std::atomic` (C++ stdlib) | C++17 | Realtime-safe count handoff (message thread → audio thread) | Already the project's universal msg↔audio idiom — 162 `std::atomic` members in `PluginProcessor.h`, all using explicit acquire/release ordering. `[VERIFIED: grep -c std::atomic src/plugin/PluginProcessor.h → 162]` |
| Unity (test framework) | vendored (`tests/unit/vendor`) | C-level unit tests | Used by every `tests/unit/voice/*.c`. Not usable for `allocateVoice` (C++ method). `[VERIFIED: tests/unit/voice/CMakeLists.txt]` |
| JUCE console-app test | bundled JUCE | Headless instantiation of `SPU94AudioProcessor` for behavioral tests | The blessed pattern for testing processor logic headlessly — `test_mono_sum.cpp`, `test_state_roundtrip.cpp`. `[VERIFIED: tests/plugin/]` |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `std::atomic<int>` | A `juce::AudioParameterInt` | Parameters are for host-automatable, persisted values. The count is internal in Phase 60 (persistence is Phase 63, GUI is Phase 62). An atomic is lighter and matches the existing `pendingGuiTriggerPitch` / recording-state pattern exactly. CONTEXT explicitly leaves this to implementer's discretion; the atomic is the lower-friction, convention-matching choice. |
| Lazy `% count` at allocation | Re-base/clamp `nextVoice` on every count change | Re-basing adds a write from the (message-thread) setter to a value the audio thread reads/writes — a second shared mutable, more race surface. Lazy modulo keeps `nextVoice` audio-thread-owned and makes the CONTEXT ring-out default automatic. See Architecture Pattern 2. |

**Installation:** None. No `npm`/`pip`/`cargo` — this is a C/C++ CMake project and the change adds no
dependencies.

## Package Legitimacy Audit

Not applicable — Phase 60 installs no external packages. The change is confined to existing first-party
source (`src/plugin/PluginProcessor.{h,cpp}`) and one new test file linking already-present targets
(`spu94_static`, bundled JUCE, vendored Unity). No registry, no third-party code.

## Architecture Patterns

### System Architecture Diagram

```
                    MESSAGE THREAD                      AUDIO THREAD (processBlock)
                  ┌────────────────┐                  ┌──────────────────────────────────┐
  Phase 62 GUI    │ setActiveVoice │   atomic store   │  for each MIDI note-on:            │
  (later) ─────►  │   Count(n)     │ ───(release)───► │    voice = allocateVoice(note)     │
  Phase 60 test   │  clamp [1,24]  │                  │      │  reads activeVoiceCount       │
  drives this ──► └────────────────┘                  │      │  (acquire load)               │
  setter directly                                     │      ▼                               │
                                                      │   nextVoice picks an index in        │
                                                      │   [0 .. count-1]                      │
                                                      │      │                                │
                                                      │      ├─ if noteForVoice[voice] >= 0 ──┼─► spu94_voice_mixer_key_off(voice)
                                                      │      │                                │      └─ sets pending_koff bit  ◄── STEAL observable
                                                      │      │  noteForVoice[voice]=note       │
                                                      │      │  nextVoice=(nextVoice+1)%count  │
                                                      │      ▼                                │
                                                      │   spu94_voice_mixer_key_on(voice) ────┼─► sets pending_kon bit       ◄── ALLOC observable
                                                      │                                        │      + stages ADSR (attack restart)
                                                      │   for each MIDI note-off:              │
                                                      │     voice = findVoiceForNote(note) ────┼─► scans noteForVoice[] (all 24 — OK)
                                                      │       spu94_voice_mixer_key_off(voice) │
                                                      └────────────────────────────────────────┘
                                                                     │
                                                                     ▼
                                                         spu94_voice_mixer_tick (next call)
                                                         applies pending_kon/koff, clears them
```

The two `pending_*` bitmasks are the seam between "allocation decided" and "DSP executes." A test can read
them *before* the tick clears them — that is the deterministic observable.

### Component Responsibilities

| File:Line | Responsibility | Phase-60 Change |
|-----------|----------------|-----------------|
| `src/plugin/PluginProcessor.h:420` | `int8_t noteForVoice[24]` (note-per-voice, −1 = free) | None (array stays size 24; only indices `< count` are used) `[VERIFIED]` |
| `src/plugin/PluginProcessor.h:422` | `int nextVoice{0}` (round-robin cursor) | None to declaration; semantics change to cycle within count `[VERIFIED]` |
| `src/plugin/PluginProcessor.h` (new) | `std::atomic<int> activeVoiceCount{24}` + `setActiveVoiceCount(int)` | **ADD** |
| `src/plugin/PluginProcessor.cpp:2593` | `allocateVoice(int note)` — round-robin pick + key-off-on-reuse | **EDIT**: read count, bound modulus |
| `src/plugin/PluginProcessor.cpp:2599` | `nextVoice = (nextVoice + 1) % 24` | **EDIT**: `% count` `[VERIFIED]` |
| `src/plugin/PluginProcessor.cpp:2603` | `findVoiceForNote(int note)` — scans all 24 | None — scanning all 24 is correct regardless of count (a note on a now-out-of-range voice must still be findable for its note-off; this is exactly the ring-out behavior) `[VERIFIED]` |
| `src/plugin/PluginProcessor.cpp:1414-1438` | MIDI note-on/off dispatch | None — already calls `allocateVoice` then `key_on` `[VERIFIED]` |

### Recommended Project Structure

```
src/plugin/PluginProcessor.h    # + std::atomic<int> activeVoiceCount, + setter decl, + test seam
src/plugin/PluginProcessor.cpp  # edit allocateVoice (2 literals), + setter def
tests/plugin/test_voice_alloc.cpp   # NEW — JUCE console-app, modeled on test_mono_sum.cpp
tests/plugin/CMakeLists.txt         # + juce_add_console_app(test_voice_alloc) + add_test
```

### Pattern 1: Realtime-safe scalar handoff (the project's established idiom)

**What:** A value written on the message thread and read on the audio thread is a `std::atomic<T>` with
explicit memory ordering — store with `release`, load/read with `acquire`. For a single scalar this is
lock-free and wait-free.
**When to use:** The active voice count (written by the Phase 62 GUI / by tests, read by `allocateVoice`
inside `processBlock`).
**Example (existing code — the exact pattern to copy):**
```cpp
// Source: src/plugin/PluginProcessor.cpp:2377 (message-thread write) and :823 (audio-thread read)
// GUI-trigger pitch: staged on message thread, consumed on audio thread.
pendingGuiTriggerPitch.store(pitch, std::memory_order_release);   // message thread
// ...
uint16_t trigPitch = pendingGuiTriggerPitch.exchange(0, std::memory_order_acquire); // audio thread
```
```cpp
// Phase 60 application (recommended):
// header:
std::atomic<int> activeVoiceCount{24};                 // default 24 = today's behavior
void setActiveVoiceCount(int n);                        // message-thread setter
// cpp:
void SPU94AudioProcessor::setActiveVoiceCount(int n) {
    n = juce::jlimit(1, 24, n);                          // clamp [1,24] per CONTEXT
    activeVoiceCount.store(n, std::memory_order_release);
}
// inside allocateVoice (audio thread):
const int count = activeVoiceCount.load(std::memory_order_acquire);
```

**Why an atomic and not a plain `int`:** A plain `int` written on one thread and read on another is a data
race (undefined behavior) even though a 32-bit aligned int store is atomic on the target CPUs — the atomic
also forbids the compiler from hoisting/caching the read across the loop. The whole codebase uses atomics
for this; matching it is the least-surprising, review-passing choice. `[VERIFIED: 162 atomics, consistent acquire/release]`

### Pattern 2: Lazy modulo bounding — the count-decrease edge case dissolves

**What:** Bound the cursor *at read time*, not by re-basing it on count change. Two equivalent forms; both
make the CONTEXT ring-out default automatic.
**When to use:** Always, for this allocator.
**Example:**
```cpp
int SPU94AudioProcessor::allocateVoice(int note)
{
    const int count = activeVoiceCount.load(std::memory_order_acquire);
    int voice = nextVoice % count;            // clamp the cursor into range lazily
    if (noteForVoice[voice] >= 0)
        spu94_voice_mixer_key_off(spu94_get_voice_mixer(), voice);   // STEAL (key-off prior)
    noteForVoice[voice] = static_cast<int8_t>(note);
    nextVoice = (voice + 1) % count;          // advance within the active band
    return voice;
}
```

**Why this resolves the "cursor beyond new count" case cleanly:** Suppose count was 24, `nextVoice` had
advanced to 18, then the count drops to 4. On the next note-on, `18 % 4 = 2` → voice 2, then
`nextVoice = 3 % 4 = 3`. The cursor self-heals into `[0,4)` on the very first allocation after the change,
with no separate re-base write. A note still sounding on voice 18 is **not** force-silenced — it keeps
playing until its own note-off (its entry in `noteForVoice[]` is still found by `findVoiceForNote`, which
scans all 24), or until round-robin reuse reaches it again (it won't, while count=4, because the cursor
now stays in `[0,4)` — so it rings out fully and is reclaimed only if count is later raised). **This is
exactly CONTEXT's recommended ring-out default, achieved for free.** `[VERIFIED: traced against allocateVoice + findVoiceForNote source]`

> **Edge guard:** `count` is clamped to `[1,24]` in the setter, so `% count` can never divide by zero.
> Mono (count=1) ⇒ `voice = nextVoice % 1 = 0` always, and `nextVoice = (0+1) % 1 = 0` — voice 0 every
> time, last-note priority (VALLOC-03) falls straight out.

### Pattern 3: Observe allocation via the mixer's pending bitmasks (the test seam)

**What:** `spu94_voice_mixer_key_on(m, voice_idx, ...)` sets `m->pending_kon |= (1u << voice_idx)` and
clears the matching `pending_koff` bit *immediately, synchronously, before any tick*. `key_off` sets
`pending_koff |= (1u << voice_idx)`. These bitmasks are readable directly off the file-scope mixer
singleton.
**When to use:** In the Phase 60 test, to assert which voice index was allocated/stolen — no DSP needed.
**Example:**
```c
// Source: src/spu94/spu94_voice.c:472 (key_on) — sets pending_kon synchronously
m->pending_kon  |= (1u << voice_idx);
m->pending_koff &= ~(1u << voice_idx);
// Source: src/spu94/spu94_voice.c:489 (key_off)
m->pending_koff |= (1u << voice_idx);
// Source: src/spu94/spu94_process.c:54 — the singleton the test reads
spu94_voice_mixer_t *spu94_get_voice_mixer(void);
```
```cpp
// In the test, after driving a note-on through the processor:
auto* mx = spu94_get_voice_mixer();
// assert exactly bit `expectedVoice` is set in pending_kon, etc.
```

### Anti-Patterns to Avoid
- **Re-basing `nextVoice` from the message-thread setter:** introduces a second message-thread→audio-thread
  shared mutable for no benefit; lazy `% count` makes it unnecessary (Pattern 2).
- **Shrinking `noteForVoice[24]` or making `findVoiceForNote` stop at `count`:** breaks ring-out — a held
  note on a now-out-of-range voice would become un-findable and its note-off would leak. Keep both at 24.
  `[VERIFIED: findVoiceForNote scans 0..24]`
- **Resetting the mixer per processor in the test and forgetting it is a process-wide singleton:** see
  Pitfall 2.
- **Adding anti-click fade logic:** explicitly DEFERRED by CONTEXT — hard cut for now.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Voice steal / takeover | A "find oldest / quietest voice" search | The existing key-off-then-key-on on the reused round-robin index | CONTEXT locks plain round-robin; the takeover (envelope restart) is already produced by `key_on` on a reused voice. No search needed. `[CITED: 60-CONTEXT.md Decisions]` |
| Envelope restart on steal | Manual ADSR reset | `spu94_voice_mixer_key_on` → `spu94_voice_key_on` (already resets ADSR to attack) | `[VERIFIED: include/spu94/spu94_voice.h:83-89 key_on contract "Resets ... Calls spu94_adsr_key_on"]` |
| msg→audio count handoff | A mutex / lock-free queue / custom flag | `std::atomic<int>` (project convention) | A single scalar needs only an atomic; locks are RT-unsafe and forbidden by the rt_safety gates. `[VERIFIED: 162 atomics in header]` |
| Cursor bounding on count change | Re-base logic + extra shared state | Lazy `% count` at allocation | Self-heals in one allocation; keeps `nextVoice` audio-thread-owned (Pattern 2). |

**Key insight:** The feature is "change two integer literals and add a clamped atomic." Almost all the
machinery (steal, envelope restart, note-off lookup) already exists and is correct. The risk is not in the
DSP — it's in (a) thread-safety of the new scalar and (b) proving the behavior with a deterministic test.

## Runtime State Inventory

Not a rename/refactor/migration phase — this is a behavioral feature addition. Section omitted per the
template's greenfield/feature guidance. (No stored data, OS-registered state, or build artifacts carry a
voice-count value yet; persistence is explicitly Phase 63.)

## Common Pitfalls

### Pitfall 1: Default-24 must reproduce current behavior bit-for-bit
**What goes wrong:** A refactor of `allocateVoice` subtly changes the index sequence at count=24 (e.g.,
off-by-one in the advance), silently regressing the Phase-42-verified 24-voice path.
**Why it happens:** `allocateVoice` has **no existing test** (added Phase 31, never modified — `git log -S`
shows a single commit, no test references it). There is no guard today. `[VERIFIED: git log -S allocateVoice]`
**How to avoid:** The new test MUST include a `count=24` case that reproduces the exact pre-change sequence:
24 successive note-ons land on voices 0,1,2,…,23 in order; the 25th wraps to voice 0 and key-offs the note
that was there. This case IS the regression guard. Run it before and after the edit.
**Warning signs:** Any change to the advance expression other than swapping the literal `24` for `count`.

### Pitfall 2: The voice mixer is a process-wide singleton, not per-instance
**What goes wrong:** A test instantiates two `SPU94AudioProcessor`s (or runs multiple test cases) and
allocation bits bleed across them, producing flaky assertions.
**Why it happens:** `spu94_get_voice_mixer()` returns `&s_mixer`, a single file-scope `static`
(`src/spu94/spu94_process.c:50`). Every processor instance and every test case shares it.
`[VERIFIED: src/spu94/spu94_process.c:50 static spu94_voice_mixer_t s_mixer;]`
**How to avoid:** At the **start of each test case**, reset the shared state explicitly:
`spu94_voice_mixer_init(spu94_get_voice_mixer());` and reset the processor's `noteForVoice[]` to −1 and
`nextVoice` to 0 (or construct a fresh processor and re-init the mixer). Don't rely on construction order.
Note `test_state_roundtrip.cpp` only reads param floats, so it never hit this — Phase 60 is the first test
to depend on mixer state, so the isolation discipline is new.
**Warning signs:** Tests pass in isolation but fail when run together, or order-dependent failures.

### Pitfall 3: MIDI dispatch is gated behind `voiceSampleLoaded`
**What goes wrong:** A `processBlock`-based test sends MIDI but `allocateVoice` is never reached, so
`pending_kon` stays 0 and assertions trivially "pass" against an empty mask (false green).
**Why it happens:** The note-on loop is wrapped in `if (voiceSampleLoaded.load(acquire))` at
`PluginProcessor.cpp:1415`; `voiceSampleLoaded` is private and only set true inside `loadVoiceSample` /
the WAV path. `[VERIFIED: PluginProcessor.cpp:1415, :2210, :2297]`
**How to avoid:** Two clean routes (see Validation Architecture → Test Approach):
  - **Direct (recommended):** test `allocateVoice` / `findVoiceForNote` directly via a friend/accessor —
    bypasses the `voiceSampleLoaded` gate and `processBlock` entirely. Fast, deterministic, no fixture.
  - **Integration:** if going through `processBlock`, the test must first flip `voiceSampleLoaded` true
    (needs a test seam or a real fixture WAV load) AND load ADPCM into the mixer.
**Warning signs:** `pending_kon == 0` after a note-on; test passes even when allocation logic is broken.

### Pitfall 4: `allocateVoice`, `findVoiceForNote`, `noteForVoice`, `nextVoice` are all `private`
**What goes wrong:** The test can't see them; engineer is tempted to test only the public `processBlock`
path (which drags in Pitfall 3) or to widen the public API unnecessarily.
**Why it happens:** `private:` at `PluginProcessor.h:277`; the four members are below it.
`[VERIFIED: PluginProcessor.h:277, :420, :422, :553, :554]`
**How to avoid:** Add a minimal test seam — preferred is `friend struct VoiceAllocTest;` (or
`friend class`) so the test reads internals without polluting the public ABI. A thin public accessor
(`int getNextVoiceCursor() const`, `int8_t noteOnVoice(int) const`) is the alternative if a friend is
distasteful. Decide this in planning; it's a one-line header change either way.
**Warning signs:** A PR that makes allocation members public for no reason other than the test.

## Code Examples

### Drive note-on/off headlessly and assert the allocated voice (direct seam)
```cpp
// Source: pattern composed from tests/plugin/test_state_roundtrip.cpp (headless processor
//   construction) + src/spu94/spu94_voice.c:472 (pending_kon observable).
#include "PluginProcessor.h"
extern "C" {
#include <spu94/spu94_voice.h>
}

// friend struct in PluginProcessor.h grants access to private allocateVoice/noteForVoice/nextVoice.
static int allocatedVoiceFor(SPU94AudioProcessor& p, int note) {
    auto* mx = spu94_get_voice_mixer();
    uint32_t before = mx->pending_kon;
    int v = p.allocateVoice(note);                  // private — reached via friend
    p.keyOnAllocated(v, note);                       // or inline the mixer key_on the dispatch does
    uint32_t newBits = mx->pending_kon & ~before;    // exactly the bit just set
    // assert newBits == (1u << v)
    return v;
}
```

### count = 1 → mono, every note takes over voice 0 (VALLOC-03)
```cpp
// Source: derived from Pattern 2 (lazy modulo) — count=1 forces voice 0.
p.setActiveVoiceCount(1);
spu94_voice_mixer_init(spu94_get_voice_mixer());     // isolate (Pitfall 2)
int v1 = p.allocateVoice(60);   // expect 0
int v2 = p.allocateVoice(64);   // expect 0; key_off bit for the prior note set first (steal)
// assert v1 == 0 && v2 == 0
// assert pending_koff had bit 0 set on the 2nd call (prior note keyed off = takeover)
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Hardcoded 24-voice round-robin (`% 24`) | Count-bounded round-robin (`% activeVoiceCount`) | Phase 60 (this) | Mono..24-voice selectable; default 24 unchanged |

**Deprecated/outdated:** Nothing. This is additive; no APIs are removed or replaced.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | An atomic (vs. `AudioParameterInt`) is the right storage for the *internal* count in Phase 60 | Standard Stack / Pattern 1 | Low. CONTEXT explicitly leaves this to implementer's discretion. If Phase 62/63 later want a param for automation/persistence, they can wrap or migrate — the atomic remains the audio-thread read source. |
| A2 | A `friend` test seam is acceptable project style | Pitfall 4 | Low. If the project prefers public accessors, swap to a thin `const` getter — same effect, one line. Both are non-invasive. |
| A3 | Phase 42's "full 24-voice operation verified" was a manual/listening verification, not an automated test (none found in `tests/`) | Pitfall 1 | Low–Med. If an automated 24-voice test does exist under a name my grep missed, the new regression case is redundant but still correct. The grep for allocation/round-robin/steal across `tests/` returned only the unrelated `test_voice_tick.c`. |

**These three are the only `[ASSUMED]` items.** Everything else (allocator behavior, line numbers, atomic
convention count, singleton location, pending-bitmask semantics, the `voiceSampleLoaded` gate, private
access levels) was verified by direct file inspection this session.

## Open Questions (RESOLVED)

1. **Friend declaration vs. public accessor for the test seam?**
   - What we know: members are private; both seams are one-line, non-invasive.
   - What's unclear: project style preference (no existing friend declarations found in the header).
   - Recommendation: planner picks; `friend` keeps the public ABI clean. Either works.
   - **RESOLVED (plan 60-01):** `friend struct VoiceAllocTest;`.

2. **Does the plan want a `count=24` regression case only, or also a full `processBlock` integration test?**
   - What we know: the direct seam covers all four criteria deterministically and fast; a `processBlock`
     integration test adds realism but pulls in the `voiceSampleLoaded` gate (Pitfall 3) and a fixture.
   - Recommendation: ship the **direct** unit test for all four criteria (required); optionally add one
     light `processBlock` smoke test that a note-on at default count produces a non-silent block (reuses
     `test_mono_sum`'s harness) if integration confidence is wanted. Not required for the criteria.
   - **RESOLVED (plan 60-01):** direct unit test only, all five cases; no processBlock integration test.

## Environment Availability

No external runtime dependencies. The phase is C++/CMake source plus a test that links already-present
targets (`spu94_static`, bundled JUCE, vendored Unity). `cmake` + the existing toolchain (the project
already builds plugin tests via `add_subdirectory(plugin)` at `tests/CMakeLists.txt:27`) are sufficient.
`[VERIFIED: tests/CMakeLists.txt:27 unconditional add_subdirectory(plugin)]`

## Validation Architecture

Nyquist validation is enabled. All four success criteria are behavioral and map to automated assertions
with **no audio rendering and no GUI** — they observe the allocation decision directly via the mixer's
synchronously-set `pending_kon` / `pending_koff` bitmasks (`spu94_get_voice_mixer()`).

### Test Framework
| Property | Value |
|----------|-------|
| Framework | JUCE console-app test (headless `SPU94AudioProcessor` instantiation), `add_test`/CTest. Modeled on `tests/plugin/test_mono_sum.cpp` + `test_state_roundtrip.cpp`. |
| Config file | `tests/plugin/CMakeLists.txt` — add a `juce_add_console_app(test_voice_alloc)` block (copy the `test_mono_sum` block; link `spu94_static`, `PluginProcessor.cpp` + its sibling sources, `juce::juce_audio_utils`). |
| Quick run command | `ctest --test-dir <build> -R voice_alloc -V` |
| Full suite command | `ctest --test-dir <build> --output-on-failure` |

### Test Entry Points & Observable
- **Drive:** `SPU94AudioProcessor::allocateVoice(int note)` and `::findVoiceForNote(int note)` (private →
  reached via a `friend` test struct or a thin public accessor — planning decision, Pitfall 4) and
  `::setActiveVoiceCount(int)` (new public setter).
- **Observe:** `spu94_get_voice_mixer()->pending_kon` (bit N set ⇒ voice N keyed on = allocated) and
  `->pending_koff` (bit N set ⇒ voice N keyed off = stolen/taken-over). Set **synchronously** inside
  `spu94_voice_mixer_key_on` / `_key_off` before any tick. `[VERIFIED: spu94_voice.c:472, :489]`
- **Isolate:** call `spu94_voice_mixer_init(spu94_get_voice_mixer())` and reset `noteForVoice[]`/`nextVoice`
  at the start of each case (singleton — Pitfall 2).

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| VCOUNT-02 | count=1 ⇒ all note-ons land on voice 0 (mono); count=N ⇒ allocations spread across voices 0..N−1 (poly) | unit | `ctest -R voice_alloc_mono_vs_poly -V` | ❌ Wave 0 |
| VALLOC-01 | with count=N, allocate 8 distinct notes; assert every keyed-on bit is in `[0,N)` (none ≥ N) | unit | `ctest -R voice_alloc_only_active -V` | ❌ Wave 0 |
| VALLOC-02 | with count=N, play N+1 simultaneous notes; the (N+1)th sets `pending_kon` bit 0 AND `pending_koff` bit 0 (oldest = least-recently-allocated reused) | unit | `ctest -R voice_alloc_steal_oldest -V` | ❌ Wave 0 |
| VALLOC-03 | count=1; two successive note-ons both return voice 0; 2nd call sets `pending_koff` bit 0 (prior note taken over) | unit | `ctest -R voice_alloc_mono_takeover -V` | ❌ Wave 0 |
| (regression) | count=24; 24 note-ons → voices 0..23 in order; 25th wraps to 0 and key-offs voice 0's note. Pre/post-edit identical. | unit | `ctest -R voice_alloc_default24_regression -V` | ❌ Wave 0 |

> **Why pending bitmasks, not audio output:** asserting "which voice sounds" via rendered audio would need
> a loaded ADPCM sample, settling blocks, and amplitude thresholds — slow and fuzzy. The bitmask is the
> allocator's *decision*, set synchronously, exact, and DSP-free. This is what makes all five cases
> deterministic and sub-millisecond. `[VERIFIED: key_on/key_off set pending bits with no tick]`

### Sampling Rate
- **Per task commit:** `ctest --test-dir <build> -R voice_alloc -V` (the five cases above; <1s)
- **Per wave merge:** `ctest --test-dir <build> --output-on-failure` (full suite, includes rt_safety + C unit)
- **Phase gate:** Full suite green before `/gsd:verify-work`.

### Wave 0 Gaps
- [ ] `tests/plugin/test_voice_alloc.cpp` — covers VCOUNT-02, VALLOC-01/02/03 + the default-24 regression case
- [ ] `tests/plugin/CMakeLists.txt` — add `juce_add_console_app(test_voice_alloc)` + `add_test(NAME voice_alloc ...)` (copy the `test_mono_sum` block verbatim and rename)
- [ ] Test seam in `src/plugin/PluginProcessor.h` — `friend struct VoiceAllocTest;` (or public accessors) so the test reads `allocateVoice`/`findVoiceForNote`/`noteForVoice`/`nextVoice`
- [ ] No framework install needed — JUCE console-app test infra already present and built.

## Security Domain

No applicable ASVS categories. Phase 60 is an internal DSP-scheduling change in a desktop/plugin audio
processor — no authentication, sessions, access control, network input, cryptography, or untrusted data.
The one input-validation surface (the count) is handled by `juce::jlimit(1, 24, n)` in the setter, which is
the correct and sufficient control (prevents the `% 0` division and out-of-range indexing). No threat
patterns from STRIDE apply to bounded integer round-robin over fixed local arrays.

## Sources

### Primary (HIGH confidence — direct codebase inspection this session)
- `src/plugin/PluginProcessor.cpp:2593-2614` — `allocateVoice`, `findVoiceForNote` (verified the exact `% 24` literals and key-off-on-reuse steal)
- `src/plugin/PluginProcessor.cpp:1414-1438` — MIDI note-on/off dispatch, `voiceSampleLoaded` gate
- `src/plugin/PluginProcessor.cpp:2377, :823` — `pendingGuiTriggerPitch` msg→audio atomic pattern
- `src/plugin/PluginProcessor.h:277, :420, :422, :553-554` — `private:`, `noteForVoice[24]`, `nextVoice`, method decls
- `src/spu94/spu94_voice.c:443-490` — `spu94_voice_mixer_key_on` / `_key_off` (synchronous `pending_kon`/`pending_koff`)
- `src/spu94/spu94_process.c:50, :54` — `static spu94_voice_mixer_t s_mixer;` + `spu94_get_voice_mixer()`
- `include/spu94/spu94_voice.h:83-167` — mixer/voice API contracts (key_on resets ADSR; key_off validates idx)
- `tests/plugin/test_mono_sum.cpp:27-85` — headless processor construction + `prepareToPlay`/`processBlock`/`MidiBuffer` template
- `tests/plugin/test_state_roundtrip.cpp` — `std::make_unique<SPU94AudioProcessor>()` headless pattern
- `tests/plugin/CMakeLists.txt:79-126` — how a processor-linking console-app test is declared
- `tests/CMakeLists.txt:27` — `add_subdirectory(plugin)` (unconditional)
- `tests/rt_safety/CMakeLists.txt` — RT-safety gates audit the **C** link closure (`spu94_static`/`spu94_shared`), not the C++ processor — confirms an atomic read in `allocateVoice` won't trip them
- `git log -S "allocateVoice"` — single commit (Phase 31), no prior tests

### Secondary (MEDIUM)
- `.planning/phases/60-engine-voice-count-allocation/60-CONTEXT.md` — locked decisions (round-robin bounded, hard restart, hard-cut steal, internal count default 24, ring-out default)
- `.planning/REQUIREMENTS.md`, `.planning/STATE.md` — requirement IDs and phase split rationale

### Tertiary (LOW)
- None. No external/web sources were needed; this phase introduces no new libraries.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new deps; atomic convention verified by count (162) and live example.
- Architecture: HIGH — allocator/steal/note-off paths read line-by-line; the change is two literals + one atomic.
- Test strategy: HIGH — observable (`pending_kon`/`pending_koff`) and harness (`test_mono_sum`) both verified in-tree; only the seam style (friend vs accessor) is open.
- Pitfalls: HIGH — singleton scope, `voiceSampleLoaded` gate, private access, and absence of an existing regression test all confirmed directly.

**Research date:** 2026-05-30
**Valid until:** Stable indefinitely for this codebase state — re-verify line numbers if `PluginProcessor.cpp`/`spu94_voice.c` are edited before planning (line cites are exact as of HEAD `3e29fdc`).
