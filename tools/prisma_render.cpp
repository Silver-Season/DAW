// Offline renderer: plays a short demo song through the core engine (clips -> Prisma) into a WAV file,
// and reports how fast the engine runs compared to real time. No JUCE, no audio device needed.
//
//   prisma_render out.wav
#include "daw/engine/Clip.h"
#include "daw/engine/DemoContent.h"
#include "daw/synth/PrismaSynth.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace daw;

namespace
{
constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 256;
constexpr double kBpm = 124.0;

struct Track
{
    std::unique_ptr<synth::PrismaSynth> synth = std::make_unique<synth::PrismaSynth>();
    engine::ClipPlayer player;
    float gain = 1.0f;
};

const synth::PrismaParams& preset (const std::string& name)
{
    for (auto& p : synth::factoryPresets())
        if (p.name == name)
            return p.params;
    return synth::factoryPresets()[0].params;
}

void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t frames = (uint32_t) l.size(), dataBytes = frames * 4;
    f.write ("RIFF", 4);
    u32 (36 + dataBytes);
    f.write ("WAVEfmt ", 8);
    u32 (16);
    u16 (1);
    u16 (2);
    u32 ((uint32_t) kSampleRate);
    u32 ((uint32_t) kSampleRate * 4);
    u16 (4);
    u16 (16);
    f.write ("data", 4);
    u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float s : { l[i], r[i] })
        {
            const auto v = (int16_t) std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f);
            f.write ((const char*) &v, 2);
        }
}
} // namespace

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "prisma_demo.wav";

    using namespace engine::demo;
    auto pad = padClip(), arp = arpClip(), reese = bassClip (false), wobble = bassClip (true), lead = leadClip();

    std::vector<Track> tracks (4);
    const char* presets[] = { "Glass Pad", "Pluck", "Reese Bass", "Supersaw Lead" };
    const float gains[] = { 0.8f, 0.5f, 0.8f, 0.55f };
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        tracks[i].synth->prepare (kSampleRate, kBlock);
        tracks[i].synth->setParams (preset (presets[i]));
        tracks[i].synth->setTempo (kBpm);
        tracks[i].gain = gains[i];
    }

    engine::Transport transport;
    transport.prepare (kSampleRate);
    transport.setTempo (kBpm);
    transport.play();

    tracks[0].player.launch (0, &pad, 0, 0);
    tracks[1].player.launch (0, &arp, 0, 0);
    tracks[2].player.launch (0, &reese, 0, 0);

    const double songBeats = 48.0; // 12 bars: intro (4), drop (8)
    const int totalSamples = (int) (songBeats * transport.samplesPerBeat() + 3.0 * kSampleRate);

    std::vector<float> mixL ((size_t) totalSamples), mixR ((size_t) totalSamples);
    std::vector<float> bufL (kBlock), bufR (kBlock);
    std::vector<MidiEvent> events;
    events.reserve (512);

    bool dropQueued = false, endQueued = false;
    const auto t0 = std::chrono::steady_clock::now();

    for (int pos = 0; pos < totalSamples; pos += kBlock)
    {
        const int n = std::min (kBlock, totalSamples - pos);
        const auto blk = transport.advance (n);

        if (! dropQueued && blk.startBeat >= 12.0)
        {
            // Bar 5: swap the bass for the wobble preset and bring in the lead (quantised to the bar).
            tracks[2].synth->setParams (preset ("Wobble Bass"));
            tracks[2].player.launch (1, &wobble, blk.startBeat, 4.0);
            tracks[3].player.launch (0, &lead, blk.startBeat, 4.0);
            dropQueued = true;
        }
        if (! endQueued && blk.startBeat >= songBeats - 1.0)
        {
            for (auto& t : tracks)
                t.player.stop (blk.startBeat, 4.0);
            endQueued = true;
        }

        for (auto& t : tracks)
        {
            events.clear();
            t.player.process (blk.startBeat, blk.endBeat, transport.samplesPerBeat(), n, events);
            engine::sortEvents (events);
            t.synth->process (bufL.data(), bufR.data(), n, events.data(), (int) events.size());
            for (int i = 0; i < n; ++i)
            {
                mixL[(size_t) (pos + i)] += bufL[(size_t) i] * t.gain;
                mixR[(size_t) (pos + i)] += bufR[(size_t) i] * t.gain;
            }
        }
    }

    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double audioSeconds = totalSamples / kSampleRate;

    float peak = 0;
    for (size_t i = 0; i < mixL.size(); ++i)
        peak = std::max (peak, std::max (std::abs (mixL[i]), std::abs (mixR[i])));
    const float norm = peak > 0.0f ? 0.89f / peak : 1.0f; // normalise to about -1 dBFS
    for (size_t i = 0; i < mixL.size(); ++i)
    {
        mixL[i] *= norm;
        mixR[i] *= norm;
    }

    writeWav (out, mixL, mixR);
    std::printf ("Rendered %.1f s of audio (4 tracks of Prisma) in %.2f s: %.1fx faster than real time. Peak before normalising %.2f\n",
                 audioSeconds, seconds, audioSeconds / seconds, peak);
    std::printf ("Wrote %s\n", out.c_str());
    return 0;
}
