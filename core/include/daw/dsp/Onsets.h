#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace daw::dsp
{
// Offline audio analysis for warping (task 7.2). Runs on a background thread at import, never on the audio thread.

// Onset times in seconds, ascending. Spectral flux (STFT 1024, hop 256, Hann, log magnitude, positive difference
// summed over bins), adaptive threshold (median of 11 frames x 1.5 + offset), at least 50 ms between onsets, each
// position refined in the time domain to the sharpest rise in energy. sensitivity 0..1 scales the threshold:
// 0.5 is the default, 1 finds the faintest transients, 0 only the strongest.
std::vector<double> detectOnsets (const float* mono, int64_t numSamples, double sampleRate, float sensitivity = 0.5f);

struct TempoEstimate
{
    double bpm = 0.0;              // 0 when nothing periodic was found
    double confidence = 0.0;       // 0..1; a file-name or bar-length hint that decided the tempo gives 1
    double firstBeatSeconds = 0.0; // the first beat that looks like a downbeat (bar 1)
    std::vector<double> beats;     // beat times in seconds, ascending
};

// Tempo and beat grid. Autocorrelation of the onset function scored over 60..200 BPM with a preference for 90..150 and
// octave checks, then a dynamic-programming beat tracker (Ellis). The tempo is refined from the beat positions and
// snapped to hints: a tempo in the file name ("_124bpm", "124 BPM", "124-") and a file length that is a whole number
// of 4/4 bars (1..64) at an integer tempo within 1 % of the estimate, as loops and tracks exported from a DAW are.
TempoEstimate estimateTempo (const float* mono, int64_t numSamples, double sampleRate, const std::string& fileName = {});

} // namespace daw::dsp
