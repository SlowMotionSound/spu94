#include "PluginEditor.h"

SPU94AudioProcessorEditor::SPU94AudioProcessorEditor(SPU94AudioProcessor& p)
    : AudioProcessorEditor(p),
      processorRef(p)
{
    // Load WAV button -- opens async file picker.
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select a WAV file",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.aif");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    processorRef.loadWavFile(result);
                }
            });
    };

    // Play button -- starts playback of loaded WAV through SPU.
    addAndMakeVisible(playButton);
    playButton.onClick = [this]()
    {
        processorRef.startPlayback();
    };

    // Stop button -- stops playback and resets position.
    addAndMakeVisible(stopButton);
    stopButton.onClick = [this]()
    {
        processorRef.stopPlayback();
    };

    setSize(800, 600);
}

void SPU94AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    g.drawFittedText("SPU-94", getLocalBounds().removeFromBottom(getHeight() - 50),
                     juce::Justification::centred, 1);
}

void SPU94AudioProcessorEditor::resized()
{
    // Buttons across the top: 120x30 each, left-aligned with 10px gaps.
    auto area = getLocalBounds().removeFromTop(40);
    area.removeFromLeft(10);

    loadButton.setBounds(area.removeFromLeft(120).reduced(0, 5));
    area.removeFromLeft(10);
    playButton.setBounds(area.removeFromLeft(120).reduced(0, 5));
    area.removeFromLeft(10);
    stopButton.setBounds(area.removeFromLeft(120).reduced(0, 5));

    // Plan 03 fills the remaining area with register slider + preset dropdown layout.
}
