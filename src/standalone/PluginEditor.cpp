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

    // Register panel -- 18 sliders grouped by register class.
    addAndMakeVisible(registerPanel);

    // Sync slider positions to the initial preset (Hall).
    registerPanel.updateFromShadows();

    // 30 Hz timer to detect preset-switch completion and sync sliders.
    lastAppliedCount = processorRef.getPresetQueue().getAppliedCount();
    startTimerHz(30);

    setSize(800, 750);
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
    // Top row (y=10, h=30): buttons + preset selector.
    loadButton.setBounds(10, 10, 120, 30);
    playButton.setBounds(140, 10, 80, 30);
    stopButton.setBounds(230, 10, 80, 30);
    presetLabel.setBounds(330, 10, 60, 30);
    presetSelector.setBounds(395, 10, 180, 30);

    // Register panel fills remaining height below buttons.
    registerPanel.setBounds(10, 50, getWidth() - 20, getHeight() - 60);
}
