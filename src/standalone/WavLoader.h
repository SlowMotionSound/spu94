#pragma once

#include <JuceHeader.h>
#include <vector>
#include <optional>
#include <cstdint>

/// Result of loading and normalizing a WAV (or AIFF) file to 44.1 kHz
/// int16 stereo -- the format spu94_process expects.
struct LoadedWav {
    std::vector<int16_t> L;    ///< left channel, 44.1 kHz int16
    std::vector<int16_t> R;    ///< right channel, 44.1 kHz int16
    uint64_t numFrames = 0;    ///< number of stereo frames
};

namespace WavLoader {
    /// Load any WAV/AIFF file and normalize to 44.1 kHz int16 stereo.
    /// Returns std::nullopt on failure (unreadable file, unsupported format).
    ///
    /// Per D-06: bit-depth conversion (any -> int16), sample-rate conversion
    /// (any -> 44.1 kHz via WindowedSincInterpolator), channel adaptation
    /// (mono -> duplicate to stereo). SPU core stays bit-faithful.
    std::optional<LoadedWav> load(const juce::File& file);
}
