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

// Custom LookAndFeel that draws the Morph Grit toggle in a focus-stable way.
// JUCE's LookAndFeel_V4 desaturates buttons that don't have keyboard focus
// (multipliedSaturation 0.9 vs 1.3), so the Int button's coral would be a
// lighter shade until the user clicked or tabbed to it. We override the
// background draw to always use the focused saturation -- consistent on
// startup, consistent through interaction.
class GritButtonLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto cornerSize = 6.0f;
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);

        auto baseColour = backgroundColour.withMultipliedSaturation(1.3f)
                                          .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
        if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
            baseColour = baseColour.contrasting(shouldDrawButtonAsDown ? 0.2f : 0.05f);

        g.setColour(baseColour);

        const bool flatOnLeft   = button.isConnectedOnLeft();
        const bool flatOnRight  = button.isConnectedOnRight();
        const bool flatOnTop    = button.isConnectedOnTop();
        const bool flatOnBottom = button.isConnectedOnBottom();

        if (flatOnLeft || flatOnRight || flatOnTop || flatOnBottom) {
            juce::Path path;
            path.addRoundedRectangle(bounds.getX(), bounds.getY(),
                                     bounds.getWidth(), bounds.getHeight(),
                                     cornerSize, cornerSize,
                                     !(flatOnLeft  || flatOnTop),
                                     !(flatOnRight || flatOnTop),
                                     !(flatOnLeft  || flatOnBottom),
                                     !(flatOnRight || flatOnBottom));
            g.fillPath(path);
            g.setColour(button.findColour(juce::ComboBox::outlineColourId));
            g.strokePath(path, juce::PathStrokeType(1.0f));
        } else {
            g.fillRoundedRectangle(bounds, cornerSize);
            g.setColour(button.findColour(juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
        }
    }
};

static GritButtonLookAndFeel gritLookAndFeel;

// Full 17-position label table (Sony anchors interleaved with user slots).
// Index by idx16 = position * 16; even = Sony anchor, odd = user slot.
static const char* kAllWaypointNames[17] = {
    "Half Echo",  "User 1", "Room",      "User 2",
    "Studio A",   "User 3", "Studio B",  "User 4",
    "Studio C",   "User 5", "Hall",      "User 6",
    "Space Echo", "User 7", "Echo",      "User 8",
    "Delay"
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
        updateEditButtonState();
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
    speedKnob.setValue(0.5, juce::dontSendNotification);
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

    // Morph Grit: binary radio strip [Int][Fract.].
    // Default Int = all reads integer, hardware-faithful, the project's
    // north-star setting. Coral PS1 palette to differentiate from the
    // speed knob's mauve.
    auto configureGritButton = [this](juce::TextButton& b, const char* label) {
        b.setClickingTogglesState(true);
        b.setRadioGroupId(0xC0DE); // shared id makes them mutually exclusive
        b.setButtonText(label);
        b.setColour(juce::TextButton::buttonColourId, psxDarkGray);
        b.setColour(juce::TextButton::buttonOnColourId, psxCoral);
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        b.setColour(juce::TextButton::textColourOffId, psxLightGray);
        b.setLookAndFeel(&gritLookAndFeel);  // focus-stable saturation
        addAndMakeVisible(b);
    };
    configureGritButton(gritIntButton,   "Int");
    configureGritButton(gritFractButton, "Fract.");
    // Connect the two adjacent edges so the strip reads as one control.
    gritIntButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    gritFractButton.setConnectedEdges(juce::Button::ConnectedOnLeft);

    gritIntButton.setTooltip("All reads integer — hardware-faithful PS1 character (default)");
    gritFractButton.setTooltip("All reads fractional — smoothed morph with feedback patina");

    gritIntButton.onClick   = [this]() { setMorphGrit(0); };
    gritFractButton.onClick = [this]() { setMorphGrit(1); };

    // Initialize toggle state from processor (preserves preset/project recall).
    setMorphGrit(processorRef.getMorphGrit().load(std::memory_order_relaxed));

    gritLabel.setText("Morph Grit", juce::dontSendNotification);
    gritLabel.setJustificationType(juce::Justification::centred);
    gritLabel.setColour(juce::Label::textColourId, psxLightGray.withAlpha(0.7f));
    gritLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(gritLabel);

    // EDIT / EXPORT / LOAD — three per-tick action buttons stacked top-right.
    // Same enabled-state logic: lit when the knob is parked on a user-slot
    // detent. Each fires its callback to PluginEditor with the slot index.
    auto configureSlotButton = [this](juce::TextButton& b, const char* label,
                                      const char* tip) {
        b.setButtonText(label);
        b.setColour(juce::TextButton::buttonColourId, psxDarkGray);
        b.setColour(juce::TextButton::buttonOnColourId, psxBlue);
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        b.setColour(juce::TextButton::textColourOffId, psxLightGray);
        b.setTooltip(tip);
        addAndMakeVisible(b);
    };
    configureSlotButton(editButton,   "EDIT",
        "Edit the user slot the knob is parked on");
    configureSlotButton(exportButton, "EXPORT",
        "Save this user slot's register values to a file");
    configureSlotButton(loadButton,   "LOAD",
        "Load a saved slot file and stamp it onto this tick");

    editButton.onClick = [this]()
    {
        const int slot = currentUserSlotAtKnob();
        if (slot >= 0 && onEditSlotClicked)
            onEditSlotClicked(slot);
    };
    exportButton.onClick = [this]()
    {
        const int slot = currentUserSlotAtKnob();
        if (slot >= 0 && onExportSlotClicked)
            onExportSlotClicked(slot);
    };
    loadButton.onClick = [this]()
    {
        const int slot = currentUserSlotAtKnob();
        if (slot >= 0 && onLoadSlotClicked)
            onLoadSlotClicked(slot);
    };

    // Set initial label to "Hall" (matching default morph position 0.625)
    updateLabelText(0.625);
    updateEditButtonState();
}

