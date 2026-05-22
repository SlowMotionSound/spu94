#include "PluginEditor.h"
#include <cmath>

SPU94AudioProcessorEditor::SPU94AudioProcessorEditor(SPU94AudioProcessor& p)
    : AudioProcessorEditor(p),
      processorRef(p),
      registerPanel(p.getRegisterBridge()),
      morphPanel(p)
{
    // WAV-loader UI (Load / Play / Stop) is the standalone testbed's signal
    // source. Gated on wrapperType so plugin formats (VST3 / AU / LV2 / CLAP)
    // never instantiate these as visible children -- they take their input
    // from the host buffer instead (PLUG-49). The toolbar slot from x=10..265
    // intentionally shows empty space in plugin formats; toolbar polish is
    // deferred to a later UI phase.
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
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

        // ---- Sampler window + voice engine controls ----
        samplerWindow = std::make_unique<SamplerWindow>();
        auto& panel = samplerWindow->getPanel();

        // Load Sample button -- opens async file picker, encodes WAV to ADPCM.
        panel.addAndMakeVisible(loadSampleButton);
        loadSampleButton.onClick = [this]()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Select a WAV file for voice engine",
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.aiff;*.aif");

            fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc)
                {
                    auto result = fc.getResult();
                    if (result.existsAsFile())
                        processorRef.loadVoiceSample(result);
                });
        };

        // Trigger button -- gate or latch behavior depending on toggle.
        panel.addAndMakeVisible(triggerVoiceButton);
        triggerVoiceButton.addMouseListener(this, false);
        panel.addAndMakeVisible(latchToggle);
        latchToggle.setToggleState(false, juce::dontSendNotification);

        // Stop Voice button -- keys off voice 0.
        panel.addAndMakeVisible(stopVoiceButton);
        stopVoiceButton.onClick = [this]()
        {
            processorRef.stopVoice();
        };

        // Voice engine pitch knob -- raw SPU pitch register value (0x0001..0x3FFF).
        voiceEnginePitchKnob.setSliderStyle(juce::Slider::Rotary);
        voiceEnginePitchKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        voiceEnginePitchKnob.setRange(1.0, 16383.0, 1.0);
        voiceEnginePitchKnob.setValue(4096.0, juce::dontSendNotification);
        voiceEnginePitchKnob.setTextValueSuffix(" pitch");
        voiceEnginePitchKnob.onValueChange = [this] {
            processorRef.setGuiVoicePitch(
                static_cast<uint16_t>(voiceEnginePitchKnob.getValue()));
        };
        panel.addAndMakeVisible(voiceEnginePitchKnob);

        voiceEnginePitchLabel.setText("Voice Pitch", juce::dontSendNotification);
        voiceEnginePitchLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(voiceEnginePitchLabel);

        // Voice sample status label -- shows filename + byte count after load.
        voiceSampleLabel.setText("", juce::dontSendNotification);
        voiceSampleLabel.setJustificationType(juce::Justification::centredLeft);
        panel.addAndMakeVisible(voiceSampleLabel);

        // Encode rate selector — pre-encode downsample rate (PS1 dev workflow).
        encodeRateBox.addItem("44100 Hz", 1);
        encodeRateBox.addItem("22050 Hz", 2);
        encodeRateBox.addItem("11025 Hz", 3);
        encodeRateBox.addItem("5512 Hz",  4);
        encodeRateBox.setSelectedId(2, juce::dontSendNotification);
        encodeRateBox.onChange = [this] {
            static const int rates[] = { 44100, 22050, 11025, 5512 };
            int idx = encodeRateBox.getSelectedId() - 1;
            if (idx >= 0 && idx < 4)
                processorRef.setEncodeRate(rates[idx]);
        };
        panel.addAndMakeVisible(encodeRateBox);
        encodeRateLabel.setText("Encode Rate", juce::dontSendNotification);
        encodeRateLabel.setJustificationType(juce::Justification::centred);
        encodeRateLabel.setFont(juce::Font(10.0f));
        panel.addAndMakeVisible(encodeRateLabel);

        // RAM usage meter label — updated in timerCallback.
        ramMeterLabel.setText("RAM: 0 / 512 KB", juce::dontSendNotification);
        ramMeterLabel.setJustificationType(juce::Justification::centredLeft);
        ramMeterLabel.setFont(juce::Font(10.0f));
        panel.addAndMakeVisible(ramMeterLabel);

        // Loop mode toggle -- one-shot (default) vs loop playback.
        panel.addAndMakeVisible(loopToggle);
        loopToggle.setClickingTogglesState(true);
        loopToggle.onClick = [this] {
            bool on = loopToggle.getToggleState();
            processorRef.getLoopModeEnabled().store(on, std::memory_order_relaxed);
            if (samplerWindow)
                samplerWindow->getWaveformDisplay().setLoopMode(on);
        };

        // Marker lock toggle — freeze relative distances, drag window as unit.
        panel.addAndMakeVisible(markerLockToggle);
        markerLockToggle.setClickingTogglesState(true);
        markerLockToggle.onClick = [this] {
            if (markerLockToggle.getToggleState()) {
                lockedLoopOffset = loopPosKnob.getValue() - startPosKnob.getValue();
                lockedEndOffset  = endPosKnob.getValue()  - startPosKnob.getValue();
            }
        };

        // Gauss toggle — switches all 24 voices between Gaussian
        // interpolation (ON, default = PS1 faithful) and zero-order hold (OFF).
        panel.addAndMakeVisible(samplerAAToggle);
        samplerAAToggle.setClickingTogglesState(true);
        samplerAAToggle.setToggleState(true, juce::dontSendNotification);
        samplerAAToggle.onClick = [this] {
            processorRef.getSamplerAAEnabled().store(
                samplerAAToggle.getToggleState(), std::memory_order_relaxed);
        };

        // Marker position knobs — precise control for start/loop/end points.
        auto setupPosKnob = [&](juce::Slider& knob, juce::Label& label,
                                const char* name, double initVal) {
            knob.setSliderStyle(juce::Slider::Rotary);
            knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
            knob.setRange(0.0, 1.0, 0.001);
            knob.setValue(initVal, juce::dontSendNotification);
            panel.addAndMakeVisible(knob);
            label.setText(name, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setFont(juce::Font(10.0f));
            panel.addAndMakeVisible(label);
        };
        setupPosKnob(startPosKnob, startPosLabel, "Start", 0.0);
        setupPosKnob(loopPosKnob,  loopPosLabel,  "Loop",  0.0);
        setupPosKnob(endPosKnob,   endPosLabel,   "End",   1.0);

        auto pushMarkers = [this](double s, double l, double e) {
            prevStartVal = s;
            prevLoopVal = l;
            prevEndVal = e;
            startPosKnob.setValue(s, juce::dontSendNotification);
            loopPosKnob.setValue(l, juce::dontSendNotification);
            endPosKnob.setValue(e, juce::dontSendNotification);
            processorRef.setSampleStartPos(s);
            processorRef.setSampleLoopPos(l);
            processorRef.setSampleEndPos(e);
            if (samplerWindow) {
                auto& wd = samplerWindow->getWaveformDisplay();
                wd.setStartPos(s);
                wd.setLoopPos(l);
                wd.setEndPos(e);
            }
        };

        auto scaleKnobDelta = [this](double raw, double prev) -> double {
            double delta = raw - prev;
            double viewSpan = samplerWindow ? samplerWindow->getWaveformDisplay().getViewSpan() : 1.0;
            return juce::jlimit(0.0, 1.0, prev + delta * viewSpan * 0.125);
        };

        startPosKnob.onValueChange = [this, pushMarkers, scaleKnobDelta] {
            double v = scaleKnobDelta(startPosKnob.getValue(), prevStartVal);
            if (markerLockToggle.getToggleState()) {
                double e = v + lockedEndOffset;
                double l = v + lockedLoopOffset;
                if (e > 1.0) { e = 1.0; v = e - lockedEndOffset; l = v + lockedLoopOffset; }
                if (v < 0.0) v = 0.0;
                pushMarkers(v, juce::jlimit(v, e, l), e);
                return;
            }
            double e = prevEndVal;
            constexpr double minGap = 0.0001;
            if (v > e - minGap) {
                e = v + minGap;
                if (e > 1.0) { e = 1.0; v = e - minGap; }
            }
            double l = juce::jlimit(v, e, prevLoopVal);
            pushMarkers(v, l, e);
        };
        endPosKnob.onValueChange = [this, pushMarkers, scaleKnobDelta] {
            double e = scaleKnobDelta(endPosKnob.getValue(), prevEndVal);
            if (markerLockToggle.getToggleState()) {
                double v = e - lockedEndOffset;
                double l = v + lockedLoopOffset;
                if (v < 0.0) { v = 0.0; e = v + lockedEndOffset; l = v + lockedLoopOffset; }
                if (e > 1.0) e = 1.0;
                pushMarkers(v, juce::jlimit(v, e, l), e);
                return;
            }
            double s = prevStartVal;
            constexpr double minGap = 0.0001;
            if (e < s + minGap) {
                s = e - minGap;
                if (s < 0.0) { s = 0.0; e = s + minGap; }
            }
            double l = juce::jlimit(s, e, prevLoopVal);
            pushMarkers(s, l, e);
        };
        loopPosKnob.onValueChange = [this, pushMarkers, scaleKnobDelta] {
            double l = scaleKnobDelta(loopPosKnob.getValue(), prevLoopVal);
            if (markerLockToggle.getToggleState()) {
                double v = l - lockedLoopOffset;
                double e = v + lockedEndOffset;
                if (v < 0.0) { v = 0.0; l = v + lockedLoopOffset; e = v + lockedEndOffset; }
                if (e > 1.0) { e = 1.0; v = e - lockedEndOffset; l = v + lockedLoopOffset; }
                pushMarkers(v, juce::jlimit(v, e, l), e);
                return;
            }
            double s = prevStartVal;
            double e = prevEndVal;
            l = juce::jlimit(s, e, l);
            pushMarkers(s, l, e);
        };

        // ADSR section
        panel.addAndMakeVisible(adsrDisplay);

        auto setupAdsrKnob = [&](juce::Slider& knob, juce::Label& label,
                                  const char* name, double init,
                                  double lo, double hi) {
            knob.setSliderStyle(juce::Slider::Rotary);
            knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
            knob.setRange(lo, hi, 0.01);
            knob.setValue(init, juce::dontSendNotification);
            panel.addAndMakeVisible(knob);
            label.setText(name, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setFont(juce::Font(10.0f));
            panel.addAndMakeVisible(label);
        };
        setupAdsrKnob(adsrAttackKnob,      adsrAttackLabel,      "Atk",  0.0,  0.0, 1.0);
        setupAdsrKnob(adsrDecayKnob,       adsrDecayLabel,       "Dec",  0.0,  0.0, 1.0);
        setupAdsrKnob(adsrSustainLvlKnob,  adsrSustainLvlLabel,  "Sus",  1.0,  0.0, 1.0);
        setupAdsrKnob(adsrSustainRateKnob, adsrSustainRateLabel,  "Rise/Fall", 0.0, -1.0, 1.0);

        auto sustainTint = juce::Colour(0xFFD49EBF);
        adsrSustainLvlKnob.setColour(juce::Slider::thumbColourId, sustainTint);
        adsrSustainRateKnob.setColour(juce::Slider::thumbColourId, sustainTint);
        setupAdsrKnob(adsrReleaseKnob,     adsrReleaseLabel,     "Rel",  0.0,  0.0, 1.0);

        adsrAttackKnob.onValueChange = [this] {
            processorRef.getAdsrAttack().store(
                static_cast<float>(adsrAttackKnob.getValue()), std::memory_order_relaxed);
            refreshAdsrDisplay();
        };
        adsrDecayKnob.onValueChange = [this] {
            processorRef.getAdsrDecay().store(
                static_cast<float>(adsrDecayKnob.getValue()), std::memory_order_relaxed);
            refreshAdsrDisplay();
        };
        adsrSustainLvlKnob.onValueChange = [this] {
            processorRef.getAdsrSustainLvl().store(
                static_cast<float>(adsrSustainLvlKnob.getValue()), std::memory_order_relaxed);
            refreshAdsrDisplay();
        };
        adsrSustainRateKnob.onValueChange = [this] {
            double v = adsrSustainRateKnob.getValue();
            if (std::abs(v) < 0.03)
            {
                v = 0.0;
                adsrSustainRateKnob.setValue(v, juce::dontSendNotification);
            }
            processorRef.getAdsrSustainRate().store(
                static_cast<float>(v), std::memory_order_relaxed);
            refreshAdsrDisplay();
        };
        adsrReleaseKnob.onValueChange = [this] {
            processorRef.getAdsrRelease().store(
                static_cast<float>(adsrReleaseKnob.getValue()), std::memory_order_relaxed);
            refreshAdsrDisplay();
        };

        panel.addAndMakeVisible(adsrAttackExpToggle);
        adsrAttackExpToggle.setClickingTogglesState(true);
        adsrAttackExpToggle.onClick = [this] {
            processorRef.getAdsrAttackExp().store(
                adsrAttackExpToggle.getToggleState(), std::memory_order_relaxed);
            refreshAdsrDisplay();
        };
        panel.addAndMakeVisible(adsrSustainExpToggle);
        adsrSustainExpToggle.setClickingTogglesState(true);
        adsrSustainExpToggle.onClick = [this] {
            processorRef.getAdsrSustainExp().store(
                adsrSustainExpToggle.getToggleState(), std::memory_order_relaxed);
            refreshAdsrDisplay();
        };
        panel.addAndMakeVisible(adsrReleaseExpToggle);
        adsrReleaseExpToggle.setClickingTogglesState(true);
        adsrReleaseExpToggle.setToggleState(false, juce::dontSendNotification);
        adsrReleaseExpToggle.onClick = [this] {
            processorRef.getAdsrReleaseExp().store(
                adsrReleaseExpToggle.getToggleState(), std::memory_order_relaxed);
            refreshAdsrDisplay();
        };

        refreshAdsrDisplay();

        // Volume L/R knobs — full signed PS1 range (Phase 34: signed volume).
        // -16384..+16383 maps to the SPU's -0x4000..+0x3FFF volume register.
        voiceVolLKnob.setSliderStyle(juce::Slider::Rotary);
        voiceVolLKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        voiceVolLKnob.setRange(-16384.0, 16383.0, 1.0);
        voiceVolLKnob.setValue(16383.0, juce::dontSendNotification);
        voiceVolLKnob.setTextValueSuffix(" L");
        voiceVolLKnob.onValueChange = [this] {
            int16_t v = static_cast<int16_t>(voiceVolLKnob.getValue());
            processorRef.setGuiVoiceVolL(v);
            // Phase-flip indicator: teal "INV" when negative
            phaseFlipLIndicator.setVisible(v < 0);
        };
        panel.addAndMakeVisible(voiceVolLKnob);
        voiceVolLLabel.setText("Vol L", juce::dontSendNotification);
        voiceVolLLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(voiceVolLLabel);

        voiceVolRKnob.setSliderStyle(juce::Slider::Rotary);
        voiceVolRKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        voiceVolRKnob.setRange(-16384.0, 16383.0, 1.0);
        voiceVolRKnob.setValue(16383.0, juce::dontSendNotification);
        voiceVolRKnob.setTextValueSuffix(" R");
        voiceVolRKnob.onValueChange = [this] {
            int16_t v = static_cast<int16_t>(voiceVolRKnob.getValue());
            processorRef.setGuiVoiceVolR(v);
            // Phase-flip indicator: teal "INV" when negative
            phaseFlipRIndicator.setVisible(v < 0);
        };
        panel.addAndMakeVisible(voiceVolRKnob);
        voiceVolRLabel.setText("Vol R", juce::dontSendNotification);
        voiceVolRLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(voiceVolRLabel);

        // Phase-flip indicators — teal "INV" text next to each volume knob
        auto tealColour = juce::Colour(0xFF5B9279);
        phaseFlipLIndicator.setText("INV", juce::dontSendNotification);
        phaseFlipLIndicator.setJustificationType(juce::Justification::centred);
        phaseFlipLIndicator.setColour(juce::Label::textColourId, tealColour);
        phaseFlipLIndicator.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        phaseFlipLIndicator.setVisible(false);
        panel.addAndMakeVisible(phaseFlipLIndicator);

        phaseFlipRIndicator.setText("INV", juce::dontSendNotification);
        phaseFlipRIndicator.setJustificationType(juce::Justification::centred);
        phaseFlipRIndicator.setColour(juce::Label::textColourId, tealColour);
        phaseFlipRIndicator.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        phaseFlipRIndicator.setVisible(false);
        panel.addAndMakeVisible(phaseFlipRIndicator);

        // Sampler mixer knobs -- own bus in the master mixer.
        samplerLevelKnob.setSliderStyle(juce::Slider::Rotary);
        samplerLevelKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        samplerLevelKnob.setRange(0.0, 1.0, 0.01);
        samplerLevelKnob.setValue(1.0, juce::dontSendNotification);
        samplerLevelKnob.onValueChange = [this] {
            processorRef.getSamplerFader().store(
                static_cast<float>(samplerLevelKnob.getValue()),
                std::memory_order_relaxed);
        };
        addAndMakeVisible(samplerLevelKnob);
        samplerLevelLabel.setText("Sampler", juce::dontSendNotification);
        samplerLevelLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(samplerLevelLabel);

        samplerSendKnob.setSliderStyle(juce::Slider::Rotary);
        samplerSendKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        samplerSendKnob.setRange(0.0, 1.0, 0.01);
        samplerSendKnob.setValue(1.0, juce::dontSendNotification);
        samplerSendKnob.onValueChange = [this] {
            processorRef.getSamplerSend().store(
                static_cast<float>(samplerSendKnob.getValue()),
                std::memory_order_relaxed);
        };
        addAndMakeVisible(samplerSendKnob);
        samplerSendLabel.setText("Samp Send", juce::dontSendNotification);
        samplerSendLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(samplerSendLabel);

        samplerDriveKnob.setSliderStyle(juce::Slider::Rotary);
        samplerDriveKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        samplerDriveKnob.setRange(0.0, 16.0, 0.01);
        samplerDriveKnob.setValue(1.0, juce::dontSendNotification);
        samplerDriveKnob.onValueChange = [this] {
            processorRef.getSamplerDrive().store(
                static_cast<float>(samplerDriveKnob.getValue()),
                std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(samplerDriveKnob);
        samplerDriveLabel.setText("Samp Drive", juce::dontSendNotification);
        samplerDriveLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(samplerDriveLabel);
    }

    presetLabel.setText("Preset:", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(presetLabel);

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

                processorRef.loadPresetFromString(text);
            });
    };

    // Per-tick EXPORT / LOAD wired through MorphPanel (see callbacks below).
    morphPanel.onExportSlotClicked = [this](int slot) { exportSingleSlot(slot); };
    morphPanel.onLoadSlotClicked   = [this](int slot) { loadSingleSlot(slot); };

    // ---- ZONE 1: Toolbar controls ----

    // Input Gain rotary knob (renamed from "Input").
    addAndMakeVisible(inputLevelKnob);
    inputLevelKnob.setSliderStyle(juce::Slider::Rotary);
    inputLevelKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    inputLevelKnob.setRange(0.0, 16.0, 0.01);
    inputLevelKnob.setValue(0.50, juce::dontSendNotification);
    inputLevelKnob.setSkewFactorFromMidPoint(1.0);  // unity at the knob midpoint for usable drive feel

    // Phase 24: wire through AudioParameterFloat gesture API for host automation.
    inputLevelKnob.onDragStart = [this] {
        processorRef.getParamInputGain()->beginChangeGesture();
    };
    inputLevelKnob.onDragEnd = [this] {
        processorRef.getParamInputGain()->endChangeGesture();
    };
    inputLevelKnob.onValueChange = [this] {
        auto* p = processorRef.getParamInputGain();
        p->setValueNotifyingHost(
            p->getNormalisableRange().convertTo0to1(
                static_cast<float>(inputLevelKnob.getValue())));
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
    adpcmSendKnob.onDragStart = [this] {
        processorRef.getParamAdpcmSend()->beginChangeGesture();
    };
    adpcmSendKnob.onDragEnd = [this] {
        processorRef.getParamAdpcmSend()->endChangeGesture();
    };
    adpcmSendKnob.onValueChange = [this] {
        processorRef.getParamAdpcmSend()->setValueNotifyingHost(
            static_cast<float>(adpcmSendKnob.getValue()));
    };
    addAndMakeVisible(adpcmSendLabel);
    adpcmSendLabel.setText("Input Send", juce::dontSendNotification);
    adpcmSendLabel.setJustificationType(juce::Justification::centred);

    // Reverb Sends: Dry Input Send knob
    addAndMakeVisible(drySendKnob);
    drySendKnob.setSliderStyle(juce::Slider::Rotary);
    drySendKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    drySendKnob.setRange(0.0, 1.0, 0.01);
    drySendKnob.setValue(0.0, juce::dontSendNotification);
    drySendKnob.onDragStart = [this] {
        processorRef.getParamDrySend()->beginChangeGesture();
    };
    drySendKnob.onDragEnd = [this] {
        processorRef.getParamDrySend()->endChangeGesture();
    };
    drySendKnob.onValueChange = [this] {
        processorRef.getParamDrySend()->setValueNotifyingHost(
            static_cast<float>(drySendKnob.getValue()));
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
    dryKnob.onDragStart = [this] {
        processorRef.getParamDryLevel()->beginChangeGesture();
    };
    dryKnob.onDragEnd = [this] {
        processorRef.getParamDryLevel()->endChangeGesture();
    };
    dryKnob.onValueChange = [this] {
        processorRef.getParamDryLevel()->setValueNotifyingHost(
            static_cast<float>(dryKnob.getValue()));
    };
    addAndMakeVisible(dryKnobLabel);
    dryKnobLabel.setText("Dry", juce::dontSendNotification);
    dryKnobLabel.setJustificationType(juce::Justification::centred);

    // ADPCM level knob
    addAndMakeVisible(adpcmKnob);
    adpcmKnob.setSliderStyle(juce::Slider::Rotary);
    adpcmKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    adpcmKnob.setRange(0.0, 1.0, 0.01);
    adpcmKnob.setValue(0.0, juce::dontSendNotification);
    adpcmKnob.onDragStart = [this] {
        processorRef.getParamAdpcmLevel()->beginChangeGesture();
    };
    adpcmKnob.onDragEnd = [this] {
        processorRef.getParamAdpcmLevel()->endChangeGesture();
    };
    adpcmKnob.onValueChange = [this] {
        processorRef.getParamAdpcmLevel()->setValueNotifyingHost(
            static_cast<float>(adpcmKnob.getValue()));
    };
    addAndMakeVisible(adpcmKnobLabel);
    adpcmKnobLabel.setText("Input", juce::dontSendNotification);
    adpcmKnobLabel.setJustificationType(juce::Justification::centred);

    // Reverb level knob
    addAndMakeVisible(reverbKnob);
    reverbKnob.setSliderStyle(juce::Slider::Rotary);
    reverbKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    reverbKnob.setRange(0.0, 1.0, 0.01);
    reverbKnob.setValue(1.0, juce::dontSendNotification);
    reverbKnob.onDragStart = [this] {
        processorRef.getParamReverbLevel()->beginChangeGesture();
    };
    reverbKnob.onDragEnd = [this] {
        processorRef.getParamReverbLevel()->endChangeGesture();
    };
    reverbKnob.onValueChange = [this] {
        processorRef.getParamReverbLevel()->setValueNotifyingHost(
            static_cast<float>(reverbKnob.getValue()));
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

    // ADPCM voice path controls
    addAndMakeVisible(gaussToggle);
    gaussToggle.setClickingTogglesState(true);
    gaussToggle.setToggleState(false, juce::dontSendNotification);
    gaussToggle.onClick = [this] {
        processorRef.getGaussEnabled().store(
            gaussToggle.getToggleState(),
            std::memory_order_relaxed);
    };

    addAndMakeVisible(aaFilterToggle);
    aaFilterToggle.setClickingTogglesState(true);
    aaFilterToggle.setToggleState(true, juce::dontSendNotification);
    aaFilterToggle.onClick = [this] {
        processorRef.getAAFilterEnabled().store(
            aaFilterToggle.getToggleState(),
            std::memory_order_relaxed);
    };

    voicePitchKnob.setSliderStyle(juce::Slider::Rotary);
    voicePitchKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    voicePitchKnob.setRange(4000.0, 44100.0, 1.0);
    voicePitchKnob.setValue(22050.0, juce::dontSendNotification);
    voicePitchKnob.setSkewFactorFromMidPoint(11025.0);
    voicePitchKnob.setTextValueSuffix(" Hz");
    voicePitchKnob.onValueChange = [this] {
        double hz = voicePitchKnob.getValue();
        int pitch = (int)(hz / 44100.0 * 0x1000 + 0.5);
        if (pitch < 0x005C) pitch = 0x005C;
        if (pitch > 0x1000) pitch = 0x1000;
        processorRef.getVoicePitch().store(pitch, std::memory_order_relaxed);
    };
    addAndMakeVisible(voicePitchKnob);

    voicePitchLabel.setText("Input Rate", juce::dontSendNotification);
    voicePitchLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(voicePitchLabel);

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
    saveButton.onClick = [this] { exitAdvancedView(true); };
    // addChildComponent (not addAndMakeVisible) so the button starts hidden
    // and only appears when entering Advanced view.
    addChildComponent(saveButton);

    revertButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE8736E)); // PS1 coral
    revertButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    revertButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    revertButton.onClick = [this] { exitAdvancedView(false); };
    addChildComponent(revertButton);

    // Sync slider positions to the initial preset (Hall).
    registerPanel.updateFromShadows();
    syncMixerKnobsFromProcessor();

    // 30 Hz timer to detect file-preset load completion and shadow syncs.
    lastFilePresetCount = processorRef.getFilePresetAppliedCount();
    lastShadowSyncCount = processorRef.getShadowSyncCount();
    startTimerHz(30);

    captureBaseline();

    setResizable(false, false);
    setSize(900, 950);
}

SPU94AudioProcessorEditor::~SPU94AudioProcessorEditor()
{
    stopTimer();
}

void SPU94AudioProcessorEditor::timerCallback()
{
#if JUCE_LINUX
    if (!windowSizeFixed)
    {
        windowSizeFixed = true;
        if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        {
            if (auto* topLevel = getTopLevelComponent())
            {
                if (topLevel->getWidth() < 800 || topLevel->getHeight() < 800)
                {
                    setSize(901, 951);
                    setSize(900, 950);
                }
            }
            if (auto* topLevel = getTopLevelComponent())
            {
                auto display = juce::Desktop::getInstance().getDisplays()
                                   .getDisplayForPoint(topLevel->getScreenBounds().getCentre());
                if (display != nullptr)
                {
                    auto area = display->userArea;
                    int mainX = area.getRight() - topLevel->getWidth() - 20;
                    int mainY = area.getY() + 40;
                    topLevel->setTopLeftPosition(mainX, mainY);
                }
            }
            if (samplerWindow)
            {
                samplerWindow->ensureMinimumSize();
                samplerWindow->positionLeftOf(this);
            }
        }
    }
#endif

    // Detect file-preset load completion (Save Preset / Load Preset buttons).
    const int fileCount = processorRef.getFilePresetAppliedCount();
    if (fileCount != lastFilePresetCount)
    {
        lastFilePresetCount = fileCount;
        syncMixerKnobsFromProcessor();
        captureBaseline();
    }

    // Detect shadow syncs from the audio thread. Fires on every morph
    // re-apply (knob movement, LOAD, SAVE/REVERT, full preset load) and
    // on view switches. Refreshing both the slider positions AND the
    // MorphPanel tick colors here keeps the GUI honest about what is
    // actually playing -- and crucially, makes a slot fill/unfill visible
    // immediately on the encoder.
    const int shadowCount = processorRef.getShadowSyncCount();
    if (shadowCount != lastShadowSyncCount)
    {
        lastShadowSyncCount = shadowCount;
        registerPanel.updateFromShadows();
        morphPanel.repaint();  // tick colors (filled vs empty) re-render
    }

    // Sync morph panel knob position when visible (Phase 17).
    if (morphPanel.isVisible())
    {
        morphPanel.updateKnobPosition();
    }

    // Phase 24: param->GUI sync for host automation playback. When the host
    // writes automation, the AudioParameterFloat values change but the GUI
    // sliders don't know. Poll each param and update the slider if it drifted
    // beyond a small epsilon. dontSendNotification avoids feedback loops.
    {
        constexpr float eps = 0.001f;
        auto syncKnob = [eps](juce::Slider& knob, float paramValue) {
            if (std::abs(static_cast<float>(knob.getValue()) - paramValue) > eps)
                knob.setValue(static_cast<double>(paramValue), juce::dontSendNotification);
        };
        syncKnob(inputLevelKnob,  processorRef.getParamInputGain()->get());
        syncKnob(adpcmSendKnob,   processorRef.getParamAdpcmSend()->get());
        syncKnob(drySendKnob,     processorRef.getParamDrySend()->get());
        syncKnob(dryKnob,         processorRef.getParamDryLevel()->get());
        syncKnob(adpcmKnob,      processorRef.getParamAdpcmLevel()->get());
        syncKnob(reverbKnob,      processorRef.getParamReverbLevel()->get());
    }

    // Phase 31: Update voice sample status label + waveform (standalone only).
    if (processorRef.getVoiceSampleLoaded().load(std::memory_order_acquire))
    {
        voiceSampleLabel.setText(
            processorRef.getVoiceSampleName() + " " +
            juce::String(processorRef.getVoiceSampleBytes()) + "B",
            juce::dontSendNotification);

        uint32_t used = processorRef.getRamUsed();
        float kb = used / 1024.0f;
        ramMeterLabel.setText(
            juce::String(kb, 1) + " / 512 KB",
            juce::dontSendNotification);

        if (samplerWindow)
        {
            if (processorRef.getWaveformFrames() != lastWaveformFrames)
            {
                lastWaveformFrames = processorRef.getWaveformFrames();
                samplerWindow->getWaveformDisplay().setSample(
                    processorRef.getWaveformData().data(),
                    processorRef.getWaveformFrames(), 44100.0);
            }
            samplerWindow->getWaveformDisplay().setPlayheadPos(
                processorRef.getVoicePlaybackPos());
            double ws = samplerWindow->getWaveformDisplay().getStartPos();
            double we = samplerWindow->getWaveformDisplay().getEndPos();
            double wl = samplerWindow->getWaveformDisplay().getLoopPos();
            processorRef.setSampleStartPos(ws);
            processorRef.setSampleEndPos(we);
            processorRef.setSampleLoopPos(wl);
            startPosKnob.setValue(ws, juce::dontSendNotification);
            endPosKnob.setValue(we, juce::dontSendNotification);
            loopPosKnob.setValue(wl, juce::dontSendNotification);
        }
    }

}

void SPU94AudioProcessorEditor::refreshAdsrDisplay()
{
    float atk = static_cast<float>(adsrAttackKnob.getValue());
    float dec = static_cast<float>(adsrDecayKnob.getValue());
    float sl  = static_cast<float>(adsrSustainLvlKnob.getValue());
    float sr  = static_cast<float>(adsrSustainRateKnob.getValue());
    float rel = static_cast<float>(adsrReleaseKnob.getValue());

    auto powerMap = [](float knob, float maxShift) -> uint8_t {
        if (knob <= 0.0f) return 0;
        if (knob >= 1.0f) return static_cast<uint8_t>(maxShift);
        float s = maxShift * std::pow(knob, 0.55f);
        int v = static_cast<int>(s + 0.5f);
        if (v > static_cast<int>(maxShift)) v = static_cast<int>(maxShift);
        return static_cast<uint8_t>(v);
    };

    uint8_t aShift = powerMap(atk, 20.0f);
    uint8_t aExp   = adsrAttackExpToggle.getToggleState() ? 1 : 0;
    uint8_t dShift = powerMap(dec, 15.0f);
    uint8_t sLevel = static_cast<uint8_t>(sl * 15.0f + 0.5f);
    uint8_t sDir   = sr < 0.0f ? 1 : 0;
    float   mag    = sr < 0.0f ? -sr : sr;
    uint8_t sShift;
    if (mag < 0.01f) {
        sShift = 31;
    } else {
        sShift = powerMap(1.0f - mag, 20.0f);
    }
    uint8_t sExp   = adsrSustainExpToggle.getToggleState() ? 1 : 0;
    uint8_t rShift = powerMap(rel, 20.0f);
    uint8_t rExp   = adsrReleaseExpToggle.getToggleState() ? 1 : 0;

    adsrDisplay.update(aShift, aExp, dShift, sLevel, sShift, sExp, sDir, rShift, rExp,
                       atk, dec, rel);
}

void SPU94AudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &triggerVoiceButton)
    {
        if (latchToggle.getToggleState()) {
            if (latchActive) {
                processorRef.stopVoice();
                latchActive = false;
            } else {
                uint16_t pitch = static_cast<uint16_t>(voiceEnginePitchKnob.getValue());
                processorRef.triggerVoice(pitch);
                latchActive = true;
            }
        } else {
            uint16_t pitch = static_cast<uint16_t>(voiceEnginePitchKnob.getValue());
            processorRef.triggerVoice(pitch);
        }
    }
}

