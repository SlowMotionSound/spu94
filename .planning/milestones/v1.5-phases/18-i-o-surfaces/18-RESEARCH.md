# Phase 18: I/O Surfaces - Research

**Researched:** 2026-05-03
**Domain:** CLI flag parsing (C/getopt_long), JUCE standalone GUI (ComboBox, MidiInput, atomic parameter bridge)
**Confidence:** HIGH

## Summary

Phase 18 wraps the completed C core tempo API (Phase 16) and preset format extension (Phase 17) with user-facing I/O surfaces: a `--tempo` CLI flag and JUCE standalone GUI controls for tempo sync mode, BPM entry, global subdivision, per-register subdivision dropdowns, and MIDI clock input.

The CLI surface is straightforward -- one new `getopt_long` flag following the established pattern in `cmd_reverb.c`. The JUCE surface is more involved: a tempo control zone with mode selector (FREE/INT/EXT), BPM text entry, global subdivision dropdown, 10 per-register subdivision dropdowns, and a MIDI clock listener for EXT mode. The existing atomic parameter bridge pattern scales cleanly to the new tempo state.

**Primary recommendation:** Split into three plans: (1) CLI `--tempo` flag, (2) JUCE tempo GUI controls + atomic bridge, (3) MIDI clock EXT mode. The CLI plan is small and self-contained. The GUI plan covers the bulk of the work -- sync mode selector, BPM field, global and per-register subdivision dropdowns, and atomic bridge plumbing. The MIDI plan layers on external clock reception.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Three sync modes: FREE (unclocked -- registers are raw sample counts), INT (internal BPM -- user-typed), EXT (external BPM -- MIDI clock input)
- **D-02:** Entering INT or EXT mode snaps all sync-enabled registers to their assigned subdivisions via the existing auto-resnap machinery (`spu94_set_tempo` -> auto-resnap pass). No new C core logic needed for the snap itself
- **D-03:** EXT mode uses MIDI clock (24 PPQN) to derive BPM. Works in standalone -- no DAW host required. DAW AudioPlayHead is a future v1.6 plugin-milestone addition feeding the same `spu94_set_tempo` pipe
- **D-04:** `--tempo <BPM>` flag on the `reverb` subcommand. Sets BPM (INT mode) and triggers resnap. No additional flags needed -- subdivision assignments come from the loaded preset's `[tempo]` section or default to Global
- **D-05:** `--tempo` interacts cleanly with `--load-preset` (preset already has tempo/subdivision state) and `--preset` (factory presets have no tempo state, so `--tempo` applies INT mode with default Global subdivision on all registers)
- **D-06:** Each of the 10 tempo registers gets a dropdown with three tiers: Free (unclocked, ignores tempo), Global (follows the global subdivision setting), or an individual musical division (1/1 through 1/16, straight/dotted/triplet)
- **D-07:** Global subdivision control -- one master setting that all "Global" registers follow. When entering a synced mode (INT/EXT), all registers default to Global
- **D-08:** Individual division override -- user can pull any register out of Global and assign a specific subdivision. That register keeps its individual assignment until explicitly returned to Global
- **D-09:** Reflection sync and comb sync group toggles (existing C core API) are NOT surfaced in this phase. Per-register dropdowns provide equivalent flexibility

### Claude's Discretion
- GUI layout and zone placement for tempo controls (toolbar vs dedicated zone vs integrated)
- MIDI device selection UI approach (dropdown, auto-detect, etc.)
- MIDI clock jitter smoothing strategy (moving average window, etc.)
- BPM field input widget style (text box, spinner, etc.)
- How to visually distinguish Free/Global/Individual states in the per-register dropdown
- Whether the global subdivision control needs its own label/section or can be inline with the mode selector