//==============================================================================
int MorphPanel::currentUserSlotAtKnob() const
{
    constexpr double snapThreshold = 0.01;
    const double v = morphKnob.getValue();
    for (int k = 0; k < 8; ++k)
    {
        const double midpoint = static_cast<double>(2 * k + 1) / 16.0;
        if (std::abs(v - midpoint) < snapThreshold)
            return k;
    }
    return -1;
}

void MorphPanel::updateEditButtonState()
{
    const int slot = currentUserSlotAtKnob();
    const bool onTick = (slot >= 0);
    editButton.setEnabled(onTick);
    loadButton.setEnabled(onTick);
    // EXPORT additionally requires the slot to actually have contents.
    exportButton.setEnabled(onTick && processorRef.isUserSlotFilled(slot));
}

//==============================================================================
void MorphPanel::setMorphGrit(int grit)
{
    if (grit != 0 && grit != 1) grit = 0; // fall back to Int (default)
    gritIntButton  .setToggleState(grit == 0, juce::dontSendNotification);
    gritFractButton.setToggleState(grit == 1, juce::dontSendNotification);
    processorRef.getMorphGrit().store(grit, std::memory_order_relaxed);
}

//==============================================================================
double MorphPanel::MorphSlider::snapValue(double attemptedValue, DragMode)
{
    // 17 detents: 9 Sony anchors at i/8, 8 user-slot midpoints at (2k+1)/16.
    // All snap with the same threshold so the encoder feel is uniform.
    constexpr int numDetents = 17;
    constexpr double snapThreshold = 0.01;
    for (int i = 0; i < numDetents; ++i)
    {
        double wp = static_cast<double>(i) / 16.0;
        if (std::abs(attemptedValue - wp) < snapThreshold)
            return wp;
    }
    return attemptedValue;
}

