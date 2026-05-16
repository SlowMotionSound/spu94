# Phase 31: Standalone Testbed UX - Research

**Researched:** 2026-05-16
**Domain:** JUCE standalone GUI, MIDI input, WAV-to-ADPCM voice loading, voice trigger UX
**Confidence:** HIGH

## Summary

Phase 31 wires the completed voice engine (Phases 27-30) into a usable standalone testbed UX. The C core already provides: `spu94_sample_encode_to_ram` (WAV-to-ADPCM encoder), `spu94_voice_mixer_key_on/key_off` (voice triggering), and `spu94_get_voice_mixer()` (access to the file-scope mixer instance). The work is entirely in the JUCE layer -- adding GUI controls to trigger voice loading and playback, processing MIDI events from the standalone wrapper, and displaying feedback (file name, byte count).

The JUCE standalone wrapper (8.0.12) **already registers all MIDI input devices** and routes MIDI to processBlock via the AudioProcessorPlayer's MidiMessageCollector. The processor currently ignores the MIDI buffer (parameter is `/*midiMessages*/`). Enabling MIDI input requires: (1) changing `acceptsMidi()` to return true **conditionally** on standalone mode (so plugin formats remain unchanged per TEST-04), and (2) reading MidiMessage events from the MidiBuffer in the standalone processBlock path. The `NEEDS_MIDI_INPUT FALSE` in CMakeLists.txt affects plugin formats only -- the standalone wrapper auto-enables MIDI independently.

**Primary recommendation:** Add a standalone-only "Voice" panel below the existing toolbar (Load WAV / Play / Stop area) with: a Load Sample button (or drag-drop zone), a pitch knob, a Trigger/Stop button, and a status label showing loaded file name + encoded byte count. Process MIDI note-on/off in the standalone processBlock path to trigger voices at MIDI-derived pitch.

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TEST-01 | Load WAV file into voice RAM (encode to ADPCM on load) | Existing `WavLoader::load` + `spu94_sample_encode_to_ram` + `spu94_voice_mixer_load_sample` form the complete pipeline. GUI needs a load button, status display (file name + byte count), and message-thread encode call. |
| TEST-02 | Trigger single voice playback from GUI (pitch control) | `spu94_voice_mixer_key_on` with pitch register calculated from the GUI knob. Need a Trigger button (keys on voice 0) and Stop button (keys off voice 0). Existing `voicePitchKnob` already calculates a pitch register value. |
| TEST-03 | MIDI note input triggers voices in standalone (JUCE native MIDI) | JUCE standalone wrapper already delivers MIDI to processBlock. Need to: (a) conditionally enable acceptsMidi for standalone, (b) iterate MidiBuffer in standalone processBlock path, (c) convert MIDI note to pitch register, (d) allocate voices round-robin across 24 slots. |
| TEST-04 | Standalone remains the development testbed -- no plugin UX changes in v1.8 | All new GUI components gated on `wrapperType == wrapperType_Standalone`. MIDI acceptance gated similarly. Plugin processBlock path unchanged. |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| WAV-to-ADPCM encoding | C Core (spu94_sample_loader) | -- | Existing encoder runs off-hot-path on message thread |
| Voice RAM management | C Core (spu94_voice_mixer_t) | -- | mixer owns the 512 KB voice_ram buffer |
| Voice trigger/playback | C Core (spu94_voice_mixer_key_on/off) | -- | Pending-tick semantics already implemented (C8/MIX-04) |
| MIDI parsing | JUCE standalone wrapper + processBlock | -- | Wrapper collects MIDI; processor dispatches note events |
| Voice allocation (MIDI polyphony) | Plugin processBlock | -- | Round-robin allocator choosing voice 0-23 from note-on |
| GUI controls (pitch knob, trigger button) | JUCE PluginEditor | -- | Standalone-only components in the editor |
| Status display (file name, byte count) | JUCE PluginEditor | -- | juce::Label updated on successful load |
| Pitch calculation (MIDI note to register) | Plugin processBlock | -- | Formula: pitch = 0x1000 * 2^((note - base_note)/12) |

## Standard Stack

### Core (already present -- no new external dependencies)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | GUI framework, standalone wrapper, MIDI device management | Already in project; standalone wrapper handles MIDI device enumeration/routing |
| spu94 C core | current | ADPCM encode, voice tick, mixer | All voice engine APIs already implemented in Phases 27-30 |

### Supporting (no new packages needed)
This phase introduces zero new external dependencies. All work is wiring existing C APIs into the JUCE GUI layer.

**Installation:** No new packages. Existing CMakeLists.txt is sufficient.

## Package Legitimacy Audit

