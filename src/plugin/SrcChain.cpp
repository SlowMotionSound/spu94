// src/plugin/SrcChain.cpp
// Phase 22: bidirectional libsamplerate SRC wrapper. See SrcChain.h for
// the design contract. All real-time-side code below MUST be free of:
//   - operator new / malloc / std::vector resize / std::string
//   - juce::String / juce::Logger / DBG
//   - Mutex / CriticalSection / SpinLock acquisition
//   - syscalls (no I/O, no time, no random)
// Audio-thread allocations are caught by pluginval --strictness-level 7
// --validate-in-process at host SR 48 kHz (advisory CI job from Phase 21).

#include "SrcChain.h"
#include "BoundaryConverter.h"

#include <samplerate.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    // Centre-of-energy of an output impulse response. out[] holds the SRC
    // response to a Kronecker delta at index 0; we return the energy-
    // weighted centroid t = sum(i * out[i]^2) / sum(out[i]^2). This is
    // robust against the Sinc filter's symmetric tap layout where a single
    // argmax would be unreliable (the response is smeared over ~kSincTaps
    // samples, not a delta).
    double centreOfEnergy(const float* out, int n) noexcept
    {
        double num = 0.0;
        double den = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double e = static_cast<double>(out[i]) * static_cast<double>(out[i]);
            num += static_cast<double>(i) * e;
            den += e;
        }
        return (den > 0.0) ? (num / den) : 0.0;
    }
}

// ============================================================================
// pull callback
// ============================================================================
// libsamplerate's pull-callback model: src_callback_read calls this until
// the caller-requested frame count is satisfied or the callback returns 0.
// We hand back the entire remaining float buffer in one shot, then return 0
// on subsequent calls so libsamplerate stops asking for more.
long SrcChain::pullCallback(void* cbData, float** dataOut) noexcept
{
    auto* ctx = static_cast<PullCtx*>(cbData);
    if (ctx == nullptr || ctx->remaining <= 0)
    {
        *dataOut = nullptr;
        return 0;
    }
    *dataOut = const_cast<float*>(ctx->data);
    const long n = ctx->remaining;
    ctx->remaining = 0;   // one-shot per processIn/processOut call
    return n;
}

// ============================================================================
// prepare / release / reset
// ============================================================================
SrcChain::~SrcChain()
{
    release();
}

void SrcChain::prepare(double hostSampleRate, int maxHostBlockSize, int numChannels)
{
    // Idempotency: re-prepare with identical args = no-op.
    if (srcIn_[0] != nullptr
        && std::abs(hostSampleRate - hostSampleRate_) < 1.0e-9
        && maxHostBlockSize == maxHostBlockSize_
        && numChannels == numChannels_)
    {
        return;
    }

    release();

    hostSampleRate_   = hostSampleRate;
    maxHostBlockSize_ = maxHostBlockSize;
    numChannels_      = std::clamp(numChannels, 1, 2);
    isFastPath_       = (std::abs(hostSampleRate - 44100.0) < 1.0e-9);

    // Scratch sizing per src_decisions.scratch_buffer_sizing in 22-PLAN.md:
    //   core-side scratch: maxHostBlockSize + 32 (downsampling host->44.1
    //     produces <= maxHostBlockSize frames; upsampling 44.1->host on
    //     the OUTPUT side produces more, but the core scratch only ever
    //     holds the SRC_in output which is bounded by the host block size
    //     at hostSR>=44100 and by hostBlockSize*ratio for hostSR<44100;
    //     the latter is a no-op corner case)
    //   host-side scratch: maxHostBlockSize * 5 + 32 (covers up to 192 kHz
    //     hostSR upsampling: 192000/44100 ~= 4.36, ceil -> 5; +32 safety
    //     margin > Sinc-Medium polyphase tap count of ~121)
    coreScratchN_ = maxHostBlockSize_ + 32;
    hostScratchN_ = maxHostBlockSize_ * 5 + 32;

    for (int ch = 0; ch < 2; ++ch)
    {
        scratchInFloat_     [ch].allocate(hostScratchN_, true);
        scratchCoreFloatIn_ [ch].allocate(coreScratchN_, true);
        scratchCoreFloatOut_[ch].allocate(coreScratchN_, true);
        scratchOutFloat_    [ch].allocate(hostScratchN_, true);
    }

    if (! isFastPath_)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            int errIn = 0, errOut = 0;
            srcIn_ [ch] = src_callback_new(&SrcChain::pullCallback,
                                           SRC_SINC_MEDIUM_QUALITY,
                                           1, &errIn,  &pullIn_ [ch]);
            srcOut_[ch] = src_callback_new(&SrcChain::pullCallback,
                                           SRC_SINC_MEDIUM_QUALITY,
                                           1, &errOut, &pullOut_[ch]);
            jassert(srcIn_[ch]  != nullptr);
            jassert(srcOut_[ch] != nullptr);
        }

        measuredLatencyHostSamples_ = measureGroupDelayInHostSamples();

        // Clear impulse-transient state on every SRC_STATE before runtime.
        for (int ch = 0; ch < 2; ++ch)
        {
            if (srcIn_ [ch] != nullptr) src_reset(srcIn_ [ch]);
            if (srcOut_[ch] != nullptr) src_reset(srcOut_[ch]);
        }
    }
    else
    {
        measuredLatencyHostSamples_ = 0;
    }
}

