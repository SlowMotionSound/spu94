// tests/plugin/test_voice_alloc.cpp
//
// Phase 60 (v1.12.0 Voice Count): active-voice-count allocation tests.
//
// Verifies SPU94AudioProcessor::allocateVoice respects the active voice count
// (1..24) set via setActiveVoiceCount, observing the allocation decision
// directly through the voice mixer's synchronously-set pending_kon /
// pending_koff bitmasks (spu94_get_voice_mixer()), with no audio render.
//
// Five CTest cases, one per argv selector (argv[1] == case name):
//   voice_alloc_mono_vs_poly        VCOUNT-02
//   voice_alloc_only_active         VALLOC-01
//   voice_alloc_steal_oldest        VALLOC-02
//   voice_alloc_mono_takeover       VALLOC-03
//   voice_alloc_default24_regression  (regression guard for the % 24 -> % count edit)
//
// The four count-sensitive cases FAIL while allocateVoice still uses % 24
// (Task 2 RED state) and PASS once Task 3 bounds the modulus to the count
// (GREEN). default24_regression passes in both states and proves the default
// path is unchanged.
//
// Pitfalls handled (60-RESEARCH.md):
//  - #2: the mixer is a process-wide singleton (s_mixer). Each case re-inits it
//        and resets the processor's noteForVoice[]/nextVoice via the friend seam.
//  - #3: allocateVoice is reached DIRECTLY via the friend, bypassing the
//        voiceSampleLoaded gate and processBlock entirely.
//  - #4: allocateVoice / findVoiceForNote / noteForVoice / nextVoice are private;
//        struct VoiceAllocTest is the granted friend.

#include "PluginProcessor.h"
#include <JuceHeader.h>

extern "C" {
#include <spu94/spu94_voice.h>
}

#include <cstdint>
#include <cstdio>
#include <memory>

// ---------------------------------------------------------------------------
// Test seam: friend struct granted access in PluginProcessor.h. Thin forwarders
// to the private allocator members the cases need.
// ---------------------------------------------------------------------------
struct VoiceAllocTest
{
    SPU94AudioProcessor& p;
    explicit VoiceAllocTest(SPU94AudioProcessor& proc) : p(proc) {}

    int  alloc(int note)        { return p.allocateVoice(note); }
    void setCount(int n)        { p.setActiveVoiceCount(n); }
    int  cursor() const         { return p.nextVoice; }
    int8_t noteOnVoice(int v) const { return p.noteForVoice[v]; }

    // Reset the processor's per-instance allocator state (Pitfall 2).
    void resetProcessorState()
    {
        for (int i = 0; i < 24; ++i)
            p.noteForVoice[i] = -1;
        p.nextVoice = 0;
    }
};

