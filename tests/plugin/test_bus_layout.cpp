// tests/plugin/test_bus_layout.cpp
//
// Phase 25 / PLUG-32, PLUG-35: bus layout whitelist unit tests.
//
// Instantiates SPU94AudioProcessor and verifies that
// checkBusesLayoutSupported returns true for exactly three
// configurations (mono-mono, mono-stereo, stereo-stereo) and
// false for all others.

#include "PluginProcessor.h"
#include <JuceHeader.h>

#include <cstdio>
#include <memory>

namespace {

// Helper: build a BusesLayout with given input and output channel sets.
juce::AudioProcessor::BusesLayout makeLayout(
    const juce::AudioChannelSet& in,
    const juce::AudioChannelSet& out)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(in);
    layout.outputBuses.add(out);
    return layout;
}

// ---------------------------------------------------------------
// Test 1: stereo in -> stereo out (accepted)
// ---------------------------------------------------------------
bool test_accepts_stereo_stereo()
{
    std::printf("test_accepts_stereo_stereo... ");
    auto proc = std::make_unique<SPU94AudioProcessor>();
    auto layout = makeLayout(juce::AudioChannelSet::stereo(),
                             juce::AudioChannelSet::stereo());
    bool result = proc->checkBusesLayoutSupported(layout);
    std::printf("%s\n", result ? "PASSED" : "FAILED");
    return result;
}

// ---------------------------------------------------------------
// Test 2: mono in -> stereo out (accepted)
// ---------------------------------------------------------------
bool test_accepts_mono_stereo()
{
    std::printf("test_accepts_mono_stereo... ");
    auto proc = std::make_unique<SPU94AudioProcessor>();
    auto layout = makeLayout(juce::AudioChannelSet::mono(),
                             juce::AudioChannelSet::stereo());
    bool result = proc->checkBusesLayoutSupported(layout);
    std::printf("%s\n", result ? "PASSED" : "FAILED");
    return result;
}

// ---------------------------------------------------------------
// Test 3: mono in -> mono out (accepted)
// ---------------------------------------------------------------
bool test_accepts_mono_mono()
{
    std::printf("test_accepts_mono_mono... ");
    auto proc = std::make_unique<SPU94AudioProcessor>();
    auto layout = makeLayout(juce::AudioChannelSet::mono(),
                             juce::AudioChannelSet::mono());
    bool result = proc->checkBusesLayoutSupported(layout);
    std::printf("%s\n", result ? "PASSED" : "FAILED");
    return result;
}

// ---------------------------------------------------------------
// Test 4: stereo in -> mono out (rejected)
// ---------------------------------------------------------------
bool test_rejects_stereo_mono()
{
    std::printf("test_rejects_stereo_mono... ");
    auto proc = std::make_unique<SPU94AudioProcessor>();
    auto layout = makeLayout(juce::AudioChannelSet::stereo(),
                             juce::AudioChannelSet::mono());
    bool result = !proc->checkBusesLayoutSupported(layout);
    std::printf("%s\n", result ? "PASSED" : "FAILED");
    return result;
}

// ---------------------------------------------------------------
// Test 5: disabled in or disabled out (rejected)
// ---------------------------------------------------------------
bool test_rejects_disabled()
{
    std::printf("test_rejects_disabled... ");
    auto proc = std::make_unique<SPU94AudioProcessor>();
    bool ok = true;

    // Disabled input, stereo output
    {
        auto layout = makeLayout(juce::AudioChannelSet::disabled(),
                                 juce::AudioChannelSet::stereo());
        if (proc->checkBusesLayoutSupported(layout))
        {
            std::printf("\n  FAIL: accepted disabled input + stereo output");
            ok = false;
        }
    }

    // Stereo input, disabled output
    {
        auto layout = makeLayout(juce::AudioChannelSet::stereo(),
                                 juce::AudioChannelSet::disabled());
        if (proc->checkBusesLayoutSupported(layout))
        {
            std::printf("\n  FAIL: accepted stereo input + disabled output");
            ok = false;
        }
    }

    // Both disabled
    {
        auto layout = makeLayout(juce::AudioChannelSet::disabled(),
                                 juce::AudioChannelSet::disabled());
        if (proc->checkBusesLayoutSupported(layout))
        {
            std::printf("\n  FAIL: accepted both disabled");
            ok = false;
        }
    }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------
// Test 6: 5.1 surround in + 5.1 out (rejected)
// ---------------------------------------------------------------
bool test_rejects_surround()
{
    std::printf("test_rejects_surround... ");
    auto proc = std::make_unique<SPU94AudioProcessor>();
    auto layout = makeLayout(juce::AudioChannelSet::create5point1(),
                             juce::AudioChannelSet::create5point1());
    bool result = !proc->checkBusesLayoutSupported(layout);
    std::printf("%s\n", result ? "PASSED" : "FAILED");
    return result;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf("==== Phase 25 / PLUG-32, PLUG-35: bus layout whitelist ====\n\n");

    int failures = 0;
    constexpr int total = 6;

    if (!test_accepts_stereo_stereo()) ++failures;
    if (!test_accepts_mono_stereo())   ++failures;
    if (!test_accepts_mono_mono())     ++failures;
    if (!test_rejects_stereo_mono())   ++failures;
    if (!test_rejects_disabled())      ++failures;
    if (!test_rejects_surround())      ++failures;

    std::printf("\n%d/%d tests passed.\n", total - failures, total);
    return failures > 0 ? 1 : 0;
}
