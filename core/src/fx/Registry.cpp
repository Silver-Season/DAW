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
        { "eq8", "EQ", "eq" },
        { "comp", "Compressor", "dynamics" },
        { "gate", "Gate", "dynamics" },
        { "limiter", "Limiter", "dynamics" },
        { "reverb", "Reverb", "space" },
        { "delay", "Delay", "space" },
        { "drive", "Drive", "colour" },
        { "crush", "Bitcrusher", "colour" },
        { "filter", "Filter", "filter" },
        { "chorus", "Chorus", "modulation" },
        { "flanger", "Flanger", "modulation" },
        { "phaser", "Phaser", "modulation" },
        { "autopan", "Auto Pan", "modulation" },
        { "distortion", "Distortion", "colour" },
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
