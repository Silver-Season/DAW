// The message-thread half of AudioEngine: listening to the model, building PlaybackGraphs, publishing
// them, and deleting what the audio thread can no longer see.
#include "AudioEngine.h"

#include "../../../plugins/prisma/Source/PrismaProcessor.h"
#include "../Strings.h"

namespace
{
struct Hasher
{
    uint64_t h = 1469598103934665603ULL; // FNV-1a

    void add (const void* data, size_t n)
    {
        const auto* p = static_cast<const unsigned char*> (data);
        for (size_t i = 0; i < n; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    }
    template <typename T>
    void add (const T& v)
    {
        add (&v, sizeof (T));
    }
};

uint64_t hashClip (const ClipRef& clip)
{
    Hasher h;
    h.add (clip.lengthBeats());
    h.add (clip.loopStart());
    h.add (clip.startOffset());
    h.add (clip.loopEnabled());
    h.add (clip.colour());
    const auto name = clip.name();
    h.add (name.toRawUTF8(), name.getNumBytesAsUTF8());
    for (int i = 0; i < clip.numNotes(); ++i)
    {
        const auto n = clip.note (i);
        h.add (n.start);
        h.add (n.length);
        h.add (n.pitch);
        h.add (n.velocity);
    }
    return h.h;
}

TrackInputMode inputModeFor (const juce::String& input)
{
    if (input == "keyboard")
        return TrackInputMode::keyboard;
    if (input == "none")
        return TrackInputMode::none;
    if (input.startsWith ("midi:"))
        return TrackInputMode::midiDevice;
    return TrackInputMode::allMidi;
}
} // namespace

//==============================================================================
void AudioEngine::setModel (ProjectModel* newModel)
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    if (model == newModel)
        return;

    if (model != nullptr)
        model->root.removeListener (this);
    retireEverything();
    model = newModel;

    if (model != nullptr)
    {
        model->root.addListener (this);
        applyProjectProperties();
        rebuildGraph();
    }
    else
    {
        // Publish "nothing": an empty graph keeps the audio thread's pointer valid.
        graphDirty = false;
        auto empty = std::make_unique<PlaybackGraph>();
        empty->generation = generationCounter.fetch_add (1) + 1;
        const auto gen = empty->generation;
        if (currentOwner != nullptr)
            retiredGraphs.push_back ({ std::move (currentOwner), gen });
        currentOwner = std::move (empty);
        current.store (currentOwner.get(), std::memory_order_release);
        collectGarbage();
    }
}

void AudioEngine::retireEverything()
{
    // Called with the lock held, before the graph is replaced; everything goes through the deferred path.
    const auto gen = generationCounter.load() + 1;
    for (auto& [id, rt] : runtimes)
        retiredTracks.push_back ({ std::move (rt), gen });
    runtimes.clear();
    for (auto& [id, d] : devices)
        retiredDevices.push_back ({ std::move (d), gen });
    devices.clear();
    pendingProcessors.clear();
    loadingDevices.clear();
    ++modelEpoch;
    clipCache.clear();
}

void AudioEngine::valueTreeRedirected (juce::ValueTree&)
{
    // The whole project was replaced. Device ids of the new project mean other devices than the same ids
    // of the old one, so nothing may be reused.
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    retireEverything();
    if (model != nullptr)
        applyProjectProperties();
    graphDirty = true;
}

void AudioEngine::setDeviceFactory (DeviceFactory f)
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    deviceFactory = std::move (f);
}

void AudioEngine::registerProcessor (int64_t deviceId, std::unique_ptr<juce::AudioProcessor> p)
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    pendingProcessors[deviceId] = std::move (p);
    graphDirty = true;
}

juce::AudioProcessor* AudioEngine::findProcessor (int64_t deviceId) const
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    const auto it = devices.find (deviceId);
    return it != devices.end() ? it->second->processor.get() : nullptr;
}

TrackRuntime* AudioEngine::findRuntime (int64_t trackId) const
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    const auto it = runtimes.find (trackId);
    return it != runtimes.end() ? it->second.get() : nullptr;
}

size_t AudioEngine::getNumRetiredObjects() const
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    return retiredGraphs.size() + retiredTracks.size() + retiredDevices.size();
}

//==============================================================================
// Live parameters never rebuild the graph: they go straight into atomics.
void AudioEngine::applyTrackProperties (TrackRuntime& rt, const TrackRef& t)
{
    const float db = t.volumeDb();
    rt.volumeGain = db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
    rt.pan = t.pan();
    rt.mute = t.mute();
    rt.solo = t.solo();
    rt.arm = t.arm();
    rt.inputMode = (int) inputModeFor (t.input());
    rt.transpose = juce::jlimit (-48, 48, t.transpose());
}

