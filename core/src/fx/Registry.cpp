#include "daw/fx/Effect.h"
namespace daw::fx
{
std::unique_ptr<Effect> makeEq8();
std::unique_ptr<Effect> makeCompressor();
std::unique_ptr<Effect> makeGate();
std::unique_ptr<Effect> makeLimiter();
std::unique_ptr<Effect> makeReverb();
std::unique_ptr<Effect> makeDelay();
std::unique_ptr<Effect> makeDrive();
std::unique_ptr<Effect> makeDistortion();
std::unique_ptr<Effect> makeCrush();
std::unique_ptr<Effect> makeFilter();
std::unique_ptr<Effect> makeChorus();
std::unique_ptr<Effect> makeFlanger();
std::unique_ptr<Effect> makePhaser();
std::unique_ptr<Effect> makeAutoPan();

const std::vector<EffectType>& effectTypes()
{
    static const std::vector<EffectType> types {
        { "eq8", "EQ", "eq",
          { "b2gain", "b3gain", "b4gain", "b5gain", "b6gain", "b7gain", "b1on", "b8on" } },
        { "comp", "Compressor", "dynamics",
          { "threshold", "ratio", "attack", "release", "knee", "makeup", "mix", "output" } },
        { "gate", "Gate", "dynamics",
          { "threshold", "hysteresis", "attack", "hold", "release", "floor", "lookahead", "output" } },
        { "limiter", "Limiter", "dynamics",
          { "ceiling", "gain", "release", "lookahead", "truePeak", "autoRelease", "output" } },
        { "reverb", "Reverb", "space",
          { "size", "decay", "predelay", "damping", "lowCut", "width", "diffusion", "mix" } },
        { "delay", "Delay", "space",
          { "divL", "divR", "feedback", "pingPong", "hp", "lp", "ducking", "mix" } },
        { "drive", "Drive", "colour",
          { "mode", "drive", "tone", "bias", "input", "mix", "output" } },
        { "crush", "Bitcrusher", "colour",
          { "bits", "rate", "dither", "mix", "output" } },
        { "filter", "Filter", "filter",
          { "type", "freq", "resonance", "drive", "lfoDepth", "lfoRate", "envDepth", "mix" } },
        { "chorus", "Chorus", "modulation",
          { "voices", "rate", "depth", "delay", "width", "feedback", "mix", "output" } },
        { "flanger", "Flanger", "modulation",
          { "rate", "depth", "delay", "feedback", "phase", "sync", "div", "mix" } },
        { "phaser", "Phaser", "modulation",
          { "stages", "rate", "depth", "center", "feedback", "phase", "sync", "mix" } },
        { "autopan", "Auto Pan", "modulation",
          { "rate", "div", "sync", "depth", "shape", "phase", "offset", "mix" } },
        { "distortion", "Distortion", "colour",
          { "mode", "drive", "tone", "bias", "input", "mix", "output" } },
    };
    return types;
}

std::unique_ptr<Effect> createEffect (std::string_view type)
{
    if (type == "eq8")
        return makeEq8();
    if (type == "comp")
        return makeCompressor();
    if (type == "gate")
        return makeGate();
    if (type == "limiter")
        return makeLimiter();
    if (type == "reverb")
        return makeReverb();
    if (type == "delay")
        return makeDelay();
    if (type == "drive")
        return makeDrive();
    if (type == "distortion")
        return makeDistortion();
    if (type == "crush")
        return makeCrush();
    if (type == "filter")
        return makeFilter();
    if (type == "chorus")
        return makeChorus();
    if (type == "flanger")
        return makeFlanger();
    if (type == "phaser")
        return makePhaser();
    if (type == "autopan")
        return makeAutoPan();
    return nullptr;
}

} // namespace daw::fx
