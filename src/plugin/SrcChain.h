// src/plugin/SrcChain.h
// Phase 22: bidirectional libsamplerate SRC wrapper used inside the
// JUCE wrapper's processBlock to convert between host_SR and the
// SPU-94 C core's fixed 44.1 kHz internal rate.
//
// Two SRC_STATE handles per direction (one per channel L/R), both
// constructed at SRC_SINC_MEDIUM_QUALITY in prepare(). prepare() also
// runs an impulse-response group-delay measurement so setLatencySamples
// can be reported correctly to the host's PDC graph.
//
// processIn() / processOut() are RT-safe: NO allocation, NO locks, NO
// syscalls, NO logging. All scratch buffers, SRC_STATE handles, and
// per-channel pull-callback state live in members and are allocated in
// prepare().
//
// Fast-path: when hostSampleRate is within 1e-9 of 44100.0, isFastPath_
// is set true in prepare() and processIn/processOut do straight
// float->int16 / int16->float conversion with no libsamplerate calls.

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

// Forward-declare libsamplerate's opaque state type so consumers of this
// header don't need <samplerate.h>. Defined in <samplerate.h> as a
// forward struct typedef'd to SRC_STATE.
struct SRC_STATE_tag;
typedef struct SRC_STATE_tag SRC_STATE;

class SrcChain
{
public:
    SrcChain() = default;
    ~SrcChain();

    // Called from prepareToPlay. Allocates SRC_STATE handles, allocates
    // scratch buffers from maxHostBlockSize, runs the impulse-based group-
    // delay measurement. NOT RT-safe (allocates). Idempotent: a second call
    // with the same args is a no-op; with different args it releases and
    // re-allocates.
    void prepare(double hostSampleRate, int maxHostBlockSize, int numChannels);

    // Called from releaseResources. Frees SRC_STATE handles and scratch
    // buffers.
    void release();

    // RT-safe. Consumes hostN host-rate float samples per channel, writes
    // coreNOut core-rate int16 samples per channel into coreOutL/R. In
    // fast-path mode hostN == coreNOut and no libsamplerate call runs.
    void processIn(const float* const* hostIn, int hostN,
                   int16_t* coreOutL, int16_t* coreOutR, int& coreNOut);

    // RT-safe. Consumes coreN core-rate int16 samples per channel from
    // coreInL/R, writes hostNOut host-rate float samples per channel into
    // hostOut[ch]. In fast-path mode coreN == hostNOut.
    void processOut(const int16_t* coreInL, const int16_t* coreInR, int coreN,
                    float* const* hostOut, int& hostNOut);

    bool isFastPath() const noexcept { return isFastPath_; }
    int  getMeasuredLatencyHostSamples() const noexcept { return measuredLatencyHostSamples_; }

private:
    // Per-channel libsamplerate callback userdata. The pull-callback walks
    // a contiguous float buffer one block at a time.
    struct PullCtx
    {
        const float* data { nullptr };  // pointer to float input scratch
        long         remaining { 0 };   // frames still to deliver
    };

    static long pullCallback(void* cbData, float** dataOut) noexcept;

    SRC_STATE*  srcIn_  [2] { nullptr, nullptr };  // host_SR -> 44100, per channel
    SRC_STATE*  srcOut_ [2] { nullptr, nullptr };  // 44100   -> host_SR, per channel
    PullCtx     pullIn_ [2] {};                    // userdata for srcIn_
    PullCtx     pullOut_[2] {};                    // userdata for srcOut_

    // Scratch buffers allocated in prepare(). Float, per channel.
    juce::HeapBlock<float> scratchInFloat_     [2]; // host-rate float in (per channel)
    juce::HeapBlock<float> scratchCoreFloatIn_ [2]; // core-rate float out of input SRC
    juce::HeapBlock<float> scratchCoreFloatOut_[2]; // core-rate float into output SRC
    juce::HeapBlock<float> scratchOutFloat_    [2]; // host-rate float out of output SRC

    double hostSampleRate_   = 0.0;
    int    maxHostBlockSize_ = 0;
    int    numChannels_      = 0;
    int    coreScratchN_     = 0;    // maxHostBlockSize + 32
    int    hostScratchN_     = 0;    // maxHostBlockSize * 5 + 32 (up to 192 kHz)
    bool   isFastPath_       = false;
    int    measuredLatencyHostSamples_ = 0;

    // Called once from prepare() (non-fast-path only). Pushes a Kronecker
    // delta through each direction's SRC and locates the centre-of-energy
    // of the response. Returns the total measured latency in host samples
    // (input_SRC latency + output_SRC latency).
    int measureGroupDelayInHostSamples();
};
