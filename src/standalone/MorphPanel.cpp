#include "MorphPanel.h"
#include "PluginProcessor.h"
#include <cmath>

// PS1 color palette (same values as archived MacroPanel)
static const auto psxDarkGray  = juce::Colour(0xFF5A5A5A);
static const auto psxLightGray = juce::Colour(0xFFB0B0B0);
static const auto psxTeal      = juce::Colour(0xFF6FD8CE);
static const auto psxMauve     = juce::Colour(0xFFD49EBF);
static const auto psxCoral     = juce::Colour(0xFFE8736E);
static const auto psxBlue      = juce::Colour(0xFF7079CC);

// Waypoint names in perceptual order (Phase 16 confirmed)
static const char* kWaypointNames[9] = {
    "Half Echo", "Room", "Studio A", "Studio B", "Studio C",
    "Hall", "Space Echo", "Echo", "Delay"
};

//==============================================================================
MorphPanel::MorphPanel(SPU94AudioProcessor& processor)
    : processorRef(processor)
{
    // Configure morph knob: single large rotary, 0.0-1.0 range
    morphKnob.setSliderStyle(juce::Slider::Rotary);
    morphKnob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    morphKnob.setRange(0.0, 1.0, 0.0001);
    morphKnob.setValue(0.625, juce::dontSendNotification);
    morphKnob.setDoubleClickReturnValue(true, 0.625);
    morphKnob.setColour(juce::Slider::rotarySliderOutlineColourId, psxLightGray);
    morphKnob.setColour(juce::Slider::rotarySliderFillColourId, psxTeal.withAlpha(0.6f));
    morphKnob.setColour(juce::Slider::thumbColourId, psxTeal);
    morphKnob.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                   juce::MathConstants<float>::pi * 2.8f,
                                   true);
    addAndMakeVisible(morphKnob);

    morphKnob.onValueChange = [this]()
    {
        if (isUpdatingFromTimer) return;
        processorRef.getMorphPosition().store(
            static_cast<float>(morphKnob.getValue()),
            std::memory_order_relaxed);
        updateLabelText(morphKnob.getValue());
    };

    // Configure dynamic label below the knob
    morphLabel.setJustificationType(juce::Justification::centred);
    morphLabel.setColour(juce::Label::textColourId, psxLightGray);
    morphLabel.setFont(juce::FontOptions(16.0f));
    addAndMakeVisible(morphLabel);

    // Morph Speed knob — small rotary tucked at the lower-right of the
    // main morph knob. Mauve PS1 palette to differentiate from the teal
    // main knob. 0 = Instant Snap (registers latch at next tick),
    // 1 = Glide (full slew duration).
    speedKnob.setSliderStyle(juce::Slider::Rotary);
    speedKnob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    speedKnob.setRange(0.0, 1.0, 0.01);
    speedKnob.setValue(1.0, juce::dontSendNotification);
    speedKnob.setColour(juce::Slider::rotarySliderOutlineColourId, psxDarkGray);
    speedKnob.setColour(juce::Slider::rotarySliderFillColourId, psxMauve.withAlpha(0.5f));
    speedKnob.setColour(juce::Slider::thumbColourId, psxMauve);
    speedKnob.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                   juce::MathConstants<float>::pi * 2.8f,
                                   true);
    speedKnob.setTooltip("Morph Speed: 0 = Instant Snap, 1 = Glide");
    addAndMakeVisible(speedKnob);

    speedKnob.onValueChange = [this]()
    {
        processorRef.getMorphSpeed().store(
            static_cast<float>(speedKnob.getValue()),
            std::memory_order_relaxed);
    };

    speedLabel.setText("Morph Speed", juce::dontSendNotification);
    speedLabel.setJustificationType(juce::Justification::centred);
    speedLabel.setColour(juce::Label::textColourId, psxLightGray.withAlpha(0.7f));
    speedLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(speedLabel);

    // Set initial label to "Hall" (matching default morph position 0.625)
    updateLabelText(0.625);
}

