#include "PluginEditor.h"

SPU94AudioProcessorEditor::SPU94AudioProcessorEditor(SPU94AudioProcessor& p)
    : AudioProcessorEditor(p),
      processorRef(p),
      registerPanel(p.getRegisterBridge()),
      morphPanel(p)
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
        const int id = presetSelector.getSelectedId();
        if (id == kCustomPresetId)
            return;  // Custom entry selected -- no action needed

        // D-10: Selecting a factory preset clears the custom entry
        if (presetSelector.indexOfItemId(kCustomPresetId) >= 0)
        {
            presetSelector.clear(juce::dontSendNotification);
            for (int i = 0; i < SPU94_PRESET__COUNT; ++i)
                presetSelector.addItem(juce::String(spu94_presets[i].name), i + 1);
            customPresetName = {};
            modifiedState = false;
        }

        const int pid = id - 1;  // back to 0-based
        if (pid >= 0 && pid < SPU94_PRESET__COUNT)
        {
            processorRef.getPresetQueue().requestPreset(
                static_cast<spu94_preset_id_t>(pid));
        }
    };

    // ---- ZONE 1: Toolbar controls ----

    // Input Gain rotary knob (renamed from "Input").
    addAndMakeVisible(inputLevelKnob);
    inputLevelKnob.setSliderStyle(juce::Slider::Rotary);
    inputLevelKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    inputLevelKnob.setRange(0.0, 1.0, 0.01);
    inputLevelKnob.setValue(0.50, juce::dontSendNotification);

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
    adpcmSendKnob.setValue(1.0, juce::dontSendNotification);
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
    drySendKnob.setValue(0.0, juce::dontSendNotification);
    drySendKnob.onValueChange = [this] {
        processorRef.getDrySend().store(
            static_cast<float>(drySendKnob.getValue()),
            std::memory_order_relaxed);
    };
    addAndMakeVisible(drySendLabel);
    drySendLabel.setText("Dry Send", juce::dontSendNotification);
    drySendLabel.setJustificationType(juce::Justification::centred);

    // (Morph Speed knob lives inside MorphPanel — see MorphPanel.cpp)

    // ---- ZONE 3: Mixer strip ----

    // Dry level knob
    addAndMakeVisible(dryKnob);
    dryKnob.setSliderStyle(juce::Slider::Rotary);
    dryKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    dryKnob.setRange(0.0, 1.0, 0.01);
    dryKnob.setValue(0.0, juce::dontSendNotification);
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
    dacToggle.setToggleState(true, juce::dontSendNotification);
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

    // Morph panel -- default view (D-01, D-02)
    addAndMakeVisible(morphPanel);

    // Start with macro view: morph panel visible, register panel hidden
    registerViewport.setVisible(false);

    // EDIT-flow buttons (D-01 superseded). User clicks EDIT in MorphPanel
    // while parked on a user-slot detent; we swap to Advanced view with the
    // sliders pre-loaded. SAVE captures the edits into the slot; REVERT
    // discards them. Both close Advanced view.
    morphPanel.onEditSlotClicked = [this](int slot) { enterAdvancedView(slot); };

    saveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF6FD8CE)); // PS1 teal
    saveButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    saveButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    saveButton.setVisible(false);
    saveButton.onClick = [this] { exitAdvancedView(true); };
    addAndMakeVisible(saveButton);

    revertButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE8736E)); // PS1 coral
    revertButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    revertButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    revertButton.setVisible(false);
    revertButton.onClick = [this] { exitAdvancedView(false); };
    addAndMakeVisible(revertButton);

    // Sync slider positions to the initial preset (Hall).
    registerPanel.updateFromShadows();

    // 30 Hz timer to detect preset-switch completion and sync sliders.
    lastAppliedCount = processorRef.getPresetQueue().getAppliedCount();
    lastFilePresetCount = processorRef.getFilePresetAppliedCount();
    startTimerHz(30);

    captureBaseline();

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

        // Reset all preset names to clean state (clears asterisks and custom entries)
        presetSelector.clear(juce::dontSendNotification);
        for (int i = 0; i < SPU94_PRESET__COUNT; ++i)
            presetSelector.addItem(juce::String(spu94_presets[i].name), i + 1);
        customPresetName = {};

        presetSelector.setSelectedId(appliedId + 1, juce::dontSendNotification);
        captureBaseline();
        modifiedState = false;
    }

    // Detect file-preset load completion
    const int fileCount = processorRef.getFilePresetAppliedCount();
    if (fileCount != lastFilePresetCount)
    {
        lastFilePresetCount = fileCount;
        registerPanel.updateFromShadows();
        syncMixerKnobsFromProcessor();

        // D-09: Show custom preset entry in dropdown
        if (presetSelector.getNumItems() == SPU94_PRESET__COUNT)
        {
            // Add custom entry (appends at end; JUCE doesn't support insert-at-position)
            presetSelector.addItem(juce::String::charToString(0x25C6) + " " + customPresetName,
                                   kCustomPresetId);
        }
        else
        {
            // Update existing custom entry text
            presetSelector.changeItemText(kCustomPresetId,
                juce::String::charToString(0x25C6) + " " + customPresetName);
        }
        presetSelector.setSelectedId(kCustomPresetId, juce::dontSendNotification);
        captureBaseline();  // D-12: file load resets baseline
    }

    // D-11: check for modified state and update asterisk
    updatePresetDisplayName();

    // Sync morph panel knob position when visible (Phase 17)
    if (morphPanel.isVisible())
    {
        morphPanel.updateKnobPosition();
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

    // Morph panel occupies the same Zone 2 bounds (D-01)
    morphPanel.setBounds(10, registerTop, viewportW, viewportH);

    // ---- ZONE 3+4: Combined mixer + DAC (single bottom row) ----
    const int bottomY = registerBottom + 5;
    // (Morph Speed knob is laid out by MorphPanel inside the viewport.)
    // Three level knobs (unchanged)
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

    // SAVE / REVERT buttons sit in the same horizontal band the old Advanced
    // toggle occupied, but as a centered pair. Visible only while editing.
    constexpr int savW = 90, savH = 30, savGap = 10;
    const int pairW = savW * 2 + savGap;
    const int pairX = (w - pairW) / 2;
    saveButton.setBounds(pairX,                    bottomY + 15, savW, savH);
    revertButton.setBounds(pairX + savW + savGap,  bottomY + 15, savW, savH);
}

