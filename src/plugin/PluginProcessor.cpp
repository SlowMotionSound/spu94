#include "PluginProcessor.h"
#include "BoundaryConverter.h"
#include "PluginEditor.h"
#include "WavLoader.h"
#include <samplerate.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

// Phase 41: Speed-to-shift mapping for VCA ramp controls.
// Maps a user-facing seconds value to the nearest SPU sweep shift parameter.
// Step is always 0 (step=0 gives the cleanest musical ramps).
namespace {
    struct SweepShiftResult { uint8_t shift; uint8_t step; };

    static const struct { uint8_t shift; float seconds; } kSweepTable[] = {
        {  9, 0.03f },
        { 10, 0.05f },
        { 11, 0.1f  },
        { 12, 0.2f  },
        { 13, 0.4f  },
        { 14, 0.85f },
        { 15, 1.7f  },
        { 16, 3.4f  },
        { 17, 6.8f  },
    };
    static constexpr int kSweepTableSize = 9;

    SweepShiftResult speedToShift(float seconds)
    {
        if (seconds <= kSweepTable[0].seconds)
            return { kSweepTable[0].shift, 0 };
        if (seconds >= kSweepTable[kSweepTableSize - 1].seconds)
            return { kSweepTable[kSweepTableSize - 1].shift, 0 };

        int bestIdx = 0;
        float bestDist = std::fabs(seconds - kSweepTable[0].seconds);
        for (int i = 1; i < kSweepTableSize; ++i)
        {
            float dist = std::fabs(seconds - kSweepTable[i].seconds);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx = i;
            }
        }
        return { kSweepTable[bestIdx].shift, 0 };
    }
    // Phase 44: Hz-to-shift mapping for tremolo speed control.
    // Uses the counter-accumulate math from spu94_envelope_step to derive the
    // actual oscillation rate (Hz) for each shift/step pair. The sweep produces
    // a full triangle cycle (up + down half-cycles) when retrigger_enable=1.
    //
    // Math derivation (linear mode, phase=0):
    //   counter_increment = 0x8000 >> max(0, shift - 11)
    //   step_magnitude = (7 - step_index) << max(0, 11 - shift)
    //   ticks_per_fire = 0x8000 / counter_increment = 1 << max(0, shift - 11)
    //   fires_per_half_cycle = 0x7FFF / step_magnitude
    //   half_cycle_ticks = fires_per_half_cycle * ticks_per_fire
    //   full_cycle_hz = 44100.0 / (2 * half_cycle_ticks)
    struct TremoloHzEntry { uint8_t shift; uint8_t step; float hz; };

    static const TremoloHzEntry kTremoloHzTable[] = {
        // shift  step   Hz (computed from counter-accumulate math at 44100 Hz)
        { 13, 0,  1.1776f },
        { 13, 1,  1.0094f },
        { 13, 2,  0.8412f },
        { 13, 3,  0.6730f },
        { 12, 3,  1.3460f },
        { 12, 2,  1.6824f },
        { 12, 1,  2.0189f },
        { 12, 0,  2.3553f },
        { 11, 3,  2.6920f },
        { 11, 2,  3.3649f },
        { 11, 1,  4.0377f },
        { 11, 0,  4.7105f },
        { 10, 3,  5.3846f },
        { 10, 2,  6.7308f },
        { 10, 1,  8.0769f },
        { 10, 0,  9.4231f },
        {  9, 3, 10.7719f },
        {  9, 2, 13.4615f },
        {  9, 1, 16.1538f },
        {  9, 0, 18.8462f },
        {  8, 3, 21.5543f },
        {  8, 2, 26.9231f },
        {  8, 1, 32.3314f },
        {  8, 0, 37.6923f },
        {  7, 3, 43.1507f },
    };
    static constexpr int kTremoloHzTableSize = 25;

    // Find the shift/step pair closest to the requested Hz (log-distance).
    // Clamps to table boundaries for out-of-range inputs (T-44-01 mitigation).
    SweepShiftResult hzToShift(float hz)
    {
        if (hz <= kTremoloHzTable[3].hz)  // below table minimum
            return { kTremoloHzTable[3].shift, kTremoloHzTable[3].step };
        if (hz >= kTremoloHzTable[kTremoloHzTableSize - 1].hz)  // above table maximum
            return { kTremoloHzTable[kTremoloHzTableSize - 1].shift, kTremoloHzTable[kTremoloHzTableSize - 1].step };

        int bestIdx = 0;
        float bestDist = std::fabs(std::log(hz) - std::log(kTremoloHzTable[0].hz));
        for (int i = 1; i < kTremoloHzTableSize; ++i)
        {
            float dist = std::fabs(std::log(hz) - std::log(kTremoloHzTable[i].hz));
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx = i;
            }
        }
        return { kTremoloHzTable[bestIdx].shift, kTremoloHzTable[bestIdx].step };
    }

    // Phase 48: Hz-to-shift mapping for AM synthesis rate control.
    // Audio-rate range (shifts 0-8) produces audible sidebands when the sweep
    // oscillates fast enough to create new frequency content rather than perceivable
    // volume pulsing. Same counter-accumulate math as tremolo but at lower shifts.
    //
    // Math (identical to tremolo derivation, different shift range):
    //   For shift <= 11: counter_increment = 0x8000 (fires every tick)
    //   step_magnitude = (7 - step_index) << (11 - shift)
    //   fires_per_half_cycle = 0x7FFF / step_magnitude
    //   full_cycle_hz = 44100.0 / (2 * fires_per_half_cycle)
    struct AmHzEntry { uint8_t shift; uint8_t step; float hz; };

    static const AmHzEntry kAmHzTable[] = {
        // shift  step   Hz (computed from counter-accumulate math at 44100 Hz)
        {  8, 3,   21.5339f },
        {  8, 2,   26.9173f },
        {  8, 1,   32.3008f },
        {  8, 0,   37.6843f },
        {  7, 3,   43.0677f },
        {  7, 2,   53.8347f },
        {  7, 1,   64.6016f },
        {  7, 0,   75.3685f },
        {  6, 3,   86.1354f },
        {  6, 2,  107.6693f },
        {  6, 1,  129.2032f },
        {  6, 0,  150.7370f },
        {  5, 3,  172.2709f },
        {  5, 2,  215.3386f },
        {  5, 1,  258.4063f },
        {  5, 0,  301.4740f },
        {  4, 3,  344.5418f },
        {  4, 2,  430.6772f },
        {  4, 1,  516.8126f },
        {  4, 0,  602.9481f },
        {  3, 3,  689.0835f },
        {  3, 2,  861.3544f },
        {  3, 1, 1033.6253f },
        {  3, 0, 1205.8962f },
        {  2, 3, 1378.1671f },
        {  2, 2, 1722.7088f },
        {  2, 1, 2067.2506f },
        {  2, 0, 2411.7924f },
        {  1, 3, 2756.3341f },
        {  1, 2, 3445.4176f },
        {  1, 1, 4134.5012f },
        {  1, 0, 4823.5847f },
        {  0, 3, 5512.6682f },
        {  0, 2, 6890.8353f },
        {  0, 1, 8269.0023f },
        {  0, 0, 9647.1694f },
    };
    static constexpr int kAmHzTableSize = 36;

    // Find the shift/step pair closest to the requested Hz (log-distance).
    // Clamps to table boundaries for out-of-range inputs (T-48-01 mitigation).
    SweepShiftResult hzToShiftAm(float hz)
    {
        if (hz <= kAmHzTable[0].hz)
            return { kAmHzTable[0].shift, kAmHzTable[0].step };
        if (hz >= kAmHzTable[kAmHzTableSize - 1].hz)
            return { kAmHzTable[kAmHzTableSize - 1].shift, kAmHzTable[kAmHzTableSize - 1].step };

        int bestIdx = 0;
        float bestDist = std::fabs(std::log(hz) - std::log(kAmHzTable[0].hz));
        for (int i = 1; i < kAmHzTableSize; ++i)
        {
            float dist = std::fabs(std::log(hz) - std::log(kAmHzTable[i].hz));
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx = i;
            }
        }
        return { kAmHzTable[bestIdx].shift, kAmHzTable[bestIdx].step };
    }
} // anonymous namespace

SPU94AudioProcessor::SPU94AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // -----------------------------------------------------------------
    // Phase 24 (PLUG-28..31): Register 9 host-automatable parameters.
    // Registration ORDER is FROZEN per PLUG-30 (AU index stability).
    // All ParameterIDs use versionHint=1. Future params MUST be added
    // at the END of this block with versionHint=2+.
    // No APVTS anywhere (PLUG-29).
    // -----------------------------------------------------------------

    // Shared helpers for percent-display parameters (0..1 real range)
    auto pctStringFromValue = [](float v, int) -> juce::String {
        return juce::String(static_cast<int>(v * 100.0f + 0.5f)) + "%";
    };
    auto pctValueFromString = [](const juce::String& s) -> float {
        return s.getFloatValue() / 100.0f;
    };
    auto pctRange = juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f);
    auto pctAttrs = juce::AudioParameterFloatAttributes()
        .withLabel("%")
        .withStringFromValueFunction(pctStringFromValue)
        .withValueFromStringFunction(pctValueFromString);

    // 1. Input Gain: dB display, 0..16 range, skew so 1.0 (unity) at midpoint
    {
        auto igRange = juce::NormalisableRange<float>(0.0f, 16.0f, 0.01f);
        igRange.setSkewForCentre(1.0f);
        addParameter(paramInputGain = new juce::AudioParameterFloat(
            juce::ParameterID{"input_gain", 1}, "Input Gain", igRange, 0.5f,
            juce::AudioParameterFloatAttributes()
                .withLabel("dB")
                .withStringFromValueFunction([](float v, int) -> juce::String {
                    if (v < 0.0001f) return "-inf dB";
                    return juce::String(20.0f * std::log10(v), 1) + " dB";
                })
                .withValueFromStringFunction([](const juce::String& s) -> float {
                    if (s.containsIgnoreCase("inf")) return 0.0f;
                    return std::pow(10.0f, s.getFloatValue() / 20.0f);
                })
        ));
    }

    // 2. ADPCM Send (percent 0-100, default 1.0 = 100%)
    addParameter(paramAdpcmSend = new juce::AudioParameterFloat(
        juce::ParameterID{"adpcm_send", 1}, "ADPCM Send", pctRange, 1.0f, pctAttrs));

    // 3. Dry Send (percent 0-100, default 0.0 = 0%)
    addParameter(paramDrySend = new juce::AudioParameterFloat(
        juce::ParameterID{"dry_send", 1}, "Dry Send", pctRange, 0.0f, pctAttrs));

    // 4. Morph Position (percent 0-100, default 0.625 = Hall position)
    addParameter(paramMorphPosition = new juce::AudioParameterFloat(
        juce::ParameterID{"morph_position", 1}, "Morph Position", pctRange, 0.625f, pctAttrs));

    // 5. Morph Speed (percent 0-100, default 0.5 = mid-glide)
    addParameter(paramMorphSpeed = new juce::AudioParameterFloat(
        juce::ParameterID{"morph_speed", 1}, "Morph Speed", pctRange, 0.5f, pctAttrs));

    // 6. Morph Grit (two-position: 0=Int, 1=Fract.)
    addParameter(paramMorphGrit = new juce::AudioParameterFloat(
        juce::ParameterID{"morph_grit", 1}, "Morph Grit",
        juce::NormalisableRange<float>(0.0f, 1.0f, 1.0f),  // step=1: two positions
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("")
            .withStringFromValueFunction([](float v, int) -> juce::String {
                return v < 0.5f ? "Int" : "Fract.";
            })
            .withValueFromStringFunction([](const juce::String& s) -> float {
                return s.containsIgnoreCase("fract") ? 1.0f : 0.0f;
            })
    ));

    // 7. Dry Level (percent 0-100, default 0.0 = OFF)
    addParameter(paramDryLevel = new juce::AudioParameterFloat(
        juce::ParameterID{"dry_level", 1}, "Dry Level", pctRange, 0.0f, pctAttrs));

    // 8. ADPCM Level (percent 0-100, default 0.0 = OFF)
    addParameter(paramAdpcmLevel = new juce::AudioParameterFloat(
        juce::ParameterID{"adpcm_level", 1}, "ADPCM Level", pctRange, 0.0f, pctAttrs));

    // 9. Reverb Level (percent 0-100, default 1.0 = FULL)
    addParameter(paramReverbLevel = new juce::AudioParameterFloat(
        juce::ParameterID{"reverb_level", 1}, "Reverb Level", pctRange, 1.0f, pctAttrs));

    // -----------------------------------------------------------------
    // End of parameter registration block.
    // -----------------------------------------------------------------

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

