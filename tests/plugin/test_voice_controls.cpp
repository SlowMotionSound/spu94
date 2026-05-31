// tests/plugin/test_voice_controls.cpp
//
// Phase 61 (v1.12.0 Voice Count): coherent per-voice control fan-out tests.
//
// Verifies that the sampler's continuous GUI controls (Level, Pan, INV via
// base_vol; NON; PMON) reach EVERY active voice in [0, activeVoiceCount), not
// just voice 0. The harness drives the extracted private method
// SPU94AudioProcessor::applyContinuousVoiceControls() directly through a
// friend struct, then observes the process-wide voice mixer's state
// (spu94_get_voice_mixer()->voices[v].base_vol_l/r, ->non_flags, ->pmon_flags)
// with NO audio render -- the same proof technique Phase 60 used for allocation.
//
// Eight CTest cases, one per argv selector (argv[1] == case name):
//   voice_controls_level_all_active     VCTRL-01   (count-sensitive -> RED pre-impl)
//   voice_controls_pan_all_active       VCTRL-02   (count-sensitive -> RED pre-impl)
//   voice_controls_non_pmon_all_active  VCTRL-03   (count-sensitive -> RED pre-impl)
//   voice_controls_adsr_shared          VCTRL-03/D-07  (GUARD -> may PASS pre-impl)
//   voice_controls_velocity_rides_level D-01       (count-sensitive -> RED pre-impl)
//   voice_controls_default24_regression D-06       (count-sensitive -> RED pre-impl)
//   voice_controls_out_of_range_untouched D-06     (count-sensitive -> RED pre-impl)
//   voice_controls_sweep_interaction    Pitfall 4  (GUARD -> PASSES trivially pre-impl)
//
// RED/GREEN contract: Plan 01 ships applyContinuousVoiceControls() as a no-op
// stub, so the six count-sensitive cases FAIL here (the documented baseline).
// Plan 02 implements the real [0, activeVoiceCount) fan-out and flips all six to
// GREEN. The two guard cases pin behavior that is ALREADY correct (ADSR is shared
// per D-07; an in-flight sweep's state machine must survive a base_vol re-base).
//
// Pitfalls handled (61-RESEARCH.md):
//  - #3: the mixer is a process-wide singleton (s_mixer). Each case re-inits it
//        AND resets the processor's noteForVoice[]/nextVoice/noteVelocity[] via
//        the friend seam (isolate()), defeating cross-case state bleed.
//  - #4: applyContinuousVoiceControls / noteVelocity / setActiveVoiceCount are
//        private; struct VoiceControlsTest is the granted friend (Task 1).

#include "PluginProcessor.h"
#include <JuceHeader.h>

extern "C" {
#include <spu94/spu94_voice.h>
#include <spu94/spu94_q15.h>
}

#include <cstdint>
#include <cstdio>
#include <memory>

// ---------------------------------------------------------------------------
// Test seam: friend struct granted access in PluginProcessor.h (Phase 61 Task 1).
// Thin forwarders to the private members the cases need.
// ---------------------------------------------------------------------------
struct VoiceControlsTest
{
    SPU94AudioProcessor& p;
    explicit VoiceControlsTest(SPU94AudioProcessor& proc) : p(proc) {}

    // --- Drive the controls under test -------------------------------------
    void setCount(int n)                 { p.setActiveVoiceCount(n); }
    void setGuiVolLR(int16_t l, int16_t r)
    {
        p.setGuiVoiceVolL(l);
        p.setGuiVoiceVolR(r);
    }
    void setNon(bool on)  { p.guiVoiceNon.store(on, std::memory_order_relaxed); }
    void setPmon(bool on) { p.guiVoicePmon.store(on, std::memory_order_relaxed); }

    // The method under test (no-op stub in Plan 01; real fan-out in Plan 02).
    void applyControls() { p.applyContinuousVoiceControls(); }

    // --- Per-note velocity (D-01 retention array) --------------------------
    void    seedVelocity(int v, int16_t velQ15) { p.noteVelocity[v] = velQ15; }
    int16_t velAt(int v) const                  { return p.noteVelocity[v]; }