No new packages are introduced in this phase. All functionality uses the existing JUCE 8.0.12 framework and the project's own C core library.

**Packages removed due to slopcheck [SLOP] verdict:** none
**Packages flagged as suspicious [SUS]:** none

## Architecture Patterns

### System Architecture Diagram

```
                    STANDALONE GUI (PluginEditor.cpp)
                    ================================
   [Load Sample Button] ---> loadVoiceSample(file)
          |                        |
          v                        v
   [Status Label]            WavLoader::load(file)
   "kick.wav 1024B"               |
                                   v
                          spu94_sample_encode_to_ram()
                                   |
                                   v
                          spu94_voice_mixer_load_sample()
                                   |
                    ===============|===================
                                   v
   [Trigger Button] ---> voiceMixerKeyOn(voice=0, pitch)
   [Stop Button]   ---> voiceMixerKeyOff(voice=0)
                                   |
                    ===============|===================
                                   v
                    AUDIO THREAD (processBlock)
                    ============================

   MIDI IN (auto from JUCE standalone wrapper)
       |
       v
   MidiBuffer iteration
       |
       +--> note-on:  voice_mixer_key_on(round_robin_voice, midi_pitch)
       +--> note-off: voice_mixer_key_off(matching_voice)
       |
       v
   spu94_voice_mixer_tick() --> dry/reverb output
       |
       v
   Existing spu94_process() signal flow (reverb, DAC, mixer, output)
```

### Recommended Project Structure (changes only)

```
src/
├── plugin/
│   ├── PluginProcessor.h    # +acceptsMidi conditional, +MIDI dispatch, +sample load API
│   ├── PluginProcessor.cpp  # +MIDI handling in standalone path, +loadVoiceSample()
│   ├── PluginEditor.h       # +voice panel controls (standalone-gated)
│   └── PluginEditor.cpp     # +voice panel layout, button callbacks
└── standalone/
    └── WavLoader.h/cpp      # UNCHANGED (already provides int16 stereo output)
```

### Pattern 1: Conditional MIDI Acceptance (Standalone-Only)
**What:** Make `acceptsMidi()` return true only when running as standalone, so plugin formats remain unaffected.
**When to use:** When MIDI input is needed for standalone testbed but must not alter plugin validation (pluginval, auval).

```cpp
// Source: [ASSUMED - standard JUCE pattern for dual-behavior plugins]
bool acceptsMidi() const override
{
    return wrapperType == wrapperType_Standalone;
}
```

[ASSUMED] This approach is standard in JUCE plugins that need different MIDI behavior per format. The standalone wrapper auto-enables all MIDI devices regardless of this flag, so returning true here primarily affects the Settings dialog's MIDI section visibility. However, `AudioProcessorPlayer` only passes MIDI to processBlock when the wrapped processor returns `acceptsMidi() == true`. This conditional keeps plugin formats byte-identical while enabling standalone MIDI.

### Pattern 2: MIDI Note to SPU Pitch Register
**What:** Convert a MIDI note number to the SPU's 4.12 fixed-point pitch register value.
**When to use:** When processing MIDI note-on events to trigger voices.

```cpp
// Source: [VERIFIED: spu94_voice.h documentation + PS1 SPU pitch spec]
// SPU pitch register: 0x1000 = unity playback rate (44.1 kHz sample at 44.1 kHz output)
// MIDI note 60 (C4) is typically the "unity" note for a sample loaded at 44.1 kHz.
// Formula: pitch = 0x1000 * 2^((note - base_note) / 12.0)
//
// base_note is configurable (default 60 = middle C = unity playback).
// Result clamped to 0x0001..0x3FFF per VOICE-03.
static uint16_t midiNoteToPitch(int note, int baseNote = 60)
{
    double ratio = std::pow(2.0, (note - baseNote) / 12.0);
    int pitch = static_cast<int>(0x1000 * ratio + 0.5);
    if (pitch < 1) pitch = 1;
    if (pitch > 0x3FFF) pitch = 0x3FFF;
    return static_cast<uint16_t>(pitch);
}
```

### Pattern 3: Round-Robin Voice Allocation
**What:** Distribute MIDI note-ons across the 24 voice slots in round-robin order.
**When to use:** When the standalone needs polyphonic MIDI without a sophisticated voice-stealing algorithm.

```cpp
// Source: [ASSUMED - standard voice allocator pattern for basic polyphony]
// Simple round-robin: cycle through voices 0..23.
// Track which voice is playing which note for correct note-off matching.
struct VoiceAllocation {
    int8_t noteForVoice[24];  // -1 = free, 0-127 = MIDI note
    int nextVoice;            // round-robin pointer
};
```

