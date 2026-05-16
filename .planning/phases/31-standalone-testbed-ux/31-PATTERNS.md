# Phase 31: Standalone Testbed UX - Pattern Map

**Mapped:** 2026-05-16
**Files analyzed:** 4 (all modifications to existing files)
**Analogs found:** 4 / 4

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/plugin/PluginProcessor.h` | controller | request-response | itself (existing WAV load API at lines 50-55) | exact |
| `src/plugin/PluginProcessor.cpp` | controller | request-response | itself (existing `loadWavFile()` at lines 776-791, standalone processBlock at lines 521-585) | exact |
| `src/plugin/PluginEditor.h` | component | event-driven | itself (existing standalone-gated buttons at lines 23-25, toggle/knob declarations) | exact |
| `src/plugin/PluginEditor.cpp` | component | event-driven | itself (existing standalone-gated Load/Play/Stop pattern at lines 15-52, knob wiring at lines 93-113) | exact |

## Pattern Assignments

### `src/plugin/PluginProcessor.h` (controller, request-response)

**Analog:** itself -- extend existing patterns

**Imports pattern** (lines 1-15):
```cpp
#pragma once

#include <JuceHeader.h>
#include "ParameterBridge.h"
#include "SrcChain.h"
#include "StateSerializer.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

extern "C" {
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
}
```

**New include needed:**
```cpp
extern "C" {
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <spu94/spu94_voice.h>
#include <spu94/spu94_sample_loader.h>
}
```

**Public API pattern** (lines 50-55 -- existing WAV load/play/stop methods):
```cpp
// --- WAV playback control (message-thread callers) ---
void loadWavFile(const juce::File& file);
void startPlayback();
void stopPlayback();
bool isPlaying() const;
bool isLoaded() const;
```

New voice engine API should follow the same style:
```cpp
// --- Voice engine control (message-thread callers) ---
void loadVoiceSample(const juce::File& file);
void triggerVoice(uint16_t pitch);
void stopVoice();
```

**Atomic accessor pattern** (lines 92-93 -- existing voice path atomics):
```cpp
// --- ADPCM voice path controls ---
std::atomic<bool>& getGaussEnabled() { return gaussEnabled; }
std::atomic<bool>& getAAFilterEnabled() { return aaFilterEnabled; }
std::atomic<int>& getVoicePitch() { return voicePitch; }
```

New voice engine state atomics should follow the same pattern:
```cpp
// --- Voice engine state (Phase 31: standalone testbed) ---
std::atomic<bool>& getVoiceSampleLoaded() { return voiceSampleLoaded; }
const juce::String& getVoiceSampleName() const { return voiceSampleName; }
uint32_t getVoiceSampleBytes() const { return voiceSampleBytes; }
```

**acceptsMidi conditional** (line 37 -- currently hardcoded false):
```cpp
bool acceptsMidi() const override { return false; }
```

Change to:
```cpp
bool acceptsMidi() const override
{
    return wrapperType == wrapperType_Standalone;
}
```

**Private member pattern** (lines 209-228 -- existing atomic declarations):
```cpp
std::atomic<bool> adpcmEnabled{false}; // ADPCM coloration toggle (D-06)

// Mixer faders (0.0-1.0 float, converted to Q15 at processBlock boundary)
std::atomic<float> dryLevel{0.0f};        // dry bus level (default OFF)
```

---

### `src/plugin/PluginProcessor.cpp` (controller, request-response)

**Analog:** itself -- extend standalone processBlock and add loadVoiceSample

**Message-thread file load pattern** (lines 776-791 -- existing `loadWavFile`):
```cpp
void SPU94AudioProcessor::loadWavFile(const juce::File& file)
{
    auto result = WavLoader::load(file);
    if (!result.has_value())
        return;

    // Write into whichever slot is NOT currently being consumed by the
    // audio thread.  The slot flip + release fence on newWavReady makes
    // the data visible before the audio thread sees the flag (CR-01).
    const auto slot = static_cast<size_t>(1 - pendingWriteSlot.load(std::memory_order_relaxed));
    pendingSlots[slot].L = std::move(result->L);
    pendingSlots[slot].R = std::move(result->R);
    pendingSlots[slot].numFrames = result->numFrames;
    pendingWriteSlot.store(slot, std::memory_order_relaxed);
    newWavReady.store(true, std::memory_order_release);
}
```

New `loadVoiceSample` should follow this exact structural pattern (message-thread, WavLoader::load, validate, write state):
```cpp
void SPU94AudioProcessor::loadVoiceSample(const juce::File& file)
{
    auto result = WavLoader::load(file);
    if (!result.has_value()) return;

    auto* mixer = spu94_get_voice_mixer();

    int32_t bytes = spu94_sample_encode_to_ram(
        result->L.data(),
        static_cast<uint32_t>(result->numFrames),
        mixer->voice_ram,
        0,                        // ram_offset
        SPU94_SPU_RAM_BYTES,      // ram_size
        1                         // loop_enable
    );

    if (bytes > 0) {
        mixer->enabled = 1;
        voiceSampleName = file.getFileName();
        voiceSampleBytes = static_cast<uint32_t>(bytes);
        voiceSampleLoaded.store(true, std::memory_order_release);
    }
}
```

**Standalone processBlock branch** (lines 519-585 -- the `isStandalone` gate and its inner pattern):
```cpp
const bool isStandalone = (wrapperType == wrapperType_Standalone);

