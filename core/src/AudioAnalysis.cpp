#include "daw/engine/AudioAnalysis.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{
std::vector<float> makeClickTrain (int numClicks, int spacing, float amplitude)
{
    std::vector<float> out ((size_t) std::max (0, numClicks) * (size_t) std::max (0, spacing), 0.0f);
    for (int k = 0; k < numClicks; ++k)
        out[(size_t) k * (size_t) spacing] = amplitude;
    return out;
}

RoundTripResult measureRoundTrip (const float* recorded, int64_t numRecorded, int numClicks, int spacing, int maxLag)
{
    RoundTripResult result;
    std::vector<int> lags;
    lags.reserve ((size_t) std::max (0, numClicks));

    for (int k = 0; k < numClicks; ++k)
    {
        const int64_t from = (int64_t) k * spacing;
        const int64_t to = std::min<int64_t> (numRecorded, from + maxLag + 1); // exclusive
        if (to <= from)
            continue;

        float peak = 0.0f;
        int64_t peakAt = from;
        double sumSq = 0.0;
        for (int64_t i = from; i < to; ++i)
        {
            const float a = std::abs (recorded[i]);
            sumSq += (double) a * a;
            if (a > peak)
            {
                peak = a;
                peakAt = i;
            }
        }
        const double rms = std::sqrt (sumSq / (double) (to - from));
        if (rms > 0.0 && peak > 8.0 * rms)
            lags.push_back ((int) (peakAt - from));
    }

    result.clicksFound = (int) lags.size();
    if (lags.empty() || result.clicksFound * 2 < numClicks)
        return result;

    std::sort (lags.begin(), lags.end());
    result.latencySamples = lags[lags.size() / 2];
    result.ok = true;
    return result;
}

AudioPhrase findLastAudioPhrase (const float* samples, int64_t numSamples, double sampleRate,
                                 double thresholdDb, double maxGapSeconds, double preRollSeconds)
{
    AudioPhrase phrase;
    if (numSamples <= 0 || sampleRate <= 0.0)
        return phrase;

    const int64_t win = std::max<int64_t> (1, std::llround (0.050 * sampleRate));
    const int64_t hop = std::max<int64_t> (1, std::llround (0.010 * sampleRate));
    const int64_t numWindows = numSamples >= win ? (numSamples - win) / hop + 1 : 1;
    const double thresholdLin = std::pow (10.0, thresholdDb / 20.0);
    const double thresholdPower = thresholdLin * thresholdLin;

    auto windowStart = [&] (int64_t w) { return w * hop; };
    auto windowEnd = [&] (int64_t w) { return std::min (numSamples, w * hop + win); };
    auto above = [&] (int64_t w)
    {
        const int64_t a = windowStart (w), b = windowEnd (w);
        double sumSq = 0.0;
        for (int64_t i = a; i < b; ++i)
            sumSq += (double) samples[i] * samples[i];
        return sumSq / (double) (b - a) > thresholdPower;
    };

    int64_t lastW = -1;
    for (int64_t w = numWindows - 1; w >= 0; --w)
        if (above (w))
        {
            lastW = w;
            break;
        }
    if (lastW < 0)
        return phrase;

    // Walk back through the audio; a pause of maxGapSeconds or more between loud windows ends the phrase.
    int64_t firstW = lastW;
    for (int64_t w = lastW - 1; w >= 0; --w)
    {
        if (! above (w))
            continue;
        const double gap = (double) (windowStart (firstW) - windowEnd (w)) / sampleRate;
        if (gap >= maxGapSeconds)
            break;
        firstW = w;
    }

    phrase.found = true;
    phrase.end = windowEnd (lastW);
    int64_t onset = windowStart (firstW);
    for (int64_t i = windowStart (firstW); i < windowEnd (firstW); ++i)
        if (std::abs ((double) samples[i]) > thresholdLin)
        {
            onset = i;
            break;
        }
    phrase.onsetSample = onset;
    phrase.start = std::max<int64_t> (0, onset - std::llround (preRollSeconds * sampleRate));
    return phrase;
}

void computePeaks (const float* samples, int64_t numSamples, int samplesPerPeak, std::vector<float>& minMaxOut)
{
    minMaxOut.clear();
    if (numSamples <= 0 || samplesPerPeak <= 0)
        return;
    minMaxOut.reserve ((size_t) ((numSamples + samplesPerPeak - 1) / samplesPerPeak) * 2);
    for (int64_t from = 0; from < numSamples; from += samplesPerPeak)
    {
        const int64_t to = std::min<int64_t> (numSamples, from + samplesPerPeak);
        float lo = samples[from], hi = samples[from];
        for (int64_t i = from + 1; i < to; ++i)
        {
            lo = std::min (lo, samples[i]);
            hi = std::max (hi, samples[i]);
        }
        minMaxOut.push_back (lo);
        minMaxOut.push_back (hi);
    }
}
} // namespace daw::engine
