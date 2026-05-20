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
        sRate = sampleRate;

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
        viewStart = 0.0;
        viewEnd = 1.0;

        repaint();
    }

    void clear()
    {
        totalFrames = 0;
        thumbnail.reset(0, 44100.0);
        repaint();
    }

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
    double getViewSpan() const { return viewEnd - viewStart; }
    std::function<void(double)> onZoomChange;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF1A1A1A));

        auto area = getLocalBounds();

        if (thumbnail.getTotalLength() > 0.0)
        {
            double totalLen = thumbnail.getTotalLength();
            double drawStart = viewStart * totalLen;
            double drawEnd   = viewEnd   * totalLen;

            g.setColour(juce::Colour(0xFF5B9279));
            thumbnail.drawChannel(g, area, drawStart, drawEnd, 0, 1.0f);

            drawMarker(g, area, startPos, juce::Colour(0xFFE06060), "S");
            drawMarker(g, area, endPos, juce::Colour(0xFFE06060), "E");
            if (loopMode)
                drawMarker(g, area, loopPos, juce::Colour(0xFFE06060), "L");

            int headX = posToPixel(playheadPos);
            if (headX >= 0 && headX < area.getWidth())
            {
                g.setColour(juce::Colours::white);
                g.drawVerticalLine(headX, static_cast<float>(area.getY()),
                                   static_cast<float>(area.getBottom()));
            }

            if (viewEnd - viewStart < 0.999)
            {
                g.setColour(juce::Colour(0x60FFFFFF));
                g.setFont(9.0f);
                double zoomLevel = 1.0 / (viewEnd - viewStart);
                g.drawText(juce::String(zoomLevel, 1) + "x",
                           area.getRight() - 40, area.getY() + 2, 38, 12,
                           juce::Justification::centredRight);
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
        if (dragTarget == DragTarget::none)
        {
            panning = true;
            panStartX = e.x;
            panStartView = viewStart;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (totalFrames == 0) return;

        if (panning)
        {
            double viewSpan = viewEnd - viewStart;
            double dx = static_cast<double>(panStartX - e.x) / static_cast<double>(getWidth());
            double newStart = panStartView + dx * viewSpan;
            newStart = juce::jlimit(0.0, 1.0 - viewSpan, newStart);
            viewStart = newStart;
            viewEnd = newStart + viewSpan;
            repaint();
            return;
        }

        if (dragTarget == DragTarget::none) return;

        double pos = pixelToPos(e.x);
        pos = juce::jlimit(0.0, 1.0, pos);

        switch (dragTarget)
        {
            case DragTarget::start:
            {
                double minGap = (totalFrames > 0) ? 56.0 / static_cast<double>(totalFrames) : 0.001;
                startPos = pos;
                if (startPos > endPos - minGap) {
                    endPos = startPos + minGap;
                    if (endPos > 1.0) { endPos = 1.0; startPos = endPos - minGap; }
                }
                loopPos = juce::jlimit(startPos, endPos, loopPos);
                break;
            }
            case DragTarget::end:
            {
                double minGap = (totalFrames > 0) ? 56.0 / static_cast<double>(totalFrames) : 0.001;
                endPos = pos;
                if (endPos < startPos + minGap) {
                    startPos = endPos - minGap;
                    if (startPos < 0.0) { startPos = 0.0; endPos = startPos + minGap; }
                }
                loopPos = juce::jlimit(startPos, endPos, loopPos);
                break;
            }
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
        panning = false;
    }

    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override
    {
        if (totalFrames == 0) return;

        double zoomFactor = (wheel.deltaY > 0) ? 0.8 : 1.25;
        double viewSpan = viewEnd - viewStart;
        double newSpan = juce::jlimit(0.001, 1.0, viewSpan * zoomFactor);

        double mousePos = pixelToPos(e.x);
        double mouseRatio = static_cast<double>(e.x) / static_cast<double>(getWidth());

        double newStart = mousePos - mouseRatio * newSpan;
        if (newStart < 0.0) newStart = 0.0;
        if (newStart + newSpan > 1.0) newStart = 1.0 - newSpan;

        viewStart = newStart;
        viewEnd = newStart + newSpan;
        repaint();
        if (onZoomChange) onZoomChange(viewEnd - viewStart);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        viewStart = 0.0;
        viewEnd = 1.0;
        repaint();
        if (onZoomChange) onZoomChange(1.0);
    }

private:
    enum class DragTarget { none, start, end, loop };

    int posToPixel(double pos) const
    {
        double viewSpan = viewEnd - viewStart;
        if (viewSpan <= 0.0) return 0;
        return static_cast<int>((pos - viewStart) / viewSpan * getWidth());
    }

    double pixelToPos(int x) const
    {
        double viewSpan = viewEnd - viewStart;
        return viewStart + (static_cast<double>(x) / static_cast<double>(getWidth())) * viewSpan;
    }

    void drawMarker(juce::Graphics& g, juce::Rectangle<int> area,
                    double pos, juce::Colour colour, const char* label)
    {
        int x = posToPixel(pos);
        if (x < -1 || x > area.getWidth() + 1) return;
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

        auto dist = [&](double pos) {
            return std::abs(mouseX - posToPixel(pos));
        };

        if (loopMode && dist(loopPos) <= grabRadius) return DragTarget::loop;
        int dStart = dist(startPos);
        int dEnd = dist(endPos);
        if (dStart <= grabRadius && dEnd <= grabRadius)
            return (dStart <= dEnd) ? DragTarget::start : DragTarget::end;
        if (dStart <= grabRadius) return DragTarget::start;
        if (dEnd <= grabRadius) return DragTarget::end;
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
    double sRate = 44100.0;
    double startPos = 0.0;
    double endPos = 1.0;
    double loopPos = 0.0;
    double playheadPos = 0.0;
    bool loopMode = false;
    DragTarget dragTarget = DragTarget::none;

    double viewStart = 0.0;
    double viewEnd = 1.0;
    bool panning = false;
    int panStartX = 0;
    double panStartView = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