void AudioEngine::applyProjectProperties()
{
    tempo = model->tempo();
    quantise = model->launchQuantise();
    selectedTrackId = model->selectedTrack();
    tuneA4 = (float) model->tuningA4();
    preview.setTuneA4 (tuneA4.load());
    for (auto& [id, d] : devices)
        if (auto* prisma = dynamic_cast<PrismaProcessor*> (d->processor.get()))
            prisma->setTuneA4 (tuneA4.load());
    const float db = model->masterVolumeDb();
    masterGain = db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

void AudioEngine::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    const auto type = tree.getType();

    if (type == ids::TRACK)
    {
        if (property == ids::volumeDb || property == ids::pan || property == ids::mute || property == ids::solo
            || property == ids::arm || property == ids::input || property == ids::transpose)
        {
            std::lock_guard<std::recursive_mutex> sl (buildLock);
            const auto it = runtimes.find ((int64_t) (juce::int64) tree.getProperty (ids::id, -1));
            if (it != runtimes.end())
                applyTrackProperties (*it->second, TrackRef (tree));
        }
    }
    else if (type == ids::DEVICE)
    {
        if (property == ids::enabled)
        {
            std::lock_guard<std::recursive_mutex> sl (buildLock);
            const auto it = devices.find ((int64_t) (juce::int64) tree.getProperty (ids::id, -1));
            if (it != devices.end())
                it->second->enabled = DeviceRef (tree).enabled();
        }
        else if (property == ids::kind || property == ids::pluginDesc)
            graphDirty = true;
    }
    else if (type == ids::MASTER || type == ids::PROJECT)
    {
        if (property == ids::volumeDb || property == ids::tempo || property == ids::launchQuantise || property == ids::selectedTrack
            || property == ids::tuningA4)
        {
            std::lock_guard<std::recursive_mutex> sl (buildLock);
            applyProjectProperties();
        }
    }
    else if (type == ids::CLIP || type == ids::NOTE)
        graphDirty = true;
}

//==============================================================================
std::unique_ptr<juce::AudioProcessor> AudioEngine::makeProcessor (const DeviceRef& d)
{
    if (deviceFactory)
        return deviceFactory (d);

    if (d.kind() == "prisma")
    {
        auto prisma = std::make_unique<PrismaProcessor>();
        const auto state = d.stateBase64();
        if (state.isNotEmpty())
        {
            juce::MemoryBlock block;
            if (block.fromBase64Encoding (state))
                prisma->setStateInformation (block.getData(), (int) block.getSize());
        }
        return prisma;
    }
    return nullptr; // plugins arrive through registerProcessor or startPluginLoad; "missing" devices pass the sound through
}

bool AudioEngine::isDeviceLoading (int64_t deviceId) const
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    return loadingDevices.count (deviceId) > 0;
}

bool AudioEngine::isAnyDeviceLoading() const
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    return ! loadingDevices.empty();
}

void AudioEngine::reviveMissingDevices()
{
    if (model == nullptr)
        return;
    const std::function<void (const juce::ValueTree&)> visit = [&] (const juce::ValueTree& node) {
        if (node.hasType (ids::DEVICE))
        {
            DeviceRef device (node);
            juce::PluginDescription desc;
            if (device.kind() == "missing")
                if (const auto xml = juce::XmlDocument::parse (device.pluginDesc()); xml != nullptr && desc.loadFromXml (*xml)
                                                                                      && formatManager.doesPluginStillExist (desc))
                    device.setKind ("plugin", nullptr);
        }
        for (const auto& child : node)
            visit (child);
    };
    visit (model->root);
}

void AudioEngine::markDeviceMissing (int64_t deviceId)
{
    // Not undoable: the node only admits what is true. The state stays in the node.
    if (model != nullptr)
        if (const auto device = model->findDevice (deviceId); device.isValid() && device.kind() == "plugin")
            device.setKind ("missing", nullptr);
    ++deviceEpoch;
}

void AudioEngine::startPluginLoad (const DeviceRef& d)
{
    const auto id = d.id();
    if (loadingDevices.count (id) > 0)
        return;

    juce::PluginDescription desc;
    const auto xml = juce::XmlDocument::parse (d.pluginDesc());
    if (xml == nullptr || ! desc.loadFromXml (*xml) || ! formatManager.doesPluginStillExist (desc))
    {
        markDeviceMissing (id);
        return;
    }

    loadingDevices.insert (id);
    ++deviceEpoch;
    formatManager.createPluginInstanceAsync (
        desc, sampleRate.load(), blockSize,
        [this, flag = alive, id, epoch = modelEpoch, state = d.stateBase64()] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
            if (flag->load())
                finishPluginLoad (id, epoch, std::move (instance), error, state);
        });
}

