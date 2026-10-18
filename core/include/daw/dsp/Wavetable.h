#pragma once

#include <memory>
#include <string>
#include <vector>

namespace daw::dsp
{

// A morphable wavetable: N single-cycle frames, each stored as a chain of band-limited mip levels
// (one per octave), so that playback at any pitch stays free of aliasing.
class Wavetable
{
public:
    static constexpr int kFrameSize = 2048; // the common 2048-sample frame, so tables made elsewhere can be imported later
    static constexpr int kNumMips = 11;     // mip m keeps harmonics 1 .. (1024 >> m)

    // Each frame must have kFrameSize samples. Frames are normalised as a whole (relative levels kept).
    static std::shared_ptr<const Wavetable> fromFrames (std::string name, const std::vector<std::vector<float>>& frames);

    const std::string& name() const noexcept { return name_; }
    int numFrames() const noexcept { return numFrames_; }

    // Mip level that keeps every harmonic below Nyquist for the given phase increment (cycles per sample).
    static int mipForIncrement (double phaseInc) noexcept;

    // framePos in [0, 1] morphs across frames, phase in [0, 1).
    float sample (float framePos, double phase, int mip) const noexcept;

    // Raw access to one mip level of one frame (kFrameSize + 1 samples, the last one is a wrap guard).
    const float* data (int frame, int mip) const noexcept;

private:
    Wavetable() = default;

    std::string name_;
    int numFrames_ = 0;
    std::vector<float> data_;
};

using WavetablePtr = std::shared_ptr<const Wavetable>;

// Built-in tables shipped with Prisma.
namespace factory
{
    const std::vector<std::string>& tableNames();
    WavetablePtr table (int index); // built lazily, cached, thread-safe
}

} // namespace daw::dsp
