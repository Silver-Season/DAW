#include "AudioRecorder.h"

#include "../project/AudioImport.h"

AudioRecorder::AudioRecorder (AudioEngine& e, ProjectModel& m) : juce::Thread ("Recording writer"), engine (e), model (m)
{
    startThread (juce::Thread::Priority::high);
}

AudioRecorder::~AudioRecorder()
{
    stopThread (2000);
    std::lock_guard<std::mutex> sl (jobsLock);
    for (auto& [id, job] : jobs)
        if (job->stream != nullptr)
            job->stream->cancel();
}

bool AudioRecorder::recordsSound (const TrackRef& track) const
{
    if (! track.isValid())
        return false;
    if (track.isAudio())
        return AudioInputSpec::parse (track.input()).kind != AudioInputKind::none;
    return track.recordSource() == "audio";
}

juce::File AudioRecorder::recordingsFolder() const
{
    const auto project = engine.getProjectFolder();
    if (project != juce::File())
        return project.getChildFile ("Samples").getChildFile ("Recorded");
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("DAW").getChildFile ("Recordings");
}

bool AudioRecorder::begin (int64_t trackId, RecordStream::Mode mode)
{
    const auto track = model.findTrack (trackId);
    auto* rt = engine.findRuntime (trackId);
    if (! recordsSound (track) || rt == nullptr || rt->recordStream == nullptr)
        return false;
    {
        std::lock_guard<std::mutex> sl (jobsLock);
        if (const auto it = jobs.find (trackId); it != jobs.end())
        {
            // A finished recording nobody collected (it was cancelled after it began): its file stays, the stream is free.
            if (! it->second->finished.load())
                return false;
            it->second->stream->reset();
            jobs.erase (it);
        }
    }

    const int channels = track.isAudio() ? AudioInputSpec::parse (track.input()).numChannels() : 2;
    const double rate = engine.getSampleRate();
    const auto folder = recordingsFolder();
    folder.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    const auto base = juce::File::createLegalFileName (track.name() + " " + stamp);
    auto file = folder.getChildFile (base + ".wav");
    for (int i = 2; file.exists(); ++i)
        file = folder.getChildFile (base + " " + juce::String (i) + ".wav");

    std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
    if (out == nullptr)
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), rate, (unsigned int) channels, engine.getSettings().recordBitDepth(), {}, 0));
    if (writer == nullptr)
    {
        out.reset();
        file.deleteFile();
        return false;
    }
    out.release(); // the writer owns it

    auto stream = rt->recordStream;
    if (! stream->request (mode, channels))
    {
        writer.reset();
        file.deleteFile();
        return false;
    }

    auto job = std::make_shared<Job>();
    job->trackId = trackId;
    job->stream = stream;
    job->writer = std::move (writer);
    job->info.file = file;
    job->info.name = track.name();
    job->info.channels = channels;
    job->info.sampleRate = rate;
    std::lock_guard<std::mutex> sl (jobsLock);
    jobs[trackId] = std::move (job);
    return true;
}

bool AudioRecorder::cancel (int64_t trackId)
{
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> sl (jobsLock);
        const auto it = jobs.find (trackId);
        if (it == jobs.end() || ! it->second->stream->cancel())
            return false;
        job = it->second;
        jobs.erase (it);
    }
    job->writer.reset();
    job->info.file.deleteFile();
    return true;
}

bool AudioRecorder::isRecording (int64_t trackId) const
{
    std::lock_guard<std::mutex> sl (jobsLock);
    return jobs.count (trackId) > 0;
}

bool AudioRecorder::isFinished (int64_t trackId) const
{
    std::lock_guard<std::mutex> sl (jobsLock);
    const auto it = jobs.find (trackId);
    return it != jobs.end() && it->second->finished.load();
}

bool AudioRecorder::takeFinished (int64_t trackId, Finished& out)
{
    std::lock_guard<std::mutex> sl (jobsLock);
    const auto it = jobs.find (trackId);
    if (it == jobs.end() || ! it->second->finished.load())
        return false;
    out = it->second->info;
    it->second->stream->reset();
    jobs.erase (it);
    return true;
}

