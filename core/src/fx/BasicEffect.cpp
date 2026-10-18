#include "daw/fx/BasicEffect.h"

#include <algorithm>

namespace daw::fx
{

BasicEffect::BasicEffect (std::vector<ParamInfo> own, float defaultMix, bool withMix) : paramList (std::move (own))
{
    if (withMix)
    {
        mixParam = (int) paramList.size();
        paramList.push_back ({ "mix", "Mix", "%", 0.0f, 100.0f, defaultMix, false, {} });
    }
    outputParam = (int) paramList.size();
    paramList.push_back ({ "output", "Output", "dB", -24.0f, 24.0f, 0.0f, false, {} });
    for (const auto& p : paramList)
        values.push_back (p.def);
    mixSmooth.setTarget (withMix ? defaultMix / 100.0f : 1.0f);
    mixSmooth.snap();
    outSmooth.setTarget (1.0f);
    outSmooth.snap();
}

void BasicEffect::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate;
    blockMax = std::max (16, maxBlock);
    for (auto& d : dry)
        d.assign ((size_t) blockMax, 0.0f);
    const int maxLatency = std::max (0, maxLatencySamples());
    for (auto& d : delay)
        d.assign ((size_t) (maxLatency + 1), 0.0f);
    delayPos = 0;
    mixSmooth.prepare (sampleRate);
    outSmooth.prepare (sampleRate);
    prepareWet (sampleRate, blockMax);
    reset();
}

void BasicEffect::reset()
{
    for (auto& d : delay)
        std::fill (d.begin(), d.end(), 0.0f);
    delayPos = 0;
    mixSmooth.snap();
    outSmooth.snap();
    resetWet();
}

void BasicEffect::setParam (int index, float v)
{
    if (index < 0 || index >= (int) paramList.size())
        return;
    const auto& p = paramList[(size_t) index];
    v = std::clamp (v, p.min, p.max);
    if (p.stepped())
        v = std::round (v);
    values[(size_t) index] = v;
    if (index == mixParam)
        mixSmooth.setTarget (v / 100.0f);
    else if (index == outputParam)
        outSmooth.setTarget (dbToGain (v));
    paramChanged (index, v);
}

float BasicEffect::getParam (int index) const
{
    return index >= 0 && index < (int) values.size() ? values[(size_t) index] : 0.0f;
}

void BasicEffect::process (float* const* io, int numChannels, int numSamples, const float* const* sidechain)
{
    const int channels = std::clamp (numChannels, 1, 2);
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (blockMax, numSamples - done);
        float* chunk[2] { io[0] + done, io[channels - 1] + done };
        const float* sc[2] { nullptr, nullptr };
        if (sidechain != nullptr && sidechain[0] != nullptr)
        {
            sc[0] = sidechain[0] + done;
            sc[1] = (sidechain[channels - 1] != nullptr ? sidechain[channels - 1] : sidechain[0]) + done;
        }

        for (int c = 0; c < channels; ++c)
            std::copy (chunk[c], chunk[c] + n, dry[(size_t) c].begin());

        processWet (chunk, channels, n, sidechain != nullptr ? sc : nullptr);

        // The dry signal waits as long as the wet one, so that both line up when they are mixed.
        const int size = (int) delay[0].size();
        const int latency = std::clamp (latencySamples(), 0, size - 1);
        if (size > 1)
        {
            for (int i = 0; i < n; ++i)
            {
                const int readPos = (delayPos - latency + size) % size;
                for (int c = 0; c < channels; ++c)
                {
                    auto& line = delay[(size_t) c];
                    line[(size_t) delayPos] = dry[(size_t) c][(size_t) i];
                    dry[(size_t) c][(size_t) i] = line[(size_t) readPos];
                }
                delayPos = (delayPos + 1) % size;
            }
        }

        const bool still = ! mixSmooth.smoothing() && ! outSmooth.smoothing();
        if (still && mixSmooth.current >= 1.0f && std::abs (outSmooth.current - 1.0f) < 1.0e-9f)
        {
            done += n;
            continue; // fully wet at 0 dB: nothing to do
        }
        for (int i = 0; i < n; ++i)
        {
            const float m = mixSmooth.next(), g = outSmooth.next();
            for (int c = 0; c < channels; ++c)
                chunk[c][i] = (dry[(size_t) c][(size_t) i] * (1.0f - m) + chunk[c][i] * m) * g;
        }
        done += n;
    }
}

} // namespace daw::fx
