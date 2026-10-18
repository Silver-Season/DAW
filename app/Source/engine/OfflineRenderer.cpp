#include "OfflineRenderer.h"

#include "AudioEngine.h"

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
} // namespace

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
    for (int i = 0; i < model->numTracks(); ++i)
        for (const auto& d : model->trackAt (i).devices())
            if (auto* p = engine->findProcessor (d.id()))
                p->setNonRealtime (true);

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
        written += n;
        progress = juce::jlimit (0.0, 1.0, (double) written / (double) juce::jmax<int64_t> (1, songSamples));
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
    if (threadShouldExit())
    {
        r.error = "cancelled";
        return r;
    }

    // Without the tail the file ends exactly at the end of the range; with it, where the silence began.
    int64_t length = options.tail && quietSince >= 0 ? juce::jmax (songSamples, quietSince) : juce::jmin (written, songSamples);
    length = juce::jmax<int64_t> (1, juce::jmin (length, written));
    out.setSize (2, (int) length, true);

    const float peak = out.getMagnitude (0, (int) length);
    r.sourcePeakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
    if (options.normalisePeakDb <= 0.0 && peak > 0.0f)
        out.applyGain (juce::Decibels::decibelsToGain ((float) options.normalisePeakDb) / peak);
    r.peakDb = juce::Decibels::gainToDecibels (out.getMagnitude (0, (int) length), -120.0f);
    r.seconds = (double) length / sampleRate;
    r.ok = writeFile (out, r);
    progress = 1.0;
    return r;
}

bool OfflineRenderer::writeFile (const juce::AudioBuffer<float>& audio, Result& r) const
{
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

    options.file.getParentDirectory().createDirectory();
    const auto temp = options.file.getSiblingFile (options.file.getFileName() + ".part");
    temp.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (temp.createOutputStream().release());
    if (stream == nullptr)
    {
        r.error = "cannot write " + temp.getFullPathName();
        return false;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer (format->createWriterFor (stream.get(), sampleRate, 2, bits, {}, 0));
    if (writer == nullptr)
    {
        r.error = "format";
        temp.deleteFile();
        return false;
    }
    stream.release();
    const bool ok = writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    writer.reset();
    if (! ok || ! temp.moveFileTo (options.file))
    {
        r.error = "cannot write " + options.file.getFullPathName();
        temp.deleteFile();
        return false;
    }
    return true;
}
