#include "WavLoader.h"
#include <algorithm>
#include <cmath>

std::optional<LoadedWav> WavLoader::load(const juce::File& file)
{
    // Register WAV + AIFF readers (all bit depths: 8/16/24/32-int/32-float).
    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    auto reader = std::unique_ptr<juce::AudioFormatReader>(fmt.createReaderFor(file));
    if (!reader)
        return std::nullopt;

    // Capture original file metadata before any conversion.
    const auto srcSR     = reader->sampleRate;
    const auto srcChans  = static_cast<int>(reader->numChannels);
    const auto srcFrames = static_cast<int64_t>(reader->lengthInSamples);
    const auto srcBits   = static_cast<int>(reader->bitsPerSample);

    if (srcFrames <= 0 || srcSR <= 0.0)
        return std::nullopt;

    // Read all samples into a float buffer. AudioFormatReader always
    // outputs float regardless of source bit depth.
    juce::AudioBuffer<float> srcBuf(srcChans, static_cast<int>(srcFrames));
    reader->read(&srcBuf, 0, static_cast<int>(srcFrames), 0, true,
                 srcChans > 1);

    // Resample to 44.1 kHz if needed. WindowedSincInterpolator is the
    // highest-quality choice; 100-sample latency is irrelevant since
    // the resample happens once at file-load time on the message thread.
    constexpr double kTargetSR = 44100.0;
    const double ratio = srcSR / kTargetSR; // >1 = downsample, <1 = upsample
    const int64_t dstFrames = static_cast<int64_t>(std::ceil(
        static_cast<double>(srcFrames) / ratio));

    // Always produce a stereo buffer.
    juce::AudioBuffer<float> dstBuf(2, static_cast<int>(dstFrames));
    dstBuf.clear();

    if (std::abs(ratio - 1.0) < 1e-9)
    {
        // Source is already 44.1 kHz -- straight copy, no interpolator.
        if (srcChans == 1)
        {
            dstBuf.copyFrom(0, 0, srcBuf, 0, 0, static_cast<int>(srcFrames));
            dstBuf.copyFrom(1, 0, srcBuf, 0, 0, static_cast<int>(srcFrames));
        }
        else
        {
            dstBuf.copyFrom(0, 0, srcBuf, 0, 0, static_cast<int>(srcFrames));
            dstBuf.copyFrom(1, 0, srcBuf, 1, 0, static_cast<int>(srcFrames));
        }
    }
    else
    {
        // Resample each channel independently -- per-channel state required
        // (Pitfall 7: sharing one interpolator across L/R mixes filter state).
        juce::WindowedSincInterpolator interpL, interpR;
        interpL.process(ratio, srcBuf.getReadPointer(0),
                        dstBuf.getWritePointer(0), static_cast<int>(dstFrames));
        if (srcChans == 1)
        {
            // Mono source: resample the same channel into R.
            interpR.process(ratio, srcBuf.getReadPointer(0),
                            dstBuf.getWritePointer(1), static_cast<int>(dstFrames));
        }
        else
        {
            interpR.process(ratio, srcBuf.getReadPointer(1),
                            dstBuf.getWritePointer(1), static_cast<int>(dstFrames));
        }
    }

    // Convert float [-1, 1] to int16 [-32768, 32767] with correct rounding
    // and saturation. Scale by 32767 (NOT 32768) -- Pitfall 10.
    auto f2i = [](float x) -> int16_t {
        const float scaled = std::clamp(x, -1.0f, 1.0f) * 32767.0f;
        const int v = static_cast<int>(std::lround(scaled));
        return static_cast<int16_t>(std::clamp(v, -32768, 32767));
    };

    LoadedWav out;
    out.numFrames = static_cast<uint64_t>(dstFrames);
    out.L.resize(static_cast<size_t>(dstFrames));
    out.R.resize(static_cast<size_t>(dstFrames));
    out.originalSampleRate   = srcSR;
    out.originalNumChannels  = srcChans;
    out.originalBitsPerSample = srcBits;

    const auto* fL = dstBuf.getReadPointer(0);
    const auto* fR = dstBuf.getReadPointer(1);
    for (int64_t i = 0; i < dstFrames; ++i)
    {
        out.L[static_cast<size_t>(i)] = f2i(fL[i]);
        out.R[static_cast<size_t>(i)] = f2i(fR[i]);
    }

    return out;
}
