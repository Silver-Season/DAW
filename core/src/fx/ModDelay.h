#pragma once

// Private building blocks of the modulation effects (chorus, flanger, phaser, auto pan). Not part of the public API.

#include "daw/fx/Effect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace daw::fx::mod
{

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// The LFO and the phaser coefficients are refreshed every few samples: the sweep is far slower than that, and it
// saves a sin/cos/tan per sample per channel.
constexpr int kControlMask = 7; // refresh when (sample index & kControlMask) == 0

// Cycle length in quarter notes for the tempo-synced divisions of the flanger and phaser ("1/4" ... "8/1").
inline constexpr std::array<double, 6> kSlowDivisions { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
// ... and for the auto pan ("1/32" ... "2/1").
inline constexpr std::array<double, 7> kFastDivisions { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };

// A tiny values below this are flushed so that feedback loops do not spend their tails in the denormal range.
inline float flush (float v) { return std::abs (v) < 1.0e-20f ? 0.0f : v; }

// Fractional delay line with 3rd-order Lagrange interpolation. read() counts the delay back from the sample that
// is written next, so that a feedback loop can read first and write after: read (d) then write (x) gives y[n] = x[n - d].
class DelayLine
{
public:
    void allocate (int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        limit = (float) (size - 4);
        pos = 0;
    }

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        pos = 0;
    }

    void write (float x)
    {
        buffer[(size_t) pos] = x;
        pos = (pos + 1) & mask;
    }

    // The stencil reaches one sample newer than the integer delay, so the shortest usable delay is 2 samples.
    float read (float delaySamples) const
    {
        const float d = std::clamp (delaySamples, 2.0f, limit);
        const int whole = (int) d;
        const float f = d - (float) whole;
        const int base = pos - whole;
        const float ym1 = buffer[(size_t) ((base + 1) & mask)];
        const float y0 = buffer[(size_t) (base & mask)];
        const float y1 = buffer[(size_t) ((base - 1) & mask)];
        const float y2 = buffer[(size_t) ((base - 2) & mask)];
        const float fm1 = f - 1.0f, fm2 = f - 2.0f, fp1 = f + 1.0f;
        return ym1 * (-f * fm1 * fm2 * (1.0f / 6.0f)) + y0 * (fp1 * fm1 * fm2 * 0.5f) + y1 * (-fp1 * f * fm2 * 0.5f)
               + y2 * (fp1 * f * fm1 * (1.0f / 6.0f));
    }

private:
    std::vector<float> buffer;
    int mask = 15, pos = 0;
    float limit = 12.0f;
};

// The phase of an LFO in cycles, free-running or locked to the song. With sync the phase is taken from the position
// of the song (ppq) whenever the host tells us about a new block, so that it lines up at every start and after every
// locate; between two such moments it just runs on at the tempo.
class ModClock
{
public:
    void prepare (double sampleRate) { fs = sampleRate; }

    void reset()
    {
        cycles = 0.0;
        locatePending = false;
    }

    void setTransport (const TransportInfo& t)
    {
        transport = t;
        transport.bpm = std::clamp (t.bpm, 1.0, 999.0);
        locatePending = true;
    }

    // Call at the start of every chunk. Returns true if the phase jumped (the song was relocated).
    bool begin (bool sync, double periodQuarters)
    {
        bool jumped = false;
        if (sync && locatePending && transport.playing)
        {
            const double target = transport.ppq / periodQuarters;
            double d = target - cycles;
            d -= std::round (d);
            jumped = std::abs (d) > 1.0e-3;
            cycles = target; // also removes any drift that built up since the last block
        }
        locatePending = false;
        return jumped;
    }

    double syncIncrement (double periodQuarters) const { return transport.bpm / 60.0 / periodQuarters / fs; }

    void advance (double increment)
    {
        cycles += increment;
        if (cycles > 1.0e6)
            cycles -= 1.0e6; // keeps the double precise after days of running; costs one phase glitch
    }

    double phase() const { return cycles - std::floor (cycles); }

    double cycles = 0.0;

private:
    double fs = 48000.0;
    TransportInfo transport;
    bool locatePending = false;
};

} // namespace daw::fx::mod