### Deferred Ideas (OUT OF SCOPE)
- Sync group toggles (reflection_sync / comb_sync) -- per-register dropdowns provide equivalent flexibility
- DAW host tempo sync (AudioPlayHead) -- deferred to v1.6 plugin milestone
- Tempo-modulated delays -- smooth real-time BPM transitions with crossfade/interpolation
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TEMPO-07 | `--tempo` flag sets BPM before processing | CLI getopt_long pattern established in cmd_reverb.c (case 1012 pattern); calls spu94_set_tempo + spu94_set_subdivision |
| TEMPO-08 | BPM field in the standalone GUI | JUCE TextEditor or Slider with integer range; atomic bridge pattern from PluginProcessor; timer-based GUI sync at 30Hz |
| TEMPO-09 | Subdivision selectors for delay registers (or a global subdivision mode) | JUCE ComboBox with addItem/addSectionHeading; 10 per-register dropdowns + 1 global; C core spu94_subdivision_to_string for labels |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| BPM storage and resnap | C Core (spu94_tempo.c) | -- | Already implemented in Phase 16. All tempo math lives in C. |
| CLI --tempo flag | CLI (cmd_reverb.c) | -- | Thin wrapper: parse arg, call spu94_set_tempo, done. |
| GUI sync mode selector | JUCE Editor | JUCE Processor (atomics) | Editor owns widgets; Processor owns state bridge to audio thread. |
| GUI BPM field | JUCE Editor | JUCE Processor (atomics) | Same pattern as inputLevelKnob -- Editor widget stores to Processor atomic. |
| Per-register subdivision dropdowns | JUCE Editor | JUCE Processor (atomics) | 10 ComboBoxes in Editor, backed by atomic arrays in Processor. |
| MIDI clock reception | JUCE Processor | JUCE standalone wrapper | processBlock receives MIDI from standalone wrapper's AudioProcessorPlayer. |
| MIDI clock BPM derivation | JUCE Processor | -- | Real-time safe: interval measurement + moving average, then store to atomic BPM. |
| MIDI device selection | JUCE standalone wrapper | -- | Built-in Audio/MIDI Settings dialog handles device enumeration and selection. |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | GUI framework, MIDI input, standalone wrapper | Already in use; pinned in CMakeLists.txt [VERIFIED: CMakeLists.txt line 27] |
| libspu94 (C core) | in-tree | Tempo API (spu94_set_tempo, spu94_set_subdivision, etc.) | Already implemented in Phase 16 [VERIFIED: spu94_tempo.c] |
| getopt_long | system libc | CLI flag parsing | Already in use for all cmd_reverb.c flags [VERIFIED: cmd_reverb.c line 18] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| juce::MidiInput | JUCE 8.0.12 | MIDI device enumeration and opening | EXT mode -- list available MIDI devices [CITED: docs.juce.com/master/classMidiInput.html] |
| juce::MidiMessage | JUCE 8.0.12 | MIDI clock message detection | isMidiClock() for 0xF8 status byte [CITED: docs.juce.com/master/classMidiMessage.html] |
| juce::ComboBox | JUCE 8.0.12 | Dropdown selectors | Mode selector, global subdivision, per-register subdivisions [CITED: docs.juce.com/master/classjuce_1_1ComboBox.html] |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| processBlock MIDI routing | Direct MidiInput::openDevice in Processor | Direct approach gives tighter timestamps but bypasses JUCE standalone wrapper; processBlock routing is the established pattern |
| juce::Slider for BPM | juce::TextEditor | Slider provides mouse-drag interaction and numeric clamping out of the box; TextEditor needs manual parsing. Slider with integer steps is recommended. |

## Architecture Patterns

### System Architecture Diagram

```
CLI path:
  argv --tempo 120  -->  getopt_long parse  -->  spu94_set_tempo(state, 120)
                                                  |
  --load-preset     -->  spu94_preset_load  -->  (preset carries tempo state)
                                                  |
                                             spu94_tick + spu94_process
                                                  |
                                             WAV output

GUI path:
  User widget interaction (message thread)
       |
       v
  std::atomic<uint16_t> tempoBpm          (Processor)
  std::atomic<uint8_t>  syncMode          (Processor)
  std::atomic<uint8_t>  globalSubdivision (Processor)
  std::atomic<uint8_t>  perRegSub[10]     (Processor)
       |
       v  (read in processBlock -- audio thread)
  spu94_set_tempo(spu, bpm)
  spu94_set_subdivision(spu, reg, sub)   for each changed register
       |
       v
  spu94_process(spu, ...)  -->  audio output

MIDI clock path (EXT mode):
  MIDI device  -->  JUCE standalone wrapper (AudioDeviceManager)
       |
       v
  AudioProcessorPlayer::MidiMessageCollector
       |
       v
  processBlock(buffer, midiMessages)
       |
       v
  for each msg in midiMessages:
    if msg.isMidiClock():
      measure interval, update moving average
      derive BPM = 60.0 / (avg_interval * 24)
      store to atomic BPM
```

### Recommended Project Structure

No new files needed beyond existing structure. All additions go into existing files:

