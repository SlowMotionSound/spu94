# Phase 63: Voice-Count Persistence - Research

**Researched:** 2026-05-31
**Domain:** JUCE plugin preset serialization (text `.spu94` format) + message-thread GUI re-sync; C++ headless unit testing
**Confidence:** HIGH (all cited code locations verified line-accurate against live source this session)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Persist the voice count to **`.spu94` preset files only** — the plugin-layer `[voice]` section written by `savePresetToString` / read by `loadPresetFromString`. This is exactly where every other per-voice control already lives. Rationale (user pick, 2026-05-31): keep it consistent; don't make voice count the lone setting that *also* auto-recalls.
- **D-02:** **Do NOT** add the count to the binary DAW/session state (`StateSerializer` / `getStateInformation`). That container serializes only the C-core engine + a 6-float morph appendix and carries none of the sampler `[voice]`/`[adsr]`/`[effects]`/`[mod_bus]` GUI sections. Deferred until the broader "all sampler state in the session blob" question is revisited.
- **D-03:** A `.spu94` with **no count key restores to 24** (engine default / full rig). Load semantics: default the restored count to 24, then override only if the key is present. NOTE: the existing parser is lenient (absent keys leave state untouched), so this needs an explicit "seed 24 then override" — relying on the lenient skip would leave the user's *current* count instead of forcing 24.
- **D-04:** On load, push the restored count through **`setActiveVoiceCount(n)`** (NOT a raw atomic store) so it clamps to 1–24 and inherits Phase 60's graceful behavior — lowering lets held notes ring out and shrinks future allocation.
- **D-05:** After a load, the standalone **`voiceCountBox` must update to the restored value** (`setSelectedId(n, dontSendNotification)`). Hook into the existing post-load resync path (editor `timerCallback` watches `getFilePresetAppliedCount()` → `syncMixerKnobsFromProcessor()`). Use `dontSendNotification` so the refresh does not re-fire `onChange` back into `setActiveVoiceCount`.

### Claude's Discretion
- Exact key name in the `[voice]` section (e.g. `active_voices=` / `voice_count=`), its format (small decimal int per the `non=%d` precedent), and placement within the section — implementer's call.
- Whether the GUI resync reads the count via a new message-thread-safe accessor or the existing atomic — implementer's call, as long as it is read-safe from the message thread.

### Deferred Ideas (OUT OF SCOPE)
- **Voice count in binary DAW/session state** — deferred until the broader "should all sampler state ride in the binary session blob?" question is taken up.
- **DAW-plugin Voice Count control** — still deferred from Phase 62 (plugin-beta milestone).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| VCOUNT-04 | The active voice count is saved to and restored from presets/system state | Confirmed: save hook at `PluginProcessor.cpp:1858` (after `non=` line in `[voice]`); load hook in `SEC_VOICE` block ending `PluginProcessor.cpp:1950`; restore routes through `setActiveVoiceCount(n)` at line 2650; GUI resync joins `syncMixerKnobsFromProcessor()` at `PluginEditor.cpp:1940`. "Presets/system state" is scoped by D-01/D-02 to the **`.spu94` text path only** — binary session state is explicitly deferred. |
</phase_requirements>

## Summary

Phase 63 is a tightly-scoped serialization addition. The mechanism — a text `.spu94` `[voice]` INI-style section with a working save/parse pair — already exists and handles 15 sibling keys (`pitch`, `vol_l`, `non`, `pmon`, `noise_shift`, …). Adding `activeVoiceCount` is fundamentally a **one-line save + one-clause parse + one GUI-refresh line**, plus a new const getter and the back-compat seed. All five cited code locations in CONTEXT.md were verified line-accurate against the live source.

Two findings change the plan's shape beyond "mirror `non=`":

1. **No getter exists for `activeVoiceCount`.** The atomic is private (`PluginProcessor.h:442`); the only public access is the *setter* (`setActiveVoiceCount`, line 281). The editor needs a new const getter to read the restored count when refreshing `voiceCountBox`. There is a perfect one-line precedent at `PluginProcessor.h:265` (`getShadowSyncCount()`), and because `activeVoiceCount` uses release/acquire ordering, the getter must load with `std::memory_order_acquire`.

2. **No test exercises the plugin-layer text preset path at all.** The grep for `savePresetToString`/`loadPresetFromString` across `tests/` returns nothing. Existing plugin tests (`test_state_roundtrip.cpp`, `test_state_serializer.cpp`) only cover the **binary** `StateSerializer` (which is out of scope per D-02), and the `tests/unit/preset/*` C tests only cover the **C-core** `spu94_preset_save`/`spu94_preset_io.c` — not the plugin `[voice]`/`[adsr]`/`[effects]`/`[mod_bus]` sections. So this phase introduces the *first* automated coverage of the plugin text-preset round-trip. That is a feature, not a blocker: the established headless-processor harness (`test_voice_alloc` / `test_voice_controls`, with the friend-struct seam + argv case selector) is the exact pattern to mirror.

