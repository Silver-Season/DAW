// Tests for the drum kit engine (DrumKit.h) and the synthesised factory sounds (DrumSynth.h).
#include "daw/drums/DrumKit.h"
#include "daw/drums/DrumSynth.h"
#include "daw/dsp/FFT.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

// Counts heap use, so the tests can prove that process() never allocates (docs/spec/00-rules.md, section 2).
namespace
{
std::atomic<long> heapCalls { 0 };
}

void* operator new (std::size_t size)
{
    heapCalls.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept
{
    if (p != nullptr)
        heapCalls.fetch_add (1, std::memory_order_relaxed);
    std::free (p);
}

void operator delete (void* p, std::size_t) noexcept
{
    if (p != nullptr)
        heapCalls.fetch_add (1, std::memory_order_relaxed);
    std::free (p);
}

namespace
{
using namespace daw::drums;

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

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Hit
{
    int64_t at;
    int note;
    float velocity; // <= 0: note-off
};

struct Stereo
{
    std::vector<float> l, r;
};

// Renders `length` samples in blocks, scheduling the hits (sorted by time) at their exact sample.
Stereo render (DrumKitEngine& e, const std::vector<Hit>& hits, int64_t length, int block = kBlock,
               const std::function<void (int64_t)>& beforeBlock = {})
{
    Stereo out;
    out.l.assign ((size_t) length, 0.0f);
    out.r.assign ((size_t) length, 0.0f);
    std::vector<DrumEvent> events;
    events.reserve (hits.size());
    size_t next = 0;
    for (int64_t pos = 0; pos < length; pos += block)
    {
        const int n = (int) std::min<int64_t> (block, length - pos);
        if (beforeBlock)
            beforeBlock (pos);
        events.clear();
        while (next < hits.size() && hits[next].at < pos + n)
        {
            events.push_back ({ (int) (hits[next].at - pos), hits[next].note, hits[next].velocity });
            ++next;
        }
        e.process (out.l.data() + pos, out.r.data() + pos, n, events.data(), (int) events.size());
    }
    return out;
}

SamplePtr constantSample (float value, int frames, double rate = kRate)
{
    auto s = std::make_shared<Sample>();
    s->rate = rate;
    s->left.assign ((size_t) frames, value);
    return s;
}

SamplePtr sineSample (double hz, float amp, int frames, double rate = kRate)
{
    auto s = std::make_shared<Sample>();
    s->rate = rate;
    s->left.resize ((size_t) frames);
    for (int i = 0; i < frames; ++i)
        s->left[(size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979 * hz * i / rate);
    return s;
}

int64_t lastNonZero (const std::vector<float>& x, float threshold = 1.0e-7f)
{
    for (int64_t i = (int64_t) x.size() - 1; i >= 0; --i)
        if (std::abs (x[(size_t) i]) > threshold)
            return i;
    return -1;
}

float maxJump (const std::vector<float>& x)
{
    float m = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

double spectralCentroid (const std::vector<float>& x, double rate)
{
    size_t n = 1;
    while (n < std::min<size_t> (x.size(), 16384))
        n <<= 1;
    std::vector<std::complex<double>> data (n);
    for (size_t i = 0; i < n && i < x.size(); ++i)
        data[i] = x[i];
    daw::dsp::fft (data, false);
    double num = 0.0, den = 0.0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double mag = std::abs (data[k]);
        num += mag * (double) k * rate / (double) n;
        den += mag;
    }
    return den > 0.0 ? num / den : 0.0;
}

// Fundamental from upward zero crossings (linearly interpolated) between t0 and t1 seconds.
double zeroCrossingHz (const std::vector<float>& x, double rate, double t0, double t1)
{
    const auto a = (size_t) (t0 * rate), b = std::min (x.size() - 1, (size_t) (t1 * rate));
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = a; i < b; ++i)
        if (x[i] <= 0.0f && x[i + 1] > 0.0f)
        {
            const double t = (double) i + (double) (-x[i]) / (double) (x[i + 1] - x[i]);
            if (first < 0.0)
                first = t;
            last = t;
            ++count;
        }
    return count > 1 ? (double) (count - 1) * rate / (last - first) : 0.0;
}

bool allFinite (const std::vector<float>& x)
{
    return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); });
}

float peakOf (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x)
        p = std::max (p, std::abs (v));
    return p;
}