```
src/
├── cli/
│   └── cmd_reverb.c          # +--tempo flag (getopt_long case 1013)
├── standalone/
│   ├── PluginProcessor.h      # +tempo atomics, +MidiInputCallback override
│   ├── PluginProcessor.cpp    # +MIDI clock processing in processBlock,
│   │                          #  +tempo state push to C core
│   ├── PluginEditor.h         # +tempo widgets (mode selector, BPM field,
│   │                          #  global sub, per-register sub dropdowns)
│   ├── PluginEditor.cpp       # +widget construction, layout, timer sync
│   └── CMakeLists.txt         # NEEDS_MIDI_INPUT -> TRUE, acceptsMidi -> true
tests/
├── cli/
│   └── test_cli_tempo.py      # NEW: --tempo flag integration tests
```

### Pattern 1: CLI Flag Addition (getopt_long)

**What:** Add `--tempo <BPM>` flag following the existing pattern.
**When to use:** All CLI flag additions in cmd_reverb.c.
**Example:**
```c
// Source: cmd_reverb.c existing pattern (verified in codebase)
// In long_opts array:
{"tempo",           required_argument, NULL, 1013},

// In switch statement:
case 1013: {  /* --tempo */
    char *endptr;
    long val = strtol(optarg, &endptr, 10);
    if (endptr == optarg || *endptr != '\0' || val < 1 || val > 65535) {
        SPU94_ERROR("invalid value for --tempo: '%s' (accepts 1 to 65535)", optarg);
        return 2;
    }
    tempo_bpm = (uint16_t)val;
    break;
}

// After preset/config loading, before processing:
if (tempo_bpm > 0) {
    // Enable both sync groups so all registers participate
    spu94_set_reflection_sync(state, 1);
    spu94_set_comb_sync(state, 1);
    spu94_result_t trc = spu94_set_tempo(state, tempo_bpm);
    if (trc != SPU94_OK) {
        SPU94_ERROR("failed to set tempo to %u BPM", (unsigned)tempo_bpm);
        /* ... cleanup ... */
        return 2;
    }
    // If no preset loaded subdivision state, apply default global subdivision
    // to all registers that are still FIXED
    for (int r = 0; r < SPU94_TEMPO_REG__COUNT; r++) {
        if (spu94_get_binding_state(state, (spu94_tempo_reg_t)r) == SPU94_BIND_FIXED) {
            spu94_set_subdivision(state, (spu94_tempo_reg_t)r, SPU94_SUB_1_4);
        }
    }
}
```

### Pattern 2: Atomic Parameter Bridge (GUI to Audio Thread)

**What:** Lock-free atomic communication between GUI and audio thread for tempo state.
**When to use:** Any GUI-driven parameter that needs to reach the C core on the audio thread.
**Example:**
```cpp
// Source: existing pattern in PluginProcessor.h (verified in codebase)

// In PluginProcessor.h -- new tempo atomics:
std::atomic<uint16_t> tempoBpm{0};        // 0 = FREE mode (no tempo)
std::atomic<uint8_t>  syncMode{0};        // 0=FREE, 1=INT, 2=EXT
std::atomic<uint8_t>  globalSubdivision{SPU94_SUB_1_4};  // default 1/4 note

// Per-register subdivision: sentinel 0xFF = "Global" (follows global),
// SPU94_SUBDIVISION__COUNT = "Free" (unclocked)
// Any valid spu94_subdivision_t value = individual override
std::atomic<uint8_t>  perRegSub[SPU94_TEMPO_REG__COUNT];

// Getter pattern (matches existing getDryLevel etc.):
std::atomic<uint16_t>& getTempoBpm() { return tempoBpm; }
std::atomic<uint8_t>&  getSyncMode() { return syncMode; }

// In processBlock -- push tempo state to C core:
uint8_t mode = syncMode.load(std::memory_order_relaxed);
if (mode != 0) {  // INT or EXT
    uint16_t bpm = tempoBpm.load(std::memory_order_relaxed);
    if (bpm > 0 && bpm != lastPushedBpm) {
        spu94_set_tempo(spu, bpm);
        lastPushedBpm = bpm;
    }
}
```

### Pattern 3: MIDI Clock BPM Derivation

