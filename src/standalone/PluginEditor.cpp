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

    // Preset Save button (D-06: opens name prompt, then file dialog)
    addAndMakeVisible(savePresetButton);
    savePresetButton.onClick = [this]() { showPresetNamePrompt(); };

    // Preset Load button (opens file dialog, reads .spu94 file)
    addAndMakeVisible(loadPresetButton);
    loadPresetButton.onClick = [this]()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Load Preset",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.spu94");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (!result.existsAsFile()) return;

                auto text = result.loadFileAsString();
                if (text.isEmpty()) return;

                if (processorRef.loadPresetFromString(text))
                {
                    // Extract name from the .spu94 file for dropdown display.
                    // Parse the name= line (between first newline-separated lines).
                    juce::String loadedName;
                    for (auto line : juce::StringArray::fromLines(text))
                    {
                        if (line.startsWith("name="))
                        {
                            loadedName = line.fromFirstOccurrenceOf("name=", false, false).trim();
                            break;
                        }
                    }
                    // Fall back to filename if no name in file (D-09)
                    if (loadedName.isEmpty())
                        loadedName = result.getFileNameWithoutExtension();

                    customPresetName = loadedName;
                }
            });
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

    // DAC True Oversample toggle (v1.2 vs v1.3 A/B)
    addAndMakeVisible(dacOversampleToggle);
    dacOversampleToggle.setClickingTogglesState(true);
    dacOversampleToggle.setToggleState(true, juce::dontSendNotification);
    dacOversampleToggle.onClick = [this] {
        processorRef.getDacTrueOversample().store(
            dacOversampleToggle.getToggleState(),
            std::memory_order_relaxed);
    };

    // Register panel -- all 35 SPU registers in a scrollable viewport.
    registerViewport.setViewedComponent(&registerPanel, false);
    registerViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(registerViewport);

    // Sync slider positions to the initial preset (Hall).
    registerPanel.updateFromShadows();

    // 30 Hz timer to detect preset-switch completion and sync sliders.
    lastAppliedCount = processorRef.getPresetQueue().getAppliedCount();
    lastFilePresetCount = processorRef.getFilePresetAppliedCount();
    startTimerHz(30);

    setResizeLimits(900, 800, 1600, 1400);
    setSize(900, 1100);
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

    // Detect file-preset load completion
    const int fileCount = processorRef.getFilePresetAppliedCount();
    if (fileCount != lastFilePresetCount)
    {
        lastFilePresetCount = fileCount;
        registerPanel.updateFromShadows();
        syncMixerKnobsFromProcessor();
    }
}

void SPU94AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);

    // Combined mixer + DAC zone label
    auto mixerLabelArea = juce::Rectangle<int>(10, getHeight() - 85, 100, 16);
    g.setFont(12.0f);
    g.setColour(juce::Colours::white);
    g.drawText("Mixer / DAC", mixerLabelArea, juce::Justification::left, false);

    // Horizontal separator line above combined zone
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine(getHeight() - 90, 10.0f, static_cast<float>(getWidth() - 10));
}

void SPU94AudioProcessorEditor::resized()
{
    const int w = getWidth();

    // ---- ZONE 1: Toolbar (y=10, h=60) ----
    loadButton.setBounds(10, 10, 100, 30);
    playButton.setBounds(115, 10, 70, 30);
    stopButton.setBounds(190, 10, 70, 30);
    savePresetButton.setBounds(270, 10, 55, 30);
    loadPresetButton.setBounds(330, 10, 55, 30);
    presetLabel.setBounds(395, 10, 50, 30);
    presetSelector.setBounds(448, 10, 180, 30);

    // Three equal-sized knobs: Input Gain, ADPCM Send, Dry Send
    inputLevelLabel.setBounds(640, 2, 80, 16);
    inputLevelKnob.setBounds(640, 16, 80, 54);
    adpcmSendLabel.setBounds(725, 2, 80, 16);
    adpcmSendKnob.setBounds(725, 16, 80, 54);
    drySendLabel.setBounds(810, 2, 80, 16);
    drySendKnob.setBounds(810, 16, 80, 54);

    // ---- ZONE 2: Register panel in scrollable viewport (fills middle) ----
    const int bottomZoneHeight = 80;
    const int registerTop = 75;
    const int registerBottom = getHeight() - bottomZoneHeight - 5;
    const int viewportW = w - 20;
    const int viewportH = registerBottom - registerTop;
    registerViewport.setBounds(10, registerTop, viewportW, viewportH);
    registerPanel.setSize(viewportW - registerViewport.getScrollBarThickness(),
                          registerPanel.getPreferredHeight());

    // ---- ZONE 3+4: Combined mixer + DAC (single bottom row) ----
    const int bottomY = registerBottom + 5;
    // Three level knobs
    dryKnobLabel.setBounds(120, bottomY, 80, 16);
    dryKnob.setBounds(120, bottomY + 14, 80, 54);
    patinaKnobLabel.setBounds(220, bottomY, 80, 16);
    patinaKnob.setBounds(220, bottomY + 14, 80, 54);
    reverbKnobLabel.setBounds(320, bottomY, 80, 16);
    reverbKnob.setBounds(320, bottomY + 14, 80, 54);
    // Toggles: Latency Comp + DAC section
    latencyCompToggle.setBounds(420, bottomY + 15, 100, 30);
    dacToggle.setBounds(520, bottomY + 15, 55, 30);
    dacFirToggle.setBounds(575, bottomY + 15, 50, 30);
    dacNoiseToggle.setBounds(625, bottomY + 15, 60, 30);
    dacOversampleToggle.setBounds(690, bottomY + 15, 50, 30);
}

