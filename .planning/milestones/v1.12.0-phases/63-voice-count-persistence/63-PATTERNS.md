# Phase 63: Voice-Count Persistence - Pattern Map

**Mapped:** 2026-05-31
**Files analyzed:** 4 (3 modified, 1 new)
**Analogs found:** 4 / 4 (every change target has an exact in-file twin)

> All line numbers below were re-verified against live source this session (not just
> copied from RESEARCH.md). Where a line drifted from the research note, the verified
> number is used and the drift is flagged.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/plugin/PluginProcessor.cpp` (save) | serializer (processor, message thread) | transform / file-I/O | the `non=%d` save line in the same `[voice]` block (`PluginProcessor.cpp:1858`) | **exact** (same function, same block, same idiom) |
| `src/plugin/PluginProcessor.cpp` (load) | parser (processor, message thread) | transform / file-I/O | the `key == "non"` parse clause in the same `SEC_VOICE` switch (`PluginProcessor.cpp:1941`) | **exact** (same function, same switch) |
| `src/plugin/PluginProcessor.h` (getter) | accessor (atomic read) | request-response | `getShadowSyncCount()` inline acquire getter (`PluginProcessor.h:265`) | **exact** (one-line inline, same memory order) |
| `src/plugin/PluginEditor.cpp` (resync line) | view-refresh (editor, message thread) | event-driven (post-load) | the per-widget refreshes in `syncMixerKnobsFromProcessor()` (`PluginEditor.cpp:1942-1985`) | **exact** (same function, 18 identical sibling lines) |
| `tests/plugin/test_voice_persist.cpp` (NEW) | test (headless processor) | transform round-trip | `test_voice_alloc.cpp` harness + two-instance style from `test_state_roundtrip.cpp` | **role-match** (headless `SPU94AudioProcessor`; different assertion target) |
| `tests/plugin/CMakeLists.txt` (new target) | build config | — | the `test_voice_alloc` block (`CMakeLists.txt:232-286`) | **exact** (copy block, rename, swap source + cases) |

---

## Pattern Assignments

### 1. `src/plugin/PluginProcessor.cpp` — SAVE side (serializer, message thread)

**Analog:** the `non=%d` line at `PluginProcessor.cpp:1858`, inside the `[voice]` block of `savePresetToString` (function opens at line 1831; `[voice]` block 1852-1866).

**Insertion point:** add one line in the `[voice]` block. Placing it immediately after the `non=` line (1858) keeps it with the other small-int flags, but anywhere in 1853-1866 is correct.

**Idiom to copy** (the exact small-decimal-int pattern — VERIFIED 1856-1860):
```cpp
std::snprintf(line, sizeof(line), "loop=%d\n", loopModeEnabled.load(std::memory_order_relaxed) ? 1 : 0); text += line;
std::snprintf(line, sizeof(line), "anti_alias=%d\n", samplerAAEnabled.load(std::memory_order_relaxed) ? 1 : 0); text += line;
std::snprintf(line, sizeof(line), "non=%d\n", guiVoiceNon.load(std::memory_order_relaxed) ? 1 : 0); text += line;
std::snprintf(line, sizeof(line), "pmon=%d\n", guiVoicePmon.load(std::memory_order_relaxed) ? 1 : 0); text += line;
std::snprintf(line, sizeof(line), "noise_shift=%d\n", noiseShift.load(std::memory_order_relaxed)); text += line;
```

**New line to add** (key name `active_voices` is illustrative — implementer's discretion per CONTEXT.md, but the save literal MUST equal the parse literal in #2):
```cpp
std::snprintf(line, sizeof(line), "active_voices=%d\n",
              activeVoiceCount.load(std::memory_order_relaxed)); text += line;
