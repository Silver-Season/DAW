#include "daw/gen/Patterns.h"

#include "Random.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace daw::gen
{
namespace
{
using engine::Note;
using music::Scale;

enum class Style
{
    house,
    hiphop,
    trap,
    techno,
    pop,
    ambient,
    dnb
};

constexpr double kEps = 1.0e-9;

// Stream salts, one per generator (see Rng).
constexpr uint32_t kSaltChords = 0x63686f72, kSaltBass = 0x62617373, kSaltMelody = 0x6d656c6f, kSaltArp = 0x61727067,
                   kSaltDrums = 0x6472756d;

Style styleOf (const std::string& s)
{
    if (s == "house")
        return Style::house;
    if (s == "hiphop")
        return Style::hiphop;
    if (s == "trap")
        return Style::trap;
    if (s == "techno")
        return Style::techno;
    if (s == "ambient")
        return Style::ambient;
    if (s == "dnb")
        return Style::dnb;
    return Style::pop;
}

// Mixed into every generator's stream, so one seed gives unrelated ideas in different styles.
uint32_t styleSalt (const GenContext& ctx)
{
    return (uint32_t) styleOf (ctx.style) * 0x1000193u;
}

double clamp01 (double v) { return std::clamp (v, 0.0, 1.0); }
int clampVelocity (double v) { return std::clamp ((int) std::lround (v), 1, 127); }
bool isWhole (double beat) { return std::abs (beat - std::round (beat)) < 1.0e-6; }

// The scale the harmony is built from. A chromatic (or empty) scale has no harmony of its own, so it borrows the major
// scale; every note of the major scale is still in the chromatic scale.
const Scale& harmonyScale (const GenContext& ctx)
{
    const Scale* s = ctx.scale;
    if (s == nullptr || music::notesPerOctave (*s) == 0 || music::notesPerOctave (*s) == 12)
        s = &music::allScales().front();
    return *s;
}

int rootOf (const GenContext& ctx) { return music::pitchClass (ctx.root, 0); }
double barBeats (const GenContext& ctx) { return std::clamp (ctx.beatsPerBar, 1.0, 16.0); }
int barCount (const GenContext& ctx) { return std::clamp (ctx.bars, 1, 256); }
double totalBeats (const GenContext& ctx) { return barBeats (ctx) * barCount (ctx); }

bool majorFamily (const Scale& s) { return s.pitches[4] && ! s.pitches[3]; }

// Pitch class (0..11, absolute) of a scale degree.
int degreePitchClass (const Scale& s, int root, int degree)
{
    return music::pitchClass (music::degreeToNote (degree, root, s, 60), 0);
}

// Progressions are written as degrees of a seven-note scale. Scales with another number of notes (pentatonics,
// blues) take the scale degree whose pitch lies nearest to the same degree of the major or minor scale.
int mapDegree (const Scale& s, int hepta)
{
    const int count = music::notesPerOctave (s);
    if (count == 7)
        return hepta;
    static const int majorSteps[7] { 0, 2, 4, 5, 7, 9, 11 }, minorSteps[7] { 0, 2, 3, 5, 7, 8, 10 };
    const int target = (majorFamily (s) ? majorSteps : minorSteps)[((hepta % 7) + 7) % 7];
    int best = 0, bestDistance = 99;
    for (int d = 0; d < count; ++d)
    {
        const int pc = music::degreeToNote (d, 0, s, 0);
        const int distance = std::abs (pc - target);
        if (distance < bestDistance) // ties keep the lower degree
        {
            best = d;
            bestDistance = distance;
        }
    }
    return best;
}

// Pitch classes of the chord on a scale degree, root first. Thirds are stacked from the notes the scale has, so a
// pentatonic or blues scale gives sus chords and inversions instead of notes outside it.
// extra: 0 = triad, 1 = seventh, 2 = added ninth.
std::vector<int> chordPitchClasses (const Scale& s, int root, int degree, int extra)
{
    const int r = degreePitchClass (s, root, degree);
    auto has = [&] (int interval) { return s.pitches[(size_t) music::pitchClass (r + interval, root)]; };
    auto firstOf = [&] (std::initializer_list<int> candidates) {
        for (int i : candidates)
            if (has (i))
                return i;
        return -1;
    };
    std::vector<int> intervals { 0 };
    auto add = [&] (int i) {
        if (i >= 0 && std::find (intervals.begin(), intervals.end(), i) == intervals.end())
            intervals.push_back (i);
    };
    const int third = firstOf ({ 3, 4 });
    add (third >= 0 ? third : firstOf ({ 5, 2 }));
    add (firstOf ({ 7, 6, 8 }));
    if (extra == 1)
        add (firstOf ({ 10, 11, 9 }));
    else if (extra == 2)
        add (firstOf ({ 2, 5 }));
    for (int i : { 10, 9, 2, 5, 11, 1 })
        if (intervals.size() < 3)
            add (has (i) ? i : -1);

    std::vector<int> pcs;
    for (int i : intervals)
        pcs.push_back ((r + i) % 12);
    return pcs;
}

std::vector<int> chordPitchClasses (const Scale& s, int root, int degree)
{
    return chordPitchClasses (s, root, degree, 0);
}

using Progression = std::vector<int>;

const std::vector<Progression>& progressionsFor (Style st, const Scale& s)
{
    // Modes with a chord that defines their colour get their signature progressions.
    const char* key = s.key;
    if (std::strcmp (key, "dorian") == 0)
    {
        static const std::vector<Progression> p { { 0, 3, 0, 3 }, { 0, 3, 6, 0 }, { 0, 1, 3, 0 } };
        return p;
    }
    if (std::strcmp (key, "mixolydian") == 0)
    {
        static const std::vector<Progression> p { { 0, 6, 3, 0 }, { 0, 6, 0, 3 }, { 0, 4, 6, 3 } };
        return p;
    }
    if (std::strcmp (key, "lydian") == 0)
    {
        static const std::vector<Progression> p { { 0, 1, 0, 1 }, { 0, 1, 4, 0 }, { 0, 1, 6, 0 } };
        return p;
    }
    if (std::strcmp (key, "phrygian") == 0)
    {
        static const std::vector<Progression> p { { 0, 1, 0, 6 }, { 0, 1, 6, 0 }, { 0, 6, 5, 1 } };
        return p;
    }

    if (majorFamily (s))
    {
        static const std::vector<Progression> pop { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 3, 0, 4, 5 }, { 5, 3, 0, 4 }, { 0, 3, 4, 3 } };
        static const std::vector<Progression> house { { 3, 4, 2, 5 }, { 0, 5, 3, 4 }, { 5, 3, 0, 4 }, { 1, 4, 0, 5 } };
        static const std::vector<Progression> hiphop { { 1, 4, 0, 5 }, { 3, 2, 1, 0 }, { 0, 5, 1, 4 } };
        static const std::vector<Progression> trap { { 0, 5, 3, 4 }, { 5, 3, 0, 4 }, { 0, 2, 5, 3 } };
        static const std::vector<Progression> techno { { 0, 0, 3, 3 }, { 0, 3, 0, 4 }, { 0, 5, 0, 5 } };
        static const std::vector<Progression> ambient { { 0, 3, 5, 3 }, { 0, 2, 3, 0 }, { 3, 0, 4, 5 }, { 0, 4, 3, 0 } };
        static const std::vector<Progression> dnb { { 5, 3, 0, 4 }, { 0, 4, 5, 3 }, { 3, 4, 5, 5 } };
        switch (st)
        {
            case Style::house: return house;
            case Style::hiphop: return hiphop;
            case Style::trap: return trap;
            case Style::techno: return techno;
            case Style::ambient: return ambient;
            case Style::dnb: return dnb;
            case Style::pop: break;
        }
        return pop;
    }

    static const std::vector<Progression> pop { { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 5, 6, 0, 0 } };
    static const std::vector<Progression> house { { 0, 6, 5, 6 }, { 0, 5, 2, 6 }, { 0, 3, 6, 2 }, { 0, 5, 6, 0 } };
    static const std::vector<Progression> hiphop { { 0, 3, 0, 5 }, { 0, 5, 3, 4 }, { 0, 6, 5, 6 }, { 0, 0, 3, 4 } };
    static const std::vector<Progression> trap { { 0, 5, 6, 0 }, { 0, 3, 5, 4 }, { 0, 5, 2, 6 }, { 0, 0, 5, 6 } };
    static const std::vector<Progression> techno { { 0, 0, 5, 6 }, { 0, 6, 0, 5 }, { 0, 0, 3, 0 } };
    static const std::vector<Progression> ambient { { 0, 5, 2, 6 }, { 0, 2, 5, 3 }, { 0, 3, 0, 5 } };
    static const std::vector<Progression> dnb { { 0, 5, 2, 6 }, { 0, 3, 5, 6 }, { 0, 6, 5, 4 } };
    switch (st)
    {
        case Style::house: return house;
        case Style::hiphop: return hiphop;
        case Style::trap: return trap;
        case Style::techno: return techno;
        case Style::ambient: return ambient;
        case Style::dnb: return dnb;
        case Style::pop: break;
    }
    return pop;
}

// A chord that can take the place of another one in a repeat of the progression (relative chords share two notes).
int substitute (int hepta, bool major)
{
    static const int majorSubs[7] { 5, 3, 0, 1, 2, 3, 4 };
    static const int minorSubs[7] { 5, 3, 0, 5, 6, 3, 4 };
    return (major ? majorSubs : minorSubs)[((hepta % 7) + 7) % 7];
}

// Places a chord in close position so that it moves as little as possible from the previous one.
std::vector<int> voiceChord (const std::vector<int>& pcs, const std::vector<int>& previous, Rng& rng)
{
    constexpr int kLow = 48, kHighestBass = 66, kTop = 76, kCentre = 62;
    std::vector<int> best;
    double bestCost = std::numeric_limits<double>::max();
    for (int bassNote = kLow; bassNote <= kHighestBass; ++bassNote)
    {
        const int bassPc = bassNote % 12;
        const auto it = std::find (pcs.begin(), pcs.end(), bassPc);
        if (it == pcs.end())
            continue;
        // Stack the other tones upwards in the order they follow the bass tone around the octave.
        std::vector<int> v { bassNote };
        for (size_t k = 1; k < pcs.size(); ++k)
        {
            const int pc = pcs[(size_t) ((it - pcs.begin()) + (std::ptrdiff_t) k) % pcs.size()];
            int n = v.back() + 1;
            while (n % 12 != pc)
                ++n;
            v.push_back (n);
        }
        if (v.back() > kTop)
            continue;
        std::sort (v.begin(), v.end());

        double mean = 0;
        for (int n : v)
            mean += n;
        mean /= (double) v.size();

        double cost = 0.3 * std::abs (mean - kCentre); // keeps a long progression from drifting up or down
        if (! previous.empty())
        {
            // Every voice of each chord looks for its nearest voice in the other: this works when one chord has
            // a seventh and the other does not.
            double move = 0;
            for (int n : v)
            {
                int d = 127;
                for (int p : previous)
                    d = std::min (d, std::abs (n - p));
                move += d;
            }
            for (int p : previous)
            {
                int d = 127;
                for (int n : v)
                    d = std::min (d, std::abs (n - p));
                move += d;
            }
            cost += move / (double) (v.size() + previous.size()) * 2.0;
        }
        else
        {
            cost += bassPc == pcs.front() ? 0.0 : 1.5; // the first chord prefers root position
        }
        cost += rng.uniform() * 0.25; // breaks ties differently for different seeds
        if (cost < bestCost)
        {
            bestCost = cost;
            best = v;
        }
    }
    if (best.empty()) // cannot happen with pcs from chordPitchClasses, but never return an empty chord
        for (int pc : pcs)
            best.push_back (60 + pc);
    return best;
}

// The degrees of the chords and how many chords fall into one bar.
std::vector<int> progressionDegrees (const GenContext& ctx, Rng& rng, std::vector<int>& extrasOut)
{
    const Style st = styleOf (ctx.style);
    const auto& s = harmonyScale (ctx);
    const double complexity = clamp01 (ctx.complexity);
    const auto& options = progressionsFor (st, s);
    const auto& prog = options[(size_t) rng.integer (0, (int) options.size() - 1)];

    const bool halfBars = barBeats (ctx) >= 2.0
                          && (ctx.bars == 1
                              || (complexity > 0.7 && (st == Style::pop || st == Style::house || st == Style::dnb) && rng.chance (0.5)));
    const int count = barCount (ctx) * (halfBars ? 2 : 1);

    const double seventhChance = complexity * (st == Style::hiphop || st == Style::house ? 0.9 : (st == Style::trap ? 0.35 : 0.55));
    const bool major = majorFamily (s);

    std::vector<int> degrees;
    extrasOut.clear();
    for (int i = 0; i < count; ++i)
    {
        int hepta = prog[(size_t) i % prog.size()];
        // Repeats of the progression vary a chord now and then, but never the first chord of a pass.
        const bool repeat = i >= (int) prog.size();
        if (repeat && i % (int) prog.size() != 0 && rng.chance (0.15 + 0.35 * complexity))
            hepta = substitute (hepta, major);
        degrees.push_back (mapDegree (s, hepta));

        int extra = 0;
        if (rng.chance (seventhChance))
            extra = st == Style::ambient ? 2 : 1;
        extrasOut.push_back (extra);
    }
    return degrees;
}

double spanLength (const GenContext& ctx, const std::vector<int>& degrees)
{
    return totalBeats (ctx) / (double) std::max<size_t> (1, degrees.size());
}

int degreeAt (const std::vector<int>& degrees, double span, double beat)
{
    if (degrees.empty())
        return 0;
    const auto i = (size_t) std::clamp ((int) std::floor (beat / span + kEps), 0, (int) degrees.size() - 1);
    return degrees[i];
}

// Keeps every note inside the clip and sorts by start, then pitch.
void finish (Notes& notes, double total)
{
    constexpr double kMinLength = 1.0 / 64.0;
    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [total] (const Note& n) { return n.start < 0.0 || n.start > total - kMinLength; }),
                 notes.end());
    for (auto& n : notes)
    {
        n.length = std::clamp (n.length, kMinLength, total - n.start);
        n.velocity = std::clamp (n.velocity, 1, 127);
    }
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
}

