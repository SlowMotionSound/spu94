// tests/plugin/test_mono_sum.cpp
//
// Phase 25 / PLUG-34: mono output summing unit test.
//
// Verifies that processBlock called with a 1-channel AudioBuffer
// (mono output path) does not crash, produces no NaN/Inf, and
// yields output within a finite range. The SPU-94 reverb engine
// processes internally in stereo; the mono path sums (L+R)*0.5
// into the single output channel.

#include "PluginProcessor.h"
#include <JuceHeader.h>

#include <cmath>
#include <cstdio>
#include <memory>

namespace {

// ---------------------------------------------------------------
// Test 1: processBlock with 1-channel buffer -- no crash, finite
// ---------------------------------------------------------------
bool test_mono_processblock_no_crash()
{
    std::printf("test_mono_processblock_no_crash... ");

    auto proc = std::make_unique<SPU94AudioProcessor>();

    // Negotiate mono-mono layout before prepareToPlay.
    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
    proc->setBusesLayout(monoLayout);

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    proc->prepareToPlay(sampleRate, blockSize);

    // Feed multiple blocks to let the reverb engine settle.
    // Use a constant DC input of 0.25f (gentle level, well below clipping).
    constexpr int numBlocks = 10;
    bool ok = true;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer(1, blockSize);

        // Fill with known signal
        for (int i = 0; i < blockSize; ++i)
            buffer.setSample(0, i, 0.25f);

        juce::MidiBuffer midi;
        proc->processBlock(buffer, midi);

        // Verify output is finite and within reasonable range
        const float* out = buffer.getReadPointer(0);
        for (int i = 0; i < blockSize; ++i)
        {
            if (std::isnan(out[i]))
            {
                std::printf("\n  FAIL: NaN at block %d, sample %d", b, i);
                ok = false;
                break;
            }
            if (std::isinf(out[i]))
            {
                std::printf("\n  FAIL: Inf at block %d, sample %d", b, i);
                ok = false;
                break;
            }
            if (out[i] > 10.0f || out[i] < -10.0f)
            {
                std::printf("\n  FAIL: out of range (%.4f) at block %d, sample %d",
                            out[i], b, i);
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }

    proc->releaseResources();
    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------
// Test 2: mono output has non-zero content after settling
// ---------------------------------------------------------------
bool test_mono_output_nonzero()
{
    std::printf("test_mono_output_nonzero... ");

    auto proc = std::make_unique<SPU94AudioProcessor>();

    // Negotiate mono-mono layout.
    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
    proc->setBusesLayout(monoLayout);

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    proc->prepareToPlay(sampleRate, blockSize);

    // Feed blocks with nonzero input; check that output eventually
    // becomes nonzero (reverb tail builds up after a few blocks).
    bool foundNonzero = false;
    constexpr int numBlocks = 20;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer(1, blockSize);
        for (int i = 0; i < blockSize; ++i)
            buffer.setSample(0, i, 0.25f);

        juce::MidiBuffer midi;
        proc->processBlock(buffer, midi);

        const float* out = buffer.getReadPointer(0);
        for (int i = 0; i < blockSize; ++i)
        {
            if (std::abs(out[i]) > 1e-6f)
            {
                foundNonzero = true;
                break;
            }
        }
        if (foundNonzero) break;
    }

    proc->releaseResources();
    std::printf("%s\n", foundNonzero ? "PASSED" : "FAILED (all zeros)");
    return foundNonzero;
}

// ---------------------------------------------------------------
// Test 3: mono-stereo layout processBlock -- no crash
// ---------------------------------------------------------------
bool test_mono_stereo_processblock()
{
    std::printf("test_mono_stereo_processblock... ");

    auto proc = std::make_unique<SPU94AudioProcessor>();

    // Negotiate mono-in, stereo-out layout.
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::mono());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    proc->setBusesLayout(layout);

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    proc->prepareToPlay(sampleRate, blockSize);

    // With mono-stereo, buffer has 2 channels (output is stereo)
    // but input should read from channel 0 for both L and R.
    juce::AudioBuffer<float> buffer(2, blockSize);
    for (int i = 0; i < blockSize; ++i)
        buffer.setSample(0, i, 0.25f);
    buffer.clear(1, 0, blockSize);  // output channel starts clear

    juce::MidiBuffer midi;
    proc->processBlock(buffer, midi);

    bool ok = true;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* out = buffer.getReadPointer(ch);
        for (int i = 0; i < blockSize; ++i)
        {
            if (std::isnan(out[i]) || std::isinf(out[i]))
            {
                std::printf("\n  FAIL: NaN/Inf on ch %d, sample %d", ch, i);
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }

    proc->releaseResources();
    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf("==== Phase 25 / PLUG-34: mono output summing ====\n\n");

    int failures = 0;
    constexpr int total = 3;

    if (!test_mono_processblock_no_crash()) ++failures;
    if (!test_mono_output_nonzero())        ++failures;
    if (!test_mono_stereo_processblock())   ++failures;

    std::printf("\n%d/%d tests passed.\n", total - failures, total);
    return failures > 0 ? 1 : 0;
}
