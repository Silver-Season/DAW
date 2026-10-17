// Tests for the Inspiration generators (core/include/daw/gen): patterns and Prisma patches. Deterministic: every
// random choice comes from a fixed seed.
#include "daw/gen/Patches.h"
#include "daw/gen/Patterns.h"
#include "daw/synth/PrismaSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        if (failures < 60)
            std::printf ("  FAIL: %s\n", what.c_str());
    }
}

void test (const char* name, const std::function<void()>& fn)
{
    std::printf ("[test] %s\n", name);
    fn();
}

using namespace daw;
using gen::GenContext;
using gen::Notes;

bool sameNotes (const Notes& a, const Notes& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].start != b[i].start || a[i].length != b[i].length || a[i].pitch != b[i].pitch || a[i].velocity != b[i].velocity)
            return false;
    return true;
}

GenContext context (const std::string& style, int bars, const char* scaleKey, uint32_t seed, int root = 0)
{
    GenContext c;
    c.style = style;
    c.bars = bars;
    c.scale = music::findScale (scaleKey);
    c.seed = seed;
    c.root = root;
    return c;
}

std::string describe (const GenContext& c)
{
    return c.style + " bars " + std::to_string (c.bars) + " scale " + (c.scale != nullptr ? c.scale->key : "major") + " root "
           + std::to_string (c.root) + " seed " + std::to_string (c.seed) + " density " + std::to_string (c.density)
           + " complexity " + std::to_string (c.complexity);
}

double total (const GenContext& c) { return c.bars * c.beatsPerBar; }

bool insideClip (const Notes& notes, const GenContext& c)
{
    for (const auto& n : notes)
        if (n.start < 0.0 || n.length <= 0.0 || n.start + n.length > total (c) + 1.0e-9 || n.velocity < 1 || n.velocity > 127
            || n.pitch < 0 || n.pitch > 127)
            return false;
    return true;
}

bool allInScale (const Notes& notes, const GenContext& c)
{
    const auto& s = c.scale != nullptr ? *c.scale : music::allScales().front();
    for (const auto& n : notes)
        if (! music::inScale (n.pitch, c.root, s))
            return false;
    return true;
}

// The chord notes of every span, as {start of the first hit -> pitches}, using the rule from Patterns.h.
std::vector<std::vector<int>> chordVoicings (const Notes& chordNotes, const std::vector<int>& degrees, const GenContext& c)
{
    const double span = total (c) / (double) degrees.size();
    std::vector<std::vector<int>> v (degrees.size());
    std::vector<double> firstHit (degrees.size(), 1.0e9);
    for (const auto& n : chordNotes)
    {
        const auto i = (size_t) std::floor (n.start / span + 1.0e-9);
        firstHit[i] = std::min (firstHit[i], n.start);
    }
    for (const auto& n : chordNotes)
    {
        const auto i = (size_t) std::floor (n.start / span + 1.0e-9);
        if (std::abs (n.start - firstHit[i]) < 1.0e-9)
            v[i].push_back (n.pitch);
    }
    return v;
}

std::set<int> pitchClassesOfSpan (const Notes& chordNotes, double span, size_t index)
{
    std::set<int> pcs;
    for (const auto& n : chordNotes)
        if ((size_t) std::floor (n.start / span + 1.0e-9) == index)
            pcs.insert (n.pitch % 12);
    return pcs;
}

int degreePc (const GenContext& c, int degree)
{
    const auto& s = c.scale != nullptr && music::notesPerOctave (*c.scale) < 12 ? *c.scale : music::allScales().front();
    return music::degreeToNote (degree, c.root, s, 60) % 12;
}

const std::vector<const char*> kScales { "major", "minor", "dorian", "minorPentatonic", "majorPentatonic", "harmonicMinor", "blues" };
} // namespace

