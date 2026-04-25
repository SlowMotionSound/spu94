#include "RegisterPanel.h"

RegisterPanel::RegisterPanel(RegisterBridge& b)
    : bridge(b)
{
    // Build sliders dynamically by iterating kSliderRegisters.
    // Labels use the raw register name from spu94_reg_name (D-01).
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

    // Group headers -- bold text for visual organization.
    auto boldFont = juce::FontOptions(14.0f, juce::Font::bold);
    for (auto* header : {&headerMasterIO, &headerIIRWall, &headerComb,
                         &headerAPF, &headerDelay})
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

void RegisterPanel::resized()
{
    // Layout: vertical stack of group-header rows and slider rows.
    // 80px label width, remaining width for slider, 28px row height, 4px gap.
    const int labelW = 80;
    const int rowH = 28;
    const int headerH = 24;
    const int gap = 4;
    const int margin = 4;

    auto area = getLocalBounds().reduced(margin, 0);
    int y = 0;

    // Helper to lay out a group header + a range of slider indices.
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

    // Groups (per plan: 4 Master I/O, 2 IIR+Wall, 4 Comb, 2 APF, 6 Delay)
    // kSliderRegisters order: vLOUT(0) vROUT(1) vLIN(2) vRIN(3)
    //   vIIR(4) vWALL(5)
    //   vCOMB1(6) vCOMB2(7) vCOMB3(8) vCOMB4(9)
    //   vAPF1(10) vAPF2(11)
    //   dLSAME(12) dRSAME(13) dLDIFF(14) dRDIFF(15) dAPF1(16) dAPF2(17)
    layoutGroup(headerMasterIO, 0, 4);
    layoutGroup(headerIIRWall, 4, 2);
    layoutGroup(headerComb, 6, 4);
    layoutGroup(headerAPF, 10, 2);
    layoutGroup(headerDelay, 12, 6);
}