struct Hit
{
    double at, length;
};

// Where the chord sounds inside its span.
std::vector<Hit> chordRhythm (Style st, double span, double density, double complexity, Rng& rng)
{
    std::vector<Hit> hits;
    const Hit whole { 0.0, span - 0.05 };
    switch (st)
    {
        case Style::ambient:
            hits.push_back ({ 0.0, span }); // legato pads
            break;
        case Style::trap:
            hits.push_back (whole);
            break;
        case Style::techno:
            if (density < 0.5)
                hits.push_back (whole);
            else
                for (double b = 0.5; b < span; b += 1.0)
                    hits.push_back ({ b, 0.2 });
            break;
        case Style::house:
            if (density < 0.35)
                hits.push_back (whole);
            else if (rng.chance (0.5))
                for (double b = 0.5; b < span; b += 1.0)
                    hits.push_back ({ b, 0.3 });
            else
                for (double b : { 0.0, 0.75, 1.5, 2.5, 3.25 })
                    hits.push_back ({ b, 0.35 });
            break;
        case Style::hiphop:
        case Style::dnb:
            if (density < 0.6)
                hits.push_back (whole);
            else
            {
                hits.push_back ({ 0.0, 1.4 });
                hits.push_back ({ 1.5, span - 1.55 });
            }
            break;
        case Style::pop:
            if (density < 0.45)
                hits.push_back (whole);
            else if (complexity > 0.5)
                for (double b : { 0.0, 1.5, 3.0 }) // the "pushed" 3 + 3 + 2 eighths
                    hits.push_back ({ b, 1.4 });
            else
                for (double b = 0.0; b < span; b += 1.0)
                    hits.push_back ({ b, 0.9 });
            break;
    }
    std::vector<Hit> fitted;
    for (auto h : hits)
        if (h.at < span - 1.0 / 32.0)
            fitted.push_back ({ h.at, std::max (0.05, std::min (h.length, span - h.at - (st == Style::ambient ? 0.0 : 0.01))) });
    return fitted;
}

