#pragma once

/* StateSerializer.h -- Binary state container for DAW project persistence
 *
 * Phase 24 Plan 01 (PLUG-22..27): wraps the existing .spu94 text body
 * from spu94_preset_save in a binary envelope with a 6-float appendix
 * for wrapper-side atomics (inputGain, morphPosition, morphSpeed)
 * that the .spu94 text format does not carry.
 *
 * Container layout (all integers little-endian):
 *   Offset  Size  Field
 *   0       4     Magic bytes: 'S','P','U','9'
 *   4       1     Version byte: 0x01
 *   5       4     Body length (uint32_t LE) = textLen + 24
 *   9       N     .spu94 text body (from spu94_preset_save)
 *   9+N     24    Float appendix: 6 x float32 LE (IEEE 754)
 *                 [inputGain, morphPosition, morphSpeed,
 *                  reserved0, reserved1, reserved2]
 *   Total: 9 + N + 24 bytes
 *
 * Locale safety (PLUG-25): the .spu94 text format uses snprintf %04X
 * for all values (hex integers, no float parsing). The float appendix
 * uses raw IEEE 754 binary bytes via memcpy. Zero locale-sensitive
 * string parsing anywhere in the chain.
 */

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

extern "C" {
#include <spu94/spu94.h>
}

namespace StateSerializer {

static constexpr uint8_t kMagic[4] = {'S', 'P', 'U', '9'};
static constexpr uint8_t kVersion  = 1;
static constexpr size_t  kHeaderSize = 9;  // 4 magic + 1 version + 4 bodyLen
static constexpr size_t  kFloatAppendixCount = 6;
static constexpr size_t  kFloatAppendixSize  = kFloatAppendixCount * sizeof(float);

/* Save engine + wrapper state into a binary container.
 *
 * engine:      the spu94_state to serialize (35 regs + mixer + DAC + user slots)
 * inputGain:   wrapper-side atomic (not in .spu94 text)
 * morphPos:    wrapper-side atomic (not in .spu94 text)
 * morphSpd:    wrapper-side atomic (not in .spu94 text)
 * pad0..pad2:  reserved (written as 0.0f)
 * dest:        output MemoryBlock (reset + filled)
 *
 * Returns true on success, false if spu94_preset_save fails.
 */
inline bool save(const spu94_state* engine,
                 float inputGain, float morphPos, float morphSpd,
                 float pad0, float pad1, float pad2,
                 juce::MemoryBlock& dest)
{
    char textBuf[SPU94_PRESET_BUF_SIZE];
    int textLen = spu94_preset_save(engine, "DAW State", "",
                                    textBuf, sizeof(textBuf));
    if (textLen <= 0)
        return false;

    uint32_t bodyLen = static_cast<uint32_t>(textLen)
                     + static_cast<uint32_t>(kFloatAppendixSize);

    dest.reset();

    // Header: magic + version + bodyLen
    dest.append(kMagic, 4);
    dest.append(&kVersion, 1);
    dest.append(&bodyLen, 4);  // LE on all target platforms (x86, ARM64)

    // Text body
    dest.append(textBuf, static_cast<size_t>(textLen));

    // Float appendix (IEEE 754 binary -- locale-proof by construction)
    float appendix[kFloatAppendixCount] = {
        inputGain, morphPos, morphSpd, pad0, pad1, pad2
    };
    dest.append(appendix, kFloatAppendixSize);

    return true;
}

/* Result of loading a binary state container. */
struct LoadResult {
    bool        ok            = false;
    const char* textBody      = nullptr;
    size_t      textLen       = 0;
    float       inputGain     = 0.5f;
    float       morphPosition = 0.625f;
    float       morphSpeed    = 0.5f;
};

/* Parse a binary state container and extract the .spu94 text body +
 * float appendix values.
 *
 * data:        raw bytes from the DAW project file
 * sizeInBytes: total size of the data blob
 *
 * On success, LoadResult.ok is true and textBody points into the
 * original data buffer (no copy). Caller must not free data while
 * using textBody.
 *
 * On failure (corrupt, truncated, future version per D-06), ok is
 * false and the float fields retain their defaults -- caller leaves
 * the engine at its current state.
 */
inline LoadResult load(const void* data, int sizeInBytes)
{
    LoadResult r;

    // Minimum size: header alone
    if (sizeInBytes < static_cast<int>(kHeaderSize))
        return r;

    auto* bytes = static_cast<const uint8_t*>(data);

    // Validate magic bytes (T-24-01: reject tampered chunks)
    if (std::memcmp(bytes, kMagic, 4) != 0)
        return r;

    // Validate version (D-06: reject future versions, fail-safe)
    if (bytes[4] > kVersion)
        return r;

    // Read body length (T-24-02: bounds-check against actual size)
    uint32_t bodyLen = 0;
    std::memcpy(&bodyLen, bytes + 5, 4);

    // Bounds check: header + body must fit within the provided data
    if (static_cast<size_t>(kHeaderSize) + static_cast<size_t>(bodyLen)
            > static_cast<size_t>(sizeInBytes))
        return r;

    // Body must be at least as large as the float appendix
    if (bodyLen < static_cast<uint32_t>(kFloatAppendixSize))
        return r;

    // Text length check: ensure it fits in pendingPresetBuf (T-24-02)
    r.textLen = static_cast<size_t>(bodyLen) - kFloatAppendixSize;
    if (r.textLen >= SPU94_PRESET_BUF_SIZE)
        return r;

    r.textBody = reinterpret_cast<const char*>(bytes + kHeaderSize);

    // Extract float appendix via memcpy (alignment-safe, no UB)
    float appendix[kFloatAppendixCount];
    std::memcpy(appendix, bytes + kHeaderSize + r.textLen,
                kFloatAppendixSize);

    // Sanitize: reject NaN/Inf, clamp to valid ranges (CR-01).
    auto sanitize = [](float v, float lo, float hi, float fallback) -> float {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    };
    r.inputGain     = sanitize(appendix[0], 0.0f, 16.0f,  0.5f);
    r.morphPosition = sanitize(appendix[1], 0.0f, 1.0f,   0.625f);
    r.morphSpeed    = sanitize(appendix[2], 0.0f, 1.0f,   0.5f);
    // appendix[3..5] are reserved padding (ignored on load)

    r.ok = true;
    return r;
}

} // namespace StateSerializer