//==============================================================================
void MorphPanel::updateLabelText(double value)
{
    constexpr int numDetents = 17;
    constexpr double snapThreshold = 0.01;
    for (int i = 0; i < numDetents; ++i)
    {
        double wp = static_cast<double>(i) / 16.0;
        if (std::abs(value - wp) < snapThreshold)
        {
            morphLabel.setText(juce::String(kAllWaypointNames[i]),
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
    updateEditButtonState();
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

    // Draw user-slot ticks at the 8 midpoint positions (1/16, 3/16, ..., 15/16).
    // Ticks sit BETWEEN the encoder ring and the Sony dot ring -- visually
    // tucked close to the knob, leaving the dots as the outer "anchor" ring.
    // Filled slot = PS1 blue, empty slot = dim grey.
    //   knob outer edge:           knob_r
    //   tick range:                knob_r + 3  ..  knob_r + 9   (this band)
    //   Sony dots (centered):      knob_r + 12 (unchanged)
    constexpr int   numUserSlots = 8;
    constexpr float tickInner    = 3.0f;   // gap between knob edge and tick
    constexpr float tickLen      = 6.0f;
    constexpr float tickWidth    = 2.0f;
    const float knobRadius = knobBounds.getWidth() * 0.5f;
    const juce::Colour filledColour = psxBlue;
    const juce::Colour emptyColour  = psxLightGray.withAlpha(0.25f);

    for (int k = 0; k < numUserSlots; ++k)
    {
        float t = static_cast<float>(2 * k + 1) / 16.0f;  // (2k+1)/16
        float angle = startAngle + t * (endAngle - startAngle);
        float ang   = angle - juce::MathConstants<float>::halfPi;
        float ux    = std::cos(ang);
        float uy    = std::sin(ang);
        float r0    = knobRadius + tickInner;
        float r1    = knobRadius + tickInner + tickLen;
        float x0 = cx + r0 * ux, y0 = cy + r0 * uy;
        float x1 = cx + r1 * ux, y1 = cy + r1 * uy;

        g.setColour(processorRef.isUserSlotFilled(k) ? filledColour : emptyColour);
        g.drawLine(x0, y0, x1, y1, tickWidth);
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

    // Speed knob + 2-button Grit strip: paired row at the bottom of the
    // viewport, centered horizontally with a gap between them. Each has
    // its label below at the same baseline so the labels align.
    constexpr int speedSize = 60;
    constexpr int gritBtnW = 60;
    constexpr int gritBtnH = 28;
    constexpr int gritStripW = gritBtnW * 2; // buttons share edges, no inter-gap
    constexpr int controlLabelH = 14;
    constexpr int bottomMargin = 4;
    constexpr int gap = 32;

    int rowY = area.getHeight() - speedSize - controlLabelH - bottomMargin;

    int pairW = speedSize + gap + gritStripW;
    int pairX = (area.getWidth() - pairW) / 2;

    int speedX = pairX;
    speedKnob.setBounds(speedX, rowY, speedSize, speedSize);
    speedLabel.setBounds(speedX - 20, rowY + speedSize, speedSize + 40, controlLabelH);

    // Grit strip vertically centered on the speed-knob row.
    int stripX = pairX + speedSize + gap;
    int stripY = rowY + (speedSize - gritBtnH) / 2;
    gritIntButton  .setBounds(stripX,             stripY, gritBtnW, gritBtnH);
    gritFractButton.setBounds(stripX + gritBtnW,  stripY, gritBtnW, gritBtnH);
    gritLabel.setBounds(stripX - 20, rowY + speedSize, gritStripW + 40, controlLabelH);

    // EDIT / EXPORT / LOAD — stacked top-right corner of the panel. All three
    // buttons share width/height; the stack is anchored just below the morph
    // knob's top edge so it doesn't crowd the encoder.
    constexpr int slotBtnW    = 80;
    constexpr int slotBtnH    = 28;
    constexpr int slotBtnGap  = 4;
    constexpr int slotMargin  = 12;
    const int slotX = area.getRight() - slotBtnW - slotMargin;
    int       slotY = startY + slotMargin;
    editButton  .setBounds(slotX, slotY, slotBtnW, slotBtnH); slotY += slotBtnH + slotBtnGap;
    exportButton.setBounds(slotX, slotY, slotBtnW, slotBtnH); slotY += slotBtnH + slotBtnGap;
    loadButton  .setBounds(slotX, slotY, slotBtnW, slotBtnH);
}
