#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

// Small building blocks shared by the built-in effects.
namespace daw::fx
{

inline float dbToGain (float db) { return db <= -120.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }

// One-pole smoothing towards a target: 20 ms by default (63 % of a step in that time).
struct Smoother
{
    void prepare (double sampleRate, double ms = 20.0)
    {
        coef = (float) (1.0 - std::exp (-1.0 / std::max (1.0, ms * 0.001 * sampleRate)));
    }
    void setTarget (float t) { target = t; }
    void snap() { current = target; }
    float next()
    {
        current += (target - current) * coef;
        return current;
    }
    bool smoothing() const { return std::abs (target - current) > 1.0e-7f * (1.0f + std::abs (target)); }

    float current = 0.0f, target = 0.0f, coef = 1.0f;
};

// RBJ "Audio EQ Cookbook" biquad, transposed direct form II, coefficients in double.
struct Biquad
{
    enum class Type
    {
        lowPass,
        highPass,
        bandPass,
        notch,
        peak,
        lowShelf,
        highShelf,
        allPass
    };

    void set (Type type, double freq, double q, double gainDb, double sampleRate)
    {
        const double f = std::clamp (freq, 1.0, sampleRate * 0.49);
        const double w = 2.0 * std::numbers::pi * f / sampleRate;
        const double cw = std::cos (w), sw = std::sin (w);
        const double qq = std::max (0.01, q);
        const double alpha = sw / (2.0 * qq);
        const double a = std::pow (10.0, gainDb / 40.0);
        double nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;
        switch (type)
        {
            case Type::lowPass:
                nb0 = (1 - cw) / 2; nb1 = 1 - cw; nb2 = (1 - cw) / 2;
                na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
            case Type::highPass:
                nb0 = (1 + cw) / 2; nb1 = -(1 + cw); nb2 = (1 + cw) / 2;
                na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
            case Type::bandPass:
                nb0 = alpha; nb1 = 0; nb2 = -alpha;
                na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
            case Type::notch:
                nb0 = 1; nb1 = -2 * cw; nb2 = 1;
                na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
            case Type::peak:
                nb0 = 1 + alpha * a; nb1 = -2 * cw; nb2 = 1 - alpha * a;
                na0 = 1 + alpha / a; na1 = -2 * cw; na2 = 1 - alpha / a;
                break;
            case Type::lowShelf:
            {
                const double s = 2 * std::sqrt (a) * alpha;
                nb0 = a * ((a + 1) - (a - 1) * cw + s); nb1 = 2 * a * ((a - 1) - (a + 1) * cw); nb2 = a * ((a + 1) - (a - 1) * cw - s);
                na0 = (a + 1) + (a - 1) * cw + s; na1 = -2 * ((a - 1) + (a + 1) * cw); na2 = (a + 1) + (a - 1) * cw - s;
                break;
            }
            case Type::highShelf:
            {
                const double s = 2 * std::sqrt (a) * alpha;
                nb0 = a * ((a + 1) + (a - 1) * cw + s); nb1 = -2 * a * ((a - 1) + (a + 1) * cw); nb2 = a * ((a + 1) + (a - 1) * cw - s);
                na0 = (a + 1) - (a - 1) * cw + s; na1 = 2 * ((a - 1) - (a + 1) * cw); na2 = (a + 1) - (a - 1) * cw - s;
                break;
            }
            case Type::allPass:
                nb0 = 1 - alpha; nb1 = -2 * cw; nb2 = 1 + alpha;
                na0 = 1 + alpha; na1 = -2 * cw; na2 = 1 - alpha;
                break;
        }
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }

    float tick (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void reset() { z1 = z2 = 0.0; }

    // |H| at `freq` in dB.
    double magnitudeDb (double freq, double sampleRate) const
    {
        const double w = 2.0 * std::numbers::pi * freq / sampleRate;
        const std::complex<double> z1c = std::polar (1.0, -w), z2c = std::polar (1.0, -2.0 * w);
        const auto h = (b0 + b1 * z1c + b2 * z2c) / (1.0 + a1 * z1c + a2 * z2c);
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
};

} // namespace daw::fx