A subtle thread-model point governs D-04: the plugin-layer `[voice]` atomics are written **synchronously on the message thread during parse** (`loadPresetFromString`), while only the C-core engine regs are deferred to the audio thread. `setActiveVoiceCount` is itself a documented message-thread setter, so calling it directly from the parse block is correct and consistent with how `guiVoiceNon` et al. are already handled.

**Primary recommendation:** In `savePresetToString`, append one line in the `[voice]` block writing `activeVoiceCount` as a decimal int. In `loadPresetFromString`, **seed a local `int restoredCount = 24` before the parse loop, set it from the new key inside `SEC_VOICE`, and after the loop call `setActiveVoiceCount(restoredCount)`** (this satisfies D-03 + D-04 together). Add a one-line `getActiveVoiceCount() const` acquire-load getter (mirroring `getShadowSyncCount`). In `syncMixerKnobsFromProcessor()`, add `voiceCountBox.setSelectedId(processorRef.getActiveVoiceCount(), juce::dontSendNotification)`. Add a new headless test `test_voice_persist` mirroring the `test_voice_alloc` CMake/harness pattern, with a save→reload round-trip case and a no-key back-compat case.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Serialize count to text | Plugin processor (message thread) | — | `savePresetToString` already builds the `[voice]` text from message-thread atomic reads |
| Parse count + restore | Plugin processor (message thread) | — | `loadPresetFromString` parses on the message thread and writes plugin-layer atomics synchronously; `setActiveVoiceCount` is a documented message-thread setter |
| Clamp + RT-safe store | Plugin processor (setter) | Audio thread (reader) | `setActiveVoiceCount` clamps [1,24] + release-stores; `allocateVoice` acquire-loads on the audio thread |
| Refresh selector after load | Plugin editor (message thread) | — | `syncMixerKnobsFromProcessor()` runs in `timerCallback`; reads processor state via getters, writes GUI with `dontSendNotification` |
| Read restored count for GUI | Plugin processor (new const getter) | — | Editor needs a message-thread-safe read; no getter exists yet — add one mirroring `getShadowSyncCount` |

**Why this matters:** Every responsibility lands in the plugin layer's existing message-thread save/load/resync plumbing. Nothing touches the C core, the audio-thread hot path (beyond the already-existing `setActiveVoiceCount` release store), or the binary `StateSerializer`. This is the correct tier alignment — the planner should reject any task that proposes parsing in `processBlock` or adding the key to `getStateInformation`.

## Standard Stack

No new external libraries. This is pure in-codebase work using existing JUCE and project facilities.

### Core (existing facilities reused)
| Facility | Location | Purpose | Why Standard |
|----------|----------|---------|--------------|
| `savePresetToString` | `PluginProcessor.cpp:1831` | Build `.spu94` text incl. `[voice]` section | The single canonical text-preset writer; 15 sibling keys already here |
| `loadPresetFromString` | `PluginProcessor.cpp:1908` | Parse `.spu94` text, write plugin atomics | The single canonical text-preset reader; lenient INI parser |
| `setActiveVoiceCount(int)` | `PluginProcessor.cpp:2650` | Clamp [1,24] + release-store the count | Phase 60's RT-safe setter; ring-out-on-decrease behavior baked in |
| `std::snprintf` + `juce::String +=` | save body | Format one key line | Exact idiom every existing key uses |
| `juce::String::getIntValue()` | parse body | Decimal int parse | Exact idiom `non`/`loop`/`noise_shift` use |
| `juce::dontSendNotification` | editor | Suppress `onChange` on programmatic set | Used 60+ times in editor; the codebase's standard |

### Supporting (new code to add — all one-liners)
| Item | Where | Purpose |
|------|-------|---------|
| New save key line | `PluginProcessor.cpp` `[voice]` block (after line 1858 `non=`) | Emit `<key>=%d` for `activeVoiceCount` |
| New parse clause | `PluginProcessor.cpp` `SEC_VOICE` block (within 1935–1950) | Capture key into a local `restoredCount` |
| Seed + apply | `loadPresetFromString` (before/after the parse loop) | `int restoredCount = 24;` … `setActiveVoiceCount(restoredCount);` |
| `getActiveVoiceCount() const` | `PluginProcessor.h` (near line 265) | Acquire-load getter for the editor |
| Selector refresh line | `PluginEditor.cpp` `syncMixerKnobsFromProcessor()` (~1986) | `voiceCountBox.setSelectedId(processorRef.getActiveVoiceCount(), juce::dontSendNotification)` |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| New `getActiveVoiceCount()` getter | Read the atomic some other way | The atomic is `private`; editor has no access. A getter is the minimal, idiomatic choice (matches `getShadowSyncCount`, `getFilePresetAppliedCount`). |
| Seed-24-then-override (D-03) | Rely on lenient parser skip | REJECTED by D-03: lenient skip leaves the user's *current* count, not 24. Must explicitly force 24 when key absent. |
| Apply via `setActiveVoiceCount` (D-04) | Raw `activeVoiceCount.store()` | REJECTED by D-04: bypasses clamp + ring-out semantics. |
| New `test_voice_persist` target | Extend `test_state_roundtrip.cpp` | `test_state_roundtrip` is the **binary** StateSerializer test (out of scope, D-02). Mixing the text-path test there blurs the scope line. Separate target mirrors `test_voice_alloc`. |