if (isStandalone)
{
    // === STANDALONE PATH (v1.6 back-compat) =========================
    const bool wavActive = wavSource.loaded.load(std::memory_order_acquire)
                         && wavSource.playing.load(std::memory_order_relaxed)
                         && wavSource.numFrames > 0;
    // ... existing WAV playback ...
}
```

MIDI dispatch inserts inside this same `if (isStandalone)` block, BEFORE the spu94_process call. The MidiBuffer parameter name changes from `/*midiMessages*/` to `midiMessages`:

**processBlock signature** (line 254-255 -- current form):
```cpp
void SPU94AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midiMessages*/)
```

Changes to:
```cpp
void SPU94AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages)
```

**MIDI iteration pattern** (standard JUCE MidiBuffer iteration inside standalone branch):
```cpp
if (isStandalone)
{
    // MIDI dispatch -- process note events before spu94_process
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            int note = msg.getNoteNumber();
            uint16_t pitch = midiNoteToPitch(note);
            int vel = msg.getVelocity();
            int16_t vol = static_cast<int16_t>((vel * 0x7FFF) / 127);
            int voice = allocateVoice(note);
            spu94_voice_mixer_key_on(spu94_get_voice_mixer(), voice,
                0, pitch, vol, vol, 1, nullptr);
        }
        else if (msg.isNoteOff())
        {
            int voice = findVoiceForNote(msg.getNoteNumber());
            if (voice >= 0)
                spu94_voice_mixer_key_off(spu94_get_voice_mixer(), voice);
        }
    }
    // ... existing WAV playback + spu94_process ...
}
```

**Simple utility method pattern** (lines 793-810 -- existing `startPlayback`/`stopPlayback`):
```cpp
void SPU94AudioProcessor::startPlayback()
{
    wavSource.playing.store(true, std::memory_order_relaxed);
}

void SPU94AudioProcessor::stopPlayback()
{
    wavSource.playing.store(false, std::memory_order_relaxed);
    wavSource.playPos.store(0, std::memory_order_relaxed);
}
```

New `triggerVoice`/`stopVoice` methods follow the same minimal pattern:
```cpp
void SPU94AudioProcessor::triggerVoice(uint16_t pitch)
{
    auto* mixer = spu94_get_voice_mixer();
    spu94_voice_mixer_key_on(mixer, 0, 0, pitch, 0x7FFF, 0x7FFF, 1, nullptr);
}

void SPU94AudioProcessor::stopVoice()
{
    auto* mixer = spu94_get_voice_mixer();
    spu94_voice_mixer_key_off(mixer, 0);
}
```

---

### `src/plugin/PluginEditor.h` (component, event-driven)

**Analog:** itself -- extend existing standalone-gated component declarations

**Standalone-gated button declarations** (lines 23-25):
```cpp
juce::TextButton loadButton{"Load WAV"};
juce::TextButton playButton{"Play"};
juce::TextButton stopButton{"Stop"};
```

New voice panel button declarations follow the same style:
```cpp
// Voice engine panel (standalone-only, Phase 31)
juce::TextButton loadSampleButton{"Load Sample"};
juce::TextButton triggerVoiceButton{"Trigger"};
juce::TextButton stopVoiceButton{"Stop Voice"};
juce::Label voiceSampleLabel;
juce::Slider voiceEnginePitchKnob;
juce::Label voiceEnginePitchLabel;
```

**Knob + label pairs** (lines 61-62 -- existing pattern):
```cpp
juce::Slider inputLevelKnob;
juce::Label inputLevelLabel;
```

---

### `src/plugin/PluginEditor.cpp` (component, event-driven)

**Analog:** itself -- extend existing standalone-gated GUI setup

**Standalone-gated addAndMakeVisible pattern** (lines 15-52 -- the constructor's Load/Play/Stop block):
```cpp
if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    // Load WAV button -- opens async file picker.
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select a WAV file",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.aif");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    processorRef.loadWavFile(result);
                }
            });
    };

    // Play button -- starts playback of loaded WAV through SPU.
    addAndMakeVisible(playButton);
    playButton.onClick = [this]()
    {
        processorRef.startPlayback();
    };

    // Stop button -- stops playback and resets position.
    addAndMakeVisible(stopButton);
    stopButton.onClick = [this]()
    {
        processorRef.stopPlayback();
    };
}
```

New voice panel controls go inside the SAME `if (wrapperType_Standalone)` block, extending it:
```cpp
    // Voice engine panel (Phase 31: standalone testbed UX)
    addAndMakeVisible(loadSampleButton);
    loadSampleButton.onClick = [this]()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select a WAV file for voice engine",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.aif");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                    processorRef.loadVoiceSample(result);
            });
    };

    addAndMakeVisible(triggerVoiceButton);
    triggerVoiceButton.onClick = [this]()
    {
        // Read pitch from the voice engine pitch knob
        processorRef.triggerVoice(/* pitch from knob */);
    };

    addAndMakeVisible(stopVoiceButton);
    stopVoiceButton.onClick = [this]()
    {
        processorRef.stopVoice();
    };