double meanOf (const std::vector<float>& x)
{
    double s = 0.0;
    for (float v : x)
        s += v;
    return x.empty() ? 0.0 : s / (double) x.size();
}

double seconds (const Sample& s) { return (double) s.numFrames() / s.rate; }
} // namespace

int main()
{
    test ("pad map follows General MIDI where it matters", [] {
        const auto& map = padMap();
        check (map[(size_t) Pad::kick].note == 36, "36 kick");
        check (map[(size_t) Pad::snare].note == 38, "38 snare");
        check (map[(size_t) Pad::clap].note == 39, "39 clap");
        check (map[(size_t) Pad::closedHat].note == 42, "42 closed hat");
        check (map[(size_t) Pad::openHat].note == 46, "46 open hat");
        check (map[(size_t) Pad::crash].note == 49, "49 crash");
        check (map[(size_t) Pad::ride].note == 51, "51 ride");
        bool ok = true;
        for (int p = 0; p < kNumPads; ++p)
            ok = ok && map[(size_t) p].note == noteForPad (p) && padForNote (noteForPad (p)) == p;
        check (ok, "note <-> pad round trip");
        check (padForNote (35) == -1 && padForNote (52) == -1, "notes outside the kit are ignored");
    });

    const auto kits = factoryKits();

    test ("every factory sound renders clean", [&] {
        check (kits.size() >= 6, "at least six kits");
        for (const char* required : { "808 Classic", "909 Club", "Trap", "Boom Bap", "Techno", "Lo-Fi" })
            check (std::any_of (kits.begin(), kits.end(), [&] (const FactoryKit& k) { return k.name == required; }),
                   std::string ("kit ") + required);

        for (const auto& kit : kits)
            for (int pad = 0; pad < kNumPads; ++pad)
            {
                const std::string what = kit.name + " / " + kit.padNames[(size_t) pad];
                const auto s = renderFactorySound (kit.name, pad, kRate);
                check (s != nullptr && s->numFrames() > 100, what + ": renders");
                if (s == nullptr || s->numFrames() <= 100)
                    continue;
                check (s->rate == kRate, what + ": at the requested rate");
                check (s->channels == 1 || (s->channels == 2 && s->right.size() == s->left.size()), what + ": channel layout");
                for (const auto* ch : { &s->left, &s->right })
                {
                    if (ch->empty())
                        continue;
                    const float peak = peakOf (*ch);
                    check (allFinite (*ch), what + ": finite");
                    check (peak <= 1.0f, what + ": peak <= 1");
                    check (std::abs (meanOf (*ch)) < 1.0e-3, what + ": no DC (mean " + std::to_string (meanOf (*ch)) + ")");
                    check (std::abs (ch->front()) < 1.0e-3f && std::abs (ch->back()) < 1.0e-3f, what + ": starts and ends at 0");
                    float head = 0.0f, tail = 0.0f;
                    for (int i = 0; i < 4; ++i)
                    {
                        head = std::max (head, std::abs ((*ch)[(size_t) i]));
                        tail = std::max (tail, std::abs ((*ch)[ch->size() - 1 - (size_t) i]));
                    }
                    check (head < 0.05f && tail < 0.02f, what + ": no click at the ends");
                }
                const float peak = std::max (peakOf (s->left), peakOf (s->right));
                check (peak > 0.25f, what + ": not silent");
            }
    });

    test ("normalisation and lengths", [&] {
        for (const auto& kit : kits)
        {
            const auto kick = renderFactorySound (kit.name, Pad::kick, kRate);
            const auto snare = renderFactorySound (kit.name, Pad::snare, kRate);
            const auto hat = renderFactorySound (kit.name, Pad::closedHat, kRate);
            const auto open = renderFactorySound (kit.name, Pad::openHat, kRate);
            const auto cymbal = renderFactorySound (kit.name, Pad::crash, kRate);
            const float minus1 = std::pow (10.0f, -1.0f / 20.0f);
            check (std::abs (peakOf (kick->left) - minus1) < 0.01f, kit.name + ": kick peaks at -1 dBFS");
            check (std::abs (peakOf (snare->left) - minus1) < 0.01f, kit.name + ": snare peaks at -1 dBFS");
            check (std::max (peakOf (hat->left), peakOf (hat->right)) < 0.6f, kit.name + ": hat is quieter");
            check (seconds (*hat) >= 0.05 && seconds (*hat) <= 0.4, kit.name + ": closed hat 0.05..0.4 s");
            check (seconds (*open) >= 0.4 && seconds (*open) <= 0.8, kit.name + ": open hat about 0.6 s");
            check (seconds (*cymbal) >= 1.8 && seconds (*cymbal) <= 2.6, kit.name + ": crash about 2 s");
        }
        const auto sub = sub808 (kRate);
        check (std::abs (seconds (*sub) - 1.5) < 0.01, "808 sub is 1.5 s");
    });

    test ("factory sounds are deterministic and rate-independent", [&] {
        const auto a = renderFactorySound ("Trap", Pad::openHat, kRate);
        const auto b = renderFactorySound ("Trap", Pad::openHat, kRate);
        check (a->left == b->left && a->right == b->right, "same render twice");
        const auto c = renderFactorySound ("Trap", Pad::openHat, 44100.0);
        check (std::abs (seconds (*a) - seconds (*c)) < 0.001, "same length at 44.1 kHz");
        check (renderFactorySound ("No Such Kit", 0, kRate) == nullptr && renderFactorySound ("Trap", 16, kRate) == nullptr,
               "unknown kit or pad gives nullptr");
    });

    test ("kick is darker than the hat", [&] {
        for (const auto& kit : kits)
        {
            const auto kick = renderFactorySound (kit.name, Pad::kick, kRate);
            const auto hat = renderFactorySound (kit.name, Pad::closedHat, kRate);
            const double ck = spectralCentroid (kick->left, kRate), ch = spectralCentroid (hat->left, kRate);
            check (ck * 5.0 < ch, kit.name + ": centroid kick " + std::to_string ((int) ck) + " Hz < hat " + std::to_string ((int) ch) + " Hz");
        }
    });

    test ("808 sub sits on C", [&] {
        const auto direct = sub808 (kRate);
        double hz = zeroCrossingHz (direct->left, kRate, 0.2, 1.2);
        check (std::abs (hz / kSub808RootHz - 1.0) < 0.02, "sub808 at C1: " + std::to_string (hz) + " Hz");

        SoundParams up;
        up.pitch = 2.0f;
        hz = zeroCrossingHz (sub808 (kRate, up)->left, kRate, 0.2, 1.2);
        check (std::abs (hz / (2.0 * kSub808RootHz) - 1.0) < 0.02, "pitch 2 gives C2: " + std::to_string (hz) + " Hz");

        for (const char* kit : { "808 Classic", "Trap", "Lo-Fi" })
        {
            const auto s = renderFactorySound (kit, Pad::kick2, kRate);
            hz = zeroCrossingHz (s->left, kRate, 0.2, 1.0);
            check (std::abs (hz / kSub808RootHz - 1.0) < 0.02, std::string (kit) + " sub on C1: " + std::to_string (hz) + " Hz");
        }
    });

    test ("a note-on at offset 100 starts exactly at sample 100", [] {
        DrumKitEngine e;
        e.prepare (kRate, kBlock);
        e.setPadSample (Pad::snare, constantSample (0.5f, 2000));
        std::vector<float> l (kBlock), r (kBlock);
        const daw::MidiEvent ev = daw::MidiEvent::noteOn (100, 38, 127);
        e.process (l.data(), r.data(), kBlock, &ev, 1);
        check (l[99] == 0.0f && r[99] == 0.0f, "silent before");
        check (l[100] == 0.5f && r[100] == 0.5f, "full sample value at 100 (got " + std::to_string (l[100]) + ")");

        // The same through the queued noteOn() call.
        e.reset();
        e.noteOn (38, 1.0f, 100);
        e.process (l.data(), r.data(), kBlock);
        check (l[99] == 0.0f && l[100] == 0.5f, "noteOn() call at offset 100");
    });

    test ("tune +12 halves the duration", [] {
        DrumKitEngine e;
        e.prepare (kRate, kBlock);
        e.setPadSample (Pad::kick, constantSample (0.5f, 4800));
        const auto base = render (e, { { 0, 36, 1.0f } }, 9600);
        e.setPadParam (Pad::kick, PadParam::tune, 12.0f);
        const auto up = render (e, { { 0, 36, 1.0f } }, 9600);
        const double ratio = (double) (lastNonZero (up.l) + 1) / (double) (lastNonZero (base.l) + 1);
        check (std::abs (ratio - 0.5) < 0.001, "duration ratio " + std::to_string (ratio));

        // A sample at another rate plays at its own speed: 24 kHz material lasts twice as long at 48 kHz.
        e.setPadParam (Pad::kick, PadParam::tune, 0.0f);
        e.setPadSample (Pad::kick, constantSample (0.5f, 2400, 24000.0));
        const auto slow = render (e, { { 0, 36, 1.0f } }, 9600);
        check (std::abs (lastNonZero (slow.l) - 4800) <= 2, "rate conversion");
    });

    test ("choke: the closed hat stops the open hat within 3 ms", [] {
        DrumKitEngine e;
        e.prepare (kRate, kBlock);
        e.setPadSample (Pad::openHat, sineSample (1000.0, 0.5f, 96000));
        e.setPadSample (Pad::closedHat, constantSample (0.0f, 100));
        e.setPadParam (Pad::openHat, PadParam::chokeGroup, 1.0f);
        e.setPadParam (Pad::closedHat, PadParam::chokeGroup, 1.0f);
        const int64_t at = 4800 + 37;
        const auto out = render (e, { { 0, 46, 1.0f }, { at, 42, 1.0f } }, 19200);
        const int64_t last = lastNonZero (out.l);
        const double ms = (double) (last - at) * 1000.0 / kRate;
        check (ms >= 2.0 && ms <= 4.0, "open hat ends " + std::to_string (ms) + " ms after the closed hat");
        check (peakOf (std::vector<float> (out.l.begin() + at - 200, out.l.begin() + at)) > 0.45f, "open hat was playing");
        check (maxJump (out.l) < 0.075f, "the choke fades instead of cutting");

        // A pad in another group is left alone.
        e.setPadParam (Pad::closedHat, PadParam::chokeGroup, 2.0f);
        const auto other = render (e, { { 0, 46, 1.0f }, { at, 42, 1.0f } }, 19200);
        check (lastNonZero (other.l) > 19000, "different group does not choke");
    });

    test ("voice stealing never clicks", [] {
        // A constant-level sample with a 2 ms ramp-in: cutting a voice would jump by 0.5, a 2 ms fade moves
        // 0.5 / 96 per sample.
        auto s = std::make_shared<Sample>();
        s->left.resize (48000);
        for (int i = 0; i < 48000; ++i)
            s->left[(size_t) i] = 0.5f * std::min (1.0f, (float) i / 96.0f);

        DrumKitEngine e;
        e.prepare (kRate, kBlock, 4);
        for (int p = 0; p < 8; ++p)
            e.setPadSample (p, s);
        e.setPadParam (Pad::kick, PadParam::polyphony, 1.0f);

        std::vector<Hit> hits;
        for (int k = 0; k < 40; ++k)
            hits.push_back ({ (int64_t) k * 1013, 36, 1.0f }); // one pad, polyphony 1: every hit steals
        auto out = render (e, hits, 48000);
        check (maxJump (out.l) < 0.02f, "per-pad steal: max jump " + std::to_string (maxJump (out.l)));

        hits.clear();
        for (int k = 0; k < 40; ++k)
            hits.push_back ({ (int64_t) k * 1013, 37 + (k % 7), 1.0f }); // 7 pads over 4 voices: global steals
        out = render (e, hits, 48000);
        check (maxJump (out.l) < 0.02f, "global steal: max jump " + std::to_string (maxJump (out.l)));
        check (e.numActiveVoices() <= 8, "sounding voices stay within the limit");

        // allNotesOff fades too.
        e.allNotesOff();
        std::vector<float> l (kBlock), r (kBlock);
        e.process (l.data(), r.data(), kBlock);
        check (std::abs (l[0]) > 0.0f && l[kBlock - 1] == 0.0f && maxJump (l) < 0.025f, "all notes off fades in 2 ms");
    });

    test ("32-voice stress with 1000 random notes", [] {
        DrumKitEngine e;
        e.prepare (kRate, 512);
        for (int p = 0; p < kNumPads; ++p)
            e.setPadSample (p, renderFactorySound ("808 Classic", p, kRate));
        std::mt19937 rng (1234);
        std::vector<Hit> hits;
        for (int k = 0; k < 1000; ++k)
            hits.push_back ({ (int64_t) (rng() % (48000 * 10)), 36 + (int) (rng() % 16), 0.05f + (float) (rng() % 100) / 100.0f });
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.at < b.at; });
        int maxVoices = 0;
        const auto out = render (e, hits, 48000 * 11, 512, [&] (int64_t) {
            // Move parameters while playing, from the same thread as process() for determinism.
            const int pad = (int) (rng() % 16);
            e.setPadParam (pad, (PadParam) (rng() % (unsigned) kNumPadParams), (float) ((int) (rng() % 2001) - 1000) / 40.0f);
            maxVoices = std::max (maxVoices, e.numActiveVoices());
        });
        check (allFinite (out.l) && allFinite (out.r), "finite");
        check (peakOf (out.l) < 64.0f, "bounded");
        check (maxVoices <= 64, "slots bounded");
    });

    test ("process() never touches the heap", [] {
        DrumKitEngine e;
        e.prepare (kRate, kBlock);
        for (int p = 0; p < kNumPads; ++p)
            e.setPadSample (p, renderFactorySound ("Techno", p, kRate));
        e.setPadParam (Pad::snare, PadParam::cutoff, 3000.0f);
        e.setPadParam (Pad::kick, PadParam::drive, 12.0f);
        std::vector<float> l (kBlock), r (kBlock);
        std::vector<DrumEvent> events;
        for (int k = 0; k < 16; ++k)
            events.push_back ({ k * 15, 36 + k, 0.9f });
        const long before = heapCalls.load();
        for (int b = 0; b < 400; ++b)
        {
            e.noteOn (36 + b % 16, 0.7f, b % kBlock);
            e.process (l.data(), r.data(), kBlock, b % 8 == 0 ? events.data() : nullptr, b % 8 == 0 ? (int) events.size() : 0);
        }
        const long after = heapCalls.load(); // read before check() builds its message string
        check (after == before, "no allocation in 400 busy blocks");
    });

    test ("swapping a pad sample while it plays", [] {
        DrumKitEngine e;
        e.prepare (kRate, kBlock);
        auto a = constantSample (0.25f, 9600);
        std::weak_ptr<const Sample> watch = a;
        e.setPadSample (Pad::kick, a);
        a.reset();

        bool swapped = false;
        auto out = render (e, { { 10, 36, 1.0f } }, 4800, kBlock, [&] (int64_t pos) {
            if (pos == 1024)
            {
                e.setPadSample (Pad::kick, constantSample (0.75f, 9600));
                e.collectGarbage();
                swapped = true;
            }
        });
        check (swapped && ! watch.expired(), "old sample kept alive while its voice plays");
        check (out.l[1000] == 0.25f && out.l[4700] == 0.25f, "old voice keeps playing the old sample");

        out = render (e, { { 0, 36, 1.0f } }, 9600);
        check (out.l[10] == 0.75f || out.l[10] == 0.25f + 0.75f, "a new hit plays the new sample");
        check (out.l[6000] == 0.75f, "the old voice has finished");
        e.collectGarbage();
        check (watch.expired() && e.numRetiredSamples() == 0, "old sample released on the message thread");

        e.setPadSample (Pad::kick, nullptr);
        out = render (e, { { 0, 36, 1.0f } }, 512);
        check (peakOf (out.l) == 0.0f, "an empty pad does not start a voice");
    });

    test ("envelope, gate, reverse, start, mix", [] {
        DrumKitEngine e;
        e.prepare (kRate, kBlock);
        e.setPadSample (Pad::snare, constantSample (0.5f, 48000));

        e.setPadParam (Pad::snare, PadParam::decay, 0.1f);
        auto out = render (e, { { 0, 38, 1.0f } }, 24000);
        check (std::abs (lastNonZero (out.l) - 4800) <= 2, "decay 0.1 s ends the sound at 0.1 s");
        check (out.l[1000] == 0.5f && out.l[4000] < 0.5f, "holds, then releases");
        e.setPadParam (Pad::snare, PadParam::decay, 10.0f);

        e.setPadParam (Pad::snare, PadParam::oneShot, 0.0f);
        out = render (e, { { 0, 38, 1.0f }, { 2400, 38, 0.0f } }, 48000);
        check (out.l[2000] == 0.5f && lastNonZero (out.l) < 2400 + 14400 + 2, "gate: note-off releases");
        e.reset();
        e.setPadParam (Pad::snare, PadParam::oneShot, 1.0f);
        out = render (e, { { 0, 38, 1.0f }, { 2400, 38, 0.0f } }, 9600);
        check (out.l[9000] == 0.5f, "one-shot ignores note-off");

        e.reset();
        e.setPadParam (Pad::snare, PadParam::attack, 10.0f);
        out = render (e, { { 0, 38, 1.0f } }, 2000);
        check (out.l[0] > 0.0f && out.l[0] < 0.01f && std::abs (out.l[239] - 0.25f) < 0.01f && out.l[600] == 0.5f, "attack ramp");
        e.setPadParam (Pad::snare, PadParam::attack, 0.0f);

        auto ramp = std::make_shared<Sample>();
        ramp->left.resize (4800);
        for (int i = 0; i < 4800; ++i)
            ramp->left[(size_t) i] = (float) i / 4800.0f;
        e.setPadSample (Pad::rim, ramp);
        e.setPadParam (Pad::rim, PadParam::reverse, 1.0f);
        e.reset();
        out = render (e, { { 0, 37, 1.0f } }, 4800);
        check (out.l[100] > 0.97f && out.l[4700] < 0.03f && out.l[0] < 0.03f, "reverse plays from the end, ramped in");
        e.setPadParam (Pad::rim, PadParam::reverse, 0.0f);
        e.reset();
        e.setPadParam (Pad::rim, PadParam::startOffset, 0.5f);
        out = render (e, { { 0, 37, 1.0f } }, 4800);
        check (std::abs (out.l[100] - 0.52f) < 0.005f && lastNonZero (out.l) < 2401, "start offset skips half");
        e.setPadParam (Pad::rim, PadParam::startOffset, 0.0f);

        e.reset();
        e.setPadParam (Pad::snare, PadParam::pan, -1.0f);
        e.setPadParam (Pad::snare, PadParam::volumeDb, -6.0f);
        out = render (e, { { 0, 38, 0.5f } }, 1000);
        check (std::abs (out.l[500] - 0.5f * 0.5f * 0.501187f) < 1.0e-4f && out.r[500] == 0.0f, "pan, volume and velocity");
        e.reset();
        e.setPadParam (Pad::snare, PadParam::velocitySens, 0.0f);
        out = render (e, { { 0, 38, 0.1f } }, 1000);
        check (std::abs (out.l[500] - 0.5f * 0.501187f) < 1.0e-4f, "velocity sensitivity 0 ignores velocity");
        e.setPadParams (Pad::snare, PadParams {});

        e.reset();
        e.setPadParam (Pad::rim, PadParam::solo, 1.0f);
        out = render (e, { { 0, 38, 1.0f } }, 1000);
        check (peakOf (out.l) == 0.0f, "solo on another pad silences this one");
        e.setPadParam (Pad::rim, PadParam::solo, 0.0f);
        e.reset();
        e.setPadParam (Pad::snare, PadParam::mute, 1.0f);
        out = render (e, { { 0, 38, 1.0f } }, 1000);
        check (peakOf (out.l) == 0.0f, "mute");
        e.setPadParam (Pad::snare, PadParam::mute, 0.0f);

        e.reset();
        e.setPadSample (Pad::snare, sineSample (8000.0, 0.5f, 48000));
        e.setPadParam (Pad::snare, PadParam::cutoff, 500.0f);
        out = render (e, { { 0, 38, 1.0f } }, 9600);
        check (peakOf (std::vector<float> (out.l.begin() + 4800, out.l.end())) < 0.01f, "low-pass removes 8 kHz at 500 Hz");
        e.setPadParam (Pad::snare, PadParam::cutoff, 20000.0f);

        check (e.padActivity (Pad::snare) > 0.0f, "activity after a hit");
        render (e, {}, 48000);
        check (e.padActivity (Pad::snare) == 0.0f, "activity decays to 0");
        render (e, { { 100, 38, 0.8f } }, 256);
        check (e.padActivity (Pad::snare) > 0.75f && e.padActivity (Pad::snare) <= 0.8f, "activity shows the velocity");
    });

    test ("kit state round trip", [] {
        KitState kit = factoryKitState ("Trap");
        check (kit.pads[(size_t) Pad::openHat].params.chokeGroup == 1 && kit.pads[(size_t) Pad::closedHat].params.chokeGroup == 1,
               "factory kit chokes the hats");
        std::string name;
        int pad = -1;
        check (parseFactorySource (kit.pads[5].source, name, pad) && name == "Trap" && pad == 5, "factory source parses");
        check (! parseFactorySource ("/Users/me/kick.wav", name, pad), "file path is not a factory source");

        kit.name = "My = kit\\with\nlines";
        kit.pads[3].source = "/Users/me/Samples/clap #2.wav";
        kit.pads[3].params.tune = 1.0f / 3.0f;
        kit.pads[3].params.fine = -12.345678f;
        kit.pads[3].params.cutoff = 1234.5f;
        kit.pads[3].params.reverse = true;
        kit.pads[3].params.oneShot = false;
        kit.pads[3].params.polyphony = 2;
        kit.pads[15].params.solo = true;
        kit.pads[15].params.drive = 0.1f;
        const std::string text = kit.toText();
        KitState back;
        check (KitState::fromText (text, back), "parses");
        check (back == kit, "round trip is exact");

        KitState untouched;
        check (! KitState::fromText ("hello=1\n", untouched) && untouched == KitState {}, "not a kit");
        KitState tolerant;
        check (KitState::fromText ("format=daw.drumkit\nversion=9\nfuture=1\npad38.tune=99\npad99.tune=1\n", tolerant), "future file loads");
        check (tolerant.pads[(size_t) Pad::snare].params.tune == 24.0f, "values are clamped");
    });

    test ("CPU: 60 s busy pattern", [] {
        DrumKitEngine e;
        e.prepare (kRate, kBlock);
        for (int p = 0; p < kNumPads; ++p)
            e.setPadSample (p, renderFactorySound ("909 Club", p, kRate));
        e.setPadParam (Pad::closedHat, PadParam::chokeGroup, 1.0f);
        e.setPadParam (Pad::openHat, PadParam::chokeGroup, 1.0f);
        e.setPadParam (Pad::snare, PadParam::cutoff, 6000.0f); // one filtered pad, the realistic case
        const int64_t length = (int64_t) (60.0 * kRate);
        const double sixteenth = kRate * 60.0 / 128.0 / 4.0; // 128 BPM
        std::vector<Hit> hits;
        for (int s = 0; (double) s * sixteenth < (double) length; ++s)
        {
            const auto at = (int64_t) std::llround ((double) s * sixteenth);
            if (s % 4 == 0)
                hits.push_back ({ at, 36, 1.0f });
            if (s % 8 == 4)
                hits.push_back ({ at, 38, 0.9f });
            hits.push_back ({ at, s % 8 == 6 ? 46 : 42, s % 2 == 0 ? 0.8f : 0.55f });
            if (s % 16 == 15)
                hits.push_back ({ at, 39, 0.7f });
        }
        std::vector<float> l ((size_t) length), r ((size_t) length);
        const auto t0 = std::chrono::steady_clock::now();
        size_t next = 0;
        DrumEvent events[16];
        for (int64_t pos = 0; pos < length; pos += kBlock)
        {
            const int n = (int) std::min<int64_t> (kBlock, length - pos);
            int count = 0;
            while (next < hits.size() && hits[next].at < pos + n && count < 16)
            {
                events[count++] = { (int) (hits[next].at - pos), hits[next].note, hits[next].velocity };
                ++next;
            }
            e.process (l.data() + pos, r.data() + pos, n, events, count);
        }
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  rendered 60 s (%zu hits, 48 kHz, block 256) in %.1f ms = %.3f %% of real time\n", hits.size(),
                     elapsed * 1000.0, elapsed / 60.0 * 100.0);
        check (allFinite (l) && peakOf (l) > 0.5f, "pattern rendered");

        const auto t1 = std::chrono::steady_clock::now();
        for (const auto& kit : factoryKits())
            for (int p = 0; p < kNumPads; ++p)
                renderFactorySound (kit.name, p, kRate);
        const double synth = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
        std::printf ("  synthesised all %zu factory sounds in %.1f ms\n", factoryKits().size() * kNumPads, synth * 1000.0);
    });

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
