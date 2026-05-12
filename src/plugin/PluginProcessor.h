#pragma once

#include <JuceHeader.h>
#include "ParameterBridge.h"
#include "SrcChain.h"
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

    // -1 when not editing; 0..7 while Advanced view is open for that slot.
    std::atomic<int>& getCurrentEditingSlot() { return currentEditingSlot; }

    // Snapshot the engine's current 35 reverb registers into user_slots[slot]
    // and mark it filled. Safe to call from the message thread; the underlying
    // C API is rt-safe (no heap, no locks).
    void saveUserSlot(int slot);

    // Mark a user slot empty.
    void clearUserSlot(int slot);

    // Force the next processBlock to re-run spu94_interp_set_morph for the
    // current position even if the position atomic hasn't changed. Used after
    // SAVE so a new slot's contents take effect immediately.
    void requestMorphReapply() { morphReapplyPending.store(true, std::memory_order_relaxed); }

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

    // GUI thread: polled by editor timer to know when the audio thread
    // has just synced shadows from the SPU. Used to refresh slider
    // positions after view switches and per-slot loads.
    int getShadowSyncCount() const { return shadowSyncCompletedCount.load(std::memory_order_acquire); }

    // Per-tick user-waypoint export / load. Each operation targets a single
    // slot (the one the encoder is parked on). exportUserSlot serializes
    // that slot's regs to text; loadUserSlot reads a single-slot file and
    // stamps it into target_slot regardless of the file's own slot index.
    juce::String exportUserSlotToString(int slot,
                                        const juce::String& name,
                                        const juce::String& description);
    bool loadUserSlotFromString(int target_slot,
                                const juce::String& presetText);

private:
    // inputLevel: pre-clamp float gain factor on the plugin path.
    // Range 0.0..16.0 (linear). Anchor points:
    //   0.0  -> silence
    //   0.5  -> -6 dB  (default; -6 dB headroom below int16 ceiling)
    //   1.0  -> unity
    //   16.0 -> +24 dB drive (intentionally clamps via sat_s16, North Star quirk)
    // Standalone path still applies this value as a Q15 engine register
    // at processBlock atomic-sync; that path is unchanged (internal testbed only).
    std::atomic<float> inputLevel{0.50f};

    // Public anchors for Phase 24 (AudioProcessorParameter wiring).
    // These are documentation anchors -- NOT used to re-initialize the
    // atomic (would tempt drift). Phase 24 will consume them when wiring
    // the AudioProcessorParameter range.
    static constexpr float kInputGainDefault = 0.5f;
    static constexpr float kInputGainMax     = 16.0f;

    // Per-channel host-rate float scratch for the pre-clamp Input Gain
    // multiply. Allocated in prepareToPlay to kMaxBlock=4096 samples.
    // Plugin path only -- standalone path uses the engine-register gain
    // path and does not touch this scratch.
    juce::HeapBlock<float> inputGainScratch_[2];

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
    std::atomic<int>  currentEditingSlot{-1}; // -1 = not editing; 0..7 = slot under edit
    std::atomic<bool> morphReapplyPending{false}; // GUI forces morph re-apply on next block
    float lastMorphPosition{-1.0f}; // audio-thread only: tracks last applied position

    // Per-sample register slewing (Phase 18)
    int16_t targetRegs[SPU94_REG__COUNT] = {};
    bool morphInitialized = false;

    // Morph Speed — 0.0 = Instant Snap (no slew), 1.0 = Glide (full slew
    // duration). Continuous in between scales the slew sample budget.
    // Default 0.5 (mid-glide) -- the polished launch default sits between
    // hardware-faithful snap and v1.6 full glide.
    std::atomic<float> morphSpeed{0.5f};

    // Morph Grit (see getMorphGrit for full docs).
    // Default 0 = Int — all reads integer, PS1 hardware faithful.
    // Reflects the project's north star: fixed-point quirks are the point.
    std::atomic<int> morphGrit{0};

    // File preset pending load mechanism (message -> audio thread handoff)
    std::array<char, SPU94_PRESET_BUF_SIZE> pendingPresetBuf{};
    std::atomic<size_t> pendingPresetLen{0};
    std::atomic<bool> filePresetReady{false};
    // -1 = full preset load, 0..7 = single-slot load into that target slot.
    std::atomic<int>  pendingTargetSlot{-1};
    std::atomic<int> filePresetAppliedCount{0};
    std::atomic<int> shadowSyncCompletedCount{0};

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

    // Phase 22: bidirectional libsamplerate SRC sandwich around the
    // SPU-94 core. Allocated in prepareToPlay (via srcChain_.prepare),
    // released in releaseResources. Runs the impulse-measured group
    // delay so setLatencySamples reports the correct PDC number.
    SrcChain srcChain_;
    double   hostSampleRate_ { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPU94AudioProcessor)
};
