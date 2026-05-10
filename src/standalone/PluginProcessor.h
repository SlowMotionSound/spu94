#pragma once

#include <JuceHeader.h>
#include "ParameterBridge.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

extern "C" {
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
}

class SPU94AudioProcessor : public juce::AudioProcessor
{
public:
    SPU94AudioProcessor();
    ~SPU94AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midiMessages) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- WAV playback control (message-thread callers) ---
    void loadWavFile(const juce::File& file);
    void startPlayback();
    void stopPlayback();
    bool isPlaying() const;
    bool isLoaded() const;

    // --- Parameter bridge (Plan 03: lock-free GUI <-> audio handoff) ---
    RegisterBridge& getRegisterBridge() { return registerBridge; }
    PresetCommandQueue& getPresetQueue() { return presetQueue; }

    // --- Input level (pre-SPU gain staging) ---
    std::atomic<float>& getInputLevel() { return inputLevel; }

    // --- ADPCM coloration toggle (ADPCM-IO-06, D-06) ---
    std::atomic<bool>& getAdpcmEnabled() { return adpcmEnabled; }

    // --- Mixer faders (0.0-1.0 float, converted to Q15 at processBlock boundary) ---
    std::atomic<float>& getDryLevel() { return dryLevel; }
    std::atomic<float>& getPatinaLevel() { return patinaLevel; }
    std::atomic<float>& getReverbLevel() { return reverbLevel; }
    std::atomic<float>& getAdpcmSend() { return adpcmSend; }
    std::atomic<float>& getDrySend() { return drySend; }

    // --- Latency compensation (D-07: default ON) ---
    std::atomic<bool>& getLatencyCompEnabled() { return latencyCompEnabled; }

    // --- DAC coloration toggles (D-09 through D-12) ---
    std::atomic<bool>& getDacEnabled() { return dacEnabled; }
    std::atomic<bool>& getDacFirEnabled() { return dacFirEnabled; }
    std::atomic<bool>& getDacNoiseEnabled() { return dacNoiseEnabled; }
    std::atomic<bool>& getDacTrueOversample() { return dacTrueOversample; }

    // --- Morph position (Phase 17, D-09/D-10: preset interpolation engine) ---
    std::atomic<float>& getMorphPosition() { return morphPosition; }
    std::atomic<bool>& getMorphActive() { return morphActive; }
    void requestShadowSync() { needShadowSync.store(true, std::memory_order_relaxed); }

    // --- Morph Speed: 0.0 = Instant Snap (registers latch at next tick),
    //     1.0 = Glide (full slew duration). In between scales slew duration. ---
    std::atomic<float>& getMorphSpeed() { return morphSpeed; }

    // --- Morph Grit (binary):
    //     0 = Int (default — all integer reads, hardware-faithful, "alive")
    //     1 = Fract (all fractional reads, smoothed) ---
    std::atomic<int>& getMorphGrit() { return morphGrit; }

    // --- User-programmable waypoint slots (8 slots at midpoints between
    //     Sony's 9 anchors; slot N at morph position (2N+1)/16). ---
    bool isUserSlotFilled(int slot) const;

    // --- File preset save/load (Phase 14, PRE-08/PRE-09) ---

    // Message thread: serialize current engine state to a .spu94 text buffer.
    // Returns the text as a juce::String, or empty string on failure.
    // name and description are metadata fields written to the preset header.
    juce::String savePresetToString(const juce::String& name,
                                    const juce::String& description);

    // Message thread: apply a .spu94 text buffer to the engine.
    // Parses the text, applies to SPU state on the audio thread via a
    // pending-file mechanism, and syncs all GUI-facing atomics.
    // Returns true on success.
    bool loadPresetFromString(const juce::String& presetText);

    // GUI thread: poll for file-preset load completion (analogous to
    // getAppliedCount for factory presets).
    int getFilePresetAppliedCount() const;

