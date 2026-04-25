#include "ParameterBridge.h"

// ---------------------------------------------------------------------------
// RegisterBridge
// ---------------------------------------------------------------------------

void RegisterBridge::setRegisterShadow(size_t sliderIndex, int16_t value)
{
    if (sliderIndex < kSliderRegisters.size())
        shadows[sliderIndex].store(value, std::memory_order_release);
}

void RegisterBridge::pushPendingRegisterWrites(spu94_state* spu)
{
    for (size_t i = 0; i < kSliderRegisters.size(); ++i)
    {
        const int16_t v = shadows[i].load(std::memory_order_acquire);
        if (v != lastApplied[i])
        {
            const spu94_reg_t reg = kSliderRegisters[i];
            if (spu94_reg_type(reg) == SPU94_REG_TYPE_I16)
                spu94_set_reg_i16(spu, reg, v);
            else
                spu94_set_reg_u16(spu, reg, static_cast<uint16_t>(v));

            lastApplied[i] = v;
        }
    }
}

void RegisterBridge::syncShadowsFromSPU(const spu94_state* spu)
{
    for (size_t i = 0; i < kSliderRegisters.size(); ++i)
    {
        const spu94_reg_t reg = kSliderRegisters[i];
        int16_t v;
        if (spu94_reg_type(reg) == SPU94_REG_TYPE_I16)
            v = spu94_get_reg_i16(spu, reg);
        else
            v = static_cast<int16_t>(spu94_get_reg_u16(spu, reg));

        shadows[i].store(v, std::memory_order_release);
        lastApplied[i] = v;
    }
}

int16_t RegisterBridge::getShadowValue(size_t sliderIndex) const
{
    if (sliderIndex < kSliderRegisters.size())
        return shadows[sliderIndex].load(std::memory_order_acquire);
    return 0;
}

// ---------------------------------------------------------------------------
// PresetCommandQueue
// ---------------------------------------------------------------------------

void PresetCommandQueue::requestPreset(spu94_preset_id_t id)
{
    requested.store(static_cast<int>(id), std::memory_order_relaxed);
    requestPending.store(true, std::memory_order_release);
}

bool PresetCommandQueue::drain(spu94_state* spu)
{
    if (!requestPending.load(std::memory_order_acquire))
        return false;

    requestPending.store(false, std::memory_order_relaxed);
    const auto id = static_cast<spu94_preset_id_t>(
        requested.load(std::memory_order_relaxed));

    spu94_load_preset(spu, id);
    appliedId.store(static_cast<int>(id), std::memory_order_relaxed);
    appliedCount.fetch_add(1, std::memory_order_release);
    return true;
}

int PresetCommandQueue::getAppliedCount() const
{
    return appliedCount.load(std::memory_order_acquire);
}

int PresetCommandQueue::getAppliedId() const
{
    return appliedId.load(std::memory_order_acquire);
}