**What:** Measure intervals between MIDI clock messages (0xF8, 24 per quarter note) and derive BPM with jitter smoothing.
**When to use:** EXT sync mode.
**Example:**
```cpp
// Source: MIDI specification (24 PPQN) + standard moving average smoothing
// [CITED: en.wikipedia.org/wiki/Pulses_per_quarter_note]

// In processBlock, iterate midiMessages:
for (const auto metadata : midiMessages) {
    auto msg = metadata.getMessage();
    if (msg.isMidiClock()) {
        double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (lastClockTime > 0.0) {
            double interval = now - lastClockTime;
            // Push into circular buffer for moving average
            clockIntervals[clockIdx] = interval;
            clockIdx = (clockIdx + 1) % kClockWindowSize;
            if (clockCount < kClockWindowSize) clockCount++;

            // Compute average interval
            double sum = 0.0;
            for (int i = 0; i < clockCount; i++) sum += clockIntervals[i];
            double avgInterval = sum / clockCount;

            // 24 clocks per quarter note
            double quarterNoteSec = avgInterval * 24.0;
            uint16_t bpm = static_cast<uint16_t>(60.0 / quarterNoteSec + 0.5);
            if (bpm >= 1 && bpm <= 999)
                tempoBpm.store(bpm, std::memory_order_relaxed);
        }
        lastClockTime = now;
    }
}
```

### Pattern 4: ComboBox for Enumerated Selections

**What:** JUCE ComboBox populated from C core enum values via string conversion functions.
**When to use:** Mode selector, global subdivision selector, per-register subdivision dropdowns.
**Example:**
```cpp
// Source: existing presetSelector pattern in PluginEditor.cpp (verified)
// + spu94_subdivision_to_string API (verified in spu94_tempo.c)

// Global subdivision dropdown:
globalSubSelector.addSectionHeading("Subdivision");
for (int i = 0; i < SPU94_SUBDIVISION__COUNT; ++i) {
    const char* label = spu94_subdivision_to_string((spu94_subdivision_t)i);
    globalSubSelector.addItem(juce::String(label), i + 1);  // 1-based IDs
}
globalSubSelector.setSelectedId(SPU94_SUB_1_4 + 1, juce::dontSendNotification);

// Per-register dropdown (3-tier: Free / Global / Individual):
perRegDropdown[r].addItem("Free", kFreeId);       // unclocked
perRegDropdown[r].addItem("Global", kGlobalId);    // follows global
perRegDropdown[r].addSeparator();
for (int i = 0; i < SPU94_SUBDIVISION__COUNT; ++i) {
    const char* label = spu94_subdivision_to_string((spu94_subdivision_t)i);
    // Gray out invalid subdivisions using spu94_subdivision_valid
    perRegDropdown[r].addItem(juce::String(label), i + 100);  // offset to avoid ID collision
}
```

### Anti-Patterns to Avoid

- **Writing tempo logic in JUCE:** The C core owns ALL tempo math. JUCE code must only call `spu94_set_tempo`, `spu94_set_subdivision`, and `spu94_set_binding_fixed`. Never compute sample counts in C++.
- **Calling C core from the GUI thread:** The `spu94_state` is NOT thread-safe. All C core mutations happen in `processBlock` (audio thread). The GUI stores values into atomics; processBlock reads them.
- **Mixing MIDI clock and user BPM in processBlock:** In EXT mode, MIDI-derived BPM overwrites the atomic. In INT mode, ignore MIDI clock. In FREE mode, ignore both. Never combine the two sources.
- **Using `std::atomic<float>` for BPM:** BPM is an integer (uint16_t). Use `std::atomic<uint16_t>` to match the C core's `spu94_set_tempo` parameter type. Avoids float-to-int conversion on every processBlock.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| MIDI device enumeration | Custom ALSA/CoreMIDI device scanning | JUCE standalone wrapper + Audio/MIDI Settings dialog | Cross-platform, handles hot-plug, built into JUCE 8 standalone wrapper |
| MIDI message parsing | Raw byte inspection of MIDI data | `juce::MidiMessage::isMidiClock()` | Handles edge cases (running status, SysEx interleaving) |
| Subdivision sample calculation | C++ formula in PluginProcessor | `spu94_set_subdivision()` in C core | C core owns ALL DSP math (project constraint) |
| CLI argument parsing | Custom string parsing | `getopt_long` with existing pattern | Established in cmd_reverb.c, handles edge cases, generates help text |
| GUI-to-audio lock-free comms | Mutex, spinlock, or lock-free queue | `std::atomic` stores polled in processBlock | Existing pattern used for all 11 current parameters; proven, simple |

