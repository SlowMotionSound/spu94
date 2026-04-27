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

    // Wet/Dry rotary knob -- equal-power crossfade (D-02, STANDALONE-06).
    addAndMakeVisible(wetDryKnob);
    wetDryKnob.setSliderStyle(juce::Slider::Rotary);
    wetDryKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    wetDryKnob.setRange(0.0, 1.0, 0.01);
    wetDryKnob.setValue(0.5, juce::dontSendNotification);  // default 50/50
    wetDryKnob.setName("Wet/Dry");

    wetDryKnob.onValueChange = [this] {
        processorRef.getWetDry().store(
            static_cast<float>(wetDryKnob.getValue()),
            std::memory_order_relaxed);
    };

    addAndMakeVisible(wetDryLabel);
    wetDryLabel.setText("Wet/Dry", juce::dontSendNotification);
    wetDryLabel.setJustificationType(juce::Justification::centred);

    // Input Level rotary knob -- pre-SPU attenuation for hot sources.
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
    inputLevelLabel.setText("Input", juce::dontSendNotification);
    inputLevelLabel.setJustificationType(juce::Justification::centred);

    // ADPCM toggle button -- amber glow when active (D-05, D-06).
    addAndMakeVisible(adpcmToggle);
    adpcmToggle.setClickingTogglesState(true);
    adpcmToggle.setColour(juce::ToggleButton::tickColourId,
                          juce::Colour(0xFFD4A017));  // amber
    adpcmToggle.onClick = [this] {
        processorRef.getAdpcmEnabled().store(
            adpcmToggle.getToggleState(),
            std::memory_order_relaxed);
    };

    // Register panel -- 18 sliders grouped by register class.
    addAndMakeVisible(registerPanel);

    // Sync slider positions to the initial preset (Hall).
    registerPanel.updateFromShadows();

    // 30 Hz timer to detect preset-switch completion and sync sliders.
    lastAppliedCount = processorRef.getPresetQueue().getAppliedCount();
    startTimerHz(30);

    setResizeLimits(850, 750, 850, 750);
    setSize(850, 750);
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
}

void SPU94AudioProcessorEditor::resized()
{
    // Top row (y=10, h=30): buttons + preset selector + Wet/Dry knob.
    loadButton.setBounds(10, 10, 120, 30);
    playButton.setBounds(140, 10, 80, 30);
    stopButton.setBounds(230, 10, 80, 30);
    presetLabel.setBounds(330, 10, 60, 30);
    presetSelector.setBounds(395, 10, 180, 30);

    // ADPCM toggle sits between preset selector and Input knob (D-05).
    adpcmToggle.setBounds(585, 10, 70, 30);

    // Input Level knob -- shifted right to accommodate ADPCM toggle.
    inputLevelLabel.setBounds(660, 2, 80, 16);
    inputLevelKnob.setBounds(660, 16, 80, 54);

    // Wet/Dry rotary knob -- shifted right.
    wetDryLabel.setBounds(750, 2, 80, 16);
    wetDryKnob.setBounds(750, 16, 80, 54);

    // Register panel fills remaining height below the toolbar.
    registerPanel.setBounds(10, 75, getWidth() - 20, getHeight() - 85);
}
