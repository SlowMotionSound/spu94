#pragma once

#include <JuceHeader.h>
#include "ParameterBridge.h"
#include <array>

// All-register panel component built from spu94_reg_name iteration.
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

    // Returns the preferred height for laying out all sliders.
    int getPreferredHeight() const;

private:
    RegisterBridge& bridge;

    std::array<juce::Slider, SPU94_REG__COUNT> sliders;
    std::array<juce::Label, SPU94_REG__COUNT> labels;

    // Group header labels for visual organization.
    juce::Label headerMasterIO{"", "Master I/O"};
    juce::Label headerIIRWall{"", "IIR + Wall"};
    juce::Label headerComb{"", "Comb"};
    juce::Label headerAPF{"", "All-Pass"};
    juce::Label headerDelay{"", "Delay Offsets"};
    juce::Label headerBase{"", "Buffer Base"};
    juce::Label headerSameGeom{"", "Same-Side Geometry"};
    juce::Label headerDiffGeom{"", "Cross-Side Geometry"};
    juce::Label headerAPFAddr{"", "APF Addresses"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RegisterPanel)
};