**Key insight:** Phase 18 adds zero new algorithms. Every computation is already in the C core. The phase is pure plumbing: parse user input (CLI arg or GUI widget), store it in the right atomic, push it to the C core in processBlock.

## Common Pitfalls

### Pitfall 1: NEEDS_MIDI_INPUT Must Be TRUE

**What goes wrong:** MIDI clock messages never arrive in processBlock because `acceptsMidi()` returns `false`.
**Why it happens:** The current CMakeLists.txt has `NEEDS_MIDI_INPUT FALSE` and PluginProcessor.h has `acceptsMidi() { return false; }`. The JUCE standalone wrapper checks `acceptsMidi()` to decide whether to route MIDI.
**How to avoid:** Change CMakeLists.txt to `NEEDS_MIDI_INPUT TRUE` and PluginProcessor.h to `return true`. These two changes are a prerequisite for EXT mode.
**Warning signs:** MIDI clock test passes in unit tests but EXT mode shows no BPM in the GUI.

### Pitfall 2: MIDI Clock Jitter Causes Tempo Flutter

**What goes wrong:** Raw BPM from individual clock intervals fluctuates by +/-2-5 BPM, causing visible jittering in the BPM display and audible resnapping.
**Why it happens:** USB MIDI has inherent jitter of 1-2ms per message [CITED: e-rm.de/data/E-RM_report_Jitter_02_14_EN.pdf]. At 120 BPM, each clock interval is 20.83ms, so 1ms jitter is ~5% error.
**How to avoid:** Moving average over 24-48 clock messages (1-2 quarter notes). This smooths jitter while still tracking real tempo changes within 1-2 beats. Do NOT resnap on every BPM atom change -- only resnap when BPM differs from the last pushed value by a threshold (e.g., 1 BPM).
**Warning signs:** BPM display flickering rapidly between adjacent values.

### Pitfall 3: Resnap Storm on Mode Entry

**What goes wrong:** Entering INT mode from FREE with 10 registers all set to "Global" triggers 10 individual `spu94_set_subdivision` calls, each of which writes to hardware registers. This is correct but wasteful -- `spu94_set_tempo` with sync groups enabled does auto-resnap in a single pass.
**Why it happens:** Misunderstanding the C core API. `spu94_set_tempo` auto-resnaps all grid-bound registers in active sync groups.
**How to avoid:** When entering INT/EXT mode: (1) enable both sync groups, (2) set each register's binding to GRID via `spu94_set_subdivision`, (3) call `spu94_set_tempo`. Step 3 does the resnap. Don't redundantly call set_subdivision again after set_tempo.
**Warning signs:** Initial snap on mode entry takes unexpectedly long or produces audible artifacts.

### Pitfall 4: GUI Thread Calling C Core Directly

**What goes wrong:** Data race between GUI thread and audio thread accessing the same `spu94_state`.
**Why it happens:** Temptation to call `spu94_get_binding_state()` from the GUI thread to check current subdivision state for dropdown display.
**How to avoid:** Read tempo state from atomics only. The audio thread pushes binding state into shadow atomics after any change; the GUI timer reads those shadows. Same pattern as the existing register bridge.
**Warning signs:** Intermittent crashes or garbled GUI values, especially when rapidly switching presets.

### Pitfall 5: --tempo with --preset vs --load-preset Interaction

**What goes wrong:** `--tempo 120 --preset hall` tries to set tempo, but the Hall factory preset has no tempo state. Or `--tempo 120 --load-preset my.spu94` conflicts with the preset's own tempo.
**Why it happens:** Unclear ordering of operations.
**How to avoid:** Per D-04 and D-05: `--tempo` is applied AFTER preset loading. For `--preset` (factory): apply the preset, then `spu94_set_tempo(120)` enables INT mode with default 1/4 subdivision on all registers. For `--load-preset`: apply the preset (which carries its own tempo/subdivision state), then `spu94_set_tempo(120)` overrides the preset's BPM -- existing bindings are preserved and resnapped to the new BPM.
**Warning signs:** Output differs unexpectedly between `--tempo 120 --preset hall` and `--tempo 120 --load-preset hall.spu94`.

### Pitfall 6: Per-Register Subdivision State Sync

