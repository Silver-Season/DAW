#include "AudioImport.h"

#include "../engine/AudioEngine.h"

#include "daw/engine/AudioClip.h"

namespace audioimport
{
bool isSoundFile (const juce::File& f)
{
    static const juce::StringArray extensions { ".wav", ".wave", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".aac", ".caf", ".ogg" };
    return extensions.contains (f.getFileExtension().toLowerCase());
}

juce::String hashOf (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return {};
    uint64_t h = 1469598103934665603ULL;
    juce::HeapBlock<char> buffer (65536);
    int64_t left = 1 << 20;
    while (left > 0 && ! in.isExhausted())
    {
        const int n = in.read (buffer.get(), (int) juce::jmin ((int64_t) 65536, left));
        if (n <= 0)
            break;
        for (int i = 0; i < n; ++i)
        {
            h ^= (unsigned char) buffer[i];
            h *= 1099511628211ULL;
        }
        left -= n;
    }
    return juce::String::toHexString ((juce::int64) h) + "-" + juce::String (file.getSize());
}

double detectTempo (const juce::File& file, const juce::StringPairArray& metadata)
{
    for (const auto* key : { "acid tempo", "apple tempo", "tempo" })
        if (const double bpm = metadata.getValue (key, {}).getDoubleValue(); bpm >= 40.0 && bpm <= 300.0)
            return bpm;
    return daw::engine::tempoFromFileName (file.getFileNameWithoutExtension().toStdString());
}

namespace
{
juce::File uniqueTarget (const juce::File& folder, const juce::File& source)
{
    auto target = folder.getChildFile (source.getFileName());
    for (int i = 2; target.exists(); ++i)
        target = folder.getChildFile (source.getFileNameWithoutExtension() + " " + juce::String (i) + source.getFileExtension());
    return target;
}
} // namespace

Imported importFile (ProjectModel& model, AudioEngine& engine, const juce::File& file, bool copyIntoProject)
{
    Imported result;
    result.name = file.getFileNameWithoutExtension();
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioPool().formats().createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        result.error = "unreadable";
        return result;
    }
    result.fileTempo = detectTempo (file, reader->metadataValues);
    const double bpm = result.fileTempo > 0.0 ? result.fileTempo : model.tempo();
    result.lengthBeats = juce::jmax (1.0 / 16.0, daw::engine::beatsForFrames ((double) reader->lengthInSamples, reader->sampleRate, bpm));
    result.rateDiffers = std::abs (reader->sampleRate - engine.getSampleRate()) > 0.5;

    const auto hash = hashOf (file);
    if (const auto existing = model.findAudioFileByHash (hash); existing.isValid() && engine.resolveAudioFile (existing).existsAsFile())
    {
        result.fileId = existing.id();
        result.ok = true;
        return result;
    }

    const auto folder = engine.getProjectFolder();
    auto stored = file;
    if (copyIntoProject && folder != juce::File() && ! file.isAChildOf (folder))
    {
        const auto imported = folder.getChildFile ("Samples").getChildFile ("Imported");
        imported.createDirectory();
        const auto target = uniqueTarget (imported, file);
        if (file.copyFileTo (target))
            stored = target;
    }

    ProjectModel::AudioFileInfo info;
    info.name = file.getFileName();
    info.absPath = stored.getFullPathName();
    info.path = folder != juce::File() && stored.isAChildOf (folder) ? stored.getRelativePathFrom (folder).replaceCharacter ('\\', '/') : juce::String();
    info.hash = hash;
    info.sampleRate = reader->sampleRate;
    info.channels = (int) reader->numChannels;
    info.lengthSamples = reader->lengthInSamples;
    info.tempo = result.fileTempo;
    result.fileId = model.addAudioFile (info).id();
    result.ok = true;
    return result;
}

int relinkMissing (ProjectModel& model, AudioEngine& engine, const juce::File& folder)
{
    int found = 0;
    juce::Array<juce::File> candidates;
    for (const auto& f : model.audioFilesNode())
    {
        AudioFileRef ref (f);
        if (engine.resolveAudioFile (ref).existsAsFile())
            continue;
        if (candidates.isEmpty())
            candidates = folder.findChildFiles (juce::File::findFiles, true);
        const auto wantedName = juce::File (ref.absPath().isNotEmpty() ? ref.absPath() : ref.path()).getFileName();
        juce::File match;
        for (const auto& c : candidates) // by name and hash first, then by hash alone (a renamed file)
            if (c.getFileName() == wantedName && hashOf (c) == ref.hash())
            {
                match = c;
                break;
            }
        if (match == juce::File())
            for (const auto& c : candidates)
                if (isSoundFile (c) && hashOf (c) == ref.hash())
                {
                    match = c;
                    break;
                }
        if (match == juce::File())
            continue;
        const auto projectFolder = engine.getProjectFolder();
        ref.setAbsPath (match.getFullPathName(), &model.undo);
        ref.setPath (projectFolder != juce::File() && match.isAChildOf (projectFolder) ? match.getRelativePathFrom (projectFolder).replaceCharacter ('\\', '/')
                                                                                       : juce::String(),
                     &model.undo);
        ++found;
    }
    return found;
}

juce::Array<int64_t> unusedFiles (const ProjectModel& model)
{
    std::set<int64_t> used;
    const std::function<void (const juce::ValueTree&)> visit = [&] (const juce::ValueTree& v) {
        if (v.hasType (ids::CLIP) && v.hasProperty (ids::file))
            used.insert ((int64_t) (juce::int64) v.getProperty (ids::file));
        for (const auto& c : v)
            visit (c);
    };
    visit (model.tracksNode());
    juce::Array<int64_t> out;
    for (const auto& f : model.audioFilesNode())
        if (const auto id = AudioFileRef (f).id(); used.count (id) == 0)
            out.add (id);
    return out;
}
} // namespace audioimport
