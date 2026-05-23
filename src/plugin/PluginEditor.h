#pragma once

#include "PluginProcessor.h"
#include "RegisterPanel.h"
#include "MorphPanel.h"
#include "SamplerWindow.h"
#include "AdsrDisplay.h"
#include <memory>

class SPU94AudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit SPU94AudioProcessorEditor(SPU94AudioProcessor& processor);
    ~SPU94AudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    SPU94AudioProcessor& processorRef;

    juce::TextButton loadButton{"Load WAV"};
    juce::TextButton playButton{"Play"};
    juce::TextButton stopButton{"Stop"};

    // Preset Save/Load buttons (D-06 through D-08)
    juce::Label presetLabel;
    juce::TextButton savePresetButton{"Save"};
    juce::TextButton loadPresetButton{"Load"};

    // Name prompt helper
    void showPresetNamePrompt();

    // Track file-preset load completion for GUI sync
    int lastFilePresetCount = 0;
    // Track audio-thread shadow syncs so the timer can refresh slider
    // positions after view switches and per-slot loads.
    int lastShadowSyncCount = 0;

    // Sync all mixer/DAC knobs from processor atomics after file load
    void syncMixerKnobsFromProcessor();

    RegisterPanel registerPanel;
    juce::Viewport registerViewport;
    MorphPanel morphPanel;
    // SAVE / REVERT — visible only when in Advanced view editing a user slot.
    // Symmetric pair: SAVE captures current registers into the slot, REVERT
    // discards. Both close Advanced view.
    juce::TextButton saveButton{"SAVE"};
    juce::TextButton revertButton{"REVERT"};

    void enterAdvancedView(int slot);
    void exitAdvancedView(bool save);

    // Per-tick EXPORT / LOAD callbacks fired by MorphPanel buttons.
    void exportSingleSlot(int slot);
    void loadSingleSlot(int slot);


    juce::Slider inputLevelKnob;
    juce::Label inputLevelLabel;

    // Zone 1: Toolbar -- Reverb Sends section
    juce::Slider adpcmSendKnob;
    juce::Label adpcmSendLabel;
    juce::Slider drySendKnob;
    juce::Label drySendLabel;

    // Zone 3: Mixer strip
    juce::Slider dryKnob;
    juce::Label dryKnobLabel;
    juce::Slider adpcmKnob;
    juce::Label adpcmKnobLabel;
    juce::Slider reverbKnob;
    juce::Label reverbKnobLabel;
    juce::ToggleButton latencyCompToggle{"Latency Comp"};

    // ADPCM voice path controls
    juce::ToggleButton gaussToggle{"Gauss"};
    juce::ToggleButton aaFilterToggle{"Anti-Aliasing"};
    juce::Slider voicePitchKnob;
    juce::Label voicePitchLabel;

    // Voice engine panel (standalone-only, Phase 31)
    juce::TextButton loadSampleButton{"Load Sample"};
    juce::TextButton triggerVoiceButton{"Trigger"};
    juce::TextButton stopVoiceButton{"Stop Voice"};
    juce::Label voiceSampleLabel;
    juce::ComboBox encodeRateBox;
    juce::Label encodeRateLabel;
    juce::Label ramMeterLabel;
    juce::Slider voiceEnginePitchKnob;
    juce::Label voiceEnginePitchLabel;
    juce::ToggleButton loopToggle{"Loop"};
    juce::ToggleButton samplerAAToggle{"Gauss"};
    juce::ToggleButton latchToggle{"Latch"};
    bool latchActive = false;
    juce::ToggleButton markerLockToggle{"Lock"};
    double lockedLoopOffset = 0.0;
    double lockedEndOffset = 0.0;
    double prevStartVal = 0.0, prevLoopVal = 0.0, prevEndVal = 1.0;

    // Marker position knobs (standalone-only)
    juce::Slider startPosKnob;
    juce::Label  startPosLabel;
    juce::Slider loopPosKnob;
    juce::Label  loopPosLabel;
    juce::Slider endPosKnob;
    juce::Label  endPosLabel;

    // ADSR controls (standalone-only)
    AdsrDisplay adsrDisplay;
    juce::Slider adsrAttackKnob;      juce::Label adsrAttackLabel;
    juce::Slider adsrDecayKnob;       juce::Label adsrDecayLabel;
    juce::Slider adsrSustainLvlKnob;  juce::Label adsrSustainLvlLabel;
    juce::Slider adsrSustainRateKnob; juce::Label adsrSustainRateLabel;
    juce::Slider adsrReleaseKnob;     juce::Label adsrReleaseLabel;
    juce::ToggleButton adsrAttackExpToggle{"Exp"};
    juce::ToggleButton adsrSustainExpToggle{"Exp"};
    juce::ToggleButton adsrReleaseExpToggle{"Exp"};
    void refreshAdsrDisplay();

    // Voice pan/level controls (Phase 39: replaces raw Vol L/R)
    juce::Slider voicePanKnob;
    juce::Label voicePanLabel;
    juce::Slider voiceLevelKnob;
    juce::Label voiceLevelLabel;
    juce::ToggleButton voiceInvToggle{"INV"};
    juce::Label voiceInvIndicator;
    // NON/PMON toggles (Phase 40: voice feature toggles)
    juce::ToggleButton voiceNonToggle{"NON"};
    juce::ToggleButton voicePmonToggle{"PMON"};
    juce::Label voiceModeLabel;
    juce::Slider noiseColorKnob;
    juce::Label noiseColorLabel;
    void updateVoiceVolumes();

    // VCA ramp controls (Phase 41: volume sweep GUI surface)
    juce::Label rampSectionLabel;
    juce::TextButton rampDirButton{"Up"};
    juce::Slider rampSpeedKnob;
    juce::Label rampSpeedLabel;
    juce::TextButton rampCurveButton{"Linear"};
    juce::TextButton rampArmButton{"ARM"};

    // Sampler mixer knobs (standalone-only)
    juce::Slider samplerLevelKnob;
    juce::Label samplerLevelLabel;
    juce::Slider samplerSendKnob;
    juce::Label samplerSendLabel;
    juce::Slider samplerDriveKnob;
    juce::Label samplerDriveLabel;

    // Zone 4: DAC section
    juce::ToggleButton dacToggle{"DAC"};
    juce::ToggleButton dacFirToggle{"FIR"};
    juce::ToggleButton dacNoiseToggle{"Noise"};
    juce::ToggleButton dacOversampleToggle{"8x"};



    // XWayland window size fix — runs once on first timer tick
    bool windowSizeFixed = false;

    // Separate sampler window (standalone-only)
    std::unique_ptr<SamplerWindow> samplerWindow;
    uint64_t lastWaveformFrames = 0;

    // Modified-state tracking (D-11, D-12)
    // Baseline snapshot captured on every preset load (factory or custom)
    struct PresetSnapshot {
        std::array<int16_t, SPU94_REG__COUNT> registers{};
        float inputGain = 0.25f;
        float dry = 1.0f, adpcm = 0.0f, reverb = 1.0f;
        float adpcmSend = 0.0f, drySend = 1.0f;
        bool latencyComp = true;
        bool dac = false, dacFir = true, dacNoise = true, dacOversample = true;
    };
    PresetSnapshot baseline;
    bool modifiedState = false;

    void captureBaseline();
    bool checkModified() const;

    // FileChooser must outlive the async callback (JUCE requirement).
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPU94AudioProcessorEditor)
};
