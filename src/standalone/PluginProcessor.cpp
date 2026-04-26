#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WavLoader.h"
#include <cmath>

SPU94AudioProcessor::SPU94AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
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
    // Allocate caller-owned SPU state and work buffers.
    stateBuf.allocate(SPU94_STATE_SIZE_MAX, true);
    workBuf.allocate(SPU94_WORK_BUF_MAX_BYTES, true);

    spu = spu94_init(stateBuf.getData(), SPU94_STATE_SIZE_MAX,
                     workBuf.getData(), SPU94_WORK_BUF_MAX_BYTES);

    if (spu != nullptr)
    {
        spu94_load_preset(spu, SPU94_PRESET_HALL);
        registerBridge.syncShadowsFromSPU(spu);
    }
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
    if (newWavReady.load(std::memory_order_acquire))
    {
        wavSource.L = std::move(pendingL);
        wavSource.R = std::move(pendingR);
        wavSource.numFrames = pendingFrames;
        wavSource.playPos.store(0, std::memory_order_relaxed);
        wavSource.loaded.store(true, std::memory_order_relaxed);
        newWavReady.store(false, std::memory_order_release);
    }

    if (!wavSource.loaded.load(std::memory_order_relaxed) ||
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

    // 2. Push any GUI-changed register values to the SPU
    registerBridge.pushPendingRegisterWrites(spu);

    const int n = buffer.getNumSamples();
    const auto numFrames = wavSource.numFrames;

    // Stack-allocated int16 I/O buffers -- no heap in the audio thread.
    // JUCE block sizes are typically 256-1024; 4096 is a generous ceiling.
    constexpr int kMaxBlock = 4096;
    jassert(n <= kMaxBlock);
    const int samplesToProcess = (n <= kMaxBlock) ? n : kMaxBlock;

    int16_t tmpL_in[kMaxBlock];
    int16_t tmpR_in[kMaxBlock];
    int16_t tmpL_out[kMaxBlock];
    int16_t tmpR_out[kMaxBlock];

    // Fill input from the loaded WAV data, wrapping for continuous loop.
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

    // Equal-power crossfade: dry input vs SPU wet output (D-02, STANDALONE-06).
    // sqrt pan law preserves perceived loudness across the sweep:
    //   wet=0.0 → dryGain=1.0, wetGain=0.0 (unprocessed input only)
    //   wet=0.5 → dryGain=0.707, wetGain=0.707 (constant-power midpoint)
    //   wet=1.0 → dryGain=0.0, wetGain=1.0 (SPU reverb output only)
    const float wet = wetDry.load(std::memory_order_relaxed);
    const float wetGain = std::sqrt(wet);
    const float dryGain = std::sqrt(1.0f - wet);

    auto* outL = buffer.getWritePointer(0);
    auto* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < samplesToProcess; ++i)
    {
        const float dryL = tmpL_in[i] / 32768.0f;
        const float dryR = tmpR_in[i] / 32768.0f;
        const float spuL = tmpL_out[i] / 32768.0f;
        const float spuR = tmpR_out[i] / 32768.0f;

        outL[i] = dryL * dryGain + spuL * wetGain;
        if (outR) outR[i] = dryR * dryGain + spuR * wetGain;
    }

    // Advance play position (continuous loop).
    wavSource.playPos.store((playPos + static_cast<uint64_t>(samplesToProcess)) % numFrames,
                            std::memory_order_relaxed);
}

void SPU94AudioProcessor::loadWavFile(const juce::File& file)
{
    auto result = WavLoader::load(file);
    if (!result.has_value())
        return;

    pendingL = std::move(result->L);
    pendingR = std::move(result->R);
    pendingFrames = result->numFrames;
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