// The bass register: the chord root between C1 and B1.
int bassRoot (const Scale& s, int root, int degree)
{
    return 24 + degreePitchClass (s, root, degree);
}

int fifthAbove (const Scale& s, int root, int note)
{
    const int f = note + 7;
    return music::inScale (f, root, s) ? f : music::snapToScale (f, root, s, 0);
}

} // namespace

const std::vector<std::string>& styleNames()
{
    static const std::vector<std::string> names { "pop", "house", "techno", "hiphop", "trap", "dnb", "ambient" };
    return names;
}

//==============================================================================
Notes chords (const GenContext& ctx, std::vector<int>* romanDegreesOut)
{
    Rng rng (ctx.seed, kSaltChords + styleSalt (ctx));
    const auto& s = harmonyScale (ctx);
    const int root = rootOf (ctx);
    const Style st = styleOf (ctx.style);

    std::vector<int> extras;
    const auto degrees = progressionDegrees (ctx, rng, extras);
    const double span = spanLength (ctx, degrees);

    Notes out;
    std::vector<int> previous;
    for (size_t i = 0; i < degrees.size(); ++i)
    {
        const auto voicing = voiceChord (chordPitchClasses (s, root, degrees[i], extras[i]), previous, rng);
        previous = voicing;
        const double start = (double) i * span;
        const auto hits = chordRhythm (st, span, clamp01 (ctx.density), clamp01 (ctx.complexity), rng);
        for (size_t h = 0; h < hits.size(); ++h)
        {
            const double accent = h == 0 ? 8.0 : 0.0;
            for (int pitch : voicing)
                out.push_back ({ start + hits[h].at, hits[h].length, pitch,
                                 clampVelocity (76.0 + 12.0 * ctx.density + accent + rng.range (-4.0, 4.0)) });
        }
    }

    if (romanDegreesOut != nullptr)
        *romanDegreesOut = degrees;
    finish (out, totalBeats (ctx));
    return out;
}

