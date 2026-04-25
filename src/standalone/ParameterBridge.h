#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

extern "C" {
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
}

// The 18 registers exposed as sliders (D-01: 12 free + 6 sample-quantized).
// This array is the single source of truth -- slider construction, atomic
// shadow indexing, and per-block register writes all index into it.
// Do NOT hardcode register names elsewhere; use this + spu94_reg_name().
constexpr std::array<spu94_reg_t, 18> kSliderRegisters = {
    // 12 free-class (v-prefix gain registers -- smooth at any rate)
    SPU94_REG_vLOUT, SPU94_REG_vROUT, SPU94_REG_vLIN, SPU94_REG_vRIN,
    SPU94_REG_vIIR, SPU94_REG_vWALL,
    SPU94_REG_vCOMB1, SPU94_REG_vCOMB2, SPU94_REG_vCOMB3, SPU94_REG_vCOMB4,
    SPU94_REG_vAPF1, SPU94_REG_vAPF2,
    // 6 sample-quantized (d-prefix delay registers -- audible stepping is character)
    SPU94_REG_dLSAME, SPU94_REG_dRSAME,
    SPU94_REG_dLDIFF, SPU94_REG_dRDIFF,
    SPU94_REG_dAPF1,  SPU94_REG_dAPF2,
};
static_assert(kSliderRegisters.size() == 18);

// ---------------------------------------------------------------------------
// RegisterBridge -- lock-free atomic bridge between GUI and audio thread
// ---------------------------------------------------------------------------
// GUI thread writes slider values via setRegisterShadow().
// Audio thread reads them via pushPendingRegisterWrites() and applies to SPU.
// After a preset switch, syncShadowsFromSPU() re-reads all register values
// so the GUI can update slider positions to reflect the new preset.
class RegisterBridge
{
public:
    // GUI thread: store a slider value into the atomic shadow.
    void setRegisterShadow(size_t sliderIndex, int16_t value);

    // Audio thread: read shadows and apply changed values to the SPU state.
    // Only writes registers whose shadow value differs from lastApplied.
    void pushPendingRegisterWrites(spu94_state* spu);

    // Audio thread (after preset switch): pull all register values from SPU
    // back into the atomic shadows so sliders reflect the new preset.
    void syncShadowsFromSPU(const spu94_state* spu);

    // GUI thread: read the current shadow value for slider display.
    int16_t getShadowValue(size_t sliderIndex) const;

private:
    std::array<std::atomic<int16_t>, 18> shadows{};
    std::array<int16_t, 18> lastApplied{};  // audio-thread-only
};

// ---------------------------------------------------------------------------
// PresetCommandQueue -- SPSC command queue for preset switches
// ---------------------------------------------------------------------------
// GUI thread calls requestPreset(). Audio thread calls drain() at the top
// of processBlock. No mutex, no allocation -- pure atomic handoff.
class PresetCommandQueue
{
public:
    // GUI thread: request a preset switch. Overwrites any pending request.
    void requestPreset(spu94_preset_id_t id);

    // Audio thread: if a request is pending, call spu94_load_preset and
    // return true. Returns false if no request was pending.
    bool drain(spu94_state* spu);

    // GUI thread: poll this to detect when a preset switch was applied.
    // Increments every time drain() applies a preset.
    int getAppliedCount() const;

    // GUI thread: returns the last-applied preset id.
    int getAppliedId() const;

private:
    std::atomic<int> requested{0};
    std::atomic<bool> requestPending{false};
    std::atomic<int> appliedId{SPU94_PRESET_HALL};
    std::atomic<int> appliedCount{0};
};
