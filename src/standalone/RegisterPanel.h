#pragma once

#include <JuceHeader.h>
#include "ParameterBridge.h"
#include <array>

// 18-slider panel component built from spu94_reg_name iteration.
// Each slider is labeled with the raw register name (D-01: raw names,
// NOT musical aliases) and grouped by register class.
// Uses JUCE stock LookAndFeel_V4 throughout (STANDALONE-07, D-04).
class RegisterPanel : public juce::Component
{
public:
    explicit RegisterPanel(RegisterBridge& bridge);
    ~RegisterPanel() override = default;

    void resized() override;

    // After a preset switch, pull current shadow values to update
    // slider positions so they reflect the new preset's register values.
    void updateFromShadows();

private:
    RegisterBridge& bridge;

    std::array<juce::Slider, 18> sliders;
    std::array<juce::Label, 18> labels;

    // Group header labels for visual organization.
    juce::Label headerMasterIO{"", "Master I/O"};
    juce::Label headerIIRWall{"", "IIR + Wall"};
    juce::Label headerComb{"", "Comb"};
    juce::Label headerAPF{"", "All-Pass"};
    juce::Label headerDelay{"", "Delay Offsets"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RegisterPanel)
};