//==============================================================================
Notes bass (const GenContext& ctx, const std::vector<int>& chordDegrees)
{
    Rng rng (ctx.seed, kSaltBass + styleSalt (ctx));
    const auto& s = harmonyScale (ctx);
    const int root = rootOf (ctx);
    const Style st = styleOf (ctx.style);
    const double total = totalBeats (ctx), bar = barBeats (ctx);
    const double density = clamp01 (ctx.density), complexity = clamp01 (ctx.complexity);
    const std::vector<int> degrees = chordDegrees.empty() ? std::vector<int> { 0 } : chordDegrees;
    const double span = total / (double) degrees.size();

    enum class Kind
    {
        chordRoot,
        octave,
        fifth,
        approach,
        slide
    };
    struct Step
    {
        double at, length;
        Kind kind;
        double probability;
    };

    std::vector<Step> pattern;
    bool offbeatStyle = false;
    switch (st)
    {
        case Style::house:
            offbeatStyle = true;
            pattern = { { 0.5, 0.4, Kind::chordRoot, 1 }, { 1.5, 0.4, Kind::chordRoot, 1 }, { 2.5, 0.4, Kind::chordRoot, 1 },
                        { 3.5, 0.4, Kind::octave, 0.5 + 0.5 * complexity }, { 0.75, 0.2, Kind::octave, 0.4 * complexity },
                        { 2.75, 0.2, Kind::fifth, 0.4 * complexity } };
            break;
        case Style::techno:
            offbeatStyle = true;
            for (double b = 0; b < 4.0; b += 1.0)
            {
                pattern.push_back ({ b + 0.25, 0.2, Kind::chordRoot, 0.5 });
                pattern.push_back ({ b + 0.5, 0.2, Kind::chordRoot, 1 });
                pattern.push_back ({ b + 0.75, 0.2, complexity > 0.6 ? Kind::octave : Kind::chordRoot, 0.6 });
            }
            break;
        case Style::hiphop:
            pattern = { { 0, 1.25, Kind::chordRoot, 1 }, { 1.75, 0.5, Kind::chordRoot, 0.7 }, { 2.5, 0.75, Kind::fifth, 0.6 },
                        { 3.25, 0.5, Kind::approach, 0.5 } };
            break;
        case Style::trap:
            pattern = { { 0, 2.0, Kind::chordRoot, 1 }, { 2.0, 1.0, Kind::slide, 0.6 + 0.3 * complexity },
                        { 2.75, 0.75, Kind::chordRoot, 0.6 }, { 3.5, 0.5, Kind::chordRoot, 0.4 } };
            break;
        case Style::dnb:
            pattern = { { 0, 1.5, Kind::chordRoot, 1 }, { 1.5, 0.5, Kind::chordRoot, 0.4 }, { 2.5, 1.0, Kind::chordRoot, 0.9 },
                        { 3.5, 0.5, Kind::fifth, 0.5 } };
            break;
        case Style::ambient:
            pattern = { { 0, bar, Kind::chordRoot, 1 }, { bar * 0.5, bar * 0.5, Kind::fifth, 0.3 * density } };
            break;
        case Style::pop:
            pattern = { { 0, 1.0, Kind::chordRoot, 1 }, { 1.5, 0.5, Kind::chordRoot, 0.6 }, { 2.0, 1.0, Kind::fifth, 0.8 },
                        { 3.0, 0.5, Kind::octave, 0.5 }, { 3.5, 0.5, Kind::approach, 0.6 * complexity } };
            break;
    }

    struct Placed
    {
        Note note;
        Kind kind;
    };
    std::vector<Placed> placed;
    for (int b = 0; b < barCount (ctx); ++b)
        for (const auto& step : pattern)
        {
            if (step.at >= bar - kEps)
                continue;
            const double p = std::min (1.0, step.probability * (0.4 + 1.2 * density));
            if (step.probability < 1.0 && ! rng.chance (p))
                continue;
            placed.push_back ({ { b * bar + step.at, step.length, 0, 0 }, step.kind });
        }
    std::sort (placed.begin(), placed.end(), [] (const Placed& a, const Placed& c) { return a.note.start < c.note.start; });

    // Every chord span has to begin with its root: on the downbeat, or (house, techno) on the first offbeat.
    for (size_t i = 0; i < degrees.size(); ++i)
    {
        const double spanStart = (double) i * span;
        const auto first = std::find_if (placed.begin(), placed.end(), [&] (const Placed& p) { return p.note.start >= spanStart - kEps; });
        const bool hasNoteInSpan = first != placed.end() && first->note.start < spanStart + span - kEps;
        const bool onDownbeat = hasNoteInSpan && std::abs (first->note.start - spanStart) < kEps;
        if ((! offbeatStyle && ! onDownbeat) || ! hasNoteInSpan)
        {
            const double at = offbeatStyle && span >= 1.0 ? spanStart + 0.5 : spanStart;
            placed.insert (first, { { at, std::min (1.0, span), 0, 0 }, Kind::chordRoot });
        }
    }
    std::sort (placed.begin(), placed.end(), [] (const Placed& a, const Placed& c) { return a.note.start < c.note.start; });

    Notes out;
    int lastSpan = -1;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        auto& p = placed[i];
        const int spanIndex = std::clamp ((int) std::floor (p.note.start / span + kEps), 0, (int) degrees.size() - 1);
        const int r = bassRoot (s, root, degrees[(size_t) spanIndex]);
        Kind kind = spanIndex != lastSpan ? Kind::chordRoot : p.kind;
        lastSpan = spanIndex;

        int pitch = r;
        switch (kind)
        {
            case Kind::chordRoot: break;
            case Kind::octave: pitch = r + 12; break;
            case Kind::fifth: pitch = fifthAbove (s, root, r); break;
            case Kind::slide: pitch = rng.chance (0.6) ? r + 12 : fifthAbove (s, root, r); break;
            case Kind::approach:
            {
                const int next = bassRoot (s, root, degrees[(size_t) std::min (spanIndex + 1, (int) degrees.size() - 1)]);
                const int below = music::snapToScale (next - 1, root, s, -1), above = music::snapToScale (next + 1, root, s, 1);
                pitch = rng.chance (0.5) ? below : above;
                if (pitch < 24)
                    pitch = above;
                break;
            }
        }
        p.note.pitch = pitch;

        double velocity = 100.0 + 8.0 * density;
        if (isWhole (p.note.start))
            velocity += 10.0;
        if (st == Style::trap || st == Style::ambient)
            velocity = 110.0;
        p.note.velocity = clampVelocity (velocity + rng.range (-5.0, 5.0));

        // The line is monophonic: a note ends before the next one starts. A trap slide is the exception: the
        // previous 808 note keeps sounding into it, so a mono synth with glide bends up to the slide note.
        if (i + 1 < placed.size())
        {
            const double nextStart = placed[i + 1].note.start;
            const bool nextIsSlide = placed[i + 1].kind == Kind::slide
                                     && (int) std::floor (nextStart / span + kEps) == spanIndex;
            const double end = nextIsSlide ? nextStart + 0.125 : nextStart - 0.02;
            p.note.length = std::min (p.note.length, end - p.note.start);
            if (nextIsSlide)
                p.note.length = end - p.note.start;
        }
        if (p.note.length > 0.01)
            out.push_back (p.note);
    }
    finish (out, total);
    return out;
}

