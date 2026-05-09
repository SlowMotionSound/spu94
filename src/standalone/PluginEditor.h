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
    juce::TextButton savePresetButton{"Save"};
    juce::TextButton loadPresetButton{"Load"};

    // Name prompt helper
    void showPresetNamePrompt();

    // Currently loaded custom preset name (D-09)
    juce::String customPresetName;

    // Track file-preset load completion for GUI sync
    int lastFilePresetCount = 0;

    // Sync all mixer/DAC knobs from processor atomics after file load
    void syncMixerKnobsFromProcessor();

    RegisterPanel registerPanel;
    juce::Viewport registerViewport;
    MorphPanel morphPanel;
    juce::TextButton advancedToggle{"Advanced"};
    juce::ComboBox presetSelector;
    juce::Label presetLabel{"", "Preset:"};

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


    // Track preset-switch completion for slider sync.
    int lastAppliedCount = 0;

    // Custom preset dropdown entry (D-09, D-10)
    static constexpr int kCustomPresetId = SPU94_PRESET__COUNT + 1;  // 11

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
    void updatePresetDisplayName();

    // FileChooser must outlive the async callback (JUCE requirement).
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPU94AudioProcessorEditor)
};
