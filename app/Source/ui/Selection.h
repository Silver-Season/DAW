#pragma once

#include <juce_events/juce_events.h>

#include <cstdint>

namespace ui
{
// What the user has selected in the window: it decides what Delete, Cmd+D, Cmd+R and the clipboard act on.
// Views write it when clicked and listen to it to draw the selection.
struct Selection
{
    enum class Kind
    {
        none,
        track,
        clip, // a slot (trackId, scene), empty or not
        scene,
        device
    };

    Kind kind = Kind::none;
    int64_t trackId = -1;
    int scene = -1;
    int64_t deviceId = -1;

    bool operator== (const Selection& o) const
    {
        return kind == o.kind && trackId == o.trackId && scene == o.scene && deviceId == o.deviceId;
    }
};

class SelectionState : public juce::ChangeBroadcaster
{
public:
    const Selection& get() const { return selection; }

    void selectTrack (int64_t trackId) { set ({ Selection::Kind::track, trackId, -1, -1 }); }
    void selectSlot (int64_t trackId, int scene) { set ({ Selection::Kind::clip, trackId, scene, -1 }); }
    void selectScene (int scene) { set ({ Selection::Kind::scene, -1, scene, -1 }); }
    void selectDevice (int64_t trackId, int64_t deviceId) { set ({ Selection::Kind::device, trackId, -1, deviceId }); }
    void clear() { set ({}); }

    // A scene was inserted or removed above `index`: keep a scene selection on the same scene.
    void setSceneIndex (int scene)
    {
        if (selection.kind == Selection::Kind::scene || selection.kind == Selection::Kind::clip)
            set ({ selection.kind, selection.trackId, scene, -1 });
    }

private:
    void set (const Selection& s)
    {
        if (s == selection)
            return;
        selection = s;
        sendChangeMessage();
    }

    Selection selection;
};
} // namespace ui
