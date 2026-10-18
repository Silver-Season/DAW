#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace daw::dsp
{
// Warp modes that change tempo without changing pitch (07-warp.md, task 7.1). Re-Pitch is plain resampling and lives
// elsewhere. The engine behind each mode:
//   beats      - R3 with the short window: sharpest transients and the cheapest R3 (until our own onset slicer
//                replaces it; R2 "crisp" smears clicks more and detunes steady tones in real-time mode)
//   tones      - R3, formants not preserved, high pitch consistency
//   texture    - R2 with smooth transients (no phase resets)
//   complex    - R2 with transient phase resets above the fundamental range ("mixed"; plain "crisp" detunes a
//                steady tone by up to 2 % in real-time mode)
//   complexPro - R3 (best quality, roughly 3-5x the CPU of R2)
enum class StretchMode
{
    beats,
    tones,
    texture,
    complex,
    complexPro
};

// Streaming time stretcher, a thin wrapper that hides Rubber Band. It is meant for a background render thread that
// keeps a ring buffer ahead of the play cursor; it allocates in prepare() and may allocate inside Rubber Band while
// processing, so never call it from the audio callback.
//
// The output is latency compensated: the stretcher pads its start and drops its start delay itself, so output
// frame k corresponds to input time k / ratio from the last prepare() or reset(). Feed it the input from the point
// where playback starts.
class Stretcher
{
public:
    Stretcher();
    ~Stretcher();
    Stretcher (const Stretcher&) = delete;
    Stretcher& operator= (const Stretcher&) = delete;

    // maxBlock is the largest number of frames passed to process() in one go (larger inputs are split internally).
    void prepare (double sampleRate, int channels, StretchMode mode, int maxBlock = 4096);

    // Output length / input length: 2.0 plays twice as long (slower), 0.5 twice as fast. Can change between calls;
    // the change is smooth.
    void setRatio (double timeRatio);
    double getRatio() const noexcept { return ratio; }

    // Forgets all buffered audio and starts a new stream (after a seek). Keeps mode, ratio and channels.
    void reset();

    // Feeds numIn frames (may be 0 to only drain) and writes up to maxOut frames to out; returns frames written.
    // Output the caller has no room for stays buffered and comes out on the next calls. Pass finalBlock = true with
    // the last input to flush the tail; afterwards calls with numIn = 0 keep draining until finished() is true.
    int process (const float* const* in, int numIn, float* const* out, int maxOut, bool finalBlock = false);

    // Frames ready to be read without feeding more input.
    int available() const;
    // Input frames the stretcher wants before it can produce more output (a hint for the render thread).
    int samplesRequired() const;
    // True after a final block once every output frame has been delivered.
    bool finished() const;

    // Input frames that must be fed ahead of the first output frame (the engine's start delay, already compensated
    // in the output; the render thread uses it to know how far ahead of the cursor it has to read).
    int latency() const;

    int numChannels() const noexcept { return channels; }
    StretchMode getMode() const noexcept { return mode; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    double rate = 44100.0, ratio = 1.0;
    int channels = 0, maxBlock = 4096;
    StretchMode mode = StretchMode::complexPro;

    void startStream();
    int retrieveInto (float* const* out, int maxOut);
};

// Stretches a whole buffer (one vector per channel, equal lengths) and returns exactly round (n * timeRatio) frames
// per channel, aligned with the input (latency compensated). For rendering and tests; runs on the calling thread.
std::vector<std::vector<float>> stretchOffline (const std::vector<std::vector<float>>& in, double sampleRate,
                                                double timeRatio, StretchMode mode);
// A key frame of a variable stretch: this input frame lands on this output frame.
using KeyFrame = std::pair<int64_t, int64_t>;

// Stretches a whole buffer along key frames (warp markers): between adjacent key frames the input is stretched
// evenly, before the first and after the last with the slope of the edge segment. Returns exactly outFrames frames per
// channel. Offline (two-pass) processing, which keeps the key frames exact; for the Beats mode it calls stretchBeats.
// Runs on the calling thread (a background render job), allocates.
std::vector<std::vector<float>> stretchOfflineMapped (const std::vector<std::vector<float>>& in, double sampleRate,
                                                      const std::vector<KeyFrame>& keyFrames, int64_t outFrames,
                                                      StretchMode mode, const std::vector<int64_t>& onsets = {},
                                                      double beatsDecay = 1.0);

// The Beats mode (task 7.1), without Rubber Band: the input is cut at its onsets (and at the key frames) and every
// slice plays at its original speed and pitch from its new place, so hits stay exactly as sharp as they were. A slice
// that has to last longer than it did is continued by its own tail, played back and forth, which fades out the faster
// the lower `decay` is (1 holds it, 0 leaves a gap); a slice that has to be shorter is cut with a 3 ms fade.
std::vector<std::vector<float>> stretchBeats (const std::vector<std::vector<float>>& in, double sampleRate,
                                              const std::vector<KeyFrame>& keyFrames, int64_t outFrames,
                                              const std::vector<int64_t>& onsets, double decay);

// Where an input frame lands under a list of key frames (sorted, at least two), extrapolating at both ends.
double mapThroughKeyFrames (const std::vector<KeyFrame>& keyFrames, double inputFrame) noexcept;

} // namespace daw::dsp