void SPU94AudioProcessorEditor::showPresetNamePrompt()
{
    // D-07: Pre-fill from current preset name
    juce::String defaultName;
    if (customPresetName.isNotEmpty())
        defaultName = customPresetName;
    else
    {
        const int pid = processorRef.getPresetQueue().getAppliedId();
        if (pid >= 0 && pid < SPU94_PRESET__COUNT)
            defaultName = juce::String(spu94_presets[pid].name);
    }

    auto* window = new juce::AlertWindow("Save Preset",
                                          "Enter a name for this preset:",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", defaultName, "Name:");
    window->addTextEditor("desc", "", "Description (optional):");
    window->addButton("Save", 1);
    window->addButton("Cancel", 0);

    window->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [this, window](int result)
            {
                if (result == 0)
                {
                    delete window;
                    return;
                }

                auto name = window->getTextEditorContents("name").trim();
                auto desc = window->getTextEditorContents("desc").trim();
                delete window;

                if (name.isEmpty())
                {
                    // Silently cancel if no name provided (name is required per D-07)
                    return;
                }

                // D-08: typed name becomes default filename
                auto suggestedFile = juce::File::getSpecialLocation(
                    juce::File::userDocumentsDirectory)
                    .getChildFile(name + ".spu94");

                fileChooser = std::make_unique<juce::FileChooser>(
                    "Save Preset",
                    suggestedFile,
                    "*.spu94");

                fileChooser->launchAsync(
                    juce::FileBrowserComponent::saveMode |
                    juce::FileBrowserComponent::canSelectFiles |
                    juce::FileBrowserComponent::warnAboutOverwriting,
                    [this, name, desc](const juce::FileChooser& fc)
                    {
                        auto result = fc.getResult();
                        if (result == juce::File()) return;

                        auto text = processorRef.savePresetToString(name, desc);
                        if (text.isNotEmpty())
                        {
                            result.replaceWithText(text);
                            customPresetName = name;
                        }
                    });
            }),
        true);
}

void SPU94AudioProcessorEditor::syncMixerKnobsFromProcessor()
{
    inputLevelKnob.setValue(
        static_cast<double>(processorRef.getInputLevel().load(std::memory_order_relaxed)),
        juce::dontSendNotification);
    dryKnob.setValue(
        static_cast<double>(processorRef.getDryLevel().load(std::memory_order_relaxed)),
        juce::dontSendNotification);
    patinaKnob.setValue(
        static_cast<double>(processorRef.getPatinaLevel().load(std::memory_order_relaxed)),
        juce::dontSendNotification);
    reverbKnob.setValue(
        static_cast<double>(processorRef.getReverbLevel().load(std::memory_order_relaxed)),
        juce::dontSendNotification);
    adpcmSendKnob.setValue(
        static_cast<double>(processorRef.getAdpcmSend().load(std::memory_order_relaxed)),
        juce::dontSendNotification);
    drySendKnob.setValue(
        static_cast<double>(processorRef.getDrySend().load(std::memory_order_relaxed)),
        juce::dontSendNotification);
    latencyCompToggle.setToggleState(
        processorRef.getLatencyCompEnabled().load(std::memory_order_relaxed),
        juce::dontSendNotification);
    dacToggle.setToggleState(
        processorRef.getDacEnabled().load(std::memory_order_relaxed),
        juce::dontSendNotification);
    dacFirToggle.setToggleState(
        processorRef.getDacFirEnabled().load(std::memory_order_relaxed),
        juce::dontSendNotification);
    dacNoiseToggle.setToggleState(
        processorRef.getDacNoiseEnabled().load(std::memory_order_relaxed),
        juce::dontSendNotification);
    dacOversampleToggle.setToggleState(
        processorRef.getDacTrueOversample().load(std::memory_order_relaxed),
        juce::dontSendNotification);
}
