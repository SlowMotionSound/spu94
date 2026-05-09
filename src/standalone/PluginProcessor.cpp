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
    for (int e = 0; e < 2; ++e)
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
    // Tear down both engines before reinitializing (WR-03 extended for dual-engine)
    for (int e = 0; e < 2; ++e)
    {
        if (engines[e] != nullptr)
        {
            spu94_destroy(engines[e]);
            engines[e] = nullptr;
        }
    }

    // Allocate caller-owned SPU state and work buffers for both engines
    stateBufA.allocate(SPU94_STATE_SIZE_MAX, true);
    workBufA.allocate(SPU94_WORK_BUF_MAX_BYTES, true);
    stateBufB.allocate(SPU94_STATE_SIZE_MAX, true);
    workBufB.allocate(SPU94_WORK_BUF_MAX_BYTES, true);

    engines[0] = spu94_init(stateBufA.getData(), SPU94_STATE_SIZE_MAX,
                            workBufA.getData(), SPU94_WORK_BUF_MAX_BYTES);
    engines[1] = spu94_init(stateBufB.getData(), SPU94_STATE_SIZE_MAX,
                            workBufB.getData(), SPU94_WORK_BUF_MAX_BYTES);

    if (engines[0] == nullptr || engines[1] == nullptr)
        return;

    // Both engines start with Hall preset (same initial state)
    spu94_load_preset(engines[0], SPU94_PRESET_HALL);
    spu94_load_preset(engines[1], SPU94_PRESET_HALL);
    registerBridge.syncShadowsFromSPU(engines[0]);
    lastMorphPosition = -1.0f;  // force morph apply on first processBlock
    morphInitialized = false;

    // Mixer defaults on BOTH engines (Pitfall 3: prevent timbral discontinuity)
    for (int e = 0; e < 2; ++e)
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
    for (int e = 0; e < 2; ++e)
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
        engines[0] == nullptr || engines[1] == nullptr)
    {
        buffer.clear();
        return;
    }

    // 1. Drain preset command queue (GUI thread may have requested a switch)
    if (presetQueue.drain(engines[0]))
    {
        // Preset was applied on audio thread. GUI thread will observe
        // appliedCount change and re-sync slider positions via Timer.
        registerBridge.syncShadowsFromSPU(engines[0]);
    }

    // 1b. Drain file-preset load request (GUI thread may have loaded a .spu94 file)
    if (filePresetReady.load(std::memory_order_acquire))
    {
        size_t len = pendingPresetLen.load(std::memory_order_relaxed);
        spu94_preset_load(engines[0], pendingPresetBuf.data(), len);
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
    if (!morphActive.load(std::memory_order_relaxed))
        registerBridge.pushPendingRegisterWrites(engines[0]);

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
    int16_t tmpL_out[kMaxBlock];
    int16_t tmpR_out[kMaxBlock];

    // Push mixer/DAC state to active engine
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

    // Morph position: compute target registers on scratch engine, arm
    // per-sample register slewing in the C core (Phase 18).
    if (morphActive.load(std::memory_order_relaxed))
    {
        float pos = morphPosition.load(std::memory_order_relaxed);
        if (pos != lastMorphPosition)
        {
            lastMorphPosition = pos;

            if (!morphInitialized)
            {
                spu94_interp_set_morph(engines[0], pos);
                morphInitialized = true;
            }
            else
            {
                spu94_interp_set_morph(engines[1], pos);
                spu94_apply_pending_writes(engines[1]);
                spu94_snapshot_registers(engines[1], targetRegs);
                spu94_set_slew_targets(engines[0], targetRegs);
            }
        }
    }

    auto playPos = wavSource.playPos.load(std::memory_order_relaxed);
    for (int i = 0; i < samplesToProcess; ++i)
    {
        const auto idx = static_cast<size_t>((playPos + static_cast<uint64_t>(i)) % numFrames);
        tmpL_in[i] = wavSource.L[idx];
        tmpR_in[i] = wavSource.R[idx];
    }

    // Single engine processes audio — registers are slewing smoothly
    spu94_process(engines[0], tmpL_in, tmpR_in, tmpL_out, tmpR_out,
                  static_cast<uint32_t>(samplesToProcess));

    // Output conversion (unchanged -- reads from tmpL_out/tmpR_out)
    auto* outL = buffer.getWritePointer(0);
    auto* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < samplesToProcess; ++i)
    {
        outL[i] = tmpL_out[i] / 32768.0f;
        if (outR) outR[i] = tmpR_out[i] / 32768.0f;
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
