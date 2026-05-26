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
        voiceEnginePitchKnob.setValue(2048.0, juce::dontSendNotification);
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
            if (idx >= 0 && idx < 4) {
                processorRef.setEncodeRate(rates[idx]);
                int pitch = static_cast<int>(0x1000 * (rates[idx] / 44100.0) + 0.5);
                voiceEnginePitchKnob.setValue(pitch, juce::sendNotification);
            }
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

        // Pan + Level + INV controls (Phase 39: replaces raw Vol L/R).
        // Pan: rotary encoder with center detent at 0.
        voicePanKnob.setSliderStyle(juce::Slider::Rotary);
        voicePanKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        voicePanKnob.setRange(-100.0, 100.0, 1.0);
        voicePanKnob.setValue(0.0, juce::dontSendNotification);
        voicePanKnob.setDoubleClickReturnValue(true, 0.0);
        voicePanKnob.onValueChange = [this] { updateVoiceVolumes(); };
        panel.addAndMakeVisible(voicePanKnob);
        voicePanLabel.setText("Pan", juce::dontSendNotification);
        voicePanLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(voicePanLabel);

        // Level: vertical fader, like a channel fader on a mixer.
        voiceLevelKnob.setSliderStyle(juce::Slider::LinearVertical);
        voiceLevelKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        voiceLevelKnob.setRange(0.0, 100.0, 1.0);
        voiceLevelKnob.setValue(100.0, juce::dontSendNotification);
        voiceLevelKnob.setTextValueSuffix("%");
        voiceLevelKnob.setDoubleClickReturnValue(true, 100.0);
        voiceLevelKnob.onValueChange = [this] { updateVoiceVolumes(); };
        panel.addAndMakeVisible(voiceLevelKnob);
        voiceLevelLabel.setText("Level", juce::dontSendNotification);
        voiceLevelLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(voiceLevelLabel);

        // INV toggle — flips sign on both vol_l and vol_r.
        voiceInvToggle.setClickingTogglesState(true);
        voiceInvToggle.setToggleState(false, juce::dontSendNotification);
        voiceInvToggle.onClick = [this] { updateVoiceVolumes(); };
        panel.addAndMakeVisible(voiceInvToggle);

        // INV indicator — teal "INV" text visible when inverted.
        auto tealColour = juce::Colour(0xFF5B9279);
        voiceInvIndicator.setText("INV", juce::dontSendNotification);
        voiceInvIndicator.setJustificationType(juce::Justification::centred);
        voiceInvIndicator.setColour(juce::Label::textColourId, tealColour);
        voiceInvIndicator.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        voiceInvIndicator.setVisible(false);
        panel.addAndMakeVisible(voiceInvIndicator);

        // Set initial volumes (pan center, level 100%, INV off → 0x3FFF both channels).
        updateVoiceVolumes();

        // NON toggle — replaces ADPCM output with noise generator (Phase 40).
        voiceNonToggle.setClickingTogglesState(true);
        voiceNonToggle.setToggleState(false, juce::dontSendNotification);
        voiceNonToggle.onClick = [this] {
            processorRef.getGuiVoiceNon().store(
                voiceNonToggle.getToggleState(), std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(voiceNonToggle);

        // PMON toggle — pitch modulation from previous voice (Phase 40).
        voicePmonToggle.setClickingTogglesState(true);
        voicePmonToggle.setToggleState(false, juce::dontSendNotification);
        voicePmonToggle.onClick = [this] {
            processorRef.getGuiVoicePmon().store(
                voicePmonToggle.getToggleState(), std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(voicePmonToggle);

        // Noise Color knob — controls noise generator shift (0-15)
        noiseColorKnob.setSliderStyle(juce::Slider::Rotary);
        noiseColorKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        noiseColorKnob.setRange(0.0, 15.0, 1.0);
        noiseColorKnob.setValue(10.0, juce::dontSendNotification);
        noiseColorKnob.onValueChange = [this] {
            processorRef.getNoiseShift().store(
                static_cast<int>(noiseColorKnob.getValue()), std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(noiseColorKnob);
        noiseColorLabel.setText("Noise Color", juce::dontSendNotification);
        noiseColorLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(noiseColorLabel);

        // Voice Mode group label
        voiceModeLabel.setText("Voice Mode", juce::dontSendNotification);
        voiceModeLabel.setJustificationType(juce::Justification::centredLeft);
        voiceModeLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFD0D0D0));
        voiceModeLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        panel.addAndMakeVisible(voiceModeLabel);

        // VCA Ramp section (Phase 41: volume sweep GUI surface)
        rampSectionLabel.setText("VCA Ramp", juce::dontSendNotification);
        rampSectionLabel.setJustificationType(juce::Justification::centredLeft);
        rampSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFD0D0D0));
        rampSectionLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        panel.addAndMakeVisible(rampSectionLabel);

        // Direction button: toggles between "Up" (fade in) and "Down" (fade out)
        rampDirButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3CBBB1)); // teal = Up
        rampDirButton.onClick = [this] {
            bool isUp = (rampDirButton.getButtonText() == "Up");
            if (isUp) {
                rampDirButton.setButtonText("Down");
                rampDirButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFCD6155)); // coral = Down
                processorRef.getRampDirection().store(1, std::memory_order_relaxed);
            } else {
                rampDirButton.setButtonText("Up");
                rampDirButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3CBBB1)); // teal = Up
                processorRef.getRampDirection().store(0, std::memory_order_relaxed);
            }
        };
        panel.addAndMakeVisible(rampDirButton);

        // Speed knob: rotary, displays seconds, skewed midpoint near 0.85s
        rampSpeedKnob.setSliderStyle(juce::Slider::Rotary);
        rampSpeedKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        rampSpeedKnob.setRange(0.03, 7.0, 0.01);
        rampSpeedKnob.setValue(1.7, juce::dontSendNotification);
        rampSpeedKnob.setTextValueSuffix(" s");
        rampSpeedKnob.setSkewFactorFromMidPoint(0.85);
        rampSpeedKnob.setDoubleClickReturnValue(true, 1.7);
        rampSpeedKnob.onValueChange = [this] {
            processorRef.getRampSpeed().store(
                static_cast<float>(rampSpeedKnob.getValue()), std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(rampSpeedKnob);

        rampSpeedLabel.setText("Speed", juce::dontSendNotification);
        rampSpeedLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(rampSpeedLabel);

        // Curve button: toggles between "Linear" and "Exponential"
        rampCurveButton.onClick = [this] {
            bool isLinear = (rampCurveButton.getButtonText() == "Linear");
            if (isLinear) {
                rampCurveButton.setButtonText("Exponential");
                processorRef.getRampCurve().store(1, std::memory_order_relaxed);
            } else {
                rampCurveButton.setButtonText("Linear");
                processorRef.getRampCurve().store(0, std::memory_order_relaxed);
            }
        };
        panel.addAndMakeVisible(rampCurveButton);

        // ARM button: one-shot trigger, visually distinct with mauve accent
        rampArmButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF9B59B6)); // mauve
        rampArmButton.onClick = [this] {
            processorRef.getRampArm().store(true, std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(rampArmButton);

        // ===== Unified VCA Effects section (Phase 54: dropdown + adaptive controls) =====
        fxSectionLabel.setText("VCA Effects", juce::dontSendNotification);
        fxSectionLabel.setJustificationType(juce::Justification::centredLeft);
        fxSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFD0D0D0));
        fxSectionLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        panel.addAndMakeVisible(fxSectionLabel);

        // Effect mode dropdown: single selector enforces mutual exclusion
        effectModeBox.addItem("Auto-Pan", 1);
        effectModeBox.addItem("Tremolo", 2);
        effectModeBox.addItem("AM", 3);
        effectModeBox.addItem("Ring Mod", 4);
        effectModeBox.addItem("Ducking", 5);
        effectModeBox.setSelectedId(1, juce::dontSendNotification);
        effectModeBox.onChange = [this] {
            int mode = effectModeBox.getSelectedId();
            // T-54-01: clamp selectedId to valid range
            if (mode < 1) mode = 1;
            if (mode > 5) mode = 5;

            // Disable ALL effects first (mutual exclusion via dropdown)
            processorRef.getTremoloEnabled().store(false, std::memory_order_relaxed);
            processorRef.getAutoPanEnabled().store(false, std::memory_order_relaxed);
            processorRef.getAmEnabled().store(false, std::memory_order_relaxed);
            processorRef.getRingModEnabled().store(false, std::memory_order_relaxed);
            processorRef.getDuckSource(0).store(-1, std::memory_order_relaxed);

            // Enable the selected effect
            switch (mode) {
                case 1: processorRef.getAutoPanEnabled().store(true, std::memory_order_relaxed); break;
                case 2: processorRef.getTremoloEnabled().store(true, std::memory_order_relaxed); break;
                case 3: processorRef.getAmEnabled().store(true, std::memory_order_relaxed); break;
                case 4: processorRef.getRingModEnabled().store(true, std::memory_order_relaxed); break;
                case 5: {
                    // Restore last duck source (or default to Voice 1 if none was set)
                    int sel = fxDuckSourceBox.getSelectedId();
                    int source = sel - 2; // -1=None, 0-23=voice
                    processorRef.getDuckSource(0).store(source, std::memory_order_relaxed);
                    break;
                }
            }

            // Store GUI state
            processorRef.getEffectMode().store(mode - 1, std::memory_order_relaxed);

            // Disable rampArmButton when any effect is active
            rampArmButton.setEnabled(false);

            updateEffectControlVisibility();
        };
        panel.addAndMakeVisible(effectModeBox);

        // Shared Rate knob (routes to correct atomic based on mode)
        fxRateKnob.setSliderStyle(juce::Slider::Rotary);
        fxRateKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        fxRateKnob.setRange(0.5, 19.0, 0.1);
        fxRateKnob.setValue(2.0, juce::dontSendNotification);
        fxRateKnob.setTextValueSuffix(" Hz");
        fxRateKnob.setSkewFactorFromMidPoint(4.0);
        fxRateKnob.setDoubleClickReturnValue(true, 2.0);
        fxRateKnob.onValueChange = [this] {
            float val = static_cast<float>(fxRateKnob.getValue());
            int mode = effectModeBox.getSelectedId();
            switch (mode) {
                case 1: processorRef.getAutoPanSpeedHz().store(val, std::memory_order_relaxed); break;
                case 2: processorRef.getTremoloSpeedHz().store(val, std::memory_order_relaxed); break;
                case 3: processorRef.getAmRateHz().store(val, std::memory_order_relaxed); break;
                case 4: processorRef.getRingModRateHz().store(val, std::memory_order_relaxed); break;
                default: break;
            }
        };
        panel.addAndMakeVisible(fxRateKnob);

        fxRateLabel.setText("Rate", juce::dontSendNotification);
        fxRateLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(fxRateLabel);

        // Shared Depth knob
        fxDepthKnob.setSliderStyle(juce::Slider::Rotary);
        fxDepthKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        fxDepthKnob.setRange(0.0, 100.0, 1.0);
        fxDepthKnob.setValue(100.0, juce::dontSendNotification);
        fxDepthKnob.setTextValueSuffix("%");
        fxDepthKnob.setDoubleClickReturnValue(true, 100.0);
        fxDepthKnob.onValueChange = [this] {
            float val = static_cast<float>(fxDepthKnob.getValue() / 100.0);
            int mode = effectModeBox.getSelectedId();
            switch (mode) {
                case 1: processorRef.getAutoPanDepth().store(val, std::memory_order_relaxed); break;
                case 2: processorRef.getTremoloDepth().store(val, std::memory_order_relaxed); break;
                case 3: processorRef.getAmDepth().store(val, std::memory_order_relaxed); break;
                case 4: processorRef.getRingModDepth().store(val, std::memory_order_relaxed); break;
                default: break;
            }
        };
        panel.addAndMakeVisible(fxDepthKnob);

        fxDepthLabel.setText("Depth", juce::dontSendNotification);
        fxDepthLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(fxDepthLabel);

        // Shape dropdown (Triangle/Saw Down/Saw Up)
        fxShapeBox.addItem("Triangle", 1);
        fxShapeBox.addItem("Saw Down", 2);
        fxShapeBox.addItem("Saw Up", 3);
        fxShapeBox.setSelectedId(1, juce::dontSendNotification);
        fxShapeBox.onChange = [this] {
            int sel = fxShapeBox.getSelectedId();
            if (sel >= 1 && sel <= 3)
                processorRef.getSweepShape().store(sel - 1, std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(fxShapeBox);

        fxShapeLabel.setText("Shape", juce::dontSendNotification);
        fxShapeLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(fxShapeLabel);

        // Lin/Exp curve toggle
        fxCurveButton.onClick = [this] {
            bool isLinear = (fxCurveButton.getButtonText() == "Linear");
            int newCurve = isLinear ? 1 : 0;
            fxCurveButton.setButtonText(isLinear ? "Exponential" : "Linear");
            int mode = effectModeBox.getSelectedId();
            switch (mode) {
                case 2: processorRef.getTremoloCurve().store(newCurve, std::memory_order_relaxed); break;
                case 3: processorRef.getAmCurve().store(newCurve, std::memory_order_relaxed); break;
                case 4: processorRef.getRingModCurve().store(newCurve, std::memory_order_relaxed); break;
                default: break; // Auto-Pan has no separate curve atomic
            }
        };
        panel.addAndMakeVisible(fxCurveButton);

        // L/R Ratio knob (visible in Auto-Pan and Tremolo modes)
        fxRatioKnob.setSliderStyle(juce::Slider::Rotary);
        fxRatioKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        fxRatioKnob.setRange(0.5, 4.0, 0.1);
        fxRatioKnob.setValue(1.0, juce::dontSendNotification);
        fxRatioKnob.setDoubleClickReturnValue(true, 1.0);
        fxRatioKnob.textFromValueFunction = [](double value) {
            return juce::String("1:") + juce::String(value, 1);
        };
        fxRatioKnob.onValueChange = [this] {
            float val = static_cast<float>(fxRatioKnob.getValue());
            int mode = effectModeBox.getSelectedId();
            if (mode == 1)
                processorRef.getAutoPanRatio().store(val, std::memory_order_relaxed);
            else if (mode == 2)
                processorRef.getTremoloRatio().store(val, std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(fxRatioKnob);

        fxRatioLabel.setText("L/R Ratio", juce::dontSendNotification);
        fxRatioLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(fxRatioLabel);

        // --- Ducking-specific controls ---
        fxDuckSourceBox.addItem("None", 1);
        for (int i = 0; i < 24; ++i)
            fxDuckSourceBox.addItem("Voice " + juce::String(i), i + 2);
        fxDuckSourceBox.setSelectedId(1, juce::dontSendNotification);
        fxDuckSourceBox.onChange = [this] {
            int sel = fxDuckSourceBox.getSelectedId();
            if (sel < 1) sel = 1;
            if (sel > 25) sel = 25;
            int source = sel - 2; // -1=None, 0-23=voice
            processorRef.getDuckSource(0).store(source, std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(fxDuckSourceBox);

        fxDuckSourceLabel.setText("Source", juce::dontSendNotification);
        fxDuckSourceLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(fxDuckSourceLabel);

        fxDuckAttackKnob.setSliderStyle(juce::Slider::Rotary);
        fxDuckAttackKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        fxDuckAttackKnob.setRange(0.001, 0.5, 0.001);
        fxDuckAttackKnob.setValue(0.01, juce::dontSendNotification);
        fxDuckAttackKnob.setTextValueSuffix(" s");
        fxDuckAttackKnob.setSkewFactorFromMidPoint(0.05);
        fxDuckAttackKnob.setDoubleClickReturnValue(true, 0.01);
        fxDuckAttackKnob.onValueChange = [this] {
            processorRef.getDuckAttack(0).store(
                static_cast<float>(fxDuckAttackKnob.getValue()), std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(fxDuckAttackKnob);

        fxDuckAttackLabel.setText("Attack", juce::dontSendNotification);
        fxDuckAttackLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(fxDuckAttackLabel);

        fxDuckReleaseKnob.setSliderStyle(juce::Slider::Rotary);
        fxDuckReleaseKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        fxDuckReleaseKnob.setRange(0.03, 6.8, 0.01);
        fxDuckReleaseKnob.setValue(0.4, juce::dontSendNotification);
        fxDuckReleaseKnob.setTextValueSuffix(" s");
        fxDuckReleaseKnob.setSkewFactorFromMidPoint(1.0);
        fxDuckReleaseKnob.setDoubleClickReturnValue(true, 0.4);
        fxDuckReleaseKnob.onValueChange = [this] {
            processorRef.getDuckRelease(0).store(
                static_cast<float>(fxDuckReleaseKnob.getValue()), std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(fxDuckReleaseKnob);

        fxDuckReleaseLabel.setText("Release", juce::dontSendNotification);
        fxDuckReleaseLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(fxDuckReleaseLabel);

        fxDuckDepthKnob.setSliderStyle(juce::Slider::Rotary);
        fxDuckDepthKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        fxDuckDepthKnob.setRange(0.0, 100.0, 1.0);
        fxDuckDepthKnob.setValue(100.0, juce::dontSendNotification);
        fxDuckDepthKnob.setTextValueSuffix("%");
        fxDuckDepthKnob.setDoubleClickReturnValue(true, 100.0);
        fxDuckDepthKnob.onValueChange = [this] {
            processorRef.getDuckDepth(0).store(
                static_cast<float>(fxDuckDepthKnob.getValue() / 100.0), std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(fxDuckDepthKnob);

        fxDuckDepthLabel.setText("Depth", juce::dontSendNotification);
        fxDuckDepthLabel.setJustificationType(juce::Justification::centred);
        panel.addAndMakeVisible(fxDuckDepthLabel);

        // Set initial visibility (Auto-Pan mode by default)
        updateEffectControlVisibility();

        // Internal Mod Bus section (Phase 50: noise-to-pitch/vol/pan per voice)
        modBusSectionLabel.setText("Mod Bus", juce::dontSendNotification);
        modBusSectionLabel.setJustificationType(juce::Justification::centredLeft);
        modBusSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFD0D0D0));
        modBusSectionLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        panel.addAndMakeVisible(modBusSectionLabel);

        modBusPitchKnob.setSliderStyle(juce::Slider::Rotary);
        modBusPitchKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        modBusPitchKnob.setRange(0.0, 100.0, 1.0);
        modBusPitchKnob.setValue(0.0, juce::dontSendNotification);
        modBusPitchKnob.setTextValueSuffix(" %");
        modBusPitchKnob.setDoubleClickReturnValue(true, 0.0);
        modBusPitchKnob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF3CBBB1));
        modBusPitchKnob.onValueChange = [this] {
            processorRef.getModBusPitchDepth().store(
                static_cast<float>(modBusPitchKnob.getValue() / 100.0),
                std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(modBusPitchKnob);

        modBusPitchLabel.setText("Pitch", juce::dontSendNotification);
        modBusPitchLabel.setJustificationType(juce::Justification::centred);
        modBusPitchLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
        modBusPitchLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFA0A0A0));
        panel.addAndMakeVisible(modBusPitchLabel);

        modBusVolKnob.setSliderStyle(juce::Slider::Rotary);
        modBusVolKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        modBusVolKnob.setRange(0.0, 100.0, 1.0);
        modBusVolKnob.setValue(0.0, juce::dontSendNotification);
        modBusVolKnob.setTextValueSuffix(" %");
        modBusVolKnob.setDoubleClickReturnValue(true, 0.0);
        modBusVolKnob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF3CBBB1));
        modBusVolKnob.onValueChange = [this] {
            processorRef.getModBusVolDepth().store(
                static_cast<float>(modBusVolKnob.getValue() / 100.0),
                std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(modBusVolKnob);

        modBusVolLabel.setText("Volume", juce::dontSendNotification);
        modBusVolLabel.setJustificationType(juce::Justification::centred);
        modBusVolLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
        modBusVolLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFA0A0A0));
        panel.addAndMakeVisible(modBusVolLabel);

        modBusPanKnob.setSliderStyle(juce::Slider::Rotary);
        modBusPanKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
        modBusPanKnob.setRange(0.0, 100.0, 1.0);
        modBusPanKnob.setValue(0.0, juce::dontSendNotification);
        modBusPanKnob.setTextValueSuffix(" %");
        modBusPanKnob.setDoubleClickReturnValue(true, 0.0);
        modBusPanKnob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF3CBBB1));
        modBusPanKnob.onValueChange = [this] {
            processorRef.getModBusPanDepth().store(
                static_cast<float>(modBusPanKnob.getValue() / 100.0),
                std::memory_order_relaxed);
        };
        panel.addAndMakeVisible(modBusPanKnob);

        modBusPanLabel.setText("Pan", juce::dontSendNotification);
        modBusPanLabel.setJustificationType(juce::Justification::centred);
        modBusPanLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
        modBusPanLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFA0A0A0));
        panel.addAndMakeVisible(modBusPanLabel);

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

    // Phase 54: Sync duck controls from processor state (voice 0).
    {
        int src = processorRef.getDuckSource(0).load(std::memory_order_relaxed);
        int expectedId = src + 2; // -1 -> 1 (None), 0 -> 2 (Voice 0), etc.
        if (fxDuckSourceBox.getSelectedId() != expectedId)
            fxDuckSourceBox.setSelectedId(expectedId, juce::dontSendNotification);

        float rel = processorRef.getDuckRelease(0).load(std::memory_order_relaxed);
        if (std::abs(static_cast<float>(fxDuckReleaseKnob.getValue()) - rel) > 0.005f)
            fxDuckReleaseKnob.setValue(static_cast<double>(rel), juce::dontSendNotification);

        float dep = processorRef.getDuckDepth(0).load(std::memory_order_relaxed);
        float depPct = dep * 100.0f;
        if (std::abs(static_cast<float>(fxDuckDepthKnob.getValue()) - depPct) > 0.5f)
            fxDuckDepthKnob.setValue(static_cast<double>(depPct), juce::dontSendNotification);

        float atk = processorRef.getDuckAttack(0).load(std::memory_order_relaxed);
        if (std::abs(static_cast<float>(fxDuckAttackKnob.getValue()) - atk) > 0.0005f)
            fxDuckAttackKnob.setValue(static_cast<double>(atk), juce::dontSendNotification);
    }

}

void SPU94AudioProcessorEditor::updateEffectControlVisibility()
{
    int mode = effectModeBox.getSelectedId(); // 1=AutoPan, 2=Tremolo, 3=AM, 4=RingMod, 5=Ducking

    // Shared VCA ramp controls: visible for modes 1-4
    bool showShared = (mode >= 1 && mode <= 4);
    fxRateKnob.setVisible(showShared);
    fxRateLabel.setVisible(showShared);
    fxDepthKnob.setVisible(showShared);
    fxDepthLabel.setVisible(showShared);
    fxShapeBox.setVisible(showShared);
    fxShapeLabel.setVisible(showShared);
    fxCurveButton.setVisible(showShared);

    // L/R Ratio: only Auto-Pan (1) and Tremolo (2)
    bool showRatio = (mode == 1 || mode == 2);
    fxRatioKnob.setVisible(showRatio);
    fxRatioLabel.setVisible(showRatio);

    // Ducking controls: only mode 5
    bool showDuck = (mode == 5);
    fxDuckSourceBox.setVisible(showDuck);
    fxDuckSourceLabel.setVisible(showDuck);
    fxDuckAttackKnob.setVisible(showDuck);
    fxDuckAttackLabel.setVisible(showDuck);
    fxDuckReleaseKnob.setVisible(showDuck);
    fxDuckReleaseLabel.setVisible(showDuck);
    fxDuckDepthKnob.setVisible(showDuck);
    fxDuckDepthLabel.setVisible(showDuck);

    // Update Rate knob range dynamically based on selected mode
    if (mode == 1 || mode == 2) {
        // Auto-Pan / Tremolo: 0.5-19 Hz
        fxRateKnob.setRange(0.5, 19.0, 0.1);
        fxRateKnob.setSkewFactorFromMidPoint(4.0);
    } else if (mode == 3) {
        // AM: 37-7350 Hz
        fxRateKnob.setRange(37.0, 7350.0, 1.0);
        fxRateKnob.setSkewFactorFromMidPoint(500.0);
    } else if (mode == 4) {
        // Ring Mod: 21.5-9647 Hz
        fxRateKnob.setRange(21.5, 9647.0, 1.0);
        fxRateKnob.setSkewFactorFromMidPoint(440.0);
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

void SPU94AudioProcessorEditor::updateVoiceVolumes()
{
    // Pan-to-register math: linear pan law.
    // Pan: -100..+100 → normalized -1.0..+1.0
    // Level: 0..100 → normalized 0.0..1.0
    double pan   = voicePanKnob.getValue() / 100.0;
    double level = voiceLevelKnob.getValue() / 100.0;

    // Linear pan law: center gives equal gain on both channels.
    double gain_l = std::min(1.0, 1.0 - pan);
    double gain_r = std::min(1.0, 1.0 + pan);

    // Scale to SPU register range (0..0x3FFF), with rounding.
    int16_t vol_l = static_cast<int16_t>(level * gain_l * 0x3FFF + 0.5);
    int16_t vol_r = static_cast<int16_t>(level * gain_r * 0x3FFF + 0.5);

    // Clamp to valid positive range before sign flip.
    if (vol_l < 0) vol_l = 0;
    if (vol_l > 0x3FFF) vol_l = 0x3FFF;
    if (vol_r < 0) vol_r = 0;
    if (vol_r > 0x3FFF) vol_r = 0x3FFF;

    // INV toggle: negate both channels for phase inversion.
    if (voiceInvToggle.getToggleState())
    {
        vol_l = static_cast<int16_t>(-vol_l);
        vol_r = static_cast<int16_t>(-vol_r);
    }

    processorRef.setGuiVoiceVolL(vol_l);
    processorRef.setGuiVoiceVolR(vol_r);

    // Show/hide teal INV indicator.
    voiceInvIndicator.setVisible(voiceInvToggle.getToggleState());
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

            // ADSR section — directly below marker knobs
            constexpr int adsrDispY = 322;
            adsrDisplay.setBounds(10, adsrDispY, 380, 55);

            constexpr int aky = 382, akw = 65, akh = 54;
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

            // Pan + Level + INV section — below ADSR
            constexpr int voly = tgy + 28;
            // Pan rotary on top
            voicePanLabel.setBounds(20, voly, 100, 16);
            voicePanKnob.setBounds(20, voly + 16, 100, 70);
            // Level vertical fader below pan
            voiceLevelLabel.setBounds(20, voly + 90, 100, 16);
            voiceLevelKnob.setBounds(35, voly + 106, 70, 100);
            // INV toggle + indicator to the right of pan
            voiceInvToggle.setBounds(160, voly + 20, 70, 30);
            voiceInvIndicator.setBounds(160, voly + 52, 70, 16);

            // Voice Mode section — NON and PMON toggles below INV (Phase 40)
            voiceModeLabel.setBounds(160, voly + 80, 100, 16);
            voiceNonToggle.setBounds(160, voly + 98, 70, 30);
            voicePmonToggle.setBounds(160, voly + 132, 70, 30);
            noiseColorLabel.setBounds(260, voly + 80, 100, 16);
            noiseColorKnob.setBounds(270, voly + 96, 80, 70);

            // VCA Ramp section — below Pan/Level area (Phase 41)
            constexpr int rampy = voly + 218;
            rampSectionLabel.setBounds(20, rampy, 120, 16);
            rampDirButton.setBounds(20, rampy + 20, 70, 28);
            rampCurveButton.setBounds(100, rampy + 20, 80, 28);
            rampSpeedLabel.setBounds(200, rampy, 80, 16);
            rampSpeedKnob.setBounds(200, rampy + 16, 80, 70);
            rampArmButton.setBounds(300, rampy + 20, 80, 36);

            // === Unified VCA Effects section (Phase 54: right half, y=142) ===
            constexpr int fx_x = 410;
            constexpr int fx_start_y = 142;

            fxSectionLabel.setBounds(fx_x, fx_start_y, 100, 16);
            effectModeBox.setBounds(fx_x + 105, fx_start_y, 140, 24);

            // Row 1: Rate + Depth (y = fx_start_y + 30)
            constexpr int row1y = fx_start_y + 30;
            fxRateLabel.setBounds(fx_x, row1y, 80, 16);
            fxRateKnob.setBounds(fx_x, row1y + 16, 80, 70);
            fxDepthLabel.setBounds(fx_x + 90, row1y, 80, 16);
            fxDepthKnob.setBounds(fx_x + 90, row1y + 16, 80, 70);

            // Row 1 continued: Shape + Curve
            fxShapeLabel.setBounds(fx_x + 180, row1y, 80, 16);
            fxShapeBox.setBounds(fx_x + 180, row1y + 20, 100, 24);
            fxCurveButton.setBounds(fx_x + 180, row1y + 50, 100, 28);

            // Row 2: L/R Ratio (y = row1y + 95)
            constexpr int row2y = row1y + 95;
            fxRatioLabel.setBounds(fx_x, row2y, 80, 16);
            fxRatioKnob.setBounds(fx_x, row2y + 16, 80, 70);

            // Ducking controls (same area, shown only in Duck mode)
            fxDuckSourceLabel.setBounds(fx_x, row1y, 80, 16);
            fxDuckSourceBox.setBounds(fx_x, row1y + 16, 140, 24);
            fxDuckAttackLabel.setBounds(fx_x, row1y + 46, 80, 16);
            fxDuckAttackKnob.setBounds(fx_x, row1y + 62, 80, 70);
            fxDuckReleaseLabel.setBounds(fx_x + 90, row1y + 46, 80, 16);
            fxDuckReleaseKnob.setBounds(fx_x + 90, row1y + 62, 80, 70);
            fxDuckDepthLabel.setBounds(fx_x + 180, row1y + 46, 80, 16);
            fxDuckDepthKnob.setBounds(fx_x + 180, row1y + 62, 80, 70);

            // Mod Bus (Phase 50) — below unified effects
            constexpr int mody = row2y + 100;
            constexpr int col_right = 410;
            modBusSectionLabel.setBounds(col_right, mody, 120, 16);
            modBusPitchLabel.setBounds(col_right, mody + 20, 80, 16);
            modBusPitchKnob.setBounds(col_right, mody + 36, 80, 70);
            modBusVolLabel.setBounds(col_right + 90, mody + 20, 80, 16);
            modBusVolKnob.setBounds(col_right + 90, mody + 36, 80, 70);
            modBusPanLabel.setBounds(col_right + 180, mody + 20, 80, 16);
            modBusPanKnob.setBounds(col_right + 180, mody + 36, 80, 70);
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