**What goes wrong:** GUI dropdown shows "Global" but the register is actually FIXED in the C core, or vice versa.
**Why it happens:** The C core has its own binding state (FIXED/GRID/PROPORTIONAL) that can change independently -- e.g., manually dragging a register slider in the GUI causes the C core's write-interception hook to transition it from GRID to PROPORTIONAL.
**How to avoid:** The GUI must re-read binding state from the audio thread's shadow atomics on every timer tick. When the slider is dragged, the write-interception hook fires in the C core (audio thread), transitions the binding to PROPORTIONAL, and the audio thread must update the shadow atomics. The GUI timer sees the change and updates the dropdown display.
**Warning signs:** Dropdown stays on "1/4" after user drags the register slider.

## Code Examples

### CLI --tempo Flag (Complete Pattern)

```c
// Source: cmd_reverb.c existing patterns (verified in codebase)

// 1. Add to long_opts array (after {"load-preset", ...}):
{"tempo",           required_argument, NULL, 1013},

// 2. Add variable:
uint16_t tempo_bpm = 0;  // 0 = no --tempo flag given

// 3. Add case in switch:
case 1013: {  /* --tempo */
    char *endptr;
    long val = strtol(optarg, &endptr, 10);
    if (endptr == optarg || *endptr != '\0' || val < 1 || val > 65535) {
        SPU94_ERROR("invalid value for --tempo: '%s' (accepts 1 to 65535)", optarg);
        return 2;
    }
    tempo_bpm = (uint16_t)val;
    break;
}

// 4. After preset loading, after spu94_tick(state), before processing:
if (tempo_bpm > 0) {
    spu94_set_reflection_sync(state, 1);
    spu94_set_comb_sync(state, 1);

    // For factory presets (no tempo state): set default subdivision on all registers
    if (preset_name) {
        for (int r = 0; r < SPU94_TEMPO_REG__COUNT; r++) {
            spu94_set_subdivision(state, (spu94_tempo_reg_t)r, SPU94_SUB_1_4);
        }
    }
    // For --load-preset: registers already have bindings from the preset file

    spu94_result_t trc = spu94_set_tempo(state, tempo_bpm);
    if (trc != SPU94_OK) {
        SPU94_ERROR("failed to set tempo to %u BPM", (unsigned)tempo_bpm);
        spu94_destroy(state);
        free(input.L); free(input.R); free(work_buf);
        return 2;
    }
    spu94_tick(state);  // commit resnapped registers
}
```

### JUCE Tempo Atomics in PluginProcessor

```cpp
// Source: existing atomic pattern in PluginProcessor.h (verified)

// Sync mode enum (not a C core type -- JUCE-layer only)
enum SyncMode : uint8_t { SYNC_FREE = 0, SYNC_INT = 1, SYNC_EXT = 2 };

// New tempo atomics in PluginProcessor private section:
std::atomic<uint16_t> tempoBpm{0};
std::atomic<uint8_t>  syncMode{SYNC_FREE};
std::atomic<uint8_t>  globalSubdivision{SPU94_SUB_1_4};

// Per-register subdivision state: 0xFF = "Global", SPU94_SUBDIVISION__COUNT = "Free"
std::array<std::atomic<uint8_t>, SPU94_TEMPO_REG__COUNT> perRegSub;
// Initialize all to 0xFF (Global) in constructor

// Audio-thread-only tracking (not atomic -- only read/written in processBlock)
uint16_t lastPushedBpm = 0;
uint8_t  lastPushedMode = SYNC_FREE;
std::array<uint8_t, SPU94_TEMPO_REG__COUNT> lastPushedSub;

// MIDI clock state (audio-thread-only)
static constexpr int kClockWindowSize = 24;  // 1 quarter note of smoothing
std::array<double, kClockWindowSize> clockIntervals{};
int clockIdx = 0;
int clockCount = 0;
double lastClockTime = 0.0;
```

### JUCE ComboBox for Per-Register Subdivision

