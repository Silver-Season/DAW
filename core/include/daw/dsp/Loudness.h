#pragma once

#include <cstddef>

namespace daw::dsp
{
// Loudness of a finished stereo mix per ITU-R BS.1770-4 / EBU R128 (docs/spec/04-export.md, task 4.4). Offline: the
// export measures the rendered song with it, never the audio thread.
//
// K-weighting: the high shelf (+4 dB around 1.7 kHz) and the high-pass (~38 Hz) of BS.1770, computed for any rate.
// Integrated loudness: 400 ms blocks every 100 ms, absolute gate -70 LUFS, relative gate -10 LU. Loudness range:
// 3 s blocks every 1 s, gates -70 LUFS and -20 LU, 10th to 95th percentile. True peak: 4x oversampled.
struct LoudnessReport
{
    double integratedLufs = -70.0; // -70 for silence
    double rangeLu = 0.0;
    double truePeakDb = -120.0;    // dBTP
    double samplePeakDb = -120.0;  // dBFS
};

LoudnessReport measureLoudness (const float* left, const float* right, size_t numFrames, double sampleRate);

// The highest absolute value between the samples, from a 4x windowed-sinc interpolation, in dBTP.
double truePeakDb (const float* samples, size_t numFrames);
} // namespace daw::dsp