**Installation:** None — no package changes. Build/test commands unchanged (see Validation Architecture).

## Package Legitimacy Audit

Not applicable — this phase installs **zero** external packages. All work uses the existing JUCE dependency (already vendored via the project's CMake/FetchContent) and standard C++. No registry interaction, no new `target_link_libraries` beyond what `test_voice_alloc` already links (`spu94_static`, `samplerate`, `juce::juce_audio_utils`).

## Architecture Patterns

### System Architecture Diagram (data flow for save + load + resync)

```
 SAVE (.spu94 write)
 ─────────────────────
 [message thread] savePresetToString()
   spu94_preset_save(engines[0]) ──> C-core regs text
   text += "[voice]\n"
   text += "non=%d\n"          (existing)
   text += "<count_key>=%d\n"  (NEW — reads activeVoiceCount.load(relaxed))
   ... other [voice] keys ...
   return text ──> written to .spu94 file


 LOAD (.spu94 read) + RESTORE
 ─────────────────────────────
 [message thread] loadPresetFromString(text)
   int restoredCount = 24;                  (NEW — D-03 seed)
   for each line:
     SEC_VOICE:
       key=="non"   -> guiVoiceNon.store(...)        (existing, synchronous)
       key=="<count>" -> restoredCount = val.getIntValue()   (NEW)
   setActiveVoiceCount(restoredCount);       (NEW — D-04, clamps[1,24], release-store)
   memcpy buf; filePresetReady = true ──┐
                                        │ (audio thread picks up C-core regs)
 [audio thread] processBlock()  <───────┘
   spu94_preset_load(engines[0], buf)        (C-core regs only)
   filePresetAppliedCount.fetch_add(1, release)   (line 547)


 RESYNC (selector catches up)
 ─────────────────────────────
 [message thread] timerCallback()  (every ~tick)
   if getFilePresetAppliedCount() != lastFilePresetCount:   (line 1328)
     syncMixerKnobsFromProcessor()
       inputLevelKnob.setValue(..., dontSendNotification)    (existing)
       voiceCountBox.setSelectedId(                           (NEW — D-05)
         getActiveVoiceCount(), dontSendNotification)
```

The diagram shows the single most important subtlety: **the count's restore (`setActiveVoiceCount`) happens on the message thread during parse, NOT on the audio thread during the C-core apply.** This matches how `guiVoiceNon`/`guiVoiceVolL`/etc. are already handled — plugin-layer atomics are message-thread-synchronous; only `engines[0]` register loading is deferred to `processBlock`. The `filePresetAppliedCount` increment (audio thread) is purely the *signal* that triggers the GUI resync; by the time the editor's `timerCallback` sees it, `setActiveVoiceCount` has long since run, so `getActiveVoiceCount()` returns the restored value.

### Pattern 1: Mirror the `non=` save/parse pair
**What:** Add the count key beside `non` using the identical decimal-int idiom.
**When to use:** This is THE pattern for the save/parse half.
**Example:**
```cpp
// Source: VERIFIED src/plugin/PluginProcessor.cpp:1858 (save) + :1941 (parse)

// --- save side (in the [voice] block, e.g. just after the non= line at 1858) ---
std::snprintf(line, sizeof(line), "active_voices=%d\n",
              activeVoiceCount.load(std::memory_order_relaxed)); text += line;

// --- parse side (in the SEC_VOICE switch, alongside the non clause at 1941) ---
// Capture into the seeded local rather than storing directly, so D-03 + D-04
// can apply once after the loop:
else if (key == "active_voices") restoredCount = val.getIntValue();
```
*(Key name `active_voices` is illustrative — final name is implementer's discretion per CONTEXT.md. Whatever is chosen, the save string literal and the parse `key ==` literal MUST match exactly.)*

### Pattern 2: Seed-then-override for forced-default back-compat (D-03)
**What:** Declare the restore target with the default before parsing; the parse only overrides if the key is present; apply once after the loop.
**When to use:** Whenever an absent key must force a specific value (not "leave current").
**Example:**
```cpp
// Source: pattern derived from CONTEXT D-03 + VERIFIED loadPresetFromString structure
bool SPU94AudioProcessor::loadPresetFromString(const juce::String& presetText)
{
    if (presetText.isEmpty() || !engines[0]) return false;
    // ... existing length guard ...

    int restoredCount = 24;   // D-03: absent key => full rig, not "current count"

    enum { SEC_NONE, SEC_VOICE, /* ... */ } sec = SEC_NONE;
    for (auto line : juce::StringArray::fromLines(presetText)) {
        // ... existing section + key/val split ...
        switch (sec) {
        case SEC_VOICE:
            // ... existing clauses ...
            else if (key == "active_voices") restoredCount = val.getIntValue();
            break;
        // ... other sections ...
        }
    }

    setActiveVoiceCount(restoredCount);   // D-04: clamp[1,24] + ring-out semantics

    // ... existing memcpy / filePresetReady handoff ...
}
```
Because `setActiveVoiceCount` clamps to [1,24], a malformed key (`active_voices=0`, `active_voices=999`, non-numeric → `getIntValue()` returns 0) lands safely on 1 or stays at the clamp edge — no extra validation needed.

### Pattern 3: One-line const acquire getter (for D-05)
**What:** Add a message-thread-safe read of the atomic.
**Example:**
```cpp
// Source: VERIFIED precedent src/plugin/PluginProcessor.h:265 (getShadowSyncCount)
// activeVoiceCount uses release(store)/acquire(load) — match the load order.
int getActiveVoiceCount() const { return activeVoiceCount.load(std::memory_order_acquire); }
```

### Pattern 4: Selector refresh in syncMixerKnobsFromProcessor (D-05)
**What:** Add the box update to the existing resync function; the existing `timerCallback` → `getFilePresetAppliedCount()` watcher already calls it after every file load.
**Example:**
```cpp
// Source: VERIFIED src/plugin/PluginEditor.cpp:1940-1986 (function body)
//         + :1327-1333 (the timerCallback watcher that invokes it)
void SPU94AudioProcessorEditor::syncMixerKnobsFromProcessor()
{
    inputLevelKnob.setValue(..., juce::dontSendNotification);   // existing
    // ... existing knob/toggle refreshes ...

    // NEW (D-05): reflect the restored active voice count. dontSendNotification
    // so this does NOT re-fire voiceCountBox.onChange -> setActiveVoiceCount.
    voiceCountBox.setSelectedId(processorRef.getActiveVoiceCount(),
                                juce::dontSendNotification);
}
```
**Standalone-only guard:** `voiceCountBox` was added Phase 62 as a standalone-only control (it lives on `samplerWindow->getPanel()`, behind `if (samplerWindow)`). `syncMixerKnobsFromProcessor` is editor-member code that already references standalone-only widgets, so the box is in scope here. The planner should confirm `voiceCountBox` is a direct editor member (not gated behind a runtime null check inside the sync function) — verified it is declared/used unconditionally in the editor at lines 73–81 and 327–330.

### Anti-Patterns to Avoid
- **Parsing the count in `processBlock`:** The plugin-layer `[voice]` atomics are message-thread-synchronous. Putting count parsing on the audio thread breaks the established split and risks reading a half-written buffer.
- **Raw `activeVoiceCount.store()` on restore:** Violates D-04 — skips clamp + ring-out.
- **Relying on the lenient parser for back-compat:** Violates D-03 — leaves the *current* count instead of forcing 24.
- **Calling `setSelectedId(n)` without `dontSendNotification`:** Re-fires `onChange` → `setActiveVoiceCount`, a redundant feedback round-trip (and a latent ordering hazard).
- **Adding the key to `getStateInformation`/`StateSerializer`:** Out of scope (D-02).
- **Putting the round-trip test in `test_state_roundtrip.cpp`:** That target tests the binary container (D-02 territory); the text path deserves its own target to keep the scope line clean.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Clamp count to valid range | Manual `if (n<1) n=1; if (n>24) n=24;` in the parser | `setActiveVoiceCount` (already clamps via `juce::jlimit(1,24,n)`) | D-04; single source of truth for the clamp + ring-out |
| INI key/value parsing | A new parser | The existing `SEC_VOICE` switch + `indexOfChar('=')` split | 15 keys already parse this way; reuse verbatim |
| Int formatting | `std::to_string` / streams | `std::snprintf(line, ..., "%d\n", ...)` + `text += line` | Matches every existing key exactly |
| Decimal parse | `std::stoi` (throws) | `juce::String::getIntValue()` (returns 0 on junk, never throws) | RT-safe-adjacent, no exception path; junk → 0 → clamps to 1 |
| Thread-safe GUI read | Bespoke locking / message queue | `getActiveVoiceCount()` acquire-load getter | Atomic already provides the ordering; mirror `getShadowSyncCount` |

**Key insight:** Every piece of this feature has an existing, battle-tested twin in the same two functions. The risk is *not* complexity — it's drifting from the established idiom (e.g. inventing a clamp, a parser, or a thread-sync mechanism that already exists). The plan should be explicitly imitative.

## Runtime State Inventory

> Rename/refactor/migration inventory. Phase 63 is an **additive serialization** feature, not a rename — but the back-compat dimension (old `.spu94` files on disk) warrants the check.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | **Existing `.spu94` preset files on disk** (user-saved patches) contain a `[voice]` section with NO count key. | None — D-03 "seed 24 then override" handles these: absent key → restored to 24. This IS criterion 3. No data migration; old files load cleanly. |
| Live service config | None — no external services, daemons, or registries involved. | None — verified: this is in-process plugin/standalone code only. |
| OS-registered state | None — no OS task/service registration touches voice count. | None — verified. |
| Secrets/env vars | None — no secrets or env vars reference voice count. | None — verified. |
| Build artifacts | New test target `test_voice_persist` requires a CMake reconfigure (`cmake -B build` re-run) before `cmake --build`. No stale artifacts from a rename (nothing renamed). | Reconfigure CMake when the new test target is added (standard for any new `add_test`). |

**Canonical question — "after every file is updated, what still holds the old format?":** Old `.spu94` files on users' disks. They are handled by design (D-03), not by code change to the files. There is no in-memory cached count that survives a reload, because every load calls `setActiveVoiceCount` fresh.

## Common Pitfalls

### Pitfall 1: Lenient parser silently no-ops the back-compat path
**What goes wrong:** Implementer stores the count directly inside `SEC_VOICE` (like the other keys) and assumes "absent key → stays at default." But the default at that moment is the user's *current* live count, not 24. Loading an old patch then leaves whatever count was already set.
**Why it happens:** The other 15 keys legitimately use "leave untouched if absent" — voice count is the one key that must *force* 24.
**How to avoid:** Use the seed-then-override pattern (Pattern 2): a local `restoredCount = 24` before the loop, applied once after. CONTEXT.md D-03 calls this out explicitly.
**Warning signs:** A back-compat test that loads a no-key preset *after* setting count to 5 returns 5, not 24.

### Pitfall 2: Forgetting the getter — editor can't read the count
**What goes wrong:** Implementer writes the `setSelectedId` line in `syncMixerKnobsFromProcessor` and reaches for `processorRef.activeVoiceCount` — compile error, it's private. Or worse, exposes the atomic publicly (breaks encapsulation, mismatched memory order).
**Why it happens:** Most other state the sync function reads already has a getter (`getParamInputGain`, `getLatencyCompEnabled`, …). Voice count is the gap — it only ever had a *setter*.
**How to avoid:** Add `getActiveVoiceCount() const` (Pattern 3) with `std::memory_order_acquire` to pair with the setter's release store.
**Warning signs:** Build error "`activeVoiceCount` is private" in `PluginEditor.cpp`.

### Pitfall 3: Re-fire feedback loop on resync
**What goes wrong:** `voiceCountBox.setSelectedId(n)` (no flag) fires `onChange` → `setActiveVoiceCount(n)`. Usually harmless (idempotent), but it's a redundant audio-thread-visible store on every preset load and a latent ordering surprise.
**Why it happens:** `setSelectedId`'s default *does* send the notification.
**How to avoid:** Always `juce::dontSendNotification` (D-05). This is already the codebase norm (60+ uses).
**Warning signs:** A debugger breakpoint in `setActiveVoiceCount` hit twice per load (once from parse, once from the box refresh).

### Pitfall 4: Save/parse key-string mismatch
**What goes wrong:** Save writes `voice_count=` but parse checks `key == "active_voices"` → the value round-trips to nothing, silently defaulting to 24 on every load.
**Why it happens:** The key name appears as a literal in two separate functions ~80 lines apart.
**How to avoid:** Pick the name once; grep both sites; ideally the round-trip test (which saves then loads a non-24 count and asserts equality) catches any mismatch immediately.
**Warning signs:** Round-trip test: save count=7, reload, get 24.

### Pitfall 5: CMake not reconfigured for the new test target
**What goes wrong:** New `test_voice_persist` `add_test` lands in `tests/plugin/CMakeLists.txt` but `cmake --build build` doesn't pick it up; `ctest -R voice_persist` finds nothing.
**Why it happens:** Adding a target requires re-running the configure step, not just the build step.
**How to avoid:** `cmake -B build -DCMAKE_BUILD_TYPE=Release` (reconfigure) then `cmake --build build`.
**Warning signs:** `ctest -R voice_persist` reports "No tests were found."

## Code Examples

### Saving the count (full context)
```cpp
// Source: VERIFIED src/plugin/PluginProcessor.cpp:1852-1866 ([voice] block)
text += "\n[voice]\n";
std::snprintf(line, sizeof(line), "pitch=0x%04X\n", ...); text += line;
// ... vol_l, vol_r, loop, anti_alias ...
std::snprintf(line, sizeof(line), "non=%d\n", guiVoiceNon.load(...) ? 1 : 0); text += line;
// NEW: place the count line here (decimal int, mirrors non= idiom)
std::snprintf(line, sizeof(line), "active_voices=%d\n",
              activeVoiceCount.load(std::memory_order_relaxed)); text += line;
// ... pmon, noise_shift, fader, send, drive, ... (unchanged) ...
```

### The existing resync watcher (no change — just where the refresh fires)
```cpp
// Source: VERIFIED src/plugin/PluginEditor.cpp:1327-1333 (timerCallback)
const int fileCount = processorRef.getFilePresetAppliedCount();
if (fileCount != lastFilePresetCount)
{
    lastFilePresetCount = fileCount;
    syncMixerKnobsFromProcessor();   // <-- voiceCountBox refresh rides here (D-05)
}
```

## State of the Art

Not applicable — no fast-moving external technology. JUCE's `ComboBox::setSelectedId(int, NotificationType)` and `String::getIntValue()` APIs are stable and long-established. The relevant "state of the art" is purely internal: the project's own established preset-text + resync conventions, all verified current this session.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The chosen key name (e.g. `active_voices`) does not collide with any future C-core key the parser might one day recognize in `[voice]`. | Patterns | LOW — the plugin parser owns `[voice]` parsing for these keys; C core skips unknown sections. A collision would require a future C-core change to claim the same `[voice]` key, which would be caught by the round-trip test. |
| A2 | `voiceCountBox` is reachable as a plain editor member inside `syncMixerKnobsFromProcessor` without an extra null/standalone guard. | Pattern 4 | LOW — verified it's referenced unconditionally at editor lines 73–81 / 327–330. If it were actually behind a member-pointer/optional, the refresh line needs a guard. Planner should glance at the member declaration in `PluginEditor.h`. |

**Note:** Both assumptions are low-risk and self-checking (the round-trip test catches A1; a one-line header glance settles A2). No user confirmation required — these are implementation details, not product decisions.

## Open Questions

1. **Exact key name + placement (Claude's discretion per CONTEXT.md).**
   - What we know: must be a decimal int (`%d`), in the `[voice]` block, save-literal must equal parse-literal.
   - What's unclear: `active_voices` vs `voice_count` vs `voices` — purely cosmetic.
   - Recommendation: Implementer picks; the plan should name it explicitly so both call sites and the test use one string. (No user input needed — this is a developer-only naming choice per the project's "no dev choices in discuss" norm.)

2. **Does `PluginEditor.h` declare `voiceCountBox` unconditionally?**
   - What we know: it's used unconditionally in `PluginEditor.cpp`.
   - What's unclear: whether the *declaration* carries any `#if` standalone guard.
   - Recommendation: One-line header check during planning; if guarded, mirror the guard around the new refresh line. Not a blocker.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build + test targets | ✓ | (project already built — 3 build dirs present: `build/`, `build-test/`, `build_test/`) | — |
| C++ toolchain | Compile plugin + tests | ✓ | (existing builds present) | — |
| JUCE | Plugin/editor + headless test harness | ✓ | Vendored via FetchContent (`_deps/` in build dirs) | — |
| `spu94_static` lib | Test linkage | ✓ | Built in-tree | — |
| ctest | Run the round-trip + back-compat tests | ✓ | Bundled with CMake | — |

**Missing dependencies with no fallback:** None.
**Missing dependencies with fallback:** None.

Note: A working MIDI controller for *audible* mono/poly verification is NOT available on standalone under Linux (per STATE.md Phase 62 note). This does **not** affect Phase 63, whose criteria are fully verifiable headlessly (round-trip equality + back-compat default) — no audible UAT is required for persistence correctness.

## Validation Architecture

> nyquist_validation enabled. This phase introduces the **first** automated coverage of the plugin-layer text-preset path.

### Test Framework
| Property | Value |
|----------|-------|
| Framework | CTest (CMake-native) + bare `main()` console-app assertions (the project's plugin-test style — see `test_state_roundtrip.cpp`, `test_voice_alloc.cpp`). C-core tests use Unity; plugin tests use hand-rolled `bool test_x()` + `printf` + return-code aggregation. |
| Config file | `tests/plugin/CMakeLists.txt` (add the new target here) |
| Quick run command | `ctest --test-dir build -R voice_persist --output-on-failure` |
| Full suite command | `ctest --test-dir build --output-on-failure` |
| Build command | `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build` (reconfigure required after adding the target — Pitfall 5) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| VCOUNT-04 (criterion 1+2) | Save then reload a non-24 count → restored count equals saved count, **and** `getActiveVoiceCount()` reflects it | unit (headless processor) | `ctest --test-dir build -R voice_persist_roundtrip --output-on-failure` | ❌ Wave 0 |
| VCOUNT-04 (criterion 3) | Load a `.spu94` text with **no** count key (after setting a non-24 count) → restores to 24 | unit (headless processor) | `ctest --test-dir build -R voice_persist_backcompat --output-on-failure` | ❌ Wave 0 |
| VCOUNT-04 (clamp guard) | Load malformed values (`0`, `999`, non-numeric) → clamps to [1,24] | unit (headless processor) | `ctest --test-dir build -R voice_persist_clamp --output-on-failure` | ❌ Wave 0 (optional but cheap) |

### Recommended test design (mirror `test_voice_alloc`)
The cleanest harness is a new `tests/plugin/test_voice_persist.cpp` that:
1. Instantiates `SPU94AudioProcessor` directly (headless), same as `test_voice_alloc.cpp` / `test_state_roundtrip.cpp`.
2. **Round-trip case:** `proc.setActiveVoiceCount(7);` → `auto text = proc.savePresetToString("t","");` → `proc2.loadPresetFromString(text);` → assert `proc2.getActiveVoiceCount() == 7`. (Two-instance style matches `test_multi_instance_independence`.)
3. **Back-compat case:** Build a `.spu94` text **without** the count key (either hand-author a minimal `[voice]` string, or call `savePresetToString` on a build *before* the key is added — simplest is a hardcoded literal string with `non=` but no count key), set the loading instance to a non-24 count first, then `loadPresetFromString(noKeyText)`, assert `getActiveVoiceCount() == 24`.
4. **Clamp case (optional):** Feed `active_voices=0` and `active_voices=999` literals, assert results are `1` and `24`.

CMake: copy the `test_voice_alloc` block in `tests/plugin/CMakeLists.txt` (lines 228–286) — same source list (`PluginProcessor.cpp` + `PluginEditor.cpp` + `ParameterBridge.cpp` + `RegisterPanel.cpp` + `MorphPanel.cpp` + `SrcChain.cpp` + conditional `WavLoader.cpp`), same compile defs (`JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_VST3_CAN_REPLACE_VST2=0`), same link libs (`spu94_static`, `samplerate`, `juce::juce_audio_utils`, `juce::juce_recommended_config_flags`), one `add_test` per argv case (`voice_persist_roundtrip`, `voice_persist_backcompat`, `voice_persist_clamp`).

**Why a headless processor test (not a GUI test):** The save/parse/restore logic is entirely processor-side and message-thread-synchronous — no audio callback, no GUI, no MIDI needed. `savePresetToString` / `loadPresetFromString` / `getActiveVoiceCount` are all directly callable on a bare `SPU94AudioProcessor`. The GUI refresh (D-05) is the one piece a headless test can't exercise; it is low-risk (a single `setSelectedId(..., dontSendNotification)` mirroring 60+ identical calls) and is covered by the existing manual standalone smoke (load a saved low-count patch, confirm the box snaps) — flag as a lightweight human-verify, not a blocker.

### Sampling Rate
- **Per task commit:** `ctest --test-dir build -R voice_persist --output-on-failure` (the new cases, seconds)
- **Per wave merge:** `ctest --test-dir build --output-on-failure` (full suite — guards the existing preset/state/voice tests against regression)
- **Phase gate:** Full suite green before `/gsd:verify-work`. Pay special attention that `test_state_roundtrip`, `test_state_serializer`, and the `tests/unit/preset/*` golden round-trips stay green — adding a `[voice]` key changes the text output, and any golden file that snapshots the full preset text will need regeneration (see Wave 0 gaps).

### Wave 0 Gaps
- [ ] `tests/plugin/test_voice_persist.cpp` — covers VCOUNT-04 round-trip + back-compat (+ optional clamp)
- [ ] `tests/plugin/CMakeLists.txt` — add the `test_voice_persist` target + `add_test` cases (mirror `test_voice_alloc` block)
- [ ] **Golden-file check:** Verify whether `tests/unit/preset/test_preset_golden_roundtrip.c` (or any golden under `tests/fixtures/`) snapshots the **plugin** `[voice]` text. The C-core golden tests likely do NOT (they test `spu94_preset_save`, which produces the C-core text *before* the plugin appends `[voice]`). **Confirm during planning** — if a golden does capture the plugin text incl. `[voice]`, adding a key will break it and the golden must be regenerated as a deliberate task step. (Heavy golden/regression discipline per project norms — do not let this surprise the verifier.)

*Initial scan suggests no existing test captures the plugin `[voice]` text, so the golden risk is likely nil — but the planner must confirm before assuming.*

## Security Domain

> `security_enforcement` not configured for this project; this is an in-process desktop audio plugin with no network, auth, or untrusted-input surface beyond local preset files.

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — (no auth surface) |
| V3 Session Management | no | — |
| V4 Access Control | no | — |
| V5 Input Validation | **yes (minor)** | Preset text is locally-authored but treated defensively: `getIntValue()` never throws (junk → 0), and `setActiveVoiceCount` clamps [1,24]. A malformed/hostile `.spu94` cannot drive an out-of-range count or crash the parser. |
| V6 Cryptography | no | — (never hand-roll; none needed) |

### Known Threat Patterns for this stack
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed `.spu94` count value (out-of-range, non-numeric, overflow) | Tampering / DoS | `getIntValue()` (no-throw) + `setActiveVoiceCount` clamp `juce::jlimit(1,24,n)`. Buffer length already guarded at parse entry (`len >= sizeof(pendingPresetBuf)` → reject). No new attack surface. |
| Oversized preset buffer | DoS | Existing guard at `loadPresetFromString` entry (verified, line 1913) — unchanged by this phase. |

No new security-relevant surface is introduced; the one new input field is the most-constrained value in the entire `[voice]` block (single small int, hard-clamped).

## Sources

### Primary (HIGH confidence — verified against live source this session)
- `src/plugin/PluginProcessor.cpp:1831-1906` — `savePresetToString`; `[voice]` block at 1852–1866 (`non=%d` at 1858). VERIFIED.
- `src/plugin/PluginProcessor.cpp:1908-1998` — `loadPresetFromString`; `SEC_VOICE` switch 1935–1950, parse-loop end 1990, handoff 1992–1997. VERIFIED.
- `src/plugin/PluginProcessor.cpp:2650-2656` — `setActiveVoiceCount` (clamp `jlimit(1,24)`, release-store at 2655). VERIFIED.
- `src/plugin/PluginProcessor.cpp:505-547` — audio-thread file-preset drain; `filePresetAppliedCount.fetch_add` at 547. VERIFIED (establishes the message/audio thread split).
- `src/plugin/PluginProcessor.cpp:2031-2034` — `getFilePresetAppliedCount() const` (acquire load). VERIFIED.
- `src/plugin/PluginProcessor.h:281` — `setActiveVoiceCount` decl (+ message-thread-setter comment 277–280). VERIFIED.
- `src/plugin/PluginProcessor.h:442` — `std::atomic<int> activeVoiceCount{24}` (private). VERIFIED.
- `src/plugin/PluginProcessor.h:260, 265` — getter precedents `getFilePresetAppliedCount()` / `getShadowSyncCount()` (inline acquire). VERIFIED — template for new getter.
- `src/plugin/PluginEditor.cpp:69-81` — `voiceCountBox` setup (`setSelectedId(24, dontSendNotification)`). VERIFIED.
- `src/plugin/PluginEditor.cpp:327-330` — `voiceCountBox.onChange → setActiveVoiceCount`. VERIFIED (the feedback path to avoid).
- `src/plugin/PluginEditor.cpp:1327-1333` — `timerCallback` watcher → `syncMixerKnobsFromProcessor()`. VERIFIED.
- `src/plugin/PluginEditor.cpp:1940-1986` — `syncMixerKnobsFromProcessor()` body (knob/toggle refresh idiom, all `dontSendNotification`). VERIFIED.
- `tests/plugin/CMakeLists.txt:228-286` — `test_voice_alloc` target + per-case `add_test` (the harness to mirror). VERIFIED.
- `tests/plugin/test_state_roundtrip.cpp` — headless `SPU94AudioProcessor` two-instance test style. VERIFIED (out-of-scope binary path, but the harness shape applies).
- `README.md:48-50, 281-283` — build + ctest commands. VERIFIED.
- `CMakeLists.txt:23` — `SPU94_BUILD_GUI` option (ON). VERIFIED.

### Secondary (MEDIUM confidence)
- Grep across `tests/` for `savePresetToString`/`loadPresetFromString` → zero hits (basis for "no existing plugin-text-path test"). MEDIUM (negative result; planner should re-confirm no golden snapshots the plugin `[voice]` text — flagged in Wave 0).

### Tertiary (LOW confidence)
- None. All claims are grounded in verified source reads.

## Metadata

**Confidence breakdown:**
- Standard stack / insertion points: HIGH — all five CONTEXT.md locations verified line-accurate; the save/parse idiom is directly observable.
- Architecture (thread model, resync trigger): HIGH — traced the full save→parse→apply→resync chain across both threads in source.
- Pitfalls: HIGH — each derived from a verified structural fact (lenient parser, private atomic, default-notify on setSelectedId, two-site key literal).
- Validation architecture: HIGH on harness pattern (mirrors verified `test_voice_alloc`); MEDIUM on the golden-file risk (likely nil, but planner must confirm no plugin-text golden exists).

**Research date:** 2026-05-31
**Valid until:** 2026-06-30 (stable internal code; the only currency risk is concurrent edits to the two functions — re-grep line numbers if other phases touch `savePresetToString`/`loadPresetFromString` first)
