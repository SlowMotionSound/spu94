#include "PluginEditor.h"

SPU94AudioProcessorEditor::SPU94AudioProcessorEditor(SPU94AudioProcessor& p)
    : AudioProcessorEditor(p),
      processorRef(p),
      registerPanel(p.getRegisterBridge())
{
    // Load WAV button -- opens async file picker.
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select a WAV file",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.aif");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    processorRef.loadWavFile(result);
                }
            });
    };

    // Play button -- starts playback of loaded WAV through SPU.
    addAndMakeVisible(playButton);
    playButton.onClick = [this]()
    {
        processorRef.startPlayback();
    };

    // Stop button -- stops playback and resets position.
    addAndMakeVisible(stopButton);
    stopButton.onClick = [this]()
    {
        processorRef.stopPlayback();
    };

    // Preset selector -- flat dropdown with all 10 PS1 factory presets.
    addAndMakeVisible(presetLabel);
    addAndMakeVisible(presetSelector);
    for (int i = 0; i < SPU94_PRESET__COUNT; ++i)
        presetSelector.addItem(juce::String(spu94_presets[i].name), i + 1);
    // JUCE ComboBox IDs are 1-based; default to Hall (matches prepareToPlay).
    presetSelector.setSelectedId(SPU94_PRESET_HALL + 1, juce::dontSendNotification);

    presetSelector.onChange = [this] {
        const int id = presetSelector.getSelectedId() - 1;  // back to 0-based
        if (id >= 0 && id < SPU94_PRESET__COUNT)
            processorRef.getPresetQueue().requestPreset(
                static_cast<spu94_preset_id_t>(id));
    };

    // ---- ZONE 1: Toolbar controls ----

    // Input Gain rotary knob (renamed from "Input").
    addAndMakeVisible(inputLevelKnob);
    inputLevelKnob.setSliderStyle(juce::Slider::Rotary);
    inputLevelKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    inputLevelKnob.setRange(0.0, 1.0, 0.01);
    inputLevelKnob.setValue(0.25, juce::dontSendNotification);

    inputLevelKnob.onValueChange = [this] {
        processorRef.getInputLevel().store(
            static_cast<float>(inputLevelKnob.getValue()),
            std::memory_order_relaxed);
    };

    addAndMakeVisible(inputLevelLabel);
    inputLevelLabel.setText("Input Gain", juce::dontSendNotification);
    inputLevelLabel.setJustificationType(juce::Justification::centred);

    // Reverb Sends: ADPCM Send knob
    addAndMakeVisible(adpcmSendKnob);
    adpcmSendKnob.setSliderStyle(juce::Slider::Rotary);
    adpcmSendKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    adpcmSendKnob.setRange(0.0, 1.0, 0.01);
    adpcmSendKnob.setValue(0.0, juce::dontSendNotification);
    adpcmSendKnob.onValueChange = [this] {
        processorRef.getAdpcmSend().store(
            static_cast<float>(adpcmSendKnob.getValue()),
            std::memory_order_relaxed);
    };
    addAndMakeVisible(adpcmSendLabel);
    adpcmSendLabel.setText("ADPCM Send", juce::dontSendNotification);
    adpcmSendLabel.setJustificationType(juce::Justification::centred);

    // Reverb Sends: Dry Input Send knob
    addAndMakeVisible(drySendKnob);
    drySendKnob.setSliderStyle(juce::Slider::Rotary);
    drySendKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    drySendKnob.setRange(0.0, 1.0, 0.01);
    drySendKnob.setValue(1.0, juce::dontSendNotification);
    drySendKnob.onValueChange = [this] {
        processorRef.getDrySend().store(
            static_cast<float>(drySendKnob.getValue()),
            std::memory_order_relaxed);
    };
    addAndMakeVisible(drySendLabel);
    drySendLabel.setText("Dry Send", juce::dontSendNotification);
    drySendLabel.setJustificationType(juce::Justification::centred);

    // ---- ZONE 3: Mixer strip ----

    // Dry level knob
    addAndMakeVisible(dryKnob);
    dryKnob.setSliderStyle(juce::Slider::Rotary);
    dryKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    dryKnob.setRange(0.0, 1.0, 0.01);
    dryKnob.setValue(1.0, juce::dontSendNotification);
    dryKnob.onValueChange = [this] {
        processorRef.getDryLevel().store(
            static_cast<float>(dryKnob.getValue()),
            std::memory_order_relaxed);
    };
    addAndMakeVisible(dryKnobLabel);
    dryKnobLabel.setText("Dry", juce::dontSendNotification);
    dryKnobLabel.setJustificationType(juce::Justification::centred);

    // ADPCM (Patina) level knob
    addAndMakeVisible(patinaKnob);
    patinaKnob.setSliderStyle(juce::Slider::Rotary);
    patinaKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    patinaKnob.setRange(0.0, 1.0, 0.01);
    patinaKnob.setValue(0.0, juce::dontSendNotification);
    patinaKnob.onValueChange = [this] {
        processorRef.getPatinaLevel().store(
            static_cast<float>(patinaKnob.getValue()),
            std::memory_order_relaxed);
    };
    addAndMakeVisible(patinaKnobLabel);
    patinaKnobLabel.setText("ADPCM", juce::dontSendNotification);
    patinaKnobLabel.setJustificationType(juce::Justification::centred);

    // Reverb level knob
    addAndMakeVisible(reverbKnob);
    reverbKnob.setSliderStyle(juce::Slider::Rotary);
    reverbKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    reverbKnob.setRange(0.0, 1.0, 0.01);
    reverbKnob.setValue(1.0, juce::dontSendNotification);
    reverbKnob.onValueChange = [this] {
        processorRef.getReverbLevel().store(
            static_cast<float>(reverbKnob.getValue()),
            std::memory_order_relaxed);
    };
    addAndMakeVisible(reverbKnobLabel);
    reverbKnobLabel.setText("Reverb", juce::dontSendNotification);
    reverbKnobLabel.setJustificationType(juce::Justification::centred);

    // Latency Comp toggle (D-11)
    addAndMakeVisible(latencyCompToggle);
    latencyCompToggle.setClickingTogglesState(true);
    latencyCompToggle.setToggleState(true, juce::dontSendNotification);  // ON by default
    latencyCompToggle.onClick = [this] {
        processorRef.getLatencyCompEnabled().store(
            latencyCompToggle.getToggleState(),
            std::memory_order_relaxed);
    };

    // ---- ZONE 4: DAC section ----

    // DAC master toggle
    addAndMakeVisible(dacToggle);
    dacToggle.setClickingTogglesState(true);
    dacToggle.setColour(juce::ToggleButton::tickColourId,
                        juce::Colour(0xFFD4A017));  // amber
    dacToggle.onClick = [this] {
        processorRef.getDacEnabled().store(
            dacToggle.getToggleState(),
            std::memory_order_relaxed);
    };

    // DAC FIR sub-toggle
    addAndMakeVisible(dacFirToggle);
    dacFirToggle.setClickingTogglesState(true);
    dacFirToggle.setToggleState(true, juce::dontSendNotification);  // ON by default
    dacFirToggle.onClick = [this] {
        processorRef.getDacFirEnabled().store(
            dacFirToggle.getToggleState(),
            std::memory_order_relaxed);
    };

    // DAC Noise sub-toggle
    addAndMakeVisible(dacNoiseToggle);
    dacNoiseToggle.setClickingTogglesState(true);
    dacNoiseToggle.setToggleState(true, juce::dontSendNotification);  // ON by default
    dacNoiseToggle.onClick = [this] {
        processorRef.getDacNoiseEnabled().store(
            dacNoiseToggle.getToggleState(),
            std::memory_order_relaxed);
    };

    // Register panel -- 18 sliders grouped by register class.
    addAndMakeVisible(registerPanel);

    // Sync slider positions to the initial preset (Hall).
    registerPanel.updateFromShadows();

    // 30 Hz timer to detect preset-switch completion and sync sliders.
    lastAppliedCount = processorRef.getPresetQueue().getAppliedCount();
    startTimerHz(30);

    setResizeLimits(900, 800, 900, 800);
    setSize(900, 800);
}

