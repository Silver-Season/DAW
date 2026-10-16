// Plain C++ tests for the framework-free core: run with `ctest` or directly.
#include "daw/dsp/FFT.h"
#include "daw/dsp/Filter.h"
#include "daw/dsp/Envelope.h"
#include "daw/dsp/Wavetable.h"
#include "daw/dsp/WavetableOscillator.h"
#include "daw/engine/Clip.h"
#include "daw/synth/PrismaSynth.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <numbers>
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

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