//==============================================================================
Notes melody (const GenContext& ctx, const std::vector<int>& chordDegrees)
{
    Rng rng (ctx.seed, kSaltMelody + styleSalt (ctx));
    const auto& s = harmonyScale (ctx);
    const int root = rootOf (ctx);
    const Style st = styleOf (ctx.style);
    const double total = totalBeats (ctx), bar = barBeats (ctx);
    const double density = clamp01 (ctx.density), complexity = clamp01 (ctx.complexity);
    const std::vector<int> degrees = chordDegrees.empty() ? std::vector<int> { 0 } : chordDegrees;
    const double span = total / (double) degrees.size();
    constexpr int kLow = 60, kHigh = 79; // C4 .. G5

    const double grid = st == Style::ambient ? 1.0
                                             : ((st == Style::trap || st == Style::dnb || complexity > 0.6) ? 0.25 : 0.5);
    const double motifLength = (barCount (ctx) >= 4 && complexity > 0.6) ? 2.0 * bar : bar;

    auto makeRhythm = [&] {
        std::vector<double> onsets;
        for (double p = 0.0; p < motifLength - kEps; p += grid)
        {
            double probability = (st == Style::ambient ? 0.2 : 0.25) + 0.6 * density;
            if (isWhole (p))
                probability *= 1.25;
            else if (! isWhole (p * 2.0))
                probability *= 0.55 + 0.4 * complexity;
            if (p >= motifLength - 1.0) // breathe at the end of the motif
                probability *= 0.45;
            if (p < kEps || rng.chance (probability))
                onsets.push_back (p);
        }
        for (double p = 1.0; onsets.size() < 3 && p < motifLength; p += 1.0)
            if (std::find (onsets.begin(), onsets.end(), p) == onsets.end())
                onsets.push_back (p);
        std::sort (onsets.begin(), onsets.end());
        return onsets;
    };

    // Scale steps between consecutive notes: mostly steps, a leap now and then that is answered by a step back.
    auto makeContour = [&] (size_t count) {
        std::vector<int> steps;
        int previous = 0;
        for (size_t i = 0; i < count; ++i)
        {
            const bool risingHalf = i < count / 2;
            int step;
            if (std::abs (previous) >= 3)
                step = previous > 0 ? -1 : 1;
            else
            {
                const double r = rng.uniform();
                const double leap = 0.08 + 0.12 * complexity;
                int size = r < 0.12 ? 0 : (r < 0.6 ? 1 : (r < 1.0 - leap ? 2 : rng.integer (3, 4)));
                const bool up = rng.chance (risingHalf ? 0.65 : 0.35);
                step = up ? size : -size;
            }
            steps.push_back (step);
            previous = step;
        }
        return steps;
    };

    auto pitchOf = [&] (int index) { return music::degreeToNote (index, root, s, 60); };
    auto indexOf = [&] (int pitch) {
        const int count = music::notesPerOctave (s);
        for (int i = -2 * count; i <= 4 * count; ++i)
            if (pitchOf (i) >= pitch)
                return i;
        return 0;
    };
    auto chordTonesAt = [&] (double beat) {
        return chordPitchClasses (s, root, degreeAt (degrees, span, beat));
    };
    // The chord tone nearest to `pitch` inside the range; a tie follows the direction of the melody.
    auto nearestChordTone = [&] (int pitch, double beat, int direction) {
        const auto pcs = chordTonesAt (beat);
        int best = pitch, bestDistance = 99;
        for (int n = kLow; n <= kHigh; ++n)
        {
            if (std::find (pcs.begin(), pcs.end(), n % 12) == pcs.end())
                continue;
            const int d = std::abs (n - pitch);
            if (d < bestDistance || (d == bestDistance && (n - pitch) * direction > 0))
            {
                best = n;
                bestDistance = d;
            }
        }
        return best;
    };

    // The nearest chord tone that continues the melody's movement away from `from` (a repeated note only when the
    // contour asks for one).
    auto chordToneToward = [&] (int from, int candidate, double beat, int direction) {
        if (direction == 0 || candidate != from)
            return candidate;
        const auto pcs = chordTonesAt (beat);
        for (int n = from + (direction > 0 ? 1 : -1); n >= kLow && n <= kHigh; n += direction > 0 ? 1 : -1)
            if (std::find (pcs.begin(), pcs.end(), n % 12) != pcs.end())
                return n;
        return candidate;
    };

    const auto rhythmA = makeRhythm();
    const auto contourA = makeContour (rhythmA.size());
    const auto rhythmB = makeRhythm();
    const auto contourB = makeContour (rhythmB.size());

    const int instances = (int) std::ceil (total / motifLength - kEps);
    const int anchor = nearestChordTone (67, 0.0, 1);
    Notes out;
    int lastPitch = anchor;
    for (int k = 0; k < instances; ++k)
    {
        const bool lastInstance = k == instances - 1;
        const bool answer = (k % 4 == 3) || (lastInstance && instances >= 2 && k % 4 != 0);
        auto rhythm = answer ? rhythmB : rhythmA;
        auto contour = answer ? contourB : contourA;
        if (! answer && k % 2 == 1)
        {
            // A varied repeat: the same rhythm with its tail bent somewhere else, and with complexity a note more or less.
            for (size_t i = contour.size() >= 2 ? contour.size() - 2 : 0; i < contour.size(); ++i)
                contour[i] = rng.integer (-2, 2);
            if (rhythm.size() > 3 && rng.chance (complexity))
            {
                const auto drop = (size_t) rng.integer (1, (int) rhythm.size() - 1);
                rhythm.erase (rhythm.begin() + (std::ptrdiff_t) drop);
                contour.erase (contour.begin() + (std::ptrdiff_t) drop);
            }
        }

        const double offset = k * motifLength;
        // Each statement starts on the chord tone nearest to where the motif first started, so a repeat over a new
        // chord sounds like the same idea moved onto that chord.
        int pitch = nearestChordTone (answer ? lastPitch : anchor, offset, 1);
        int index = indexOf (pitch);
        const size_t firstNote = out.size();
        for (size_t i = 0; i < rhythm.size(); ++i)
        {
            const double start = offset + rhythm[i];
            if (start >= total - kEps)
                break;
            const int previousPitch = pitch;
            int moved = 0;
            if (i > 0)
            {
                int step = contour[i];
                if (pitchOf (index + step) > kHigh || pitchOf (index + step) < kLow)
                    step = -step;
                index += step;
                moved = step;
                pitch = pitchOf (index);
                if (pitch > kHigh)
                    index -= music::notesPerOctave (s);
                else if (pitch < kLow)
                    index += music::notesPerOctave (s);
                pitch = pitchOf (index);
            }
            if (isWhole (start))
            {
                // Strong beats sit on the harmony.
                pitch = chordToneToward (previousPitch, nearestChordTone (pitch, start, moved), start, moved);
                index = indexOf (pitch);
            }

            const double nextOnset = i + 1 < rhythm.size() ? rhythm[i + 1] : motifLength;
            double length = std::min (nextOnset - rhythm[i], 2.0) * (0.85 + 0.1 * rng.uniform());
            if (i + 1 == rhythm.size())
                length = std::min (length, 1.5);

            double velocity = isWhole (start) ? (isWhole (start / bar) ? 108.0 : 98.0) : (isWhole (start * 2.0) ? 88.0 : 78.0);
            velocity += rng.range (-6.0, 6.0) + 6.0 * (density - 0.5);
            out.push_back ({ start, length, pitch, clampVelocity (velocity) });
        }

        // The phrase comes home: the last note of the melody is the root of the last chord, held to the end.
        if (lastInstance && out.size() > firstNote)
        {
            auto& last = out.back();
            const int chordRoot = degreePitchClass (s, root, degreeAt (degrees, span, last.start));
            int best = last.pitch, bestDistance = 99;
            for (int n = kLow; n <= kHigh; ++n)
                if (n % 12 == chordRoot && std::abs (n - last.pitch) < bestDistance)
                {
                    best = n;
                    bestDistance = std::abs (n - last.pitch);
                }
            last.pitch = best;
            last.length = std::max (last.length, total - last.start - 0.05);
        }
        if (out.size() > firstNote)
            lastPitch = out.back().pitch;
    }

    if (ctx.allowChromatic && complexity > 0.4)
    {
        // A whole step on a weak, long enough note is split by the chromatic note in between.
        Notes withPassing;
        for (size_t i = 0; i < out.size(); ++i)
        {
            auto n = out[i];
            const bool canSplit = i + 1 < out.size() && ! isWhole (n.start) && n.length >= 2.0 * grid
                                  && std::abs (out[i + 1].pitch - n.pitch) == 2 && ! isWhole (n.start + 0.5 * n.length);
            if (canSplit && rng.chance (0.5))
            {
                const int between = (n.pitch + out[i + 1].pitch) / 2;
                n.length *= 0.5;
                withPassing.push_back (n);
                withPassing.push_back ({ n.start + n.length, n.length, between, std::max (1, n.velocity - 10) });
                continue;
            }
            withPassing.push_back (n);
        }
        out = std::move (withPassing);
    }

    finish (out, total);
    return out;
}

