#pragma once

#include "PluginProcessor.h"
#include "RegisterPanel.h"
#include "MorphPanel.h"
#include "SamplerWindow.h"
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
    juce::TextButton recordButton{"Record"};
    juce::ComboBox recordModeBox;
    juce::Label recordModeLabel;
    juce::Slider thresholdKnob;
    juce::Label thresholdLabel;
    juce::Label inputPeakMeterLabel;
    juce::Label recordStatsLabel;
    juce::TextButton loadSampleButton{"Load Sample"};
    juce::TextButton triggerVoiceButton{"Trigger"};
    juce::TextButton stopVoiceButton{"Stop Voice"};
    juce::TextButton exportSampleButton{"Export"};
    juce::Label voiceSampleLabel;
    juce::ComboBox encodeRateBox;
    juce::Label encodeRateLabel;
    juce::Slider encodeRateKnob;
    juce::Label ramMeterLabel;
    juce::Slider voiceEnginePitchKnob;
    juce::Label voiceEnginePitchLabel;
    juce::ToggleButton loopToggle{"Loop"};
    juce::ToggleButton samplerAAToggle{"Gauss"};
    juce::ToggleButton latchToggle{"Latch"};
    bool latchActive = false;
    juce::ToggleButton oneShotToggle{"1-Shot"};
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
    juce::Slider adsrAttackKnob;      juce::Label adsrAttackLabel;
    juce::Slider adsrDecayKnob;       juce::Label adsrDecayLabel;
    juce::Slider adsrSustainLvlKnob;  juce::Label adsrSustainLvlLabel;
    juce::Slider adsrSustainRateKnob; juce::Label adsrSustainRateLabel;
    juce::Slider adsrReleaseKnob;     juce::Label adsrReleaseLabel;
    juce::ToggleButton adsrAttackExpToggle{"Exp"};
    juce::ToggleButton adsrSustainExpToggle{"Exp"};
    juce::ToggleButton adsrReleaseExpToggle{"Exp"};

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
    // Voice Count selector (Phase 62, VCOUNT-01/03): 1-24 active voices, default 24.
    // Governs the per-voice controls above (D-02). onChange drives
    // processorRef.setActiveVoiceCount (Phase 60 setter).
    juce::ComboBox voiceCountBox;
    juce::Label voiceCountLabel;
    void updateVoiceVolumes();

    // Unified VCA Effects section (Phase 54: dropdown + adaptive controls)
    juce::Label fxSectionLabel;
    juce::ComboBox effectModeBox;
    juce::Slider fxRateKnob;        juce::Label fxRateLabel;
    juce::Slider fxDepthKnob;       juce::Label fxDepthLabel;
    juce::ComboBox fxShapeBox;      juce::Label fxShapeLabel;
    juce::TextButton fxCurveButton{"Linear"};
    juce::Slider fxRatioKnob;       juce::Label fxRatioLabel;
    // Ducking-specific controls
    juce::ComboBox fxDuckSourceBox;  juce::Label fxDuckSourceLabel;
    juce::Slider fxDuckAttackKnob;   juce::Label fxDuckAttackLabel;
    juce::Slider fxDuckReleaseKnob;  juce::Label fxDuckReleaseLabel;
    juce::Slider fxDuckDepthKnob;    juce::Label fxDuckDepthLabel;

    void updateEffectControlVisibility();

    // Internal Mod Bus (Phase 50: noise-to-pitch/vol/pan modulation)
    juce::Label modBusSectionLabel;
    juce::Slider modBusPitchKnob;
    juce::Label modBusPitchLabel;
    juce::Slider modBusVolKnob;
    juce::Label modBusVolLabel;
    juce::Slider modBusPanKnob;
    juce::Label modBusPanLabel;

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

    uint16_t computePitchFromCents() const;

    struct PitchKeyListener : public juce::KeyListener {
        juce::Slider& knob;
        PitchKeyListener(juce::Slider& k) : knob(k) {}
        bool keyPressed(const juce::KeyPress& key, juce::Component*) override {
            if (!key.getModifiers().isShiftDown()) return false;
            double v = knob.getValue();
            if (key.getKeyCode() == juce::KeyPress::upKey) {
                double next = (std::floor(v / 100.0) + 1.0) * 100.0;
                if (next > knob.getMaximum()) next = knob.getMaximum();
                knob.setValue(next, juce::sendNotification);
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::downKey) {
                double prev = (std::ceil(v / 100.0) - 1.0) * 100.0;
                if (prev < knob.getMinimum()) prev = knob.getMinimum();
                knob.setValue(prev, juce::sendNotification);
                return true;
            }
            return false;
        }
    };
    std::unique_ptr<PitchKeyListener> pitchKeyListener;

    // FileChooser must outlive the async callback (JUCE requirement).
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPU94AudioProcessorEditor)
};
