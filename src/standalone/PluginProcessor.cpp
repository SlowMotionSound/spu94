#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WavLoader.h"
#include <cstring>
#include <memory>

SPU94AudioProcessor::SPU94AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Pre-seed register shadows with the Hall preset so the editor shows
    // correct slider values even before prepareToPlay runs (CR-02).
    // Heap-allocate the temporary buffers (~528 KB) to avoid a large stack frame.
    auto tmpState = std::make_unique<unsigned char[]>(SPU94_STATE_SIZE_MAX);
    auto tmpWork  = std::make_unique<unsigned char[]>(SPU94_WORK_BUF_MAX_BYTES);
    std::memset(tmpState.get(), 0, SPU94_STATE_SIZE_MAX);
    std::memset(tmpWork.get(),  0, SPU94_WORK_BUF_MAX_BYTES);

    auto* tmp = spu94_init(tmpState.get(), SPU94_STATE_SIZE_MAX,
                           tmpWork.get(),  SPU94_WORK_BUF_MAX_BYTES);
    if (tmp)
    {
        spu94_load_preset(tmp, SPU94_PRESET_HALL);
        registerBridge.syncShadowsFromSPU(tmp);
        spu94_destroy(tmp);
    }
}

SPU94AudioProcessor::~SPU94AudioProcessor()
{
    for (int e = 0; e < 3; ++e)
    {
        if (engines[e] != nullptr)
        {
            spu94_destroy(engines[e]);
            engines[e] = nullptr;
        }
    }
}

const juce::String SPU94AudioProcessor::getName() const
{
    return juce::String("SPU-94");
}

void SPU94AudioProcessor::prepareToPlay(double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Tear down all three engines before reinitializing (WR-03 extended)
    for (int e = 0; e < 3; ++e)
    {
        if (engines[e] != nullptr)
        {
            spu94_destroy(engines[e]);
            engines[e] = nullptr;
        }
    }

    // Allocate caller-owned SPU state and work buffers for all three engines
    stateBufA.allocate(SPU94_STATE_SIZE_MAX, true);
    workBufA.allocate(SPU94_WORK_BUF_MAX_BYTES, true);
    stateBufB.allocate(SPU94_STATE_SIZE_MAX, true);
    workBufB.allocate(SPU94_WORK_BUF_MAX_BYTES, true);
    stateBufC.allocate(SPU94_STATE_SIZE_MAX, true);
    workBufC.allocate(SPU94_WORK_BUF_MAX_BYTES, true);

    engines[0] = spu94_init(stateBufA.getData(), SPU94_STATE_SIZE_MAX,
                            workBufA.getData(), SPU94_WORK_BUF_MAX_BYTES);
    engines[1] = spu94_init(stateBufB.getData(), SPU94_STATE_SIZE_MAX,
                            workBufB.getData(), SPU94_WORK_BUF_MAX_BYTES);
    engines[2] = spu94_init(stateBufC.getData(), SPU94_STATE_SIZE_MAX,
                            workBufC.getData(), SPU94_WORK_BUF_MAX_BYTES);

    if (engines[0] == nullptr || engines[1] == nullptr || engines[2] == nullptr)
        return;

    // engines[0] (smooth) and engines[2] (instant) both start at Hall preset
    spu94_load_preset(engines[0], SPU94_PRESET_HALL);
    spu94_load_preset(engines[1], SPU94_PRESET_HALL);
    spu94_load_preset(engines[2], SPU94_PRESET_HALL);
    registerBridge.syncShadowsFromSPU(engines[0]);
    lastMorphPosition = -1.0f;  // force morph apply on first processBlock
    morphInitialized = false;

    // Mixer defaults on all three engines (timbral parity)
    for (int e = 0; e < 3; ++e)
    {
        spu94_set_input_gain(engines[e], 0x7FFF);
        spu94_set_dry_fader(engines[e], 0x0000);
        spu94_set_reverb_fader(engines[e], 0x7FFF);
        spu94_set_dry_send(engines[e], 0x0000);
        spu94_set_patina_send(engines[e], 0x7FFF);
        spu94_set_dac_enabled(engines[e], 1);
        spu94_set_latency_comp(engines[e], 1);
    }
}