//==============================================================================
Notes arp (const GenContext& ctx, const std::vector<int>& chordDegrees, ArpPattern pattern, double stepBeats)
{
    Rng rng (ctx.seed, kSaltArp + styleSalt (ctx));
    const auto& s = harmonyScale (ctx);
    const int root = rootOf (ctx);
    const double total = totalBeats (ctx);
    const double density = clamp01 (ctx.density);
    const std::vector<int> degrees = chordDegrees.empty() ? std::vector<int> { 0 } : chordDegrees;
    const double span = total / (double) degrees.size();
    const double step = std::clamp (stepBeats, 1.0 / 32.0, 2.0);

    Notes out;
    for (size_t c = 0; c < degrees.size(); ++c)
    {
        const auto pcs = chordPitchClasses (s, root, degrees[c], ctx.complexity > 0.6 ? 1 : 0);
        // Two octaves of chord tones, ascending from the chord root between G3 and F#4.
        const int base = 55 + music::pitchClass (pcs.front(), 55 % 12);
        std::vector<int> tones;
        for (int octave = 0; octave < 2; ++octave)
            for (int pc : pcs)
                tones.push_back (base + music::pitchClass (pc, pcs.front()) + 12 * octave);
        std::sort (tones.begin(), tones.end());
        const int n = (int) tones.size();

        const double spanStart = (double) c * span;
        int previous = -1, i = 0;
        for (double t = spanStart; t < spanStart + span - kEps; t += step, ++i)
        {
            int index = 0;
            switch (pattern)
            {
                case ArpPattern::up: index = i % n; break;
                case ArpPattern::down: index = n - 1 - i % n; break;
                case ArpPattern::upDown:
                {
                    const int period = std::max (1, 2 * n - 2);
                    const int p = i % period;
                    index = p < n ? p : period - p;
                    break;
                }
                case ArpPattern::random:
                    index = rng.integer (0, n - 1);
                    if (index == previous)
                        index = (index + 1) % n;
                    break;
            }
            previous = index;
            const double inBeat = t - spanStart;
            // Thin sparse arps on the weakest steps, never on the beat.
            if (density < 0.3 && ! isWhole (inBeat * 2.0) && rng.chance (0.3))
                continue;
            const double velocity = isWhole (inBeat) ? 100.0 : (isWhole (inBeat * 2.0) ? 88.0 : 78.0);
            out.push_back ({ t, step * (0.45 + 0.4 * density), tones[(size_t) index], clampVelocity (velocity + rng.range (-5.0, 5.0)) });
        }
    }
    finish (out, total);
    return out;
}