void SPU94AudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    if (e.eventComponent == &triggerVoiceButton && !latchToggle.getToggleState())
        processorRef.stopVoice();
}

void SPU94AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);

    // Horizontal separator line above combined zone
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine(getHeight() - 90, 10.0f, static_cast<float>(getWidth() - 10));

    g.setFont(12.0f);
    g.setColour(juce::Colours::white);
}

void SPU94AudioProcessorEditor::resized()
{
    const int w = getWidth();

    // ---- ZONE 1: Toolbar (y=10, h=60) ----

    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        // WAV loader buttons (stacked vertically, left-aligned)
        loadButton.setBounds(10, 8, 80, 28);
        playButton.setBounds(10, 40, 80, 28);
        stopButton.setBounds(10, 72, 80, 28);

        // Voice engine controls are in the sampler window — lay them out there.
        if (samplerWindow)
        {
            loadSampleButton.setBounds(10, 10, 110, 30);
            triggerVoiceButton.setBounds(125, 10, 70, 30);
            stopVoiceButton.setBounds(200, 10, 90, 30);
            encodeRateLabel.setBounds(295, 10, 95, 12);
            encodeRateBox.setBounds(295, 22, 95, 22);
            voiceEnginePitchLabel.setBounds(10, 50, 80, 16);
            voiceEnginePitchKnob.setBounds(10, 64, 80, 54);
            samplerDriveLabel.setBounds(95, 50, 80, 16);
            samplerDriveKnob.setBounds(95, 64, 80, 54);
            voiceSampleLabel.setBounds(180, 50, 110, 30);
            loopToggle.setBounds(300, 50, 85, 26);
            latchToggle.setBounds(300, 76, 65, 26);
            markerLockToggle.setBounds(300, 102, 65, 26);
            samplerAAToggle.setBounds(180, 82, 70, 26);
            ramMeterLabel.setBounds(10, 128, 380, 12);
            samplerWindow->getWaveformDisplay().setBounds(10, 142, 380, 108);
            startPosLabel.setBounds(15, 252, 70, 14);
            startPosKnob.setBounds(15, 264, 70, 54);
            loopPosLabel.setBounds(160, 252, 70, 14);
            loopPosKnob.setBounds(160, 264, 70, 54);
            endPosLabel.setBounds(320, 252, 70, 14);
            endPosKnob.setBounds(320, 264, 70, 54);

            // Volume L/R section (Phase 34: signed volume)
            constexpr int voly = 322;
            voiceVolLLabel.setBounds(60, voly, 80, 14);
            voiceVolLKnob.setBounds(60, voly + 14, 80, 54);
            phaseFlipLIndicator.setBounds(60, voly + 68, 80, 14);
            voiceVolRLabel.setBounds(250, voly, 80, 14);
            voiceVolRKnob.setBounds(250, voly + 14, 80, 54);
            phaseFlipRIndicator.setBounds(250, voly + 68, 80, 14);

            // ADSR section (shifted down 50px for volume knobs)
            constexpr int adsrDispY = 393;
            adsrDisplay.setBounds(10, adsrDispY, 380, 55);

            constexpr int aky = 453, akw = 65, akh = 54;
            adsrAttackLabel.setBounds(15, aky, akw, 12);
            adsrAttackKnob.setBounds(15, aky + 12, akw, akh);
            adsrDecayLabel.setBounds(91, aky, akw, 12);
            adsrDecayKnob.setBounds(91, aky + 12, akw, akh);
            adsrSustainLvlLabel.setBounds(167, aky, akw, 12);
            adsrSustainLvlKnob.setBounds(167, aky + 12, akw, akh);
            adsrSustainRateLabel.setBounds(243, aky, akw, 12);
            adsrSustainRateKnob.setBounds(243, aky + 12, akw, akh);
            adsrReleaseLabel.setBounds(319, aky, akw, 12);
            adsrReleaseKnob.setBounds(319, aky + 12, akw, akh);

            constexpr int tgy = aky + 12 + akh + 2;
            adsrAttackExpToggle.setBounds(15, tgy, akw, 18);
            adsrSustainExpToggle.setBounds(243, tgy, akw, 18);
            adsrReleaseExpToggle.setBounds(319, tgy, akw, 18);
        }
    }

    // Input controls: Input Gain, Anti-Aliasing, Input Rate, Gauss, Input Send
    // Centered as a group between Stop button (ends x=260) and Dry Send (starts x=770).
    constexpr int groupLeft = 307;
    inputLevelLabel.setBounds(groupLeft, 2, 70, 16);
    inputLevelKnob.setBounds(groupLeft, 16, 70, 54);
    aaFilterToggle.setBounds(groupLeft + 75, 24, 120, 30);
    voicePitchLabel.setBounds(groupLeft + 190, 2, 80, 16);
    voicePitchKnob.setBounds(groupLeft + 190, 16, 80, 54);
    gaussToggle.setBounds(groupLeft + 275, 24, 65, 30);
    adpcmSendLabel.setBounds(groupLeft + 345, 2, 70, 16);
    adpcmSendKnob.setBounds(groupLeft + 345, 16, 70, 54);

    // Send knobs: Dry Send, Samp Send
    drySendLabel.setBounds(770, 2, 60, 16);
    drySendKnob.setBounds(770, 16, 60, 54);
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        samplerSendLabel.setBounds(835, 2, 60, 16);
        samplerSendKnob.setBounds(835, 16, 60, 54);
    }

    // ---- ZONE 2: Register panel in scrollable viewport (fills middle) ----
    const int bottomZoneHeight = 80;
    // Standalone voice panel extends to y=105; push Zone 2 down to avoid overlap.
    const int registerTop = (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone) ? 110 : 75;
    const int registerBottom = getHeight() - bottomZoneHeight - 5;
    const int viewportW = w - 20;
    const int viewportH = registerBottom - registerTop;
    registerViewport.setBounds(10, registerTop, viewportW, viewportH);
    registerPanel.setSize(viewportW - registerViewport.getScrollBarThickness(),
                          registerPanel.getPreferredHeight());

    // Morph panel occupies the same Zone 2 bounds (D-01)
    morphPanel.setBounds(10, registerTop, viewportW, viewportH);

    // Preset section — stacked below User Slot in MorphPanel (matching style).
    // Mirrors MorphPanel's slot layout: same btnW/btnH/gap/margin, same x column.
    {
        constexpr int slotBtnW = 80, slotBtnH = 28, slotBtnGap = 4, slotMargin = 12;
        const int slotX = 10 + viewportW - slotBtnW - slotMargin;
        const int morphStartY = (viewportH - (280 + 24 + 8)) / 2;
        const int userSlotBottom = registerTop + morphStartY + slotMargin + 18
                                   + (slotBtnH + slotBtnGap) * 2 + slotBtnH;
        int py = userSlotBottom + slotBtnGap + 8;
        presetLabel.setBounds(slotX, py, slotBtnW, 16); py += 18;
        savePresetButton.setBounds(slotX, py, slotBtnW, slotBtnH); py += slotBtnH + slotBtnGap;
        loadPresetButton.setBounds(slotX, py, slotBtnW, slotBtnH);
        presetLabel.toFront(false);
        savePresetButton.toFront(false);
        loadPresetButton.toFront(false);
    }

    // ---- ZONE 3+4: Combined mixer + DAC (single bottom row) ----
    const int bottomY = registerBottom + 5;
    // DRY section: Dry Level knob
    dryKnobLabel.setBounds(10, bottomY, 80, 16);
    dryKnob.setBounds(10, bottomY + 14, 80, 54);
    // ADPCM section: Input | Sampler | Reverb
    adpcmKnobLabel.setBounds(120, bottomY, 80, 16);
    adpcmKnob.setBounds(120, bottomY + 14, 80, 54);
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        samplerLevelLabel.setBounds(220, bottomY, 80, 16);
        samplerLevelKnob.setBounds(220, bottomY + 14, 80, 54);
    }
    const int reverbX = (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
                        ? 320 : 220;
    reverbKnobLabel.setBounds(reverbX, bottomY, 80, 16);
    reverbKnob.setBounds(reverbX, bottomY + 14, 80, 54);
    // Toggles: Latency Comp + DAC section
    const int toggleX = reverbX + 100;
    latencyCompToggle.setBounds(toggleX, bottomY + 15, 90, 30);
    dacToggle.setBounds(toggleX + 95, bottomY + 15, 50, 30);
    dacFirToggle.setBounds(toggleX + 145, bottomY + 15, 45, 30);
    dacNoiseToggle.setBounds(toggleX + 190, bottomY + 15, 55, 30);
    dacOversampleToggle.setBounds(toggleX + 250, bottomY + 15, 45, 30);

    // SAVE / REVERT hover at the TOP of the Advanced viewport (matching
    // where the master-branch Abort button lived). Floats over the top
    // of the register sliders so it's always visible during edit.
    constexpr int savW = 90, savH = 30, savGap = 10;
    const int pairW = savW * 2 + savGap;
    const int pairX = (w - pairW) / 2;
    const int savY = registerTop + 5;
    saveButton.setBounds(pairX,                    savY, savW, savH);
    revertButton.setBounds(pairX + savW + savGap,  savY, savW, savH);
}

