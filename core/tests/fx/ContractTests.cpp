// The contract every effect in the registry keeps (docs/agents/EFFECTS.md). A new effect is checked by these tests the
// moment it is added to effectTypes(), before it has a single test of its own.
#include "FxTest.h"

#include <atomic>
#include <cstdlib>
#include <new>
#include <set>

using namespace fxtest;

// This test program replaces the global allocator so that it can count what the audio-thread calls allocate.
namespace
{
std::atomic<bool> counting { false };
std::atomic<int> allocations { 0 };

void* allocate (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* allocateAligned (std::size_t size, std::align_val_t align)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    const auto a = std::max ((std::size_t) align, sizeof (void*));
    if (void* p = std::aligned_alloc (a, (size + a - 1) / a * a))
        return p;
    throw std::bad_alloc();
}

// Counts the allocations made while it lives.
struct AllocationCounter
{
    AllocationCounter()
    {
        allocations = 0;
        counting = true;
    }
    ~AllocationCounter() { counting = false; }
    int count() const { return allocations.load(); }
};
} // namespace

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void* operator new (std::size_t size, std::align_val_t align) { return allocateAligned (size, align); }
void* operator new[] (std::size_t size, std::align_val_t align) { return allocateAligned (size, align); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }

namespace
{
// Runs `fx` over `in` with blocks of the given sizes in turn, on `channels` channels, feeding the input as the sidechain
// too when the effect takes one. No allocation of its own once the buffers exist.
void runVaried (daw::fx::Effect& fx, Stereo& io, const std::vector<int>& blocks, int channels)
{
    size_t pos = 0;
    for (size_t b = 0; pos < io.size(); ++b)
    {
        const int n = (int) std::min ((size_t) blocks[b % blocks.size()], io.size() - pos);
        float* ch[2] { io.l.data() + pos, io.r.data() + pos };
        const float* sc[2] { io.l.data() + pos, io.r.data() + pos };
        fx.process (ch, channels, n, fx.acceptsSidechain() ? sc : nullptr);
        pos += (size_t) n;
    }
}

double peakOf (const Stereo& s) { return std::max (peak (s.l), peak (s.r)); }
} // namespace

FX_TEST ("contract: the registry lists unique ids with names and categories, and creates each of them")
{
    std::set<std::string> ids;
    for (const auto& t : daw::fx::effectTypes())
    {
        check (! t.id.empty() && ids.insert (t.id).second, "effect id '" + t.id + "' is unique and not empty");
        check (! t.name.empty(), t.id + ": has an English name");
        check (! t.category.empty(), t.id + ": has a category");
        auto fx = daw::fx::createEffect (t.id);
        check (fx != nullptr && fx->type() == t.id, t.id + ": createEffect makes it and type() returns the same id");
        check (t.keyParams.size() <= 8, t.id + ": at most eight key parameters");
        for (const auto& k : t.keyParams)
            check (fx != nullptr && fx->paramIndex (k) >= 0, t.id + ": key parameter " + k + " exists");
    }
    check (daw::fx::createEffect ("no-such-effect") == nullptr, "an unknown id gives null");
}

FX_TEST ("contract: after prepare(), process, setParam, setTransport and readouts never allocate")
{
    for (const auto& t : daw::fx::effectTypes())
    {
        auto fx = make (t.id.c_str());
        if (fx == nullptr)
            continue;
        auto io = noise (-6.0, 48000, 11);
        const auto& ps = fx->params();
        const std::vector<int> blocks { kBlock, 1, 17, kBlock - 3 };
        int count = 0;
        {
            AllocationCounter counter;
            daw::fx::TransportInfo transport;
            transport.playing = true;
            for (int round = 0; round < 3; ++round)
            {
                for (int i = 0; i < (int) ps.size(); ++i)
                    fx->setParam (i, round == 0 ? ps[(size_t) i].max : round == 1 ? ps[(size_t) i].min : ps[(size_t) i].def);
                transport.ppq += 4.0;
                transport.bpm = 90.0 + 30.0 * round;
                fx->setTransport (transport);
                runVaried (*fx, io, blocks, 2);
                for (int r = 0; r < 4; ++r)
                    (void) fx->readout (r);
                (void) fx->latencySamples();
                (void) fx->tailSeconds();
            }
            fx->reset();
            count = counter.count();
        }
        check (count == 0, t.id + ": " + std::to_string (count) + " allocations on the audio thread");
    }
}

FX_TEST ("contract: every parameter at its minimum and at its maximum gives finite, bounded output")
{
    for (const auto& t : daw::fx::effectTypes())
    {
        auto probe = daw::fx::createEffect (t.id);
        if (probe == nullptr)
            continue;
        const auto& ps = probe->params();
        for (int i = 0; i < (int) ps.size(); ++i)
            for (const float v : { ps[(size_t) i].min, ps[(size_t) i].max })
            {
                auto fx = make (t.id.c_str());
                fx->setParam (i, v);
                fx->reset();
                auto io = noise (-6.0, 24000, 5);
                runVaried (*fx, io, { kBlock }, 2);
                check (finite (io), t.id + "." + ps[(size_t) i].id + " = " + std::to_string (v) + ": finite");
                // Output at +24 dB on a -6 dBFS input may reach about 16; anything far above is a runaway.
                check (peakOf (io) < 64.0, t.id + "." + ps[(size_t) i].id + " = " + std::to_string (v) + ": bounded (peak " + std::to_string (peakOf (io)) + ")");
            }
    }
}

FX_TEST ("contract: random settings at 44.1, 48 and 96 kHz, mono and stereo, odd blocks, stay finite")
{
    std::mt19937 rng (2026);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    for (const auto& t : daw::fx::effectTypes())
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            for (const int channels : { 1, 2 })
            {
                auto fx = make (t.id.c_str(), rate, 512);
                if (fx == nullptr)
                    continue;
                const auto& ps = fx->params();
                for (int round = 0; round < 4; ++round)
                {
                    for (int i = 0; i < (int) ps.size(); ++i)
                        fx->setParam (i, daw::fx::fromNormalised (ps[(size_t) i], unit (rng)));
                    auto io = noise (-6.0, 9600, (unsigned) round + 1);
                    runVaried (*fx, io, { 512, 1, 33, 128, 7 }, channels);
                    check (finite (io), t.id + ": finite at " + std::to_string ((int) rate) + " Hz, " + std::to_string (channels) + " channel(s), round "
                                            + std::to_string (round));
                }
            }
}

FX_TEST ("contract: latency and tail are sane and the latency fits the reported maximum after prepare")
{
    for (const auto& t : daw::fx::effectTypes())
    {
        auto fx = make (t.id.c_str());
        if (fx == nullptr)
            continue;
        check (fx->latencySamples() >= 0 && fx->latencySamples() < (int) kRate, t.id + ": latency between 0 and 1 s");
        check (fx->tailSeconds() >= 0.0 && fx->tailSeconds() <= 60.0, t.id + ": tail between 0 and 60 s");
    }
}