private:
    std::atomic<float> inputLevel{0.50f}; // [0.0 = silence, 1.0 = unity gain]
    std::atomic<bool> adpcmEnabled{false}; // ADPCM coloration toggle (D-06)

    // Mixer faders (0.0-1.0 float, converted to Q15 at processBlock boundary)
    std::atomic<float> dryLevel{0.0f};        // dry bus level (default OFF)
    std::atomic<float> patinaLevel{0.0f};     // ADPCM bus level
    std::atomic<float> reverbLevel{1.0f};     // reverb return level
    std::atomic<float> adpcmSend{1.0f};       // ADPCM bus reverb send (default FULL)
    std::atomic<float> drySend{0.0f};         // dry bus reverb send (default OFF)

    // Latency compensation (default ON per D-07)
    std::atomic<bool> latencyCompEnabled{true};

    // DAC coloration toggles (default ON)
    std::atomic<bool> dacEnabled{true};
    std::atomic<bool> dacFirEnabled{true};    // sub-toggle: ON when DAC section is used
    std::atomic<bool> dacNoiseEnabled{true};  // sub-toggle: ON when DAC section is used
    std::atomic<bool> dacTrueOversample{true}; // v1.3 true 8x path (default ON)

    // Morph position (Phase 17: preset interpolation engine, D-09/D-10)
    // 0.625 = Hall preset position (waypoint 5 of 8 = 5/8), matching default SPU94_PRESET_HALL
    std::atomic<float> morphPosition{0.625f};
    std::atomic<bool> morphActive{true}; // true = morph engine owns reverb registers
    std::atomic<bool> needShadowSync{false}; // GUI requests shadow sync on mode switch
    float lastMorphPosition{-1.0f}; // audio-thread only: tracks last applied position

    // Per-sample register slewing (Phase 18)
    int16_t targetRegs[SPU94_REG__COUNT] = {};
    bool morphInitialized = false;

    // Morph Speed — 0.0 = Instant Snap (no slew), 1.0 = Glide (full slew
    // duration). Continuous in between scales the slew sample budget.
    // Default 1.0 = full Glide character (the polished v1.6 default).
    std::atomic<float> morphSpeed{1.0f};

    // Morph Grit (see getMorphGrit for full docs).
    // Default 0 = Int — all reads integer, PS1 hardware faithful.
    // Reflects the project's north star: fixed-point quirks are the point.
    std::atomic<int> morphGrit{0};

    // File preset pending load mechanism (message -> audio thread handoff)
    std::array<char, 4096> pendingPresetBuf{};
    std::atomic<size_t> pendingPresetLen{0};
    std::atomic<bool> filePresetReady{false};
    std::atomic<int> filePresetAppliedCount{0};

    RegisterBridge registerBridge;
    PresetCommandQueue presetQueue;
    // engines[0] = audio path. Slew rate is set per-morph from the
    //   Register Behavior knob: 0 = direct write (registers latch at next
    //   tick → click character of original v1.5), 1 = full slew (glide).
    // engines[1] = scratch (computes target registers for engines[0]'s slew).
    juce::HeapBlock<unsigned char> stateBufA, stateBufB;
    juce::HeapBlock<unsigned char> workBufA, workBufB;
    spu94_state* engines[2] = { nullptr, nullptr };

    // WAV playback source. Message thread writes pendingL/R + sets
    // newWavReady; audio thread swaps into the live source struct.
    struct WavSource {
        std::vector<int16_t> L, R;
        uint64_t numFrames = 0;
        std::atomic<uint64_t> playPos{0};
        std::atomic<bool> playing{false};
        std::atomic<bool> loaded{false};
    };
    WavSource wavSource;

    // Double-buffered pending WAV data.  Message thread fills one slot;
    // audio thread consumes from whichever slot the generation counter
    // points at.  Two slots prevent a data race when loadWavFile() is
    // called while the audio thread is mid-swap (CR-01).  The old
    // wavSource vectors are swapped *into* the consumed slot, so the
    // heap deallocation happens on the next message-thread load rather
    // than on the audio thread (WR-01).
    struct PendingWav {
        std::vector<int16_t> L, R;
        uint64_t numFrames = 0;
    };
    std::array<PendingWav, 2> pendingSlots;
    std::atomic<int> pendingWriteSlot{0};
    std::atomic<bool> newWavReady{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPU94AudioProcessor)
};