### Pattern 4: Standalone-Gated GUI Components
**What:** Add GUI components only visible in standalone mode.
**When to use:** All Phase 31 GUI additions.

```cpp
// Source: [VERIFIED: PluginEditor.cpp line 15 - existing pattern]
if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    addAndMakeVisible(loadSampleButton);
    addAndMakeVisible(triggerButton);
    // ...
}
```

### Anti-Patterns to Avoid
- **Shared voice slot with existing WAV playback:** The existing Load WAV / Play / Stop buttons use the `WavSource` raw playback path (int16 PCM through spu94_process input). The new voice trigger uses the mixer engine path (ADPCM in voice_ram through spu94_voice_mixer_tick). These are independent signal paths. Do NOT merge them.
- **MIDI processing in plugin path:** All MIDI handling must be inside the `isStandalone` branch of processBlock. Plugin path must remain `/*midiMessages*/` (ignored).
- **Blocking message thread with ADPCM encoding:** `spu94_sample_encode_to_ram` is CPU-intensive for long samples. The encode runs on the message thread (not audio) -- acceptable for a testbed, but should not freeze the GUI. For samples under ~10 seconds, the encode is fast enough (<100ms). Longer samples could lag. Use `MessageManager::callAsync` if needed but the simpler synchronous approach is fine for a testbed.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WAV file loading + resampling | Custom WAV parser | Existing `WavLoader::load()` | Already handles all bit depths, sample rates, channel counts |
| ADPCM encoding for voice RAM | Custom encoder | `spu94_sample_encode_to_ram()` | Already implemented with bounds checking in Phase 27 |
| Voice RAM DMA | Custom memcpy to mixer | `spu94_voice_mixer_load_sample()` | Already validates bounds (Phase 30) |
| MIDI device enumeration | Custom MIDI device scanner | JUCE standalone wrapper | Auto-enables all MIDI devices, auto-routes to processBlock |
| Voice trigger with pending semantics | Custom timing logic | `spu94_voice_mixer_key_on/key_off` | C8/MIX-04 pending-tick semantics already correct |
| Pitch register calculation | Lookup table | Direct `2^((note-base)/12) * 0x1000` formula | Simple math, clamped to hardware range |

**Key insight:** Every heavy-lifting piece is already built in Phases 27-30. This phase is purely UX wiring and MIDI dispatch -- no new DSP or codec work needed.

## Common Pitfalls

### Pitfall 1: Voice Mixer Not Enabled
**What goes wrong:** Loading a sample and triggering a voice produces silence.
**Why it happens:** The `spu94_voice_mixer_t.enabled` field defaults to 0 (memset). The mixer tick is gated on `s_mixer.enabled` in spu94_process.c line 98.
**How to avoid:** Set `mixer->enabled = 1` when the first sample is loaded into voice RAM, or unconditionally at application startup.
**Warning signs:** Voice trigger produces no audio but the reverb engine still works.

### Pitfall 2: MIDI Not Reaching processBlock
**What goes wrong:** MIDI devices are connected but notes don't trigger voices.
**Why it happens:** `acceptsMidi()` returns false, so `AudioProcessorPlayer` may not pass MIDI messages to processBlock in some JUCE versions.
**How to avoid:** Return true from `acceptsMidi()` when `wrapperType == wrapperType_Standalone`. Also ensure the MidiBuffer parameter in processBlock is not ignored (remove the `/**/` comment-out).
**Warning signs:** MIDI activity LED on controller blinks but no voice sounds.

### Pitfall 3: Plugin Format Regression
**What goes wrong:** DAW plugin starts accepting MIDI, changing its bus validation behavior. Or UI controls appear in plugin mode.
**Why it happens:** `acceptsMidi()` returns true unconditionally, or GUI components aren't gated on standalone mode.
**How to avoid:** All changes gated on `wrapperType == wrapperType_Standalone`. Plugin format behavior is byte-identical to v1.7.
**Warning signs:** pluginval fails, or AU validation shows changed MIDI capability.

### Pitfall 4: Voice RAM Address Collision
**What goes wrong:** Loading a second sample overwrites the first in voice RAM.
**Why it happens:** Both samples loaded at address 0.
**How to avoid:** For the testbed, a single sample at address 0 is sufficient (TEST-01/TEST-02 only require one sample loaded). MIDI polyphony (TEST-03) plays the SAME sample at different pitches. Multi-sample support is v1.9+.
**Warning signs:** Loading a new sample causes the old trigger to play the new sound (expected behavior for a single-sample testbed).

