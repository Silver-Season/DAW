#pragma once

#include "daw/engine/Clip.h"
#include "daw/music/Scales.h"

#include <cstdint>
#include <string>
#include <vector>

// Pattern generators for "Inspiration": chord progressions, bass lines, melodies, arpeggios and drum grooves as
// plain notes that the app can drop into a MIDI clip. Everything is a pure function of its arguments, so the same
// seed always gives the same notes on every platform (the generators use their own random number generator, not
// the standard distributions, whose output differs between standard libraries).
//
// Times are in beats from the start of the clip; every note lies inside [0, bars * beatsPerBar). Pitches use the
// convention 60 = C4. All pitches belong to the scale, except chromatic passing tones in the melody when
// GenContext::allowChromatic is set.
namespace daw::gen
{

using Notes = std::vector<engine::Note>;

struct GenContext
{
    int root = 0;                               // pitch class of the tonic, 0 = C .. 11 = B
    const music::Scale* scale = nullptr;        // nullptr = major
    double beatsPerBar = 4.0;
    int bars = 4;
    uint32_t seed = 1;
    double density = 0.5;                       // 0..1: how many notes
    double complexity = 0.5;                    // 0..1: syncopation, sevenths, ghost notes, faster subdivisions
    std::string style = "pop";                  // one of styleNames(); unknown styles fall back to "pop"
    bool allowChromatic = false;                // lets the melody use chromatic passing tones
};

// The styles the generators know, in menu order.
const std::vector<std::string>& styleNames();

// A chord progression, one chord per bar or per half bar, voiced around C3..C5 with close voice leading.
// `romanDegreesOut` receives the scale degree (0-based index into the scale, 0 = tonic) of every chord root, one
// entry per chord. The chords split the clip into equal spans: chord i covers
// [i * span, (i + 1) * span) with span = bars * beatsPerBar / romanDegrees.size(). The other generators take this
// list and rely on the same rule, so they follow the progression.
Notes chords (const GenContext& ctx, std::vector<int>* romanDegreesOut = nullptr);

// A bass line in C1..C2 (roots; octave jumps may reach C3) that follows the chord roots with a rhythm typical
// for the style. The first note of every chord span is the chord root. Trap lines contain overlapping long notes
// meant as 808 slides on a mono synth with glide.
Notes bass (const GenContext& ctx, const std::vector<int>& chordDegrees);

// A motif-based melody (motif, varied repeat, answer phrase) in C4..G5. Notes on whole beats are chord tones.
Notes melody (const GenContext& ctx, const std::vector<int>& chordDegrees);

enum class ArpPattern
{
    up,
    down,
    upDown,
    random
};

// Arpeggiates the chords over two octaves from about C4. stepBeats is the rate: 0.5 = 1/8, 0.25 = 1/16.
Notes arp (const GenContext& ctx, const std::vector<int>& chordDegrees, ArpPattern pattern = ArpPattern::up,
           double stepBeats = 0.25);

// General MIDI drum notes, used by drums().
namespace drum
{
    constexpr int kick = 36, rim = 37, snare = 38, clap = 39, closedHat = 42, openHat = 46, crash = 49;
    constexpr int tomHigh = 50, tomMid = 47, tomLow = 45;
} // namespace drum

// A drum groove for the style on the General MIDI map, with a fill in the last bar when there are two bars or more.
Notes drums (const GenContext& ctx);

// Moves every note by up to +-timingBeats and its velocity by up to +-velocityRange (kept in 1..127). Starts never
// go below 0; with clipLengthBeats > 0 notes also stay inside [0, clipLengthBeats).
Notes humanise (Notes notes, double timingBeats, int velocityRange, uint32_t seed, double clipLengthBeats = 0.0);

} // namespace daw::gen
