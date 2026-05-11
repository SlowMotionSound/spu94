#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WavLoader.h"
#include <algorithm>
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
    spu94_set_patina_send(engines[0], 0x7FFF);
    spu94_set_dac_enabled(engines[0], 1);
    spu94_set_latency_comp(engines[0], 1);
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
        patinaLevel.store(static_cast<float>(spu94_get_patina_fader(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        reverbLevel.store(static_cast<float>(spu94_get_reverb_fader(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        drySend.store(static_cast<float>(spu94_get_dry_send(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        adpcmSend.store(static_cast<float>(spu94_get_patina_send(engines[0])) / 0x7FFF, std::memory_order_relaxed);
        latencyCompEnabled.store(spu94_get_latency_comp(engines[0]) != 0, std::memory_order_relaxed);
        dacEnabled.store(spu94_get_dac_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacFirEnabled.store(spu94_get_dac_fir_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacNoiseEnabled.store(spu94_get_dac_noise_enabled(engines[0]) != 0, std::memory_order_relaxed);
        dacTrueOversample.store(spu94_get_dac_true_oversample(engines[0]) != 0, std::memory_order_relaxed);
        morphGrit.store(spu94_get_morph_grit(engines[0]), std::memory_order_relaxed);
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

    // Push mixer/DAC state to the single audio engine. State management --
    // runs unconditionally so the GUI's view of engine state is always
    // current, regardless of WAV/playback status.
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
    spu94_set_morph_grit(engines[0],
        morphGrit.load(std::memory_order_relaxed));

    // Morph position. The Register Behavior knob picks one of two paths:
    //   knob ≈ 0  → direct write (snap): registers latch at next tick.
    //                Same TICK_LATCHED click character as original v1.5.
    //   knob > 0  → slew on engines[0] over (knob * max_delta) samples.
    //                knob=1.0 reproduces the full Phase 18 glide duration.
    constexpr float kSnapThreshold = 0.001f;
    const float behavior = morphSpeed.load(std::memory_order_relaxed);
    if (morphActive.load(std::memory_order_acquire))
    {
        float pos = morphPosition.load(std::memory_order_relaxed);
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
                const int32_t maxDelta =
                    spu94_is_slewing(engines[0])
                        ? std::max(1, (int)(behavior * 22050.0f))
                        : 1;
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

    // Audio I/O gate: skip the actual sample read + process + output mix
    // when no WAV is loaded or playback is paused. ALL state management
    // above has already run, so the engine is up-to-date and the GUI's
    // sliders/ticks always reflect current engine state -- even with no
    // audio playing.
    if (!wavSource.loaded.load(std::memory_order_acquire) ||
        !wavSource.playing.load(std::memory_order_relaxed))
    {
        buffer.clear();
        return;
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

    // Build the int16 input to the reverb directly from the WAV source.
    auto playPos = wavSource.playPos.load(std::memory_order_relaxed);
    for (int i = 0; i < samplesToProcess; ++i)
    {
        const auto idx = static_cast<size_t>((playPos + static_cast<uint64_t>(i)) % numFrames);
        tmpL_in[i] = wavSource.L[idx];
        tmpR_in[i] = wavSource.R[idx];
    }

    // Single reverb engine — Register Behavior is enacted via slew on the
    // morph register writes above; the per-sample audio path is unchanged.
    spu94_process(engines[0], tmpL_in, tmpR_in, tmpL_out, tmpR_out,
                  static_cast<uint32_t>(samplesToProcess));

    // Post-pass: side-channel limiter, emit
    auto* outL = buffer.getWritePointer(0);
    auto* outR = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < samplesToProcess; ++i)
    {
        const float wetL = tmpL_out[i] / 32768.0f;
        const float wetR = tmpR_out[i] / 32768.0f;

        // Side-channel soft limiter: bound L/R divergence without affecting
        // the mono content. Mid passes through unchanged; side gets a tanh
        // limiter that kicks in around `kSideKnee` and asymptotes well below
        // it at `kSideCeiling`, pulling hard-panned content noticeably toward
        // center while leaving the mono level untouched.
        constexpr float kSideKnee    = 0.125f;  // tanh starts compressing here
        constexpr float kSideCeiling = 0.06f;   // max side amplitude after limit
        const float mid = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR);
        const float sideLimited = std::tanh(side / kSideKnee) * kSideCeiling;

        outL[i] = mid + sideLimited;
        if (outR) outR[i] = mid - sideLimited;
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
    spu94_set_morph_grit(engines[0],
        morphGrit.load(std::memory_order_relaxed));

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
