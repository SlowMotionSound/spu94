#pragma once
#include <JuceHeader.h>

class SPU94AudioProcessor;

class MorphPanel : public juce::Component
{
public:
    explicit MorphPanel(SPU94AudioProcessor& processor);
    ~MorphPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Called from editor timer to sync knob position from processor state
    void updateKnobPosition();

private:
    SPU94AudioProcessor& processorRef;

    // Inner slider subclass with detent snap
    class MorphSlider : public juce::Slider {
    public:
        double snapValue(double attemptedValue, DragMode) override;
    };

    MorphSlider morphKnob;
    juce::Label morphLabel;

    // Morph Speed: 0 = Instant Snap, 1 = Glide. Smaller rotary at the
    // lower-right of the main knob, mauve PS1 palette.
    juce::Slider speedKnob;
    juce::Label speedLabel;

    bool isUpdatingFromTimer = false;

    void updateLabelText(double value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphPanel)
};