//==============================================================================
double MorphPanel::MorphSlider::snapValue(double attemptedValue, DragMode)
{
    constexpr int numWaypoints = 9;  // SPU94_INTERP_WAYPOINT_COUNT
    constexpr double snapThreshold = 0.01;
    for (int i = 0; i < numWaypoints; ++i)
    {
        double wp = static_cast<double>(i) / 8.0;
        if (std::abs(attemptedValue - wp) < snapThreshold)
            return wp;
    }
    return attemptedValue;
}

//==============================================================================
void MorphPanel::updateLabelText(double value)
{
    constexpr int numWaypoints = 9;
    constexpr double snapThreshold = 0.01;
    for (int i = 0; i < numWaypoints; ++i)
    {
        double wp = static_cast<double>(i) / 8.0;
        if (std::abs(value - wp) < snapThreshold)
        {
            morphLabel.setText(juce::String(kWaypointNames[i]),
                               juce::dontSendNotification);
            return;
        }
    }
    // Between detents: show 0.0-100.0 with one decimal place
    morphLabel.setText(juce::String(value * 100.0, 1),
                       juce::dontSendNotification);
}

//==============================================================================
void MorphPanel::updateKnobPosition()
{
    isUpdatingFromTimer = true;
    double pos = static_cast<double>(
        processorRef.getMorphPosition().load(std::memory_order_relaxed));
    morphKnob.setValue(pos, juce::dontSendNotification);
    updateLabelText(pos);
    isUpdatingFromTimer = false;
}

//==============================================================================
void MorphPanel::paint(juce::Graphics& g)
{
    // Dark background matching editor
    g.fillAll(juce::Colours::darkgrey);

    // Draw waypoint dots around the knob arc
    auto knobBounds = morphKnob.getBounds().toFloat();
    float cx = knobBounds.getCentreX();
    float cy = knobBounds.getCentreY();
    // Dot radius sits just outside the knob track
    float dotRadius = (knobBounds.getWidth() * 0.5f) + 12.0f;
    float dotSize = 6.0f;

    // Match the knob's rotary arc: 1.2*pi to 2.8*pi (270 degrees)
    float startAngle = juce::MathConstants<float>::pi * 1.2f;
    float endAngle   = juce::MathConstants<float>::pi * 2.8f;

    // Cycle through PS1 palette colors for the dots
    const juce::Colour dotColors[4] = { psxTeal, psxMauve, psxCoral, psxBlue };

    constexpr int numWaypoints = 9;
    for (int i = 0; i < numWaypoints; ++i)
    {
        float t = static_cast<float>(i) / 8.0f;
        float angle = startAngle + t * (endAngle - startAngle);
        // JUCE rotary angles: clockwise from 12 o'clock (top center).
        // std::cos/sin: counter-clockwise from 3 o'clock.
        // Subtract pi/2 to convert: angle 0 points up (12 o'clock).
        float dx = cx + dotRadius * std::cos(angle - juce::MathConstants<float>::halfPi);
        float dy = cy + dotRadius * std::sin(angle - juce::MathConstants<float>::halfPi);

        g.setColour(dotColors[i % 4]);
        g.fillEllipse(dx - dotSize * 0.5f, dy - dotSize * 0.5f, dotSize, dotSize);
    }
}

//==============================================================================
void MorphPanel::resized()
{
    auto area = getLocalBounds();
    int knobSize = 280;
    int labelHeight = 24;
    int totalHeight = knobSize + labelHeight + 8;

    int startY = (area.getHeight() - totalHeight) / 2;
    int startX = (area.getWidth() - knobSize) / 2;

    morphKnob.setBounds(startX, startY, knobSize, knobSize);
    morphLabel.setBounds(startX, startY + knobSize + 8, knobSize, labelHeight);

    // Speed knob: anchored to the bottom of the viewport (just above the
    // mixer/DAC bar), centered horizontally, with its label below it.
    // Visually separated from the main morph knob — its own row.
    constexpr int speedSize = 60;
    constexpr int speedLabelH = 14;
    constexpr int speedBottomMargin = 4;
    int speedX = (area.getWidth() - speedSize) / 2;
    int speedY = area.getHeight() - speedSize - speedLabelH - speedBottomMargin;
    speedKnob.setBounds(speedX, speedY, speedSize, speedSize);
    speedLabel.setBounds(speedX - 40, speedY + speedSize, speedSize + 80, speedLabelH);
}
