#include "Spectrum.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"

#include "daw/dsp/FFT.h"

#include <complex>

namespace ui
{
namespace
{
constexpr int kFftSize = 4096;
constexpr float kFloorDb = -84.0f, kTopDb = -6.0f;
constexpr double kLowHz = 30.0, kHighHz = 18000.0;
} // namespace

Spectrum::Spectrum (AudioEngine& e) : engine (e), samples ((size_t) kFftSize), window ((size_t) kFftSize)
{
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kFftSize - 1));
    level.fill (kFloorDb);
    peak.fill (kFloorDb);
    setInterceptsMouseClicks (true, false);
}

juce::String Spectrum::helpAt (juce::Point<int>) const { return Strings::help::spectrum; }

void Spectrum::tick()
{
    if (! isShowing())
        return;
    engine.copyAnalysis (samples.data(), kFftSize);
    float loudest = 0.0f;
    std::vector<std::complex<double>> bins ((size_t) kFftSize);
    for (int i = 0; i < kFftSize; ++i)
    {
        loudest = std::max (loudest, std::abs (samples[(size_t) i]));
        bins[(size_t) i] = { (double) (samples[(size_t) i] * window[(size_t) i]), 0.0 };
    }
    const bool nowSilent = loudest < 1.0e-5f;
    if (nowSilent && silent)
        return; // nothing sounds and nothing is left to fall: no work, no repaint
    daw::dsp::fft (bins, false);

    const double rate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
    const double binHz = rate / kFftSize;
    bool moving = false;
    for (int b = 0; b < kBands; ++b)
    {
        // Each band spans a slice of the log axis; its value is the strongest bin in it (narrow tones stay visible).
        const double f0 = kLowHz * std::pow (kHighHz / kLowHz, (double) b / kBands);
        const double f1 = kLowHz * std::pow (kHighHz / kLowHz, (double) (b + 1) / kBands);
        const int i0 = juce::jlimit (1, kFftSize / 2 - 1, (int) std::floor (f0 / binHz));
        const int i1 = juce::jlimit (i0, kFftSize / 2 - 1, (int) std::ceil (f1 / binHz));
        double power = 0.0;
        for (int i = i0; i <= i1; ++i)
            power = std::max (power, std::norm (bins[(size_t) i]));
        // Hann gain and the one-sided spectrum, then a tilt of +4.5 dB/octave so a mix looks level, as the ear hears it.
        const double amplitude = 4.0 * std::sqrt (power) / kFftSize;
        const float db = (float) (20.0 * std::log10 (std::max (1.0e-9, amplitude)) + 4.5 * std::log2 (std::sqrt (f0 * f1) / 1000.0));
        const float target = juce::jlimit (kFloorDb, kTopDb + 6.0f, db);
        auto& l = level[(size_t) b];
        l = target > l ? target : l + (target - l) * 0.18f; // up at once, down slowly
        auto& p = peak[(size_t) b];
        p = std::max (l, p - 0.6f);
        moving = moving || l > kFloorDb + 0.5f || p > kFloorDb + 0.5f;
    }
    silent = nowSilent && ! moving;
    repaint();
}

void Spectrum::paint (juce::Graphics& g)
{
    const auto& t = theme();
    auto r = getLocalBounds().toFloat();
    fillCell (g, r, t.panel);
    auto area = r.reduced (6.0f, 4.0f);
    const auto yOf = [&] (float db) { return area.getBottom() - juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / (kTopDb - kFloorDb)) * area.getHeight(); };
    const auto xOf = [&] (int band) { return area.getX() + area.getWidth() * ((float) band + 0.5f) / (float) kBands; };

    juce::Path curve, fill;
    curve.startNewSubPath (area.getX(), yOf (level[0]));
    for (int b = 0; b < kBands; ++b)
    {
        // a quadratic through the middles keeps the line soft without overshooting
        const float x = xOf (b), y = yOf (level[(size_t) b]);
        if (b == 0)
            curve.lineTo (x, y);
        else
        {
            const float px = xOf (b - 1), py = yOf (level[(size_t) b - 1]);
            curve.quadraticTo (px, py, (px + x) * 0.5f, (py + y) * 0.5f);
        }
    }
    curve.lineTo (area.getRight(), yOf (level[kBands - 1]));
    fill = curve;
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();

    // The prism: the palette from bass to treble, brightest at the line, fading to the floor.
    juce::ColourGradient rainbow (t.palette (0), area.getX(), 0.0f, t.palette (7), area.getRight(), 0.0f, false);
    for (int i = 1; i < 7; ++i)
        rainbow.addColour (i / 7.0, t.palette (i));
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (fill);
        g.setGradientFill (rainbow);
        g.setOpacity (0.55f);
        g.fillRect (area);
        juce::ColourGradient fade (juce::Colours::transparentBlack, 0.0f, area.getY(), t.panel.withAlpha (0.85f), 0.0f, area.getBottom(), false);
        g.setGradientFill (fade);
        g.fillRect (area);
    }
    g.setGradientFill (rainbow);
    if (t.glow > 0.0f)
    {
        g.setOpacity (0.25f * t.glow);
        g.strokePath (curve, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setOpacity (1.0f);
    g.strokePath (curve, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // peaks: small dashes that fall slowly
    g.setColour (t.text.withAlpha (0.35f));
    for (int b = 0; b < kBands; b += 2)
        if (peak[(size_t) b] > kFloorDb + 1.0f)
            g.fillRect (xOf (b) - 1.0f, yOf (peak[(size_t) b]) - 0.5f, 2.0f, 1.0f);
}
} // namespace ui
