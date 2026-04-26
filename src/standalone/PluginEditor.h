#pragma once

#include "PluginProcessor.h"
#include "RegisterPanel.h"
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

    RegisterPanel registerPanel;
    juce::ComboBox presetSelector;
    juce::Label presetLabel{"", "Preset:"};

    juce::Slider wetDryKnob;
    juce::Label wetDryLabel;

    // Track preset-switch completion for slider sync.
    int lastAppliedCount = 0;

    // FileChooser must outlive the async callback (JUCE requirement).
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPU94AudioProcessorEditor)
};