int64_t AudioRecorder::addToPool (const Finished& f)
{
    const auto folder = engine.getProjectFolder();
    ProjectModel::AudioFileInfo info;
    info.name = f.file.getFileName();
    info.absPath = f.file.getFullPathName();
    info.path = folder != juce::File() && f.file.isAChildOf (folder) ? f.file.getRelativePathFrom (folder).replaceCharacter ('\\', '/') : juce::String();
    info.hash = audioimport::hashOf (f.file);
    info.sampleRate = f.sampleRate;
    info.channels = f.channels;
    info.lengthSamples = f.frames;
    info.tempo = 0.0;
    return model.addAudioFile (info).id();
}

bool AudioRecorder::getLivePeaks (int64_t trackId, std::vector<float>& minMax, int64_t& frames, double& rate) const
{
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> sl (jobsLock);
        const auto it = jobs.find (trackId);
        if (it == jobs.end())
            return false;
        job = it->second;
    }
    std::lock_guard<std::mutex> pl (job->peakLock);
    minMax = job->peaks;
    frames = job->framesOnDisk;
    rate = job->info.sampleRate;
    return true;
}

//==============================================================================
void AudioRecorder::run()
{
    while (! threadShouldExit())
    {
        process();
        wait (3);
    }
}

void AudioRecorder::process()
{
    if (writerPaused.load())
        return;
    std::lock_guard<std::mutex> wl (writeLock);
    std::vector<std::shared_ptr<Job>> active;
    {
        std::lock_guard<std::mutex> sl (jobsLock);
        for (auto& [id, job] : jobs)
            if (! job->finished.load())
                active.push_back (job);
    }
    for (auto& job : active)
        writeJob (*job);
}

void AudioRecorder::writeJob (Job& job)
{
    auto& stream = *job.stream;
    const int channels = stream.getChannels();
    for (;;)
    {
        // The state is read before the data: once it says done, every frame is in the ring already.
        const int state = stream.state.load (std::memory_order_acquire);
        if (state == RecordStream::idle || state == RecordStream::requested)
            return; // not begun (or cancelled)
        float* planes[2] { scratch.getWritePointer (0), scratch.getWritePointer (1) };
        int n = stream.read (planes, scratch.getNumSamples());
        if (n == 0 && state == RecordStream::done)
        {
            // A gap that never reached the queue can only be at the end: the file is padded to its length.
            const int64_t missing = stream.totalFrames.load() - job.framesOnDisk;
            if (missing > 0)
            {
                n = (int) juce::jmin ((int64_t) scratch.getNumSamples(), missing);
                scratch.clear (0, n);
            }
        }
        if (n > 0)
        {
            job.writer->writeFromFloatArrays (planes, channels, n);
            std::lock_guard<std::mutex> pl (job.peakLock);
            for (int i = 0; i < n; ++i)
            {
                float lo = planes[0][i], hi = planes[0][i];
                if (channels > 1)
                {
                    lo = juce::jmin (lo, planes[1][i]);
                    hi = juce::jmax (hi, planes[1][i]);
                }
                if (job.peakFill == 0)
                {
                    job.peakMin = lo;
                    job.peakMax = hi;
                }
                else
                {
                    job.peakMin = juce::jmin (job.peakMin, lo);
                    job.peakMax = juce::jmax (job.peakMax, hi);
                }
                if (++job.peakFill == kPeakFrames)
                {
                    job.peaks.push_back (job.peakMin);
                    job.peaks.push_back (job.peakMax);
                    job.peakFill = 0;
                }
            }
            job.framesOnDisk += n;
            continue;
        }
        if (state != RecordStream::done)
            return; // everything there is was written; more comes later
        break;
    }

    job.writer.reset(); // flushes and closes the file
    job.info.ok = true;
    job.info.frames = job.framesOnDisk;
    job.info.startSample = stream.startSample.load();
    job.info.latency = stream.latency.load();
    job.info.droppedFrames = stream.droppedFrames.load();
    job.info.firstDropFrame = stream.firstDropFrame.load();
    job.finished.store (true);
}
