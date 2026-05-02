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
    if (spu != nullptr)
    {
        spu94_destroy(spu);
        spu = nullptr;
    }
}

const juce::String SPU94AudioProcessor::getName() const
{
    return juce::String("SPU-94");
}

void SPU94AudioProcessor::prepareToPlay(double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Tear down any existing SPU state before reinitializing (WR-03).
    // JUCE calls prepareToPlay on every audio device change; without this
    // the old spu pointer leaks its internal bookkeeping.
    if (spu != nullptr)
    {
        spu94_destroy(spu);
        spu = nullptr;
    }

    // Allocate caller-owned SPU state and work buffers.
    stateBuf.allocate(SPU94_STATE_SIZE_MAX, true);
    workBuf.allocate(SPU94_WORK_BUF_MAX_BYTES, true);

    spu = spu94_init(stateBuf.getData(), SPU94_STATE_SIZE_MAX,
                     workBuf.getData(), SPU94_WORK_BUF_MAX_BYTES);

    if (spu == nullptr)
        return;

    spu94_load_preset(spu, SPU94_PRESET_HALL);
    registerBridge.syncShadowsFromSPU(spu);

    // Phase 7 (D-05): C core now owns mixing. Set sensible defaults
    // so audio is audible immediately (faders default to 0x0000 = silence).
    spu94_set_input_gain(spu,   0x7FFF);  // full input level
    spu94_set_dry_fader(spu,    0x7FFF);  // dry bus at unity
    spu94_set_reverb_fader(spu, 0x7FFF);  // reverb at unity
    spu94_set_dry_send(spu,     0x7FFF);  // dry bus feeds reverb at unity
    // patina_fader and patina_send stay at 0 until ADPCM is enabled

    // Latency comp ON by default (D-07)
    spu94_set_latency_comp(spu, 1);
}

void SPU94AudioProcessor::releaseResources()
{
    if (spu != nullptr)
    {
        spu94_destroy(spu);
        spu = nullptr;
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
        spu == nullptr)
    {
        buffer.clear();
        return;
    }

    // 1. Drain preset command queue (GUI thread may have requested a switch)
    if (presetQueue.drain(spu))
    {
        // Preset was applied on audio thread. GUI thread will observe
        // appliedCount change and re-sync slider positions via Timer.
        registerBridge.syncShadowsFromSPU(spu);
    }

    // 1b. Drain file-preset load request (GUI thread may have loaded a .spu94 file)
    if (filePresetReady.load(std::memory_order_acquire))
    {
        size_t len = pendingPresetLen.load(std::memory_order_relaxed);
        spu94_preset_load(spu, pendingPresetBuf.data(), len);
        registerBridge.syncShadowsFromSPU(spu);
        filePresetReady.store(false, std::memory_order_release);
        filePresetAppliedCount.fetch_add(1, std::memory_order_release);

        // Sync mixer/DAC atomics from loaded SPU state so GUI reflects the file's values
        inputLevel.store(static_cast<float>(spu94_get_input_gain(spu)) / 0x7FFF, std::memory_order_relaxed);
        dryLevel.store(static_cast<float>(spu94_get_dry_fader(spu)) / 0x7FFF, std::memory_order_relaxed);
        patinaLevel.store(static_cast<float>(spu94_get_patina_fader(spu)) / 0x7FFF, std::memory_order_relaxed);
        reverbLevel.store(static_cast<float>(spu94_get_reverb_fader(spu)) / 0x7FFF, std::memory_order_relaxed);
        drySend.store(static_cast<float>(spu94_get_dry_send(spu)) / 0x7FFF, std::memory_order_relaxed);
        adpcmSend.store(static_cast<float>(spu94_get_patina_send(spu)) / 0x7FFF, std::memory_order_relaxed);
        latencyCompEnabled.store(spu94_get_latency_comp(spu) != 0, std::memory_order_relaxed);
        dacEnabled.store(spu94_get_dac_enabled(spu) != 0, std::memory_order_relaxed);
        dacFirEnabled.store(spu94_get_dac_fir_enabled(spu) != 0, std::memory_order_relaxed);
        dacNoiseEnabled.store(spu94_get_dac_noise_enabled(spu) != 0, std::memory_order_relaxed);
        dacTrueOversample.store(spu94_get_dac_true_oversample(spu) != 0, std::memory_order_relaxed);
    }

    // 2. Push any GUI-changed register values to the SPU
    registerBridge.pushPendingRegisterWrites(spu);

    // 3. ADPCM coloration toggle (ADPCM-IO-06, D-06): read GUI atomic,
    // push to C API. Takes effect on the next spu94_process block.
    // ADPCM auto-enables when patina fader or ADPCM send is non-zero.
    // The old dedicated toggle was removed (D-08); the mixer controls
    // now implicitly drive ADPCM on/off.
    {
        const bool patina_active =
            patinaLevel.load(std::memory_order_relaxed) > 0.0f ||
            adpcmSend.load(std::memory_order_relaxed) > 0.0f;
        spu94_set_adpcm_enabled(spu, patina_active ? 1 : 0);
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

    // Push host input level to C core mixer (D-05: float -> Q15 at boundary).
    // The C core's input_gain handles attenuation; host passes full-scale samples.
    spu94_set_input_gain(spu, static_cast<int16_t>(
        inputLevel.load(std::memory_order_relaxed) * 0x7FFF));

    // Mixer faders: float -> Q15 at host boundary (D-05)
    spu94_set_dry_fader(spu, static_cast<int16_t>(
        dryLevel.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_patina_fader(spu, static_cast<int16_t>(
        patinaLevel.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_reverb_fader(spu, static_cast<int16_t>(
        reverbLevel.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_dry_send(spu, static_cast<int16_t>(
        drySend.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_patina_send(spu, static_cast<int16_t>(
        adpcmSend.load(std::memory_order_relaxed) * 0x7FFF));

    // Latency compensation
    spu94_set_latency_comp(spu,
        latencyCompEnabled.load(std::memory_order_relaxed) ? 1 : 0);

    // DAC coloration section
    spu94_set_dac_enabled(spu,
        dacEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_dac_fir_enabled(spu,
        dacFirEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_dac_noise_enabled(spu,
        dacNoiseEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_dac_true_oversample(spu,
        dacTrueOversample.load(std::memory_order_relaxed) ? 1 : 0);

    auto playPos = wavSource.playPos.load(std::memory_order_relaxed);
    for (int i = 0; i < samplesToProcess; ++i)
    {
        const auto idx = static_cast<size_t>((playPos + static_cast<uint64_t>(i)) % numFrames);
        tmpL_in[i] = wavSource.L[idx];
        tmpR_in[i] = wavSource.R[idx];
    }

    // Feed through the SPU reverb.
    spu94_process(spu, tmpL_in, tmpR_in, tmpL_out, tmpR_out,
                  static_cast<uint32_t>(samplesToProcess));

    // C core mixer owns all mixing (D-02). Straight passthrough.
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
    if (!spu) return {};
    char buf[SPU94_PRESET_BUF_SIZE];
    int written = spu94_preset_save(
        spu,
        name.isNotEmpty() ? name.toRawUTF8() : nullptr,
        description.isNotEmpty() ? description.toRawUTF8() : nullptr,
        buf, sizeof(buf));
    if (written < 0) return {};
    return juce::String(buf, static_cast<size_t>(written));
}

bool SPU94AudioProcessor::loadPresetFromString(const juce::String& presetText)
{
    if (presetText.isEmpty() || !spu) return false;
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
