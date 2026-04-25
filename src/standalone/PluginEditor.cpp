#include "PluginEditor.h"

SPU94AudioProcessorEditor::SPU94AudioProcessorEditor(SPU94AudioProcessor& p)
    : AudioProcessorEditor(p),
      processorRef(p)
{
    setSize(800, 600);
}

void SPU94AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    g.drawFittedText("SPU-94", getLocalBounds(), juce::Justification::centred, 1);
}

void SPU94AudioProcessorEditor::resized()
{
    // Plan 03 fills this with register slider + preset dropdown layout.
}