void SPU94AudioProcessorEditor::showPresetNamePrompt()
{
    juce::String defaultName;
    if (customPresetName.isNotEmpty())
        defaultName = customPresetName;
    else
    {
        const int pid = processorRef.getPresetQueue().getAppliedId();
        if (pid >= 0 && pid < SPU94_PRESET__COUNT)
            defaultName = juce::String(spu94_presets[pid].name);
    }

    auto suggestedFile = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory)
        .getChildFile(defaultName + ".spu94");

    // Touch-create so kdialog pre-fills the filename (it drops names for non-existent files)
    if (!suggestedFile.exists())
        suggestedFile.create();

    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Preset",
        suggestedFile,
        "*.spu94");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this, suggestedFile](const juce::FileChooser& fc)
        {
            auto chosen = fc.getResult();

            // Clean up the touch-created file if user cancelled or picked a different path
            if (suggestedFile.getSize() == 0 && suggestedFile != chosen)
                suggestedFile.deleteFile();

            if (chosen == juce::File()) return;

            auto name = chosen.getFileNameWithoutExtension();
            auto text = processorRef.savePresetToString(name, {});
            if (text.isNotEmpty())
            {
                chosen.replaceWithText(text);
                customPresetName = name;
            }
        });
}

void SPU94AudioProcessorEditor::captureBaseline()
{
    for (size_t i = 0; i < SPU94_REG__COUNT; ++i)
        baseline.registers[i] = processorRef.getRegisterBridge().getShadowValue(i);
    baseline.inputGain = processorRef.getInputLevel().load(std::memory_order_relaxed);
    baseline.dry = processorRef.getDryLevel().load(std::memory_order_relaxed);
    baseline.patina = processorRef.getPatinaLevel().load(std::memory_order_relaxed);
    baseline.reverb = processorRef.getReverbLevel().load(std::memory_order_relaxed);
    baseline.adpcmSend = processorRef.getAdpcmSend().load(std::memory_order_relaxed);
    baseline.drySend = processorRef.getDrySend().load(std::memory_order_relaxed);
    baseline.latencyComp = processorRef.getLatencyCompEnabled().load(std::memory_order_relaxed);
    baseline.dac = processorRef.getDacEnabled().load(std::memory_order_relaxed);
    baseline.dacFir = processorRef.getDacFirEnabled().load(std::memory_order_relaxed);
    baseline.dacNoise = processorRef.getDacNoiseEnabled().load(std::memory_order_relaxed);
    baseline.dacOversample = processorRef.getDacTrueOversample().load(std::memory_order_relaxed);
    modifiedState = false;
}

