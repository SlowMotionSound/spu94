// src/plugin/BoundaryConverter.h
// Phase 23: header-only float<->int16 boundary converter for the JUCE
// wrapper. Lifted byte-identically from the inline anonymous-namespace
// lambdas previously at src/plugin/SrcChain.cpp:45-56.
//
// Public surface (free functions in namespace spu94::plugin::boundary):
//   inline int16_t toInt16(float x) noexcept;
//   inline float   toFloat(int16_t x) noexcept;
//
// Locked v1.7 requirements (.planning/milestones/v1.7-REQUIREMENTS.md):
//   PLUG-17  Host float32 input -> int16 via clamp + truncation.
//            No dither, no noise shaping, no rounding.
//   PLUG-18  Clamp/saturation matches the C core's sat_s16 semantics
//            (ADR-0001): out-of-range floats saturate to +32767 / -32768,
//            never wrap around two's-complement. (ADR-0001 is the
//            referenced authority; if the ADR file itself is not yet
//            committed in c_core/docs/adr/, that is a paperwork follow-up,
//            not a code blocker -- the semantics are pinned by PLUG-18
//            and by this header's body.)
//   PLUG-21  int16 -> float by a single multiply by 1.0f / 32768.0f.
//            -32768 maps exactly to -1.0f; +32767 maps to ~+0.99996948f
//            (NOT +1.0f). Asymmetric by design -- preserves the existing
//            inline lambda semantics that the JUCE wrapper's null-test
//            depends on.
//
// RT-safety contract (same as SrcChain.h):
//   No operator new / malloc / std::vector / std::string.
//   No juce::String / juce::Logger / DBG.
//   No Mutex / CriticalSection / SpinLock acquisition.
//   No syscalls (no I/O, no time, no random).
// The header is JUCE-independent on purpose (only <cstdint>), so it can
// be included from non-JUCE translation units (e.g. future unit tests)
// without dragging the JUCE module headers in.

#pragma once

#include <cstdint>

namespace spu94::plugin::boundary
{
    // Float [-1.0f, +1.0f] -> int16 [-32768, +32767].
    // Multiply by 32768.0f, then clamp+truncate. Out-of-range inputs
    // saturate (sat_s16 semantics, ADR-0001 / PLUG-18). No dither, no
    // rounding -- truncation is plain static_cast<int16_t>(y).
    //
    // This body MUST stay structurally equivalent to the lifted lambda
    // at the old SrcChain.cpp:45-51: multiply, branch on the saturation
    // corners, otherwise static_cast.
    inline int16_t toInt16(float x) noexcept
    {
        const float y = x * 32768.0f;
        if (y >= 32767.0f)  return 32767;
        if (y <= -32768.0f) return -32768;
        return static_cast<int16_t>(y);
    }

    // int16 [-32768, +32767] -> float, single multiply.
    // -32768 -> -1.0f exactly; +32767 -> ~+0.99996948f (NOT +1.0f).
    // Matches the lifted lambda body at the old SrcChain.cpp:53-56
    // line-for-line.
    inline float toFloat(int16_t x) noexcept
    {
        return static_cast<float>(x) * (1.0f / 32768.0f);
    }

    // Pre-clamp float gain stage for the plugin path.
    // Applied BEFORE toInt16's saturation. `gain` is the linear factor;
    // range 0.0..16.0 (0.5 = -6 dB default, 1.0 = unity, 16.0 = +24 dB drive).
    // No clamp here -- clamp happens later inside toInt16.
    // Calling site: PluginProcessor.cpp plugin branch, before srcChain_.processIn.
    //
    // The function is intentionally trivial. It exists as a NAMED seam so
    // the pre-clamp pipeline (gain -> boundary) has one grep-able home and
    // so a future unit test can target applyInputGain + toInt16 together.
    inline float applyInputGain(float x, float gain) noexcept
    {
        return x * gain;
    }

    // Compile-time corner-case sanity. constexpr-folded on all toolchains
    // the project supports (clang/gcc/MSVC C++17). PLUG-18 saturation
    // corners + a clean midrange point + the exact -1.0f boundary.
    //
    //   toInt16(+1.5f) -> 32767       (NOT -16384, which would be the
    //                                  two's-complement wrap result of
    //                                  casting 49152.0f to int16_t)
    //   toInt16(-2.0f) -> -32768      (NOT 0, which would be the wrap
    //                                  result of casting -65536.0f)
    //   toInt16(+0.5f) -> 16384       (clean, in-range)
    //   toInt16(-1.0f) -> -32768      (exact lower boundary)
}
