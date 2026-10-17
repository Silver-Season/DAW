#pragma once

#include <cstdint>
#include <vector>

namespace daw::engine
{
// Loopback latency test (docs/spec/05-audio-recording.md, task 5.4). The DAW played `numClicks` one-sample impulses
// `spacing` samples apart starting at sample 0 of the output, and recorded the input into `recorded`. For each click
// the lag (0..maxLag) is the position of the peak absolute value in the window [k * spacing, k * spacing + maxLag],
// which is the cross-correlation maximum for a single-sample impulse. The result is the median lag over all clicks
// whose peak stands out (peak > 8x the RMS of the window). ok is false if fewer than half the clicks were found.
struct RoundTripResult
{
    bool ok = false;
    int latencySamples = 0;
    int clicksFound = 0;
};
RoundTripResult measureRoundTrip (const float* recorded, int64_t numRecorded, int numClicks, int spacing, int maxLag);

// The click train to play: numClicks impulses of value `amplitude` every `spacing` samples, starting at 0.
std::vector<float> makeClickTrain (int numClicks, int spacing, float amplitude);

// Audio capture (task 5.7): finds the last phrase in a mono level signal, oldest sample first. A phrase is a stretch
// where the RMS over 50 ms windows (hop 10 ms) is above thresholdDb; pauses of maxGapSeconds or more split phrases.
// Returns the LAST phrase. start = onsetSample - preRollSeconds (clamped to 0), end = end of the last window above the
// threshold (exclusive). onsetSample is the first sample of the phrase whose |x| exceeds the threshold level, so a
// stopped transport can start at the first attack. found is false if nothing is above the threshold.
struct AudioPhrase
{
    bool found = false;
    int64_t start = 0, end = 0, onsetSample = 0;
};
AudioPhrase findLastAudioPhrase (const float* samples, int64_t numSamples, double sampleRate,
                                 double thresholdDb = -50.0, double maxGapSeconds = 4.0, double preRollSeconds = 0.2);

// Peak overview for drawing waveforms: for each block of samplesPerPeak samples the min and max (interleaved
// min, max pairs). The last partial block is included.
void computePeaks (const float* samples, int64_t numSamples, int samplesPerPeak, std::vector<float>& minMaxOut);
} // namespace daw::engine
