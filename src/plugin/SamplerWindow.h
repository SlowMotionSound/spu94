#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "WaveformDisplay.h"

class SamplerWindow : public juce::DocumentWindow
{
public:
    SamplerWindow()
        : DocumentWindow("PSX Sampler",
                          juce::Colours::darkgrey,
                          DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);

        contentPanel.addAndMakeVisible(waveformDisplay);
        contentPanel.setSize(400, 320);
        setContentNonOwned(&contentPanel, true);

        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

    juce::Component& getPanel() { return contentPanel; }
    WaveformDisplay& getWaveformDisplay() { return waveformDisplay; }

private:
    WaveformDisplay waveformDisplay;
    juce::Component contentPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerWindow)
};
