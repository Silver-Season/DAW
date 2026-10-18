#include "ProjectFile.h"

#include "../Strings.h"

namespace project
{
using daw::project::VersionKind;

const std::vector<Migration>& migrations()
{
    // Step N -> N+1 is appended here when the schema changes. Existing files keep opening because the loader walks this
    // list from the version stored in the file up to the current one.
    static const std::vector<Migration> steps {
        // 1 -> 2 (stage 3): every track has an ARRANGEMENT and the project has MARKERS. The arrangement properties of the
        // project have defaults, so a version 1 file needs nothing else.
        [] (juce::ValueTree& tree) {
            if (! tree.getChildWithName (ids::MARKERS).isValid())
                tree.appendChild (juce::ValueTree (ids::MARKERS), nullptr);
            for (auto track : tree.getChildWithName (ids::TRACKS))
                if (! track.getChildWithName (ids::ARRANGEMENT).isValid())
                    track.appendChild (juce::ValueTree (ids::ARRANGEMENT), nullptr);
        },
        // 2 -> 3 (stage 5): the pool of sound files. MIDI projects simply get an empty one.
        [] (juce::ValueTree& tree) {
            if (! tree.getChildWithName (ids::AUDIOFILES).isValid())
                tree.appendChild (juce::ValueTree (ids::AUDIOFILES), nullptr);
        },
        // 3 -> 4 (stage 7): a clip that follows the project tempo keeps the tempo of its audio itself (`originalTempo`),
        // and says how it is stretched (`warpMode`). Older clips took the tempo from their FILE.
        [] (juce::ValueTree& tree) {
            const auto pool = tree.getChildWithName (ids::AUDIOFILES);
            std::function<void (juce::ValueTree)> walk = [&] (juce::ValueTree node) {
                if (node.hasType (ids::CLIP) && (bool) node.getProperty (ids::warp, false) && ! node.hasProperty (ids::originalTempo))
                {
                    const auto file = AudioFileRef (pool.getChildWithProperty (ids::id, node.getProperty (ids::file, -1)));
                    if (file.isValid() && file.tempo() > 0.0)
                    {
                        node.setProperty (ids::originalTempo, file.tempo(), nullptr);
                        node.setProperty (ids::warpMode, ProjectModel::warpModeFor (file), nullptr);
                    }
                }
                for (auto child : node)
                    walk (child);
            };
            walk (tree.getChildWithName (ids::TRACKS));
        }
    };
    return steps;
}

bool migrate (juce::ValueTree& tree, int target, const std::vector<Migration>& steps, juce::String& error)
{
    int version = (int) tree.getProperty (ids::schemaVersion, 0);
    if (version < 1)
    {
        error = Strings::errorBrokenProject;
        return false;
    }
    if (version > target)
    {
        error = Strings::errorNewerProject;
        return false;
    }
    while (version < target)
    {
        const size_t index = (size_t) (version - 1);
        if (index >= steps.size() || ! steps[index])
        {
            error = Strings::errorBrokenProject;
            return false;
        }
        steps[index] (tree);
        ++version;
        tree.setProperty (ids::schemaVersion, version, nullptr);
    }
    return true;
}

LoadResult parseProject (const juce::String& xmlText)
{
    return parseProject (xmlText, ProjectModel::kSchemaVersion, migrations());
}

LoadResult parseProject (const juce::String& xmlText, int target, const std::vector<Migration>& steps)
{
    LoadResult result;
    const auto xml = juce::XmlDocument::parse (xmlText);
    if (xml == nullptr || xml->getTagName() != ids::PROJECT.toString())
    {
        result.error = Strings::errorBrokenProject;
        return result;
    }
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
    {
        result.error = Strings::errorBrokenProject;
        return result;
    }
    if (! migrate (tree, target, steps, result.error))
        return result;
    result.tree = tree;
    return result;
}

LoadResult loadProjectFile (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        LoadResult r;
        r.error = Strings::errorFileMissing (file.getFullPathName());
        return r;
    }
    return parseProject (file.loadFileAsString());
}

juce::String serialise (const juce::ValueTree& tree)
{
    return tree.toXmlString();
}

bool writeAtomically (const juce::File& target, const juce::String& text, juce::String& error)
{
    if (! target.getParentDirectory().createDirectory())
    {
        error = Strings::errorWriteFile (target.getFullPathName());
        return false;
    }
    juce::TemporaryFile temp (target);
    if (! temp.getFile().replaceWithText (text, false, false, "\n") || ! temp.overwriteTargetFileWithTemporary())
    {
        error = Strings::errorWriteFile (target.getFullPathName());
        return false;
    }
    return true;
}

//==============================================================================
juce::File mainFile (const juce::File& folder, const juce::String& name)
{
    return folder.getChildFile (juce::File::createLegalFileName (name) + kExtension);
}

juce::File findMainFile (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension);
    files.sort();
    return files.isEmpty() ? juce::File() : files.getFirst();
}

juce::File autosaveFile (const juce::File& folder) { return folder.getChildFile ("Backup").getChildFile (juce::String ("autosave") + kExtension); }
juce::File historyFolder (const juce::File& folder) { return folder.getChildFile ("History"); }

juce::File versionFile (const juce::File& folder, int number)
{
    return historyFolder (folder).getChildFile ("v" + juce::String (number).paddedLeft ('0', 4) + kExtension);
}

juce::String projectNameOfFolder (const juce::File& folder)
{
    const auto name = folder.getFileName();
    return name.endsWith (" Project") ? name.dropLastCharacters (8) : name;
}