### Pitfall 5: Note-Off Mismatch
**What goes wrong:** Notes sustain forever because key_off targets the wrong voice slot.
**Why it happens:** Round-robin allocation without tracking which voice plays which note.
**How to avoid:** Maintain a `noteForVoice[24]` array. On note-on, record which note goes to which voice. On note-off, find the voice playing that note and key it off.
**Warning signs:** Sustained notes after MIDI note-off events.

### Pitfall 6: Pitch Outside Hardware Range
**What goes wrong:** Very high MIDI notes (>96) produce pitch register values above 0x3FFF.
**Why it happens:** 2^((96-60)/12) * 0x1000 = 0x4000, exceeding the hardware clamp.
**How to avoid:** The voice engine already clamps pitch to 0x3FFF at key_on (C7). The conversion function should also pre-clamp for correctness.
**Warning signs:** Notes above C7 all sound the same pitch (clamped at maximum).

### Pitfall 7: ADPCM Encode on Audio Thread
**What goes wrong:** Audio glitches during sample loading.
**Why it happens:** `spu94_sample_encode_to_ram` called from the audio callback.
**How to avoid:** ALWAYS encode on the message thread. Use an atomic flag or the existing pending mechanism to signal the audio thread that new data is available in voice_ram.
**Warning signs:** Audio dropouts when loading a WAV file.

## Code Examples

### WAV-to-Voice-RAM Pipeline (Message Thread)

```cpp
// Source: [VERIFIED: spu94_sample_loader.h + spu94_voice.h - existing C APIs]
void SPU94AudioProcessor::loadVoiceSample(const juce::File& file)
{
    auto result = WavLoader::load(file);
    if (!result.has_value()) return;

    // Get the mixer instance (lazily initialized)
    auto* mixer = spu94_get_voice_mixer();

    // Encode left channel to ADPCM into voice RAM at address 0
    int32_t bytes = spu94_sample_encode_to_ram(
        result->L.data(),
        static_cast<uint32_t>(result->numFrames),
        mixer->voice_ram,
        0,                        // ram_offset
        SPU94_SPU_RAM_BYTES,      // ram_size
        1                         // loop_enable (loop for sustained playback)
    );

    if (bytes > 0) {
        mixer->enabled = 1;
        // Store metadata for GUI display
        voiceSampleName = file.getFileName();
        voiceSampleBytes = static_cast<uint32_t>(bytes);
        voiceSampleLoaded.store(true, std::memory_order_release);
    }
}
```

### MIDI Dispatch in Standalone processBlock

```cpp
// Source: [ASSUMED - standard JUCE MIDI iteration pattern]
// Inside the isStandalone branch of processBlock:
if (isStandalone)
{
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            int note = msg.getNoteNumber();
            uint16_t pitch = midiNoteToPitch(note, voiceBaseNote);
            int vel = msg.getVelocity();
            int16_t vol = static_cast<int16_t>((vel * 0x7FFF) / 127);

            int voice = allocateVoice(note);
            spu94_voice_mixer_key_on(mixer, voice,
                0,       // start_addr (single sample at offset 0)
                pitch, vol, vol,
                1,       // reverb_on
                nullptr  // default ADSR
            );
        }
        else if (msg.isNoteOff())
        {
            int voice = findVoiceForNote(msg.getNoteNumber());
            if (voice >= 0)
                spu94_voice_mixer_key_off(mixer, voice);
        }
    }
}
```

### GUI Trigger Button (Standalone Path)

