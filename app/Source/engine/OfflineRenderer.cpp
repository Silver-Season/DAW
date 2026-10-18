#include "OfflineRenderer.h"

#include "AudioEngine.h"
#include "Mp3Writer.h"

#include "daw/dsp/Loudness.h"

#include <random>

namespace
{
constexpr int kBlock = 1024;

// The device the copy's engine is prepared with: stereo out, the project's rate, no inputs.
class OfflineDevice final : public juce::AudioIODevice
{
public:
    explicit OfflineDevice (double rate) : AudioIODevice ("Offline", "Offline"), sampleRate (rate) {}
    juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { sampleRate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { kBlock }; }
    int getDefaultBufferSize() override { return kBlock; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return kBlock; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 0; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

private:
    double sampleRate;
};

juce::String safeFileName (const juce::String& name)
{
    auto s = juce::File::createLegalFileName (name.trim());
    return s.isEmpty() ? juce::String ("Track") : s;
}
} // namespace

// A stem while the song renders: its track and the float file its blocks go to.
struct OfflineRenderer::Stem
{
    int64_t trackId = -1;
    juce::File temp, target;
    std::unique_ptr<juce::AudioFormatWriter> writer;
};

OfflineRenderer::OfflineRenderer (Settings& s, const ProjectModel& source, AudioEngine& live, const Options& o)
    : juce::Thread ("Export"), settings (s), options (o)
{
    sampleRate = live.getSampleRate() > 0.0 ? live.getSampleRate() : 48000.0;
    // Every device's live state goes into the copy, so the export sounds like what the user hears.
    live.captureDeviceStates (source.root);
    model = std::make_unique<ProjectModel> (source.root.createCopy());
    model->setMetronome (false);
    model->setArrangementLoopOn (false);
    model->setArrangementPlayStart (juce::jmax (0.0, options.startBeat));
    model->undo.clearUndoHistory();
    engine = std::make_unique<AudioEngine> (settings, false);
    engine->setProjectFolder (live.getProjectFolder());
    engine->setModel (model.get());
    engine->getWarp().setRenderInline (true);
}

OfflineRenderer::~OfflineRenderer()
{
    stopThread (10000);
    if (engine != nullptr)
        engine->setModel (nullptr);
}

void OfflineRenderer::waitForDevices (int attempts)
{
    if (engine->isAnyDeviceLoading() && attempts > 0 && ! threadShouldExit())
    {
        juce::Timer::callAfterDelay (50, [this, attempts] { waitForDevices (attempts - 1); });
        return;
    }
    startThread (juce::Thread::Priority::normal);
}

void OfflineRenderer::start (std::function<void (const Result&)> done)
{
    onDone = std::move (done);
    engine->rebuildGraph();
    waitForDevices (200); // hosted plugins load asynchronously; give them up to 10 s
}

void OfflineRenderer::run()
{
    result = renderNow();
    juce::MessageManager::callAsync ([this, r = result] {
        if (onDone)
            onDone (r);
    });
}

OfflineRenderer::Result OfflineRenderer::renderNow()
{
    Result r;
    OfflineDevice device (sampleRate);
    engine->rebuildGraphIfNeeded();
    engine->audioDeviceAboutToStart (&device);
    engine->getAudioPool().waitForLoads();
    engine->rebuildGraph(); // with every file loaded, warped clips get their stretched copies (rendered here, see WarpService)
    const auto nonRealtime = [this] (const juce::ValueTree& devicesNode) {
        for (const auto& d : DeviceRef::childrenOf (devicesNode))
            if (auto* p = engine->findProcessor (d.id()))
                p->setNonRealtime (true);
    };
    for (int i = 0; i < model->numTracks(); ++i)
        nonRealtime (model->trackAt (i).devicesNode());
    nonRealtime (model->masterDevicesNode());

    // Stems: a float file per track, written block by block (a whole song per track would not fit in memory).
    std::vector<Stem> stems;
    if (options.stems)
    {
        engine->setStemCapture (true);
        juce::WavAudioFormat wav;
        int number = 0;
        for (const auto& track : model->tracks())
        {
            ++number;
            if (track.mute() && ! options.stemsIncludeMuted)
                continue;
            Stem stem;
            stem.trackId = track.id();
            stem.target = options.stemsFolder.getChildFile (juce::String (number).paddedLeft ('0', 2) + " " + safeFileName (track.name()) + "."
                                                            + (options.format == "aiff" ? "aif" : options.format));
            stem.temp = stem.target.getSiblingFile (stem.target.getFileNameWithoutExtension() + ".render.part");
            options.stemsFolder.createDirectory();
            stem.temp.deleteFile();
            if (auto out = stem.temp.createOutputStream())
                if (auto* w = wav.createWriterFor (out.get(), sampleRate, 2, 32, {}, 0))
                {
                    out.release();
                    stem.writer.reset (w);
                }
            if (stem.writer == nullptr)
            {
                r.error = "cannot write " + stem.temp.getFullPathName();
                engine->audioDeviceStopped();
                return r;
            }
            stems.push_back (std::move (stem));
        }
    }
    const auto dropStemTemps = [&stems] {
        for (auto& stem : stems)
        {
            stem.writer.reset();
            stem.temp.deleteFile();
        }
    };

    const double tempo = juce::jmax (1.0, model->tempo());
    const double lengthBeats = juce::jmax (0.0, options.endBeat - options.startBeat);
    const auto songSamples = (int64_t) std::ceil (lengthBeats * 60.0 / tempo * sampleRate);
    const auto maxTail = options.tail ? (int64_t) (10.0 * sampleRate) : 0;
    juce::AudioBuffer<float> out (2, (int) juce::jmin<int64_t> (songSamples + maxTail + kBlock, (int64_t) std::numeric_limits<int>::max() / 4));
    out.clear();
    juce::AudioBuffer<float> block (2, kBlock);

    engine->play();
    int64_t written = 0, quietSince = -1;
    const float quiet = juce::Decibels::decibelsToGain (-90.0f);
    while (! threadShouldExit())
    {
        block.clear();
        engine->audioDeviceIOCallbackWithContext (nullptr, 0, block.getArrayOfWritePointers(), 2, kBlock, {});
        const int n = (int) juce::jmin<int64_t> (kBlock, out.getNumSamples() - written);
        if (n <= 0)
            break;
        out.copyFrom (0, (int) written, block, 0, 0, n);
        out.copyFrom (1, (int) written, block, 1, 0, n);
        for (auto& stem : stems)
            if (auto* rt = engine->findRuntime (stem.trackId))
                stem.writer->writeFromAudioSampleBuffer (rt->stemBuffer, 0, n);
        written += n;
        progress = juce::jlimit (0.0, stems.empty() ? 1.0 : 0.8, (double) written / (double) juce::jmax<int64_t> (1, songSamples) * (stems.empty() ? 1.0 : 0.8));
        if (written < songSamples)
            continue;
        if (! options.tail)
            break;
        // The tail: until a second of silence, at most 10 s
        const float peak = juce::jmax (block.getMagnitude (0, 0, n), block.getMagnitude (1, 0, n));
        if (peak >= quiet)
            quietSince = -1;
        else if (quietSince < 0)
            quietSince = written - n;
        if ((quietSince >= 0 && written - quietSince >= (int64_t) sampleRate) || written >= songSamples + maxTail)
            break;
    }
    engine->stop();
    engine->audioDeviceStopped();
    engine->setStemCapture (false);
    for (auto& stem : stems)
        stem.writer.reset(); // flushes the float file
    if (threadShouldExit())
    {
        dropStemTemps();
        r.error = "cancelled";
        return r;
    }

    // Without the tail the file ends exactly at the end of the range; with it, where the silence began.
    int64_t length = options.tail && quietSince >= 0 ? juce::jmax (songSamples, quietSince) : juce::jmin (written, songSamples);
    length = juce::jmax<int64_t> (1, juce::jmin (length, written));
    out.setSize (2, (int) length, true);

    // One gain for the master and every stem: from the peak, or from the loudness held under -1 dBTP.
    const float peak = out.getMagnitude (0, (int) length);
    r.sourcePeakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
    float gain = 1.0f;
    if (options.normaliseLufs)
    {
        const auto loud = daw::dsp::measureLoudness (out.getReadPointer (0), out.getReadPointer (1), (size_t) length, sampleRate);
        if (loud.integratedLufs > -70.0)
        {
            double gainDb = options.lufsTarget - loud.integratedLufs;
            if (loud.truePeakDb + gainDb > -1.0)
            {
                gainDb = -1.0 - loud.truePeakDb;
                r.limitedByPeak = true;
            }
            gain = (float) juce::Decibels::decibelsToGain (gainDb);
        }
    }
    else if (options.normalisePeakDb <= 0.0 && peak > 0.0f)
        gain = juce::Decibels::decibelsToGain ((float) options.normalisePeakDb) / peak;
    if (std::abs (gain - 1.0f) > 1.0e-7f)
        out.applyGain (gain);

    const auto report = daw::dsp::measureLoudness (out.getReadPointer (0), out.getReadPointer (1), (size_t) length, sampleRate);
    r.lufs = report.integratedLufs;
    r.rangeLu = report.rangeLu;
    r.truePeakDb = report.truePeakDb;
    r.peakDb = juce::Decibels::gainToDecibels (out.getMagnitude (0, (int) length), -120.0f);
    r.seconds = (double) length / sampleRate;

    r.ok = ! options.stems || options.stemsIncludeMaster ? writeFile (out, options.file, r.error) : true;
    // The stems: read back, the common gain, the final format, the same length as the master.
    for (size_t i = 0; i < stems.size() && r.ok; ++i)
    {
        auto& stem = stems[i];
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stem.temp.createInputStream().release(), true));
        juce::AudioBuffer<float> audio (2, (int) length);
        audio.clear();
        if (reader != nullptr)
            reader->read (&audio, 0, (int) juce::jmin<juce::int64> (length, reader->lengthInSamples), 0, true, true);
        reader.reset();
        audio.applyGain (gain);
        r.ok = writeFile (audio, stem.target, r.error);
        if (r.ok)
            r.stemFiles.add (stem.target.getFullPathName());
        progress = 0.8 + 0.2 * (double) (i + 1) / (double) stems.size();
    }
    dropStemTemps();
    progress = 1.0;
    return r;
}