void SrcChain::release()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        if (srcIn_ [ch] != nullptr) { src_delete(srcIn_ [ch]); srcIn_ [ch] = nullptr; }
        if (srcOut_[ch] != nullptr) { src_delete(srcOut_[ch]); srcOut_[ch] = nullptr; }
        scratchInFloat_     [ch].free();
        scratchCoreFloatIn_ [ch].free();
        scratchCoreFloatOut_[ch].free();
        scratchOutFloat_    [ch].free();
        pullIn_ [ch] = {};
        pullOut_[ch] = {};
    }
    hostSampleRate_              = 0.0;
    maxHostBlockSize_            = 0;
    numChannels_                 = 0;
    coreScratchN_                = 0;
    hostScratchN_                = 0;
    isFastPath_                  = false;
    measuredLatencyHostSamples_  = 0;
}

void SrcChain::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        if (srcIn_ [ch] != nullptr) src_reset(srcIn_ [ch]);
        if (srcOut_[ch] != nullptr) src_reset(srcOut_[ch]);
    }
}

// ============================================================================
// Group-delay measurement (called from prepare(), non-fast-path only)
// ============================================================================
int SrcChain::measureGroupDelayInHostSamples()
{
    // Heap-allocated measurement buffers — this runs inside prepare(), NOT
    // on the audio thread. kMeasureLen = 4096 generously covers the Sinc-
    // Medium ~121-tap response even at 192 kHz hostSR where the upsampler
    // smears the impulse over ~4.36x the input length.
    constexpr int kMeasureLen = 4096;

    juce::HeapBlock<float> impulseIn (kMeasureLen, true);
    juce::HeapBlock<float> outBuf    (kMeasureLen, true);
    impulseIn[0] = 1.0f;

    // ---- input SRC: ratio = 44100 / hostSR ----
    // Feed the impulse through srcIn_[0]. The SRC's output is at core rate
    // (44100); the time index in its output buffer corresponds to a host
    // sample location of  i * (hostSR / 44100).
    pullIn_[0].data      = impulseIn.getData();
    pullIn_[0].remaining = kMeasureLen;

    const double ratioIn  = 44100.0 / hostSampleRate_;
    long producedIn = 0;
    // Pump until we've gathered kMeasureLen output frames or the SRC
    // runs dry (it will because the pull-callback is one-shot).
    while (producedIn < kMeasureLen)
    {
        const long got = src_callback_read(srcIn_[0],
                                           ratioIn,
                                           kMeasureLen - producedIn,
                                           outBuf.getData() + producedIn);
        if (got <= 0) break;
        producedIn += got;
    }
    const double centroidInCore  = centreOfEnergy(outBuf.getData(), (int) producedIn);
    // Convert core-rate centroid to host-rate samples: a delay of k samples
    // at 44.1 kHz corresponds to k * (hostSR / 44100) samples at hostSR.
    const double centroidInHost  = centroidInCore * (hostSampleRate_ / 44100.0);

    // ---- output SRC: ratio = hostSR / 44100 ----
    std::memset(outBuf.getData(), 0, sizeof(float) * (size_t) kMeasureLen);
    pullOut_[0].data      = impulseIn.getData();
    pullOut_[0].remaining = kMeasureLen;

    const double ratioOut = hostSampleRate_ / 44100.0;
    long producedOut = 0;
    while (producedOut < kMeasureLen)
    {
        const long got = src_callback_read(srcOut_[0],
                                           ratioOut,
                                           kMeasureLen - producedOut,
                                           outBuf.getData() + producedOut);
        if (got <= 0) break;
        producedOut += got;
    }
    // Output of srcOut_ is already at host rate, so the centroid IS the
    // delay in host samples.
    const double centroidOutHost = centreOfEnergy(outBuf.getData(), (int) producedOut);

    return static_cast<int>(std::ceil(centroidInHost + centroidOutHost));
}

