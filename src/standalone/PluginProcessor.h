#pragma once

#include <JuceHeader.h>
#include "ParameterBridge.h"
#include <array>
#include <atomic>
#include <vector>
#include <cstdint>

extern "C" {
#include <spu94/spu94.h>
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

private:
    std::atomic<float> inputLevel{0.25f}; // [0.0 = silence, 1.0 = unity gain]
    std::atomic<bool> adpcmEnabled{false}; // ADPCM coloration toggle (D-06)

    // Mixer faders (0.0-1.0 float, converted to Q15 at processBlock boundary)
    std::atomic<float> dryLevel{1.0f};        // dry bus level
    std::atomic<float> patinaLevel{0.0f};     // ADPCM bus level
    std::atomic<float> reverbLevel{1.0f};     // reverb return level
    std::atomic<float> adpcmSend{0.0f};       // ADPCM bus reverb send
    std::atomic<float> drySend{1.0f};         // dry bus reverb send

    // Latency compensation (default ON per D-07)
    std::atomic<bool> latencyCompEnabled{true};

    // DAC coloration toggles (default OFF)
    std::atomic<bool> dacEnabled{false};
    std::atomic<bool> dacFirEnabled{true};    // sub-toggle: ON when DAC section is used
    std::atomic<bool> dacNoiseEnabled{true};  // sub-toggle: ON when DAC section is used
    std::atomic<bool> dacTrueOversample{true}; // v1.3 true 8x path (default ON)
    RegisterBridge registerBridge;
    PresetCommandQueue presetQueue;
    // SPU state -- caller-owned buffers per libspu94 API contract
    juce::HeapBlock<unsigned char> stateBuf;
    juce::HeapBlock<unsigned char> workBuf;
    spu94_state* spu = nullptr;

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
