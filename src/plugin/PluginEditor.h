#pragma once

#include "PluginProcessor.h"
#include "RegisterPanel.h"
#include "MorphPanel.h"
#include <memory>

class SPU94AudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit SPU94AudioProcessorEditor(SPU94AudioProcessor& processor);
    ~SPU94AudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

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
    juce::Slider patinaKnob;
    juce::Label patinaKnobLabel;
    juce::Slider reverbKnob;
    juce::Label reverbKnobLabel;
    juce::ToggleButton latencyCompToggle{"Latency Comp"};

    // Zone 4: DAC section
    juce::ToggleButton dacToggle{"DAC"};
    juce::ToggleButton dacFirToggle{"FIR"};
    juce::ToggleButton dacNoiseToggle{"Noise"};
    juce::ToggleButton dacOversampleToggle{"8x"};



    // Modified-state tracking (D-11, D-12)
    // Baseline snapshot captured on every preset load (factory or custom)
    struct PresetSnapshot {
        std::array<int16_t, SPU94_REG__COUNT> registers{};
        float inputGain = 0.25f;
        float dry = 1.0f, patina = 0.0f, reverb = 1.0f;
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
