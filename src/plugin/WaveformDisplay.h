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

        totalFrames = numFrames;

        juce::AudioBuffer<float> buffer(1, static_cast<int>(numFrames));
        auto* dest = buffer.getWritePointer(0);
        for (uint64_t i = 0; i < numFrames; ++i)
            dest[i] = static_cast<float>(data[i]) / 32768.0f;

        thumbnail.reset(1, sampleRate, static_cast<int>(numFrames));
        thumbnail.addBlock(0, buffer, 0, static_cast<int>(numFrames));

        startPos = 0.0;
        endPos = 1.0;
        loopPos = 0.0;
        playheadPos = 0.0;

        repaint();
    }

    void clear()
    {
        totalFrames = 0;
        thumbnail.reset(0, 44100.0);
        repaint();
    }

    // Normalized positions (0.0 = start, 1.0 = end of sample)
    double getStartPos() const { return startPos; }
    double getEndPos() const { return endPos; }
    double getLoopPos() const { return loopPos; }

    void setStartPos(double pos) { startPos = pos; repaint(); }
    void setEndPos(double pos)   { endPos = pos; repaint(); }
    void setLoopPos(double pos)  { loopPos = pos; repaint(); }

    void setLoopMode(bool enabled) { loopMode = enabled; repaint(); }
    bool getLoopMode() const { return loopMode; }

    void setPlayheadPos(double pos) { playheadPos = pos; repaint(); }

    uint64_t getTotalFrames() const { return totalFrames; }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF1A1A1A));

        auto area = getLocalBounds();

        if (thumbnail.getTotalLength() > 0.0)
        {
            g.setColour(juce::Colour(0xFF5B9279));
            thumbnail.drawChannel(g, area, 0.0, thumbnail.getTotalLength(), 0, 1.0f);

            // Start marker (coral)
            drawMarker(g, area, startPos, juce::Colour(0xFFE06060), "S");
            // End marker (coral)
            drawMarker(g, area, endPos, juce::Colour(0xFFE06060), "E");
            // Loop marker (coral, only in loop mode)
            if (loopMode)
                drawMarker(g, area, loopPos, juce::Colour(0xFFE06060), "L");
            // Playback head (white, thin)
            int headX = static_cast<int>(playheadPos * area.getWidth());
            if (headX >= 0 && headX < area.getWidth())
            {
                g.setColour(juce::Colours::white);
                g.drawVerticalLine(headX, static_cast<float>(area.getY()),
                                   static_cast<float>(area.getBottom()));
            }
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

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (totalFrames == 0) return;
        dragTarget = hitTest(e.x);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragTarget == DragTarget::none || totalFrames == 0) return;

        double pos = juce::jlimit(0.0, 1.0,
            static_cast<double>(e.x) / static_cast<double>(getWidth()));

        switch (dragTarget)
        {
            case DragTarget::start:
                startPos = std::min(pos, endPos - 0.01);
                loopPos = juce::jlimit(startPos, endPos, loopPos);
                break;
            case DragTarget::end:
                endPos = std::max(pos, startPos + 0.01);
                loopPos = juce::jlimit(startPos, endPos, loopPos);
                break;
            case DragTarget::loop:
                loopPos = juce::jlimit(startPos, endPos, pos);
                break;
            default: break;
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragTarget = DragTarget::none;
    }

private:
    enum class DragTarget { none, start, end, loop };

    void drawMarker(juce::Graphics& g, juce::Rectangle<int> area,
                    double pos, juce::Colour colour, const char* label)
    {
        int x = static_cast<int>(pos * area.getWidth());
        g.setColour(colour);
        g.drawVerticalLine(x, static_cast<float>(area.getY()),
                           static_cast<float>(area.getBottom()));
        g.drawVerticalLine(x + 1, static_cast<float>(area.getY()),
                           static_cast<float>(area.getBottom()));
        g.setFont(10.0f);
        g.drawText(label, x - 5, area.getY() + 2, 12, 12,
                   juce::Justification::centred);
    }

    DragTarget hitTest(int mouseX)
    {
        constexpr int grabRadius = 6;
        int w = getWidth();

        auto dist = [&](double pos) {
            return std::abs(mouseX - static_cast<int>(pos * w));
        };

        // Check loop first (narrowest hit zone when overlapping)
        if (loopMode && dist(loopPos) <= grabRadius) return DragTarget::loop;
        if (dist(startPos) <= grabRadius) return DragTarget::start;
        if (dist(endPos) <= grabRadius) return DragTarget::end;
        return DragTarget::none;
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        repaint();
    }

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache;
    juce::AudioThumbnail thumbnail;

    uint64_t totalFrames = 0;
    double startPos = 0.0;
    double endPos = 1.0;
    double loopPos = 0.0;
    double playheadPos = 0.0;
    bool loopMode = false;
    DragTarget dragTarget = DragTarget::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
