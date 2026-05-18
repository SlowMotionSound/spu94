#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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

        contentPanel.setSize(300, 160);
        setContentNonOwned(&contentPanel, true);

        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

    juce::Component& getPanel() { return contentPanel; }

private:
    juce::Component contentPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerWindow)
};
