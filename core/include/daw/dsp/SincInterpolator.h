#pragma once

#include <cstdint>
#include <vector>

namespace daw::dsp
{
// Windowed-sinc interpolation for resampling audio clips in real time: 16 taps, 256 sub-sample phases precomputed
// (Kaiser window, beta 8), linear interpolation between adjacent phase rows. Allocation free after construction;
// safe on the audio thread.
class SincInterpolator
{
public:
    static constexpr int kTaps = 16, kPhases = 256;

    SincInterpolator(); // builds the table (message thread)

    // Value of the band-limited signal `data` (length samples, zero outside [0, length)) at fractional position pos.
    // Positions within 1e-9 of an integer return data[i] exactly (fast path), which keeps unity-rate playback bit exact.
    float valueAt (const float* data, int64_t length, double pos) const noexcept;

private:
    std::vector<float> table; // (kPhases + 1) rows of kTaps; row p holds the weights for fraction p / kPhases
};

// A process-wide instance built on first use (call once on the message thread before audio starts).
const SincInterpolator& sharedSincInterpolator();
} // namespace daw::dsp
