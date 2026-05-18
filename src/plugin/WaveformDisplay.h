#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cstdint>

class WaveformDisplay : public juce::Component,
                         private juce::ChangeListener
{
public:
    WaveformDisplay()
        : thumbnailCache(1),
          thumbnail(512, formatManager, thumbnailCache)
    {
        formatManager.registerBasicFormats();
        thumbnail.addChangeListener(this);
    }

    ~WaveformDisplay() override
    {
        thumbnail.removeChangeListener(this);
    }

    void setSample(const int16_t* data, uint64_t numFrames, double sampleRate)
    {
        if (data == nullptr || numFrames == 0) return;

        juce::AudioBuffer<float> buffer(1, static_cast<int>(numFrames));
        auto* dest = buffer.getWritePointer(0);
        for (uint64_t i = 0; i < numFrames; ++i)
            dest[i] = static_cast<float>(data[i]) / 32768.0f;

        thumbnail.reset(1, sampleRate, static_cast<int>(numFrames));
        thumbnail.addBlock(0, buffer, 0, static_cast<int>(numFrames));
        repaint();
    }

    void clear()
    {
        thumbnail.reset(0, 44100.0);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF1A1A1A));

        auto area = getLocalBounds();

        if (thumbnail.getTotalLength() > 0.0)
        {
            g.setColour(juce::Colour(0xFF5B9279));
            thumbnail.drawChannel(g, area, 0.0, thumbnail.getTotalLength(), 0, 1.0f);
        }
        else
        {
            g.setColour(juce::Colours::grey);
            g.setFont(12.0f);
            g.drawText("No sample loaded", area, juce::Justification::centred);
        }

        g.setColour(juce::Colours::grey);
        g.drawRect(area);
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        repaint();
    }

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache;
    juce::AudioThumbnail thumbnail;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