void AudioEngine::finishPluginLoad (int64_t deviceId, uint64_t epoch, std::unique_ptr<juce::AudioPluginInstance> instance,
                                    const juce::String& error, const juce::String& stateBase64)
{
    juce::ignoreUnused (error);
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    if (epoch != modelEpoch)
        return; // another project is open now
    loadingDevices.erase (deviceId);
    ++deviceEpoch;

    const auto device = model != nullptr ? model->findDevice (deviceId) : DeviceRef();
    if (! device.isValid() || device.kind() != "plugin")
        return; // deleted again while it was loading: the instance is simply dropped

    if (instance == nullptr)
    {
        markDeviceMissing (deviceId);
        return;
    }
    if (stateBase64.isNotEmpty())
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding (stateBase64))
            instance->setStateInformation (block.getData(), (int) block.getSize());
    }
    registerProcessor (deviceId, std::move (instance));
}

DeviceInstance* AudioEngine::acquireDevice (const DeviceRef& d, std::set<int64_t>& used)
{
    const auto id = d.id();
    used.insert (id);
    const auto sourceKey = d.kind() + "|" + d.pluginDesc();
    const auto gen = generationCounter.load() + 1; // the graph being built

    if (const auto it = devices.find (id); it != devices.end())
    {
        if (it->second->sourceKey == sourceKey)
        {
            it->second->enabled = d.enabled();
            return it->second.get();
        }
        retiredDevices.push_back ({ std::move (it->second), gen }); // the device was swapped for another kind
        devices.erase (it);
    }

    std::unique_ptr<juce::AudioProcessor> processor;
    if (const auto pending = pendingProcessors.find (id); pending != pendingProcessors.end())
    {
        processor = std::move (pending->second);
        pendingProcessors.erase (pending);
    }
    else
        processor = makeProcessor (d);

    if (processor == nullptr)
    {
        if (d.kind() == "plugin")
            startPluginLoad (d);
        return nullptr;
    }

    auto instance = std::make_unique<DeviceInstance>();
    instance->deviceId = id;
    instance->processor = std::move (processor);
    instance->enabled = d.enabled();
    instance->sourceKey = sourceKey;
    if (auto* prisma = dynamic_cast<PrismaProcessor*> (instance->processor.get()))
        prisma->setTuneA4 (tuneA4.load());
    prepareDevice (*instance); // before the graph that contains it is published
    auto* raw = instance.get();
    devices[id] = std::move (instance);
    return raw;
}

std::shared_ptr<const daw::engine::MidiClip> AudioEngine::acquireClip (const ClipRef& clip, std::set<int64_t>& used)
{
    const auto id = clip.id();
    used.insert (id);
    const auto hash = hashClip (clip);

    auto& entry = clipCache[id];
    if (entry.clip == nullptr || entry.hash != hash)
    {
        auto c = std::make_shared<daw::engine::MidiClip>();
        c->id = id;
        c->name = clip.name().toStdString();
        c->colour = 0;
        c->lengthBeats = clip.lengthBeats();
        c->loopStart = clip.loopStart();
        c->startOffset = clip.startOffset();
        c->loop = clip.loopEnabled();
        c->notes = clip.notes();
        entry.clip = std::move (c);
        entry.hash = hash;
    }
    return entry.clip;
}

//==============================================================================
void AudioEngine::rebuildGraphIfNeeded()
{
    if (graphDirty.exchange (false))
        rebuildGraph();
}

