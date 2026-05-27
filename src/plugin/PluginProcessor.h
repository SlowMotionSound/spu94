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
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_registers.h>
#include <spu94/spu94_voice.h>
#include <spu94/spu94_sample_loader.h>
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

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;

    bool acceptsMidi() const override
    {
        return wrapperType == wrapperType_Standalone;
    }
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

    // --- Voice engine control (message-thread callers, Phase 31) ---
    void loadVoiceSample(const juce::File& file);
    void triggerVoice(uint16_t pitch);
    void stopVoice();
    void setGuiVoicePitch(uint16_t pitch) { guiVoicePitch.store(pitch, std::memory_order_relaxed); }
    void setGuiVoiceVolL(int16_t v) { guiVoiceVolL.store(v, std::memory_order_relaxed); }
    void setGuiVoiceVolR(int16_t v) { guiVoiceVolR.store(v, std::memory_order_relaxed); }
    void setEncodeRate(int rateHz) { encodeRate.store(rateHz, std::memory_order_relaxed); }
    int  getEncodeRate() const { return encodeRate.load(std::memory_order_relaxed); }
    uint32_t getRamUsed() const { return ramUsed.load(std::memory_order_relaxed); }
    void setSampleStartPos(double pos) { sampleStartPos.store(pos, std::memory_order_relaxed); }
    void setSampleEndPos(double pos) { sampleEndPos.store(pos, std::memory_order_relaxed); }
    void setSampleLoopPos(double pos) { sampleLoopPos.store(pos, std::memory_order_relaxed); }
    std::atomic<bool>& getLoopModeEnabled() { return loopModeEnabled; }
    std::atomic<bool>& getSamplerAAEnabled() { return samplerAAEnabled; }
    std::atomic<bool>& getGuiVoiceNon() { return guiVoiceNon; }
    std::atomic<bool>& getGuiVoicePmon() { return guiVoicePmon; }
    std::atomic<int>& getNoiseShift() { return noiseShift; }

    // Tremolo controls (Phase 44: continuous VCA oscillation via retrigger)
    std::atomic<bool>&  getTremoloEnabled()  { return tremoloEnabled; }
    std::atomic<float>& getTremoloSpeedHz()  { return tremoloSpeedHz; }
    std::atomic<float>& getTremoloDepth()    { return tremoloDepth; }
    std::atomic<int>&   getTremoloCurve()    { return tremoloCurve; }
    std::atomic<float>& getTremoloRatio()    { return tremoloRatio; }

    // Auto-pan controls (Phase 45: opposition-phase L/R sweep for stereo movement)
    std::atomic<bool>&  getAutoPanEnabled()  { return autoPanEnabled; }
    std::atomic<float>& getAutoPanSpeedHz()  { return autoPanSpeedHz; }
    std::atomic<float>& getAutoPanDepth()    { return autoPanDepth; }
    std::atomic<float>& getAutoPanRatio()    { return autoPanRatio; }

    // AM synthesis controls (Phase 48: audio-rate retrigger for metallic sidebands)
    std::atomic<bool>&  getAmEnabled()  { return amEnabled; }
    std::atomic<float>& getAmRateHz()   { return amRateHz; }
    std::atomic<float>& getAmDepth()    { return amDepth; }
    std::atomic<int>&   getAmCurve()    { return amCurve; }

    // Ring mod controls (Phase 52: bipolar sweep for phase-inversion ring mod effect)
    std::atomic<bool>&  getRingModEnabled()  { return ringModEnabled; }
    std::atomic<float>& getRingModRateHz()   { return ringModRateHz; }
    std::atomic<float>& getRingModDepth()    { return ringModDepth; }
    std::atomic<int>&   getRingModCurve()    { return ringModCurve; }

    // Sweep shape (Phase 53: shared by all VCA ramp effects -- triangle/saw down/saw up)
    std::atomic<int>&   getSweepShape()    { return sweepShape; }

    // Sidechain duck controls (Phase 46: event-triggered VCA duck via KON detection)
    std::atomic<int>&   getDuckSource(int voice) { return duckSource[voice]; }
    std::atomic<float>& getDuckRelease(int voice) { return duckRelease[voice]; }
    std::atomic<float>& getDuckDepth(int voice) { return duckDepth[voice]; }
    std::atomic<float>& getDuckAttack(int voice) { return duckAttack[voice]; }

    // Unified effect mode selector (Phase 54: GUI state only, does not affect DSP routing)
    // 0=Auto-Pan, 1=Tremolo, 2=AM, 3=Ring Mod, 4=Ducking
    std::atomic<int>&   getEffectMode() { return effectMode; }
    std::atomic<bool>&  getEffectModeChanged() { return effectModeChanged; }

    // Internal mod bus controls (Phase 50: per-voice noise-to-pitch/vol/pan routing)
    std::atomic<float>& getModBusPitchDepth() { return modBusPitchDepth; }
    std::atomic<float>& getModBusVolDepth()   { return modBusVolDepth; }
    std::atomic<float>& getModBusPanDepth()   { return modBusPanDepth; }

    // ADSR parameter accessors (standalone sampler voice)
    std::atomic<bool>&  getAdsrEnabled()     { return adsrEnabled; }
    std::atomic<float>& getAdsrAttack()      { return adsrAttack; }
    std::atomic<bool>&  getAdsrAttackExp()   { return adsrAttackExp; }
    std::atomic<float>& getAdsrDecay()       { return adsrDecay; }
    std::atomic<float>& getAdsrSustainLvl()  { return adsrSustainLvl; }
    std::atomic<float>& getAdsrSustainRate() { return adsrSustainRate; }
    std::atomic<bool>&  getAdsrSustainExp()  { return adsrSustainExp; }
    std::atomic<float>& getAdsrRelease()     { return adsrRelease; }
    std::atomic<bool>&  getAdsrReleaseExp()  { return adsrReleaseExp; }
    spu94_adsr_state_t buildAdsrConfig() const;
    float getAdsrAttackSeconds() const;
    float getAdsrDecaySeconds() const;
    float getAdsrReleaseSeconds() const;
    float adsrAttackSecondsForKnob(float knob) const;
    float adsrDecaySecondsForKnob(float knob) const;
    float adsrReleaseSecondsForKnob(float knob) const;

    // Waveform display data — stashed on load for the GUI thumbnail
    const std::vector<int16_t>& getWaveformData() const { return waveformData; }
    uint64_t getWaveformFrames() const { return waveformFrames; }

    // Playback position for waveform display (normalized 0..1)
    double getVoicePlaybackPos() const;

    // --- Parameter bridge (Plan 03: lock-free GUI <-> audio handoff) ---
    RegisterBridge& getRegisterBridge() { return registerBridge; }
    PresetCommandQueue& getPresetQueue() { return presetQueue; }

    // --- Input level (pre-SPU gain staging) ---
    std::atomic<float>& getInputLevel() { return inputLevel; }

    // --- ADPCM coloration toggle (ADPCM-IO-06, D-06) ---
    std::atomic<bool>& getAdpcmEnabled() { return adpcmEnabled; }

    // --- Mixer faders (0.0-1.0 float, converted to Q15 at processBlock boundary) ---
    std::atomic<float>& getDryLevel() { return dryLevel; }
    std::atomic<float>& getAdpcmLevel() { return adpcmLevel; }
    std::atomic<float>& getReverbLevel() { return reverbLevel; }
    std::atomic<float>& getAdpcmSend() { return adpcmSend; }
    std::atomic<float>& getDrySend() { return drySend; }

    // --- Host-automatable AudioParameterFloat accessors (Phase 24 PLUG-28) ---
    // Non-owning raw pointers. JUCE owns the params after addParameter.
    // Registration order is FROZEN per PLUG-30 (AU index stability).
    juce::AudioParameterFloat* getParamInputGain()    const { return paramInputGain; }
    juce::AudioParameterFloat* getParamAdpcmSend()    const { return paramAdpcmSend; }
    juce::AudioParameterFloat* getParamDrySend()      const { return paramDrySend; }
    juce::AudioParameterFloat* getParamMorphPosition() const { return paramMorphPosition; }
    juce::AudioParameterFloat* getParamMorphSpeed()   const { return paramMorphSpeed; }
    juce::AudioParameterFloat* getParamMorphGrit()    const { return paramMorphGrit; }
    juce::AudioParameterFloat* getParamDryLevel()     const { return paramDryLevel; }
    juce::AudioParameterFloat* getParamAdpcmLevel()   const { return paramAdpcmLevel; }
    juce::AudioParameterFloat* getParamReverbLevel()  const { return paramReverbLevel; }

    // --- Latency compensation (D-07: default ON) ---
    std::atomic<bool>& getLatencyCompEnabled() { return latencyCompEnabled; }

    // --- ADPCM voice path controls ---
    std::atomic<bool>& getGaussEnabled() { return gaussEnabled; }
    std::atomic<bool>& getAAFilterEnabled() { return aaFilterEnabled; }
    std::atomic<int>& getVoicePitch() { return voicePitch; }

    // --- Voice engine mixer (standalone-only) ---
    std::atomic<float>& getSamplerFader() { return samplerFader; }
    std::atomic<float>& getSamplerSend() { return samplerSend; }
    std::atomic<float>& getSamplerDrive() { return samplerDrive; }

    // --- GUI voice volume (Phase 34: signed volume for GUI trigger path) ---
    std::atomic<int16_t>& getGuiVoiceVolL() { return guiVoiceVolL; }
    std::atomic<int16_t>& getGuiVoiceVolR() { return guiVoiceVolR; }

    // --- Voice engine state (Phase 31: standalone testbed) ---
    std::atomic<bool>& getVoiceSampleLoaded() { return voiceSampleLoaded; }
    const juce::String& getVoiceSampleName() const { return voiceSampleName; }
    uint32_t getVoiceSampleBytes() const { return voiceSampleBytes; }

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
    std::atomic<int>& getMorphSpeedRange() { return morphSpeedRange; }

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
    // --- Host-automatable AudioParameterFloat pointers (Phase 24 PLUG-28).
    //     Non-owning: JUCE owns the params after addParameter.
    //     Registration order is FROZEN per PLUG-30 (AU index stability).
    //     Future params MUST be added at the END of this list. ---
    juce::AudioParameterFloat* paramInputGain     = nullptr;
    juce::AudioParameterFloat* paramAdpcmSend     = nullptr;
    juce::AudioParameterFloat* paramDrySend       = nullptr;
    juce::AudioParameterFloat* paramMorphPosition = nullptr;
    juce::AudioParameterFloat* paramMorphSpeed    = nullptr;
    juce::AudioParameterFloat* paramMorphGrit     = nullptr;
    juce::AudioParameterFloat* paramDryLevel      = nullptr;
    juce::AudioParameterFloat* paramAdpcmLevel    = nullptr;
    juce::AudioParameterFloat* paramReverbLevel   = nullptr;

    // inputLevel: pre-clamp float gain factor. Range 0.0..16.0 (linear).
    // Anchor points:
    //   0.0  -> silence
    //   0.5  -> -6 dB  (default; -6 dB headroom below int16 ceiling)
    //   1.0  -> unity
    //   16.0 -> +24 dB drive (intentionally clamps via sat_s16, North Star quirk)
    // Applied as a pre-clamp float multiply on BOTH the plugin and standalone
    // paths (post-Phase-23 UAT correction). The engine register
    // `spu94_set_input_gain` is pinned at 0x7FFF on both paths.
    std::atomic<float> inputLevel{0.50f};

    // Public anchors for Phase 24 (AudioProcessorParameter wiring).
    // These are documentation anchors -- NOT used to re-initialize the
    // atomic (would tempt drift). Phase 24 will consume them when wiring
    // the AudioProcessorParameter range.
    static constexpr float kInputGainDefault = 0.5f;
    static constexpr float kInputGainMax     = 16.0f;

    // Per-channel host-rate float scratch for the pre-clamp Input Gain
    // multiply on the plugin path. Allocated in prepareToPlay to
    // kMaxBlock=4096 samples. Standalone path applies the same pre-clamp
    // gain inline (per-sample int16 -> toFloat -> applyInputGain -> toInt16)
    // and does not need this scratch.
    juce::HeapBlock<float> inputGainScratch_[2];

    std::atomic<bool> adpcmEnabled{false}; // ADPCM coloration toggle (D-06)

    // Mixer faders (0.0-1.0 float, converted to Q15 at processBlock boundary)
    std::atomic<float> dryLevel{0.0f};        // dry bus level (default OFF)
    std::atomic<float> adpcmLevel{0.0f};     // ADPCM bus level
    std::atomic<float> reverbLevel{1.0f};     // reverb return level
    std::atomic<float> adpcmSend{1.0f};       // ADPCM bus reverb send (default FULL)
    std::atomic<float> drySend{0.0f};         // dry bus reverb send (default OFF)

    // Voice engine mixer (standalone-only, not host-automated)
    std::atomic<float> samplerFader{1.0f};
    std::atomic<float> samplerSend{1.0f};
    std::atomic<float> samplerDrive{1.0f};
    std::atomic<int>   encodeRate{22050};
    std::atomic<uint32_t> ramUsed{0};

    // Latency compensation (default ON per D-07)
    std::atomic<bool> latencyCompEnabled{true};

    // DAC coloration toggles (default ON)
    std::atomic<bool> gaussEnabled{true};
    std::atomic<bool> aaFilterEnabled{true};
    std::atomic<int> voicePitch{0x0800};
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

    // Morph Speed Range: 0 = Fast (0–0.5s), 1 = Slow (0.5s–60s).
    std::atomic<int> morphSpeedRange{0};

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

    // Voice engine state (Phase 31: standalone testbed)
    std::atomic<bool> voiceSampleLoaded{false};
    juce::String voiceSampleName;
    uint32_t voiceSampleBytes{0};
    std::vector<spu94_adpcm_state> adpcmStateCache;
    int8_t noteForVoice[24] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                               -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    int nextVoice{0};

    // Pending GUI trigger/stop — staged on message thread, applied on audio thread
    std::atomic<uint16_t> pendingGuiTriggerPitch{0}; // 0 = no pending trigger
    std::atomic<bool> pendingGuiStop{false};
    // Live pitch for GUI-triggered voice 0 — updated every audio callback
    std::atomic<uint16_t> guiVoicePitch{0x800};
    // Per-voice signed volume for GUI trigger path (Phase 34: signed volume).
    // Full PS1 range: -0x4000..+0x3FFF (-16384..+16383). Default = max positive.
    std::atomic<int16_t> guiVoiceVolL{0x3FFF};
    std::atomic<int16_t> guiVoiceVolR{0x3FFF};
    // Sample start/end/loop positions (normalized 0..1) from waveform markers
    std::atomic<double> sampleStartPos{0.0};
    std::atomic<double> sampleEndPos{1.0};
    std::atomic<double> sampleLoopPos{0.0};
    std::atomic<bool> loopModeEnabled{false};
    std::atomic<bool> samplerAAEnabled{true};  // AA-03: true = Gauss interp ON (PS1 faithful)
    // Per-voice NON/PMON toggles (Phase 40: voice feature toggles)
    std::atomic<bool> guiVoiceNon{false};   // NON: replace ADPCM output with noise generator
    std::atomic<bool> guiVoicePmon{false};  // PMON: pitch modulation from previous voice
    std::atomic<int> noiseShift{10};        // noise generator shift (0-15), controls noise color/frequency

    // VCA ramp controls (Phase 41: volume sweep GUI surface)

    // Tremolo controls (Phase 44: continuous VCA oscillation via retrigger)
    std::atomic<bool>  tremoloEnabled{false};   // true = sweep configured for retrigger oscillation
    std::atomic<float> tremoloSpeedHz{5.0f};    // oscillation rate in Hz (0.5..20.0)
    std::atomic<float> tremoloDepth{1.0f};      // modulation depth (0.0 = no mod, 1.0 = full range)
    std::atomic<int>   tremoloCurve{0};         // 0 = linear (triangle), 1 = exponential (asymmetric)
    std::atomic<float> tremoloRatio{1.0f};      // L/R rate ratio (0.5..4.0; 1.0 = same both channels)

    // Audio-thread-only tremolo state (not atomic -- only accessed from processBlock)
    bool  tremoloWasActive{false};
    float lastTremoloHz{-1.0f};
    int   lastTremoloCurve{-1};
    float lastTremoloRatio{-1.0f};

    // Auto-pan controls (Phase 45: opposition-phase L/R sweep for stereo movement)
    std::atomic<bool>  autoPanEnabled{false};     // true = sweep configured for opposition-phase L/R
    std::atomic<float> autoPanSpeedHz{2.0f};      // oscillation rate in Hz (0.5..43.0)
    std::atomic<float> autoPanDepth{1.0f};        // pan depth (0.0 = no pan, 1.0 = full L-to-R)
    std::atomic<float> autoPanRatio{1.0f};        // L/R rate ratio (0.5..4.0; 1.0 = symmetric)

    // Audio-thread-only auto-pan state (not atomic -- only accessed from processBlock)
    bool  autoPanWasActive{false};
    float lastAutoPanHz{-1.0f};
    float lastAutoPanRatio{-1.0f};

    // AM synthesis controls (Phase 48: audio-rate retrigger for metallic sidebands)
    std::atomic<bool>  amEnabled{false};       // true = sweep configured for audio-rate AM
    std::atomic<float> amRateHz{440.0f};       // modulation rate in Hz (37..9647, default musical A)
    std::atomic<float> amDepth{1.0f};          // modulation depth (0.0 = no mod, 1.0 = full metallic)
    std::atomic<int>   amCurve{0};             // 0 = linear (symmetric), 1 = exponential (asymmetric)

    // Audio-thread-only AM state (not atomic -- only accessed from processBlock)
    bool  amWasActive{false};
    float lastAmHz{-1.0f};
    int   lastAmCurve{-1};

    // Ring mod controls (Phase 52: bipolar sweep for phase-inversion ring mod effect)
    std::atomic<bool>  ringModEnabled{false};     // true = sweep configured for bipolar oscillation
    std::atomic<float> ringModRateHz{440.0f};     // oscillation rate in Hz (audio-rate, uses kAmHzTable)
    std::atomic<float> ringModDepth{1.0f};        // depth (0.0=no mod, 0.5=zero boundary, 1.0=full inversion)
    std::atomic<int>   ringModCurve{0};           // 0 = linear (symmetric), 1 = exponential (asymmetric)

    // Audio-thread-only ring mod state (not atomic -- only accessed from processBlock)
    bool  ringModWasActive{false};
    float lastRingModHz{-1.0f};
    int   lastRingModCurve{-1};

    // Sweep shape (Phase 53: shared by all VCA ramp effects)
    std::atomic<int>   sweepShape{0};         // 0=Triangle, 1=Saw Down, 2=Saw Up

    // Audio-thread-only shape state (not atomic -- only accessed from processBlock)
    int   lastSweepShape{0};

    // Sidechain duck controls (Phase 46: event-triggered VCA duck via KON detection)
    // Per-voice: which voice's KON triggers duck (-1 = none, 0-23 = source voice)
    std::atomic<int>   duckSource[24] = {
        {-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},
        {-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1},{-1}};
    // Recovery speed in seconds (0.03-6.8, same range as VCA ramp)
    std::atomic<float> duckRelease[24] = {
        {0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},
        {0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f},{0.4f}};
    // How far volume drops (0.0 = no duck, 1.0 = full silence)
    std::atomic<float> duckDepth[24] = {
        {1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},
        {1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f},{1.0f}};
    // Duck attack time in seconds (0.001-0.5, controls how fast volume drops)
    std::atomic<float> duckAttack[24] = {
        {0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},
        {0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f},{0.01f}};

    // Unified effect mode selector (Phase 54: GUI state only)
    // 0=Auto-Pan, 1=Tremolo, 2=AM, 3=Ring Mod, 4=Ducking
    std::atomic<int> effectMode{0};
    std::atomic<bool> effectModeChanged{false};

    // Internal mod bus controls (Phase 50: per-voice noise-to-pitch/vol/pan routing)
    // Each 0.0..1.0, converted to int16_t depth in processBlock before passing to C core.
    std::atomic<float> modBusPitchDepth{0.0f};
    std::atomic<float> modBusVolDepth{0.0f};
    std::atomic<float> modBusPanDepth{0.0f};

    // Audio-thread-only duck state (NOT atomic -- only touched in processBlock)
    enum DuckPhase { DUCK_IDLE = 0, DUCK_DECREASING, DUCK_RECOVERING };
    DuckPhase duckState[24] = {};          // all DUCK_IDLE
    int16_t   duckOrigLevel_l[24] = {};    // volume level before duck started
    int16_t   duckOrigLevel_r[24] = {};

    // ADSR parameters (standalone sampler voice)
    std::atomic<bool>  adsrEnabled{true};
    std::atomic<float> adsrAttack{0.0f};
    std::atomic<bool>  adsrAttackExp{false};
    std::atomic<float> adsrDecay{0.0f};
    std::atomic<float> adsrSustainLvl{1.0f};
    std::atomic<float> adsrSustainRate{0.0f};
    std::atomic<bool>  adsrSustainExp{false};
    std::atomic<float> adsrRelease{0.0f};
    std::atomic<bool>  adsrReleaseExp{false};

    std::atomic<bool> pendingMixerEnable{false};

    // Waveform display data (stashed on sample load for GUI thumbnail)
    std::vector<int16_t> waveformData;
    uint64_t waveformFrames = 0;

    // Voice engine helpers (Phase 31)
    static uint16_t midiNoteToPitch(int note, int baseNote = 60);
    uint32_t posToBlockAddr(double pos) const;
    int allocateVoice(int note);
    int findVoiceForNote(int note);

    // Phase 22: bidirectional libsamplerate SRC sandwich around the
    // SPU-94 core. Allocated in prepareToPlay (via srcChain_.prepare),
    // released in releaseResources. Runs the impulse-measured group
    // delay so setLatencySamples reports the correct PDC number.
    SrcChain srcChain_;
    double   hostSampleRate_ { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPU94AudioProcessor)
};