void SPU94AudioProcessorEditor::showPresetNamePrompt()
{
    juce::String defaultName = "preset";

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
                chosen.replaceWithText(text);
        });
}

void SPU94AudioProcessorEditor::captureBaseline()
{
    for (size_t i = 0; i < SPU94_REG__COUNT; ++i)
        baseline.registers[i] = processorRef.getRegisterBridge().getShadowValue(i);
    baseline.inputGain = processorRef.getParamInputGain()->get();
    baseline.dry = processorRef.getParamDryLevel()->get();
    baseline.adpcm = processorRef.getParamAdpcmLevel()->get();
    baseline.reverb = processorRef.getParamReverbLevel()->get();
    baseline.adpcmSend = processorRef.getParamAdpcmSend()->get();
    baseline.drySend = processorRef.getParamDrySend()->get();
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
    if (!feq(p.getParamInputGain()->get(), baseline.inputGain)) return true;
    if (!feq(p.getParamDryLevel()->get(), baseline.dry)) return true;
    if (!feq(p.getParamAdpcmLevel()->get(), baseline.adpcm)) return true;
    if (!feq(p.getParamReverbLevel()->get(), baseline.reverb)) return true;
    if (!feq(p.getParamAdpcmSend()->get(), baseline.adpcmSend)) return true;
    if (!feq(p.getParamDrySend()->get(), baseline.drySend)) return true;
    if (p.getLatencyCompEnabled().load(std::memory_order_relaxed) != baseline.latencyComp) return true;
    if (p.getDacEnabled().load(std::memory_order_relaxed) != baseline.dac) return true;
    if (p.getDacFirEnabled().load(std::memory_order_relaxed) != baseline.dacFir) return true;
    if (p.getDacNoiseEnabled().load(std::memory_order_relaxed) != baseline.dacNoise) return true;
    if (p.getDacTrueOversample().load(std::memory_order_relaxed) != baseline.dacOversample) return true;
    return false;
}