namespace {

// Re-isolate before every case: the voice mixer is a process-wide singleton, so
// pending_kon/pending_koff must be cleared and the processor cursor/notes reset.
void isolate(VoiceAllocTest& t)
{
    spu94_voice_mixer_init(spu94_get_voice_mixer());
    t.resetProcessorState();
}

// Result of one production-shaped allocation step.
struct AllocStep
{
    int      voice;        // index allocateVoice returned
    uint32_t stoleKoff;    // pending_koff snapshot taken AFTER allocateVoice but
                           // BEFORE key_on -- this is the steal/takeover observable.
};

// Replicate the production allocate + key_on pair (PluginProcessor.cpp:1426-1430).
// CRITICAL: spu94_voice_mixer_key_on clears the pending_koff bit for the voice it
// keys on (spu94_voice.c:472). Since a steal key-offs and then keys on the SAME
// voice, the key_on would wipe the steal bit. So we snapshot pending_koff right
// after allocateVoice (which emits the steal) and before key_on. pending_kon is
// then read normally after key_on to confirm the allocation.
AllocStep allocAndKeyOn(VoiceAllocTest& t, int note)
{
    auto* mx = spu94_get_voice_mixer();
    int voice = t.alloc(note);                  // may emit a steal key-off on `voice`
    AllocStep step{voice, mx->pending_koff};     // capture the steal before key_on clears it
    // Any in-range pitch/volume is fine; we only observe the pending bitmasks.
    spu94_voice_mixer_key_on(mx, voice,
                             /*start_addr*/ 0,
                             /*pitch     */ (uint16_t)0x1000,
                             /*vol_l     */ (int16_t)0x4000,
                             /*vol_r     */ (int16_t)0x4000,
                             /*reverb_on */ 1,
                             /*adsr_cfg  */ nullptr);
    return step;
}

uint32_t kon()  { return spu94_get_voice_mixer()->pending_kon; }

// ---------------------------------------------------------------------------
// VCOUNT-02: count=1 routes every note to voice 0; count=N spreads 0..N-1.
// ---------------------------------------------------------------------------
bool test_mono_vs_poly(VoiceAllocTest& t)
{
    std::printf("voice_alloc_mono_vs_poly... ");
    bool ok = true;

    // Mono: count=1 -> every allocation is voice 0, only pending_kon bit 0 set.
    isolate(t);
    t.setCount(1);
    const int monoNotes[] = {60, 64, 67, 72, 55};
    for (int n : monoNotes)
    {
        int v = allocAndKeyOn(t, n).voice;
        if (v != 0) { std::printf("\n  FAIL: mono note %d -> voice %d (expected 0)", n, v); ok = false; }
    }
    if (kon() != 0x1u) { std::printf("\n  FAIL: mono pending_kon=0x%X (expected 0x1)", kon()); ok = false; }

    // Poly: count=6 -> six distinct notes land on voices 0,1,2,3,4,5 in order.
    isolate(t);
    t.setCount(6);
    for (int i = 0; i < 6; ++i)
    {
        int v = allocAndKeyOn(t, 60 + i).voice;
        if (v != i) { std::printf("\n  FAIL: poly note #%d -> voice %d (expected %d)", i, v, i); ok = false; }
    }
    if (kon() != 0x3Fu) { std::printf("\n  FAIL: poly pending_kon=0x%X (expected 0x3F)", kon()); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// VALLOC-01: with count=N, no allocation ever keys on a voice index >= N.
// ---------------------------------------------------------------------------
bool test_only_active(VoiceAllocTest& t)
{
    std::printf("voice_alloc_only_active... ");
    bool ok = true;

    const int N = 5;
    isolate(t);
    t.setCount(N);
    for (int i = 0; i < 8; ++i)   // allocate more notes than active voices
        (void)allocAndKeyOn(t, 60 + i);

    const uint32_t outOfRange = kon() & ~((1u << N) - 1u);
    if (outOfRange != 0) { std::printf("\n  FAIL: keyed-on bit(s) >= N: pending_kon=0x%X", kon()); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// VALLOC-02: the (N+1)th note reuses the least-recently-allocated voice (0):
// pending_koff bit 0 set (steal of prior note), pending_kon bit 0 set (new note).
// ---------------------------------------------------------------------------
bool test_steal_oldest(VoiceAllocTest& t)
{
    std::printf("voice_alloc_steal_oldest... ");
    bool ok = true;

    const int N = 4;
    isolate(t);
    t.setCount(N);

    for (int i = 0; i < N; ++i)   // fill voices 0..N-1
        (void)allocAndKeyOn(t, 60 + i);

    // The (N+1)th note must reuse voice 0 (least-recently-allocated): the steal
    // key-off (captured before key_on clears it) and the new key-on are both bit 0.
    AllocStep s = allocAndKeyOn(t, 60 + N);
    if (s.voice != 0)               { std::printf("\n  FAIL: overflow note -> voice %d (expected 0)", s.voice); ok = false; }
    if ((s.stoleKoff & 0x1u) == 0)  { std::printf("\n  FAIL: steal pending_koff bit 0 not set: 0x%X", s.stoleKoff); ok = false; }
    if ((kon() & 0x1u) == 0)        { std::printf("\n  FAIL: pending_kon bit 0 not set (no key-on): 0x%X", kon()); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// VALLOC-03: count=1; note B takes over voice 0 from note A (pending_koff bit 0).
// ---------------------------------------------------------------------------
bool test_mono_takeover(VoiceAllocTest& t)
{
    std::printf("voice_alloc_mono_takeover... ");
    bool ok = true;

    isolate(t);
    t.setCount(1);

    int vA = allocAndKeyOn(t, 60).voice;   // note A -> voice 0
    if (vA != 0) { std::printf("\n  FAIL: note A -> voice %d (expected 0)", vA); ok = false; }

    // Note B must take over voice 0: the second allocation key-offs A (steal),
    // captured before the new key_on clears the bit.
    AllocStep b = allocAndKeyOn(t, 64);
    if (b.voice != 0)               { std::printf("\n  FAIL: note B -> voice %d (expected 0)", b.voice); ok = false; }
    if ((b.stoleKoff & 0x1u) == 0)  { std::printf("\n  FAIL: takeover pending_koff bit 0 not set: 0x%X", b.stoleKoff); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// Regression: default count 24 reproduces the exact pre-change round-robin.
// 24 note-ons -> voices 0..23 in order; 25th wraps to 0 and key-offs voice 0.
// ---------------------------------------------------------------------------
bool test_default24_regression(VoiceAllocTest& t)
{
    std::printf("voice_alloc_default24_regression... ");
    bool ok = true;

    isolate(t);
    t.setCount(24);   // explicit, though 24 is the default

    for (int i = 0; i < 24; ++i)
    {
        int v = allocAndKeyOn(t, 36 + i).voice;
        if (v != i) { std::printf("\n  FAIL: note #%d -> voice %d (expected %d)", i, v, i); ok = false; }
    }
    if (kon() != 0x00FFFFFFu) { std::printf("\n  FAIL: pending_kon=0x%X (expected 0x00FFFFFF)", kon()); ok = false; }

    // 25th note wraps to voice 0 and steals (key-offs) the note that was there
    // (steal captured before the new key_on clears the koff bit).
    AllocStep s = allocAndKeyOn(t, 60);
    if (s.voice != 0)              { std::printf("\n  FAIL: 25th note -> voice %d (expected 0)", s.voice); ok = false; }
    if ((s.stoleKoff & 0x1u) == 0) { std::printf("\n  FAIL: 25th note did not key-off voice 0: 0x%X", s.stoleKoff); ok = false; }

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

    auto proc = std::make_unique<SPU94AudioProcessor>();
    VoiceAllocTest t(*proc);

    bool ok = false;
    if      (caseName == "voice_alloc_mono_vs_poly")          ok = test_mono_vs_poly(t);
    else if (caseName == "voice_alloc_only_active")           ok = test_only_active(t);
    else if (caseName == "voice_alloc_steal_oldest")          ok = test_steal_oldest(t);
    else if (caseName == "voice_alloc_mono_takeover")         ok = test_mono_takeover(t);
    else if (caseName == "voice_alloc_default24_regression")  ok = test_default24_regression(t);
    else
    {
        std::printf("unknown case: %s\n", argv[1]);
        return 2;
    }

    return ok ? 0 : 1;
}