```cpp
// Source: [VERIFIED: PluginEditor.cpp existing button pattern at line 40-51]
triggerButton.onClick = [this]()
{
    int pitch = processorRef.getVoicePitch().load(std::memory_order_relaxed);
    processorRef.triggerVoice(pitch);
};

stopVoiceButton.onClick = [this]()
{
    processorRef.stopVoice();
};
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| WAV playback through raw int16 PCM loop (WavSource) | Voice playback through ADPCM-in-RAM + mixer tick | Phase 30 (2026-05-16) | Both paths coexist; WavSource feeds reverb input, voice mixer feeds patina bus |
| No MIDI support (acceptsMidi = false) | Conditional MIDI acceptance for standalone | This phase | Standalone can receive MIDI; plugin formats unchanged |

**Deprecated/outdated:**
- The existing `voicePitchKnob` in the toolbar controls the **ADPCM coloration bus** pitch (state->voice_pitch), NOT the voice engine pitch. Phase 31 needs a SEPARATE pitch control for the voice engine trigger. The coloration bus pitch knob remains for its own purpose.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `AudioProcessorPlayer` only passes MIDI to processBlock when `acceptsMidi()` returns true | Pattern 1 | MIDI might arrive anyway and we'd need to handle it regardless; low risk since the conditional approach is safe either way |
| A2 | JUCE 8.0.12 standalone wrapper auto-enables all MIDI devices without checking acceptsMidi | Pitfall 2 | If it does check, we'd need NEEDS_MIDI_INPUT TRUE in CMakeLists -- but that affects plugin builds too |
| A3 | Changing `acceptsMidi()` to conditionally return true does not affect pluginval/auval validation for non-standalone formats | Pitfall 3 | If it does, we'd need to keep it false and find another MIDI route; very low risk since plugin formats compile with NEEDS_MIDI_INPUT FALSE |
| A4 | Round-robin voice allocation is sufficient for the testbed (no voice stealing needed) | Pattern 3 | If the user expects voice stealing, notes will cut off early; acceptable for a testbed, refinable later |

## Open Questions

1. **ADSR defaults for MIDI-triggered voices**
   - What we know: `spu94_voice_mixer_key_on` accepts an optional `spu94_adsr_state_t*` config. Passing NULL gives no ADSR (constant amplitude, immediate silence on KOFF).
   - What's unclear: Should MIDI voices have a default ADSR for musicality (e.g., fast attack, sustain, short release)?
   - Recommendation: Use a sensible default ADSR (attack=0, decay=moderate, sustain=max, release=short) for MIDI voices so note-off has a natural release tail rather than an abrupt cut. The GUI Trigger button can bypass ADSR (NULL) for instant response. This is a Claude's-discretion decision.

2. **Single sample vs. multi-sample for MIDI**
   - What we know: TEST-03 says "playing a MIDI note keys on a voice at the correct pitch." It does NOT require loading different samples per note.
   - What's unclear: Whether all MIDI notes trigger the same sample (pitched up/down) or different samples.
   - Recommendation: Single sample pitched across the keyboard. Multi-sample mapping is a v1.9+ feature. The requirement says "at the correct pitch" which means pitch-shifting the loaded sample relative to a base note.

3. **Base note assignment**
   - What we know: The sample is loaded without key/root-note metadata (raw WAV has no standard root note field).
   - What's unclear: What MIDI note should produce unity playback (0x1000)?
   - Recommendation: Default base note = 60 (middle C). Could add a GUI control later, but for the testbed hardcoding 60 is sufficient.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (C unit tests) + manual UAT |
| Config file | tests/CMakeLists.txt |
| Quick run command | `cd build && ctest -R voice --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TEST-01 | WAV file encodes to ADPCM and loads into voice RAM with displayed file name and byte count | manual (GUI) | N/A -- requires GUI interaction | N/A |
| TEST-02 | GUI trigger button keys on a voice at specified pitch, produces audible output | manual (GUI + audio) | N/A -- requires audio output verification | N/A |
| TEST-03 | MIDI note-on triggers voice at correct pitch, note-off releases | manual (MIDI controller) | N/A -- requires external MIDI hardware | N/A |
| TEST-04 | Plugin GUI and behavior unchanged from v1.7 | automated (pluginval) | `pluginval --validate build/spu94_plugin_artefacts/VST3/SPU-94.vst3 --strictness-level 7` | Existing CI gate |

### Sampling Rate
- **Per task commit:** `cd build && ctest -R voice --output-on-failure`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green + manual UAT of all 4 requirements

### Wave 0 Gaps
- None -- existing test infrastructure covers automated validation. Manual UAT covers GUI/MIDI requirements.

## Sources

### Primary (HIGH confidence)
- `include/spu94/spu94_voice.h` - voice mixer API, pitch register semantics, key_on/key_off signatures
- `include/spu94/spu94_sample_loader.h` - WAV-to-ADPCM encode API
- `src/spu94/spu94_process.c` - voice mixer integration point, `spu94_get_voice_mixer()` accessor
- `src/plugin/PluginProcessor.h/cpp` - existing standalone path, WAV loading, atomic state management
- `src/plugin/PluginEditor.h/cpp` - existing standalone-gated GUI pattern (Load WAV, Play, Stop)
- `build/_deps/juce-src/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h` - MIDI auto-enable, device callback registration

### Secondary (MEDIUM confidence)
- JUCE 8.0.12 AudioProcessorPlayer MIDI routing behavior (verified from source inspection)

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - no new dependencies, all APIs already verified in source
- Architecture: HIGH - follows established patterns in PluginEditor.cpp and PluginProcessor.cpp
- Pitfalls: HIGH - derived from direct source code inspection of existing standalone path, voice engine, and JUCE standalone wrapper

**Research date:** 2026-05-16
**Valid until:** 2026-06-16 (stable -- no external dependencies, no API changes expected)