void SPU94AudioProcessorEditor::syncMixerKnobsFromProcessor()
{
    inputLevelKnob.setValue(
        static_cast<double>(processorRef.getParamInputGain()->get()),
        juce::dontSendNotification);
    dryKnob.setValue(
        static_cast<double>(processorRef.getParamDryLevel()->get()),
        juce::dontSendNotification);
    adpcmKnob.setValue(
        static_cast<double>(processorRef.getParamAdpcmLevel()->get()),
        juce::dontSendNotification);
    reverbKnob.setValue(
        static_cast<double>(processorRef.getParamReverbLevel()->get()),
        juce::dontSendNotification);
    adpcmSendKnob.setValue(
        static_cast<double>(processorRef.getParamAdpcmSend()->get()),
        juce::dontSendNotification);
    drySendKnob.setValue(
        static_cast<double>(processorRef.getParamDrySend()->get()),
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
    gaussToggle.setToggleState(
        processorRef.getGaussEnabled().load(std::memory_order_relaxed),
        juce::dontSendNotification);
    aaFilterToggle.setToggleState(
        processorRef.getAAFilterEnabled().load(std::memory_order_relaxed),
        juce::dontSendNotification);
    {
        int pitch = processorRef.getVoicePitch().load(std::memory_order_relaxed);
        double hz = (double)pitch / (double)0x1000 * 44100.0;
        voicePitchKnob.setValue(hz, juce::dontSendNotification);
    }
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

void SPU94AudioProcessorEditor::exportSingleSlot(int slot)
{
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return;
    if (!processorRef.isUserSlotFilled(slot)) return;  // nothing to export

    auto suggestedFile = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory)
        .getChildFile(juce::String("slot_") + juce::String(slot + 1) + ".spu94");
    if (!suggestedFile.exists()) suggestedFile.create();

    fileChooser = std::make_unique<juce::FileChooser>(
        "Export Slot",
        suggestedFile,
        "*.spu94");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this, slot, suggestedFile](const juce::FileChooser& fc) {
            auto chosen = fc.getResult();
            if (suggestedFile.getSize() == 0 && suggestedFile != chosen)
                suggestedFile.deleteFile();
            if (chosen == juce::File()) return;
            if (chosen.getFileExtension().isEmpty())
                chosen = chosen.withFileExtension("spu94");
            auto name = chosen.getFileNameWithoutExtension();
            auto text = processorRef.exportUserSlotToString(
                slot, name, juce::String());
            if (text.isNotEmpty())
                chosen.replaceWithText(text);
        });
}