juce::File newUnsavedFolder (const juce::File& musicFolder, juce::Time now)
{
    return musicFolder.getChildFile ("Prisma DAW").getChildFile ("Unsaved").getChildFile (now.formatted ("%Y-%m-%d %H.%M.%S") + " Project");
}

//==============================================================================
namespace
{
juce::File indexFile (const juce::File& folder) { return historyFolder (folder).getChildFile ("index.xml"); }

struct Index
{
    int next = 1;
    std::vector<VersionRecord> versions;
};

Index readIndex (const juce::File& folder)
{
    Index index;
    if (const auto xml = juce::XmlDocument::parse (indexFile (folder)))
    {
        index.next = juce::jmax (1, xml->getIntAttribute ("next", 1));
        for (const auto* e : xml->getChildWithTagNameIterator ("VERSION"))
        {
            VersionRecord r;
            r.number = e->getIntAttribute ("n");
            r.timeMs = e->getStringAttribute ("time").getLargeIntValue();
            r.kind = kindFromString (e->getStringAttribute ("kind"));
            r.label = e->getStringAttribute ("label");
            index.versions.push_back (r);
            index.next = juce::jmax (index.next, r.number + 1);
        }
    }
    std::sort (index.versions.begin(), index.versions.end(), [] (auto& a, auto& b) { return a.number < b.number; });
    return index;
}

bool writeIndex (const juce::File& folder, const Index& index, juce::String& error)
{
    juce::XmlElement root ("HISTORY");
    root.setAttribute ("next", index.next);
    for (const auto& v : index.versions)
    {
        auto* e = root.createNewChildElement ("VERSION");
        e->setAttribute ("n", v.number);
        e->setAttribute ("time", juce::String (v.timeMs));
        e->setAttribute ("kind", kindToString (v.kind));
        e->setAttribute ("label", v.label);
    }
    return writeAtomically (indexFile (folder), root.toString(), error);
}
} // namespace

juce::String kindToString (VersionKind k)
{
    switch (k)
    {
        case VersionKind::save: return "save";
        case VersionKind::restore: return "restore";
        case VersionKind::autosave: break;
    }
    return "autosave";
}

VersionKind kindFromString (const juce::String& s)
{
    return s == "save" ? VersionKind::save : (s == "restore" ? VersionKind::restore : VersionKind::autosave);
}

std::vector<VersionRecord> readVersions (const juce::File& folder)
{
    return readIndex (folder).versions;
}

int addVersion (const juce::File& folder, const juce::String& xmlText, VersionKind kind, juce::int64 nowMs, juce::String& error)
{
    auto index = readIndex (folder);
    const int number = index.next;
    if (! writeAtomically (versionFile (folder, number), xmlText, error))
        return -1;

    index.next = number + 1;
    index.versions.push_back ({ number, nowMs, kind, {} });

    std::vector<daw::project::VersionInfo> infos;
    for (const auto& v : index.versions)
        infos.push_back ({ v.number, v.timeMs, v.kind, v.label.isNotEmpty() });
    const auto doomed = daw::project::versionsToPrune (infos);
    for (int n : doomed)
    {
        versionFile (folder, n).deleteFile();
        index.versions.erase (std::remove_if (index.versions.begin(), index.versions.end(), [n] (auto& v) { return v.number == n; }),
                              index.versions.end());
    }
    return writeIndex (folder, index, error) ? number : -1;
}

bool setVersionLabel (const juce::File& folder, int number, const juce::String& label)
{
    auto index = readIndex (folder);
    for (auto& v : index.versions)
        if (v.number == number)
        {
            v.label = label.trim();
            juce::String error;
            return writeIndex (folder, index, error);
        }
    return false;
}

LoadResult loadVersion (const juce::File& folder, int number)
{
    return loadProjectFile (versionFile (folder, number));
}

bool writeSave (const juce::File& folder, const juce::String& name, const juce::String& xmlText, juce::int64 nowMs, juce::String& error)
{
    if (! writeAtomically (mainFile (folder, name), xmlText, error))
        return false;
    return addVersion (folder, xmlText, VersionKind::save, nowMs, error) >= 0;
}

bool writeAutosave (const juce::File& folder, const juce::String& xmlText, juce::int64 nowMs, juce::String& error)
{
    if (! writeAtomically (autosaveFile (folder), xmlText, error))
        return false;
    return addVersion (folder, xmlText, VersionKind::autosave, nowMs, error) >= 0;
}

bool writeRestorePoint (const juce::File& folder, const juce::String& xmlText, juce::int64 nowMs, juce::String& error)
{
    return addVersion (folder, xmlText, VersionKind::restore, nowMs, error) >= 0;
}

bool copyProjectFolder (const juce::File& from, const juce::File& to, juce::String& error)
{
    if (! to.createDirectory())
    {
        error = Strings::errorWriteFile (to.getFullPathName());
        return false;
    }
    const auto oldMain = findMainFile (from);
    for (const auto& child : from.findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        if (child.getFileName() == "Backup")
            continue; // the autosave belongs to the old project
        if (child == oldMain)
            continue; // the new file is written by the save that follows
        const bool ok = child.isDirectory() ? child.copyDirectoryTo (to.getChildFile (child.getFileName()))
                                            : child.copyFileTo (to.getChildFile (child.getFileName()));
        if (! ok)
        {
            error = Strings::errorWriteFile (to.getChildFile (child.getFileName()).getFullPathName());
            return false;
        }
    }
    return true;
}
} // namespace project
