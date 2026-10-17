#include "daw/gen/Patches.h"

#include "Random.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace daw::gen
{
namespace
{
using synth::ModDest;
using synth::ModSource;
using synth::OscParams;
using synth::PrismaParams;

// Prisma's factory wavetables (core/src/Wavetable.cpp) and the positions of the shapes in "Basic Shapes".
namespace table
{
    constexpr float basic = 0, harmonicSweep = 1, pwm = 2, formant = 3, syncSaw = 4, wavefold = 5;
    constexpr float sawPos = 0.66f, squarePos = 1.0f;
} // namespace table

// Filter types and LFO shapes by their index in the descriptor choices.
constexpr float kLp12 = 0, kLp24 = 1, kHp12 = 2, kBp12 = 4, kNotch = 5;
constexpr float kSine = 0, kTriangle = 1, kSawUp = 2, kSawDown = 3, kSampleHold = 5;
constexpr float kDiv1_8 = 5, kDiv1_16 = 6, kDiv1_8T = 9;

// Draws parameter values from the archetype's ranges. Wildness widens every range: linear ranges by up to half their
// width on each side, time and frequency ranges by up to 1.5 octaves (they are perceived logarithmically).
class Designer
{
public:
    Designer (uint32_t seed, SoundKind kind, float wildness)
        : rng (seed, 0x70617400u + (uint32_t) kind), wild (std::clamp (wildness, 0.0f, 1.0f))
    {
    }

    float lin (float lo, float hi)
    {
        const float widen = 0.5f * wild * (hi - lo);
        return (float) rng.range (lo - widen, hi + widen);
    }
    float geo (float lo, float hi)
    {
        const double factor = std::exp2 (1.5 * wild);
        const double a = std::log ((double) lo / factor), b = std::log ((double) hi * factor);
        return (float) std::exp (rng.range (a, b));
    }
    float pick (std::initializer_list<float> options)
    {
        const int i = rng.integer (0, (int) options.size() - 1);
        return *(options.begin() + i);
    }
    // An index in [0, count), not widened: for tables, shapes and list entries.
    int index (int count) { return rng.integer (0, count - 1); }
    float whole (int lo, int hi) { return (float) rng.integer (lo, hi + (int) std::lround (wild * (hi - lo + 1))); }
    bool chance (double p) { return rng.chance (p); }
    double uniform() { return rng.uniform(); }
    float wildness() const { return wild; }

private:
    Rng rng;
    float wild;
};

void setOsc (OscParams& o, float tableIndex, float pos, float octave, float level, float unison, float detune, float spread)
{
    o.on = 1;
    o.table = tableIndex;
    o.wtPos = pos;
    o.octave = octave;
    o.semi = 0;
    o.fine = 0;
    o.level = level;
    o.pan = 0;
    o.unison = unison;
    o.detune = detune;
    o.spread = spread;
    o.blend = 0.75f;
    o.phase = 0;
    o.randPhase = 1;
}

// Puts a routing into the first free slot of the modulation matrix (no-op when it is full).
void route (PrismaParams& p, ModSource source, ModDest dest, float amount)
{
    for (auto& slot : p.mod)
        if (slot.source < 0.5f || slot.dest < 0.5f)
        {
            slot = { (float) source, (float) dest, amount };
            return;
        }
}

void envelope (synth::EnvParams& e, float a, float d, float s, float r)
{
    e = { a, d, s, r };
}

//==============================================================================
void designBass (PrismaParams& p, Designer& x)
{
    auto& a = p.osc[0];
    const float t = x.pick ({ table::basic, table::basic, table::pwm, table::syncSaw, table::wavefold });
    const float pos = t == table::basic ? x.lin (0.6f, 0.72f) : (t == table::wavefold ? x.lin (0.1f, 0.4f) : x.lin (0.2f, 0.6f));
    setOsc (a, t, pos, -1, x.lin (0.75f, 0.85f), x.whole (1, 2), x.lin (5, 18), x.lin (0.3f, 0.6f));
    if (x.chance (0.6))
    {
        setOsc (p.osc[1], table::basic, x.pick ({ table::sawPos, table::squarePos }), -1, x.lin (0.3f, 0.5f), 1, 0, 0);
        p.osc[1].fine = x.chance (0.5) ? x.lin (-9, -4) : x.lin (4, 9);
    }
    p.subOn = 1;
    p.subLevel = x.lin (0.45f, 0.7f);
    p.subOctave = 0;

    p.filterType = kLp24;
    p.cutoff = x.geo (250, 900);
    p.resonance = x.lin (0.1f, 0.35f);
    p.drive = x.lin (0.2f, 0.6f);
    p.keyTrack = x.lin (0.2f, 0.5f);
    p.filterEnvAmount = x.lin (0.2f, 0.45f);
    envelope (p.env[1], 0.001f, x.geo (0.12f, 0.4f), x.lin (0.0f, 0.2f), 0.15f);
    envelope (p.env[0], x.geo (0.001f, 0.005f), 0.3f, x.lin (0.85f, 1.0f), x.geo (0.06f, 0.18f));

    p.mono = 1;
    p.legato = 1;
    p.glide = x.chance (0.5) ? x.geo (0.02f, 0.06f) : 0.0f;
    route (p, ModSource::Velocity, ModDest::FilterCutoff, x.lin (0.1f, 0.2f));
    p.masterGain = x.lin (-9, -7);
}

void designSub (PrismaParams& p, Designer& x)
{
    auto& a = p.osc[0];
    setOsc (a, table::basic, x.lin (0.0f, 0.12f), -1, 0.85f, 1, 0, 0);
    a.randPhase = 0; // every note starts on the same phase, so the low end hits the same way each time
    p.osc[1].on = 0;
    p.subOn = x.chance (0.5) ? 1.0f : 0.0f;
    p.subLevel = x.lin (0.25f, 0.45f);

    p.filterType = kLp24;
    p.cutoff = x.geo (150, 450);
    p.resonance = x.lin (0.0f, 0.1f);
    p.drive = x.lin (0.05f, 0.35f); // a few harmonics so the sub is heard on small speakers
    envelope (p.env[0], x.geo (0.001f, 0.006f), 0.5f, 1.0f, x.geo (0.08f, 0.3f));

    p.mono = 1;
    p.legato = 1;
    p.glide = x.geo (0.04f, 0.12f); // 808 slides
    p.bendRange = 12;
    p.masterGain = x.lin (-8, -6);
}

void designLead (PrismaParams& p, Designer& x)
{
    const float t = x.pick ({ table::basic, table::syncSaw, table::harmonicSweep });
    const float pos = t == table::basic ? x.lin (0.6f, 0.7f) : (t == table::syncSaw ? x.lin (0.2f, 0.7f) : x.lin (0.6f, 1.0f));
    setOsc (p.osc[0], t, pos, 0, 0.8f, x.whole (5, 9), x.lin (18, 40), x.lin (0.7f, 1.0f));
    p.osc[0].blend = x.lin (0.6f, 0.85f);

    auto& b = p.osc[1];
    const float tb = x.pick ({ table::basic, table::pwm });
    setOsc (b, tb, tb == table::basic ? x.pick ({ table::sawPos, table::squarePos }) : x.lin (0.3f, 0.7f), x.pick ({ 0, 1 }),
            x.lin (0.3f, 0.5f), x.whole (3, 5), x.lin (10, 25), 0.9f);
    b.semi = x.chance (0.25) ? 7.0f : 0.0f;
    b.fine = x.lin (-8, 8);

    p.filterType = kLp24;
    p.cutoff = x.geo (2500, 8000);
    p.resonance = x.lin (0.12f, 0.35f);
    p.drive = x.lin (0.0f, 0.25f);
    p.filterEnvAmount = x.lin (0.1f, 0.35f);
    envelope (p.env[1], 0.001f, x.geo (0.2f, 0.6f), x.lin (0.2f, 0.5f), 0.3f);
    envelope (p.env[0], x.geo (0.002f, 0.02f), 0.3f, x.lin (0.75f, 0.95f), x.geo (0.15f, 0.5f));

    p.mono = x.chance (0.5) ? 1.0f : 0.0f;
    p.legato = 1;
    p.glide = x.geo (0.02f, 0.08f);
    p.lfo[0] = { kSine, x.lin (4.5f, 6.5f), 0, 4, 1 };
    route (p, ModSource::ModWheel, ModDest::FilterCutoff, x.lin (0.3f, 0.5f));
    route (p, ModSource::Velocity, ModDest::FilterCutoff, x.lin (0.1f, 0.2f));
    if (x.chance (0.6))
        route (p, ModSource::Lfo1, ModDest::OscAPitch, x.lin (0.0015f, 0.004f)); // a light vibrato
    p.masterGain = x.lin (-12, -10);
}

void designPad (PrismaParams& p, Designer& x)
{
    const float t = x.pick ({ table::harmonicSweep, table::formant, table::basic, table::pwm });
    const float pos = t == table::basic ? x.lin (0.3f, 0.7f) : x.lin (0.15f, 0.8f);
    setOsc (p.osc[0], t, pos, 0, 0.7f, x.whole (4, 8), x.lin (12, 30), x.lin (0.85f, 1.0f));
    p.osc[0].blend = x.lin (0.7f, 0.9f);
    setOsc (p.osc[1], x.pick ({ table::basic, table::harmonicSweep, table::pwm, table::formant }), x.lin (0.1f, 0.8f),
            x.pick ({ 0, 1 }), x.lin (0.25f, 0.45f), x.whole (3, 6), x.lin (8, 22), 1.0f);
    p.noiseLevel = x.lin (0.0f, 0.04f);

    p.filterType = x.pick ({ kLp12, kLp24 });
    p.cutoff = x.geo (1200, 4500);
    p.resonance = x.lin (0.1f, 0.3f);
    p.keyTrack = 0.3f;
    p.filterEnvAmount = x.lin (0.0f, 0.2f);
    envelope (p.env[1], x.geo (0.5f, 2.5f), 2.0f, 0.5f, 2.0f);
    envelope (p.env[0], x.geo (0.3f, 2.0f), x.geo (1.0f, 2.0f), x.lin (0.7f, 0.9f), x.geo (1.5f, 4.0f));

    p.lfo[0] = { x.pick ({ kSine, kTriangle }), x.geo (0.08f, 0.35f), 0, 4, 0 };
    p.lfo[1] = { kSine, x.geo (0.1f, 0.5f), 0, 4, 0 };
    route (p, ModSource::Lfo1, ModDest::FilterCutoff, x.lin (0.04f, 0.12f));
    route (p, ModSource::Lfo1, ModDest::OscAPos, x.lin (0.1f, 0.3f));
    route (p, ModSource::Lfo2, ModDest::OscBPos, x.lin (0.1f, 0.25f));
    if (x.chance (0.5))
        route (p, ModSource::Lfo2, ModDest::Pan, x.lin (0.1f, 0.3f));
    p.polyphony = 16;
    p.masterGain = x.lin (-15, -13);
}

void designPluck (PrismaParams& p, Designer& x)
{
    const float t = x.pick ({ table::basic, table::basic, table::pwm, table::harmonicSweep });
    const float pos = t == table::basic ? x.lin (0.55f, 1.0f) : x.lin (0.2f, 0.9f);
    setOsc (p.osc[0], t, pos, 0, 0.8f, x.whole (1, 3), x.lin (6, 15), 0.7f);
    p.osc[0].randPhase = x.lin (0.0f, 0.3f); // a consistent attack transient
    if (x.chance (0.7))
    {
        setOsc (p.osc[1], table::basic, x.lin (0.0f, 0.35f), 1, x.lin (0.2f, 0.35f), 1, 0, 0);
        p.osc[1].randPhase = 0;
    }

    p.filterType = kLp24;
    p.cutoff = x.geo (250, 700);
    p.resonance = x.lin (0.2f, 0.4f);
    p.keyTrack = 0.5f;
    p.filterEnvAmount = x.lin (0.45f, 0.7f);
    envelope (p.env[1], 0.0005f, x.geo (0.12f, 0.35f), 0.0f, 0.15f);
    envelope (p.env[0], x.geo (0.0005f, 0.002f), x.geo (0.3f, 0.8f), 0.0f, x.geo (0.2f, 0.5f));
    route (p, ModSource::Velocity, ModDest::FilterCutoff, x.lin (0.1f, 0.25f));
    p.masterGain = x.lin (-11, -9);
}

void designKeys (PrismaParams& p, Designer& x)
{
    const float t = x.pick ({ table::basic, table::harmonicSweep });
    setOsc (p.osc[0], t, t == table::basic ? x.lin (0.15f, 0.45f) : x.lin (0.05f, 0.3f), 0, 0.8f, x.whole (1, 2), x.lin (4, 10), 0.5f);
    p.osc[0].randPhase = 0;
    setOsc (p.osc[1], table::basic, x.lin (0.0f, 0.2f), x.pick ({ 1, 2 }), x.lin (0.12f, 0.25f), 1, 0, 0); // the tine
    p.osc[1].randPhase = 0;

    p.filterType = kLp12;
    p.cutoff = x.geo (1800, 5000);
    p.resonance = x.lin (0.05f, 0.2f);
    p.keyTrack = 0.5f;
    p.filterEnvAmount = x.lin (0.1f, 0.25f);
    envelope (p.env[1], 0.001f, x.geo (0.4f, 1.0f), x.lin (0.1f, 0.3f), 0.4f);
    envelope (p.env[0], x.geo (0.001f, 0.004f), x.geo (1.0f, 2.5f), x.lin (0.25f, 0.5f), x.geo (0.25f, 0.6f));

    p.velocitySens = x.lin (0.8f, 0.95f);
    route (p, ModSource::Velocity, ModDest::FilterCutoff, x.lin (0.15f, 0.3f));
    p.lfo[0] = { kSine, x.lin (3.5f, 6.0f), 0, 4, 0 };
    if (x.chance (0.5))
        route (p, ModSource::Lfo1, ModDest::AmpLevel, x.lin (0.03f, 0.08f)); // tremolo
    else
        route (p, ModSource::Lfo1, ModDest::Pan, x.lin (0.1f, 0.3f)); // auto-pan
    p.masterGain = x.lin (-12, -10);
}

void designBell (PrismaParams& p, Designer& x)
{
    const float t = x.pick ({ table::basic, table::harmonicSweep });
    setOsc (p.osc[0], t, t == table::basic ? x.lin (0.0f, 0.1f) : x.lin (0.0f, 0.2f), 0, 0.7f, 1, 0, 0);
    p.osc[0].randPhase = 0;

    // Bells are inharmonic: the second oscillator sits on a ratio between partials (about 2.7, 4 or 3).
    auto& b = p.osc[1];
    setOsc (b, x.pick ({ table::basic, table::wavefold }), x.lin (0.0f, 0.3f), 1, x.lin (0.3f, 0.55f), 1, 0, 0);
    b.randPhase = 0;
    const int ratio = (int) x.pick ({ 0, 1, 2 });
    if (ratio == 0)
    {
        b.semi = 5;
        b.fine = x.lin (30, 40);
    }
    else if (ratio == 1)
    {
        b.octave = 2;
        b.fine = x.lin (-10, 10);
    }
    else
    {
        b.semi = 7;
        b.fine = x.lin (0, 12);
    }

    p.filterType = kLp12;
    p.cutoff = x.geo (5000, 12000);
    p.resonance = x.lin (0.0f, 0.15f);
    envelope (p.env[0], x.geo (0.0005f, 0.002f), x.geo (1.5f, 3.5f), 0.0f, x.geo (1.5f, 3.0f));
    envelope (p.env[2], 0.0005f, x.geo (0.15f, 0.5f), 0.0f, 0.3f);
    route (p, ModSource::Env3, ModDest::OscBLevel, x.lin (0.2f, 0.4f)); // a brighter strike
    p.lfo[0] = { kSine, x.geo (0.2f, 0.5f), 0, 4, 0 };
    route (p, ModSource::Lfo1, ModDest::Pan, x.lin (0.15f, 0.3f));
    p.polyphony = 16;
    p.masterGain = x.lin (-11, -9);
}

void designFx (PrismaParams& p, Designer& x)
{
    setOsc (p.osc[0], x.pick ({ table::formant, table::syncSaw, table::wavefold, table::harmonicSweep }), x.lin (0.0f, 1.0f),
            x.pick ({ -1, 0, 1 }), 0.7f, x.whole (1, 6), x.lin (10, 60), 0.9f);
    if (x.chance (0.6))
    {
        setOsc (p.osc[1], (float) x.index (6), x.lin (0.0f, 1.0f), 0, x.lin (0.3f, 0.5f), 1, 0, 0);
        p.osc[1].semi = (float) (int) std::lround (x.lin (-12, 12));
        p.osc[1].fine = x.lin (-50, 50);
    }
    p.noiseLevel = x.lin (0.05f, 0.35f);

    p.filterType = x.pick ({ kLp12, kHp12, kBp12, kNotch });
    p.cutoff = x.geo (300, 4000);
    p.resonance = x.lin (0.35f, 0.7f);
    p.drive = x.lin (0.0f, 0.5f);
    envelope (p.env[0], x.geo (0.02f, 1.0f), x.geo (0.5f, 2.0f), x.lin (0.5f, 1.0f), x.geo (1.0f, 4.0f));
    envelope (p.env[2], x.geo (0.2f, 2.0f), x.geo (0.5f, 3.0f), 0.0f, 1.0f);

    p.lfo[0] = { x.pick ({ kSawUp, kSawDown, kSampleHold, kSine }), x.geo (0.3f, 8.0f), x.chance (0.4) ? 1.0f : 0.0f,
                 (float) x.whole (4, 10), 1 };
    p.lfo[1] = { kSine, x.geo (0.05f, 0.5f), 0, 4, 0 };
    route (p, ModSource::Lfo1, ModDest::FilterCutoff, x.lin (0.15f, 0.4f));
    route (p, ModSource::Lfo1, ModDest::OscAPos, x.lin (0.2f, 0.5f));
    route (p, ModSource::Lfo2, ModDest::Pan, x.lin (0.2f, 0.6f));
    route (p, ModSource::Env3, ModDest::OscAPitch, x.lin (-0.3f, 0.3f)); // a rise or a drop of up to 7 semitones
    route (p, ModSource::Env3, ModDest::FilterCutoff, x.lin (0.2f, 0.4f));
    p.masterGain = x.lin (-16, -14);
}

void designArp (PrismaParams& p, Designer& x)
{
    const float t = x.pick ({ table::basic, table::pwm });
    setOsc (p.osc[0], t, t == table::basic ? x.pick ({ table::sawPos, table::squarePos }) : x.lin (0.2f, 0.5f), 0, 0.8f,
            x.whole (2, 4), x.lin (8, 20), 0.6f);
    if (x.chance (0.5))
    {
        setOsc (p.osc[1], table::basic, table::sawPos, 1, 0.25f, 1, 0, 0);
        p.osc[1].fine = x.lin (-6, 6);
    }

    p.filterType = kLp24;
    p.cutoff = x.geo (700, 2000);
    p.resonance = x.lin (0.15f, 0.35f);
    p.keyTrack = 0.4f;
    p.filterEnvAmount = x.lin (0.3f, 0.5f);
    envelope (p.env[1], 0.0005f, x.geo (0.08f, 0.2f), x.lin (0.05f, 0.15f), 0.1f);
    envelope (p.env[0], x.geo (0.0005f, 0.002f), x.geo (0.15f, 0.35f), x.lin (0.1f, 0.3f), x.geo (0.1f, 0.25f));

    p.lfo[0] = { x.pick ({ kSawDown, kTriangle }), 1, 1, x.pick ({ kDiv1_16, kDiv1_8, kDiv1_8T }), 1 };
    route (p, ModSource::Lfo1, ModDest::FilterCutoff, x.lin (0.05f, 0.15f));
    route (p, ModSource::Velocity, ModDest::FilterCutoff, 0.15f);
    p.polyphony = 8;
    p.masterGain = x.lin (-11, -9);
}

// Wildness also changes the character, not only the amounts: other tables and extra modulation.
void mutate (PrismaParams& p, Designer& x, SoundKind kind)
{
    const float w = x.wildness();
    if (w <= 0.0f)
        return;
    if (x.chance (0.5 * w))
    {
        p.osc[0].table = (float) x.index (6);
        p.osc[0].wtPos = (float) x.uniform();
    }
    if (p.osc[1].on > 0.5f && x.chance (0.4 * w))
        p.osc[1].table = (float) x.index (6);
    if (kind != SoundKind::sub && kind != SoundKind::bass && p.osc[1].on > 0.5f && x.chance (0.3 * w))
        p.osc[1].semi = x.pick ({ 7, 12, -12, 5 });

    static const ModSource sources[] { ModSource::Lfo1, ModSource::Lfo2, ModSource::Env3, ModSource::Random, ModSource::Velocity,
                                       ModSource::KeyTrack };
    static const ModDest dests[] { ModDest::OscAPos, ModDest::OscBPos, ModDest::FilterCutoff, ModDest::Pan, ModDest::OscBLevel,
                                   ModDest::FilterDrive };
    for (int i = 0; i < 2; ++i)
        if (x.chance (0.6 * w))
        {
            const auto s = sources[x.index (6)];
            const auto d = dests[x.index (6)];
            const float amount = (float) (0.05 + 0.3 * x.uniform()) * w * (x.chance (0.5) ? 1.0f : -1.0f);
            route (p, s, d, amount);
        }
    if (x.chance (0.3 * w))
        p.lfo[1] = { (float) x.index (6), x.geo (0.05f, 8.0f), 0, 4, p.lfo[1].retrig };
}

float sanitise (const synth::ParamDescriptor& d, float v)
{
    if (! std::isfinite (v))
        v = d.def;
    v = std::clamp (v, d.min, d.max);
    if (d.kind != synth::ParamDescriptor::Kind::Float)
        v = std::round (v);
    return v;
}

} // namespace

const std::vector<SoundKind>& allSoundKinds()
{
    static const std::vector<SoundKind> kinds { SoundKind::bass, SoundKind::sub,  SoundKind::lead, SoundKind::pad, SoundKind::pluck,
                                                SoundKind::keys, SoundKind::bell, SoundKind::fx,   SoundKind::arp };
    return kinds;
}

const char* soundKindKey (SoundKind kind)
{
    switch (kind)
    {
        case SoundKind::bass: return "bass";
        case SoundKind::sub: return "sub";
        case SoundKind::lead: return "lead";
        case SoundKind::pad: return "pad";
        case SoundKind::pluck: return "pluck";
        case SoundKind::keys: return "keys";
        case SoundKind::bell: return "bell";
        case SoundKind::fx: return "fx";
        case SoundKind::arp: return "arp";
    }
    return "";
}

std::vector<float> generatePatch (SoundKind kind, uint32_t seed, float wildness)
{
    Designer x (seed, kind, wildness);
    PrismaParams p; // starts from the descriptor defaults, so parameters an archetype ignores stay sensible
    p.osc[1].on = 0;
    switch (kind)
    {
        case SoundKind::bass: designBass (p, x); break;
        case SoundKind::sub: designSub (p, x); break;
        case SoundKind::lead: designLead (p, x); break;
        case SoundKind::pad: designPad (p, x); break;
        case SoundKind::pluck: designPluck (p, x); break;
        case SoundKind::keys: designKeys (p, x); break;
        case SoundKind::bell: designBell (p, x); break;
        case SoundKind::fx: designFx (p, x); break;
        case SoundKind::arp: designArp (p, x); break;
    }
    mutate (p, x, kind);
    return valuesFromParams (p);
}

std::vector<float> valuesFromParams (const synth::PrismaParams& params)
{
    const auto& descriptors = synth::parameterDescriptors();
    std::vector<float> values;
    values.reserve (descriptors.size());
    for (const auto& d : descriptors)
        values.push_back (sanitise (d, d.get (params)));
    return values;
}

synth::PrismaParams paramsFromValues (const std::vector<float>& values)
{
    PrismaParams p;
    const auto& descriptors = synth::parameterDescriptors();
    for (size_t i = 0; i < descriptors.size() && i < values.size(); ++i)
        descriptors[i].ref (p) = sanitise (descriptors[i], values[i]);
    return p;
}

std::string patchName (SoundKind kind, uint32_t seed)
{
    static const char* const adjectives[] {
        "Velvet", "Neon",   "Hollow", "Golden", "Frozen", "Silver", "Dusty",  "Electric", "Liquid", "Crimson", "Midnight",
        "Amber",  "Glass",  "Faded",  "Lunar",  "Rusty",  "Static", "Gentle", "Molten",   "Distant", "Bright", "Shadow",
        "Cosmic", "Tidal",  "Silent", "Wild",   "Prism",  "Opal",   "Smoky",  "Polar",    "Copper",  "Violet",
    };
    static const char* const bassNouns[] { "Bite", "Growl", "Rumble", "Engine", "Pulse", "Grit", "Torque", "Stomp", "Weight", "Snarl" };
    static const char* const subNouns[] { "Depth", "Floor", "Quake", "Boom", "Undertow", "Abyss", "Tremor", "Core", "Thump", "Gravity" };
    static const char* const leadNouns[] { "Blade", "Laser", "Signal", "Siren", "Comet", "Flare", "Arrow", "Voice", "Beam", "Spark" };
    static const char* const padNouns[] { "Drift", "Haze", "Horizon", "Cloud", "Aurora", "Veil", "Tide", "Bloom", "Mist", "Dream" };
    static const char* const pluckNouns[] { "Drop", "Pebble", "Pick", "Ripple", "Flick", "Dew", "Snap", "Bead", "Spark", "Pop" };
    static const char* const keysNouns[] { "Parlour", "Lounge", "Keys", "Tines", "Room", "Ballad", "Hammer", "Study", "Salon", "Glow" };
    static const char* const bellNouns[] { "Chime", "Bell", "Temple", "Crystal", "Halo", "Carillon", "Toll", "Gong", "Star", "Ring" };
    static const char* const fxNouns[] { "Rift", "Portal", "Static", "Storm", "Warp", "Glitch", "Vortex", "Transmission", "Swarm", "Void" };
    static const char* const arpNouns[] { "Circuit", "Cascade", "Runner", "Spiral", "Sequence", "Motion", "Stairs", "Ticker", "Orbit", "Loop" };

    const char* const* nouns = bassNouns;
    switch (kind)
    {
        case SoundKind::bass: nouns = bassNouns; break;
        case SoundKind::sub: nouns = subNouns; break;
        case SoundKind::lead: nouns = leadNouns; break;
        case SoundKind::pad: nouns = padNouns; break;
        case SoundKind::pluck: nouns = pluckNouns; break;
        case SoundKind::keys: nouns = keysNouns; break;
        case SoundKind::bell: nouns = bellNouns; break;
        case SoundKind::fx: nouns = fxNouns; break;
        case SoundKind::arp: nouns = arpNouns; break;
    }
    Rng rng (seed, 0x6e616d00u + (uint32_t) kind);
    constexpr int numAdjectives = (int) (sizeof (adjectives) / sizeof (adjectives[0]));
    int a = rng.integer (0, numAdjectives - 1);
    const char* noun = nouns[rng.integer (0, 9)];
    if (std::string (adjectives[a]) == noun) // "Static Static"
        a = (a + 1) % numAdjectives;
    const char* adjective = adjectives[a];
    return std::string (adjective) + " " + noun;
}

} // namespace daw::gen