    // Reproduce the production note-on pair: store the per-note velocity, then
    // key on the same voice with that velocity as the (centered) volume. The
    // GREEN apply method (Plan 02) will RE-derive base_vol from
    // noteVelocity x guiVol -- the whole point of the velocity-retention design.
    void keyOnMidiLike(int voice, int16_t velQ15)
    {
        p.noteVelocity[voice] = velQ15;
        spu94_voice_mixer_key_on(spu94_get_voice_mixer(), voice,
                                 /*start_addr*/ 0,
                                 /*pitch     */ (uint16_t)0x1000,
                                 /*vol_l     */ velQ15,
                                 /*vol_r     */ velQ15,
                                 /*reverb_on */ 1,
                                 /*adsr_cfg  */ nullptr);
    }

    // Reset the processor's per-instance audio-thread state (Pitfall 3).
    void resetProcessorState()
    {
        for (int i = 0; i < 24; ++i)
        {
            p.noteForVoice[i]  = -1;
            p.noteVelocity[i]  = 0;
        }
        p.nextVoice = 0;
    }
};

namespace {

// Mixer-state readers (observe the engine directly -- no audio render).
int16_t  baseL(int v)   { return spu94_get_voice_mixer()->voices[v].base_vol_l; }
int16_t  baseR(int v)   { return spu94_get_voice_mixer()->voices[v].base_vol_r; }
uint32_t nonBit(int v)  { return (spu94_get_voice_mixer()->non_flags  >> v) & 1u; }
uint32_t pmonBit(int v) { return (spu94_get_voice_mixer()->pmon_flags >> v) & 1u; }

// 0..127 MIDI velocity -> 0..0x7FFF Q15 scalar (61-RESEARCH.md Code Examples).
int16_t velToQ15(int vel)
{
    if (vel < 0)   vel = 0;
    if (vel > 127) vel = 127;
    return static_cast<int16_t>((vel * 0x7FFF) / 127);
}

// Re-isolate before every case: the voice mixer is a process-wide singleton, so
// its per-voice state must be cleared, and the processor's audio-thread arrays
// reset (Pitfall 3). Clone of test_voice_alloc.cpp's isolate().
void isolate(VoiceControlsTest& t)
{
    spu94_voice_mixer_init(spu94_get_voice_mixer());
    t.resetProcessorState();
}

// ---------------------------------------------------------------------------
// VCTRL-01: Level scales the base_vol of EVERY active voice, not just voice 0.
// RED pre-impl: the no-op stub leaves every base_vol at its mixer-init 0.
// ---------------------------------------------------------------------------
bool test_level_all_active(VoiceControlsTest& t)
{
    std::printf("voice_controls_level_all_active... ");
    bool ok = true;

    isolate(t);
    t.setCount(6);
    for (int v = 0; v < 6; ++v) t.seedVelocity(v, 0x7FFF);   // full velocity on 0..5
    t.setGuiVolLR(0x2000, 0x2000);                            // Level ~50%, pan center
    t.applyControls();

    // Every active voice must be scaled to (roughly) the Level ceiling.
    for (int v = 0; v < 6; ++v)
    {
        if (baseL(v) == 0)
        { std::printf("\n  FAIL: active voice %d base_vol_l == 0 (not scaled)", v); ok = false; }
        if (baseL(v) > 0x2000 + 1)
        { std::printf("\n  FAIL: voice %d base_vol_l=0x%X exceeds Level ceiling 0x2000", v, (unsigned)(uint16_t)baseL(v)); ok = false; }
    }
    // A voice OUTSIDE the active set (v=7) must NOT have been scaled to the Level.
    if (baseL(7) == baseL(0) && baseL(0) != 0)
    { std::printf("\n  FAIL: out-of-range voice 7 was scaled to the active Level"); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// VCTRL-02: a pan-left GUI value puts EVERY active voice at the SAME stereo
// position (base_vol_l > base_vol_r on all of them).
// RED pre-impl: the no-op stub leaves base_vol_l == base_vol_r == 0.
// ---------------------------------------------------------------------------
bool test_pan_all_active(VoiceControlsTest& t)
{
    std::printf("voice_controls_pan_all_active... ");
    bool ok = true;

    isolate(t);
    t.setCount(5);
    for (int v = 0; v < 5; ++v) t.seedVelocity(v, 0x7FFF);
    t.setGuiVolLR(0x3FFF, 0x1000);   // pan-left: guiL > guiR
    t.applyControls();

    for (int v = 0; v < 5; ++v)
    {
        if (!(baseL(v) > baseR(v)))
        {
            std::printf("\n  FAIL: voice %d not panned left: base_vol_l=0x%X base_vol_r=0x%X",
                        v, (unsigned)(uint16_t)baseL(v), (unsigned)(uint16_t)baseR(v));
            ok = false;
        }
    }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// VCTRL-03 (toggles): NON/PMON land on EVERY active voice.
// RED pre-impl: the no-op stub never sets the flags, so all bits stay clear.
// ---------------------------------------------------------------------------
bool test_non_pmon_all_active(VoiceControlsTest& t)
{
    std::printf("voice_controls_non_pmon_all_active... ");
    bool ok = true;

    isolate(t);
    t.setCount(4);
    t.setNon(true);
    t.setPmon(true);
    t.applyControls();

    for (int v = 0; v < 4; ++v)
        if (nonBit(v) != 1)
        { std::printf("\n  FAIL: NON bit %d not set (non_flags=0x%X)", v, spu94_get_voice_mixer()->non_flags); ok = false; }

    // PMON: bit 0 is accepted-but-ignored by the engine (voice 0 has no
    // predecessor), so only assert bits [1, count).
    for (int v = 1; v < 4; ++v)
        if (pmonBit(v) != 1)
        { std::printf("\n  FAIL: PMON bit %d not set (pmon_flags=0x%X)", v, spu94_get_voice_mixer()->pmon_flags); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// VCTRL-03 (ADSR) / D-07 GUARD: the ADSR shape already fans out to every
// triggered voice (buildAdsrConfig is passed at each key_on). This is a
// regression GUARD, not new wiring -- it may PASS pre-impl. We pass the SAME
// non-null adsr config to two key_on calls and assert both voices' staged ADSR
// (pending_config[v].adsr -- key_on copies *adsr_cfg there, spu94_voice.c:461)
// carries the identical enabled flag and rate field.
// ---------------------------------------------------------------------------
bool test_adsr_shared(VoiceControlsTest& t)
{
    std::printf("voice_controls_adsr_shared... ");
    bool ok = true;

    isolate(t);
    t.setCount(2);

    spu94_adsr_state_t cfg = {};
    cfg.enabled      = 1;
    cfg.attack_shift = 7;   // any distinctive non-default rate field
    cfg.decay_shift  = 3;
    cfg.sustain_level = 12;

    auto* mx = spu94_get_voice_mixer();
    // Two voices keyed on with the IDENTICAL config -> both pending slots match.
    spu94_voice_mixer_key_on(mx, 0, 0, 0x1000, 0x4000, 0x4000, 1, &cfg);
    spu94_voice_mixer_key_on(mx, 1, 0, 0x1000, 0x4000, 0x4000, 1, &cfg);

    const spu94_adsr_state_t& a0 = mx->pending_config[0].adsr;
    const spu94_adsr_state_t& a1 = mx->pending_config[1].adsr;
    if (a0.enabled != a1.enabled)
    { std::printf("\n  FAIL: adsr.enabled differs (%u vs %u)", a0.enabled, a1.enabled); ok = false; }
    if (a0.attack_shift != a1.attack_shift)
    { std::printf("\n  FAIL: adsr.attack_shift differs (%u vs %u)", a0.attack_shift, a1.attack_shift); ok = false; }
    if (a0.decay_shift != a1.decay_shift)
    { std::printf("\n  FAIL: adsr.decay_shift differs (%u vs %u)", a0.decay_shift, a1.decay_shift); ok = false; }
    // And the shape that was shared matches the source config (proves the fan-out).
    if (a0.attack_shift != cfg.attack_shift || a1.attack_shift != cfg.attack_shift)
    { std::printf("\n  FAIL: staged attack_shift != config (%u)", cfg.attack_shift); ok = false; }

    std::printf("%s\n", ok ? "PASSED (guard)" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// D-01 (the Pitfall-1 trap): two voices keyed at different velocities, same
// Level -> their base_vol must DIFFER and scale with velocity. A flat overwrite
// (the bug) makes them EQUAL.
// RED pre-impl: the no-op stub never recomputes base_vol from velocity, so both
// voices keep their key_on value -- but keyOnMidiLike keyed them at DIFFERENT
// velocities, so they are not equal yet. The trap is that the GREEN apply must
// PRESERVE that velocity ordering after the per-block recompute. We assert the
// post-apply ordering, which the no-op stub cannot guarantee once Plan 02 makes
// apply authoritative; pre-impl this fails because apply does not scale by Level
// at all (base stays at the raw velocity, not velocity x Level), so the
// documented RED is base_vol not reflecting the Level ceiling.
// ---------------------------------------------------------------------------
bool test_velocity_rides_level(VoiceControlsTest& t)
{
    std::printf("voice_controls_velocity_rides_level... ");
    bool ok = true;

    isolate(t);
    t.setCount(2);
    t.keyOnMidiLike(0, velToQ15(40));    // soft note
    t.keyOnMidiLike(1, velToQ15(120));   // hard note
    t.setGuiVolLR(0x3FFF, 0x3FFF);       // full Level, pan center
    t.applyControls();

    // Velocity must survive: the harder note stays louder, and the two are not
    // flattened to one value. Against the no-op stub base_vol is never re-derived
    // through Level, so the unified velocity x Level scaling is absent -> RED.
    int16_t b0 = baseL(0);
    int16_t b1 = baseL(1);
    if (b0 == b1)
    { std::printf("\n  FAIL: velocities flattened: base_vol_l[0]=0x%X == [1]=0x%X",
                  (unsigned)(uint16_t)b0, (unsigned)(uint16_t)b1); ok = false; }
    if (!(b1 > b0))
    { std::printf("\n  FAIL: harder note not louder: [1]=0x%X !> [0]=0x%X",
                  (unsigned)(uint16_t)b1, (unsigned)(uint16_t)b0); ok = false; }
    // base_vol must reflect velocity x full Level (q15 combine), not the raw
    // key_on velocity -- the unification Plan 02 owns. Full-velocity-120 through
    // full Level should land near q15_mul_truncate(0x3FFF, velToQ15(120)).
    int16_t expect1 = q15_mul_truncate((int16_t)0x3FFF, velToQ15(120));
    if (b1 != expect1)
    { std::printf("\n  FAIL: voice 1 base_vol_l=0x%X != velocity x Level 0x%X (no-op stub did not recompute)",
                  (unsigned)(uint16_t)b1, (unsigned)(uint16_t)expect1); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// D-06 regression: at count=24 the loop reaches all 24 voices, and voice 0 from
// a full-velocity Trigger seed lands at the documented unified full-velocity x
// full-Level value (~0x3FFE = q15_mul_truncate(0x3FFF, 0x7FFF)).
// RED pre-impl: the no-op stub leaves base_vol at the mixer-init 0, so neither
// the voice-0 value nor the voice-23 reach holds.
// ---------------------------------------------------------------------------
bool test_default24_regression(VoiceControlsTest& t)
{
    std::printf("voice_controls_default24_regression... ");
    bool ok = true;

    isolate(t);
    t.setCount(24);
    for (int v = 0; v < 24; ++v) t.seedVelocity(v, 0x7FFF);  // full-velocity seed (D-08)
    t.setGuiVolLR(0x3FFF, 0x3FFF);
    t.applyControls();

    // Voice 0 == full velocity x full Level (the unified Trigger/MIDI value).
    int16_t expect = q15_mul_truncate((int16_t)0x3FFF, (int16_t)0x7FFF);  // ~0x3FFE
    if (baseL(0) != expect)
    { std::printf("\n  FAIL: voice 0 base_vol_l=0x%X != full-vel x full-Level 0x%X",
                  (unsigned)(uint16_t)baseL(0), (unsigned)(uint16_t)expect); ok = false; }
    // The loop must have reached voice 23 (last active voice) with the same value.
    if (baseL(23) != expect)
    { std::printf("\n  FAIL: voice 23 base_vol_l=0x%X != 0x%X (loop did not reach 23)",
                  (unsigned)(uint16_t)baseL(23), (unsigned)(uint16_t)expect); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// D-06 bound: a voice keyed on, then count lowered below its index, must NOT
// receive a control update -- its base_vol stays at the key_on value.
// RED pre-impl: the no-op stub never touches ANY voice, so the recorded value is
// trivially unchanged -- BUT we also assert the in-range voices DID change, which
// the stub cannot satisfy, making this case RED until Plan 02.
// ---------------------------------------------------------------------------
bool test_out_of_range_untouched(VoiceControlsTest& t)
{
    std::printf("voice_controls_out_of_range_untouched... ");
    bool ok = true;

    isolate(t);
    t.setCount(8);
    t.keyOnMidiLike(5, velToQ15(100));   // key on voice 5 (in range at count 8)
    // key_on stages pending; mirror the key-on base_vol into the live voice so we
    // have a stable "before" to compare against (the apply loop reads live voices).
    spu94_get_voice_mixer()->voices[5].base_vol_l = velToQ15(100);
    spu94_get_voice_mixer()->voices[5].base_vol_r = velToQ15(100);
    int16_t before = baseL(5);

    t.setCount(3);                       // index 5 now OUT of range [0,3)
    t.setGuiVolLR(0x1000, 0x1000);       // a new, different Level
    t.applyControls();

    // Voice 5 (out of range) must be untouched.
    if (baseL(5) != before)
    { std::printf("\n  FAIL: out-of-range voice 5 changed: 0x%X -> 0x%X",
                  (unsigned)(uint16_t)before, (unsigned)(uint16_t)baseL(5)); ok = false; }
    // In-range voices [0,3) must have received the new Level (proves apply ran).
    for (int v = 0; v < 3; ++v)
    {
        t.seedVelocity(v, 0x7FFF);
    }
    t.applyControls();                   // re-apply now that [0,3) have velocity
    for (int v = 0; v < 3; ++v)
        if (baseL(v) == 0)
        { std::printf("\n  FAIL: in-range voice %d not updated by apply (base_vol_l==0)", v); ok = false; }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------------------
// Pitfall 4 / A2 GUARD: a voice with an in-flight L-sweep must keep its sweep
// state machine (sweep_l.active) after Level moves -- the apply re-bases base_vol
// as the ceiling WITHOUT resetting the sweep. The no-op stub trivially passes
// (it touches nothing); the case becomes meaningful after Plan 02 makes apply
// authoritative over base_vol.
// ---------------------------------------------------------------------------
bool test_sweep_interaction(VoiceControlsTest& t)
{
    std::printf("voice_controls_sweep_interaction... ");
    bool ok = true;

    isolate(t);
    t.setCount(2);
    t.keyOnMidiLike(0, 0x7FFF);
    // Simulate an in-flight tremolo sweep on voice 0's left channel.
    spu94_get_voice_mixer()->voices[0].sweep_l.active = 1;

    t.setGuiVolLR(0x2000, 0x2000);   // move Level
    t.applyControls();

    // The sweep state machine must survive a base_vol re-base.
    if (spu94_get_voice_mixer()->voices[0].sweep_l.active != 1)
    { std::printf("\n  FAIL: sweep_l.active was reset by the apply loop"); ok = false; }

    std::printf("%s\n", ok ? "PASSED (guard)" : "\nFAILED");
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
    VoiceControlsTest t(*proc);

    bool ok = false;
    if      (caseName == "voice_controls_level_all_active")     ok = test_level_all_active(t);
    else if (caseName == "voice_controls_pan_all_active")       ok = test_pan_all_active(t);
    else if (caseName == "voice_controls_non_pmon_all_active")  ok = test_non_pmon_all_active(t);
    else if (caseName == "voice_controls_adsr_shared")          ok = test_adsr_shared(t);
    else if (caseName == "voice_controls_velocity_rides_level") ok = test_velocity_rides_level(t);
    else if (caseName == "voice_controls_default24_regression") ok = test_default24_regression(t);
    else if (caseName == "voice_controls_out_of_range_untouched") ok = test_out_of_range_untouched(t);
    else if (caseName == "voice_controls_sweep_interaction")    ok = test_sweep_interaction(t);
    else
    {
        std::printf("unknown case: %s\n", argv[1]);
        return 2;
    }

    return ok ? 0 : 1;
}