void SPU94AudioProcessorEditor::loadSingleSlot(int slot)
{
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return;

    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Slot",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.spu94");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this, slot](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (!result.existsAsFile()) return;
            auto text = result.loadFileAsString();
            if (text.isEmpty()) return;
            processorRef.loadUserSlotFromString(slot, text);
        });
}

void SPU94AudioProcessorEditor::exitAdvancedView(bool save)
{
    const int slot = processorRef.getCurrentEditingSlot().load(std::memory_order_relaxed);
    if (slot >= 0) {
        if (save) {
            processorRef.saveUserSlot(slot);
        } else {
            // REVERT semantics: not just "discard in-session edits" but
            // "clear this slot entirely" -- emergency reset for when the
            // user wants out. The morph re-apply below at the (now-empty)
            // slot's midpoint writes the v1.5 transparent-interp Sony[k] /
            // Sony[k+1] state back into the engine, and the tick reverts
            // to its empty visual.
            processorRef.clearUserSlot(slot);
        }
    }

    processorRef.getCurrentEditingSlot().store(-1, std::memory_order_relaxed);

    // Hand registers back to the morph engine and force re-apply at the
    // current (still-midpoint) position. ORDER MATTERS: set the reapply flag
    // FIRST, then flip morphActive=true. Otherwise an audio block scheduled
    // between the two stores would see morphActive=true && forceReapply=false
    // && pos==lastMorphPos and skip the re-apply entirely, leaving engines[0]
    // at the user's pre-REVERT edited register values.
    processorRef.requestMorphReapply();
    processorRef.getMorphActive().store(true, std::memory_order_release);

    saveButton.setVisible(false);
    revertButton.setVisible(false);
    registerViewport.setVisible(false);
    morphPanel.setVisible(true);
}