```cpp
// Source: existing presetSelector and ComboBox API patterns (verified)

// IDs for the three-tier dropdown
static constexpr int kFreeId   = 1;    // Free (unclocked)
static constexpr int kGlobalId = 2;    // Global (follows master)
static constexpr int kSubBase  = 100;  // Individual subdivisions: 100 + enum value

// Construction (in PluginEditor constructor or helper):
for (int r = 0; r < SPU94_TEMPO_REG__COUNT; ++r) {
    auto& dropdown = perRegDropdowns[r];
    dropdown.addItem("Free", kFreeId);
    dropdown.addItem("Global", kGlobalId);
    dropdown.addSeparator();
    for (int s = 0; s < SPU94_SUBDIVISION__COUNT; ++s) {
        dropdown.addItem(
            juce::String(spu94_subdivision_to_string((spu94_subdivision_t)s)),
            kSubBase + s);
    }
    dropdown.setSelectedId(kGlobalId, juce::dontSendNotification);  // default

    dropdown.onChange = [this, r] {
        int id = perRegDropdowns[r].getSelectedId();
        if (id == kFreeId)
            processorRef.getPerRegSub(r).store(
                SPU94_SUBDIVISION__COUNT, std::memory_order_relaxed);
        else if (id == kGlobalId)
            processorRef.getPerRegSub(r).store(
                0xFF, std::memory_order_relaxed);
        else
            processorRef.getPerRegSub(r).store(
                static_cast<uint8_t>(id - kSubBase), std::memory_order_relaxed);
    };
    addAndMakeVisible(dropdown);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Projucer NEEDS_MIDI_INPUT checkbox | CMake `NEEDS_MIDI_INPUT` property in `juce_add_plugin` | JUCE 6+ | Must set in CMakeLists.txt, not Projucer |
| `MidiInput::getDevices()` returning StringArray | `MidiInput::getAvailableDevices()` returning `Array<MidiDeviceInfo>` | JUCE 7.0 | Use `.identifier` field for `openDevice`, not index |
| Direct `MidiInput::openDevice(index, callback)` | `MidiInput::openDevice(identifier, callback)` | JUCE 7.0 | String identifier instead of integer index |

**Deprecated/outdated:**
- `MidiInput::getDevices()` (returns StringArray) -- replaced by `getAvailableDevices()` which returns `Array<MidiDeviceInfo>` with both name and identifier fields. [VERIFIED: JUCE 8.0.12 source]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Moving average window of 24 clock messages (1 quarter note) provides adequate jitter smoothing while tracking tempo changes within 1-2 beats | Architecture Patterns (Pattern 3) | BPM display may flutter or track too slowly; window size needs empirical tuning |
| A2 | Per-register subdivision sentinel value 0xFF for "Global" does not collide with any valid spu94_subdivision_t enum value | Code Examples | 0xFF is well above SPU94_SUBDIVISION__COUNT (15), so no collision. LOW risk. |
| A3 | The standalone wrapper's built-in Audio/MIDI Settings dialog is sufficient for MIDI device selection -- no custom MIDI device dropdown needed | Architecture Patterns | If the settings dialog is too hidden or confusing, a dedicated MIDI device ComboBox in the editor may be needed |
| A4 | JUCE standalone wrapper on Linux uses ALSA MIDI, which supports USB-MIDI devices and provides timestamps on incoming messages | Architecture Patterns | If ALSA timestamps are unreliable, MIDI clock jitter smoothing may need different strategy |

## Open Questions (RESOLVED)

1. **Per-register subdivision display refresh after slider drag**
   - What we know: The C core's write-interception hook transitions GRID bindings to PROPORTIONAL when a user writes to a d-prefix register (confirmed in spu94_tempo.c line 387-396). This happens on the audio thread.
   - What's unclear: The existing `RegisterBridge::pushPendingRegisterWrites` does not currently propagate binding-state changes back to GUI-readable atomics. The planner needs to decide how to surface binding state transitions.
   - Recommendation: Add per-register `std::atomic<uint8_t>` binding state shadows in PluginProcessor, updated by the audio thread after `pushPendingRegisterWrites`. The GUI timer reads these shadows and updates dropdown selections.

2. **Preset load interaction with tempo GUI state**
   - What we know: File presets carry tempo state (BPM, per-register bindings). Factory presets do not.
   - What's unclear: When a preset is loaded via the GUI (factory or file), should the tempo GUI controls reset to match the preset state?
   - Recommendation: Yes. On factory preset load: mode -> FREE, BPM -> 0, all dropdowns -> "Free". On file preset load: mode -> INT if preset has BPM, BPM -> preset value, dropdowns -> match preset bindings. Sync via the existing filePresetReady mechanism.

3. **Modified-state tracking for tempo changes**
   - What we know: The existing baseline snapshot (PresetSnapshot) tracks registers, mixer faders, and DAC toggles. Tempo state is not currently in the snapshot.
   - What's unclear: Should tempo changes trigger the asterisk (*) modified-state indicator on the preset name?
   - Recommendation: Yes. Add tempoBpm, syncMode, globalSubdivision, and perRegSub to PresetSnapshot. This ensures "Save" captures the full state and modified-state tracking is accurate.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| JUCE 8.0.12 | GUI controls, MIDI input | Yes | 8.0.12 (FetchContent) | -- |
| ALSA MIDI | EXT mode MIDI clock | Yes | System | -- |
| getopt_long | CLI --tempo flag | Yes | System libc | -- |
| cmake | Build system | Yes | 3.31.6 | -- |
| g++ | C++ compilation | Yes | 15.2.0 | -- |
| python3 + pytest | CLI integration tests | Yes | 3.13.7 | -- |

**Missing dependencies with no fallback:** None.

**Missing dependencies with fallback:** None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | pytest (CLI tests), CTest (C unit tests) |
| Config file | tests/cli/CMakeLists.txt, tests/unit/tempo/CMakeLists.txt |
| Quick run command | `cd build && ctest -L cli -R tempo --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TEMPO-07 | CLI --tempo flag sets BPM before processing | integration | `cd build && python3 -m pytest tests/cli/test_cli_tempo.py -v` | No -- Wave 0 |
| TEMPO-08 | BPM field in standalone GUI | manual-only | Manual: launch GUI, type BPM, verify resnap | N/A (GUI) |
| TEMPO-09 | Subdivision selectors for delay registers | manual-only | Manual: launch GUI, select subdivisions, verify register values | N/A (GUI) |

