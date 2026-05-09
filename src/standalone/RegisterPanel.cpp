#include "RegisterPanel.h"

namespace {
// Map each register to its L/R partner (or SPU94_REG__COUNT if none).
// Used to clamp the L/R spread when a user drags a slider in Advanced mode —
// independent L/R divergence on address/delay/gain registers can produce
// pathologically asymmetric stereo that blasts one ear.
spu94_reg_t pairedRegister(spu94_reg_t r)
{
    switch (r) {
        case SPU94_REG_vLOUT:  return SPU94_REG_vROUT;
        case SPU94_REG_vROUT:  return SPU94_REG_vLOUT;
        case SPU94_REG_vLIN:   return SPU94_REG_vRIN;
        case SPU94_REG_vRIN:   return SPU94_REG_vLIN;
        case SPU94_REG_dLSAME: return SPU94_REG_dRSAME;
        case SPU94_REG_dRSAME: return SPU94_REG_dLSAME;
        case SPU94_REG_dLDIFF: return SPU94_REG_dRDIFF;
        case SPU94_REG_dRDIFF: return SPU94_REG_dLDIFF;
        case SPU94_REG_mLSAME: return SPU94_REG_mRSAME;
        case SPU94_REG_mRSAME: return SPU94_REG_mLSAME;
        case SPU94_REG_mLDIFF: return SPU94_REG_mRDIFF;
        case SPU94_REG_mRDIFF: return SPU94_REG_mLDIFF;
        case SPU94_REG_mLCOMB1: return SPU94_REG_mRCOMB1;
        case SPU94_REG_mRCOMB1: return SPU94_REG_mLCOMB1;
        case SPU94_REG_mLCOMB2: return SPU94_REG_mRCOMB2;
        case SPU94_REG_mRCOMB2: return SPU94_REG_mLCOMB2;
        case SPU94_REG_mLCOMB3: return SPU94_REG_mRCOMB3;
        case SPU94_REG_mRCOMB3: return SPU94_REG_mLCOMB3;
        case SPU94_REG_mLCOMB4: return SPU94_REG_mRCOMB4;
        case SPU94_REG_mRCOMB4: return SPU94_REG_mLCOMB4;
        case SPU94_REG_mLAPF1: return SPU94_REG_mRAPF1;
        case SPU94_REG_mRAPF1: return SPU94_REG_mLAPF1;
        case SPU94_REG_mLAPF2: return SPU94_REG_mRAPF2;
        case SPU94_REG_mRAPF2: return SPU94_REG_mLAPF2;
        default:               return SPU94_REG__COUNT;
    }
}

// Find the slider index that drives a given register, or -1 if none.
int sliderIndexForRegister(spu94_reg_t r)
{
    for (size_t j = 0; j < kSliderRegisters.size(); ++j)
        if (kSliderRegisters[j] == r) return (int)j;
    return -1;
}

// Maximum allowed |L - R| difference per register type.
// Factory presets have L/R diffs around 1000–1500 on address/delay regs,
// so 4096 leaves plenty of musical room without runaway divergence.
constexpr double kMaxLRDiffI16 = 4096.0;  // gain pairs (±25% of full scale)
constexpr double kMaxLRDiffU16 = 4096.0;  // address/delay pairs
}  // namespace

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
        // Disable wheel scroll on sliders — only click+drag changes value;
        // wheel events propagate up to the viewport for two-finger scrolling.
        slider.setScrollWheelEnabled(false);

        if (type == SPU94_REG_TYPE_I16)
            slider.setRange(-32768.0, 32767.0, 1.0);
        else
            slider.setRange(0.0, 65535.0, 1.0);

        slider.onValueChange = [this, i] {
            double v = sliders[i].getValue();

            // L/R spread clamp: if this register has a paired register,
            // limit |this - pair| to a max so the user can't drag stereo
            // divergence into ear-blasting territory.
            const spu94_reg_t reg = kSliderRegisters[i];
            const spu94_reg_t pair = pairedRegister(reg);
            if (pair != SPU94_REG__COUNT)
            {
                const int pairIdx = sliderIndexForRegister(pair);
                if (pairIdx >= 0)
                {
                    const auto type = spu94_reg_type(reg);
                    double pairVal;
                    if (type == SPU94_REG_TYPE_I16)
                        pairVal = (double)bridge.getShadowValue((size_t)pairIdx);
                    else
                        pairVal = (double)(uint16_t)bridge.getShadowValue((size_t)pairIdx);

                    const double maxDiff = (type == SPU94_REG_TYPE_I16)
                                            ? kMaxLRDiffI16 : kMaxLRDiffU16;
                    if (v > pairVal + maxDiff) v = pairVal + maxDiff;
                    if (v < pairVal - maxDiff) v = pairVal - maxDiff;

                    if (v != sliders[i].getValue())
                        sliders[i].setValue(v, juce::dontSendNotification);
                }
            }

            bridge.setRegisterShadow(i, static_cast<int16_t>(v));
            baseline[i] = v;
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

    scaleSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    scaleSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    scaleSlider.setScrollWheelEnabled(false);
    scaleSlider.setRange(0.0, 2.0, 0.01);
    scaleSlider.setValue(1.0, juce::dontSendNotification);
    scaleSlider.onValueChange = [this] { applyScale(scaleSlider.getValue()); };
    scaleLabel.setText("SCALE", juce::dontSendNotification);
    scaleLabel.setJustificationType(juce::Justification::centredRight);
    scaleLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(scaleSlider);
    addAndMakeVisible(scaleLabel);
}

void RegisterPanel::updateFromShadows()
{
    for (size_t i = 0; i < kSliderRegisters.size(); ++i)
    {
        double val = bridge.getShadowValue(i);
        baseline[i] = val;
        sliders[i].setValue(val, juce::dontSendNotification);
    }
    scaleSlider.setValue(1.0, juce::dontSendNotification);
}

void RegisterPanel::applyScale(double scale)
{
    for (size_t i = 0; i < kSliderRegisters.size(); ++i)
    {
        const auto reg = kSliderRegisters[i];

        if (reg == SPU94_REG_vLOUT || reg == SPU94_REG_vROUT ||
            reg == SPU94_REG_vLIN  || reg == SPU94_REG_vRIN)
            continue;

        const auto type = spu94_reg_type(reg);
        double scaled = baseline[i] * scale;

        if (type == SPU94_REG_TYPE_I16)
            scaled = std::clamp(scaled, -32768.0, 32767.0);
        else
            scaled = std::clamp(scaled, 0.0, 65535.0);

        sliders[i].setValue(std::round(scaled), juce::dontSendNotification);
        bridge.setRegisterShadow(i, static_cast<int16_t>(std::round(scaled)));
    }
}

int RegisterPanel::getPreferredHeight() const
{
    const int rowH = 24;
    const int headerH = 22;
    const int gap = 2;
    const int numHeaders = 9;
    const int numSliders = static_cast<int>(kSliderRegisters.size());
    return numSliders * (rowH + gap) + numHeaders * (headerH + gap) + rowH + gap + 4 + 8;
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

    y += 8;
    const int scaleH = 32;
    scaleLabel.setBounds(area.getX(), y, labelW, scaleH);
    scaleSlider.setBounds(area.getX() + labelW + gap, y,
                          area.getWidth() - labelW - gap, scaleH);
}