SPU94AudioProcessorEditor::~SPU94AudioProcessorEditor()
{
    stopTimer();
}

void SPU94AudioProcessorEditor::timerCallback()
{
    const int current = processorRef.getPresetQueue().getAppliedCount();
    if (current != lastAppliedCount)
    {
        lastAppliedCount = current;
        registerPanel.updateFromShadows();
        const int appliedId = processorRef.getPresetQueue().getAppliedId();
        presetSelector.setSelectedId(appliedId + 1,
                                      juce::dontSendNotification);
    }
}

void SPU94AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);

    // Reverb Sends outlined section (D-10)
    g.setColour(juce::Colours::lightgrey);
    auto sendsArea = juce::Rectangle<int>(590, 2, 190, 68);
    g.drawRoundedRectangle(sendsArea.toFloat(), 4.0f, 1.0f);
    g.setFont(11.0f);
    g.drawText("Reverb Sends", sendsArea.removeFromTop(14),
               juce::Justification::centred, false);

    // Mixer strip zone label
    auto mixerLabelArea = juce::Rectangle<int>(10, getHeight() - 170, 100, 16);
    g.setFont(12.0f);
    g.setColour(juce::Colours::white);
    g.drawText("Mixer", mixerLabelArea, juce::Justification::left, false);

    // DAC section zone label
    auto dacLabelArea = juce::Rectangle<int>(10, getHeight() - 55, 100, 16);
    g.drawText("DAC Model", dacLabelArea, juce::Justification::left, false);

    // Horizontal separator lines between zones
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine(getHeight() - 175, 10.0f, static_cast<float>(getWidth() - 10));  // above mixer
    g.drawHorizontalLine(getHeight() - 60, 10.0f, static_cast<float>(getWidth() - 10));   // above DAC
}