```

**Knob setup pattern** (lines 251-263 -- existing voicePitchKnob setup):
```cpp
voicePitchKnob.setSliderStyle(juce::Slider::Rotary);
voicePitchKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
voicePitchKnob.setRange(4000.0, 44100.0, 1.0);
voicePitchKnob.setValue(22050.0, juce::dontSendNotification);
voicePitchKnob.setSkewFactorFromMidPoint(11025.0);
voicePitchKnob.setTextValueSuffix(" Hz");
voicePitchKnob.onValueChange = [this] {
    double hz = voicePitchKnob.getValue();
    int pitch = (int)(hz / 44100.0 * 0x1000 + 0.5);
    if (pitch < 0x005C) pitch = 0x005C;
    if (pitch > 0x1000) pitch = 0x1000;
    processorRef.getVoicePitch().store(pitch, std::memory_order_relaxed);
};
addAndMakeVisible(voicePitchKnob);
```

**Standalone-gated resized layout pattern** (lines 437-442 -- existing bounds):
```cpp
if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    loadButton.setBounds(10, 10, 100, 30);
    playButton.setBounds(115, 10, 70, 30);
    stopButton.setBounds(190, 10, 70, 30);
}
```

**Label text update in timerCallback** (lines 365-412 -- existing timer polling):
```cpp
void SPU94AudioProcessorEditor::timerCallback()
{
    // Detect file-preset load completion ...
    const int fileCount = processorRef.getFilePresetAppliedCount();
    if (fileCount != lastFilePresetCount)
    {
        lastFilePresetCount = fileCount;
        syncMixerKnobsFromProcessor();
        captureBaseline();
    }
    // ...
}
```

Voice sample status label update goes in the same timerCallback:
```cpp
// Update voice sample status label (standalone only)
if (processorRef.getVoiceSampleLoaded().load(std::memory_order_acquire))
{
    voiceSampleLabel.setText(
        processorRef.getVoiceSampleName() + " " +
        juce::String(processorRef.getVoiceSampleBytes()) + " B",
        juce::dontSendNotification);
}
```

---

## Shared Patterns

### Standalone Wrapper Type Gate
**Source:** `src/plugin/PluginEditor.cpp` line 15 and `src/plugin/PluginProcessor.cpp` line 519
**Apply to:** All new GUI components and all MIDI processing

```cpp
// In editor constructor and resized():
if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    // standalone-only components
}

// In processBlock:
const bool isStandalone = (wrapperType == wrapperType_Standalone);
if (isStandalone)
{
    // MIDI processing, voice engine dispatch
}
```

### Atomic State Communication (Message Thread to Audio Thread)
**Source:** `src/plugin/PluginProcessor.h` lines 63-99 and `src/plugin/PluginProcessor.cpp` lines 793-810
**Apply to:** Voice sample loaded flag, voice trigger/stop commands

```cpp
// Declaration (header):
std::atomic<bool> voiceSampleLoaded{false};

// Write from message thread:
voiceSampleLoaded.store(true, std::memory_order_release);

// Read from audio thread:
voiceSampleLoaded.load(std::memory_order_acquire);
```

### File Chooser Async Pattern
**Source:** `src/plugin/PluginEditor.cpp` lines 19-37
**Apply to:** Load Sample button

```cpp
fileChooser = std::make_unique<juce::FileChooser>(
    "prompt text",
    juce::File::getSpecialLocation(juce::File::userMusicDirectory),
    "*.wav;*.aiff;*.aif");

fileChooser->launchAsync(
    juce::FileBrowserComponent::openMode |
    juce::FileBrowserComponent::canSelectFiles,
    [this](const juce::FileChooser& fc)
    {
        auto result = fc.getResult();
        if (result.existsAsFile())
        {
            processorRef.loadVoiceSample(result);
        }
    });
```

### Button onClick Callback Pattern
**Source:** `src/plugin/PluginEditor.cpp` lines 40-51
**Apply to:** Trigger and Stop Voice buttons

```cpp
addAndMakeVisible(triggerVoiceButton);
triggerVoiceButton.onClick = [this]()
{
    processorRef.triggerVoice(/* pitch */);
};
```

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | -- | -- | All Phase 31 work modifies existing files with existing patterns |

**Note:** The MIDI dispatch loop and round-robin voice allocation are new code without a direct analog in the codebase. However, they follow standard JUCE MidiBuffer iteration (documented in JUCE API) and the `spu94_voice_mixer_key_on/key_off` API signatures are fully specified in `include/spu94/spu94_voice.h` lines 134-141.

## Metadata

**Analog search scope:** `src/plugin/`, `include/spu94/`, `src/standalone/`
**Files scanned:** 6 (PluginProcessor.h, PluginProcessor.cpp, PluginEditor.h, PluginEditor.cpp, spu94_voice.h, spu94_sample_loader.h)
**Pattern extraction date:** 2026-05-16
