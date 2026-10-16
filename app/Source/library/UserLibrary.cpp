#include "UserLibrary.h"

#include "../Strings.h"

namespace library
{
namespace
{
juce::File uniqueFile (const juce::File& folder, const juce::String& baseName, const juce::String& extension)
{
    const auto legal = juce::File::createLegalFileName (baseName.trim());
    auto file = folder.getChildFile (legal + extension);
    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile (legal + " " + juce::String (n) + extension);
    return file;
}

bool writeXml (const juce::File& file, const juce::XmlElement& xml, juce::String& error)
{
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (xml.toString(), false, false, "\n") || ! temp.overwriteTargetFileWithTemporary())
    {
        error = Strings::errorWriteFile (file.getFullPathName());
        return false;
    }
    return true;
}
} // namespace

juce::File UserLibrary::defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Prisma DAW").getChildFile ("User Library");
}

juce::File UserLibrary::presetsFolderFor (const juce::String& deviceName) const
{
    return presetsFolder().getChildFile (juce::File::createLegalFileName (deviceName));
}

juce::File UserLibrary::savePreset (const juce::String& deviceName, const juce::String& name, const juce::String& deviceKind,
                                    const juce::String& pluginDesc, const juce::String& stateBase64, juce::String& error) const
{
    const auto folder = presetsFolderFor (deviceName);
    const auto file = uniqueFile (folder, name, ".dawpreset");
    juce::XmlElement xml ("DAWPRESET");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("deviceKind", deviceKind);
    xml.setAttribute ("name", file.getFileNameWithoutExtension()); // the name after "name 2" was added
    xml.setAttribute ("pluginDesc", pluginDesc);
    xml.setAttribute ("state", stateBase64);
    xml.setAttribute ("created", juce::Time::getCurrentTime().toISO8601 (true));
    return writeXml (file, xml, error) ? file : juce::File();
}

PresetData UserLibrary::readPreset (const juce::File& file)
{
    PresetData d;
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("DAWPRESET"))
        return d;
    d.deviceKind = xml->getStringAttribute ("deviceKind");
    d.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    d.pluginDesc = xml->getStringAttribute ("pluginDesc");
    d.state = xml->getStringAttribute ("state");
    d.valid = d.deviceKind.isNotEmpty();
    return d;
}

std::vector<PresetInfo> UserLibrary::listPresets() const
{
    std::vector<PresetInfo> out;
    auto folders = presetsFolder().findChildFiles (juce::File::findDirectories, false);
    folders.sort();
    for (const auto& folder : folders)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, "*.dawpreset");
        files.sort();
        for (const auto& f : files)
        {
            PresetInfo info;
            info.file = f;
            info.deviceName = folder.getFileName();
            info.data = readPreset (f);
            if (info.data.valid)
                out.push_back (std::move (info));
        }
    }
    return out;
}

juce::File UserLibrary::renamePreset (const juce::File& file, const juce::String& newName)
{
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return {};
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return {};
    const auto target = uniqueFile (file.getParentDirectory(), trimmed, ".dawpreset");
    if (target == file)
        return file;
    xml->setAttribute ("name", target.getFileNameWithoutExtension());
    juce::String error;
    if (! writeXml (target, *xml, error))
        return {};
    file.deleteFile();
    return target;
}

//==============================================================================
juce::File UserLibrary::saveChain (const juce::String& name, int colour, const juce::Array<juce::ValueTree>& deviceNodes, juce::String& error) const
{
    const auto file = uniqueFile (chainsFolder(), name, ".dawchain");
    juce::XmlElement xml ("DAWCHAIN");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("name", file.getFileNameWithoutExtension());
    xml.setAttribute ("colour", colour);
    for (const auto& node : deviceNodes)
    {
        auto copy = node.createCopy();
        copy.removeProperty ("id", nullptr);
        if (auto child = copy.createXml())
            xml.addChildElement (child.release());
    }
    return writeXml (file, xml, error) ? file : juce::File();
}

juce::Array<juce::File> UserLibrary::listChains() const
{
    auto files = chainsFolder().findChildFiles (juce::File::findFiles, false, "*.dawchain");
    files.sort();
    return files;
}

ChainData UserLibrary::readChain (const juce::File& file)
{
    ChainData d;
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("DAWCHAIN"))
        return d;
    d.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    d.colour = xml->getIntAttribute ("colour", 0);
    for (const auto* e : xml->getChildWithTagNameIterator ("DEVICE"))
    {
        auto v = juce::ValueTree::fromXml (*e);
        if (v.isValid())
            d.devices.add (v);
    }
    d.valid = true;
    return d;
}

juce::File UserLibrary::renameChain (const juce::File& file, const juce::String& newName)
{
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return {};
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return {};
    const auto target = uniqueFile (file.getParentDirectory(), trimmed, ".dawchain");
    if (target == file)
        return file;
    xml->setAttribute ("name", target.getFileNameWithoutExtension());
    juce::String error;
    if (! writeXml (target, *xml, error))
        return {};
    file.deleteFile();
    return target;
}
} // namespace library