void SPU94AudioProcessorEditor::resized()
{
    const int w = getWidth();

    // ---- ZONE 1: Toolbar (y=10, h=60) ----
    loadButton.setBounds(10, 10, 120, 30);
    playButton.setBounds(140, 10, 80, 30);
    stopButton.setBounds(230, 10, 80, 30);
    presetLabel.setBounds(330, 10, 60, 30);
    presetSelector.setBounds(395, 10, 180, 30);

    // Input Gain knob (renamed from "Input")
    inputLevelLabel.setBounds(790, 2, 80, 16);
    inputLevelKnob.setBounds(790, 16, 80, 54);

    // Reverb Sends outlined section (D-10)
    // ADPCM Send and Dry Send knobs inside the outlined area
    adpcmSendLabel.setBounds(600, 14, 80, 14);
    adpcmSendKnob.setBounds(600, 28, 80, 42);
    drySendLabel.setBounds(690, 14, 80, 14);
    drySendKnob.setBounds(690, 28, 80, 42);

    // ---- ZONE 2: Register panel (fills middle) ----
    const int mixerZoneHeight = 90;
    const int dacZoneHeight = 50;
    const int registerTop = 75;
    const int registerBottom = getHeight() - mixerZoneHeight - dacZoneHeight - 10;
    registerPanel.setBounds(10, registerTop, w - 20, registerBottom - registerTop);

    // ---- ZONE 3: Mixer strip (below registers) ----
    const int mixerY = registerBottom + 10;
    // Three level knobs
    dryKnobLabel.setBounds(120, mixerY, 80, 16);
    dryKnob.setBounds(120, mixerY + 14, 80, 54);
    patinaKnobLabel.setBounds(220, mixerY, 80, 16);
    patinaKnob.setBounds(220, mixerY + 14, 80, 54);
    reverbKnobLabel.setBounds(320, mixerY, 80, 16);
    reverbKnob.setBounds(320, mixerY + 14, 80, 54);
    // Latency Comp toggle (D-11)
    latencyCompToggle.setBounds(430, mixerY + 20, 130, 30);

    // ---- ZONE 4: DAC section (bottom row) ----
    const int dacY = mixerY + mixerZoneHeight;
    dacToggle.setBounds(120, dacY + 5, 60, 30);
    dacFirToggle.setBounds(200, dacY + 5, 60, 30);
    dacNoiseToggle.setBounds(280, dacY + 5, 70, 30);
}