void AudioEngine::rebuildGraph()
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);
    if (model == nullptr)
        return;
    graphDirty = false;

    const uint64_t generation = generationCounter.load() + 1;
    auto graph = std::make_unique<PlaybackGraph>();
    graph->generation = generation;

    std::set<int64_t> usedTracks, usedDevices, usedClips;
    for (const auto& track : model->tracks())
    {
        const auto trackId = track.id();
        usedTracks.insert (trackId);

        auto& slot = runtimes[trackId];
        if (slot == nullptr)
        {
            slot = std::make_unique<TrackRuntime> (trackId);
            slot->prepare (blockSize);
        }
        applyTrackProperties (*slot, track);

        PlaybackGraph::Track gt;
        gt.runtime = slot.get();
        for (const auto& device : track.devices())
            gt.chain.push_back (acquireDevice (device, usedDevices)); // nullptr keeps the position of a device without an instance
        for (int scene = 0; scene < track.numSlots(); ++scene)
        {
            const auto clip = track.clipAt (scene);
            gt.slots.push_back (clip.isValid() ? acquireClip (clip, usedClips) : nullptr);
        }
        graph->tracks.push_back (std::move (gt));
    }

    for (const auto& device : DeviceRef::childrenOf (model->masterDevicesNode()))
        graph->masterChain.push_back (acquireDevice (device, usedDevices));

    // What disappeared from the model is retired, not deleted: the audio thread may still be using the
    // previous graph, which contains it.
    for (auto it = runtimes.begin(); it != runtimes.end();)
    {
        if (usedTracks.count (it->first) == 0)
        {
            retiredTracks.push_back ({ std::move (it->second), generation });
            it = runtimes.erase (it);
        }
        else
            ++it;
    }
    for (auto it = devices.begin(); it != devices.end();)
    {
        if (usedDevices.count (it->first) == 0)
        {
            retiredDevices.push_back ({ std::move (it->second), generation });
            it = devices.erase (it);
        }
        else
            ++it;
    }
    for (auto it = clipCache.begin(); it != clipCache.end();)
        it = usedClips.count (it->first) == 0 ? clipCache.erase (it) : std::next (it);

    if (currentOwner != nullptr)
        retiredGraphs.push_back ({ std::move (currentOwner), generation });
    currentOwner = std::move (graph);
    generationCounter = generation;
    current.store (currentOwner.get(), std::memory_order_release);

    collectGarbage();
}

void AudioEngine::collectGarbage()
{
    std::lock_guard<std::recursive_mutex> sl (buildLock);

    // With the device stopped nobody reads the graph, so everything retired can go at once.
    const uint64_t seen = audioRunning.load() ? seenGeneration.load (std::memory_order_acquire) : ~(uint64_t) 0;
    const auto reclaim = [seen] (auto& list) {
        list.erase (std::remove_if (list.begin(), list.end(), [seen] (auto& r) { return r.generation <= seen; }), list.end());
    };
    // Graphs first: they point at runtimes and devices, and deleting them frees the clips they alone hold.
    reclaim (retiredGraphs);
    reclaim (retiredTracks);
    for (auto& r : retiredDevices)
        if (r.generation <= seen && r.object->processor != nullptr)
            r.object->processor->releaseResources();
    reclaim (retiredDevices);
}

//==============================================================================
void AudioEngine::captureDeviceStates (const juce::ValueTree& subtree)
{
    if (model == nullptr)
        return;
    if (subtree.hasType (ids::DEVICE))
    {
        DeviceRef device (subtree);
        if (auto* processor = findProcessor (device.id()))
        {
            juce::MemoryBlock state;
            processor->getStateInformation (state);
            // Not undoable on purpose: if the write were part of the transaction, undoing the removal would undo it too
            // and the device would come back with its old state.
            device.setStateBase64 (state.toBase64Encoding(), nullptr);
        }
    }
    for (const auto& child : subtree)
        captureDeviceStates (child);
}

void AudioEngine::removeDevice (const DeviceRef& device)
{
    if (model == nullptr)
        return;
    captureDeviceStates (device.state);
    model->removeDevice (device);
}

void AudioEngine::deleteTrack (int64_t trackId)
{
    if (model == nullptr)
        return;
    captureDeviceStates (model->findTrack (trackId).state);
    model->deleteTrack (trackId);
}

void AudioEngine::loadPluginInstrument (int64_t trackId, const juce::PluginDescription& desc,
                                        std::function<void (juce::String, int64_t)> done)
{
    formatManager.createPluginInstanceAsync (
        desc, sampleRate.load(), blockSize,
        [this, trackId, desc, done = std::move (done)] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error) {
            const auto track = model != nullptr ? model->findTrack (trackId) : TrackRef();
            if (instance == nullptr || ! track.isValid())
            {
                done (instance == nullptr ? Strings::errorPluginLoad + (error.isNotEmpty() ? error : desc.name) : juce::String ("track gone"), -1);
                return;
            }
            model->undo.beginNewTransaction (Strings::undoSetInstrument);
            if (track.numDevices() > 0)
                removeDevice (track.device (0));
            auto device = model->addDevice (track, "plugin", desc.name, 0);
            device.setPluginDesc (desc.createXml()->toString (juce::XmlElement::TextFormat().singleLine()), &model->undo);
            registerProcessor (device.id(), std::move (instance));
            rebuildGraph();
            done ({}, device.id());
        });
}
