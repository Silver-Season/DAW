#include "WarpService.h"

#include "daw/dsp/Onsets.h"
#include "daw/dsp/TimeStretch.h"

namespace
{
daw::dsp::StretchMode modeOf (const juce::String& m)
{
    if (m == "beats")
        return daw::dsp::StretchMode::beats;
    if (m == "tones")
        return daw::dsp::StretchMode::tones;
    if (m == "texture")
        return daw::dsp::StretchMode::texture;
    if (m == "complexpro")
        return daw::dsp::StretchMode::complexPro;
    return daw::dsp::StretchMode::complex;
}
} // namespace

WarpService::WarpService (juce::AudioFormatManager& f) : formats (f) {}

WarpService::~WarpService()
{
    *alive = false;
    pool.removeAllJobs (true, 30000);
}

void WarpService::waitForJobs()
{
    while (pool.getNumJobs() > 0)
        juce::Thread::sleep (5);
}

WarpService::Analysis WarpService::compute (const juce::File& file) const
{
    Analysis result;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return result;
    // Mono, at most 20 minutes: plenty for a tempo, and the memory stays bounded.
    const auto n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 1200.0));
    juce::AudioBuffer<float> buffer ((int) juce::jlimit (1u, 2u, reader->numChannels), n);
    reader->read (&buffer, 0, n, 0, true, buffer.getNumChannels() > 1);
    if (buffer.getNumChannels() > 1)
    {
        buffer.addFrom (0, 0, buffer, 1, 0, n);
        buffer.applyGain (0, 0, n, 0.5f);
    }
    const auto estimate = daw::dsp::estimateTempo (buffer.getReadPointer (0), n, reader->sampleRate,
                                                   file.getFileNameWithoutExtension().toStdString());
    if (estimate.bpm > 0.0 && estimate.confidence > 0.05)
    {
        result.bpm = estimate.bpm;
        result.firstBeatSeconds = estimate.firstBeatSeconds;
        result.confidence = estimate.confidence;
        result.beats = estimate.beats;
    }
    result.onsets = daw::dsp::detectOnsets (buffer.getReadPointer (0), n, reader->sampleRate);
    return result;
}

namespace
{
constexpr int kCacheVersion = 1; // bump when the analysis changes, so old results are computed again

juce::String joinTimes (const std::vector<double>& times)
{
    juce::StringArray parts;
    for (const double t : times)
        parts.add (juce::String (t, 6));
    return parts.joinIntoString (" ");
}

std::vector<double> splitTimes (const juce::String& text)
{
    std::vector<double> out;
    for (const auto& token : juce::StringArray::fromTokens (text, " ", {}))
        if (token.isNotEmpty())
            out.push_back (token.getDoubleValue());
    return out;
}
} // namespace

bool WarpService::readCache (const juce::String& hash, Analysis& a) const
{
    if (hash.isEmpty() || cacheFolder == juce::File())
        return false;
    const auto xml = juce::parseXML (cacheFolder.getChildFile (hash + ".xml"));
    if (xml == nullptr || ! xml->hasTagName ("ANALYSIS") || xml->getIntAttribute ("version") != kCacheVersion)
        return false;
    a.bpm = xml->getDoubleAttribute ("bpm");
    a.firstBeatSeconds = xml->getDoubleAttribute ("firstBeat");
    a.confidence = xml->getDoubleAttribute ("confidence");
    a.onsets = splitTimes (xml->getStringAttribute ("onsets"));
    a.beats = splitTimes (xml->getStringAttribute ("beats"));
    return true;
}

void WarpService::writeCache (const juce::String& hash, const Analysis& a) const
{
    if (hash.isEmpty() || cacheFolder == juce::File())
        return;
    juce::XmlElement xml ("ANALYSIS");
    xml.setAttribute ("version", kCacheVersion);
    xml.setAttribute ("bpm", a.bpm);
    xml.setAttribute ("firstBeat", a.firstBeatSeconds);
    xml.setAttribute ("confidence", a.confidence);
    xml.setAttribute ("onsets", joinTimes (a.onsets));
    xml.setAttribute ("beats", joinTimes (a.beats));
    cacheFolder.createDirectory();
    xml.writeTo (cacheFolder.getChildFile (hash + ".xml"));
}

void WarpService::analyse (const juce::File& file, const juce::String& hash, std::function<void (const Analysis&)> done)
{
    const auto key = hash.isNotEmpty() ? hash : file.getFullPathName();
    if (const auto it = analyses.find (key); it != analyses.end() && it->second != nullptr)
    {
        auto result = it->second;
        juce::MessageManager::callAsync ([done, result, flag = alive] {
            if (*flag && done)
                done (*result);
        });
        return;
    }
    analyses[key] = nullptr;
    auto flag = alive;
    pool.addJob ([this, file, hash, key, done, flag] {
        auto result = std::make_shared<Analysis>();
        if (! readCache (hash, *result))
        {
            *result = compute (file);
            writeCache (hash, *result);
        }
        juce::MessageManager::callAsync ([this, key, done, result, flag] {
            if (! *flag)
                return;
            analyses[key] = result;
            if (done)
                done (*result);
            if (onAnalysisReady)
                onAnalysisReady();
        });
    });
}