```

**Notes carried from idiom:**
- `char line[128];` is already declared at 1850 — reuse it, do not redeclare.
- Save reads use `std::memory_order_relaxed` (every sibling line does; the release/acquire ordering on `activeVoiceCount` is for the audio-thread allocator, not for this message-thread snapshot read — `relaxed` matches `noiseShift`/`encode_rate`).
- `non`/`pmon` write a bool as `? 1 : 0`; `noise_shift`/`encode_rate` write a plain int directly. The count is a plain int → mirror the **`noise_shift`** form (no ternary).

---

### 2. `src/plugin/PluginProcessor.cpp` — LOAD side (parser, message thread)

**Analog:** the `else if (key == "non")` clause at `PluginProcessor.cpp:1941`, inside the `SEC_VOICE` case of `loadPresetFromString` (function opens 1908; `SEC_VOICE` switch 1935-1950; parse loop ends 1990; handoff 1992-1997).

**This target is in TWO parts** — the seed/apply (D-03 + D-04) wraps the loop; the parse clause sits inside it.

**Idiom to copy — the parse clause** (VERIFIED 1939-1949, the lenient `else if` chain):
```cpp
else if (key == "loop")    loopModeEnabled.store(val.getIntValue() != 0, std::memory_order_relaxed);
else if (key == "anti_alias") samplerAAEnabled.store(val.getIntValue() != 0, std::memory_order_relaxed);
else if (key == "non")     guiVoiceNon.store(val.getIntValue() != 0, std::memory_order_relaxed);
else if (key == "pmon")    guiVoicePmon.store(val.getIntValue() != 0, std::memory_order_relaxed);
else if (key == "noise_shift") noiseShift.store(val.getIntValue(), std::memory_order_relaxed);
else if (key == "encode_rate") encodeRate.store(val.getIntValue(), std::memory_order_relaxed);
```

**CRITICAL DEVIATION from the idiom (Pitfall 1 / D-03):** every sibling clause stores **directly** into its atomic. The count must NOT — it captures into a pre-seeded local so an absent key forces 24, not "leave current". Capture, do not store:
```cpp
else if (key == "active_voices") restoredCount = val.getIntValue();
```

**The seed + apply that wraps the loop** (D-03 seed before the loop; D-04 apply after — no exact in-file twin, this is the one piece with no direct analog; pattern from CONTEXT D-03/D-04). Structural anchors VERIFIED:
- Function entry guard at 1910-1913 (`presetText.isEmpty()` / length check) — seed the local right after.
- `for (auto line : juce::StringArray::fromLines(presetText))` loop at 1918; closes at 1990.
- Apply between loop-close (1990) and the existing `std::memcpy` handoff (1992).

```cpp
bool SPU94AudioProcessor::loadPresetFromString(const juce::String& presetText)
{
    if (presetText.isEmpty() || !engines[0]) return false;
    auto raw = presetText.toRawUTF8();
    auto len = presetText.getNumBytesAsUTF8();
    if (len == 0 || len >= sizeof(pendingPresetBuf)) return false;

    int restoredCount = 24;   // D-03: absent key => full rig (24), NOT current count

    enum { SEC_NONE, SEC_VOICE, SEC_ADSR, SEC_EFFECTS, SEC_MOD_BUS } sec = SEC_NONE;
    for (auto line : juce::StringArray::fromLines(presetText))
    {
        // ... existing section dispatch + key/val split (1920-1932, unchanged) ...
        switch (sec) {
        case SEC_VOICE:
            // ... existing clauses 1936-1949 ...
            else if (key == "active_voices") restoredCount = val.getIntValue();   // capture only
            break;
        // ... SEC_ADSR / SEC_EFFECTS / SEC_MOD_BUS unchanged ...
        }
    }

    setActiveVoiceCount(restoredCount);   // D-04: clamp[1,24] + ring-out (see analog #3 below)

    std::memcpy(pendingPresetBuf.data(), raw, len);   // existing handoff, unchanged (1992-1997)
    // ...
    return true;
}
```

**Why `setActiveVoiceCount` and not a raw store** (the restore target — VERIFIED `PluginProcessor.cpp:2650-2656`):
```cpp
void SPU94AudioProcessor::setActiveVoiceCount(int n)
{
    n = juce::jlimit(1, 24, n);                              // clamp — absorbs active_voices=0/999/junk
    activeVoiceCount.store(n, std::memory_order_release);    // release pairs with the getter's acquire
}
```
`getIntValue()` returns 0 on non-numeric junk and never throws, so `jlimit` makes every malformed value land safely (0→1, 999→24). No extra validation in the parser (Don't-Hand-Roll: the clamp lives in one place).

---

### 3. `src/plugin/PluginProcessor.h` — new const getter (accessor)

**Analog:** `getShadowSyncCount()` at `PluginProcessor.h:265` — a one-line inline acquire-load getter. (`getFilePresetAppliedCount()` at line 260 is the same shape but is defined out-of-line in the .cpp; prefer the inline `getShadowSyncCount` form.)

**Idiom to copy** (VERIFIED 265):
```cpp
int getShadowSyncCount() const { return shadowSyncCompletedCount.load(std::memory_order_acquire); }
```

**New getter to add** (place near 260-265 with the other GUI-poll getters; declare it `public` — the atomic it reads, `activeVoiceCount`, is `private` at `PluginProcessor.h:442`):
```cpp
// GUI thread: read the active voice count for selector resync after a load.
// acquire pairs with setActiveVoiceCount's release store.
int getActiveVoiceCount() const { return activeVoiceCount.load(std::memory_order_acquire); }
```

**Why this is required (Pitfall 2):** `activeVoiceCount` is `private` (line 442) and today has only a *setter* (`setActiveVoiceCount`, decl line 281). The editor cannot read it without this getter. Use `std::memory_order_acquire` to pair with the setter's `release` store (analog #2) — do NOT use `relaxed` here, and do NOT expose the atomic itself.

---

### 4. `src/plugin/PluginEditor.cpp` — selector resync line (view-refresh, message thread)

**Analog:** the per-widget refresh lines in `syncMixerKnobsFromProcessor()`, body at `PluginEditor.cpp:1940-1986`. Every line reads a processor getter and writes a widget with `juce::dontSendNotification`.

**Insertion point:** append one line just before the closing `}` at line 1986 (after the `voicePitchKnob` block at 1981-1985).

**Idiom to copy** (VERIFIED 1960-1965 — getter read + widget set + `dontSendNotification`):
```cpp
latencyCompToggle.setToggleState(
    processorRef.getLatencyCompEnabled().load(std::memory_order_relaxed),
    juce::dontSendNotification);
