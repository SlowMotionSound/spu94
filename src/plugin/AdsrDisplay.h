#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <spu94/spu94_adsr.h>
#include <cmath>
#include <algorithm>
#include <vector>

class AdsrDisplay : public juce::Component {
public:
    void update(uint8_t aShift, uint8_t aExp,
                uint8_t dShift, uint8_t sLevel,
                uint8_t sShift, uint8_t sExp, uint8_t sDir,
                uint8_t rShift, uint8_t rExp,
                float atkSec = 0.001f, float decSec = 0.001f, float relSec = 0.1f)
    {
        if (sLevel == 0)
            sustainTarget = 0;
        else
            sustainTarget = static_cast<int16_t>(
                std::min(((int32_t)sLevel + 1) * 0x800, (int32_t)0x7FFF));

        constexpr int kTotal = 380;
        constexpr double kSusW = 0.15;

        double wA = std::max(0.01, (double)atkSec);
        double wD = std::max(0.01, (double)decSec);
        double wS = kSusW;
        double wR = std::max(0.01, (double)relSec);
        double wT = wA + wD + wS + wR;

        int nA = std::max(1, (int)(kTotal * wA / wT));
        int nD = std::max(1, (int)(kTotal * wD / wT));
        int nS = std::max(8, (int)(kTotal * wS / wT));
        int nR = std::max(1, (int)(kTotal * wR / wT));

        levels.clear();
        levels.reserve(nA + nD + nS + nR + 8);

        // Attack: 0 -> 0x7FFF (linear ramp, kink at 0x6000 when fake-exp)
        if (aExp) {
            double kink = 0x6000;
            double peak = 0x7FFF;
            double fastFrac = kink / peak;
            int nFast = std::max(1, (int)(nA * fastFrac / (fastFrac + (1.0 - fastFrac) * 4.0)));
            int nSlow = std::max(1, nA - nFast);
            for (int i = 0; i <= nFast; i++)
                levels.push_back((int16_t)(kink * i / nFast));
            for (int i = 1; i <= nSlow; i++)
                levels.push_back((int16_t)(kink + (peak - kink) * i / nSlow));
        } else {
            for (int i = 0; i <= nA; i++)
                levels.push_back((int16_t)(0x7FFF * i / nA));
        }

        // Decay: 0x7FFF -> sustain target (exponential)
        double dStart = 0x7FFF;
        double dEnd = std::max((double)sustainTarget, 1.0);
        for (int i = 1; i <= nD; i++) {
            double frac = (double)i / nD;
            double lv = std::pow(dStart, 1.0 - frac) * std::pow(dEnd, frac);
            levels.push_back((int16_t)std::clamp((int)lv, (int)sustainTarget, 0x7FFF));
        }

        // Sustain hold: flat or drift
        double sStart = (double)sustainTarget;
        double sEnd = sStart;
        if (sShift < 31) {
            int sStep = 7 << std::max(0, 11 - (int)sShift);
            double drift = sStep * (double)nS * 1.5;
            if (sDir == 0) {
                sEnd = std::min(sStart + drift, 32767.0);
            } else {
                if (sExp) {
                    double f = (double)sStep / 0x8000;
                    sEnd = sStart * std::pow(std::max(1.0 - f, 0.01), nS * 1.5);
                } else {
                    sEnd = std::max(sStart - drift, 0.0);
                }
            }
        }
        for (int i = 1; i <= nS; i++) {
            double frac = (double)i / nS;
            levels.push_back((int16_t)std::clamp(
                (int)(sStart + frac * (sEnd - sStart)), 0, 0x7FFF));
        }

        // Release: sustain -> 0
        double rStart = std::max((double)levels.back(), 1.0);
        for (int i = 1; i <= nR; i++) {
            double frac = (double)i / nR;
            double lv;
            if (rExp) {
                lv = std::pow(rStart, 1.0 - frac);
            } else {
                lv = rStart * (1.0 - frac);
            }
            levels.push_back((int16_t)std::max((int)lv, 0));
        }

        if (levels.back() != 0) levels.push_back(0);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF1A1A1A));
        auto area = getLocalBounds();

        if (levels.size() < 2) {
            g.setColour(juce::Colours::grey);
            g.drawRect(area);
            return;
        }

        constexpr float kPad = 4.0f;
        auto inner = area.reduced(0, static_cast<int>(kPad));

        juce::Path path;
        float xScale = static_cast<float>(inner.getWidth()) /
                        static_cast<float>(levels.size() - 1);
        float yScale = static_cast<float>(inner.getHeight()) / 32767.0f;

        for (size_t i = 0; i < levels.size(); i++) {
            float x = static_cast<float>(inner.getX()) +
                      static_cast<float>(i) * xScale;
            float y = static_cast<float>(inner.getBottom()) -
                      static_cast<float>(levels[i]) * yScale;
            if (i == 0) path.startNewSubPath(x, y);
            else path.lineTo(x, y);
        }

        // Sustain level reference line
        float refY = static_cast<float>(inner.getBottom()) -
                     static_cast<float>(sustainTarget) * yScale;
        g.setColour(juce::Colour(0x60D49EBF));
        float dashX = static_cast<float>(inner.getX());
        float dashEnd = static_cast<float>(inner.getRight());
        while (dashX < dashEnd) {
            float segEnd = std::min(dashX + 4.0f, dashEnd);
            g.drawLine(dashX, refY, segEnd, refY, 1.0f);
            dashX += 7.0f;
        }

        g.setColour(juce::Colour(0xFF5B9279));
        g.strokePath(path, juce::PathStrokeType(1.5f));
        g.setColour(juce::Colours::grey);
        g.drawRect(area);
    }

private:
    std::vector<int16_t> levels;
    int16_t sustainTarget = 0;
};