// Phase 25 / PLUG-32, PLUG-35: bus layout whitelist.
// Accepts exactly three configurations; rejects everything else
// (surround, Atmos, sidechain, multi-bus, disabled).
// The BusesProperties constructor (stereo/stereo default) is unchanged --
// this override handles host negotiation for mono tracks.
bool SPU94AudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    // Never accept disabled buses
    if (mainIn  == juce::AudioChannelSet::disabled()) return false;
    if (mainOut == juce::AudioChannelSet::disabled()) return false;

    // Mono in -> mono out:   OK (PLUG-32, PLUG-34)
    // Mono in -> stereo out: OK (PLUG-32, PLUG-33)
    if (mainIn == juce::AudioChannelSet::mono())
        return (mainOut == juce::AudioChannelSet::mono()
             || mainOut == juce::AudioChannelSet::stereo());

    // Stereo in -> stereo out: OK (PLUG-32)
    if (mainIn == juce::AudioChannelSet::stereo())
        return (mainOut == juce::AudioChannelSet::stereo());

    // Everything else (surround, Atmos, sidechain, multi-bus): rejected (PLUG-35)
    return false;
}

const juce::String SPU94AudioProcessor::getName() const
{
    return juce::String("SPU-94");
}

void SPU94AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Tear down both engines before reinitializing (WR-03 extended)
    for (int e = 0; e < 2; ++e)
    {
        if (engines[e] != nullptr)
        {
            spu94_destroy(engines[e]);
            engines[e] = nullptr;
        }
    }

    // engines[0] = audio path. engines[1] = scratch (target-register
    // computation only — never produces audio). The Register Behavior knob
    // dials slew duration on engines[0]; instant snap is achieved by
    // skipping the slew machinery and writing registers directly.
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

    spu94_load_preset(engines[0], SPU94_PRESET_HALL);
    spu94_load_preset(engines[1], SPU94_PRESET_HALL);
    registerBridge.syncShadowsFromSPU(engines[0]);
    lastMorphPosition = -1.0f;  // force morph apply on first processBlock
    morphInitialized = false;

    // Mixer defaults on the audio engine (scratch is silent and inherits its
    // settings from preset load — its mixer state is never observed).
    spu94_set_input_gain(engines[0], 0x7FFF);
    spu94_set_dry_fader(engines[0], 0x0000);
    spu94_set_reverb_fader(engines[0], 0x7FFF);
    spu94_set_dry_send(engines[0], 0x0000);
    spu94_set_adpcm_send(engines[0], 0x7FFF);
    spu94_set_sampler_fader(engines[0], 0x7FFF);
    spu94_set_sampler_send(engines[0], 0x7FFF);
    spu94_set_sampler_drive(engines[0], 0x1000);
    spu94_set_dac_enabled(engines[0], 1);
    spu94_set_latency_comp(engines[0], 1);

    // Enable voice mixer at startup so noise generator runs without a loaded sample
    {
        auto* mx = spu94_get_voice_mixer();
        mx->enabled = 1;
        mx->master_vol_l = 0x7FFF;
        mx->master_vol_r = 0x7FFF;
    }

    // Phase 22: bidirectional libsamplerate SRC sandwich. All allocation
    // and the impulse-based group-delay measurement happen here in
    // prepareToPlay -- processBlock is allocation-free (PLUG-12).
    hostSampleRate_ = sampleRate;
    constexpr int kMaxBlock = 4096;
    const int maxBlock = juce::jmin(samplesPerBlock, kMaxBlock);

    // Phase 23 Plan 02: per-channel host-rate scratch for the pre-clamp
    // Input Gain multiply (plugin path). Sized to the same kMaxBlock
    // ceiling the audio I/O branches use. Plugin path only -- standalone
    // path uses the engine-register gain path and does not touch this
    // scratch.
    inputGainScratch_[0].allocate(kMaxBlock, true);
    inputGainScratch_[1].allocate(kMaxBlock, true);

    srcChain_.prepare(sampleRate, maxBlock, /*numChannels=*/2);

    // Latency reporting (PLUG-14). Core latency lives at 44.1 kHz; scale
    // to host samples with ceil() so we over-report by at most 1 sample
    // (under-reporting drifts wet AHEAD of dry, which is the bad direction
    // -- PITFALLS B6). Add the measured input+output SRC group delay.
    // Calling setLatencySamples every prepareToPlay is the only correct
    // policy: some hosts re-poll on transport start; others ignore mid-
    // stream changes; the consistent rule is "always report current".
    const uint32_t coreLatency44k = spu94_get_total_latency_samples(engines[0]);
    const int coreLatencyHostSamples = static_cast<int>(
        std::ceil(static_cast<double>(coreLatency44k) * (sampleRate / 44100.0)));
    setLatencySamples(srcChain_.getMeasuredLatencyHostSamples() + coreLatencyHostSamples);
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
    srcChain_.release();
}

