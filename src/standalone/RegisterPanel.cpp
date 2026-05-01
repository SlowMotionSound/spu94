#include "RegisterPanel.h"

RegisterPanel::RegisterPanel(RegisterBridge& b)
    : bridge(b)
{
    for (size_t i = 0; i < kSliderRegisters.size(); ++i)
    {
        const spu94_reg_t reg = kSliderRegisters[i];
        const char* name = spu94_reg_name(reg);
        const auto type = spu94_reg_type(reg);

        auto& slider = sliders[i];
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);

        if (type == SPU94_REG_TYPE_I16)
            slider.setRange(-32768.0, 32767.0, 1.0);
        else
            slider.setRange(0.0, 65535.0, 1.0);

        slider.onValueChange = [this, i] {
            bridge.setRegisterShadow(i,
                static_cast<int16_t>(sliders[i].getValue()));
        };

        labels[i].setText(juce::String(name), juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centredRight);

        addAndMakeVisible(slider);
        addAndMakeVisible(labels[i]);
    }

    auto boldFont = juce::FontOptions(14.0f, juce::Font::bold);
    for (auto* header : {&headerMasterIO, &headerIIRWall, &headerComb,
                         &headerAPF, &headerDelay, &headerBase,
                         &headerSameGeom, &headerDiffGeom, &headerAPFAddr})
    {
        header->setFont(boldFont);
        header->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(header);
    }
}

void RegisterPanel::updateFromShadows()
{
    for (size_t i = 0; i < kSliderRegisters.size(); ++i)
    {
        sliders[i].setValue(bridge.getShadowValue(i),
                            juce::dontSendNotification);
    }
}

int RegisterPanel::getPreferredHeight() const
{
    const int rowH = 24;
    const int headerH = 22;
    const int gap = 2;
    const int numHeaders = 9;
    const int numSliders = static_cast<int>(kSliderRegisters.size());
    return numSliders * (rowH + gap) + numHeaders * (headerH + gap) + 8;
}

void RegisterPanel::resized()
{
    const int labelW = 80;
    const int rowH = 24;
    const int headerH = 22;
    const int gap = 2;
    const int margin = 4;

    auto area = getLocalBounds().reduced(margin, 0);
    int y = 0;

    auto layoutGroup = [&](juce::Label& header, size_t startIdx, size_t count) {
        header.setBounds(area.getX(), y, area.getWidth(), headerH);
        y += headerH + gap;

        for (size_t i = startIdx; i < startIdx + count; ++i)
        {
            labels[i].setBounds(area.getX(), y, labelW, rowH);
            sliders[i].setBounds(area.getX() + labelW + gap,
                                  y,
                                  area.getWidth() - labelW - gap,
                                  rowH);
            y += rowH + gap;
        }
    };

    // Groups match kSliderRegisters order:
    // 0-3: Master I/O (vLOUT, vROUT, vLIN, vRIN)
    // 4-5: IIR + Wall (vIIR, vWALL)
    // 6-9: Comb (vCOMB1-4)
    // 10-11: All-Pass (vAPF1, vAPF2)
    // 12-17: Delay Offsets (dAPF1, dAPF2, dLSAME, dRSAME, dLDIFF, dRDIFF)
    // 18: Buffer Base (mBASE)
    // 19-24: Same-Side Geometry (mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2)
    // 25-30: Cross-Side Geometry (mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4)
    // 31-34: APF Addresses (mLAPF1, mRAPF1, mLAPF2, mRAPF2)
    layoutGroup(headerMasterIO,  0, 4);
    layoutGroup(headerIIRWall,   4, 2);
    layoutGroup(headerComb,      6, 4);
    layoutGroup(headerAPF,      10, 2);
    layoutGroup(headerDelay,    12, 6);
    layoutGroup(headerBase,     18, 1);
    layoutGroup(headerSameGeom, 19, 6);
    layoutGroup(headerDiffGeom, 25, 6);
    layoutGroup(headerAPFAddr,  31, 4);
}