dacToggle.setToggleState(
    processorRef.getDacEnabled().load(std::memory_order_relaxed),
    juce::dontSendNotification);
```

**New line to add** (D-05 — `dontSendNotification` is mandatory to avoid re-firing `onChange`):
```cpp
voiceCountBox.setSelectedId(processorRef.getActiveVoiceCount(),
                            juce::dontSendNotification);
```

**The trigger that calls this (no change — context only).** `timerCallback` watches the file-preset counter and calls the sync function (VERIFIED `PluginEditor.cpp:1327-1333`):
```cpp
const int fileCount = processorRef.getFilePresetAppliedCount();
if (fileCount != lastFilePresetCount)
{
    lastFilePresetCount = fileCount;
    syncMixerKnobsFromProcessor();   // <-- the new voiceCountBox line rides here
}
```

**The feedback loop the `dontSendNotification` flag prevents (VERIFIED `PluginEditor.cpp:327-330`):**
```cpp
voiceCountBox.onChange = [this]()
{
    processorRef.setActiveVoiceCount(voiceCountBox.getSelectedId());
};
```
Calling `setSelectedId(n)` WITHOUT the flag re-fires this `onChange` → a redundant `setActiveVoiceCount` (Pitfall 3). The flag suppresses it.

**Box default for reference (VERIFIED `PluginEditor.cpp:69-77`):** `voiceCountBox` holds items 1..24 (`itemId == count`), default `setSelectedId(24, dontSendNotification)`. So the restored int maps 1:1 to an itemId — `setSelectedId(getActiveVoiceCount(), ...)` is a direct pass-through, no lookup.

---

### 5. `tests/plugin/test_voice_persist.cpp` (NEW) + `tests/plugin/CMakeLists.txt` (new target)

**Harness analog:** `tests/plugin/test_voice_alloc.cpp` (headless `SPU94AudioProcessor`, argv case selector, bare `main` + `printf` + return-code aggregation).
**Two-instance analog:** `tests/plugin/test_state_roundtrip.cpp` (save on instance A, load on instance B). Note: `test_state_roundtrip` tests the **binary** `StateSerializer` path (out of scope per D-02) — borrow its *shape*, not its target.
**CMake analog:** the `test_voice_alloc` block at `CMakeLists.txt:232-286`.

**Why a new target, not an extension** (Anti-pattern from RESEARCH): putting the text-path round-trip in `test_state_roundtrip.cpp` blurs the D-02 scope line (that file is the binary container). A separate `test_voice_persist` target keeps the text path clean.

**Harness skeleton to copy** (idiom VERIFIED `test_voice_alloc.cpp:242-269` — argv selector + `ScopedJuceInitialiser_GUI` + direct processor construction):
```cpp
#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <cstdio>
#include <memory>

