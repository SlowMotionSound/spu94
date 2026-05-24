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
        contentPanel.setSize(400, 1250);
        setContentNonOwned(&contentPanel, true);

        centreWithSize(400, 1250);
        setVisible(true);
    }

    void positionLeftOf(juce::Component* mainWindow)
    {
        if (!mainWindow)
            return;
        if (auto* top = mainWindow->getTopLevelComponent())
        {
            auto mainBounds = top->getScreenBounds();
            int x = mainBounds.getX() - getWidth() - 10;
            int y = mainBounds.getY();
            if (x < 0) x = 0;
            setTopLeftPosition(x, y);
        }
    }

    void ensureMinimumSize()
    {
        if (getWidth() < 400 || getHeight() < 1240)
        {
            centreWithSize(401, 1251);
            centreWithSize(400, 1250);
        }
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