//==============================================================================
Notes drums (const GenContext& ctx)
{
    Rng rng (ctx.seed, kSaltDrums + styleSalt (ctx));
    const Style st = styleOf (ctx.style);
    const double bar = barBeats (ctx), total = totalBeats (ctx);
    const double density = clamp01 (ctx.density), complexity = clamp01 (ctx.complexity);
    const int bars = barCount (ctx);
    const int beats = (int) std::floor (bar + kEps);
    const double humanVelocity = 3.0 + 9.0 * complexity;

    Notes out;
    auto add = [&] (int pitch, double at, double velocity, double length = 0.1) {
        out.push_back ({ at, length, pitch, clampVelocity (velocity + rng.range (-humanVelocity, humanVelocity)) });
    };
    auto sixteenths = [&] (auto&& fn) {
        for (double p = 0.0; p < bar - kEps; p += 0.25)
            fn (p);
    };

    // One-bar template decisions are drawn once, so the groove repeats like a real drum part; only ghost notes and
    // velocities change from bar to bar.
    const bool clapLayer = rng.chance (0.5 + 0.5 * density);
    const bool openOffbeats = rng.chance (0.6);
    const double swing = st == Style::hiphop ? 0.05 + 0.08 * complexity : 0.0;
    std::vector<double> extraKicks;
    switch (st)
    {
        case Style::hiphop:
            for (double k : { 0.75, 1.75, 3.25 })
                if (rng.chance (0.25 + 0.5 * density))
                    extraKicks.push_back (k);
            break;
        case Style::trap:
            for (double k : { 0.75, 1.5, 3.25, 3.75 })
                if (rng.chance (0.2 + 0.5 * density))
                    extraKicks.push_back (k);
            break;
        case Style::dnb:
            for (double k : { 1.75, 3.25 })
                if (rng.chance (0.6 * complexity))
                    extraKicks.push_back (k);
            break;
        case Style::pop:
            for (double k : { 1.5, 2.5 })
                if (rng.chance (0.2 + 0.5 * density))
                    extraKicks.push_back (k);
            break;
        case Style::house:
        case Style::techno:
        case Style::ambient:
            break;
    }
    auto extraKick = [&] (double p) {
        for (double k : extraKicks)
            if (std::abs (k - p) < kEps && k < bar - kEps)
                return true;
        return false;
    };
    auto backbeat = [&] (double p) { return isWhole (p) && ((int) std::lround (p)) % 2 == 1; };
    const double halfTimeSnare = (double) (beats / 2);

    bool trapRollPlaced = false;
    for (int b = 0; b < bars; ++b)
    {
        const double o = b * bar;
        switch (st)
        {
            case Style::house:
            case Style::techno:
                for (int k = 0; k < beats; ++k)
                    add (drum::kick, o + k, k == 0 ? 124.0 : 118.0);
                sixteenths ([&] (double p) {
                    if (backbeat (p) && (st == Style::house || clapLayer))
                    {
                        add (drum::clap, o + p, 105.0);
                        if (st == Style::house && density > 0.6)
                            add (drum::snare, o + p, 70.0);
                    }
                    const double inBeat = p - std::floor (p);
                    if (std::abs (inBeat - 0.5) < kEps)
                        add (openOffbeats ? drum::openHat : drum::closedHat, o + p, 92.0, 0.25);
                    else if (! isWhole (p) && rng.chance (st == Style::techno ? 0.4 + 0.6 * density : density))
                        add (drum::closedHat, o + p, 62.0);
                    else if (isWhole (p) && rng.chance (density * 0.4))
                        add (drum::closedHat, o + p, 48.0);
                    if (std::abs (inBeat - 0.75) < kEps && rng.chance (0.3 * complexity))
                        add (drum::rim, o + p, 60.0);
                });
                break;

            case Style::hiphop:
                sixteenths ([&] (double p) {
                    if (p < kEps || std::abs (p - 2.5) < kEps || extraKick (p))
                        add (drum::kick, o + p, p < kEps ? 115.0 : 104.0);
                    if (backbeat (p))
                        add (drum::snare, o + p, 112.0);
                    const double inBeat = p - std::floor (p);
                    if (isWhole (p))
                        add (drum::closedHat, o + p, 86.0);
                    else if (std::abs (inBeat - 0.5) < kEps)
                        add (std::abs (p - 3.5) < kEps && openOffbeats ? drum::openHat : drum::closedHat, o + p + swing, 66.0);
                    else if (density > 0.7 && rng.chance (density - 0.4))
                        add (drum::closedHat, o + p + swing * 0.5, 50.0);
                    if (! isWhole (p * 2.0) && rng.chance (0.35 * complexity))
                        add (drum::snare, o + p + swing * 0.5, 32.0); // ghost
                });
                break;

            case Style::trap:
            {
                sixteenths ([&] (double p) {
                    if (p < kEps || extraKick (p))
                        add (drum::kick, o + p, p < kEps ? 118.0 : 108.0);
                    if (std::abs (p - halfTimeSnare) < kEps)
                    {
                        add (drum::snare, o + p, 115.0);
                        if (clapLayer)
                            add (drum::clap, o + p, 100.0);
                    }
                    if (! isWhole (p * 2.0) && rng.chance (0.2 * complexity))
                        add (drum::rim, o + p, 55.0);
                });
                // Hats: eighths with rolls in sixteenths, sixteenth triplets or thirty-seconds, drawn per beat.
                for (int k = 0; k < beats; ++k)
                {
                    const bool forceRoll = ! trapRollPlaced && (k == beats - 1) && (b == bars - 1 || b == 0);
                    const double rollChance = 0.15 + 0.45 * complexity;
                    if (forceRoll || rng.chance (rollChance))
                    {
                        const double rates[] { 0.25, 1.0 / 6.0, 0.125 };
                        const double r = rates[rng.integer (0, complexity > 0.4 ? 2 : 1)];
                        const int count = (int) std::lround (1.0 / r);
                        for (int i = 0; i < count; ++i)
                            add (drum::closedHat, o + k + i * r, 58.0 + 40.0 * i / count, 0.06);
                        trapRollPlaced = true;
                    }
                    else
                    {
                        add (drum::closedHat, o + k, 88.0);
                        if (k + 0.5 < bar)
                            add (k == beats - 1 && rng.chance (0.3) ? drum::openHat : drum::closedHat, o + k + 0.5, 70.0);
                    }
                }
                break;
            }

            case Style::dnb:
                sixteenths ([&] (double p) {
                    if (p < kEps || std::abs (p - 2.5) < kEps || extraKick (p))
                        add (drum::kick, o + p, p < kEps ? 120.0 : 110.0);
                    if (backbeat (p))
                        add (drum::snare, o + p, 118.0);
                    const double inBeat = p - std::floor (p);
                    if (isWhole (p * 2.0))
                        add (std::abs (p - 1.5) < kEps && openOffbeats ? drum::openHat : drum::closedHat, o + p,
                             std::abs (inBeat - 0.5) < kEps ? 78.0 : 70.0);
                    else if (rng.chance (density * 0.6))
                        add (drum::closedHat, o + p, 52.0);
                    if ((std::abs (inBeat - 0.75) < kEps) && rng.chance (0.3 + 0.5 * complexity))
                        add (drum::snare, o + p, 36.0); // ghost
                });
                break;

            case Style::pop:
                sixteenths ([&] (double p) {
                    if (p < kEps || std::abs (p - 2.0) < kEps || extraKick (p))
                        add (drum::kick, o + p, 112.0);
                    if (backbeat (p))
                    {
                        add (drum::snare, o + p, 110.0);
                        if (clapLayer)
                            add (drum::clap, o + p, 90.0);
                    }
                    if (isWhole (p * 2.0))
                        add (drum::closedHat, o + p, isWhole (p) ? 84.0 : 68.0);
                    else if (rng.chance (0.5 * density * complexity))
                        add (drum::closedHat, o + p, 50.0);
                    if (! isWhole (p * 2.0) && rng.chance (0.2 * complexity))
                        add (drum::snare, o + p, 30.0); // ghost
                });
                break;

            case Style::ambient:
                add (drum::kick, o, 82.0, 0.25);
                if (bar > 2.5 && rng.chance (0.5))
                    add (drum::kick, o + 2.5, 64.0, 0.25);
                sixteenths ([&] (double p) {
                    if (backbeat (p) && rng.chance (0.6))
                        add (drum::rim, o + p, 55.0);
                    if (isWhole (p * 2.0) && rng.chance (0.3 + 0.5 * density))
                        add (drum::closedHat, o + p, 40.0);
                });
                if (bar > 2.5)
                    add (drum::openHat, o + 2.5, 45.0, 0.5);
                break;
        }
    }

    // The fill: the end of the last bar is cleared (house and techno keep their kick) and replaced.
    if (bars >= 2)
    {
        const double fillLength = std::min (bar, complexity > 0.6 ? 2.0 : 1.0);
        const double fillStart = total - fillLength;
        const bool keepKick = st == Style::house || st == Style::techno;
        out.erase (std::remove_if (out.begin(), out.end(), [&] (const Note& n) {
                       return n.start >= fillStart - kEps && ! (keepKick && n.pitch == drum::kick)
                              && ! (st == Style::trap && n.pitch == drum::closedHat);
                   }),
                   out.end());
        const int steps = (int) std::lround (fillLength / 0.25);
        for (int i = 0; i < steps; ++i)
        {
            const double at = fillStart + i * 0.25;
            const double rise = 70.0 + 45.0 * i / std::max (1, steps - 1);
            switch (st)
            {
                case Style::house:
                case Style::techno:
                    add (i % 2 == 0 ? drum::snare : drum::clap, at, rise);
                    break;
                case Style::trap:
                    add (drum::snare, at, rise);
                    if (complexity > 0.5)
                        add (drum::snare, at + 0.125, rise - 15.0);
                    break;
                case Style::ambient:
                    if (i % 2 == 0)
                        add (i < steps / 2 ? drum::tomMid : drum::tomLow, at, 60.0 + 10.0 * i / steps, 0.25);
                    break;
                case Style::hiphop:
                case Style::dnb:
                case Style::pop:
                {
                    static const int toms[] { drum::snare, drum::tomHigh, drum::tomMid, drum::tomLow };
                    const int which = std::min (3, i * 4 / std::max (1, steps));
                    add (toms[which], at, rise);
                    if (i == 0)
                        add (drum::kick, at, 105.0);
                    break;
                }
            }
        }
    }

    if ((st == Style::pop || st == Style::dnb || st == Style::house) && rng.chance (0.4 + 0.4 * complexity))
        add (drum::crash, 0.0, 100.0, 1.0);

    finish (out, total);
    return out;
}

//==============================================================================
Notes humanise (Notes notes, double timingBeats, int velocityRange, uint32_t seed, double clipLengthBeats)
{
    Rng rng (seed, 0x68756d61);
    const double t = std::max (0.0, timingBeats);
    const int v = std::max (0, velocityRange);
    for (auto& n : notes)
    {
        n.start = std::max (0.0, n.start + rng.range (-t, t));
        n.velocity = std::clamp (n.velocity + rng.integer (-v, v), 1, 127);
        if (clipLengthBeats > 0.0)
        {
            n.start = std::min (n.start, clipLengthBeats - 1.0 / 64.0);
            n.length = std::clamp (n.length, 1.0 / 64.0, clipLengthBeats - n.start);
        }
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const engine::Note& a, const engine::Note& b) { return a.start < b.start; });
    return notes;
}

} // namespace daw::gen