namespace {

// Round-trip (criterion 1+2): save a non-24 count, reload into a 2nd instance,
// assert it round-trips. Two-instance style from test_state_roundtrip.cpp.
bool test_roundtrip()
{
    std::printf("voice_persist_roundtrip... ");
    bool ok = true;
    auto a = std::make_unique<SPU94AudioProcessor>();
    a->setActiveVoiceCount(7);
    auto text = a->savePresetToString("t", "");

    auto b = std::make_unique<SPU94AudioProcessor>();
    b->loadPresetFromString(text);
    if (b->getActiveVoiceCount() != 7) { std::printf("\n  FAIL: got %d expected 7", b->getActiveVoiceCount()); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// Back-compat (criterion 3 / Pitfall 1): a [voice] text with NO count key,
// loaded after setting a non-24 count, must restore to 24 (not stay at the
// current count). Hand-authored minimal text is simplest.
bool test_backcompat()
{
    std::printf("voice_persist_backcompat... ");
    bool ok = true;
    auto p = std::make_unique<SPU94AudioProcessor>();
    p->setActiveVoiceCount(5);   // current count != 24

    // Minimal valid text with a [voice] section but NO active_voices key.
    // (Alternatively: capture savePresetToString output from a pre-key build.)
    juce::String noKey = "[preset]\nname=back\n[voice]\nnon=0\n";
    p->loadPresetFromString(noKey);
    if (p->getActiveVoiceCount() != 24) { std::printf("\n  FAIL: got %d expected 24", p->getActiveVoiceCount()); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// Clamp guard (optional, cheap): active_voices=0 -> 1, =999 -> 24.
bool test_clamp() { /* same shape; assert 1 and 24 */ }

} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf("usage: %s <case>\n", argv[0]); return 2; }
    const juce::String c(argv[1]);
    bool ok = false;
    if      (c == "voice_persist_roundtrip")  ok = test_roundtrip();
    else if (c == "voice_persist_backcompat") ok = test_backcompat();
    else if (c == "voice_persist_clamp")      ok = test_clamp();
    else { std::printf("unknown case: %s\n", argv[1]); return 2; }
    return ok ? 0 : 1;
}
```

> NOTE: the back-compat hand-authored text MUST use whatever key string is chosen in
> #1/#2 by its *absence* — i.e. it deliberately omits `active_voices=`. If the round-trip
> test (which saves with the real key) and back-compat (which omits it) both pass, Pitfall 4
> (save/parse literal mismatch) is impossible to miss.

**CMake block to copy** (VERIFIED `CMakeLists.txt:232-286`). Copy verbatim, rename `test_voice_alloc` → `test_voice_persist`, swap the test `.cpp`, and replace the five `add_test` cases with three:
```cmake
juce_add_console_app(test_voice_persist
    PRODUCT_NAME "SPU-94 Voice Persist Test"
)
juce_generate_juce_header(test_voice_persist)

target_sources(test_voice_persist PRIVATE
    test_voice_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginEditor.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/ParameterBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/RegisterPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/MorphPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/SrcChain.cpp
)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_sources(test_voice_persist PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone/WavLoader.cpp)
    target_include_directories(test_voice_persist PRIVATE
        ${CMAKE_SOURCE_DIR}/src/standalone)
endif()

target_include_directories(test_voice_persist PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugin
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(test_voice_persist PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

include(${CMAKE_SOURCE_DIR}/cmake/libsamplerate.cmake)

target_link_libraries(test_voice_persist PRIVATE
    spu94_static
    samplerate
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)

add_test(NAME voice_persist_roundtrip  COMMAND test_voice_persist voice_persist_roundtrip)
add_test(NAME voice_persist_backcompat COMMAND test_voice_persist voice_persist_backcompat)
add_test(NAME voice_persist_clamp      COMMAND test_voice_persist voice_persist_clamp)
```

**Reconfigure required (Pitfall 5):** a new `add_test` needs a CMake reconfigure, not just a rebuild:
`cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`, then `ctest --test-dir build -R voice_persist --output-on-failure`.

---

## Shared Patterns

### Atomic memory-ordering pairing (release store / acquire load)
**Sources:** setter `PluginProcessor.cpp:2655` (release) ↔ new getter `PluginProcessor.h` (acquire, mirroring `getShadowSyncCount` :265).
**Apply to:** the new getter (#3) and any read of `activeVoiceCount` off the audio thread.
**Rule:** `activeVoiceCount` is written with `memory_order_release` and must be read with `memory_order_acquire`. The *save* snapshot read (#1) is an exception — it uses `relaxed` to match its 13 sibling lines, which is correct because it's a plain message-thread snapshot, not a cross-thread handoff.

### `dontSendNotification` on every programmatic widget set
**Source:** `PluginEditor.cpp:1942-1985` (`syncMixerKnobsFromProcessor`, 18 uses) + the construction defaults at 62/76/79.
**Apply to:** the new `voiceCountBox.setSelectedId` resync line (#4).
**Rule:** any processor→GUI push uses `juce::dontSendNotification` so it doesn't re-enter the widget's `onChange`. Codebase norm (60+ uses).

### Lenient INI parse, EXCEPT where a default must be forced
**Source:** the `SEC_VOICE` else-if chain `PluginProcessor.cpp:1936-1949` (lenient: absent key → untouched).
**Apply to:** the load clause (#2) — this is the one key that breaks the norm.
**Rule:** 15 sibling keys legitimately "leave current if absent." Voice count must *force* 24 on absence (D-03), so it uses seed-then-override (a local seeded to 24, applied once after the loop) instead of an in-switch store.

### Headless-processor test harness
**Source:** `test_voice_alloc.cpp` (argv selector + `ScopedJuceInitialiser_GUI` + direct `std::make_unique<SPU94AudioProcessor>()`), CMake `CMakeLists.txt:232-286`.
**Apply to:** the new `test_voice_persist` target (#5).
**Rule:** plugin tests are bare console apps — `bool test_x()` + `printf` + a `main` that selects on `argv[1]` and returns 0/1; one `add_test` per case. No GoogleTest, no Catch (C-core tests use Unity; plugin tests are hand-rolled).

---

## No Analog Found

| Item | Role | Why no direct twin |
|------|------|--------------------|
| The "seed 24 before loop, apply after loop" wrapper (#2) | parser control-flow | Every other `[voice]` key stores in-place inside the switch. No existing key forces a default on absence, so the seed/apply structure has no in-file precedent — it is derived from D-03 + D-04. **Mitigation:** the structure is trivial (one local declaration + one post-loop call) and is fully specified in #2 above; the back-compat test (#5) proves it. |

Everything else maps to an exact same-file or same-function analog. There is NO need to fall back to RESEARCH.md `Code Examples` for any target — the live source twins are tighter.

---

## Resolved Open Questions (from RESEARCH.md)

| RESEARCH Q | Resolution (verified this session) |
|------------|-----------------------------------|
| A2 / Open Q2 — is `voiceCountBox` reachable in the sync fn without a guard? | **YES, unconditionally.** `voiceCountBox` is a plain editor member (`PluginEditor.h:151`, no `#if`). `samplerWindow` is created unconditionally in the ctor (`PluginEditor.cpp:55`, no guard). `syncMixerKnobsFromProcessor` (`PluginEditor.cpp:1940`) is a flat function with no standalone guard and references plain members directly. The new refresh line needs **no guard**. |
| Wave 0 golden-file risk — does any test snapshot the plugin `[voice]` text? | **NO. Risk is nil.** `grep savePresetToString\|loadPresetFromString tests/` → zero hits. The C-core golden (`tests/unit/preset/test_preset_golden_roundtrip.c`) calls only `spu94_preset_save` (C-core text, written *before* the plugin appends `[voice]`). The lone `non` hit in `test_voice_controls.cpp` is `non_flags` (voice-mixer bit), unrelated. **No golden regeneration needed.** |
| A1 — key-name collision with a future C-core `[voice]` key | LOW, self-checking. The plugin parser owns these `[voice]` keys; C core skips unknown sections. The round-trip test catches any collision. |

---

## Metadata

**Analog search scope:** `src/plugin/` (PluginProcessor.{h,cpp}, PluginEditor.{h,cpp}), `tests/plugin/` (all targets + CMakeLists.txt), `tests/unit/preset/`, `tests/golden/`, `tests/fixtures/`.
**Files scanned:** 9 source/test files + CMake + golden/fixture inventory.
**Line numbers:** all re-verified against live source on 2026-05-31 (matched RESEARCH.md; the editor sync-fn body is at 1940-1986, not the ~1986 noted in RESEARCH summary — the new line lands just before the closing brace at 1986).
**Pattern extraction date:** 2026-05-31