void SPU94AudioProcessor::releaseResources()
{
    for (int e = 0; e < 3; ++e)
    {
        if (engines[e] != nullptr)
        {
            spu94_destroy(engines[e]);
            engines[e] = nullptr;
        }
    }
}

void SPU94AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midiMessages*/)
{
    // Check if new WAV data is ready from the message thread.
    // Double-buffer swap (CR-01): read from whichever slot the message
    // thread last wrote.  Old wavSource vectors are moved into the
    // consumed slot so their heap storage is freed on the *next*
    // message-thread load, not here on the audio thread (WR-01).
    if (newWavReady.load(std::memory_order_acquire))
    {
        const auto slot = static_cast<size_t>(pendingWriteSlot.load(std::memory_order_relaxed));
        std::swap(wavSource.L, pendingSlots[slot].L);
        std::swap(wavSource.R, pendingSlots[slot].R);
        wavSource.numFrames = pendingSlots[slot].numFrames;
        wavSource.playPos.store(0, std::memory_order_relaxed);
        wavSource.loaded.store(true, std::memory_order_release);
        newWavReady.store(false, std::memory_order_release);
    }

    if (!wavSource.loaded.load(std::memory_order_acquire) ||
        !wavSource.playing.load(std::memory_order_relaxed) ||
        engines[0] == nullptr || engines[1] == nullptr || engines[2] == nullptr)
    {
        buffer.clear();
        return;
    }

    // 1. Drain preset command queue — apply to both audio engines so they
    // stay in lockstep when the GUI requests a preset switch.
    if (presetQueue.drain(engines[0]))
    {
        // Mirror the same preset onto engines[2] (instant audio path).
        spu94_load_preset(engines[2], presetQueue.getAppliedId() < SPU94_PRESET__COUNT
                                       ? (spu94_preset_id_t)presetQueue.getAppliedId()
                                       : SPU94_PRESET_HALL);
        registerBridge.syncShadowsFromSPU(engines[0]);
    }

    // 1b. Drain file-preset load request (GUI thread may have loaded a .spu94 file)
    if (filePresetReady.load(std::memory_order_acquire))
    {
        size_t len = pendingPresetLen.load(std::memory_order_relaxed);
        spu94_preset_load(engines[0], pendingPresetBuf.data(), len);
        spu94_preset_load(engines[2], pendingPresetBuf.data(), len);
        registerBridge.syncShadowsFromSPU(engines[0]);
        filePresetReady.store(false, std::memory_order_release);
        filePresetAppliedCount.fetch_add(1, std::memory_order_release);

        // Sync mixer/DAC atomics from loaded SPU state so GUI reflects the file's values
        inputLevel.store(static_cast<float>(spu94_get_input_gain(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        dryLevel.store(static_cast<float>(spu94_get_dry_fader(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        patinaLevel.store(static_cast<float>(spu94_get_patina_fader(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        reverbLevel.store(static_cast<float>(spu94_get_reverb_fader(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        drySend.store(static_cast<float>(spu94_get_dry_send(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        adpcmSend.store(static_cast<float>(spu94_get_patina_send(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        latencyCompEnabled.store(spu94_get_latency_comp(engines[0]) != 0, std::memory_order_relaxed);
        dacEnabled.store(spu94_get_dac_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacFirEnabled.store(spu94_get_dac_fir_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacNoiseEnabled.store(spu94_get_dac_noise_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacTrueOversample.store(spu94_get_dac_true_oversample(engines[0]) != 0, std::memory_order_relaxed);
    }

    // 2. Push register values: morph engine OR register bridge, never both.
    // When morph is active, the interpolation engine owns all 30 reverb registers.
    // When advanced mode is active, the register bridge (GUI sliders) owns them.
    // Push to both audio engines so they stay in sync in Advanced mode.
    if (!morphActive.load(std::memory_order_relaxed))
    {
        registerBridge.pushPendingRegisterWrites(engines[0]);
        registerBridge.pushPendingRegisterWrites(engines[2]);
    }

    // Sync shadows from SPU when switching from morph to advanced mode
    if (needShadowSync.exchange(false, std::memory_order_relaxed))
        registerBridge.syncShadowsFromSPU(engines[0]);

    // 3. ADPCM coloration toggle (ADPCM-IO-06, D-06): read GUI atomic,
    // push to C API. Takes effect on the next spu94_process block.
    // ADPCM auto-enables when patina fader or ADPCM send is non-zero.
    // The old dedicated toggle was removed (D-08); the mixer controls
    // now implicitly drive ADPCM on/off.
    {
        const bool patina_active =
            patinaLevel.load(std::memory_order_relaxed) > 0.0f ||
            adpcmSend.load(std::memory_order_relaxed) > 0.0f;
        const int adpcm_flag = patina_active ? 1 : 0;
        spu94_set_adpcm_enabled(engines[0], adpcm_flag);
    }

    const int n = buffer.getNumSamples();
    const auto numFrames = wavSource.numFrames;

    // Guard against divide-by-zero if numFrames is somehow 0 (CR-03).
    if (numFrames == 0) { buffer.clear(); return; }

    // Stack-allocated int16 I/O buffers -- no heap in the audio thread.
    // JUCE block sizes are typically 256-1024; 4096 is a generous ceiling.
    // If the host delivers an oversized block, clear and bail rather than
    // silently truncating (WR-02: unwritten tail would be garbage audio).
    constexpr int kMaxBlock = 4096;
    jassert(n <= kMaxBlock);
    if (n > kMaxBlock) { buffer.clear(); return; }
    const int samplesToProcess = n;

    int16_t tmpL_in[kMaxBlock];
    int16_t tmpR_in[kMaxBlock];
    int16_t tmpL_out[kMaxBlock];   // smooth engine output
    int16_t tmpR_out[kMaxBlock];
    int16_t tmpL_out2[kMaxBlock];  // instant engine output
    int16_t tmpR_out2[kMaxBlock];

    // Push mixer/DAC state to BOTH audio engines so they sound identical
    // modulo morph behavior. Mismatched mixer state would cause the blend
    // to sound like two unrelated reverbs rather than two morph styles.
    for (int e : {0, 2})
    {
        spu94_set_input_gain(engines[e], static_cast<int16_t>(
            inputLevel.load(std::memory_order_relaxed) * 0x7FFF));
        spu94_set_dry_fader(engines[e], static_cast<int16_t>(
            dryLevel.load(std::memory_order_relaxed) * 0x7FFF));
        spu94_set_patina_fader(engines[e], static_cast<int16_t>(
            patinaLevel.load(std::memory_order_relaxed) * 0x7FFF));
        spu94_set_reverb_fader(engines[e], static_cast<int16_t>(
            reverbLevel.load(std::memory_order_relaxed) * 0x7FFF));
        spu94_set_dry_send(engines[e], static_cast<int16_t>(
            drySend.load(std::memory_order_relaxed) * 0x7FFF));
        spu94_set_patina_send(engines[e], static_cast<int16_t>(
            adpcmSend.load(std::memory_order_relaxed) * 0x7FFF));
        spu94_set_latency_comp(engines[e],
            latencyCompEnabled.load(std::memory_order_relaxed) ? 1 : 0);
        spu94_set_dac_enabled(engines[e],
            dacEnabled.load(std::memory_order_relaxed) ? 1 : 0);
        spu94_set_dac_fir_enabled(engines[e],
            dacFirEnabled.load(std::memory_order_relaxed) ? 1 : 0);
        spu94_set_dac_noise_enabled(engines[e],
            dacNoiseEnabled.load(std::memory_order_relaxed) ? 1 : 0);
        spu94_set_dac_true_oversample(engines[e],
            dacTrueOversample.load(std::memory_order_relaxed) ? 1 : 0);
    }

    // Morph position: smooth path uses scratch engine + slew targets;
    // instant path writes register values directly (TICK_LATCHED, clicky).
    if (morphActive.load(std::memory_order_relaxed))
    {
        float pos = morphPosition.load(std::memory_order_relaxed);
        if (pos != lastMorphPosition)
        {
            lastMorphPosition = pos;

            if (!morphInitialized)
            {
                spu94_interp_set_morph(engines[0], pos);
                spu94_interp_set_morph(engines[2], pos);
                morphInitialized = true;
            }
            else
            {
                // Smooth path: compute targets on scratch, arm slew on engines[0]
                spu94_interp_set_morph(engines[1], pos);
                spu94_apply_pending_writes(engines[1]);
                spu94_snapshot_registers(engines[1], targetRegs);
                spu94_set_slew_targets(engines[0], targetRegs);

                // Instant path: write directly to engines[2] — registers latch
                // at the next reverb tick boundary (the v1.5 click behavior).
                spu94_interp_set_morph(engines[2], pos);
            }
        }
    }

    // Engine blend, haze, freeze — read once per block
    const float b = engineBlend.load(std::memory_order_relaxed);
    const float bSmooth  = b;
    const float bInstant = 1.0f - b;
    const float haze     = hazeAmount.load(std::memory_order_relaxed);
    const float freeze   = hazeFreeze.load(std::memory_order_relaxed);
    // Playhead advance rate: knob=0 → 1.0 (tracks write head with fixed lag),
    // knob=1 → ~0.001 (nearly stopped, ~1000x time stretch). Pitch preserved
    // because grains always read at unity rate.
    const float playheadRate = std::exp(freeze * -6.908f);
    // Feedback safety: keeps the grain → reverb input loop stable even at
    // Haze=1.0. The reverb itself can have feedback near unity in some
    // presets, so we cap the grain re-injection well below that.
    // While morph is actively slewing the registers may pass through
    // unstable combinations momentarily — drop feedback further then.
    constexpr float kGrainFeedbackSafety = 0.5f;
    constexpr float kGrainFeedbackMorphing = 0.2f;
    const bool morphing = spu94_is_slewing(engines[0]) != 0;
    const float fbSafety = morphing ? kGrainFeedbackMorphing
                                     : kGrainFeedbackSafety;

    // Pre-pass: per-sample, generate audio input + grain feedback for the reverb.
    // Grains read from the granular buffer (filled by previous blocks' reverb
    // output) and inject into the reverb input scaled by Haze * safety.
    auto playPos = wavSource.playPos.load(std::memory_order_relaxed);
    int16_t grainL_pre[kMaxBlock];
    int16_t grainR_pre[kMaxBlock];
    for (int i = 0; i < samplesToProcess; ++i)
    {
        const auto idx = static_cast<size_t>((playPos + static_cast<uint64_t>(i)) % numFrames);
        const float dryInL = wavSource.L[idx] / 32768.0f;
        const float dryInR = wavSource.R[idx] / 32768.0f;

        // Advance playhead
        hazePlayhead += playheadRate;
        if (hazePlayhead >= (float)kHazeBufLen) hazePlayhead -= (float)kHazeBufLen;

        // Spawn new grains at fixed intervals
        if (++hazeSpawnCounter >= kHazeSpawnInterval)
        {
            hazeSpawnCounter = 0;
            for (auto& g : hazeGrains)
            {
                if (g.active) continue;
                g.active = true;
                g.age = 0;
                hazeRng = hazeRng * 1664525u + 1013904223u;
                int jitter = (int)(hazeRng & 0x3FFFu) - 0x2000;  // ±8192 samples
                int p = (int)hazePlayhead + jitter;
                p %= kHazeBufLen;
                if (p < 0) p += kHazeBufLen;
                g.posInBuf = p;
                break;
            }
        }

        // Sum active grain contributions (unity rate, no pitch shift)
        float gL = 0.0f, gR = 0.0f;
        for (auto& g : hazeGrains)
        {
            if (!g.active) continue;
            const float tNorm = (float)g.age / (float)kHazeGrainLen;
            const float env = 4.0f * tNorm * (1.0f - tNorm);
            gL += hazeBufL[g.posInBuf] * env;
            gR += hazeBufR[g.posInBuf] * env;
            g.posInBuf = (g.posInBuf + 1) % kHazeBufLen;
            if (++g.age >= kHazeGrainLen)
                g.active = false;
        }

        // Mix dry input + grain feedback (clamped) into the reverb input.
        // Narrow the grain stereo width before feedback to prevent runaway
        // one-side blasts (independent L/R grain positions can produce
        // pathologically asymmetric content that the reverb amplifies).
        // 60% original / 40% mono blend keeps motion without extremes.
        const float fbGain = haze * kGrainFeedbackSafety;
        constexpr float kGrainStereoWidth = 0.6f;
        const float gMid  = 0.5f * (gL + gR);
        const float gLNarrow = gL * kGrainStereoWidth + gMid * (1.0f - kGrainStereoWidth);
        const float gRNarrow = gR * kGrainStereoWidth + gMid * (1.0f - kGrainStereoWidth);
        float reverbInL = dryInL + gLNarrow * fbGain;
        float reverbInR = dryInR + gRNarrow * fbGain;
        // Clamp to [-1, 1] before int16 conversion
        if (reverbInL >  1.0f) reverbInL =  1.0f;
        if (reverbInL < -1.0f) reverbInL = -1.0f;
        if (reverbInR >  1.0f) reverbInR =  1.0f;
        if (reverbInR < -1.0f) reverbInR = -1.0f;
        tmpL_in[i] = (int16_t)(reverbInL * 32767.0f);
        tmpR_in[i] = (int16_t)(reverbInR * 32767.0f);

        // Stash grain values for any later use (currently unused but cheap)
        (void)grainL_pre; (void)grainR_pre;
    }

    // Process both reverb engines in parallel on the grain-shaped input
    spu94_process(engines[0], tmpL_in, tmpR_in, tmpL_out,  tmpR_out,
                  static_cast<uint32_t>(samplesToProcess));
    spu94_process(engines[2], tmpL_in, tmpR_in, tmpL_out2, tmpR_out2,
                  static_cast<uint32_t>(samplesToProcess));

    // Post-pass: blend reverb outputs, capture into granular buffer, emit
    auto* outL = buffer.getWritePointer(0);
    auto* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < samplesToProcess; ++i)
    {
        const float blendedL = (tmpL_out[i] * bSmooth + tmpL_out2[i] * bInstant) / 32768.0f;
        const float blendedR = (tmpR_out[i] * bSmooth + tmpR_out2[i] * bInstant) / 32768.0f;

        // Capture reverb output into the granular buffer (this is what the
        // grains will replay back into the reverb input on future blocks)
        hazeBufL[hazeWritePos] = blendedL;
        hazeBufR[hazeWritePos] = blendedR;
        hazeWritePos = (hazeWritePos + 1) % kHazeBufLen;

        outL[i] = blendedL;
        if (outR) outR[i] = blendedR;
    }

    // Advance play position (continuous loop).
    wavSource.playPos.store((playPos + static_cast<uint64_t>(samplesToProcess)) % numFrames,
                            std::memory_order_relaxed);
}

// --- File preset save/load (Phase 14, PRE-08/PRE-09) ---

juce::String SPU94AudioProcessor::savePresetToString(
    const juce::String& name, const juce::String& description)
{
    if (!engines[0]) return {};

    // processBlock only pushes GUI atomics to the SPU while audio is playing.
    // Sync them here so the saved state always matches what the knobs show.
    spu94_set_input_gain(engines[0], static_cast<int16_t>(
        inputLevel.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_dry_fader(engines[0], static_cast<int16_t>(
        dryLevel.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_patina_fader(engines[0], static_cast<int16_t>(
        patinaLevel.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_reverb_fader(engines[0], static_cast<int16_t>(
        reverbLevel.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_dry_send(engines[0], static_cast<int16_t>(
        drySend.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_patina_send(engines[0], static_cast<int16_t>(
        adpcmSend.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_latency_comp(engines[0],
        latencyCompEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_dac_enabled(engines[0],
        dacEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_dac_fir_enabled(engines[0],
        dacFirEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_dac_noise_enabled(engines[0],
        dacNoiseEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_dac_true_oversample(engines[0],
        dacTrueOversample.load(std::memory_order_relaxed) ? 1 : 0);

    char buf[SPU94_PRESET_BUF_SIZE];
    int written = spu94_preset_save(
        engines[0],
        name.isNotEmpty() ? name.toRawUTF8() : nullptr,
        description.isNotEmpty() ? description.toRawUTF8() : nullptr,
        buf, sizeof(buf));
    if (written < 0) return {};
    return juce::String(buf, static_cast<size_t>(written));
}

bool SPU94AudioProcessor::loadPresetFromString(const juce::String& presetText)
{
    if (presetText.isEmpty() || !engines[0]) return false;
    auto raw = presetText.toRawUTF8();
    auto len = presetText.getNumBytesAsUTF8();
    if (len == 0 || len >= sizeof(pendingPresetBuf)) return false;
    std::memcpy(pendingPresetBuf.data(), raw, len);
    pendingPresetBuf[len] = '\0';
    pendingPresetLen.store(len, std::memory_order_relaxed);
    filePresetReady.store(true, std::memory_order_release);
    return true;
}

int SPU94AudioProcessor::getFilePresetAppliedCount() const
{
    return filePresetAppliedCount.load(std::memory_order_acquire);
}

void SPU94AudioProcessor::loadWavFile(const juce::File& file)
{
    auto result = WavLoader::load(file);
    if (!result.has_value())
        return;

    // Write into whichever slot is NOT currently being consumed by the
    // audio thread.  The slot flip + release fence on newWavReady makes
    // the data visible before the audio thread sees the flag (CR-01).
    const auto slot = static_cast<size_t>(1 - pendingWriteSlot.load(std::memory_order_relaxed));
    pendingSlots[slot].L = std::move(result->L);
    pendingSlots[slot].R = std::move(result->R);
    pendingSlots[slot].numFrames = result->numFrames;
    pendingWriteSlot.store(slot, std::memory_order_relaxed);
    newWavReady.store(true, std::memory_order_release);
}

void SPU94AudioProcessor::startPlayback()
{
    wavSource.playing.store(true, std::memory_order_relaxed);
}

void SPU94AudioProcessor::stopPlayback()
{
    wavSource.playing.store(false, std::memory_order_relaxed);
    wavSource.playPos.store(0, std::memory_order_relaxed);
}

bool SPU94AudioProcessor::isPlaying() const
{
    return wavSource.playing.load(std::memory_order_relaxed);
}

bool SPU94AudioProcessor::isLoaded() const
{
    return wavSource.loaded.load(std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SPU94AudioProcessor::createEditor()
{
    return new SPU94AudioProcessorEditor(*this);
}

int SPU94AudioProcessor::getNumPrograms()
{
    return 1;
}

int SPU94AudioProcessor::getCurrentProgram()
{
    return 0;
}

void SPU94AudioProcessor::setCurrentProgram(int /*index*/)
{
}

const juce::String SPU94AudioProcessor::getProgramName(int /*index*/)
{
    return {};
}

void SPU94AudioProcessor::changeProgramName(int /*index*/,
                                             const juce::String& /*newName*/)
{
}

void SPU94AudioProcessor::getStateInformation(juce::MemoryBlock& /*destData*/)
{
    // Plan 03 fills this with register state serialization.
}

void SPU94AudioProcessor::setStateInformation(const void* /*data*/, int /*sizeInBytes*/)
{
    // Plan 03 fills this with register state deserialization.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SPU94AudioProcessor();
}