int main()
{
    test ("patterns: the same seed gives the same notes, another seed other notes", [] {
        for (const auto& style : gen::styleNames())
        {
            auto c = context (style, 4, "minor", 42, 9);
            std::vector<int> d1, d2, d3;
            const auto chords1 = gen::chords (c, &d1), chords2 = gen::chords (c, &d2);
            check (sameNotes (chords1, chords2) && d1 == d2, style + ": chords repeat for a seed");
            check (sameNotes (gen::bass (c, d1), gen::bass (c, d1)), style + ": bass repeats");
            check (sameNotes (gen::melody (c, d1), gen::melody (c, d1)), style + ": melody repeats");
            check (sameNotes (gen::arp (c, d1, gen::ArpPattern::random), gen::arp (c, d1, gen::ArpPattern::random)), style + ": arp repeats");
            check (sameNotes (gen::drums (c), gen::drums (c)), style + ": drums repeat");

            // Over a handful of other seeds every generator must come up with something different at least once.
            bool chordsDiffer = false, bassDiffers = false, melodyDiffers = false, drumsDiffer = false;
            for (uint32_t s = 43; s < 50; ++s)
            {
                auto o = c;
                o.seed = s;
                const auto other = gen::chords (o, &d3);
                chordsDiffer = chordsDiffer || ! sameNotes (other, chords1);
                bassDiffers = bassDiffers || ! sameNotes (gen::bass (o, d3), gen::bass (c, d1));
                melodyDiffers = melodyDiffers || ! sameNotes (gen::melody (o, d3), gen::melody (c, d1));
                drumsDiffer = drumsDiffer || ! sameNotes (gen::drums (o), gen::drums (c));
            }
            check (chordsDiffer && bassDiffers && melodyDiffers && drumsDiffer, style + ": other seeds give other patterns");
        }
    });

    test ("patterns: every style, length and scale gives notes inside the clip and in the scale", [] {
        int combinations = 0;
        for (const auto& style : gen::styleNames())
            for (int bars : { 1, 2, 4, 8 })
                for (const char* scale : kScales)
                    for (uint32_t seed : { 1u, 7u, 1234u })
                        for (double knob : { 0.0, 0.5, 1.0 })
                        {
                            auto c = context (style, bars, scale, seed, (int) (seed % 12));
                            c.density = knob;
                            c.complexity = 1.0 - knob;
                            const auto what = describe (c);
                            std::vector<int> degrees;
                            const auto ch = gen::chords (c, &degrees);
                            const auto bs = gen::bass (c, degrees);
                            const auto ml = gen::melody (c, degrees);
                            const auto ar = gen::arp (c, degrees, (gen::ArpPattern) (seed % 4), seed % 2 == 0 ? 0.25 : 0.5);
                            const auto dr = gen::drums (c);
                            check (! ch.empty() && ! bs.empty() && ! ml.empty() && ! ar.empty() && ! dr.empty(), what + ": something for every part");
                            check (degrees.size() == (size_t) bars || degrees.size() == (size_t) bars * 2, what + ": one chord per bar or half bar");
                            for (const auto* part : { &ch, &bs, &ml, &ar, &dr })
                                check (insideClip (*part, c), what + ": notes inside the clip");
                            check (allInScale (ch, c), what + ": chords in scale");
                            check (allInScale (bs, c), what + ": bass in scale");
                            check (allInScale (ml, c), what + ": melody in scale");
                            check (allInScale (ar, c), what + ": arp in scale");
                            ++combinations;
                        }
        std::printf ("  %d combinations\n", combinations);
    });

    test ("patterns: odd meters (3/4, 7/8, 6/8) stay inside the clip and in the scale", [] {
        for (double meter : { 3.0, 3.5, 6.0 })
            for (const auto& style : gen::styleNames())
                for (uint32_t seed = 1; seed <= 6; ++seed)
                {
                    auto c = context (style, 4, "dorian", seed, 2);
                    c.beatsPerBar = meter;
                    c.complexity = (seed % 3) * 0.5;
                    const auto what = describe (c) + " meter " + std::to_string (meter);
                    std::vector<int> degrees;
                    const auto ch = gen::chords (c, &degrees);
                    const auto bs = gen::bass (c, degrees);
                    const auto ml = gen::melody (c, degrees);
                    const auto ar = gen::arp (c, degrees, gen::ArpPattern::upDown, 0.5);
                    const auto dr = gen::drums (c);
                    for (const auto* part : { &ch, &bs, &ml, &ar, &dr })
                        check (! part->empty() && insideClip (*part, c), what + ": notes inside the clip");
                    for (const auto* part : { &ch, &bs, &ml, &ar })
                        check (allInScale (*part, c), what + ": in scale");
                    if (style == "house")
                    {
                        std::set<double> kicks;
                        for (const auto& n : dr)
                            if (n.pitch == gen::drum::kick)
                                kicks.insert (n.start);
                        bool all = true;
                        for (int bar = 0; bar < 4; ++bar)
                            for (int beat = 0; beat < (int) meter; ++beat)
                                all = all && kicks.count (bar * meter + beat) == 1;
                        check (all, what + ": house kick on every beat");
                    }
                }
    });

    test ("patterns: a chromatic scale borrows major harmony and a missing scale means major", [] {
        auto c = context ("pop", 4, "chromatic", 5);
        std::vector<int> degrees;
        const auto ch = gen::chords (c, &degrees);
        auto major = context ("pop", 4, "major", 5);
        const auto chMajor = gen::chords (major, nullptr);
        check (sameNotes (ch, chMajor), "chromatic = major harmony");
        auto none = context ("pop", 4, "", 5);
        check (none.scale == nullptr && sameNotes (gen::chords (none), chMajor), "no scale = major");
    });

    test ("chords: three notes or more from the scale, close voice leading, around C3..C5", [] {
        double worstAverage = 0.0;
        for (const auto& style : gen::styleNames())
            for (const char* scale : kScales)
                for (uint32_t seed = 1; seed <= 12; ++seed)
                    for (int bars : { 2, 4, 8 })
                    {
                        auto c = context (style, bars, scale, seed, (int) (seed * 5 % 12));
                        c.complexity = (seed % 3) * 0.5;
                        c.density = (seed % 4) / 3.0;
                        std::vector<int> degrees;
                        const auto notes = gen::chords (c, &degrees);
                        const auto what = describe (c);

                        // every hit (all notes with the same start) is a chord of three or more distinct notes
                        std::map<double, std::set<int>> hits;
                        for (const auto& n : notes)
                            hits[n.start].insert (n.pitch);
                        bool chordsFull = true, inRange = true;
                        for (const auto& [at, pitches] : hits)
                            chordsFull = chordsFull && pitches.size() >= 3;
                        for (const auto& n : notes)
                            inRange = inRange && n.pitch >= 48 && n.pitch <= 76;
                        check (chordsFull, what + ": every chord has 3+ notes");
                        check (inRange, what + ": chords between C3 and E5");
                        check (allInScale (notes, c), what + ": chord notes in scale");

                        // the root of each degree is in its chord
                        const double span = total (c) / (double) degrees.size();
                        for (size_t i = 0; i < degrees.size(); ++i)
                            check (pitchClassesOfSpan (notes, span, i).count (degreePc (c, degrees[i])) == 1, what + ": chord contains its root");

                        // voice leading: every voice of the next chord finds a voice of this chord close by
                        const auto voicings = chordVoicings (notes, degrees, c);
                        double movement = 0;
                        int moves = 0;
                        for (size_t i = 1; i < voicings.size(); ++i)
                            for (int n : voicings[i])
                            {
                                int nearest = 127;
                                for (int p : voicings[i - 1])
                                    nearest = std::min (nearest, std::abs (n - p));
                                movement += nearest;
                                ++moves;
                            }
                        const double average = moves > 0 ? movement / moves : 0.0;
                        worstAverage = std::max (worstAverage, average);
                        check (average <= 4.0, what + ": average voice movement " + std::to_string (average) + " <= 4 semitones");
                    }
        std::printf ("  worst average voice movement %.2f semitones\n", worstAverage);
    });

    test ("bass: the first note of every chord is its root, in the bass register", [] {
        for (const auto& style : gen::styleNames())
            for (const char* scale : kScales)
                for (uint32_t seed = 1; seed <= 10; ++seed)
                    for (int bars : { 1, 4, 8 })
                    {
                        auto c = context (style, bars, scale, seed, (int) (seed * 7 % 12));
                        c.density = (seed % 3) * 0.5;
                        c.complexity = (seed % 2) * 0.9;
                        std::vector<int> degrees;
                        gen::chords (c, &degrees);
                        const auto notes = gen::bass (c, degrees);
                        const auto what = describe (c);
                        const double span = total (c) / (double) degrees.size();
                        const bool offbeat = style == "house" || style == "techno";
                        for (size_t i = 0; i < degrees.size(); ++i)
                        {
                            const double from = (double) i * span;
                            const auto first = std::find_if (notes.begin(), notes.end(), [&] (const engine::Note& n) { return n.start >= from - 1.0e-9; });
                            const bool found = first != notes.end() && first->start < from + span - 1.0e-9;
                            check (found && first->pitch % 12 == degreePc (c, degrees[i]), what + ": span " + std::to_string (i) + " starts on its root");
                            if (found && ! offbeat)
                                check (std::abs (first->start - from) < 1.0e-9, what + ": root on the downbeat of span " + std::to_string (i));
                        }
                        bool register_ = true;
                        for (const auto& n : notes)
                            register_ = register_ && n.pitch >= 24 && n.pitch <= 48;
                        check (register_, what + ": bass in C1..C3");
                    }
    });

    test ("bass: trap has long 808 notes and slides that overlap", [] {
        int slides = 0, longNotes = 0;
        for (uint32_t seed = 1; seed <= 20; ++seed)
        {
            auto c = context ("trap", 4, "minor", seed);
            c.complexity = 0.8;
            std::vector<int> degrees;
            gen::chords (c, &degrees);
            const auto notes = gen::bass (c, degrees);
            for (size_t i = 0; i < notes.size(); ++i)
            {
                longNotes += notes[i].length >= 1.5 ? 1 : 0;
                if (i + 1 < notes.size() && notes[i].start + notes[i].length > notes[i + 1].start + 1.0e-9 && notes[i].pitch != notes[i + 1].pitch)
                    ++slides;
            }
        }
        check (longNotes >= 20, "trap bass has long notes (" + std::to_string (longNotes) + ")");
        check (slides >= 10, "trap bass has overlapping slides (" + std::to_string (slides) + ")");
    });

    test ("drums: house and techno have a kick on every beat; fills in the last bar", [] {
        for (const char* style : { "house", "techno" })
            for (uint32_t seed = 1; seed <= 20; ++seed)
                for (int bars : { 1, 2, 4, 8 })
                {
                    auto c = context (style, bars, "minor", seed);
                    c.density = (seed % 5) / 4.0;
                    c.complexity = (seed % 3) / 2.0;
                    const auto notes = gen::drums (c);
                    std::set<double> kicks;
                    for (const auto& n : notes)
                        if (n.pitch == gen::drum::kick)
                            kicks.insert (n.start);
                    bool all = true;
                    for (int beat = 0; beat < bars * 4; ++beat)
                        all = all && kicks.count ((double) beat) == 1;
                    check (all, describe (c) + ": kick on every beat");
                }

        // with two bars or more the last bar differs from the first one: a fill
        for (const auto& style : gen::styleNames())
            for (uint32_t seed = 1; seed <= 5; ++seed)
            {
                auto c = context (style, 4, "minor", seed);
                const auto notes = gen::drums (c);
                std::multiset<std::pair<int, double>> first, last;
                for (const auto& n : notes)
                {
                    if (n.start < 4.0)
                        first.insert ({ n.pitch, n.start });
                    else if (n.start >= 12.0)
                        last.insert ({ n.pitch, n.start - 12.0 });
                }
                check (first != last, describe (c) + ": the last bar has a fill");
            }
    });

    test ("drums: trap has hats on 1/16 or finer, and every style uses the GM map", [] {
        for (uint32_t seed = 1; seed <= 30; ++seed)
            for (int bars : { 1, 2, 4 })
            {
                auto c = context ("trap", bars, "minor", seed);
                c.complexity = (seed % 3) / 2.0;
                std::vector<double> hats;
                for (const auto& n : gen::drums (c))
                    if (n.pitch == gen::drum::closedHat)
                        hats.push_back (n.start);
                std::sort (hats.begin(), hats.end());
                bool fine = false;
                for (size_t i = 1; i < hats.size(); ++i)
                    fine = fine || hats[i] - hats[i - 1] <= 0.25 + 1.0e-9;
                check (fine, describe (c) + ": a hat roll");
            }

        const std::set<int> gm { 36, 37, 38, 39, 42, 46, 49, 50, 47, 45 };
        for (const auto& style : gen::styleNames())
        {
            auto c = context (style, 4, "minor", 3);
            c.complexity = 1.0;
            c.density = 1.0;
            bool ok = true;
            for (const auto& n : gen::drums (c))
                ok = ok && gm.count (n.pitch) == 1;
            check (ok, style + ": only General MIDI drum notes");
        }
    });

    test ("drums: backbeats, ghost notes and velocity humanisation follow the knobs", [] {
        auto count = [] (const Notes& notes, int pitch, int maxVelocity) {
            int k = 0;
            for (const auto& n : notes)
                k += n.pitch == pitch && n.velocity <= maxVelocity ? 1 : 0;
            return k;
        };
        int ghostsLow = 0, ghostsHigh = 0;
        for (uint32_t seed = 1; seed <= 20; ++seed)
        {
            auto c = context ("hiphop", 4, "minor", seed);
            c.complexity = 0.0;
            ghostsLow += count (gen::drums (c), gen::drum::snare, 50);
            c.complexity = 1.0;
            ghostsHigh += count (gen::drums (c), gen::drum::snare, 50);

            auto pop = context ("pop", 2, "major", seed);
            std::set<double> snares;
            for (const auto& n : gen::drums (pop))
                if (n.pitch == gen::drum::snare && n.velocity > 80)
                    snares.insert (n.start);
            check (snares.count (1.0) == 1 && snares.count (3.0) == 1, "pop snare on 2 and 4 (seed " + std::to_string (seed) + ")");
        }
        check (ghostsHigh > ghostsLow + 20, "complexity adds ghost snares (" + std::to_string (ghostsLow) + " -> " + std::to_string (ghostsHigh) + ")");
    });

    test ("melody: chord tones on strong beats, range about 1.5 octaves, rests and accents", [] {
        int strong = 0, strongChordTones = 0;
        for (const auto& style : gen::styleNames())
            for (const char* scale : kScales)
                for (uint32_t seed = 1; seed <= 10; ++seed)
                    for (int bars : { 2, 4, 8 })
                    {
                        auto c = context (style, bars, scale, seed, (int) (seed * 3 % 12));
                        c.complexity = (seed % 3) * 0.5;
                        c.density = (seed % 4) / 3.0;
                        std::vector<int> degrees;
                        const auto chordNotes = gen::chords (c, &degrees);
                        const auto notes = gen::melody (c, degrees);
                        const double span = total (c) / (double) degrees.size();
                        int clipStrong = 0, clipChordTones = 0;
                        bool range = true;
                        for (const auto& n : notes)
                        {
                            range = range && n.pitch >= 60 && n.pitch <= 79;
                            if (std::abs (n.start - std::round (n.start)) > 1.0e-9)
                                continue;
                            ++clipStrong;
                            const auto pcs = pitchClassesOfSpan (chordNotes, span, (size_t) std::floor (n.start / span + 1.0e-9));
                            clipChordTones += pcs.count (n.pitch % 12) == 1 ? 1 : 0;
                        }
                        strong += clipStrong;
                        strongChordTones += clipChordTones;
                        check (range, describe (c) + ": melody in C4..G5");
                        check (clipStrong == 0 || clipChordTones * 10 >= clipStrong * 6, describe (c) + ": 60 % chord tones on strong beats");
                    }
        std::printf ("  %d of %d strong-beat notes are chord tones\n", strongChordTones, strong);

        // a melody breathes: there are gaps between notes, and the downbeats are accented
        auto c = context ("pop", 4, "major", 11);
        std::vector<int> degrees;
        gen::chords (c, &degrees);
        const auto notes = gen::melody (c, degrees);
        double sounding = 0;
        double downbeatVelocity = 0, otherVelocity = 0;
        int downbeats = 0, others = 0;
        for (const auto& n : notes)
        {
            sounding += n.length;
            const bool downbeat = std::abs (n.start / 4.0 - std::round (n.start / 4.0)) < 1.0e-9;
            (downbeat ? downbeatVelocity : otherVelocity) += n.velocity;
            (downbeat ? downbeats : others) += 1;
        }
        check (sounding < total (c) * 0.95, "the melody has rests");
        check (downbeats > 0 && others > 0 && downbeatVelocity / downbeats > otherVelocity / others, "downbeats are accented");
    });

    test ("melody: a motif, its varied repeat, the motif again and an answer", [] {
        // Four bars are A A' A B: the rhythm of bar 1 comes back in bar 3, bar 2 is mostly the same and bar 4 differs.
        int sameSecond = 0, differentAnswer = 0;
        for (uint32_t seed = 1; seed <= 20; ++seed)
        {
            auto c = context ("pop", 4, "major", seed);
            c.complexity = 0.3;
            std::vector<int> degrees;
            gen::chords (c, &degrees);
            std::set<double> bars[4];
            for (const auto& n : gen::melody (c, degrees))
            {
                const double bar = std::floor (n.start / 4.0);
                bars[(size_t) bar].insert (n.start - 4.0 * bar);
            }
            check (bars[0] == bars[2], "the motif rhythm returns in bar 3 (seed " + std::to_string (seed) + ")");
            sameSecond += bars[0] == bars[1] ? 1 : 0;
            differentAnswer += bars[0] != bars[3] ? 1 : 0;
        }
        check (sameSecond >= 8, "the varied repeat mostly keeps the rhythm (" + std::to_string (sameSecond) + " of 20)");
        check (differentAnswer >= 10, "the answer phrase has its own rhythm (" + std::to_string (differentAnswer) + " of 20)");
    });

    test ("melody: chromatic passing tones only when allowed", [] {
        int chromatic = 0;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            auto c = context ("pop", 4, "major", seed);
            c.complexity = 0.9;
            c.density = 0.4;
            std::vector<int> degrees;
            gen::chords (c, &degrees);
            check (allInScale (gen::melody (c, degrees), c), "no chromatic notes by default");
            c.allowChromatic = true;
            const auto notes = gen::melody (c, degrees);
            for (const auto& n : notes)
                chromatic += music::inScale (n.pitch, 0, *c.scale) ? 0 : 1;
            check (insideClip (notes, c), "chromatic melody inside the clip");
        }
        check (chromatic > 0, "allowChromatic adds passing tones (" + std::to_string (chromatic) + ")");
    });

    test ("arp: patterns and rates", [] {
        auto c = context ("house", 2, "minor", 9, 2);
        c.density = 0.8;
        std::vector<int> degrees;
        gen::chords (c, &degrees);
        const double span = total (c) / (double) degrees.size();

        const auto up = gen::arp (c, degrees, gen::ArpPattern::up, 0.25);
        const auto down = gen::arp (c, degrees, gen::ArpPattern::down, 0.5);
        check (up.size() == (size_t) (total (c) / 0.25), "1/16 arp has a note on every sixteenth");
        check (down.size() == (size_t) (total (c) / 0.5), "1/8 arp has a note on every eighth");
        check (up.size() >= 2 && up[1].pitch > up[0].pitch, "up rises");
        check (down.size() >= 2 && down[1].pitch < down[0].pitch, "down falls");

        const auto upDown = gen::arp (c, degrees, gen::ArpPattern::upDown, 0.25);
        bool turned = false;
        for (size_t i = 2; i < upDown.size() && upDown[i].start < span; ++i)
            turned = turned || (upDown[i - 1].pitch > upDown[i - 2].pitch && upDown[i].pitch < upDown[i - 1].pitch);
        check (turned, "up-down turns around");
        bool randomRepeats = false;
        const auto rnd = gen::arp (c, degrees, gen::ArpPattern::random, 0.25);
        for (size_t i = 1; i < rnd.size(); ++i)
            randomRepeats = randomRepeats || (rnd[i].pitch == rnd[i - 1].pitch && std::floor (rnd[i].start / span) == std::floor (rnd[i - 1].start / span));
        check (! randomRepeats, "random never repeats a note straight away");

        // every arp note of a span belongs to that span's chord
        const auto chordNotes = gen::chords (c, nullptr);
        bool chordTones = true;
        for (const auto& n : up)
            chordTones = chordTones && pitchClassesOfSpan (chordNotes, span, (size_t) std::floor (n.start / span + 1.0e-9)).count (n.pitch % 12) == 1;
        check (chordTones, "arp plays chord tones");
    });

    test ("humanise: bounded, deterministic, keeps notes in the clip", [] {
        auto c = context ("pop", 4, "major", 2);
        const auto drums = gen::drums (c);
        const auto a = gen::humanise (drums, 0.02, 10, 77, total (c));
        const auto b = gen::humanise (drums, 0.02, 10, 77, total (c));
        const auto other = gen::humanise (drums, 0.02, 10, 78, total (c));
        check (sameNotes (a, b), "same seed, same result");
        check (! sameNotes (a, other), "another seed, another result");
        check (a.size() == drums.size() && insideClip (a, c), "same notes, inside the clip");

        // compare as multisets per pitch: every humanised note is near an original one
        bool bounded = true;
        for (const auto& n : a)
        {
            bool near = false;
            for (const auto& o : drums)
                near = near || (o.pitch == n.pitch && std::abs (o.start - n.start) <= 0.02 + 1.0e-9 && std::abs (o.velocity - n.velocity) <= 10);
            bounded = bounded && near;
        }
        check (bounded, "moves stay within the given ranges");

        Notes edge { { 0.0, 1.0, 60, 127 }, { 15.99, 0.01, 62, 1 } };
        const auto e = gen::humanise (edge, 0.5, 20, 3, 16.0);
        check (e[0].start >= 0.0 && e.back().start + e.back().length <= 16.0 + 1.0e-9, "edges are kept inside");
        for (const auto& n : e)
            check (n.velocity >= 1 && n.velocity <= 127, "velocity in 1..127");
    });

    test ("patches: every parameter in range for 200 seeds per kind, whole numbers where needed", [] {
        const auto& descriptors = synth::parameterDescriptors();
        for (auto kind : gen::allSoundKinds())
        {
            bool sizes = true, ranges = true, whole = true, finite = true;
            std::string firstBad;
            for (uint32_t seed = 0; seed < 200; ++seed)
            {
                const float wildness = (float) (seed % 5) / 4.0f;
                const auto values = gen::generatePatch (kind, seed, wildness);
                sizes = sizes && values.size() == descriptors.size();
                for (size_t i = 0; i < values.size() && i < descriptors.size(); ++i)
                {
                    const auto& d = descriptors[i];
                    const float v = values[i];
                    finite = finite && std::isfinite (v);
                    if (v < d.min || v > d.max)
                    {
                        ranges = false;
                        if (firstBad.empty())
                            firstBad = d.id + " = " + std::to_string (v);
                    }
                    if (d.kind != synth::ParamDescriptor::Kind::Float)
                        whole = whole && v == std::round (v);
                }
            }
            const std::string k = gen::soundKindKey (kind);
            check (sizes, k + ": one value per descriptor");
            check (finite, k + ": finite values");
            check (ranges, k + ": values in range " + firstBad);
            check (whole, k + ": whole numbers for ints, choices and toggles");
        }
    });

    test ("patches: deterministic, seeds differ, archetypes hold", [] {
        const auto& descriptors = synth::parameterDescriptors();
        auto indexOf = [&] (const std::string& id) {
            for (size_t i = 0; i < descriptors.size(); ++i)
                if (descriptors[i].id == id)
                    return i;
            return descriptors.size();
        };
        for (auto kind : gen::allSoundKinds())
        {
            check (gen::generatePatch (kind, 5, 0.5f) == gen::generatePatch (kind, 5, 0.5f), std::string (gen::soundKindKey (kind)) + ": same seed, same patch");
            check (gen::generatePatch (kind, 5, 0.5f) != gen::generatePatch (kind, 6, 0.5f), std::string (gen::soundKindKey (kind)) + ": seeds differ");
        }

        const size_t attack = indexOf ("env1_a"), sustain = indexOf ("env1_s"), unison = indexOf ("oscA_unison"), mono = indexOf ("mono");
        const size_t release = indexOf ("env1_r"), sub = indexOf ("sub_on"), glide = indexOf ("glide");
        auto average = [&] (gen::SoundKind kind, size_t index, float wildness) {
            double sum = 0;
            for (uint32_t seed = 0; seed < 200; ++seed)
                sum += gen::generatePatch (kind, seed, wildness)[index];
            return sum / 200.0;
        };
        for (float w : { 0.0f, 0.5f, 1.0f })
        {
            const std::string at = " (wildness " + std::to_string (w) + ")";
            check (average (gen::SoundKind::pad, attack, w) > 10.0 * average (gen::SoundKind::bass, attack, w), "pad attack > bass attack" + at);
            check (average (gen::SoundKind::pad, release, w) > average (gen::SoundKind::pluck, release, w), "pad release > pluck release" + at);
            check (average (gen::SoundKind::pad, unison, w) >= 4.0, "pads use unison" + at);
            check (average (gen::SoundKind::bass, mono, w) == 1.0 && average (gen::SoundKind::bass, sub, w) == 1.0, "bass is mono with a sub" + at);
            check (average (gen::SoundKind::sub, glide, w) > 0.0, "sub glides" + at);
            check (average (gen::SoundKind::pluck, sustain, w) == 0.0 && average (gen::SoundKind::bell, sustain, w) == 0.0, "plucks and bells have no sustain" + at);
        }
        // with wildness 0 the archetype ranges are exact
        bool padRange = true;
        for (uint32_t seed = 0; seed < 200; ++seed)
        {
            const float a = gen::generatePatch (gen::SoundKind::pad, seed, 0.0f)[attack];
            padRange = padRange && a >= 0.3f && a <= 2.0f;
        }
        check (padRange, "pad attack 0.3..2 s at wildness 0");
        check (gen::generatePatch (gen::SoundKind::lead, 9, 0.0f) != gen::generatePatch (gen::SoundKind::lead, 9, 1.0f), "wildness changes the patch");
    });

    test ("patches: values convert to the synth parameters and back", [] {
        for (const auto& preset : synth::factoryPresets())
        {
            const auto values = gen::valuesFromParams (preset.params);
            check (gen::valuesFromParams (gen::paramsFromValues (values)) == values, "round trip of " + preset.name);
        }
        const auto p = gen::paramsFromValues ({});
        check (gen::valuesFromParams (p) == gen::valuesFromParams (synth::PrismaParams()), "an empty vector gives the defaults");
        std::vector<float> wild (synth::parameterDescriptors().size(), 1.0e9f);
        const auto clamped = gen::valuesFromParams (gen::paramsFromValues (wild));
        bool inside = true;
        for (size_t i = 0; i < clamped.size(); ++i)
            inside = inside && clamped[i] <= synth::parameterDescriptors()[i].max;
        check (inside, "out-of-range values are clamped");
    });

    test ("patches: names are two words, stable and varied", [] {
        for (auto kind : gen::allSoundKinds())
        {
            std::set<std::string> names;
            bool twoWords = true;
            for (uint32_t seed = 0; seed < 100; ++seed)
            {
                const auto name = gen::patchName (kind, seed);
                const auto space = name.find (' ');
                twoWords = twoWords && space != std::string::npos && space > 0 && space + 1 < name.size()
                           && name.find (' ', space + 1) == std::string::npos && name.substr (0, space) != name.substr (space + 1);
                names.insert (name);
            }
            const std::string k = gen::soundKindKey (kind);
            check (twoWords, k + ": two different words");
            check (names.size() >= 50, k + ": varied names (" + std::to_string (names.size()) + " of 100)");
            check (gen::patchName (kind, 17) == gen::patchName (kind, 17), k + ": the same seed gives the same name");
        }
    });

    test ("patches: every kind renders finite, audible and below 0 dBFS in the core synth", [] {
        constexpr double sampleRate = 44100.0;
        constexpr int block = 256, seconds = 1;
        float loudest = 0.0f;
        std::string loudestWhat;
        for (auto kind : gen::allSoundKinds())
            for (uint32_t seed = 1; seed <= 12; ++seed)
                for (float wildness : { 0.0f, 0.5f, 1.0f })
                {
                    synth::PrismaSynth s;
                    s.prepare (sampleRate, block);
                    s.setTempo (120.0);
                    s.setParams (gen::paramsFromValues (gen::generatePatch (kind, seed, wildness)));

                    // Poly kinds play a triad, as they will in a clip; bass, sub and lead one note.
                    std::vector<int> pitches;
                    if (kind == gen::SoundKind::bass || kind == gen::SoundKind::sub)
                        pitches = { 36 };
                    else if (kind == gen::SoundKind::lead || kind == gen::SoundKind::fx)
                        pitches = { 64 };
                    else
                        pitches = { 60, 64, 67 };

                    std::vector<float> l (block), r (block);
                    std::vector<MidiEvent> on, off;
                    for (int p : pitches)
                    {
                        on.push_back (MidiEvent::noteOn (0, p, 110));
                        off.push_back (MidiEvent::noteOff (0, p));
                    }
                    const int blocks = (int) (seconds * sampleRate / block);
                    float peak = 0.0f;
                    bool finite = true;
                    double energy = 0.0;
                    for (int b = 0; b < blocks; ++b)
                    {
                        const auto* events = b == 0 ? &on : (b == blocks * 3 / 4 ? &off : nullptr);
                        s.process (l.data(), r.data(), block, events != nullptr ? events->data() : nullptr, events != nullptr ? (int) events->size() : 0);
                        for (int i = 0; i < block; ++i)
                        {
                            finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                            peak = std::max (peak, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
                            energy += (double) l[(size_t) i] * l[(size_t) i];
                        }
                    }
                    const auto what = std::string (gen::soundKindKey (kind)) + " seed " + std::to_string (seed) + " wildness "
                                      + std::to_string (wildness) + " peak " + std::to_string (peak);
                    check (finite, what + ": finite");
                    check (peak > 0.01f && energy > 1.0e-3, what + ": audible");
                    check (peak < 1.0f, what + ": below 0 dBFS");
                    if (peak > loudest)
                    {
                        loudest = peak;
                        loudestWhat = what;
                    }
                }
        std::printf ("  loudest: %s\n", loudestWhat.c_str());
    });

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
