// Plain C++ tests for the framework-free core: run with `ctest` or directly.
#include "daw/dsp/FFT.h"
#include "daw/dsp/Filter.h"
#include "daw/dsp/Envelope.h"
#include "daw/dsp/Wavetable.h"
#include "daw/dsp/WavetableOscillator.h"
#include "daw/engine/Capture.h"
#include "daw/engine/Clip.h"
#include "daw/engine/Recording.h"
#include "daw/engine/Quantize.h"
#include "daw/music/Scales.h"
#include "daw/project/VersionHistory.h"
#include "daw/synth/PrismaSynth.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <numbers>
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
        std::printf ("  FAIL: %s\n", what.c_str());
    }
}

void test (const char* name, const std::function<void()>& fn)
{
    std::printf ("[test] %s\n", name);
    fn();
}

double rms (const std::vector<float>& x, size_t from = 0)
{
    double s = 0;
    for (size_t i = from; i < x.size(); ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / (double) std::max<size_t> (1, x.size() - from));
}

using namespace daw;
} // namespace

int main()
{
    test ("wavetable mip levels are band-limited", [] {
        auto wt = dsp::factory::table (0); // Basic Shapes, saw at the 2/3 position
        const int frame = (int) std::lround (0.66 * (wt->numFrames() - 1));
        for (int mip : { 0, 3, 6, 9 })
        {
            const int N = dsp::Wavetable::kFrameSize;
            std::vector<std::complex<double>> bins (N);
            const float* d = wt->data (frame, mip);
            for (int i = 0; i < N; ++i)
                bins[(size_t) i] = d[i];
            dsp::fft (bins, false);
            const int limit = (N / 2) >> mip;
            double inBand = 0, outBand = 0;
            for (int h = 1; h < N / 2; ++h)
                (h <= limit ? inBand : outBand) += std::norm (bins[(size_t) h]);
            check (outBand < inBand * 1e-10, "mip " + std::to_string (mip) + " leaks harmonics above " + std::to_string (limit));
        }
        check (dsp::Wavetable::mipForIncrement (440.0 / 48000.0) >= 4, "mip selection for A4");
        check (dsp::Wavetable::mipForIncrement (20.0 / 48000.0) == 0, "mip selection for 20 Hz");
    });

    test ("every factory table builds and is normalised", [] {
        for (int i = 0; i < (int) dsp::factory::tableNames().size(); ++i)
        {
            auto wt = dsp::factory::table (i);
            float peak = 0;
            for (int f = 0; f < wt->numFrames(); ++f)
                for (int s = 0; s < dsp::Wavetable::kFrameSize; ++s)
                    peak = std::max (peak, std::abs (wt->data (f, 0)[s]));
            check (wt->numFrames() == 32 && peak > 0.99f && peak < 1.01f, "table " + wt->name());
        }
    });

    test ("oscillator pitch is correct", [] {
        dsp::WavetableOscillator osc;
        auto wt = dsp::factory::table (0);
        osc.setTable (wt.get());
        osc.reset (0, 0, 1);
        osc.setUnison (1, 0, 0, 0);
        const double sr = 48000, f = 1000;
        int crossings = 0;
        float prev = 0;
        for (int i = 0; i < 48000; ++i)
        {
            float l, r;
            osc.tick (f, 0.0f, sr, l, r); // pure sine frame
            if (prev < 0 && l >= 0)
                ++crossings;
            prev = l;
        }
        check (std::abs (crossings - 1000) <= 1, "1 kHz gives " + std::to_string (crossings) + " cycles/s");
    });

    test ("SVF low-pass attenuates above cutoff", [] {
        auto measure = [] (dsp::FilterType type, float freq) {
            dsp::SynthFilter flt;
            flt.set (type, 500.0f, 0.0f, 0.0f, 48000.0f);
            std::vector<float> y (48000);
            for (int i = 0; i < 48000; ++i)
                y[(size_t) i] = flt.process (std::sin (2.0f * std::numbers::pi_v<float> * freq * (float) i / 48000.0f));
            return 20.0 * std::log10 (rms (y, 4800) / (1.0 / std::sqrt (2.0)));
        };
        const double pass = measure (dsp::FilterType::LowPass24, 100.0f);
        const double stop = measure (dsp::FilterType::LowPass24, 8000.0f);
        const double hpStop = measure (dsp::FilterType::HighPass12, 50.0f);
        std::printf ("    LP24 @100Hz %.2f dB, @8kHz %.2f dB, HP12 @50Hz %.2f dB\n", pass, stop, hpStop);
        check (pass > -1.5, "pass band near 0 dB");
        check (stop < -80.0, "LP24 two octaves+ above cutoff below -80 dB");
        check (hpStop < -30.0, "HP12 attenuates below cutoff");
    });

    test ("envelope ADSR shape", [] {
        dsp::Envelope env;
        env.setParameters (0.01f, 0.1f, 0.5f, 0.2f, 48000.0f);
        env.noteOn();
        float v = env.advance (480);
        check (v > 0.99f, "attack reaches 1 after attack time");
        v = env.advance (48000);
        check (std::abs (v - 0.5f) < 1e-3f && env.getStage() == dsp::Envelope::Stage::Sustain, "decays to sustain");
        env.noteOff();
        env.advance (48000 / 5 + 10);
        check (! env.isActive(), "release ends after release time");
    });

    test ("synth renders, stays bounded, releases", [] {
        synth::PrismaSynth s;
        s.prepare (48000, 512);
        s.setParams (synth::factoryPresets()[1].params); // Supersaw Lead
        std::vector<float> l (512), r (512);
        std::vector<MidiEvent> ev { MidiEvent::noteOn (0, 60, 110), MidiEvent::noteOn (0, 64, 110), MidiEvent::noteOn (0, 67, 110) };
        float peak = 0;
        double energy = 0;
        for (int b = 0; b < 100; ++b)
        {
            s.process (l.data(), r.data(), 512, b == 0 ? ev.data() : nullptr, b == 0 ? 3 : 0);
            for (int i = 0; i < 512; ++i)
            {
                peak = std::max (peak, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
                energy += l[(size_t) i] * l[(size_t) i];
            }
        }
        check (energy > 1.0, "produces sound");
        check (peak < 2.0f, "peak bounded (" + std::to_string (peak) + ")");
        check (s.getNumActiveVoices() == 3, "three voices playing");

        std::vector<MidiEvent> off { MidiEvent::noteOff (0, 60), MidiEvent::noteOff (0, 64), MidiEvent::noteOff (0, 67) };
        s.process (l.data(), r.data(), 512, off.data(), 3);
        for (int b = 0; b < 200; ++b)
            s.process (l.data(), r.data(), 512, nullptr, 0);
        check (s.getNumActiveVoices() == 0, "voices free after release");
    });

    test ("every preset renders without NaN", [] {
        for (const auto& preset : synth::factoryPresets())
        {
            synth::PrismaSynth s;
            s.prepare (44100, 256);
            s.setParams (preset.params);
            std::vector<float> l (256), r (256);
            auto on = MidiEvent::noteOn (10, 48, 100);
            bool finite = true;
            double energy = 0;
            for (int b = 0; b < 200; ++b)
            {
                s.process (l.data(), r.data(), 256, b == 0 ? &on : nullptr, b == 0 ? 1 : 0);
                for (float x : l)
                {
                    finite = finite && std::isfinite (x);
                    energy += x * x;
                }
            }
            check (finite && energy > 0.01, "preset " + preset.name);
        }
    });

    test ("polyphony limit and voice stealing", [] {
        synth::PrismaSynth s;
        s.prepare (48000, 256);
        auto p = synth::PrismaParams();
        p.polyphony = 8;
        s.setParams (p);
        std::vector<MidiEvent> ev;
        for (int n = 0; n < 40; ++n)
            ev.push_back (MidiEvent::noteOn (n, 30 + n, 100));
        std::vector<float> l (256), r (256);
        s.process (l.data(), r.data(), 256, ev.data(), (int) ev.size());
        check (s.getNumActiveVoices() == 8, "8 voices max, got " + std::to_string (s.getNumActiveVoices()));
    });

    test ("mono legato keeps one voice", [] {
        synth::PrismaSynth s;
        s.prepare (48000, 256);
        auto p = synth::PrismaParams();
        p.mono = 1;
        p.glide = 0.1f;
        s.setParams (p);
        std::vector<MidiEvent> ev { MidiEvent::noteOn (0, 60, 100), MidiEvent::noteOn (100, 67, 100), MidiEvent::noteOff (200, 67) };
        std::vector<float> l (256), r (256);
        s.process (l.data(), r.data(), 256, ev.data(), 3);
        check (s.getNumActiveVoices() == 1, "one voice in mono mode");
    });

    test ("clip launch is quantised and loops without stuck notes", [] {
        engine::MidiClip clip;
        clip.lengthBeats = 4;
        for (int i = 0; i < 4; ++i)
            clip.notes.push_back ({ (double) i, 0.5, 60 + i, 100 });

        engine::ClipPlayer player;
        engine::Transport transport;
        transport.prepare (48000);
        transport.setTempo (120);
        transport.play();

        std::vector<MidiEvent> out;
        int ons = 0, offs = 0, firstOnBlock = -1;
        for (int b = 0; b < 800; ++b) // 800 blocks of 480 samples = 8000 ms = 16 beats
        {
            auto blk = transport.advance (480);
            if (b == 30) // beat 0.6: should start at beat 4 (bar quantise)
                player.launch (0, &clip, blk.startBeat, 4.0);
            if (b == 500) // beat 10: stop at beat 12
                player.stop (blk.startBeat, 4.0);
            out.clear();
            player.process (blk.startBeat, blk.endBeat, transport.samplesPerBeat(), 480, out);
            for (auto& e : out)
            {
                if ((e.status & 0xF0) == 0x90)
                {
                    ++ons;
                    if (firstOnBlock < 0)
                        firstOnBlock = b;
                }
                else
                    ++offs;
            }
        }
        check (firstOnBlock == 200, "first note at beat 4 (block 200), got block " + std::to_string (firstOnBlock));
        check (ons == 8, "8 note-ons over two loops, got " + std::to_string (ons));
        check (offs == ons, "every note-on has a note-off");
        check (player.playingSlot() == engine::ClipPlayer::kNoClip, "stopped");
    });

    test ("long note across the loop point and stop releases it", [] {
        engine::MidiClip clip;
        clip.lengthBeats = 2;
        clip.notes.push_back ({ 1.0, 4.0, 50, 100 }); // longer than the clip: truncated at the loop end
        engine::ClipPlayer player;
        std::vector<MidiEvent> out;
        player.launch (0, &clip, 0.0, 0.0);
        int balance = 0;
        double beat = 0;
        for (int b = 0; b < 100; ++b, beat += 0.05)
        {
            if (b == 70)
                player.stop (beat, 0.0);
            out.clear();
            player.process (beat, beat + 0.05, 24000, 1200, out);
            for (auto& e : out)
                balance += ((e.status & 0xF0) == 0x90) ? 1 : -1;
            check (balance >= 0 && balance <= 1, "at most one note held");
        }
        check (balance == 0, "no hanging note after stop");
    });

    test ("sortEvents is stable, puts note-offs first and works without allocating", [] {
        std::vector<MidiEvent> ev;
        ev.reserve (8);
        ev.push_back (MidiEvent::noteOn (10, 60, 100));
        ev.push_back (MidiEvent::noteOn (5, 64, 100));
        ev.push_back (MidiEvent::noteOn (5, 67, 100));
        ev.push_back (MidiEvent::noteOff (10, 62));
        ev.push_back (MidiEvent::noteOff (5, 59));
        const auto* before = ev.data();
        engine::sortEvents (ev);
        check (ev.data() == before, "sorted in place");
        check (ev[0].sampleOffset == 5 && ev[0].status == 0x80 && ev[0].data1 == 59, "note-off first at equal offsets");
        check (ev[1].data1 == 64 && ev[2].data1 == 67, "equal events keep their order");
        check (ev[3].sampleOffset == 10 && ev[3].status == 0x80 && ev[4].data1 == 60, "later offsets follow");
    });

    test ("relocate keeps a playing clip going when its slot index moves", [] {
        engine::MidiClip clip;
        clip.lengthBeats = 4;
        clip.notes.push_back ({ 0.0, 1.0, 60, 100 });
        engine::ClipPlayer player;
        std::vector<MidiEvent> out;
        player.launch (2, &clip, 0.0, 0.0);
        player.process (0.0, 0.5, 24000, 12000, out);
        check (player.playingSlot() == 2 && player.playingClipPtr() == &clip, "playing slot 2");
        player.relocate (3, engine::ClipPlayer::kNoClip);
        check (player.playingSlot() == 3 && player.playingClipPtr() == &clip, "now slot 3, same clip");
        out.clear();
        player.process (0.5, 1.5, 24000, 24000, out);
        int ons = 0;
        for (auto& e : out)
            ons += ((e.status & 0xF0) == 0x90) ? 1 : 0;
        check (ons == 0, "no restart after relocation");
        player.clipChanged (3, nullptr);
        check (player.playingSlot() == engine::ClipPlayer::kNoClip, "a deleted clip stops the player");
    });

    test ("version history thinning (test 9): 300 autosaves over 5 days, 5 saves, 2 labels", [] {
        using namespace project;
        constexpr int64_t kStep = 24LL * 60 * 1000; // 24 minutes: 300 steps cover exactly 5 days
        const int64_t t0 = 1'700'000'000'000LL;
        std::vector<VersionInfo> all;
        int number = 1;
        for (int i = 0; i < 300; ++i)
            all.push_back ({ number++, t0 + i * kStep, VersionKind::autosave, i == 100 || i == 101 });
        for (int i = 0; i < 5; ++i)
            all.push_back ({ number++, t0 + (i * 60 + 7) * kStep, VersionKind::save, false });
        all.push_back ({ number++, t0 + 150 * kStep + 1, VersionKind::restore, false });

        const auto removed = versionsToPrune (all);
        std::set<int> gone (removed.begin(), removed.end());
        check (gone.size() == removed.size(), "no version is listed twice");

        std::vector<VersionInfo> kept;
        for (const auto& v : all)
            if (! gone.count (v.number))
                kept.push_back (v);

        int saves = 0, labelled = 0, restores = 0, recent = 0, hourly = 0;
        std::set<int64_t> hours;
        bool hourUnique = true;
        for (const auto& v : kept)
        {
            if (v.kind == VersionKind::save)
                ++saves;
            else if (v.kind == VersionKind::restore)
                ++restores;
            else if (v.labelled)
                ++labelled;
            else if (v.timeMs >= t0 + (300 - kRecentAutosavesKept) * kStep)
                ++recent;
            else
            {
                ++hourly;
                hourUnique = hourUnique && hours.insert (v.timeMs / 3'600'000LL).second;
            }
        }
        check (saves == 5 && restores == 1 && labelled == 2, "every save, restore and labelled version is kept");
        check (recent == kRecentAutosavesKept, "the newest 50 autosaves are kept one by one");
        check (hourUnique, "older autosaves: at most one per clock hour");

        // expected count of hourly ones, counted independently: distinct hours among the older, unlabelled autosaves
        std::set<int64_t> expectedHours;
        for (int i = 0; i < 300 - kRecentAutosavesKept; ++i)
            if (i != 100 && i != 101)
                expectedHours.insert ((t0 + i * kStep) / 3'600'000LL);
        check (hourly == (int) expectedHours.size(), "one autosave for every hour that has any (" + std::to_string (hourly) + " of "
                                                         + std::to_string (expectedHours.size()) + ")");
        check ((int) kept.size() == 5 + 1 + 2 + 50 + (int) expectedHours.size(), "total of the kept list");
        // the newest autosave of each older hour is the one that stays
        bool newestOfHour = true;
        for (const auto& v : kept)
            if (v.kind == VersionKind::autosave && ! v.labelled && v.timeMs < t0 + (300 - kRecentAutosavesKept) * kStep)
                for (const auto& w : all)
                    if (w.kind == VersionKind::autosave && ! w.labelled && w.timeMs < t0 + (300 - kRecentAutosavesKept) * kStep
                        && w.timeMs / 3'600'000LL == v.timeMs / 3'600'000LL && w.timeMs > v.timeMs)
                        newestOfHour = false;
        check (newestOfHour, "the newest autosave of an hour is the one that stays");

        // pruning again changes nothing, and a short list is untouched
        check (versionsToPrune (kept).empty(), "pruning is idempotent");
        check (versionsToPrune ({}).empty(), "empty list");
    });

    //==========================================================================
    // docs/spec/02-midi-recording.md, task 2.4 / 2.7
    test ("quantise: strength, swing, triplets, ends (spec test 6)", [] {
        using engine::Note;
        const double sixteenth = 0.25, eighth = 0.5;

        // strength 100 % puts the start on the grid, the length stays
        {
            const Note n { 1.13, 0.4, 60, 100 };
            const auto q = engine::quantizeNote (n, sixteenth, 1.0, 0.0, false);
            check (std::abs (q.start - 1.25) < 1e-9 && std::abs (q.length - 0.4) < 1e-9, "100 % lands on the 1/16 line and keeps the length");
        }
        // 50 % goes half way
        {
            const Note n { 1.13, 0.4, 60, 100 };
            const auto q = engine::quantizeNote (n, sixteenth, 0.5, 0.0, false);
            check (std::abs (q.start - (1.13 + 0.5 * (1.25 - 1.13))) < 1e-9, "50 % goes half way to the line");
            check (std::abs (engine::quantizeNote (n, sixteenth, 0.0, 0.0, false).start - 1.13) < 1e-12, "0 % changes nothing");
        }
        // a note before the line goes back to it
        check (std::abs (engine::quantizeNote ({ 2.6, 1, 60, 100 }, eighth, 1.0, 0.0, false).start - 2.5) < 1e-9, "snaps backwards too");

        // swing 50 % moves the second eighths by a thirty-second (0.125 beat); the first ones stay
        {
            std::vector<Note> notes { { 0.0, 0.25, 60, 100 }, { 0.5, 0.25, 60, 100 }, { 1.0, 0.25, 60, 100 }, { 1.5, 0.25, 60, 100 } };
            const auto q = engine::quantizeNotes (notes, eighth, 1.0, 0.5, false);
            check (std::abs (q[0].start - 0.0) < 1e-9 && std::abs (q[2].start - 1.0) < 1e-9, "first eighths stay on the beat");
            check (std::abs (q[1].start - 0.625) < 1e-9 && std::abs (q[3].start - 1.625) < 1e-9, "second eighths move by 1/32 at 50 % swing");
            // a note played on the swung position stays there
            check (std::abs (engine::quantizeNote ({ 0.62, 0.2, 60, 100 }, eighth, 1.0, 0.5, false).start - 0.625) < 1e-9, "the swung line is the target");
        }
        // triplets are counted from the bar: 1/8 triplet = 1/3 beat
        {
            const double t = 1.0 / 3.0;
            check (std::abs (engine::quantizeNote ({ 1.30, 0.2, 60, 100 }, t, 1.0, 0.0, false).start - 4.0 / 3.0) < 1e-9, "1/8T line 4 is at 4/3 beat");
            check (std::abs (engine::quantizeNote ({ 3.4, 0.2, 60, 100 }, t, 1.0, 0.0, false).start - 10.0 / 3.0) < 1e-9, "1/8T line 10 is at 10/3 beat");
            check (std::abs (engine::quantizeNote ({ 3.99, 0.2, 60, 100 }, t, 1.0, 0.0, false).start - 4.0) < 1e-9, "the bar line is a triplet line too");
        }
        // ends
        {
            const auto q = engine::quantizeNote ({ 1.13, 0.4, 60, 100 }, sixteenth, 1.0, 0.0, true);
            check (std::abs (q.start - 1.25) < 1e-9 && std::abs (q.start + q.length - 1.5) < 1e-9, "the end goes to its own line");
            const auto tiny = engine::quantizeNote ({ 1.02, 0.03, 60, 100 }, sixteenth, 1.0, 0.0, true);
            check (tiny.length > 0.0, "a note never collapses to nothing");
            check (engine::quantizeNote ({ 1.1, 0.4, 61, 77 }, sixteenth, 1.0, 0.0, true).pitch == 61, "pitch and velocity are untouched");
        }
        check (engine::quantizeNotes ({}, eighth, 1.0, 0.0, false).empty(), "empty list");
    });

    test ("scales: inScale, snapToScale, degreeToNote (spec test 13)", [] {
        using namespace music;
        const Scale* major = findScale ("major");
        const Scale* minor = findScale ("minor");
        const Scale* penta = findScale ("minorPentatonic");
        check (major != nullptr && minor != nullptr && penta != nullptr, "scales are found by key");
        check (findScale ("") == nullptr && findScale ("nonsense") == nullptr, "no scale for \"\" and unknown keys");
        check (allScales().size() == 13, "twelve scales and the chromatic one");

        // inScale: C major has only white keys
        for (int n = 48; n < 72; ++n)
        {
            const int pc = n % 12;
            const bool white = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
            check (inScale (n, 0, *major) == white, "C major membership of " + std::to_string (n));
        }
        check (inScale (66, 6, *major) && inScale (70, 6, *major) && ! inScale (67, 6, *major), "F# major: F# A# in, G out");
        check (inScale (57, 9, *minor) && inScale (60, 9, *minor) && ! inScale (61, 9, *minor), "A minor");
        check (inScale (-12, 0, *major) && ! inScale (-11, 0, *major), "negative notes work");

        // snapToScale
        check (snapToScale (60, 0, *major, 1) == 60 && snapToScale (60, 0, *major, -1) == 60 && snapToScale (60, 0, *major, 0) == 60, "an in-scale note stays");
        check (snapToScale (61, 0, *major, 1) == 62 && snapToScale (61, 0, *major, -1) == 60, "C# goes up to D or down to C");
        check (snapToScale (61, 0, *major, 0) == 60, "a tie goes down");
        check (snapToScale (66, 0, *major, 0) == 65 && snapToScale (68, 0, *major, 0) == 67, "nearest note, F# -> F, G# -> G");
        check (snapToScale (70, 0, *major, 0) == 69, "A# -> A in C major");
        check (snapToScale (71, 0, *penta, 1) == 72 && snapToScale (71, 0, *penta, -1) == 70, "C minor pentatonic around B");
        check (snapToScale (11, 0, *major, 1) == 11 && snapToScale (13, 0, *major, 1) == 14, "low notes");
        check (snapToScale (-1, 0, *major, -1) == -1 && snapToScale (-2, 0, *major, 1) == -1, "below zero");

        // +1 in C major moves E to F and F to G (spec test 15, the musical half)
        check (snapToScale (64 + 1, 0, *major, 1) == 65, "E + 1 step = F");
        check (snapToScale (65 + 1, 0, *major, 1) == 67, "F + 1 step = G");
        check (snapToScale (65 - 1, 0, *major, -1) == 64, "F - 1 step = E");
        check (snapToScale (60 - 1, 0, *major, -1) == 59, "C - 1 step = B");

        // degreeToNote
        const int cMajor[] { 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83, 84 };
        for (int d = 0; d < 15; ++d)
            check (degreeToNote (d, 0, *major, 60) == cMajor[d], "C major degree " + std::to_string (d));
        check (degreeToNote (-1, 0, *major, 60) == 59 && degreeToNote (-7, 0, *major, 60) == 48 && degreeToNote (-8, 0, *major, 60) == 47, "negative degrees");
        const int aMinor[] { 57, 59, 60, 62, 64, 65, 67, 69 };
        for (int d = 0; d < 8; ++d)
            check (degreeToNote (d, 9, *minor, 60) == aMinor[d], "A minor degree " + std::to_string (d) + " (tonic at or below the base note)");
        const int cPenta[] { 60, 63, 65, 67, 70, 72, 75, 77, 79, 82, 84 };
        for (int d = 0; d < 11; ++d)
            check (degreeToNote (d, 0, *penta, 60) == cPenta[d], "C minor pentatonic degree " + std::to_string (d));
        check (degreeToNote (5, 2, *major, 62) == 71 && degreeToNote (2, 2, *major, 64) == 66, "D major, base octave from the base note");
        check (degreeToNote (0, 0, *major, 71) == 60, "the base note picks the octave of the tonic below it");
        check (notesPerOctave (*major) == 7 && notesPerOctave (*penta) == 5, "notes per octave");
    });

    test ("Prisma follows tuneA4 (spec test 14)", [] {
        // A pure sine (first frame of the Basic Shapes table), one voice, no detune, no filter: the spectrum
        // must have its peak at the tuned frequency.
        const double sr = 48000.0;
        const auto measure = [&] (float a4) {
            synth::PrismaSynth s;
            s.prepare (sr, 512);
            auto p = synth::PrismaParams();
            p.osc[0].on = 1;
            p.osc[0].table = 0;
            p.osc[0].wtPos = 0.0f;
            p.osc[0].octave = 0;
            p.osc[0].semi = 0;
            p.osc[0].fine = 0;
            p.osc[0].level = 1.0f;
            p.osc[0].unison = 1;
            p.osc[0].detune = 0;
            p.osc[1].on = 0;
            p.subOn = 0;
            p.noiseLevel = 0;
            p.filterOn = 0;
            p.env[0] = { 0.001f, 0.1f, 1.0f, 0.1f };
            for (auto& m : p.mod)
                m.amount = 0;
            s.setParams (p);
            s.setTuneA4 (a4);
            const int n = 1 << 16;
            std::vector<float> l ((size_t) n), r ((size_t) n);
            const auto on = MidiEvent::noteOn (0, 69, 100);
            for (int i = 0; i < n; i += 512)
                s.process (l.data() + i, r.data() + i, 512, i == 0 ? &on : nullptr, i == 0 ? 1 : 0);
            std::vector<std::complex<double>> bins ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n); // Hann window
                bins[(size_t) i] = w * l[(size_t) i];
            }
            dsp::fft (bins, false);
            size_t best = 1;
            for (size_t k = 1; k < (size_t) n / 2; ++k)
                if (std::abs (bins[k]) > std::abs (bins[best]))
                    best = k;
            // parabolic interpolation of the peak
            const double a = std::log (std::abs (bins[best - 1])), b = std::log (std::abs (bins[best])), c = std::log (std::abs (bins[best + 1]));
            const double offset = 0.5 * (a - c) / (a - 2.0 * b + c);
            return ((double) best + offset) * sr / n;
        };
        const double f432 = measure (432.0f), f440 = measure (440.0f), f415 = measure (415.0f);
        std::printf("    A4=432 -> %.3f Hz, 440 -> %.3f Hz, 415 -> %.3f Hz\n", f432, f440, f415);
        check (std::abs (f432 - 432.0) < 0.5, "tuneA4 = 432 gives 432 +- 0.5 Hz");
        check (std::abs (f440 - 440.0) < 0.5, "the default stays at 440 Hz");
        check (std::abs (f415 - 415.0) < 0.5, "415 Hz (baroque pitch)");
    });

    test ("clips: start point, loop brace and one-shots", [] {
        const auto onsOf = [] (const engine::MidiClip& clip, double fromBeat, double toBeat) {
            // renders in blocks of 0.25 beat at 100 samples per beat; returns (beat, pitch) of each note-on
            std::vector<std::pair<double, int>> out;
            std::vector<MidiEvent> ev;
            for (double a = fromBeat; a < toBeat - 1e-9; a += 0.25)
            {
                ev.clear();
                engine::renderClip (clip, 0.0, a, a + 0.25, 100.0, 25, ev);
                for (const auto& e : ev)
                    if ((e.status & 0xF0) == 0x90)
                        out.emplace_back (a + e.sampleOffset / 100.0, e.data1);
            }
            return out;
        };

        engine::MidiClip clip;
        clip.lengthBeats = 4;
        for (int i = 0; i < 4; ++i)
            clip.notes.push_back ({ (double) i, 0.5, 60 + i, 100 });

        // default: loops the whole clip
        auto a = onsOf (clip, 0, 8);
        check (a.size() == 8 && a[4].second == 60 && std::abs (a[4].first - 4.0) < 0.011, "defaults loop 0..4");

        // start point inside the clip: the first pass starts there, later passes start at the brace
        clip.startOffset = 2.0;
        a = onsOf (clip, 0, 6);
        check (a.size() == 6 && a[0].second == 62 && std::abs (a[0].first - 0.0) < 0.011, "plays from the start point");
        check (a[2].second == 60 && std::abs (a[2].first - 2.0) < 0.011, "then the whole brace repeats from its own start");

        // loop brace 1..3 repeats only the notes inside
        clip.startOffset = 0.0;
        clip.loopStart = 1.0;
        clip.lengthBeats = 3.0;
        a = onsOf (clip, 0, 7);
        // pass 0 = notes 0,1,2 at beats 0,1,2; then the brace [1,3) every 2 beats: 1 at 3, 2 at 4, 1 at 5, 2 at 6
        check (a.size() == 7, "brace plays " + std::to_string (a.size()) + " notes");
        check (a.size() == 7 && a[3].second == 61 && std::abs (a[3].first - 3.0) < 0.011 && a[4].second == 62 && a[5].second == 61, "the brace repeats");
        check (std::abs (clip.positionAfter (0.5) - 0.5) < 1e-9 && std::abs (clip.positionAfter (3.5) - 1.5) < 1e-9, "position inside the content");

        // loop off: one pass, then the player lets go of the clip
        clip.loop = false;
        clip.loopStart = 0.0;
        clip.lengthBeats = 2.0;
        engine::ClipPlayer player;
        std::vector<MidiEvent> out;
        player.launch (0, &clip, 0.0, 0.0);
        int ons = 0, offs = 0;
        double beat = 0.0;
        for (int i = 0; i < 40; ++i, beat += 0.25)
        {
            out.clear();
            player.process (beat, beat + 0.25, 100.0, 25, out);
            for (const auto& e : out)
                ((e.status & 0xF0) == 0x90 ? ons : offs)++;
        }
        check (ons == 2 && offs == 2, "a one-shot clip plays its notes once");
        check (player.playingSlot() == engine::ClipPlayer::kNoClip, "and ends after the first pass");
    });


    test ("Note assembly: earliest open note closes, hanging notes close at the end", [] {
        using namespace daw::engine;
        NoteAssembler a;
        a.noteOn (0.0, 60, 100);
        a.noteOn (1.0, 60, 90);   // the same pitch again, before the first one ended
        a.noteOff (2.0, 60);      // closes the EARLIEST one
        check (a.notes().size() == 1 && a.notes()[0].start == 0.0 && std::abs (a.notes()[0].length - 2.0) < 1e-9 && a.notes()[0].velocity == 100,
               "a note-off closes the earliest open note of the pitch");
        const uint8_t offAsOn[3] { 0x90, 60, 0 };
        a.feed (3.0, offAsOn, 3); // note-on with velocity 0
        check (a.notes().size() == 2 && std::abs (a.notes()[1].length - 2.0) < 1e-9 && a.notes()[1].velocity == 90, "note-on with velocity 0 is a note-off");
        a.noteOn (3.5, 64, 80);
        a.noteOff (3.6, 70); // nothing open at that pitch: ignored
        a.closeAll (4.0);
        check (a.notes().size() == 3 && std::abs (a.notes()[2].start - 3.5) < 1e-9 && std::abs (a.notes()[2].length - 0.5) < 1e-9 && a.openNotes().empty(),
               "a hanging note is closed at the end");
        const uint8_t cc1[3] { 0xB0, 1, 20 }, cc1Again[3] { 0xB0, 1, 20 }, cc7[3] { 0xB0, 7, 50 }, bend[3] { 0xE0, 0, 96 }, pressure[2] { 0xD0, 33 };
        NoteAssembler c;
        c.feed (0.5, cc1, 3);
        c.feed (0.6, cc1Again, 3); // same value: not stored twice
        c.feed (0.7, cc7, 3);      // volume is not recorded
        c.feed (1.0, bend, 3);
        c.feed (1.5, pressure, 2);
        check (c.controllers().size() == 3 && c.controllers()[1].status == 0xE0 && c.controllers()[2].status == 0xD0 && c.controllers()[2].data1 == 33,
               "modwheel, bend and pressure are collected; repeats and other controllers are not");
    });

    test ("Controllers play back at the recorded positions", [] {
        using namespace daw;
        engine::MidiClip clip;
        clip.lengthBeats = 4.0;
        clip.controllers = { { 0.5, 0xB0, 1, 10 }, { 1.25, 0xB0, 64, 127 }, { 2.0, 0xE0, 0x00, 0x60 }, { 3.5, 0xD0, 40, 0 } };
        const double spb = 24000.0; // 120 BPM at 48 kHz
        const int block = 512;
        std::vector<std::pair<int64_t, MidiEvent>> got;
        std::vector<MidiEvent> out;
        for (int b = 0; b < 4 * 24000 / block + 1; ++b)
        {
            out.clear();
            engine::renderClip (clip, 0.0, b * block / spb, (b + 1) * block / spb, spb, block, out);
            for (const auto& e : out)
                got.push_back ({ (int64_t) b * block + e.sampleOffset, e });
        }
        check (got.size() == 4, "all four controller events play once: " + std::to_string (got.size()));
        bool exact = got.size() == 4;
        for (size_t i = 0; exact && i < got.size(); ++i)
            exact = std::llabs (got[i].first - std::llround (clip.controllers[i].time * spb)) <= 1 && got[i].second.status == clip.controllers[i].status
                    && got[i].second.data1 == clip.controllers[i].data1 && got[i].second.data2 == clip.controllers[i].data2;
        check (exact, "values and positions are the recorded ones (within one sample)");

        // Stopping a clip that moved sustain and pitch bend puts both back
        engine::ClipPlayer player;
        player.launch (0, &clip, 0.0, 0.0);
        std::vector<MidiEvent> ev;
        player.process (0.0, 4.0, 100.0, 400, ev);
        ev.clear();
        player.stop (4.0, 0.0);
        player.process (4.0, 4.5, 100.0, 50, ev);
        bool sustainOff = false, bendCentre = false;
        for (const auto& e : ev)
        {
            sustainOff = sustainOff || (e.status == 0xB0 && e.data1 == 64 && e.data2 == 0);
            bendCentre = bendCentre || (e.status == 0xE0 && e.data1 == 0 && e.data2 == 0x40);
        }
        check (sustainOff && bendCentre, "a stopped clip releases sustain and centres the pitch wheel");
    });

    test ("ClipPlayer: count-in blocks and aligned launches", [] {
        using namespace daw;
        engine::MidiClip clip;
        clip.lengthBeats = 4.0;
        clip.notes = { { 0.0, 1.0, 60, 100 }, { 1.0, 1.0, 62, 100 } };
        const double spb = 100.0;
        engine::ClipPlayer player;
        std::vector<MidiEvent> out;
        player.launch (0, &clip, 0.0, 0.0);
        player.process (-1.0, -0.5, spb, 50, out);
        check (out.empty() && player.playingSlot() == engine::ClipPlayer::kNoClip, "nothing starts before beat 0");
        player.process (-0.25, 0.25, spb, 50, out);
        bool ok = out.size() == 1 && out[0].sampleOffset == 25 && out[0].data1 == 60;
        check (ok, "the clip starts at beat 0 with the offset measured from the real block start");

        // An aligned launch that reaches the player a little late: the note on the downbeat is not lost
        engine::ClipPlayer late;
        out.clear();
        late.launchAt (0, &clip, 8.0);
        late.process (8.1, 8.2, spb, 10, out);
        check (out.size() == 1 && out[0].sampleOffset == 0 && (out[0].status & 0xF0) == 0x90 && out[0].data1 == 60, "a note 0.1 beat late is played at the first sample");
        check (std::abs (late.playingStartBeat() - 8.0) < 1e-12, "and the loop stays on the original beat");
        out.clear();
        late.process (8.9, 9.1, spb, 20, out);
        check (out.size() == 2 && (out[0].status & 0xF0) == 0x80 && out[0].sampleOffset == 10 && out[1].data1 == 62, "later notes render at their own positions");

        engine::ClipPlayer tooLate;
        out.clear();
        tooLate.launchAt (0, &clip, 8.0);
        tooLate.process (8.4, 8.5, spb, 10, out);
        check (out.empty() && tooLate.playingSlot() == 0, "more than a quarter beat late: no catch-up");

        engine::ClipPlayer inTime;
        out.clear();
        inTime.launchAt (0, &clip, 8.0);
        inTime.process (7.95, 8.05, spb, 10, out);
        check (out.size() == 1 && out[0].sampleOffset == 5, "a launch in the future is exact");
    });

    test ("Capture buffer keeps ten minutes", [] {
        using namespace daw::engine;
        CaptureBuffer buffer;
        for (int i = 0; i < 1000; ++i)
        {
            InputEvent e;
            e.engineSample = i * 100;
            buffer.push (e);
        }
        check (buffer.size() == 1000 && buffer[0].engineSample == 0 && buffer[999].engineSample == 99900, "events are kept in order");
        buffer.evictOlderThan (50000);
        check (buffer.size() == 500 && buffer[0].engineSample == 50000, "old events are dropped");
        CaptureBuffer full;
        for (size_t i = 0; i < CaptureBuffer::kCapacity + 10; ++i)
        {
            InputEvent e;
            e.engineSample = (int64_t) i;
            full.push (e);
        }
        check (full.size() == CaptureBuffer::kCapacity && full[0].engineSample == 10, "a full buffer drops the oldest");
    });

    test ("Tempo estimate and phrase capture", [] {
        using namespace daw::engine;
        const double sr = 48000.0;
        uint32_t lcg = 12345;
        const auto jitter = [&lcg] (double maxMs) {
            lcg = lcg * 1664525u + 1013904223u;
            return ((double) (lcg >> 8) / (double) (1u << 24) * 2.0 - 1.0) * maxMs / 1000.0;
        };
        const auto note = [] (std::vector<InputEvent>& v, int64_t sample, double beat, bool playing, int pitch, int velocity) {
            InputEvent on;
            on.engineSample = sample;
            on.beat = beat;
            on.playing = playing;
            on.size = 3;
            on.bytes[0] = 0x90;
            on.bytes[1] = (uint8_t) pitch;
            on.bytes[2] = (uint8_t) velocity;
            v.push_back (on);
        };
        const auto off = [] (std::vector<InputEvent>& v, int64_t sample, double beat, bool playing, int pitch) {
            InputEvent e;
            e.engineSample = sample;
            e.beat = beat;
            e.playing = playing;
            e.size = 3;
            e.bytes[0] = 0x80;
            e.bytes[1] = (uint8_t) pitch;
            v.push_back (e);
        };

        // 6 bars played while the transport ran, starting in the middle of bar 3 (beats 8..): the clip is 8 bars long
        // and starts on the bar line of the first note
        {
            std::vector<InputEvent> ev;
            const int count = 23;
            for (int i = 0; i < count; ++i)
            {
                const double beat = 9.5 + i; // 9.5 .. 31.5
                const int pitch = 40 + (i * 7) % 29;
                note (ev, (int64_t) (beat * 24000.0), beat, true, pitch, 90);
                off (ev, (int64_t) ((beat + 0.5) * 24000.0), beat + 0.5, true, pitch);
            }
            const auto r = capturePhrase (ev, 0, 100000000, sr, 4.0, 120.0);
            check (r.ok && r.transportWasPlaying && r.numBars == 8 && std::abs (r.lengthBeats - 32.0) < 1e-9, "a 6-bar phrase gives an 8-bar clip");
            check (r.ok && r.numNotes == count && std::abs (r.notes.front().start - 1.5) < 1e-9, "it starts on the bar line of the first note");
        }

        // 16th notes at 128 BPM with +-10 ms of jitter, transport stopped
        {
            std::vector<double> onsets;
            std::vector<InputEvent> ev;
            const double step = 60.0 / 128.0 / 4.0;
            for (int i = 0; i < 64; ++i)
            {
                const double t = 2.0 + i * step + jitter (10.0);
                onsets.push_back (t);
                const int pitch = 48 + (i * 5) % 24;
                note (ev, (int64_t) (t * sr), 0.0, false, pitch, 100);
                off (ev, (int64_t) ((t + 0.05) * sr), 0.0, false, pitch);
            }
            const auto est = estimateTempo (onsets, 100.0);
            std::printf ("    estimated tempo %.3f\n", est.bpm);
            check (est.confident && std::abs (est.bpm - 128.0) < 0.5, "the tempo of jittery 16ths is found within half a BPM");
            const auto r = capturePhrase (ev, 0, 100000000, sr, 4.0, 100.0);
            check (r.ok && ! r.transportWasPlaying && r.tempoFromPhrase && std::abs (r.bpm - 128.0) < 0.5, "the phrase brings its own tempo");
            check (r.ok && r.numBars == 4 && std::abs (r.lengthBeats - 16.0) < 1e-9 && r.numNotes == 64, "and makes a 4-bar clip");
            // Too little to go on: the tempo of the project stays
            std::vector<double> few { 1.0, 1.31, 1.9 };
            check (! estimateTempo (few, 100.0).confident && std::abs (estimateTempo (few, 100.0).bpm - 100.0) < 1e-9, "three notes do not make a tempo");
            // Random-ish gaps do not either
            std::vector<double> loose { 0.0, 0.41, 0.97, 1.30, 2.11, 2.50, 3.33, 3.61, 4.58, 5.02 };
            check (! estimateTempo (loose, 100.0).confident, "unevenly spaced notes keep the project tempo");
        }

        // The same four bars played twice: one round is captured
        {
            std::vector<InputEvent> ev;
            for (int round = 0; round < 2; ++round)
                for (int i = 0; i < 16; ++i)
                {
                    const double beat = 8.0 + round * 16.0 + i; // from the bar line of beat 8
                    const int pitch = 50 + (i * 3) % 17;
                    note (ev, (int64_t) (beat * 24000.0), beat, true, pitch, 100);
                    off (ev, (int64_t) ((beat + 0.5) * 24000.0), beat + 0.5, true, pitch);
                }
            const auto r = capturePhrase (ev, 0, 100000000, sr, 4.0, 120.0);
            check (r.ok && r.numBars == 4 && r.numNotes == 16 && std::abs (r.lengthBeats - 16.0) < 1e-9, "a phrase played twice gives one 4-bar round");
        }

        // A pause longer than four seconds ends the phrase; an older phrase is not taken along
        {
            std::vector<InputEvent> ev;
            note (ev, 0, 0.0, false, 60, 100);
            off (ev, 1000, 0.0, false, 60);
            note (ev, (int64_t) (10.0 * sr), 0.0, false, 64, 100);
            off (ev, (int64_t) (10.5 * sr), 0.0, false, 64);
            const auto r = capturePhrase (ev, 0, (int64_t) (11.0 * sr), sr, 4.0, 120.0);
            check (r.ok && r.numNotes == 1 && r.notes[0].pitch == 64, "only the last phrase is captured");
            // events of another track are not mixed in; with none on the target the events without a track are used
            for (auto& e : ev)
                e.trackId = 7;
            check (! capturePhrase (ev, 3, (int64_t) (11.0 * sr), sr, 4.0, 120.0).ok, "events that went to another track are not taken");
            check (capturePhrase (ev, 7, (int64_t) (11.0 * sr), sr, 4.0, 120.0).ok, "the target track's own events are");
        }
    });

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
