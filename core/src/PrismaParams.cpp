#include "daw/synth/PrismaParams.h"
#include "daw/dsp/Wavetable.h"

#include <cassert>
#include <iterator>

namespace daw::synth
{

namespace
{
    using K = ParamDescriptor::Kind;

    const std::vector<std::string> kFilterTypes { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "Notch" };
    const std::vector<std::string> kLfoShapes { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H" };
    const std::vector<std::string> kSubOctaves { "-1 oct", "-2 oct" };

    struct Builder
    {
        std::vector<ParamDescriptor> list;
        std::string group;

        ParamDescriptor& add (std::string id, std::string name, K kind, float min, float max, float def,
                              std::function<float& (PrismaParams&)> access)
        {
            ParamDescriptor d;
            d.id = std::move (id);
            d.name = std::move (name);
            d.group = group;
            d.kind = kind;
            d.min = min;
            d.max = max;
            d.def = def;
            d.access = std::move (access);
            list.push_back (std::move (d));
            return list.back();
        }

        ParamDescriptor& choice (std::string id, std::string name, const std::vector<std::string>& choices, int def,
                                 std::function<float& (PrismaParams&)> access)
        {
            auto& d = add (std::move (id), std::move (name), K::Choice, 0, (float) choices.size() - 1, (float) def, std::move (access));
            d.choices = choices;
            return d;
        }
    };

    std::vector<ParamDescriptor> build()
    {
        Builder b;

        for (int o = 0; o < PrismaParams::kNumOsc; ++o)
        {
            const std::string pre = o == 0 ? "oscA_" : "oscB_";
            const std::string nm = o == 0 ? "Osc A " : "Osc B ";
            b.group = o == 0 ? "Osc A" : "Osc B";
            auto O = [o] (auto member) {
                return [o, member] (PrismaParams& p) -> float& { return p.osc[(size_t) o].*member; };
            };
            b.add (pre + "on", nm + "On", K::Bool, 0, 1, o == 0 ? 1.0f : 0.0f, O (&OscParams::on));
            b.choice (pre + "table", nm + "Table", dsp::factory::tableNames(), 0, O (&OscParams::table));
            b.add (pre + "pos", nm + "WT Pos", K::Float, 0, 1, 0.66f, O (&OscParams::wtPos));
            b.add (pre + "oct", nm + "Octave", K::Int, -3, 3, 0, O (&OscParams::octave));
            b.add (pre + "semi", nm + "Semi", K::Int, -12, 12, 0, O (&OscParams::semi));
            b.add (pre + "fine", nm + "Fine", K::Float, -100, 100, 0, O (&OscParams::fine)).unit = "ct";
            b.add (pre + "level", nm + "Level", K::Float, 0, 1, 0.75f, O (&OscParams::level));
            b.add (pre + "pan", nm + "Pan", K::Float, -1, 1, 0, O (&OscParams::pan));
            b.add (pre + "unison", nm + "Unison", K::Int, 1, 16, 1, O (&OscParams::unison));
            b.add (pre + "detune", nm + "Detune", K::Float, 0, 100, 25, O (&OscParams::detune)).unit = "ct";
            b.add (pre + "spread", nm + "Spread", K::Float, 0, 1, 0.8f, O (&OscParams::spread));
            b.add (pre + "blend", nm + "Blend", K::Float, 0, 1, 0.75f, O (&OscParams::blend));
            b.add (pre + "phase", nm + "Phase", K::Float, 0, 1, 0, O (&OscParams::phase));
            b.add (pre + "rand", nm + "Rand Phase", K::Float, 0, 1, 1, O (&OscParams::randPhase));
        }

        b.group = "Sub / Noise";
        b.add ("sub_on", "Sub On", K::Bool, 0, 1, 0, [] (PrismaParams& p) -> float& { return p.subOn; });
        b.add ("sub_level", "Sub Level", K::Float, 0, 1, 0.5f, [] (PrismaParams& p) -> float& { return p.subLevel; });
        b.choice ("sub_oct", "Sub Octave", kSubOctaves, 0, [] (PrismaParams& p) -> float& { return p.subOctave; });
        b.add ("noise_level", "Noise Level", K::Float, 0, 1, 0, [] (PrismaParams& p) -> float& { return p.noiseLevel; });

        b.group = "Filter";
        b.add ("flt_on", "Filter On", K::Bool, 0, 1, 1, [] (PrismaParams& p) -> float& { return p.filterOn; });
        b.choice ("flt_type", "Filter Type", kFilterTypes, 1, [] (PrismaParams& p) -> float& { return p.filterType; });
        auto& cut = b.add ("flt_cutoff", "Cutoff", K::Float, 20, 20000, 12000, [] (PrismaParams& p) -> float& { return p.cutoff; });
        cut.skewCentre = 1000;
        cut.unit = "Hz";
        b.add ("flt_res", "Resonance", K::Float, 0, 1, 0.1f, [] (PrismaParams& p) -> float& { return p.resonance; });
        b.add ("flt_drive", "Drive", K::Float, 0, 1, 0, [] (PrismaParams& p) -> float& { return p.drive; });
        b.add ("flt_key", "Key Track", K::Float, 0, 1, 0, [] (PrismaParams& p) -> float& { return p.keyTrack; });
        b.add ("flt_env", "Env 2 Amount", K::Float, -1, 1, 0, [] (PrismaParams& p) -> float& { return p.filterEnvAmount; });

        for (int e = 0; e < PrismaParams::kNumEnv; ++e)
        {
            const std::string pre = "env" + std::to_string (e + 1) + "_";
            const std::string nm = e == 0 ? "Amp Env " : "Env " + std::to_string (e + 1) + " ";
            b.group = e == 0 ? "Amp Env" : "Env " + std::to_string (e + 1);
            auto E = [e] (auto member) {
                return [e, member] (PrismaParams& p) -> float& { return p.env[(size_t) e].*member; };
            };
            auto& a = b.add (pre + "a", nm + "Attack", K::Float, 0.0005f, 10, 0.005f, E (&EnvParams::attack));
            a.skewCentre = 0.3f;
            a.unit = "s";
            auto& d = b.add (pre + "d", nm + "Decay", K::Float, 0.001f, 10, 0.4f, E (&EnvParams::decay));
            d.skewCentre = 0.6f;
            d.unit = "s";
            b.add (pre + "s", nm + "Sustain", K::Float, 0, 1, e == 0 ? 1.0f : 0.5f, E (&EnvParams::sustain));
            auto& r = b.add (pre + "r", nm + "Release", K::Float, 0.001f, 15, 0.25f, E (&EnvParams::release));
            r.skewCentre = 0.8f;
            r.unit = "s";
        }

        for (int l = 0; l < PrismaParams::kNumLfo; ++l)
        {
            const std::string pre = "lfo" + std::to_string (l + 1) + "_";
            const std::string nm = "LFO " + std::to_string (l + 1) + " ";
            b.group = "LFO " + std::to_string (l + 1);
            auto L = [l] (auto member) {
                return [l, member] (PrismaParams& p) -> float& { return p.lfo[(size_t) l].*member; };
            };
            b.choice (pre + "shape", nm + "Shape", kLfoShapes, 0, L (&LfoParams::shape));
            auto& rate = b.add (pre + "rate", nm + "Rate", K::Float, 0.01f, 30, 1, L (&LfoParams::rate));
            rate.skewCentre = 2;
            rate.unit = "Hz";
            b.add (pre + "sync", nm + "Sync", K::Bool, 0, 1, 0, L (&LfoParams::sync));
            b.choice (pre + "div", nm + "Division", syncDivisionNames(), 4, L (&LfoParams::syncDiv));
            b.add (pre + "retrig", nm + "Retrigger", K::Bool, 0, 1, 1, L (&LfoParams::retrig));
        }

        b.group = "Mod Matrix";
        for (int m = 0; m < PrismaParams::kNumModSlots; ++m)
        {
            const std::string pre = "mod" + std::to_string (m + 1) + "_";
            const std::string nm = "Mod " + std::to_string (m + 1) + " ";
            auto M = [m] (auto member) {
                return [m, member] (PrismaParams& p) -> float& { return p.mod[(size_t) m].*member; };
            };
            b.choice (pre + "src", nm + "Source", modSourceNames(), 0, M (&ModSlot::source));
            b.choice (pre + "dst", nm + "Destination", modDestNames(), 0, M (&ModSlot::dest));
            b.add (pre + "amt", nm + "Amount", K::Float, -1, 1, 0, M (&ModSlot::amount));
        }

        b.group = "Master";
        b.add ("master_gain", "Master Gain", K::Float, -48, 6, -6, [] (PrismaParams& p) -> float& { return p.masterGain; }).unit = "dB";
        b.add ("poly", "Polyphony", K::Int, 1, 32, 16, [] (PrismaParams& p) -> float& { return p.polyphony; });
        b.add ("mono", "Mono", K::Bool, 0, 1, 0, [] (PrismaParams& p) -> float& { return p.mono; });
        b.add ("legato", "Legato", K::Bool, 0, 1, 1, [] (PrismaParams& p) -> float& { return p.legato; });
        auto& glide = b.add ("glide", "Glide", K::Float, 0, 2, 0, [] (PrismaParams& p) -> float& { return p.glide; });
        glide.skewCentre = 0.2f;
        glide.unit = "s";
        b.add ("bend", "Bend Range", K::Int, 0, 24, 2, [] (PrismaParams& p) -> float& { return p.bendRange; });
        b.add ("vel_sens", "Velocity Sens", K::Float, 0, 1, 0.7f, [] (PrismaParams& p) -> float& { return p.velocitySens; });

        return b.list;
    }

    PrismaParams preset (const std::function<void (PrismaParams&)>& edit)
    {
        PrismaParams p;
        edit (p);
        return p;
    }

    void setMod (PrismaParams& p, int slot, ModSource s, ModDest d, float amount)
    {
        p.mod[(size_t) slot] = { (float) s, (float) d, amount };
    }
} // namespace

const std::vector<std::string>& syncDivisionNames()
{
    static const std::vector<std::string> n { "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/4.", "1/8." };
    return n;
}

double syncDivisionBeats (int index)
{
    static const double beats[] { 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75 };
    if (index < 0 || index >= (int) std::size (beats))
        return 1.0;
    return beats[index];
}

const std::vector<std::string>& modSourceNames()
{
    static const std::vector<std::string> n { "None", "Env 2", "Env 3", "LFO 1", "LFO 2", "Velocity", "Mod Wheel", "Aftertouch", "Key Track", "Random" };
    assert (n.size() == (size_t) ModSource::count);
    return n;
}

const std::vector<std::string>& modDestNames()
{
    static const std::vector<std::string> n { "None", "Osc A Pitch", "Osc A WT Pos", "Osc A Level", "Osc B Pitch", "Osc B WT Pos",
                                              "Osc B Level", "Filter Cutoff", "Filter Reso", "Filter Drive", "Amp Level", "Pan",
                                              "LFO 1 Rate", "LFO 2 Rate", "Sub Level", "Noise Level" };
    assert (n.size() == (size_t) ModDest::count);
    return n;
}

const std::vector<ParamDescriptor>& parameterDescriptors()
{
    static const std::vector<ParamDescriptor> list = build();
    return list;
}

PrismaParams::PrismaParams()
{
    for (const auto& d : parameterDescriptors())
        d.ref (*this) = d.def;
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets {
        { "Init", PrismaParams() },

        { "Supersaw Lead", preset ([] (PrismaParams& p) {
              p.osc[0] = { 1, 0, 0.66f, 0, 0, 0, 0.8f, 0, 7, 38, 0.9f, 0.8f, 0, 1 };
              p.osc[1] = { 1, 0, 0.66f, 1, 0, 0, 0.35f, 0, 5, 22, 1.0f, 0.7f, 0, 1 };
              p.filterType = 1;
              p.cutoff = 5500;
              p.resonance = 0.2f;
              p.filterEnvAmount = 0.25f;
              p.env[0] = { 0.004f, 0.3f, 0.85f, 0.35f };
              p.env[1] = { 0.001f, 0.45f, 0.3f, 0.3f };
              p.lfo[0] = { 0, 5.5f, 0, 4, 1 };
              setMod (p, 0, ModSource::Velocity, ModDest::FilterCutoff, 0.15f);
              setMod (p, 1, ModSource::ModWheel, ModDest::FilterCutoff, 0.4f);
              p.glide = 0.03f;
          }) },

        { "Reese Bass", preset ([] (PrismaParams& p) {
              p.osc[0] = { 1, 0, 0.66f, -1, 0, -9, 0.7f, -0.2f, 2, 25, 0.6f, 1, 0, 1 };
              p.osc[1] = { 1, 0, 0.66f, -1, 0, 9, 0.7f, 0.2f, 2, 25, 0.6f, 1, 0, 1 };
              p.subOn = 1;
              p.subLevel = 0.55f;
              p.filterType = 1;
              p.cutoff = 850;
              p.resonance = 0.15f;
              p.drive = 0.45f;
              p.env[0] = { 0.002f, 0.3f, 1, 0.15f };
              p.lfo[0] = { 0, 0.25f, 0, 4, 0 };
              setMod (p, 0, ModSource::Lfo1, ModDest::FilterCutoff, 0.08f);
              p.mono = 1;
              p.glide = 0.04f;
          }) },

        { "Wobble Bass", preset ([] (PrismaParams& p) {
              p.osc[0] = { 1, 1, 0.3f, -1, 0, 0, 0.8f, 0, 3, 12, 0.5f, 0.8f, 0, 1 };
              p.osc[1] = { 1, 4, 0.2f, -1, 0, 7, 0.4f, 0, 1, 0, 0, 0, 0, 1 };
              p.subOn = 1;
              p.subLevel = 0.7f;
              p.filterType = 1;
              p.cutoff = 380;
              p.resonance = 0.45f;
              p.drive = 0.5f;
              p.env[0] = { 0.003f, 0.3f, 1, 0.12f };
              p.lfo[0] = { 0, 2, 1, 5, 1 }; // 1/8, synced to the tempo
              setMod (p, 0, ModSource::Lfo1, ModDest::FilterCutoff, 0.38f);
              setMod (p, 1, ModSource::Lfo1, ModDest::OscAPos, 0.35f);
              setMod (p, 2, ModSource::Lfo1, ModDest::OscBPos, 0.4f);
              p.mono = 1;
          }) },

        { "Glass Pad", preset ([] (PrismaParams& p) {
              p.osc[0] = { 1, 2, 0.3f, 0, 0, 0, 0.7f, 0, 6, 18, 1, 0.85f, 0, 1 };
              p.osc[1] = { 1, 5, 0.2f, 1, 0, 0, 0.3f, 0, 4, 12, 1, 0.8f, 0, 1 };
              p.filterType = 0;
              p.cutoff = 3200;
              p.resonance = 0.25f;
              p.env[0] = { 1.2f, 1.5f, 0.8f, 2.5f };
              p.lfo[0] = { 1, 0.15f, 0, 4, 0 };
              p.lfo[1] = { 0, 0.23f, 0, 4, 0 };
              setMod (p, 0, ModSource::Lfo1, ModDest::OscAPos, 0.25f);
              setMod (p, 1, ModSource::Lfo2, ModDest::FilterCutoff, 0.1f);
              setMod (p, 2, ModSource::Lfo2, ModDest::OscBPos, 0.2f);
              p.masterGain = -9;
          }) },

        { "Pluck", preset ([] (PrismaParams& p) {
              p.osc[0] = { 1, 0, 0.5f, 0, 0, 0, 0.8f, 0, 3, 10, 0.7f, 0.7f, 0, 0.3f };
              p.osc[1] = { 1, 5, 0.35f, 1, 0, 0, 0.3f, 0, 1, 0, 0, 0, 0, 0 };
              p.filterType = 1;
              p.cutoff = 350;
              p.resonance = 0.3f;
              p.filterEnvAmount = 0.55f;
              p.env[0] = { 0.001f, 0.6f, 0, 0.4f };
              p.env[1] = { 0.001f, 0.25f, 0, 0.2f };
              setMod (p, 0, ModSource::Velocity, ModDest::FilterCutoff, 0.2f);
          }) },

        { "Vox Sweep", preset ([] (PrismaParams& p) {
              p.osc[0] = { 1, 3, 0.5f, 0, 0, 0, 0.8f, 0, 3, 8, 0.7f, 0.7f, 0, 1 };
              p.filterType = 0;
              p.cutoff = 6000;
              p.env[0] = { 0.05f, 0.4f, 0.9f, 0.6f };
              p.lfo[0] = { 1, 0.25f, 0, 4, 1 };
              setMod (p, 0, ModSource::Lfo1, ModDest::OscAPos, 0.5f);
              setMod (p, 1, ModSource::ModWheel, ModDest::OscAPos, 0.5f);
          }) },
    };
    return presets;
}

} // namespace daw::synth