bool OfflineRenderer::writeFile (const juce::AudioBuffer<float>& source, const juce::File& file, juce::String& error) const
{
    file.getParentDirectory().createDirectory();
    const auto temp = file.getSiblingFile (file.getFileName() + ".part");
    temp.deleteFile();

    if (options.format == "mp3")
    {
        Mp3Writer::Settings s;
        s.kbps = options.mp3Kbps;
        s.vbrQuality = options.mp3Vbr;
        s.title = model->name().isNotEmpty() ? model->name() : file.getFileNameWithoutExtension();
        s.artist = model->author();
        s.tempo = model->tempo();
        Mp3Writer mp3;
        bool ok = mp3.open (temp, sampleRate, s, error);
        for (int at = 0; ok && at < source.getNumSamples(); at += kBlock)
        {
            const int n = juce::jmin (kBlock, source.getNumSamples() - at);
            ok = mp3.write (source.getReadPointer (0, at), source.getReadPointer (1, at), n);
        }
        ok = mp3.close() && ok;
        if (! ok || ! temp.moveFileTo (file))
        {
            if (error.isEmpty())
                error = "cannot write " + file.getFullPathName();
            temp.deleteFile();
            return false;
        }
        return true;
    }

    std::unique_ptr<juce::AudioFormat> format;
    if (options.format == "aiff")
        format = std::make_unique<juce::AiffAudioFormat>();
    else if (options.format == "flac")
        format = std::make_unique<juce::FlacAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();
    int bits = options.bitDepth;
    if (options.format == "flac" && bits > 24)
        bits = 24;

    // 16 bits: TPDF dither of one step, the same noise on every export (fixed seed).
    const juce::AudioBuffer<float>* audio = &source;
    juce::AudioBuffer<float> dithered;
    if (bits == 16)
    {
        dithered.makeCopyOf (source);
        std::mt19937 rng (16);
        std::uniform_real_distribution<float> uniform (-0.5f, 0.5f);
        const float lsb = 1.0f / 32768.0f;
        for (int c = 0; c < dithered.getNumChannels(); ++c)
        {
            auto* d = dithered.getWritePointer (c);
            for (int i = 0; i < dithered.getNumSamples(); ++i)
                d[i] += (uniform (rng) + uniform (rng)) * lsb;
        }
        audio = &dithered;
    }

    std::unique_ptr<juce::OutputStream> stream (temp.createOutputStream().release());
    if (stream == nullptr)
    {
        error = "cannot write " + temp.getFullPathName();
        return false;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer (format->createWriterFor (stream.get(), sampleRate, 2, bits, {}, 0));
    if (writer == nullptr)
    {
        error = "format";
        temp.deleteFile();
        return false;
    }
    stream.release();
    const bool ok = writer->writeFromAudioSampleBuffer (*audio, 0, audio->getNumSamples());
    writer.reset();
    if (! ok || ! temp.moveFileTo (file))
    {
        error = "cannot write " + file.getFullPathName();
        temp.deleteFile();
        return false;
    }
    return true;
}
