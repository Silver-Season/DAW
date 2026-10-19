#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Built-in effects (docs/spec/06-effects-mixing.md, tasks 6.1 and 6.2). Plain C++, no JUCE: the app wraps each effect in
// a juce::AudioProcessor (BuiltInEffectProcessor), the tests drive them directly.
namespace daw::fx
{

struct ParamInfo
{
    std::string id, name, unit;     // id is stable (saved in projects); name is English, the app shows its own translation
    float min = 0.0f, max = 1.0f, def = 0.0f;
    bool logarithmic = false;       // frequencies, times: the knob moves on a log scale
    std::vector<std::string> choices; // a stepped parameter: value = index into choices (min 0, max size - 1)

    bool stepped() const { return ! choices.empty(); }
};

// Parameter value <-> 0..1, the way knobs, automation and plugin hosts see it.
inline float toNormalised (const ParamInfo& p, float value)
{
    if (p.max <= p.min)
        return 0.0f;
    const float v = std::fmin (p.max, std::fmax (p.min, value));
    if (p.logarithmic && p.min > 0.0f)
        return (float) (std::log (v / p.min) / std::log (p.max / p.min));
    return (v - p.min) / (p.max - p.min);
}

inline float fromNormalised (const ParamInfo& p, float normalised)
{
    const float n = std::fmin (1.0f, std::fmax (0.0f, normalised));
    float v = p.logarithmic && p.min > 0.0f ? p.min * std::pow (p.max / p.min, n) : p.min + n * (p.max - p.min);
    if (p.stepped())
        v = std::round (v);
    return v;
}

// Tempo and position of the song, for effects that sync to it (delay times, LFO rates). Set once per block.
struct TransportInfo
{
    double bpm = 120.0;
    double ppq = 0.0;     // quarter notes since the start of the song at the first sample of the block
    bool playing = false;
};

class Effect
{
public:
    virtual ~Effect() = default;

    virtual std::string_view type() const = 0; // "eq8", "comp", ...
    virtual const std::vector<ParamInfo>& params() const = 0;

    // The only place that may allocate. Called before the first process and whenever the rate or the block size changes.
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;

    // Value in the parameter's units; the effect smooths it (20 ms) unless it is stepped. Any thread that owns the effect.
    virtual void setParam (int index, float value) = 0;
    virtual float getParam (int index) const = 0;

    // In place on 1 or 2 channels. `sidechain` (same number of channels, may be null) is the key signal of compressor and gate.
    virtual void process (float* const* io, int numChannels, int numSamples, const float* const* sidechain = nullptr) = 0;

    virtual int latencySamples() const { return 0; }
    virtual double tailSeconds() const { return 0.0; }
    virtual void setTransport (const TransportInfo&) {}
    virtual bool acceptsSidechain() const { return false; }

    // Values for the device's display, written by the audio thread (gain reduction in dB, input level, ...). Each effect
    // documents its own indices; 0 if it has none.
    virtual float readout (int /*index*/) const { return 0.0f; }
    // The magnitude response in dB at the given frequencies (the curve of the equaliser). False if the effect has none.
    virtual bool response (const float* /*frequencies*/, float* /*dbOut*/, int /*count*/) const { return false; }

    int paramIndex (std::string_view id) const
    {
        const auto& ps = params();
        for (int i = 0; i < (int) ps.size(); ++i)
            if (ps[(size_t) i].id == id)
                return i;
        return -1;
    }
};

struct EffectType
{
    std::string id;       // "eq8"
    std::string name;     // English name; the app has its own translation by id
    std::string category; // "eq", "dynamics", "space", "colour", "modulation", "filter"
    // Up to eight parameter ids the compact device shows as knobs, most important first; the rest fill in from params().
    std::vector<std::string> keyParams;
};

// Every built-in effect, in the order the browser lists them.
const std::vector<EffectType>& effectTypes();
// A new effect of that type, or null for an unknown id.
std::unique_ptr<Effect> createEffect (std::string_view type);

} // namespace daw::fx