bool SPU94AudioProcessorEditor::checkModified() const
{
    for (size_t i = 0; i < SPU94_REG__COUNT; ++i)
    {
        if (processorRef.getRegisterBridge().getShadowValue(i) != baseline.registers[i])
            return true;
    }
    auto& p = processorRef;
    // Float comparisons: these are slider-sourced values stored/loaded without
    // arithmetic, so bitwise equality is the correct test. Use memcmp to
    // avoid -Wfloat-equal warnings while preserving exact-match semantics.
    auto feq = [](float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; };
    if (!feq(p.getInputLevel().load(std::memory_order_relaxed), baseline.inputGain)) return true;
    if (!feq(p.getDryLevel().load(std::memory_order_relaxed), baseline.dry)) return true;
    if (!feq(p.getPatinaLevel().load(std::memory_order_relaxed), baseline.patina)) return true;
    if (!feq(p.getReverbLevel().load(std::memory_order_relaxed), baseline.reverb)) return true;
    if (!feq(p.getAdpcmSend().load(std::memory_order_relaxed), baseline.adpcmSend)) return true;
    if (!feq(p.getDrySend().load(std::memory_order_relaxed), baseline.drySend)) return true;
    if (p.getLatencyCompEnabled().load(std::memory_order_relaxed) != baseline.latencyComp) return true;
    if (p.getDacEnabled().load(std::memory_order_relaxed) != baseline.dac) return true;
    if (p.getDacFirEnabled().load(std::memory_order_relaxed) != baseline.dacFir) return true;
    if (p.getDacNoiseEnabled().load(std::memory_order_relaxed) != baseline.dacNoise) return true;
    if (p.getDacTrueOversample().load(std::memory_order_relaxed) != baseline.dacOversample) return true;
    return false;
}

void SPU94AudioProcessorEditor::updatePresetDisplayName()
{
    bool nowModified = checkModified();
    if (nowModified == modifiedState) return;  // no change
    modifiedState = nowModified;

    const int selectedId = presetSelector.getSelectedId();
    if (selectedId == kCustomPresetId)
    {
        juce::String display = juce::String::charToString(0x25C6) + " " + customPresetName;
        if (nowModified) display += " *";
        presetSelector.changeItemText(kCustomPresetId, display);
        // Force visual refresh
        presetSelector.setSelectedId(kCustomPresetId, juce::dontSendNotification);
    }
    else if (selectedId >= 1 && selectedId <= SPU94_PRESET__COUNT)
    {
        const int pid = selectedId - 1;
        juce::String display(spu94_presets[pid].name);
        if (nowModified) display += " *";
        presetSelector.changeItemText(selectedId, display);
        presetSelector.setSelectedId(selectedId, juce::dontSendNotification);
    }
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

//==============================================================================
// User-slot edit flow.
//
// Entry: MorphPanel calls onEditSlotClicked when its EDIT button fires while
// the knob is parked on a user-slot detent. We hand register ownership from
// the morph engine to the GUI sliders, snapshot the current interpolated
// state into the slider shadows, and reveal SAVE / REVERT.
//
// Exit (SAVE): capture the edited registers into user_slots[slot], hand
// register ownership back to the morph engine, force a morph re-apply so the
// new slot contents take effect at the current position. The bit-identity
// carve-out at the slot midpoint means SAVE itself produces no audible glitch
// -- the registers the morph engine writes back match what was just captured.
//
// Exit (REVERT): same teardown without the snapshot. The morph re-apply
// overwrites the slider edits with the v1.5 transparent-slot interpolation.
//==============================================================================
void SPU94AudioProcessorEditor::enterAdvancedView(int slot)
{
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return;

    // Knob is already parked on the slot's midpoint (the EDIT button is only
    // enabled at user-slot detents), so the morph engine has already applied
    // set_morph at this position -- engines[0] regs reflect that state.
    processorRef.getCurrentEditingSlot().store(slot, std::memory_order_relaxed);
    processorRef.getMorphActive().store(false, std::memory_order_relaxed);
    processorRef.requestShadowSync();

    morphPanel.setVisible(false);
    registerViewport.setVisible(true);
    saveButton.setVisible(true);
    revertButton.setVisible(true);
}

void SPU94AudioProcessorEditor::exitAdvancedView(bool save)
{
    const int slot = processorRef.getCurrentEditingSlot().load(std::memory_order_relaxed);
    if (save && slot >= 0)
        processorRef.saveUserSlot(slot);

    processorRef.getCurrentEditingSlot().store(-1, std::memory_order_relaxed);

    // Hand registers back to the morph engine and force re-apply at the
    // current (still-midpoint) position. After SAVE the slot is freshly
    // filled, so the bit-identity carve-out writes the captured regs back
    // -- audibly silent transition. After REVERT the slot stays empty and
    // the engine writes the transparent-slot interpolated state.
    processorRef.getMorphActive().store(true, std::memory_order_relaxed);
    processorRef.requestMorphReapply();

    saveButton.setVisible(false);
    revertButton.setVisible(false);
    registerViewport.setVisible(false);
    morphPanel.setVisible(true);
}