// ============================================================================
// RT-safe processIn / processOut
// ============================================================================
void SrcChain::processIn(const float* const* hostIn, int hostN,
                         int16_t* coreOutL, int16_t* coreOutR, int& coreNOut)
{
    jassert(hostIn != nullptr && hostIn[0] != nullptr && hostIn[1] != nullptr);
    jassert(coreOutL != nullptr && coreOutR != nullptr);
    jassert(hostN >= 0 && hostN <= hostScratchN_);

    if (isFastPath_)
    {
        // 44.1 kHz hostSR -> straight float->int16, no libsamplerate.
        const float* L = hostIn[0];
        const float* R = hostIn[1];
        for (int i = 0; i < hostN; ++i)
        {
            coreOutL[i] = spu94::plugin::boundary::toInt16(L[i]);
            coreOutR[i] = spu94::plugin::boundary::toInt16(R[i]);
        }
        coreNOut = hostN;
        return;
    }

    // Non-fast-path: ratio = 44100 / hostSR (<1 = downsample, >1 = upsample
    // for hostSR<44100). One pull per channel per block.
    const double ratio = 44100.0 / hostSampleRate_;

    // Copy host float input into the per-channel pull buffer so the
    // userdata pointer is stable for the duration of src_callback_read.
    // This is a plain memcpy — no allocation.
    std::memcpy(scratchInFloat_[0].getData(), hostIn[0], sizeof(float) * (size_t) hostN);
    std::memcpy(scratchInFloat_[1].getData(), hostIn[1], sizeof(float) * (size_t) hostN);

    pullIn_[0].data = scratchInFloat_[0].getData();
    pullIn_[0].remaining = hostN;
    pullIn_[1].data = scratchInFloat_[1].getData();
    pullIn_[1].remaining = hostN;

    const long nL = src_callback_read(srcIn_[0], ratio, coreScratchN_,
                                      scratchCoreFloatIn_[0].getData());
    const long nR = src_callback_read(srcIn_[1], ratio, coreScratchN_,
                                      scratchCoreFloatIn_[1].getData());

    const long n = std::min(nL, nR);
    jassert(n <= coreScratchN_);   // R5 mitigation
    srcCallbacksThisBlock_.fetch_add(1, std::memory_order_relaxed);

    const float* fL = scratchCoreFloatIn_[0].getData();
    const float* fR = scratchCoreFloatIn_[1].getData();
    for (long i = 0; i < n; ++i)
    {
        coreOutL[i] = spu94::plugin::boundary::toInt16(fL[i]);
        coreOutR[i] = spu94::plugin::boundary::toInt16(fR[i]);
    }
    coreNOut = static_cast<int>(n);
}

void SrcChain::processOut(const int16_t* coreInL, const int16_t* coreInR, int coreN,
                          float* const* hostOut, int& hostNOut)
{
    jassert(coreInL != nullptr && coreInR != nullptr);
    jassert(hostOut != nullptr && hostOut[0] != nullptr);
    jassert(coreN >= 0 && coreN <= coreScratchN_);

    if (isFastPath_)
    {
        // 44.1 kHz hostSR -> straight int16->float, no libsamplerate.
        float* L = hostOut[0];
        float* R = (hostOut[1] != nullptr) ? hostOut[1] : nullptr;
        for (int i = 0; i < coreN; ++i)
        {
            L[i] = spu94::plugin::boundary::toFloat(coreInL[i]);
            if (R) R[i] = spu94::plugin::boundary::toFloat(coreInR[i]);
        }
        hostNOut = coreN;
        return;
    }

    // Convert core int16 -> core float into pull scratch.
    float* fL = scratchCoreFloatOut_[0].getData();
    float* fR = scratchCoreFloatOut_[1].getData();
    for (int i = 0; i < coreN; ++i)
    {
        fL[i] = spu94::plugin::boundary::toFloat(coreInL[i]);
        fR[i] = spu94::plugin::boundary::toFloat(coreInR[i]);
    }

    pullOut_[0].data = fL;
    pullOut_[0].remaining = coreN;
    pullOut_[1].data = fR;
    pullOut_[1].remaining = coreN;

    const double ratio = hostSampleRate_ / 44100.0;

    // Request up to maxHostBlockSize_ frames per channel; libsamplerate
    // will give us roughly coreN * ratio frames.
    const long nL = src_callback_read(srcOut_[0], ratio, maxHostBlockSize_,
                                      scratchOutFloat_[0].getData());
    const long nR = src_callback_read(srcOut_[1], ratio, maxHostBlockSize_,
                                      scratchOutFloat_[1].getData());

    const long n = std::min(nL, nR);
    jassert(n <= hostScratchN_);
    srcCallbacksThisBlock_.fetch_add(1, std::memory_order_relaxed);

    float* outL = hostOut[0];
    float* outR = (hostOut[1] != nullptr) ? hostOut[1] : nullptr;
    const float* sL = scratchOutFloat_[0].getData();
    const float* sR = scratchOutFloat_[1].getData();
    for (long i = 0; i < n; ++i)
    {
        outL[i] = sL[i];
        if (outR) outR[i] = sR[i];
    }
    hostNOut = static_cast<int>(n);
}