std::shared_ptr<const WarpService::Analysis> WarpService::analysisFor (const juce::File& file, const juce::String& hash)
{
    const auto key = hash.isNotEmpty() ? hash : file.getFullPathName();
    if (const auto it = analyses.find (key); it != analyses.end())
        return it->second;
    if (file.existsAsFile())
        analyse (file, hash, {});
    return nullptr;
}

std::shared_ptr<daw::engine::AudioSource> WarpService::stretched (const std::shared_ptr<daw::engine::AudioSource>& original, double timeRatio,
                                                                  const juce::String& mode)
{
    if (original == nullptr)
        return nullptr;
    const auto out = (int64_t) std::llround ((double) original->length * timeRatio);
    return stretched (original, { { 0, 0 }, { original->length, out } }, out, mode, 1.0);
}

std::shared_ptr<daw::engine::AudioSource> WarpService::stretched (const std::shared_ptr<daw::engine::AudioSource>& original,
                                                                  std::vector<std::pair<int64_t, int64_t>> keyFrames, int64_t outFrames,
                                                                  const juce::String& mode, double beatsDecay)
{
    if (original == nullptr || ! original->isComplete() || original->length <= 0 || outFrames <= 0 || keyFrames.size() < 2)
        return nullptr;
    uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h] (uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    for (const auto& [from, to] : keyFrames)
    {
        mix ((uint64_t) from);
        mix ((uint64_t) to);
    }
    mix ((uint64_t) outFrames);
    mix ((uint64_t) std::llround (beatsDecay * 1000.0));
    const Key key { original.get(), h, mode };
    if (const auto it = cache.find (key); it != cache.end())
    {
        it->second.used = true;
        return it->second.result;
    }
    Job job { std::move (keyFrames), outFrames, mode, beatsDecay };
    if (renderInline)
    {
        auto& entry = cache[key];
        entry.original = original;
        entry.result = render (*original, job);
        return entry.result;
    }
    // One render per file at a time: while the tempo or a marker is being dragged, only the last value gets rendered
    // (the finished render triggers a rebuild, which asks again for the then current map).
    for (const auto& [k, e] : cache)
        if (k.source == original.get() && e.result == nullptr)
            return nullptr;
    auto& entry = cache[key];
    entry.original = original;
    entry.used = true;

    auto flag = alive;
    pool.addJob ([this, key, original, job, flag] {
        auto copy = render (*original, job);
        juce::MessageManager::callAsync ([this, key, copy, flag] {
            if (! *flag)
                return;
            if (const auto it = cache.find (key); it != cache.end())
            {
                it->second.result = copy;
                if (onStretchReady)
                    onStretchReady();
            }
        });
    });
    return nullptr;
}

std::shared_ptr<daw::engine::AudioSource> WarpService::render (const daw::engine::AudioSource& original, const Job& job)
{
    std::vector<std::vector<float>> in ((size_t) original.numChannels);
    for (int c = 0; c < original.numChannels; ++c)
        in[(size_t) c].assign (original.channel (c), original.channel (c) + original.length);
    const auto mode = modeOf (job.mode);
    std::vector<int64_t> onsets;
    if (mode == daw::dsp::StretchMode::beats)
    {
        std::vector<float> mono (in[0]);
        if (in.size() > 1)
            for (size_t i = 0; i < mono.size(); ++i)
                mono[i] = 0.5f * (mono[i] + in[1][i]);
        for (const double t : daw::dsp::detectOnsets (mono.data(), (int64_t) mono.size(), original.sampleRate))
            onsets.push_back ((int64_t) std::llround (t * original.sampleRate));
    }
    const auto out = daw::dsp::stretchOfflineMapped (in, original.sampleRate, job.keyFrames, job.outFrames, mode, onsets, job.decay);
    const auto frames = out.empty() ? (int64_t) 0 : (int64_t) out[0].size();
    auto copy = std::make_shared<daw::engine::AudioSource> (original.numChannels, frames, original.sampleRate);
    for (int c = 0; c < original.numChannels && frames > 0; ++c)
    {
        float* dest = c == 0 ? copy->left.get() : copy->right.get();
        std::copy (out[(size_t) c].begin(), out[(size_t) c].end(), dest);
    }
    copy->publish (frames);
    return copy;
}

void WarpService::beginUse()
{
    for (auto& [key, entry] : cache)
        entry.used = false;
}

void WarpService::endUse()
{
    // A render still running stays: it will be wanted again in a moment (a tempo being typed in, say).
    for (auto it = cache.begin(); it != cache.end();)
        it = ! it->second.used && it->second.result != nullptr ? cache.erase (it) : std::next (it);
}
