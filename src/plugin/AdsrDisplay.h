#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <spu94/spu94_adsr.h>
#include <vector>

class AdsrDisplay : public juce::Component {
public:
    void update(uint8_t aShift, uint8_t aExp,
                uint8_t dShift, uint8_t sLevel,
                uint8_t sShift, uint8_t sExp, uint8_t sDir,
                uint8_t rShift, uint8_t rExp)
    {
        spu94_adsr_state_t a;
        spu94_adsr_init(&a);
        a.attack_shift  = aShift;  a.attack_step = 0; a.attack_exp = aExp;
        a.decay_shift   = dShift;
        a.sustain_level = sLevel;
        a.sustain_shift = sShift;  a.sustain_step = 0;
        a.sustain_exp   = sExp;    a.sustain_dir  = sDir;
        a.release_shift = rShift;  a.release_exp  = rExp;
        a.enabled = 1;
        spu94_adsr_key_on(&a);

        sustainTarget = static_cast<int16_t>(((int32_t)sLevel + 1) * 0x800);
        if (sustainTarget > 0x7FFF) sustainTarget = 0x7FFF;

        levels.clear();
        constexpr int kDS = 128;
        constexpr int kMaxTicks = 80000;
        constexpr int kSustainHold = 15000;
        constexpr int kForceKeyOff = 40000;
        int sustainCount = 0;
        bool hitSustain = false, released = false;

        for (int i = 0; i < kMaxTicks; i++) {
            int16_t lv = spu94_adsr_tick(&a);
            if (i % kDS == 0) levels.push_back(lv);

            if (a.phase == ADSR_SUSTAIN && !hitSustain) hitSustain = true;

            if (hitSustain && !released) {
                if (++sustainCount >= kSustainHold) {
                    spu94_adsr_key_off(&a);
                    released = true;
                }
            }
            if (!hitSustain && !released && i >= kForceKeyOff) {
                spu94_adsr_key_off(&a);
                released = true;
            }
            if (a.phase == ADSR_OFF) {
                levels.push_back(0);
                break;
            }
        }
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