void SPU94AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages)
{
    // Phase 22 / PLUG-16 (R3): denormals OFF for the entire block.
    // RAII guarantees DAZ/FTZ remain set through the SRC sandwich and the
    // SPU core call, so reverb tails (which routinely produce subnormal
    // floats) don't trigger microcode-trap stalls on Intel CPUs. This
    // MUST be the first statement of processBlock.
    juce::ScopedNoDenormals noDenormals;

    // R4 (fast-path slip verification): clear the SRC-call counter so the
    // Task-3 verify step can read a clean per-block number.
    srcChain_.resetSrcCallbacksCounter();

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

    // STATE MANAGEMENT runs unconditionally (presets, file loads, morph
    // re-apply, shadow syncs) so the GUI always reflects current engine
    // state -- even with no WAV loaded and no playback. Only the actual
    // audio I/O (sample reads + spu94_process + output mixing) is gated
    // on loaded/playing below.
    if (engines[0] == nullptr || engines[1] == nullptr)
    {
        buffer.clear();
        return;
    }

    // 1. Drain preset command queue — applied to the single audio engine.
    if (presetQueue.drain(engines[0]))
    {
        registerBridge.syncShadowsFromSPU(engines[0]);
        shadowSyncCompletedCount.fetch_add(1, std::memory_order_release);
    }

    // 1b. Drain file-preset load request (GUI thread may have loaded a .spu94 file)
    if (filePresetReady.load(std::memory_order_acquire))
    {
        size_t len = pendingPresetLen.load(std::memory_order_relaxed);
        const int targetSlot = pendingTargetSlot.load(std::memory_order_relaxed);
        if (targetSlot >= 0) {
            // Per-slot load mutates user_slots[targetSlot] only -- engines[0]
            // active regs are unchanged. Force a morph re-apply so the slot's
            // contents take effect at the current (midpoint) position; the
            // post-reapply sync below will then capture the updated state.
            spu94_load_user_slot(engines[0], targetSlot,
                                 pendingPresetBuf.data(), len);
            // Mirror to engines[1] -- the scratch engine the glide path uses
            // for set_morph reads its OWN user_slots[]; without this mirror
            // glided morph movements would treat the slot as empty.
            spu94_load_user_slot(engines[1], targetSlot,
                                 pendingPresetBuf.data(), len);
            morphReapplyPending.store(true, std::memory_order_relaxed);
        } else {
            spu94_preset_load(engines[0], pendingPresetBuf.data(), len);
            // Mirror just the user_slots block to engines[1] (the rest of
            // engines[1] is scratch; only its user_slots[] are read via
            // set_morph in the glide path).
            for (int s = 0; s < SPU94_INTERP_USER_SLOT_COUNT; s++) {
                if (spu94_interp_user_slot_is_filled(engines[0], s)) {
                    int16_t regs[SPU94_REG__COUNT];
                    spu94_interp_get_user_slot(engines[0], s, regs);
                    spu94_interp_set_user_slot(engines[1], s, regs);
                } else {
                    spu94_interp_clear_user_slot(engines[1], s);
                }
            }
            registerBridge.syncShadowsFromSPU(engines[0]);
            shadowSyncCompletedCount.fetch_add(1, std::memory_order_release);
        }
        filePresetReady.store(false, std::memory_order_release);
        filePresetAppliedCount.fetch_add(1, std::memory_order_release);

        // Sync mixer/DAC atomics from loaded SPU state so GUI reflects the file's values
        inputLevel.store(static_cast<float>(spu94_get_input_gain(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        dryLevel.store(static_cast<float>(spu94_get_dry_fader(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        adpcmLevel.store(static_cast<float>(spu94_get_adpcm_fader(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        reverbLevel.store(static_cast<float>(spu94_get_reverb_fader(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        drySend.store(static_cast<float>(spu94_get_dry_send(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        adpcmSend.store(static_cast<float>(spu94_get_adpcm_send(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        latencyCompEnabled.store(spu94_get_latency_comp(engines[0]) != 0, std::memory_order_relaxed);
        dacEnabled.store(spu94_get_dac_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacFirEnabled.store(spu94_get_dac_fir_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacNoiseEnabled.store(spu94_get_dac_noise_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacTrueOversample.store(spu94_get_dac_true_oversample(engines[0]) != 0, std::memory_order_relaxed);
        morphGrit.store(spu94_get_morph_grit(engines[0]), std::memory_order_relaxed);

        // Sync AudioParameterFloat values from the freshly-loaded atomics so the
        // host sees the correct values in automation lanes. setValueNotifyingHost
        // takes NORMALIZED 0..1; for mixer params (0..1 real range) normalized == real.
        paramDryLevel->setValueNotifyingHost(dryLevel.load(std::memory_order_relaxed));
        paramAdpcmLevel->setValueNotifyingHost(adpcmLevel.load(std::memory_order_relaxed));
        paramReverbLevel->setValueNotifyingHost(reverbLevel.load(std::memory_order_relaxed));
        paramDrySend->setValueNotifyingHost(drySend.load(std::memory_order_relaxed));
        paramAdpcmSend->setValueNotifyingHost(adpcmSend.load(std::memory_order_relaxed));
        paramInputGain->setValueNotifyingHost(paramInputGain->getNormalisableRange().convertTo0to1(
            inputLevel.load(std::memory_order_relaxed)));
        paramMorphPosition->setValueNotifyingHost(morphPosition.load(std::memory_order_relaxed));
        paramMorphSpeed->setValueNotifyingHost(morphSpeed.load(std::memory_order_relaxed));
        paramMorphGrit->setValueNotifyingHost(morphGrit.load(std::memory_order_relaxed) >= 1 ? 1.0f : 0.0f);
    }

    // 2. Push register values: morph engine OR register bridge, never both.
    // When morph is active, the interpolation engine owns all 30 reverb registers.
    // When advanced mode is active, the register bridge (GUI sliders) owns them.
    // Push to both audio engines so they stay in sync in Advanced mode.
    if (!morphActive.load(std::memory_order_relaxed))
    {
        registerBridge.pushPendingRegisterWrites(engines[0]);
    }

    // DIAGNOSTIC: sync shadows directly from engines[0] without rewriting
    // morph target. If the sliders end up showing the same wrong values as
    // the Macro audio sounds, that's direct evidence the slew path leaves
    // engines[0] off-target. If sliders show the right values, the bug is
    // somewhere else.
    if (needShadowSync.exchange(false, std::memory_order_relaxed)) {
        registerBridge.syncShadowsFromSPU(engines[0]);
        shadowSyncCompletedCount.fetch_add(1, std::memory_order_release);
    }

    // 3. ADPCM coloration toggle (ADPCM-IO-06, D-06): read GUI atomic,
    // push to C API. Takes effect on the next spu94_process block.
    // ADPCM auto-enables when ADPCM fader or ADPCM send is non-zero.
    // The old dedicated toggle was removed (D-08); the mixer controls
    // now implicitly drive ADPCM on/off.
    {
        const bool adpcm_active =
            paramAdpcmLevel->get() > 0.0f ||
            paramAdpcmSend->get() > 0.0f;
        const int adpcm_flag = adpcm_active ? 1 : 0;
        spu94_set_adpcm_enabled(engines[0], adpcm_flag);
    }
    spu94_set_gauss_enabled(engines[0],
        gaussEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_aa_filter_enabled(engines[0],
        aaFilterEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    spu94_set_voice_pitch(engines[0],
        static_cast<uint16_t>(voicePitch.load(std::memory_order_relaxed)));

    // Push mixer/DAC state to the single audio engine. State management --
    // runs unconditionally so the GUI's view of engine state is always
    // current, regardless of WAV/playback status.
    //
    // Phase 23 Plan 02 (D-03): on the plugin path, Input Gain is a
    // pre-clamp float multiply applied to the host buffer below; the
    // engine register is no longer the gain stage and is pinned at
    // unity (0x7FFF). On the standalone path, the engine register
    // remains the gain stage (testbed-only, unchanged).
    {
        // Phase 23 UAT: standalone path now also uses pre-clamp float gain so
        // the +24 dB drive range works in the testbed (was wrapping when
        // atomic * 0x7FFF exceeded int16_t range on the old register path).
        spu94_set_input_gain(engines[0], static_cast<int16_t>(0x7FFF));
    }
    // Phase 24: read mixer/send params from AudioParameterFloat (PLUG-28/31).
    // DAC/latency-comp toggles remain on their atomics (not host-automated).
    spu94_set_dry_fader(engines[0], static_cast<int16_t>(
        paramDryLevel->get() * 0x7FFF));
    spu94_set_adpcm_fader(engines[0], static_cast<int16_t>(
        paramAdpcmLevel->get() * 0x7FFF));
    spu94_set_reverb_fader(engines[0], static_cast<int16_t>(
        paramReverbLevel->get() * 0x7FFF));
    spu94_set_dry_send(engines[0], static_cast<int16_t>(
        paramDrySend->get() * 0x7FFF));
    spu94_set_adpcm_send(engines[0], static_cast<int16_t>(
        paramAdpcmSend->get() * 0x7FFF));
    spu94_set_sampler_fader(engines[0], static_cast<int16_t>(
        samplerFader.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_sampler_send(engines[0], static_cast<int16_t>(
        samplerSend.load(std::memory_order_relaxed) * 0x7FFF));
    spu94_set_sampler_drive(engines[0], static_cast<int32_t>(
        samplerDrive.load(std::memory_order_relaxed) * 0x1000));
    // AA-03: push anti-aliasing toggle to voice mixer. Inverted: AA enabled
    // means Gaussian interpolation ON (gauss_bypass=0); AA disabled means raw
    // zero-order hold (gauss_bypass=1).
    spu94_get_voice_mixer()->gauss_bypass =
        samplerAAEnabled.load(std::memory_order_relaxed) ? 0 : 1;
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
    spu94_set_morph_grit(engines[0],
        static_cast<int>(paramMorphGrit->get() + 0.5f));

    // Morph position. The Register Behavior knob picks one of two paths:
    //   knob ≈ 0  → direct write (snap): registers latch at next tick.
    //                Same TICK_LATCHED click character as original v1.5.
    //   knob > 0  → slew on engines[0] over (knob * max_delta) samples.
    //                knob=1.0 reproduces the full Phase 18 glide duration.
    constexpr float kSnapThreshold = 0.001f;
    const float behavior = paramMorphSpeed->get();
    if (morphActive.load(std::memory_order_acquire))
    {
        float pos = paramMorphPosition->get();
        const bool forceReapply =
            morphReapplyPending.exchange(false, std::memory_order_relaxed);
        if (pos != lastMorphPosition || forceReapply)
        {
            const bool wasForced = forceReapply;
            lastMorphPosition = pos;

            // Forced re-applies (LOAD, SAVE/REVERT exit) snap regardless of
            // the Morph Speed knob -- the user just clicked an action button
            // and expects an instantaneous response, not a glide.
            const bool tookSnapPath =
                wasForced || !morphInitialized || behavior <= kSnapThreshold;
            if (tookSnapPath)
            {
                // First-time apply OR snap path: write registers directly.
                spu94_interp_set_morph(engines[0], pos);
                morphInitialized = true;
            }
            else
            {
                // Glide path: compute targets on scratch, arm slew on
                // engines[0], then scale slew duration by the knob so
                // smaller behavior values produce shorter slews.
                spu94_interp_set_morph(engines[1], pos);
                spu94_apply_pending_writes(engines[1]);
                spu94_snapshot_registers(engines[1], targetRegs);
                spu94_set_slew_targets(engines[0], targetRegs);
                int32_t maxDelta = 1;
                if (spu94_is_slewing(engines[0])) {
                    const int range = morphSpeedRange.load(std::memory_order_relaxed);
                    if (range == 0)
                        maxDelta = std::max(1, (int)(behavior * 22050.0f));
                    else
                        maxDelta = std::max(1, (int)(11025.0f + behavior * 165375.0f));
                }
                spu94_set_slew_duration(engines[0], maxDelta);
            }
            // Sync shadows from the engine that holds the FINAL target
            // register values: engines[0] in the snap path (immediate),
            // engines[1] in the glide path (engines[0] is mid-slew and
            // would give a stale snapshot). Cheap (35*int16 memcpy) and
            // dontSendNotification on the GUI side keeps it from
            // interfering with active slider drags in Advanced view.
            registerBridge.syncShadowsFromSPU(tookSnapPath ? engines[0] : engines[1]);
            shadowSyncCompletedCount.fetch_add(1, std::memory_order_release);
        }
    }

    // ------------------------------------------------------------------
    // AUDIO I/O -- split into standalone (v1.6 WavSource testbed) and
    // plugin (Phase 22 SRC sandwich on the host buffer) branches.
    // ------------------------------------------------------------------
    const int n = buffer.getNumSamples();
    constexpr int kMaxBlock = 4096;
    jassert(n <= kMaxBlock);
    if (n > kMaxBlock) { buffer.clear(); return; }

    // Side-channel limiter constants (unchanged from v1.6 standalone).
    // Tanh-shape ceiling on the L-R difference; mid (L+R)/2 is preserved.
    constexpr float kSideKnee    = 0.125f;
    constexpr float kSideCeiling = 0.06f;

    const bool isStandalone = (wrapperType == wrapperType_Standalone);

    if (isStandalone)
    {
        // === STANDALONE PATH (v1.6 back-compat) =========================

        // Apply pending GUI commands on the audio thread (CR-01/CR-02 fix)
        if (pendingMixerEnable.exchange(false, std::memory_order_acquire))
        {
            auto* mx = spu94_get_voice_mixer();
            mx->enabled = 1;
            mx->master_vol_l = 0x7FFF;
            mx->master_vol_r = 0x7FFF;
        }
        {
            uint16_t trigPitch = pendingGuiTriggerPitch.exchange(0, std::memory_order_acquire);
            if (trigPitch != 0)
            {
                auto* mx = spu94_get_voice_mixer();
                uint32_t startAddr = posToBlockAddr(sampleStartPos.load(std::memory_order_relaxed));
                spu94_adsr_state_t adsrCfg = buildAdsrConfig();
                spu94_voice_mixer_key_on(mx, 0, startAddr, trigPitch,
                                         guiVoiceVolL.load(std::memory_order_relaxed),
                                         guiVoiceVolR.load(std::memory_order_relaxed),
                                         1, adsrCfg.enabled ? &adsrCfg : nullptr);
                uint32_t blockIdx = startAddr / SPU94_ADPCM_BLOCK_BYTES;
                if (blockIdx < adpcmStateCache.size()) {
                    mx->voices[0].adpcm_state.old = adpcmStateCache[blockIdx].old;
                    mx->voices[0].adpcm_state.older = adpcmStateCache[blockIdx].older;
                }
                uint32_t endAddr;
                bool nonOn = guiVoiceNon.load(std::memory_order_relaxed);
                if (nonOn && voiceSampleBytes == 0) {
                    endAddr = SPU94_SPU_RAM_BYTES;
                    mx->pending_config[0].loop_enabled = 1;
                    mx->pending_config[0].loop_addr = 0;
                } else {
                    endAddr = posToBlockAddr(sampleEndPos.load(std::memory_order_relaxed));
                }
                if (endAddr <= startAddr)
                    endAddr = startAddr + SPU94_ADPCM_BLOCK_BYTES;
                mx->pending_config[0].end_addr = endAddr;
                bool looping = loopModeEnabled.load(std::memory_order_relaxed);
                mx->pending_config[0].loop_enabled = looping ? 1 : 0;
                uint32_t loopAddr = posToBlockAddr(sampleLoopPos.load(std::memory_order_relaxed));
                if (loopAddr < startAddr) loopAddr = startAddr;
                if (loopAddr >= endAddr) loopAddr = (endAddr > SPU94_ADPCM_BLOCK_BYTES)
                    ? endAddr - SPU94_ADPCM_BLOCK_BYTES : startAddr;
                mx->pending_config[0].loop_addr = loopAddr;
            }
        }
        if (pendingGuiStop.exchange(false, std::memory_order_acquire))
            spu94_voice_mixer_key_off(spu94_get_voice_mixer(), 0);

        spu94_voice_mixer_set_pitch(spu94_get_voice_mixer(), 0,
            guiVoicePitch.load(std::memory_order_relaxed));

        // Apply Pan/Level continuously — writes base_vol so sweep scales relative to pan position
        {
            auto* mx = spu94_get_voice_mixer();
            mx->voices[0].base_vol_l = guiVoiceVolL.load(std::memory_order_relaxed);
            mx->voices[0].base_vol_r = guiVoiceVolR.load(std::memory_order_relaxed);
        }

        // Apply NON/PMON toggles on voice 0 (Phase 40: voice feature toggles)
        {
            bool nonOn = guiVoiceNon.load(std::memory_order_relaxed);
            spu94_voice_mixer_set_non(spu94_get_voice_mixer(), 0, nonOn ? 1 : 0);
            auto* mx = spu94_get_voice_mixer();
            int shift = noiseShift.load(std::memory_order_relaxed);
            if (shift < 0) shift = 0;
            if (shift > 15) shift = 15;
            mx->noise_gen.shift = (uint8_t)shift;
            mx->noise_gen.step = (uint8_t)(4 + (shift > 7 ? 3 : shift / 3));
        }
        spu94_voice_mixer_set_pmon(spu94_get_voice_mixer(), 0,
            guiVoicePmon.load(std::memory_order_relaxed) ? 1 : 0);

        if (effectModeChanged.exchange(false, std::memory_order_acquire))
        {
            auto* mx = spu94_get_voice_mixer();
            mx->voices[0].sweep_l.active = 0;
            mx->voices[0].sweep_r.active = 0;
            tremoloWasActive = false;
            autoPanWasActive = false;
            amWasActive = false;
            ringModWasActive = false;
        }

        // Tremolo activation (Phase 44: continuous VCA oscillation via retrigger)
        // Configures both L/R sweeps with retrigger_enable=1 at Hz-derived shift/step.
        {
            bool tremEn = tremoloEnabled.load(std::memory_order_relaxed);
            if (tremEn && tremoloWasActive) {
                auto* mx = spu94_get_voice_mixer();
                if (!mx->voices[0].sweep_l.active) tremoloWasActive = false;
            }
            if (tremEn && !tremoloWasActive)
            {
                // Tremolo just enabled: configure sweeps for continuous oscillation
                float hz    = tremoloSpeedHz.load(std::memory_order_relaxed);
                int   curve = tremoloCurve.load(std::memory_order_relaxed);
                float ratio = tremoloRatio.load(std::memory_order_relaxed);
                int   shape = sweepShape.load(std::memory_order_relaxed);

                // T-44-01: clamp Hz to table range
                if (hz < 0.5f) hz = 0.5f;
                if (hz > 43.0f) hz = 43.0f;
                // T-53-01: clamp shape to valid range
                if (shape < 0) shape = 0;
                if (shape > 2) shape = 2;

                auto ss = hzToShift(hz);

                // L channel: start decreasing from current vol (direction=1)
                spu94_voice_mixer_set_sweep_l(spu94_get_voice_mixer(), 0,
                    static_cast<uint8_t>(curve & 1),  // mode: 0=linear, 1=exponential
                    1,  // direction=decrease (start from current vol going down)
                    0,  // phase=positive
                    ss.shift, ss.step, 1, 0,
                    static_cast<uint8_t>(shape));  // retrigger_enable=1, bipolar=0, shape

                // R channel: apply ratio for independent rate
                // T-44-03: clamp ratio to safe range
                if (ratio < 0.5f) ratio = 0.5f;
                if (ratio > 4.0f) ratio = 4.0f;
                uint8_t r_shift = ss.shift;
                if (ratio > 1.0f) {
                    int offset = static_cast<int>(std::floor(std::log2(ratio)));
                    r_shift = static_cast<uint8_t>(std::min(31, (int)ss.shift + offset));
                } else if (ratio < 1.0f) {
                    int offset = static_cast<int>(std::floor(std::log2(1.0f / ratio)));
                    r_shift = static_cast<uint8_t>(std::max(0, (int)ss.shift - offset));
                }

                spu94_voice_mixer_set_sweep_r(spu94_get_voice_mixer(), 0,
                    static_cast<uint8_t>(curve & 1),
                    1, 0, r_shift, ss.step, 1, 0,
                    static_cast<uint8_t>(shape));  // retrigger_enable=1, bipolar=0, shape

                lastTremoloHz = hz;
                lastTremoloCurve = curve;
                lastTremoloRatio = ratio;
                lastSweepShape = shape;
                tremoloWasActive = true;

                // AM-04: Tremolo takes priority -- force-deactivate AM if it was active
                if (amWasActive) amWasActive = false;
                // Ring mod has lowest priority -- force-deactivate if it was active
                if (ringModWasActive) ringModWasActive = false;
            }
            else if (tremEn && tremoloWasActive)
            {
                // Tremolo already active: check if parameters changed
                float hz    = tremoloSpeedHz.load(std::memory_order_relaxed);
                int   curve = tremoloCurve.load(std::memory_order_relaxed);
                float ratio = tremoloRatio.load(std::memory_order_relaxed);
                int   shape = sweepShape.load(std::memory_order_relaxed);

                if (hz < 0.5f) hz = 0.5f;
                if (hz > 43.0f) hz = 43.0f;
                if (ratio < 0.5f) ratio = 0.5f;
                if (ratio > 4.0f) ratio = 4.0f;
                if (shape < 0) shape = 0;
                if (shape > 2) shape = 2;

                if (hz != lastTremoloHz || curve != lastTremoloCurve || ratio != lastTremoloRatio || shape != lastSweepShape)
                {
                    auto ss = hzToShift(hz);
                    auto* mx = spu94_get_voice_mixer();

                    // Update speed/curve/shape without resetting level/counter —
                    // oscillation keeps running at new rate/shape
                    mx->voices[0].sweep_l.shift = ss.shift;
                    mx->voices[0].sweep_l.step = ss.step;
                    mx->voices[0].sweep_l.mode = static_cast<uint8_t>(curve & 1);
                    mx->voices[0].sweep_l.shape = static_cast<uint8_t>(shape);

                    uint8_t r_shift = ss.shift;
                    if (ratio > 1.0f) {
                        int offset = static_cast<int>(std::floor(std::log2(ratio)));
                        r_shift = static_cast<uint8_t>(std::min(31, (int)ss.shift + offset));
                    } else if (ratio < 1.0f) {
                        int offset = static_cast<int>(std::floor(std::log2(1.0f / ratio)));
                        r_shift = static_cast<uint8_t>(std::max(0, (int)ss.shift - offset));
                    }

                    mx->voices[0].sweep_r.shift = r_shift;
                    mx->voices[0].sweep_r.step = ss.step;
                    mx->voices[0].sweep_r.mode = static_cast<uint8_t>(curve & 1);
                    mx->voices[0].sweep_r.shape = static_cast<uint8_t>(shape);

                    lastTremoloHz = hz;
                    lastTremoloCurve = curve;
                    lastTremoloRatio = ratio;
                    lastSweepShape = shape;
                }
            }
            else if (!tremEn && tremoloWasActive)
            {
                // Tremolo just disabled: deactivate sweeps
                auto* mx = spu94_get_voice_mixer();
                mx->voices[0].sweep_l.active = 0;
                mx->voices[0].sweep_r.active = 0;
                tremoloWasActive = false;
            }

            if (tremEn)
            {
                auto* mx = spu94_get_voice_mixer();
                float depth = tremoloDepth.load(std::memory_order_relaxed);
                if (depth < 0.0f) depth = 0.0f;
                if (depth > 1.0f) depth = 1.0f;
                int16_t dq = static_cast<int16_t>(depth * 0x7FFF);
                mx->voices[0].sweep_depth_l = dq;
                mx->voices[0].sweep_depth_r = dq;
            }
        }

        // Auto-pan activation (Phase 45: opposition-phase L/R sweep for stereo movement)
        // PAN-05: Mutual exclusion -- auto-pan skipped if tremolo is active (tremolo wins).
        // PAN-04: Always linear (mode=0) -- no equal-power, no exponential.
        // PAN-01: L direction=1 (decrease), R direction=0 (increase) -- OPPOSITION.
        {
            bool panEn  = autoPanEnabled.load(std::memory_order_relaxed);
            if (panEn && autoPanWasActive) {
                auto* mx = spu94_get_voice_mixer();
                if (!mx->voices[0].sweep_l.active) autoPanWasActive = false;
            }
            if (panEn && !autoPanWasActive)
            {
                // Auto-pan just enabled: configure L/R sweeps in opposition
                float hz    = autoPanSpeedHz.load(std::memory_order_relaxed);
                float ratio = autoPanRatio.load(std::memory_order_relaxed);
                int   shape = sweepShape.load(std::memory_order_relaxed);

                // T-45-01: clamp Hz to table range
                if (hz < 0.5f) hz = 0.5f;
                if (hz > 43.0f) hz = 43.0f;
                if (shape < 0) shape = 0;
                if (shape > 2) shape = 2;

                auto ss = hzToShift(hz);

                // L channel: direction=1 (decrease from current vol)
                spu94_voice_mixer_set_sweep_l(spu94_get_voice_mixer(), 0,
                    0,  // mode=0 (linear always, PAN-04)
                    1,  // direction=decrease (L starts going down)
                    0,  // phase=positive
                    ss.shift, ss.step, 1, 0,
                    static_cast<uint8_t>(shape));  // retrigger_enable=1, bipolar=0, shape

                // R channel: direction=0 (increase) -- OPPOSITION to L
                // Apply ratio for independent rate
                if (ratio < 0.5f) ratio = 0.5f;
                if (ratio > 4.0f) ratio = 4.0f;
                uint8_t r_shift = ss.shift;
                if (ratio > 1.0f) {
                    int offset = static_cast<int>(std::floor(std::log2(ratio)));
                    r_shift = static_cast<uint8_t>(std::min(31, (int)ss.shift + offset));
                } else if (ratio < 1.0f) {
                    int offset = static_cast<int>(std::floor(std::log2(1.0f / ratio)));
                    r_shift = static_cast<uint8_t>(std::max(0, (int)ss.shift - offset));
                }

                spu94_voice_mixer_set_sweep_r(spu94_get_voice_mixer(), 0,
                    0,  // mode=0 (linear always, PAN-04)
                    0,  // direction=increase (R goes UP while L goes DOWN)
                    0, r_shift, ss.step, 1, 0,
                    static_cast<uint8_t>(shape));  // retrigger_enable=1, bipolar=0, shape

                // R must start at 0 so it increases WHILE L decreases from 0x7FFF.
                // set_sweep_r copies vol_r as starting level, but for opposition
                // panning R needs to start at the opposite end of its range.
                spu94_get_voice_mixer()->voices[0].sweep_r.level = 0;

                lastAutoPanHz = hz;
                lastAutoPanRatio = ratio;
                lastSweepShape = shape;
                autoPanWasActive = true;

                // AM-04: Auto-pan takes priority -- force-deactivate AM if it was active
                if (amWasActive) amWasActive = false;
                // Ring mod has lowest priority -- force-deactivate if it was active
                if (ringModWasActive) ringModWasActive = false;
            }
            else if (panEn && autoPanWasActive)
            {
                // Auto-pan already active: check if parameters changed
                float hz    = autoPanSpeedHz.load(std::memory_order_relaxed);
                float ratio = autoPanRatio.load(std::memory_order_relaxed);
                int   shape = sweepShape.load(std::memory_order_relaxed);

                if (hz < 0.5f) hz = 0.5f;
                if (hz > 43.0f) hz = 43.0f;
                if (ratio < 0.5f) ratio = 0.5f;
                if (ratio > 4.0f) ratio = 4.0f;
                if (shape < 0) shape = 0;
                if (shape > 2) shape = 2;

                if (hz != lastAutoPanHz || ratio != lastAutoPanRatio || shape != lastSweepShape)
                {
                    auto ss = hzToShift(hz);
                    auto* mx = spu94_get_voice_mixer();

                    // Update speed/shape without resetting level/counter/direction —
                    // the oscillation keeps running, just at a new rate/shape
                    mx->voices[0].sweep_l.shift = ss.shift;
                    mx->voices[0].sweep_l.step = ss.step;
                    mx->voices[0].sweep_l.shape = static_cast<uint8_t>(shape);

                    uint8_t r_shift = ss.shift;
                    if (ratio > 1.0f) {
                        int offset = static_cast<int>(std::floor(std::log2(ratio)));
                        r_shift = static_cast<uint8_t>(std::min(31, (int)ss.shift + offset));
                    } else if (ratio < 1.0f) {
                        int offset = static_cast<int>(std::floor(std::log2(1.0f / ratio)));
                        r_shift = static_cast<uint8_t>(std::max(0, (int)ss.shift - offset));
                    }

                    mx->voices[0].sweep_r.shift = r_shift;
                    mx->voices[0].sweep_r.step = ss.step;
                    mx->voices[0].sweep_r.shape = static_cast<uint8_t>(shape);

                    lastAutoPanHz = hz;
                    lastAutoPanRatio = ratio;
                    lastSweepShape = shape;
                }
            }
            else if (!panEn && autoPanWasActive)
            {
                // Auto-pan just disabled: deactivate sweeps
                auto* mx = spu94_get_voice_mixer();
                mx->voices[0].sweep_l.active = 0;
                mx->voices[0].sweep_r.active = 0;
                autoPanWasActive = false;
            }

            // Auto-pan depth scaling: sweep levels directly become vol_l/vol_r.
            // L sweep decreases (0x7FFF→0) while R increases (0→0x7FFF) in opposition.
            // Depth scales the deviation from the base pan/level volume.
            // At depth=1.0: vol follows sweep level directly (full hard-pan excursion).
            // At depth=0.0: vol stays at the pan/level base (no panning).
            if (panEn)
            {
                auto* mx = spu94_get_voice_mixer();
                float depth = autoPanDepth.load(std::memory_order_relaxed);
                if (depth < 0.0f) depth = 0.0f;
                if (depth > 1.0f) depth = 1.0f;
                int16_t dq = static_cast<int16_t>(depth * 0x7FFF);
                mx->voices[0].sweep_depth_l = dq;
                mx->voices[0].sweep_depth_r = dq;
            }
        }

        // AM synthesis activation (Phase 48: audio-rate retrigger for metallic sidebands)
        // AM-04: Mutual exclusion -- AM has LOWEST priority (tremolo and auto-pan both win).
        // Both L and R run at the same rate (no ratio) -- AM produces sidebands, not stereo movement.
        {
            bool amEn   = amEnabled.load(std::memory_order_relaxed);

            if (amEn && amWasActive) {
                auto* mx = spu94_get_voice_mixer();
                if (!mx->voices[0].sweep_l.active) amWasActive = false;
            }
            if (amEn && !amWasActive)
            {
                float hz   = amRateHz.load(std::memory_order_relaxed);
                int   curve = amCurve.load(std::memory_order_relaxed);
                int   shape = sweepShape.load(std::memory_order_relaxed);

                // T-48-01: clamp Hz to table range
                if (hz < kAmHzTable[0].hz) hz = kAmHzTable[0].hz;
                if (hz > kAmHzTable[kAmHzTableSize - 1].hz) hz = kAmHzTable[kAmHzTableSize - 1].hz;
                if (shape < 0) shape = 0;
                if (shape > 2) shape = 2;

                auto ss = hzToShiftAm(hz);

                // Both L and R: same direction=1 (decrease from current vol), same rate
                spu94_voice_mixer_set_sweep_l(spu94_get_voice_mixer(), 0,
                    static_cast<uint8_t>(curve & 1),  // mode: 0=linear, 1=exponential
                    1,  // direction=decrease (start from current vol going down)
                    0,  // phase=positive
                    ss.shift, ss.step, 1, 0,
                    static_cast<uint8_t>(shape));  // retrigger_enable=1, bipolar=0, shape

                spu94_voice_mixer_set_sweep_r(spu94_get_voice_mixer(), 0,
                    static_cast<uint8_t>(curve & 1),
                    1, 0, ss.shift, ss.step, 1, 0,
                    static_cast<uint8_t>(shape));  // retrigger_enable=1, bipolar=0, shape

                lastAmHz = hz;
                lastAmCurve = curve;
                lastSweepShape = shape;
                amWasActive = true;

                // Ring mod has lowest priority -- force-deactivate if it was active
                if (ringModWasActive) ringModWasActive = false;
            }
            else if (amEn && amWasActive)
            {
                // AM already active: check if parameters changed
                float hz   = amRateHz.load(std::memory_order_relaxed);
                int   curve = amCurve.load(std::memory_order_relaxed);
                int   shape = sweepShape.load(std::memory_order_relaxed);

                if (hz < kAmHzTable[0].hz) hz = kAmHzTable[0].hz;
                if (hz > kAmHzTable[kAmHzTableSize - 1].hz) hz = kAmHzTable[kAmHzTableSize - 1].hz;
                if (shape < 0) shape = 0;
                if (shape > 2) shape = 2;

                if (hz != lastAmHz || curve != lastAmCurve || shape != lastSweepShape)
                {
                    auto ss = hzToShiftAm(hz);
                    auto* mx = spu94_get_voice_mixer();

                    mx->voices[0].sweep_l.shift = ss.shift;
                    mx->voices[0].sweep_l.step = ss.step;
                    mx->voices[0].sweep_l.mode = static_cast<uint8_t>(curve & 1);
                    mx->voices[0].sweep_l.shape = static_cast<uint8_t>(shape);
                    mx->voices[0].sweep_r.shift = ss.shift;
                    mx->voices[0].sweep_r.step = ss.step;
                    mx->voices[0].sweep_r.mode = static_cast<uint8_t>(curve & 1);
                    mx->voices[0].sweep_r.shape = static_cast<uint8_t>(shape);

                    lastAmHz = hz;
                    lastAmCurve = curve;
                    lastSweepShape = shape;
                }
            }
            else if (!amEn && amWasActive)
            {
                // AM just disabled (either explicitly or by mutual exclusion): deactivate sweeps
                auto* mx = spu94_get_voice_mixer();
                mx->voices[0].sweep_l.active = 0;
                mx->voices[0].sweep_r.active = 0;
                amWasActive = false;
            }

            if (amEn)
            {
                auto* mx = spu94_get_voice_mixer();
                float depth = amDepth.load(std::memory_order_relaxed);
                if (depth < 0.0f) depth = 0.0f;
                if (depth > 1.0f) depth = 1.0f;
                int16_t dq = static_cast<int16_t>(depth * 0x7FFF);
                mx->voices[0].sweep_depth_l = dq;
                mx->voices[0].sweep_depth_r = dq;
            }
        }

        // Ring mod activation (Phase 52: bipolar sweep for phase-inversion ring mod effect)
        // Replaces Phase 49's phase mod workaround. Ring mod uses native bipolar sweep
        // (level traverses +0x7FFF through 0 to -0x7FFF) with the same depth formula as
        // tremolo/AM. No host-layer remapping needed -- bipolar sweep produces negative
        // volume naturally at high depth.
        // Mutual exclusion: ring mod has LOWEST priority (tremolo, auto-pan, AM all win).
        {
            bool rmEn   = ringModEnabled.load(std::memory_order_relaxed);
            if (rmEn && ringModWasActive) {
                auto* mx = spu94_get_voice_mixer();
                if (!mx->voices[0].sweep_l.active) ringModWasActive = false;
            }

            if (rmEn && !ringModWasActive)
            {
                // Ring mod just enabled: configure sweeps for bipolar oscillation
                float hz   = ringModRateHz.load(std::memory_order_relaxed);
                int   curve = ringModCurve.load(std::memory_order_relaxed);
                int   shape = sweepShape.load(std::memory_order_relaxed);

                // Clamp Hz to AM table range (ring mod uses same audio-rate table)
                if (hz < kAmHzTable[0].hz) hz = kAmHzTable[0].hz;
                if (hz > kAmHzTable[kAmHzTableSize - 1].hz) hz = kAmHzTable[kAmHzTableSize - 1].hz;
                if (shape < 0) shape = 0;
                if (shape > 2) shape = 2;

                auto ss = hzToShiftAm(hz);

                // Both L and R: direction=1 (decrease from current vol), phase=0 (positive),
                // retrigger_enable=1, bipolar=1 -- sweep crosses zero into negative territory
                spu94_voice_mixer_set_sweep_l(spu94_get_voice_mixer(), 0,
                    static_cast<uint8_t>(curve & 1),  // mode: 0=linear, 1=exponential
                    1,  // direction=decrease (start from current vol going down)
                    0,  // phase=positive (bipolar handles zero crossing)
                    ss.shift, ss.step, 1, 1,
                    static_cast<uint8_t>(shape));  // retrigger_enable=1, bipolar=1, shape

                spu94_voice_mixer_set_sweep_r(spu94_get_voice_mixer(), 0,
                    static_cast<uint8_t>(curve & 1),
                    1, 0, ss.shift, ss.step, 1, 1,
                    static_cast<uint8_t>(shape));  // retrigger_enable=1, bipolar=1, shape

                lastRingModHz = hz;
                lastRingModCurve = curve;
                lastSweepShape = shape;
                ringModWasActive = true;
            }
            else if (rmEn && ringModWasActive)
            {
                float hz   = ringModRateHz.load(std::memory_order_relaxed);
                int   curve = ringModCurve.load(std::memory_order_relaxed);
                int   shape = sweepShape.load(std::memory_order_relaxed);

                if (hz < kAmHzTable[0].hz) hz = kAmHzTable[0].hz;
                if (hz > kAmHzTable[kAmHzTableSize - 1].hz) hz = kAmHzTable[kAmHzTableSize - 1].hz;
                if (shape < 0) shape = 0;
                if (shape > 2) shape = 2;

                if (hz != lastRingModHz || curve != lastRingModCurve || shape != lastSweepShape)
                {
                    auto ss = hzToShiftAm(hz);
                    auto* mx = spu94_get_voice_mixer();

                    mx->voices[0].sweep_l.shift = ss.shift;
                    mx->voices[0].sweep_l.step = ss.step;
                    mx->voices[0].sweep_l.mode = static_cast<uint8_t>(curve & 1);
                    mx->voices[0].sweep_l.shape = static_cast<uint8_t>(shape);
                    mx->voices[0].sweep_r.shift = ss.shift;
                    mx->voices[0].sweep_r.step = ss.step;
                    mx->voices[0].sweep_r.mode = static_cast<uint8_t>(curve & 1);
                    mx->voices[0].sweep_r.shape = static_cast<uint8_t>(shape);

                    lastRingModHz = hz;
                    lastRingModCurve = curve;
                    lastSweepShape = shape;
                }
            }
            else if (!rmEn && ringModWasActive)
            {
                // Ring mod just disabled (explicitly or by mutual exclusion): deactivate sweeps
                auto* mx = spu94_get_voice_mixer();
                mx->voices[0].sweep_l.active = 0;
                mx->voices[0].sweep_r.active = 0;
                ringModWasActive = false;
            }

            if (rmEn)
            {
                auto* mx = spu94_get_voice_mixer();
                float depth = ringModDepth.load(std::memory_order_relaxed);
                if (depth < 0.0f) depth = 0.0f;
                if (depth > 1.0f) depth = 1.0f;
                int16_t dq = static_cast<int16_t>(depth * 0x7FFF);
                mx->voices[0].sweep_depth_l = dq;
                mx->voices[0].sweep_depth_r = dq;
            }
        }

        // Update all marker positions on voice 0 in real time
        {
            auto* mx = spu94_get_voice_mixer();
            uint32_t sAddr = posToBlockAddr(sampleStartPos.load(std::memory_order_relaxed));
            uint32_t eAddr = posToBlockAddr(sampleEndPos.load(std::memory_order_relaxed));
            uint32_t lAddr = posToBlockAddr(sampleLoopPos.load(std::memory_order_relaxed));

            if (eAddr <= sAddr)
                eAddr = sAddr + SPU94_ADPCM_BLOCK_BYTES;
            if (lAddr < sAddr) lAddr = sAddr;
            if (lAddr >= eAddr) lAddr = (eAddr > SPU94_ADPCM_BLOCK_BYTES)
                ? eAddr - SPU94_ADPCM_BLOCK_BYTES : sAddr;

            mx->voices[0].end_addr = eAddr;
            mx->voices[0].sample_start_addr = sAddr;
            mx->voices[0].loop_addr = lAddr;
            mx->voices[0].loop_enabled = loopModeEnabled.load(std::memory_order_relaxed) ? 1 : 0;

            if (mx->voices[0].active) {
                uint32_t cur = mx->voices[0].current_addr;
                if (cur < sAddr || cur >= eAddr) {
                    mx->voices[0].current_addr = sAddr;
                    mx->voices[0].has_block = 0;
                    uint32_t blockIdx = sAddr / SPU94_ADPCM_BLOCK_BYTES;
                    if (blockIdx < adpcmStateCache.size()) {
                        auto& cached = adpcmStateCache[blockIdx];
                        mx->voices[0].adpcm_state.old = cached.old;
                        mx->voices[0].adpcm_state.older = cached.older;
                        mx->voices[0].loop_adpcm_old = cached.old;
                        mx->voices[0].loop_adpcm_older = cached.older;
                    }
                }
            }

            if (mx->voices[0].active && mx->voices[0].adsr.enabled) {
                spu94_adsr_state_t cfg = buildAdsrConfig();
                if (cfg.enabled) {
                    auto& ad = mx->voices[0].adsr;
                    ad.attack_shift  = cfg.attack_shift;
                    ad.attack_step   = cfg.attack_step;
                    ad.attack_exp    = cfg.attack_exp;
                    ad.decay_shift   = cfg.decay_shift;
                    ad.decay_step    = cfg.decay_step;
                    ad.sustain_level = cfg.sustain_level;
                    ad.sustain_shift = cfg.sustain_shift;
                    ad.sustain_step  = cfg.sustain_step;
                    ad.sustain_exp   = cfg.sustain_exp;
                    ad.sustain_dir   = cfg.sustain_dir;
                    ad.release_shift = cfg.release_shift;
                    ad.release_step  = cfg.release_step;
                    ad.release_exp   = cfg.release_exp;
                }
            }
        }

        // MIDI dispatch -- process note events before spu94_process (Phase 31)
        if (voiceSampleLoaded.load(std::memory_order_acquire))
        {
            for (const auto metadata : midiMessages)
            {
                auto msg = metadata.getMessage();
                if (msg.isNoteOn())
                {
                    int note = msg.getNoteNumber();
                    uint16_t pitch = midiNoteToPitch(note);
                    int vel = msg.getVelocity();
                    int16_t vol = static_cast<int16_t>((vel * 0x7FFF) / 127);
                    int voice = allocateVoice(note);
                    spu94_adsr_state_t midiAdsr = buildAdsrConfig();
                    spu94_voice_mixer_key_on(spu94_get_voice_mixer(), voice,
                        0, pitch, vol, vol, 1,
                        midiAdsr.enabled ? &midiAdsr : nullptr);
                }
                else if (msg.isNoteOff())
                {
                    int voice = findVoiceForNote(msg.getNoteNumber());
                    if (voice >= 0)
                        spu94_voice_mixer_key_off(spu94_get_voice_mixer(), voice);
                }
            }
        }

        // Phase 46: Sidechain duck — KON snapshot and duck trigger.
        // Read pending_kon BEFORE spu94_process (which calls mixer_tick and clears it).
        // This tells us which voices are about to key-on this block.
        {
            auto* mx = spu94_get_voice_mixer();
            uint32_t konSnapshot = mx->pending_kon;

            if (konSnapshot != 0)
            {
                bool tremEn = tremoloEnabled.load(std::memory_order_relaxed);
                bool panEn  = autoPanEnabled.load(std::memory_order_relaxed);
                bool amEn   = amEnabled.load(std::memory_order_relaxed);
                bool rmEn   = ringModEnabled.load(std::memory_order_relaxed);

                for (int v = 0; v < 24; ++v)
                {
                    if (duckState[v] != DUCK_IDLE) continue;

                    int src = duckSource[v].load(std::memory_order_relaxed);
                    // T-46-01: clamp source index to valid range
                    if (src < 0 || src > 23) continue;
                    // T-46-02: self-duck blocked
                    if (src == v) continue;
                    // Mutual exclusion: duck disabled on voice 0 when tremolo/auto-pan/AM/ringMod active
                    if (v == 0 && (tremEn || panEn || amEn || rmEn)) continue;

                    // Check if the source voice is triggering KON this block
                    if (!(konSnapshot & (1u << src))) continue;

                    // Duck triggers on voice v!
                    duckOrigLevel_l[v] = mx->voices[v].vol_l;
                    duckOrigLevel_r[v] = mx->voices[v].vol_r;

                    // Configure exponential decrease using configurable attack time
                    // T-54-02: clamp minimum to 0.001s to prevent division by zero
                    float atkSec = duckAttack[v].load(std::memory_order_relaxed);
                    if (atkSec < 0.001f) atkSec = 0.001f;
                    if (atkSec > 0.5f) atkSec = 0.5f;
                    auto atkSS = speedToShift(atkSec);
                    spu94_voice_mixer_set_sweep_l(mx, v, 1, 1, 0, atkSS.shift, atkSS.step, 0, 0, 0);
                    spu94_voice_mixer_set_sweep_r(mx, v, 1, 1, 0, atkSS.shift, atkSS.step, 0, 0, 0);
                    duckState[v] = DUCK_DECREASING;
                }
            }
        }

        // wavSource gate: when no WAV is loaded or playback is paused,
        // emit silence. This preserves the standalone testbed's
        // Load-Play-Stop behaviour byte-identically with end-of-Phase-21.
        const bool wavActive = wavSource.loaded.load(std::memory_order_acquire)
                             && wavSource.playing.load(std::memory_order_relaxed)
                             && wavSource.numFrames > 0;

        const auto numFrames = wavSource.numFrames;

        const int samplesToProcess = n;

        int16_t tmpL_in [kMaxBlock];
        int16_t tmpR_in [kMaxBlock];
        int16_t splitVoiceL[kMaxBlock];
        int16_t splitVoiceR[kMaxBlock];
        int16_t splitRevL  [kMaxBlock];
        int16_t splitRevR  [kMaxBlock];

        // Phase 23 UAT: pre-clamp float gain on standalone path too.
        // int16 WAV sample -> float -> apply gain -> clamp+truncate back to int16.
        // Phase 24: read from AudioParameterFloat instead of atomic.
        const float inputGain = paramInputGain->get();
        auto playPos = wavSource.playPos.load(std::memory_order_relaxed);
        if (wavActive) {
            for (int i = 0; i < samplesToProcess; ++i)
            {
                const auto idx = static_cast<size_t>(
                    (playPos + static_cast<uint64_t>(i)) % numFrames);
                const float fL = spu94::plugin::boundary::toFloat(wavSource.L[idx]);
                const float fR = spu94::plugin::boundary::toFloat(wavSource.R[idx]);
                tmpL_in[i] = spu94::plugin::boundary::toInt16(
                    spu94::plugin::boundary::applyInputGain(fL, inputGain));
                tmpR_in[i] = spu94::plugin::boundary::toInt16(
                    spu94::plugin::boundary::applyInputGain(fR, inputGain));
            }
        } else {
            for (int i = 0; i < samplesToProcess; ++i) {
                tmpL_in[i] = 0;
                tmpR_in[i] = 0;
            }
        }

        spu94_process_split(engines[0], tmpL_in, tmpR_in,
                            splitVoiceL, splitVoiceR,
                            splitRevL, splitRevR,
                            static_cast<uint32_t>(samplesToProcess));

        // Phase 46: Sidechain duck state machine — runs AFTER spu94_process
        // so sweeps have been ticked for this block.
        {
            auto* mx = spu94_get_voice_mixer();
            for (int v = 0; v < 24; ++v)
            {
                if (duckState[v] == DUCK_DECREASING)
                {
                    float depth = duckDepth[v].load(std::memory_order_relaxed);
                    if (depth < 0.0f) depth = 0.0f;
                    if (depth > 1.0f) depth = 1.0f;

                    int16_t floor_l = static_cast<int16_t>((1.0f - depth) * duckOrigLevel_l[v]);
                    int16_t floor_r = static_cast<int16_t>((1.0f - depth) * duckOrigLevel_r[v]);

                    // Check if decrease has reached the depth-scaled floor
                    bool l_done = (mx->voices[v].sweep_l.active == 0) ||
                                  (mx->voices[v].sweep_l.level <= floor_l);
                    bool r_done = (mx->voices[v].sweep_r.active == 0) ||
                                  (mx->voices[v].sweep_r.level <= floor_r);

                    if (l_done && r_done)
                    {
                        // Clamp at floor and deactivate decrease sweep
                        mx->voices[v].sweep_l.active = 0;
                        mx->voices[v].sweep_r.active = 0;
                        mx->voices[v].vol_l = floor_l;
                        mx->voices[v].vol_r = floor_r;

                        // Transition to recovery: exponential increase from floor back to original
                        float relSec = duckRelease[v].load(std::memory_order_relaxed);
                        auto ss = speedToShift(relSec);

                        // Set volume to floor before configuring increase sweep
                        // (set_sweep_l initializes level from current vol_l)
                        spu94_voice_mixer_set_sweep_l(mx, v, 1, 0, 0, ss.shift, ss.step, 0, 0, 0);
                        spu94_voice_mixer_set_sweep_r(mx, v, 1, 0, 0, ss.shift, ss.step, 0, 0, 0);

                        duckState[v] = DUCK_RECOVERING;
                    }
                }
                else if (duckState[v] == DUCK_RECOVERING)
                {
                    // Check if recovery sweep has reached the original level.
                    // One-shot sweeps stay active=1 at the clamping boundary,
                    // so check level >= target (not active==0).
                    bool l_recovered = (mx->voices[v].sweep_l.level >= duckOrigLevel_l[v]) ||
                                       (mx->voices[v].sweep_l.active == 0);
                    bool r_recovered = (mx->voices[v].sweep_r.level >= duckOrigLevel_r[v]) ||
                                       (mx->voices[v].sweep_r.active == 0);

                    if (l_recovered && r_recovered)
                    {
                        // Deactivate sweep and restore exact original volume
                        mx->voices[v].sweep_l.active = 0;
                        mx->voices[v].sweep_r.active = 0;
                        mx->voices[v].vol_l = duckOrigLevel_l[v];
                        mx->voices[v].vol_r = duckOrigLevel_r[v];
                        duckState[v] = DUCK_IDLE;
                    }
                }
            }
        }


        // Internal mod bus (Phase 50: noise-to-pitch/vol/pan per-voice modulation)
        // MOD-05: Runs at control rate (once per processBlock call) to set depths.
        // The actual modulation runs at sample rate inside spu94_voice_tick.
        // NOT mutually exclusive with sweep effects — mod bus coexists with everything.
        {
            float pitchD = modBusPitchDepth.load(std::memory_order_relaxed);
            float volD   = modBusVolDepth.load(std::memory_order_relaxed);
            float panD   = modBusPanDepth.load(std::memory_order_relaxed);
            // T-50-01: clamp floats to 0.0-1.0 at point of use before int16 conversion
            if (pitchD < 0.0f) pitchD = 0.0f;
            if (pitchD > 1.0f) pitchD = 1.0f;
            if (volD < 0.0f) volD = 0.0f;
            if (volD > 1.0f) volD = 1.0f;
            if (panD < 0.0f) panD = 0.0f;
            if (panD > 1.0f) panD = 1.0f;
            // Convert 0.0-1.0 float to int16 depth for C core
            int16_t pd   = static_cast<int16_t>(pitchD * 0x7FFF);
            int16_t vd   = static_cast<int16_t>(volD * 0x7FFF);
            int16_t pand = static_cast<int16_t>(panD * 0x7FFF);
            spu94_voice_mixer_set_mod_bus(spu94_get_voice_mixer(), 0, pd, vd, pand);
        }

        auto* outL = buffer.getWritePointer(0);
        auto* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;
        for (int i = 0; i < samplesToProcess; ++i)
        {
            const float vL = splitVoiceL[i] / 32768.0f;
            const float vR = splitVoiceR[i] / 32768.0f;

            const float rL = splitRevL[i] / 32768.0f;
            const float rR = splitRevR[i] / 32768.0f;
            const float revMid  = 0.5f * (rL + rR);
            const float revSide = 0.5f * (rL - rR);
            const float revSideLimited = std::tanh(revSide / kSideKnee) * kSideCeiling;

            outL[i] = vL + revMid + revSideLimited;
            if (outR) outR[i] = vR + revMid - revSideLimited;
        }


        if (wavActive) {
            wavSource.playPos.store(
                (playPos + static_cast<uint64_t>(samplesToProcess)) % numFrames,
                std::memory_order_relaxed);
        }
    }
    else
    {
        // === PLUGIN PATH (Phase 22: host buffer through SRC sandwich) ===
        // No WavSource gate -- the plugin processes host-provided audio
        // every block. setLatencySamples (called from prepareToPlay)
        // already handed the host the PDC number it needs.
        //
        // RT-safety: stack-allocated int16 scratch (32 KiB total at the
        // 4096-sample ceiling). srcChain_.processIn/processOut hold all
        // SRC state + float scratch internally; both are allocation-free.
        int16_t coreInL [kMaxBlock];
        int16_t coreInR [kMaxBlock];
        int16_t coreOutL[kMaxBlock];
        int16_t coreOutR[kMaxBlock];
        int16_t plugVoiceL[kMaxBlock];
        int16_t plugVoiceR[kMaxBlock];
        int16_t plugRevL  [kMaxBlock];
        int16_t plugRevR  [kMaxBlock];

        // Phase 25 / PLUG-34: stack scratch for the R channel when the host
        // negotiated mono output. SrcChain::processOut writes L+R into two
        // pointers; for mono output the R data would be lost without this
        // scratch. After processOut returns, we sum (L+R)*0.5 into the
        // single output channel.
        float monoRScratch[kMaxBlock];
        const bool monoOutput = (buffer.getNumChannels() == 1);

        // Phase 23 Plan 02 (D-01 + D-02): pre-clamp Input Gain stage.
        // Apply the atomic as a single float multiply on the host-rate
        // signal BEFORE the SRC sandwich, so the boundary clamp inside
        // toInt16 lives downstream of the gain knob. Sub-unity values
        // (e.g. 0.5 default = -6 dB) give the user real headroom below
        // the int16 ceiling; super-unity values (up to ~16.0 = +24 dB)
        // drive the signal hard into sat_s16, turning the boundary
        // into a deliberate saturator/overdrive (North Star).
        //
        // The atomic is read ONCE per block (per R4): per-sample loads
        // would create wasted release-acquire chains for no audible
        // benefit. The scalar loop calls applyInputGain (a named seam
        // in BoundaryConverter.h) so the pre-clamp pipeline has one
        // grep-able home.
        const float gain = paramInputGain->get();
        const float* rawL = buffer.getReadPointer(0);
        const float* rawR = buffer.getNumChannels() > 1
                                ? buffer.getReadPointer(1)
                                : buffer.getReadPointer(0);
        float* scratchL = inputGainScratch_[0].getData();
        float* scratchR = inputGainScratch_[1].getData();
        for (int i = 0; i < n; ++i)
        {
            scratchL[i] = spu94::plugin::boundary::applyInputGain(rawL[i], gain);
            scratchR[i] = spu94::plugin::boundary::applyInputGain(rawR[i], gain);
        }

        const float* hostInPtrs[2] = { scratchL, scratchR };
        int coreN = 0;
        srcChain_.processIn(hostInPtrs, n, coreInL, coreInR, coreN);

        // Bail out if the input SRC produced no frames (transient corner;
        // shouldn't happen at kMaxBlock>=64 with hostSR>=44100/5).
        if (coreN <= 0) { buffer.clear(); return; }

        spu94_process_split(engines[0], coreInL, coreInR,
                            plugVoiceL, plugVoiceR,
                            plugRevL, plugRevR,
                            static_cast<uint32_t>(coreN));

        for (int i = 0; i < coreN; ++i)
        {
            const float vL = plugVoiceL[i] / 32768.0f;
            const float vR = plugVoiceR[i] / 32768.0f;
            const float rL = plugRevL[i] / 32768.0f;
            const float rR = plugRevR[i] / 32768.0f;
            const float revMid  = 0.5f * (rL + rR);
            const float revSide = 0.5f * (rL - rR);
            const float revSideLimited = std::tanh(revSide / kSideKnee) * kSideCeiling;
            float oL = vL + revMid + revSideLimited;
            float oR = vR + revMid - revSideLimited;
            coreOutL[i] = static_cast<int16_t>(
                oL >= 1.0f ? 32767 : oL <= -1.0f ? -32768
                : static_cast<int16_t>(oL * 32768.0f));
            coreOutR[i] = static_cast<int16_t>(
                oR >= 1.0f ? 32767 : oR <= -1.0f ? -32768
                : static_cast<int16_t>(oR * 32768.0f));
        }

        float* hostOutPtrs[2] = {
            buffer.getWritePointer(0),
            monoOutput ? monoRScratch : buffer.getWritePointer(1)
        };
        int hostNOut = 0;
        srcChain_.processOut(coreOutL, coreOutR, coreN, hostOutPtrs, hostNOut);

        // If the output SRC under-produces (off-by-one drift), pad with
        // the last produced sample held; if it over-produces, the extra
        // samples beyond the host buffer simply weren't written. The
        // host's PDC pulls everything back into alignment.
        // Note: hostOutPtrs[1] is always non-null now (either stereo ch1
        // or monoRScratch), so R padding is unconditional.
        if (hostNOut < n)
        {
            float* outL = hostOutPtrs[0];
            float* outR = hostOutPtrs[1];
            const float lastL = (hostNOut > 0) ? outL[hostNOut - 1] : 0.0f;
            const float lastR = (hostNOut > 0) ? outR[hostNOut - 1] : 0.0f;
            for (int i = hostNOut; i < n; ++i)
            {
                outL[i] = lastL;
                outR[i] = lastR;
            }
        }

        // Phase 25 / PLUG-34: mono output summing. After processOut (and
        // padding), hostOutPtrs[0] holds L and monoRScratch holds R.
        // Sum to mono via standard (L+R)*0.5 into the single output channel.
        if (monoOutput)
        {
            float* out = hostOutPtrs[0];
            for (int i = 0; i < n; ++i)
                out[i] = (out[i] + monoRScratch[i]) * 0.5f;
        }

        // Side-channel limiter moved to core-rate split-bus loop above.
        // Reverb is limited at 44.1kHz before SRC; voice stereo passes clean.
    }
}

// --- File preset save/load (Phase 14, PRE-08/PRE-09) ---

juce::String SPU94AudioProcessor::savePresetToString(
    const juce::String& name, const juce::String& description)
{
    if (!engines[0]) return {};

    // CR-02: do NOT write to engines[0] here. processBlock pushes all
    // param/atomic values every block; the engine already has the current
    // state. Writing from the message thread races with the audio thread.

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
    pendingTargetSlot.store(-1, std::memory_order_relaxed);
    filePresetReady.store(true, std::memory_order_release);
    return true;
}

bool SPU94AudioProcessor::loadUserSlotFromString(int target_slot,
                                                 const juce::String& presetText)
{
    if (target_slot < 0 || target_slot >= SPU94_INTERP_USER_SLOT_COUNT)
        return false;
    if (presetText.isEmpty() || !engines[0]) return false;
    auto raw = presetText.toRawUTF8();
    auto len = presetText.getNumBytesAsUTF8();
    if (len == 0 || len >= sizeof(pendingPresetBuf)) return false;
    std::memcpy(pendingPresetBuf.data(), raw, len);
    pendingPresetBuf[len] = '\0';
    pendingPresetLen.store(len, std::memory_order_relaxed);
    pendingTargetSlot.store(target_slot, std::memory_order_relaxed);
    filePresetReady.store(true, std::memory_order_release);
    return true;
}

juce::String SPU94AudioProcessor::exportUserSlotToString(
    int slot, const juce::String& name, const juce::String& description)
{
    if (!engines[0]) return {};
    char buf[SPU94_PRESET_BUF_SIZE];
    int written = spu94_export_user_slot(
        engines[0], slot,
        name.isNotEmpty() ? name.toRawUTF8() : nullptr,
        description.isNotEmpty() ? description.toRawUTF8() : nullptr,
        buf, sizeof(buf));
    if (written < 0) return {};
    return juce::String(buf, static_cast<size_t>(written));
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

// --- Voice engine methods (Phase 31: standalone testbed) ---

void SPU94AudioProcessor::loadVoiceSample(const juce::File& file)
{
    auto result = WavLoader::load(file);
    if (!result.has_value()) return;

    const int targetRate = encodeRate.load(std::memory_order_relaxed);
    const int16_t* pcmData = result->L.data();
    uint64_t pcmFrames = result->numFrames;
    std::vector<int16_t> resampled;

    if (targetRate < 44100) {
        double ratio = static_cast<double>(targetRate) / 44100.0;
        auto outFrames = static_cast<uint64_t>(std::ceil(pcmFrames * ratio));
        resampled.resize(outFrames);
        SRC_DATA src{};
        std::vector<float> fIn(pcmFrames), fOut(outFrames);
        for (uint64_t i = 0; i < pcmFrames; i++)
            fIn[i] = result->L[i] / 32768.0f;
        src.data_in = fIn.data();
        src.input_frames = static_cast<long>(pcmFrames);
        src.data_out = fOut.data();
        src.output_frames = static_cast<long>(outFrames);
        src.src_ratio = ratio;
        src_simple(&src, SRC_SINC_BEST_QUALITY, 1);
        outFrames = static_cast<uint64_t>(src.output_frames_gen);
        resampled.resize(outFrames);
        for (uint64_t i = 0; i < outFrames; i++) {
            float s = fOut[i] * 32768.0f;
            if (s > 32767.0f) s = 32767.0f;
            if (s < -32768.0f) s = -32768.0f;
            resampled[i] = static_cast<int16_t>(s);
        }
        pcmData = resampled.data();
        pcmFrames = outFrames;
    }

    auto* mixer = spu94_get_voice_mixer();

    int32_t bytes = spu94_sample_encode_to_ram(
        pcmData,
        static_cast<uint32_t>(pcmFrames),
        mixer->voice_ram,
        0,                        // ram_offset
        SPU94_SPU_RAM_BYTES,      // ram_size
        1                         // loop_enable
    );

    if (bytes > 0) {
        pendingMixerEnable.store(true, std::memory_order_release);
        voiceSampleName = file.getFileName();
        voiceSampleBytes = static_cast<uint32_t>(bytes);
        ramUsed.store(voiceSampleBytes, std::memory_order_relaxed);

        uint32_t numBlocks = voiceSampleBytes / SPU94_ADPCM_BLOCK_BYTES;
        adpcmStateCache.resize(numBlocks);
        spu94_adpcm_state st = {0, 0};
        int16_t tmp[SPU94_ADPCM_BLOCK_SAMPLES];
        for (uint32_t b = 0; b < numBlocks; b++) {
            adpcmStateCache[b] = st;
            spu94_adpcm_decode_block(&st, mixer->voice_ram + b * SPU94_ADPCM_BLOCK_BYTES, tmp);
        }

        waveformData = result->L;
        waveformFrames = result->numFrames;

        voiceSampleLoaded.store(true, std::memory_order_release);
    }
}

void SPU94AudioProcessor::triggerVoice(uint16_t pitch)
{
    pendingGuiTriggerPitch.store(pitch, std::memory_order_release);
}

void SPU94AudioProcessor::stopVoice()
{
    pendingGuiStop.store(true, std::memory_order_release);
}

namespace {

struct AdsrRate { uint8_t shift; uint8_t step; float seconds; };

// All tables: measured from real engine, perceptually deduplicated (>5.7% spacing).
// All tables: measured from real engine, perceptually deduplicated (>5.7% spacing).

// Attack linear: 53 entries
static constexpr AdsrRate kAttackLinTable[] = {
    { 4,2, 0.0012f}, { 4,3, 0.0015f}, { 5,0, 0.0017f}, { 5,1, 0.0019f},
    { 5,2, 0.0023f}, { 5,3, 0.0029f}, { 6,0, 0.0033f}, { 6,1, 0.0039f},
    { 6,2, 0.0046f}, { 6,3, 0.0058f}, { 7,0, 0.0066f}, { 7,1, 0.0078f},
    { 7,2, 0.0093f}, { 7,3, 0.0116f}, { 8,0, 0.0133f}, { 8,1, 0.0155f},
    { 8,2, 0.0186f}, { 8,3, 0.0232f}, { 9,0, 0.0266f}, { 9,1, 0.0310f},
    { 9,2, 0.0372f}, { 9,3, 0.0464f}, {10,0, 0.0531f}, {10,1, 0.0619f},
    {10,2, 0.0743f}, {10,3, 0.0929f}, {11,0, 0.1061f}, {11,1, 0.1239f},
    {11,2, 0.1486f}, {11,3, 0.1858f}, {12,0, 0.2123f}, {12,1, 0.2477f},
    {12,2, 0.2972f}, {12,3, 0.3715f}, {13,0, 0.4246f}, {13,1, 0.4954f},
    {13,2, 0.5945f}, {13,3, 0.7430f}, {14,0, 0.8492f}, {14,1, 0.9908f},
    {14,2, 1.1889f}, {14,3, 1.4861f}, {15,0, 1.6983f}, {15,1, 1.9817f},
    {15,2, 2.3779f}, {15,3, 2.9722f}, {16,0, 3.3966f}, {16,1, 3.9634f},
    {16,2, 4.7557f}, {16,3, 5.9443f}, {17,0, 6.7933f}, {17,1, 7.9267f},
    {17,2, 9.5115f},
};
static constexpr int kAttackLinTableSize = sizeof(kAttackLinTable) / sizeof(kAttackLinTable[0]);

// Attack exponential: 52 entries
static constexpr AdsrRate kAttackExpTable[] = {
    { 3,3, 0.0012f}, { 4,0, 0.0015f}, { 4,1, 0.0017f}, { 4,2, 0.0021f},
    { 4,3, 0.0025f}, { 5,0, 0.0030f}, { 5,1, 0.0034f}, { 5,2, 0.0041f},
    { 5,3, 0.0050f}, { 6,0, 0.0059f}, { 6,1, 0.0067f}, { 6,2, 0.0081f},
    { 6,3, 0.0101f}, { 7,0, 0.0116f}, { 7,1, 0.0135f}, { 7,2, 0.0162f},
    { 7,3, 0.0202f}, { 8,0, 0.0233f}, { 8,1, 0.0271f}, { 8,2, 0.0325f},
    { 8,3, 0.0406f}, { 9,0, 0.0465f}, { 9,1, 0.0542f}, { 9,2, 0.0651f},
    { 9,3, 0.0812f}, {10,0, 0.0929f}, {10,1, 0.1083f}, {10,2, 0.1300f},
    {10,3, 0.1625f}, {11,0, 0.1857f}, {11,1, 0.2167f}, {11,2, 0.2600f},
    {11,3, 0.3250f}, {12,0, 0.3715f}, {12,1, 0.4334f}, {12,2, 0.5201f},
    {12,3, 0.6500f}, {13,0, 0.7429f}, {13,1, 0.8668f}, {13,2, 1.0402f},
    {13,3, 1.3000f}, {14,0, 1.4859f}, {14,1, 1.7337f}, {14,2, 2.0804f},
    {14,3, 2.6001f}, {15,0, 2.9718f}, {15,1, 3.4674f}, {15,2, 4.1607f},
    {15,3, 5.2002f}, {16,0, 5.9436f}, {16,1, 6.9348f}, {16,2, 8.3215f},
};
static constexpr int kAttackExpTableSize = sizeof(kAttackExpTable) / sizeof(kAttackExpTable[0]);

// Decay (always exponential): 48 entries
static constexpr AdsrRate kDecayTable[] = {
    { 6,2, 0.0109f}, { 6,3, 0.0131f}, { 7,0, 0.0164f},
    { 7,1, 0.0188f}, { 7,2, 0.0221f}, { 7,3, 0.0266f}, { 8,0, 0.0336f},
    { 8,1, 0.0388f}, { 8,2, 0.0454f}, { 8,3, 0.0558f}, { 9,0, 0.0702f},
    { 9,1, 0.0833f}, { 9,2, 0.1001f}, { 9,3, 0.1225f}, {10,0, 0.1541f},
    {10,1, 0.1754f}, {10,2, 0.2024f}, {10,3, 0.2380f}, {11,0, 0.2872f},
    {11,1, 0.3197f}, {11,2, 0.3601f}, {11,3, 0.4117f}, {12,0, 0.5745f},
    {12,1, 0.6395f}, {12,2, 0.7203f}, {12,3, 0.8235f}, {13,0, 1.1489f},
    {13,1, 1.2789f}, {13,2, 1.4405f}, {13,3, 1.6470f}, {14,0, 2.2979f},
    {14,1, 2.5578f}, {14,2, 2.8811f}, {14,3, 3.2940f}, {15,0, 4.5957f},
    {15,1, 5.1156f}, {15,2, 5.7622f}, {15,3, 6.5879f}, {16,0, 9.1915f},
};
static constexpr int kDecayTableSize = sizeof(kDecayTable) / sizeof(kDecayTable[0]);

// Release linear: 49 entries
static constexpr AdsrRate kReleaseLinTable[] = {
    { 5,3, 0.0023f}, { 6,0, 0.0029f}, { 6,1, 0.0033f}, { 6,2, 0.0039f},
    { 6,3, 0.0046f}, { 7,0, 0.0058f}, { 7,1, 0.0066f}, { 7,2, 0.0078f},
    { 7,3, 0.0093f}, { 8,0, 0.0116f}, { 8,1, 0.0133f}, { 8,2, 0.0155f},
    { 8,3, 0.0186f}, { 9,0, 0.0232f}, { 9,1, 0.0266f}, { 9,2, 0.0310f},
    { 9,3, 0.0372f}, {10,0, 0.0464f}, {10,1, 0.0531f}, {10,2, 0.0619f},
    {10,3, 0.0743f}, {11,0, 0.0929f}, {11,1, 0.1061f}, {11,2, 0.1239f},
    {11,3, 0.1486f}, {12,0, 0.1858f}, {12,1, 0.2123f}, {12,2, 0.2477f},
    {12,3, 0.2972f}, {13,0, 0.3715f}, {13,1, 0.4246f}, {13,2, 0.4954f},
    {13,3, 0.5945f}, {14,0, 0.7430f}, {14,1, 0.8492f}, {14,2, 0.9908f},
    {14,3, 1.1889f}, {15,0, 1.4861f}, {15,1, 1.6983f}, {15,2, 1.9817f},
    {15,3, 2.3779f}, {16,0, 2.9722f}, {16,1, 3.3966f}, {16,2, 3.9634f},
    {16,3, 4.7557f}, {17,0, 5.9443f}, {17,1, 6.7933f}, {17,2, 7.9267f},
    {17,3, 9.5115f},
};
static constexpr int kReleaseLinTableSize = sizeof(kReleaseLinTable) / sizeof(kReleaseLinTable[0]);

// Release exponential: 54 entries
static constexpr AdsrRate kReleaseExpTable[] = {
    { 2,2, 0.0022f}, { 2,3, 0.0026f}, { 3,0, 0.0032f}, { 3,1, 0.0037f},
    { 3,2, 0.0042f}, { 3,3, 0.0050f}, { 4,0, 0.0061f}, { 4,1, 0.0069f},
    { 4,2, 0.0079f}, { 4,3, 0.0093f}, { 5,0, 0.0112f}, { 5,1, 0.0127f},
    { 5,2, 0.0145f}, { 5,3, 0.0170f}, { 6,0, 0.0206f}, { 6,1, 0.0231f},
    { 6,2, 0.0264f}, { 6,3, 0.0308f}, { 7,0, 0.0372f}, { 7,1, 0.0417f},
    { 7,2, 0.0474f}, { 7,3, 0.0552f}, { 8,0, 0.0664f}, { 8,1, 0.0742f},
    { 8,2, 0.0841f}, { 8,3, 0.0976f}, { 9,0, 0.1167f}, { 9,1, 0.1298f},
    { 9,2, 0.1465f}, { 9,3, 0.1689f}, {10,0, 0.2005f}, {10,1, 0.2218f},
    {10,2, 0.2489f}, {10,3, 0.2845f}, {11,0, 0.3337f}, {11,1, 0.3662f},
    {11,2, 0.4066f}, {11,3, 0.4582f}, {12,0, 0.6673f}, {12,1, 0.7323f},
    {12,2, 0.8132f}, {12,3, 0.9164f}, {13,0, 1.3347f}, {13,1, 1.4647f},
    {13,2, 1.6263f}, {13,3, 1.8327f}, {14,0, 2.6694f}, {14,1, 2.9293f},
    {14,2, 3.2526f}, {14,3, 3.6655f}, {15,0, 5.3388f}, {15,1, 5.8587f},
    {15,2, 6.5052f}, {15,3, 7.3310f},
};
static constexpr int kReleaseExpTableSize = sizeof(kReleaseExpTable) / sizeof(kReleaseExpTable[0]);

// Knob curve: 0%=1ms, 50%=2s, 100%=10s
// time = 0.001 * 10000^(knob^0.277)
AdsrRate knobToRate(float knob, const AdsrRate* table, int tableSize) {
    knob = std::clamp(knob, 0.0f, 1.0f);
    float targetSec;
    if (knob < 0.001f) {
        targetSec = table[0].seconds;
    } else if (knob <= 0.25f) {
        float t = knob / 0.25f;
        targetSec = std::exp(std::log(0.001f) + t * (std::log(0.335f) - std::log(0.001f)));
    } else {
        targetSec = 0.001f * std::pow(10000.0f, std::pow(knob, 0.277f));
    }

    int best = 0;
    float bestDist = 1e30f;
    for (int i = 0; i < tableSize; i++) {
        float dist = std::abs(std::log(table[i].seconds) - std::log(targetSec));
        if (dist < bestDist) { bestDist = dist; best = i; }
    }
    return table[best];
}

} // anon namespace

spu94_adsr_state_t SPU94AudioProcessor::buildAdsrConfig() const
{
    spu94_adsr_state_t cfg;
    spu94_adsr_init(&cfg);
    cfg.enabled = 1;

    float atk = adsrAttack.load(std::memory_order_relaxed);
    bool atkExp = adsrAttackExp.load(std::memory_order_relaxed);
    auto ar = knobToRate(atk, atkExp ? kAttackExpTable : kAttackLinTable,
                              atkExp ? kAttackExpTableSize : kAttackLinTableSize);
    cfg.attack_shift = ar.shift;
    cfg.attack_step  = ar.step;
    cfg.attack_exp   = atkExp ? 1 : 0;

    float dec = adsrDecay.load(std::memory_order_relaxed);
    auto dr = knobToRate(dec, kDecayTable, kDecayTableSize);
    cfg.decay_shift = dr.shift;
    cfg.decay_step  = dr.step;

    float sl = adsrSustainLvl.load(std::memory_order_relaxed);
    cfg.sustain_level = static_cast<uint8_t>(sl * 15.0f + 0.5f);

    float sr = adsrSustainRate.load(std::memory_order_relaxed);
    cfg.sustain_dir  = sr < 0.0f ? 1 : 0;
    float mag = sr < 0.0f ? -sr : sr;
    if (mag < 0.01f) {
        cfg.sustain_shift = 31;
        cfg.sustain_step  = 3;
    } else {
        auto srr = knobToRate(1.0f - mag, kAttackLinTable, kAttackLinTableSize);
        cfg.sustain_shift = srr.shift;
        cfg.sustain_step  = srr.step;
    }
    cfg.sustain_exp   = adsrSustainExp.load(std::memory_order_relaxed) ? 1 : 0;

    float rel = adsrRelease.load(std::memory_order_relaxed);
    bool relExp = adsrReleaseExp.load(std::memory_order_relaxed);
    auto rr = knobToRate(rel, relExp ? kReleaseExpTable : kReleaseLinTable,
                              relExp ? kReleaseExpTableSize : kReleaseLinTableSize);
    cfg.release_shift = rr.shift;
    cfg.release_step  = rr.step;
    cfg.release_exp   = relExp ? 1 : 0;

    return cfg;
}

float SPU94AudioProcessor::getAdsrAttackSeconds() const {
    float knob = adsrAttack.load(std::memory_order_relaxed);
    bool exp = adsrAttackExp.load(std::memory_order_relaxed);
    return knobToRate(knob, exp ? kAttackExpTable : kAttackLinTable,
                            exp ? kAttackExpTableSize : kAttackLinTableSize).seconds;
}

float SPU94AudioProcessor::getAdsrDecaySeconds() const {
    float knob = adsrDecay.load(std::memory_order_relaxed);
    return knobToRate(knob, kDecayTable, kDecayTableSize).seconds;
}

float SPU94AudioProcessor::getAdsrReleaseSeconds() const {
    float knob = adsrRelease.load(std::memory_order_relaxed);
    bool exp = adsrReleaseExp.load(std::memory_order_relaxed);
    return knobToRate(knob, exp ? kReleaseExpTable : kReleaseLinTable,
                            exp ? kReleaseExpTableSize : kReleaseLinTableSize).seconds;
}

uint32_t SPU94AudioProcessor::posToBlockAddr(double pos) const
{
    if (voiceSampleBytes == 0) return 0;
    uint32_t bytePos = static_cast<uint32_t>(pos * voiceSampleBytes);
    return (bytePos / SPU94_ADPCM_BLOCK_BYTES) * SPU94_ADPCM_BLOCK_BYTES;
}

double SPU94AudioProcessor::getVoicePlaybackPos() const
{
    if (voiceSampleBytes == 0) return 0.0;
    auto* mx = spu94_get_voice_mixer();
    auto& v = mx->voices[0];
    if (!v.active) return 0.0;
    return static_cast<double>(v.current_addr) / static_cast<double>(voiceSampleBytes);
}

uint16_t SPU94AudioProcessor::midiNoteToPitch(int note, int baseNote)
{
    double ratio = std::pow(2.0, (note - baseNote) / 12.0);
    int pitch = static_cast<int>(0x1000 * ratio + 0.5);
    if (pitch < 1) pitch = 1;
    if (pitch > 0x3FFF) pitch = 0x3FFF;
    return static_cast<uint16_t>(pitch);
}

int SPU94AudioProcessor::allocateVoice(int note)
{
    int voice = nextVoice;
    if (noteForVoice[voice] >= 0)
        spu94_voice_mixer_key_off(spu94_get_voice_mixer(), voice);
    noteForVoice[voice] = static_cast<int8_t>(note);
    nextVoice = (nextVoice + 1) % 24;
    return voice;
}

int SPU94AudioProcessor::findVoiceForNote(int note)
{
    for (int i = 0; i < 24; ++i)
    {
        if (noteForVoice[i] == static_cast<int8_t>(note))
        {
            noteForVoice[i] = -1;
            return i;
        }
    }
    return -1;
}

bool SPU94AudioProcessor::isUserSlotFilled(int slot) const
{
    if (!engines[0]) return false;
    return spu94_interp_user_slot_is_filled(engines[0], slot) != 0;
}

void SPU94AudioProcessor::saveUserSlot(int slot)
{
    if (!engines[0]) return;
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return;

    // Force-flush any pending m/d-prefix writes BEFORE snapshotting -- those
    // are TICK_LATCHED and would otherwise be missed by snapshot_registers
    // (which reads only active reg_values[]). Without this, a freshly-edited
    // address-register value can be in pending but not yet committed by
    // spu94_tick when SAVE fires.
    spu94_apply_pending_writes(engines[0]);

    int16_t snapshot[SPU94_REG__COUNT] = {};
    spu94_snapshot_registers(engines[0], snapshot);

    // CRITICAL: install the slot on BOTH engines. engines[1] is the scratch
    // engine the glide path uses for spu94_interp_set_morph -- if its
    // user_slots[] aren't kept in sync, the glide path treats the slot as
    // empty and falls through to transparent Sony interp, ignoring the user
    // save. The Advanced-entry path (which uses engines[0] directly) would
    // then disagree with what gliding to the slot produces.
    spu94_interp_set_user_slot(engines[0], slot, snapshot);
    if (engines[1])
        spu94_interp_set_user_slot(engines[1], slot, snapshot);
}

void SPU94AudioProcessor::clearUserSlot(int slot)
{
    if (!engines[0]) return;
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return;
    // Mirror the clear to engines[1] for the same reason as saveUserSlot.
    spu94_interp_clear_user_slot(engines[0], slot);
    if (engines[1])
        spu94_interp_clear_user_slot(engines[1], slot);
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

void SPU94AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (!engines[0]) return;

    // CR-02: do NOT write to engines[0] here. processBlock already pushes
    // all AudioParameterFloat + atomic values to the engine every block.
    // Writing from the message thread while the audio thread reads/writes
    // the same engine is a data race. The engine's current state (from the
    // most recent processBlock) is the correct snapshot to serialize.

    StateSerializer::save(
        engines[0],
        paramInputGain->get(),
        paramMorphPosition->get(),
        paramMorphSpeed->get(),
        paramMorphGrit->get(),
        0.0f, 0.0f,
        destData);
}

void SPU94AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Parse the binary container (message thread -- no audio-thread allocation).
    auto result = StateSerializer::load(data, sizeInBytes);

    // D-06: on any validation failure, leave engine at defaults.
    if (!result.ok)
        return;

    // T-24-02: textLen already validated against SPU94_PRESET_BUF_SIZE
    // inside StateSerializer::load -- safe to memcpy into the fixed-size
    // pendingPresetBuf without overflow.
    std::memcpy(pendingPresetBuf.data(), result.textBody, result.textLen);
    pendingPresetBuf[result.textLen] = '\0';
    pendingPresetLen.store(result.textLen, std::memory_order_relaxed);
    pendingTargetSlot.store(-1, std::memory_order_relaxed);  // full preset load
    filePresetReady.store(true, std::memory_order_release);

    // WR-03: write restored values directly to AudioParameterFloat (single
    // source of truth). The old atomics are no longer read for these params;
    // processBlock reads from the AudioParameterFloat instances.

    // Phase 24: update the AudioParameterFloat instances so the host
    // sees the restored values in its automation lanes. This runs on the
    // message thread, so setValueNotifyingHost is the correct call.
    paramInputGain->setValueNotifyingHost(
        paramInputGain->getNormalisableRange().convertTo0to1(result.inputGain));
    paramMorphPosition->setValueNotifyingHost(result.morphPosition);  // 0..1 range, normalized==real
    paramMorphSpeed->setValueNotifyingHost(result.morphSpeed);
    paramMorphGrit->setValueNotifyingHost(result.morphGrit >= 0.5f ? 1.0f : 0.0f);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SPU94AudioProcessor();
}
