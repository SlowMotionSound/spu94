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

    // PluginEditor sets this to be notified when the user clicks EDIT while
    // the knob is parked on a user-slot detent. Slot index is 0..7.
    std::function<void(int slot)> onEditSlotClicked;

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

    // Morph Grit: binary radio strip [Int][Fract.].
    // Coral PS1 palette differentiates from Morph Speed's mauve. The
    // selected button lights coral; the other sits dark gray.
    juce::TextButton gritIntButton;
    juce::TextButton gritFractButton;
    juce::Label gritLabel;
    void setMorphGrit(int grit); // syncs button state + processor atomic

    // EDIT button — opens Advanced view for the user slot the knob is parked
    // on. Disabled when the knob isn't on a user-slot detent.
    juce::TextButton editButton{"EDIT"};

    bool isUpdatingFromTimer = false;

    void updateLabelText(double value);
    // Returns the user-slot index (0..7) if the knob is parked on a user
    // detent within the snap threshold, or -1 otherwise. Drives editButton
    // enabled state.
    int  currentUserSlotAtKnob() const;
    void updateEditButtonState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphPanel)
};
