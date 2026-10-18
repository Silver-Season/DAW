#pragma once

#include "daw/fx/Effect.h"
#include "daw/fx/Helpers.h"

#include <array>
#include <vector>

namespace daw::fx
{

// The common half of every built-in effect: the parameter list with the Mix and Output knobs added at the end, smoothing
// of both, and a dry path delayed by the effect's latency so that Mix = 0 gives back the input exactly (null test).
//
// A subclass lists its own parameters, reads them with value(), and implements processWet() on the signal in place. It
// allocates only in prepareWet().
class BasicEffect : public Effect
{
public:
    const std::vector<ParamInfo>& params() const override { return paramList; }

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void setParam (int index, float value) override;
    float getParam (int index) const override;
    void process (float* const* io, int numChannels, int numSamples, const float* const* sidechain) override;

    int mixIndex() const { return mixParam; }
    int outputIndex() const { return outputParam; }

protected:
    // `own` are the effect's parameters; Mix (0..100 %, default `defaultMix`) and Output (-24..+24 dB) are appended.
    explicit BasicEffect (std::vector<ParamInfo> own, float defaultMix = 100.0f, bool withMix = true);

    float value (int index) const { return values[(size_t) index]; }

    virtual void prepareWet (double /*sampleRate*/, int /*maxBlock*/) {}
    virtual void resetWet() {}
    virtual void processWet (float* const* io, int numChannels, int numSamples, const float* const* sidechain) = 0;
    // Called by setParam after the value was stored (clamped, rounded if stepped). Audio or message thread.
    virtual void paramChanged (int /*index*/, float /*value*/) {}
    // The largest latency the effect can ever report, so the dry delay can be allocated in prepare().
    virtual int maxLatencySamples() const { return 0; }

    double rate = 48000.0;
    int blockMax = 512;

private:
    std::vector<ParamInfo> paramList;
    std::vector<float> values;
    int mixParam = -1, outputParam = -1;
    Smoother mixSmooth, outSmooth;
    std::array<std::vector<float>, 2> dry;   // a copy of the input of the block
    std::array<std::vector<float>, 2> delay; // the dry signal's delay line
    int delayPos = 0;
};

} // namespace daw::fx