### Sampling Rate
- **Per task commit:** `cd build && ctest -L cli -R tempo --output-on-failure`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/cli/test_cli_tempo.py` -- covers TEMPO-07 (--tempo flag integration tests)
- [ ] Update `tests/cli/CMakeLists.txt` -- register test_cli_tempo in ctest

## Sources

### Primary (HIGH confidence)
- cmd_reverb.c -- complete getopt_long pattern for all existing CLI flags (verified in codebase)
- PluginProcessor.h/cpp -- complete atomic parameter bridge pattern (verified in codebase)
- PluginEditor.h/cpp -- complete ComboBox, Slider, Timer, and layout patterns (verified in codebase)
- spu94.h -- complete tempo API surface: set_tempo, set_subdivision, get_binding_state, subdivision_to_string, tempo_reg_name, subdivision_valid (verified in codebase)
- spu94_tempo.c -- complete implementation of subdivision table, auto-resnap, write-interception (verified in codebase)
- CMakeLists.txt -- JUCE 8.0.12 pinned, NEEDS_MIDI_INPUT=FALSE (verified in codebase)
- juce_StandaloneFilterWindow.h -- standalone wrapper MIDI routing: addMidiInputDeviceCallback, timerCallback auto-detect, Audio/MIDI Settings dialog (verified in JUCE 8.0.12 source)

### Secondary (MEDIUM confidence)
- [JUCE MidiInput class docs](https://docs.juce.com/master/classjuce_1_1MidiInput.html) -- getAvailableDevices(), openDevice(), MidiInputCallback
- [JUCE MidiMessage class docs](https://docs.juce.com/master/classjuce_1_1MidiMessage.html) -- isMidiClock(), getTimeStamp()
- [JUCE ComboBox class docs](https://docs.juce.com/master/classjuce_1_1ComboBox.html) -- addItem, addSectionHeading, onChange
- [JUCE MidiDemo.h](https://github.com/juce-framework/JUCE/blob/master/examples/Audio/MidiDemo.h) -- device enumeration and opening pattern
- [MIDI clock specification (Wikipedia)](https://en.wikipedia.org/wiki/Pulses_per_quarter_note) -- 24 PPQN, F8 status byte

### Tertiary (LOW confidence)
- [JUCE forum: MIDI clock jitter](https://forum.juce.com/t/midi-latency-and-jitter/32316) -- community reports of 1-2ms jitter on USB MIDI
- [e-rm.de clock jitter report](https://www.e-rm.de/data/E-RM_report_Jitter_02_14_EN.pdf) -- technical measurement of MIDI clock jitter

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already in use, versions pinned and verified in CMakeLists.txt
- Architecture: HIGH -- all patterns directly observed in existing codebase; no new architectural concepts
- Pitfalls: HIGH -- derived from verified codebase state (NEEDS_MIDI_INPUT=FALSE, acceptsMidi=false, write-interception hook behavior)
- MIDI clock smoothing: MEDIUM -- algorithm is standard but window size needs empirical tuning (flagged as A1)

**Research date:** 2026-05-03
**Valid until:** 2026-06-03 (stable -- no external dependencies changing)
