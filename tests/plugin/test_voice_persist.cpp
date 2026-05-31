// tests/plugin/test_voice_persist.cpp
//
// Phase 63 (v1.12.0 Voice Count, VCOUNT-04): voice-count text-preset persistence.
//
// First automated coverage of the plugin-layer `.spu94` text-preset round-trip.
// Verifies that SPU94AudioProcessor::savePresetToString writes the active voice
// count into the [voice] section and loadPresetFromString restores it through
// setActiveVoiceCount (clamp [1,24], routed via the public getActiveVoiceCount
// getter). Headless processor — no audio render, no GUI, no MIDI.
//
// Canonical key string: "active_voices" (decimal int, mirrors the non=/noise_shift
// idiom in the same [voice] block). The save literal and the parse `key ==` literal
// MUST stay byte-identical (63-RESEARCH Pitfall 4); the round-trip case below proves
// it (save 7 -> reload -> assert 7).
//
// NOTE: loadPresetFromString early-returns false unless engines[0] is live, and
// engines[0] is created in prepareToPlay (not the ctor). So every instance must be
// prepared before save/load or the load is a silent no-op (the count never changes).
// test_state_roundtrip.cpp doesn't need this because it exercises the BINARY
// setStateInformation path, which doesn't gate on engines[0].
//
// Three CTest cases, one per argv selector (argv[1] == case name):
//   voice_persist_roundtrip   VCOUNT-04 criterion 1 (+2 headless half)
//   voice_persist_backcompat  VCOUNT-04 criterion 3 / Pitfall 1 (seed-then-override)
//   voice_persist_clamp       clamp guard (malformed values land in [1,24])
//
// All three are RED until Task 2 adds the save line + seed-then-override parse +
// setActiveVoiceCount restore: round-trip and clamp fail because no save/parse
// exists yet; back-compat fails because the seed-then-override is not yet in place
// (the instance stays at its pre-load count of 5). This is the intended baseline.

#include "PluginProcessor.h"
#include <JuceHeader.h>

#include <cstdio>
#include <memory>

namespace {

// Construct a processor and bring its SPU engines online (prepareToPlay creates
// engines[0]; without it loadPresetFromString early-returns and never restores).
std::unique_ptr<SPU94AudioProcessor> makePreparedProcessor()
{
    auto p = std::make_unique<SPU94AudioProcessor>();
    p->prepareToPlay(44100.0, 512);
    return p;
}

// Round-trip (criterion 1 + the headless half of 2): instance A is set to a
// non-default count, saved to text, then loaded into a fresh instance B; B must
// report the same count. Two-instance style mirrors test_state_roundtrip.cpp.
bool test_roundtrip()
{
    std::printf("voice_persist_roundtrip... ");
    bool ok = true;

    auto a = makePreparedProcessor();
    a->setActiveVoiceCount(7);
    auto text = a->savePresetToString("t", "");

    auto b = makePreparedProcessor();
    b->loadPresetFromString(text);
    if (b->getActiveVoiceCount() != 7)
    {
        std::printf("\n  FAIL: round-trip got %d expected 7", b->getActiveVoiceCount());
        ok = false;
    }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// Back-compat (criterion 3 / Pitfall 1): a [voice] section with NO count key,
// loaded after setting a non-24 count, must restore to 24 (the full rig) rather
// than leave the instance's current count. The literal deliberately OMITS the
// active_voices key — its absence is the test.
bool test_backcompat()
{
    std::printf("voice_persist_backcompat... ");
    bool ok = true;

    auto p = makePreparedProcessor();
    p->setActiveVoiceCount(5);   // current count != 24

    // Minimal valid text with a [voice] section but NO active_voices key.
    const juce::String noKey = "[preset]\nname=back\n[voice]\nnon=0\n";
    p->loadPresetFromString(noKey);
    if (p->getActiveVoiceCount() != 24)
    {
        std::printf("\n  FAIL: back-compat got %d expected 24", p->getActiveVoiceCount());
        ok = false;
    }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// Clamp guard: malformed count values land safely inside [1,24] because the
// restore routes through setActiveVoiceCount (juce::jlimit). active_voices=0 -> 1,
// active_voices=999 -> 24. getIntValue() never throws (junk -> 0 -> clamps to 1).
bool test_clamp()
{
    std::printf("voice_persist_clamp... ");
    bool ok = true;

    {
        auto p = makePreparedProcessor();
        const juce::String low = "[preset]\nname=lo\n[voice]\nactive_voices=0\n";
        p->loadPresetFromString(low);
        if (p->getActiveVoiceCount() != 1)
        {
            std::printf("\n  FAIL: clamp low got %d expected 1", p->getActiveVoiceCount());
            ok = false;
        }
    }
    {
        auto p = makePreparedProcessor();
        const juce::String high = "[preset]\nname=hi\n[voice]\nactive_voices=999\n";
        p->loadPresetFromString(high);
        if (p->getActiveVoiceCount() != 24)
        {
            std::printf("\n  FAIL: clamp high got %d expected 24", p->getActiveVoiceCount());
            ok = false;
        }
    }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 2)
    {
        std::printf("usage: %s <case-name>\n", argv[0]);
        return 2;
    }
    const juce::String caseName(argv[1]);

    bool ok = false;
    if      (caseName == "voice_persist_roundtrip")  ok = test_roundtrip();
    else if (caseName == "voice_persist_backcompat") ok = test_backcompat();
    else if (caseName == "voice_persist_clamp")      ok = test_clamp();
    else
    {
        std::printf("unknown case: %s\n", argv[1]);
        return 2;
    }

    return ok ? 0 : 1;
}
