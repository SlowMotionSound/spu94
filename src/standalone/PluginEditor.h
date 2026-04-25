#pragma once

#include "PluginProcessor.h"
#include <memory>

class SPU94AudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SPU94AudioProcessorEditor(SPU94AudioProcessor& processor);
    ~SPU94AudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    SPU94AudioProcessor& processorRef;

    juce::TextButton loadButton{"Load WAV"};
    juce::TextButton playButton{"Play"};
    juce::TextButton stopButton{"Stop"};

    // FileChooser must outlive the async callback (JUCE requirement).
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPU94AudioProcessorEditor)
};
