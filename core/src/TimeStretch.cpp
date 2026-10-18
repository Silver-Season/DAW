#include "daw/dsp/TimeStretch.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace daw::dsp
{
namespace
{
using RB = RubberBand::RubberBandStretcher;

RB::Options optionsFor (StretchMode mode)
{
    // Real-time mode lets the ratio change while playing; the render thread, not the audio callback, drives it.
    // Channels together keeps the stereo image (and a centred mono source stays identical in both channels).
    RB::Options o = RB::OptionProcessRealTime | RB::OptionThreadingNever | RB::OptionChannelsTogether;
    switch (mode)
    {
        case StretchMode::beats:
            return o | RB::OptionEngineFiner | RB::OptionWindowShort;
        case StretchMode::tones:
            return o | RB::OptionEngineFiner | RB::OptionFormantShifted | RB::OptionPitchHighConsistency;
        case StretchMode::texture:
            return o | RB::OptionEngineFaster | RB::OptionTransientsSmooth;
        case StretchMode::complex:
            return o | RB::OptionEngineFaster | RB::OptionTransientsMixed;
        case StretchMode::complexPro:
            break;
    }
    return o | RB::OptionEngineFiner;
}
} // namespace

struct Stretcher::Impl
{
    std::unique_ptr<RB> rb;
    std::vector<std::vector<float>> scratch; // channels x maxBlock: zeros for padding, sink for dropped frames
    std::vector<float*> scratchPtrs, outPtrs;
    std::vector<const float*> inPtrs;
    int64_t toDrop = 0;         // start-delay frames still to discard from the output
    bool fedInput = false;      // any caller input since the stream started
    bool finalSent = false;
};

Stretcher::Stretcher() : impl (std::make_unique<Impl>()) {}
Stretcher::~Stretcher() = default;

void Stretcher::prepare (double sampleRate, int numChannels, StretchMode newMode, int newMaxBlock)
{
    rate = sampleRate;
    channels = std::max (1, numChannels);
    mode = newMode;
    maxBlock = std::max (64, newMaxBlock);

    impl->rb = std::make_unique<RB> ((size_t) std::lround (rate), (size_t) channels, optionsFor (mode), ratio, 1.0);
    impl->rb->setMaxProcessSize ((size_t) maxBlock);

    impl->scratch.assign ((size_t) channels, std::vector<float> ((size_t) maxBlock, 0.0f));
    impl->scratchPtrs.resize ((size_t) channels);
    for (int c = 0; c < channels; ++c)
        impl->scratchPtrs[(size_t) c] = impl->scratch[(size_t) c].data();
    impl->outPtrs.resize ((size_t) channels);
    impl->inPtrs.resize ((size_t) channels);

    startStream();
}

void Stretcher::setRatio (double timeRatio)
{
    ratio = std::clamp (timeRatio, 0.05, 20.0);
    if (impl->rb == nullptr)
        return;
    if (! impl->fedInput)
        startStream(); // the start pad and delay depend on the ratio, so recompute them before the first input
    else
        impl->rb->setTimeRatio (ratio);
}

void Stretcher::reset()
{
    if (impl->rb != nullptr)
        startStream();
}

void Stretcher::startStream()
{
    auto& rb = *impl->rb;
    rb.reset();
    rb.setTimeRatio (ratio);

    // Rubber Band's recommended real-time start: feed its preferred pad of silence, then drop its start delay from
    // the output, so the first output frame lines up with the first input frame.
    for (auto& ch : impl->scratch)
        std::fill (ch.begin(), ch.end(), 0.0f);
    for (int64_t pad = (int64_t) rb.getPreferredStartPad(); pad > 0;)
    {
        const int n = (int) std::min<int64_t> (pad, maxBlock);
        rb.process (impl->scratchPtrs.data(), (size_t) n, false);
        pad -= n;
    }
    impl->toDrop = (int64_t) rb.getStartDelay();
    impl->fedInput = false;
    impl->finalSent = false;
}

int Stretcher::retrieveInto (float* const* out, int maxOut)
{
    auto& rb = *impl->rb;
    while (impl->toDrop > 0)
    {
        const int avail = rb.available();
        if (avail <= 0)
            return 0;
        const int n = (int) std::min<int64_t> ({ (int64_t) avail, impl->toDrop, (int64_t) maxBlock });
        rb.retrieve (impl->scratchPtrs.data(), (size_t) n);
        impl->toDrop -= n;
    }

    const int avail = rb.available();
    if (avail <= 0 || maxOut <= 0)
        return 0;
    return (int) rb.retrieve (out, (size_t) std::min (avail, maxOut));
}

int Stretcher::process (const float* const* in, int numIn, float* const* out, int maxOut, bool finalBlock)
{
    if (impl->rb == nullptr)
        return 0;

    auto& rb = *impl->rb;
    int written = 0, offset = 0;
    auto drain = [&]
    {
        for (int c = 0; c < channels; ++c)
            impl->outPtrs[(size_t) c] = out[c] + written;
        written += retrieveInto (impl->outPtrs.data(), maxOut - written);
    };

    // Feed in chunks the engine accepts, reading output between chunks so its buffers stay bounded.
    while (offset < numIn)
    {
        drain();
        const int n = std::min (maxBlock, numIn - offset);
        const bool last = finalBlock && offset + n == numIn;
        for (int c = 0; c < channels; ++c)
            impl->inPtrs[(size_t) c] = in[c] + offset;
        rb.process (impl->inPtrs.data(), (size_t) n, last);
        impl->fedInput = true;
        impl->finalSent = impl->finalSent || last;
        offset += n;
    }

    if (finalBlock && ! impl->finalSent)
    {
        for (int c = 0; c < channels; ++c)
            impl->inPtrs[(size_t) c] = impl->scratchPtrs[(size_t) c];
        rb.process (impl->inPtrs.data(), 0, true);
        impl->fedInput = impl->finalSent = true;
    }

    drain();
    return written;
}

int Stretcher::available() const
{
    if (impl->rb == nullptr)
        return 0;
    const int avail = impl->rb->available();
    return avail <= 0 ? 0 : (int) std::max<int64_t> (0, avail - impl->toDrop);
}

int Stretcher::samplesRequired() const
{
    return impl->rb == nullptr ? 0 : (int) impl->rb->getSamplesRequired();
}

bool Stretcher::finished() const
{
    return impl->rb != nullptr && impl->finalSent && impl->rb->available() < 0;
}

int Stretcher::latency() const
{
    if (impl->rb == nullptr)
        return 0;
    // The start delay is in output frames; the render thread reads ahead in input frames.
    return (int) std::ceil ((double) impl->rb->getStartDelay() / ratio);
}

std::vector<std::vector<float>> stretchOffline (const std::vector<std::vector<float>>& in, double sampleRate,
                                                double timeRatio, StretchMode mode)
{
    const int channels = (int) in.size();
    if (channels == 0)
        return {};

    const int64_t n = (int64_t) in[0].size();
    const int64_t target = std::llround ((double) n * timeRatio);
    std::vector<std::vector<float>> result ((size_t) channels, std::vector<float> ((size_t) target, 0.0f));
    if (n == 0 || target == 0)
        return result;

    constexpr int kBlock = 4096;
    Stretcher s;
    s.setRatio (timeRatio);
    s.prepare (sampleRate, channels, mode, kBlock);

    std::vector<const float*> inPtrs ((size_t) channels);
    std::vector<float*> outPtrs ((size_t) channels);
    int64_t read = 0, written = 0;
    auto run = [&] (int numIn, bool last)
    {
        for (int c = 0; c < channels; ++c)
        {
            inPtrs[(size_t) c] = in[(size_t) c].data() + read;
            outPtrs[(size_t) c] = result[(size_t) c].data() + written;
        }
        const int room = (int) std::min<int64_t> (target - written, 1 << 30);
        const int got = s.process (inPtrs.data(), numIn, outPtrs.data(), room, last);
        read += numIn;
        written += got;
        return got;
    };

    while (read < n && written < target)
    {
        const int numIn = (int) std::min<int64_t> (kBlock, n - read);
        run (numIn, read + numIn == n);
    }
    // Drain the tail; anything the engine does not deliver (it rounds lengths slightly) stays silent.
    for (int idle = 0; written < target && ! s.finished() && idle < 4;)
        idle = run (0, false) == 0 ? idle + 1 : 0;
    return result;
}
} // namespace daw::dsp
