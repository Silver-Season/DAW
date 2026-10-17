#include "SessionView.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../engine/SessionRecorder.h"
#include "../Settings.h"
#include "PaletteMenu.h"

namespace ui
{
namespace
{
// Where the playing clip is, 0..1 of its length, as the loop and the start point play it.
double clipProgress (const ClipRef& clip, double elapsedBeats)
{
    if (! clip.isValid())
        return std::fmod (elapsedBeats, 4.0) / 4.0;
    daw::engine::MidiClip shape;
    shape.lengthBeats = clip.lengthBeats();
    shape.loopStart = clip.loopStart();
    shape.startOffset = clip.startOffset();
    shape.loop = clip.loopEnabled();
    return juce::jlimit (0.0, 1.0, shape.positionAfter (elapsedBeats) / shape.endBeat());
}
} // namespace

namespace
{
constexpr int kStep = SessionView::kSlotHeight + SessionView::kGap;

// Soft glow around a playing clip, rendered once per size and colour (not every frame).
juce::Image glowImage (juce::Colour colour, int w, int h, float glow)
{
    struct Key
    {
        juce::uint32 argb;
        int w, h, glow10, generation;
        bool operator< (const Key& o) const
        {
            return std::tie (argb, w, h, glow10, generation) < std::tie (o.argb, o.w, o.h, o.glow10, o.generation);
        }
    };
    static std::map<Key, juce::Image> cache;
    const Key key { colour.getARGB(), w, h, juce::roundToInt (glow * 10.0f), ThemeManager::get().generation() };
    if (auto it = cache.find (key); it != cache.end())
        return it->second;
    if (cache.size() > 64)
        cache.clear();

    const int radius = juce::roundToInt (16.0f * glow);
    juce::Image image (juce::Image::ARGB, w + 2 * radius, h + 2 * radius, true);
    {
        juce::Graphics g (image);
        juce::DropShadow (colour.withAlpha (0.7f), radius, {}).drawForRectangle (g, { radius, radius, w, h });
    }
    cache[key] = image;
    return image;
}

juce::String shortcutText (juce::juce_wchar key, int modifiers = juce::ModifierKeys::commandModifier)
{
    return juce::KeyPress (key, modifiers, 0).getTextDescriptionWithIcons();
}
} // namespace

//==============================================================================
class SessionView::StripButton final : public juce::Component
{
public:
    enum class Role
    {
        activator,
        solo,
        arm
    };

    StripButton (Role r, const juce::String& label, const juce::String& help) : role (r), text (label)
    {
        setHelpText (help);
    }

    std::function<void (const juce::MouseEvent&)> onClick;

    void setState (bool v)
    {
        if (v != on)
        {
            on = v;
            repaint();
        }
    }
    void setText (const juce::String& s)
    {
        if (s != text)
        {
            text = s;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat();
        juce::Colour fill = t.panel2, ink = t.dimText;
        if (on)
        {
            fill = role == Role::activator ? t.accent : (role == Role::solo ? t.accent2 : t.record);
            ink = t.clipInk;
        }
        else if (isMouseOver())
            fill = t.panel2.brighter (0.08f);
        fillCell (g, r, fill, 0.7f);
        if (! on)
            strokeCell (g, r, t.line, 1.0f, 0.7f);
        g.setColour (ink);
        if (role == Role::arm)
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (r.getCentre()));
        else
        {
            g.setFont (t.uiFont (11.0f, 600));
            g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && ! e.mods.isPopupMenu() && onClick)
            onClick (e);
    }

private:
    Role role;
    juce::String text;
    bool on = false;
};

//==============================================================================
// Track names above the columns. Click selects, double-click renames, the right edge resizes the column.
class SessionView::Headers final : public juce::Component, public HelpProvider
{
public:
    explicit Headers (SessionView& v) : view (v) {}

    juce::String helpAt (juce::Point<int>) const override { return Strings::help::trackHeader; }

    juce::Rectangle<int> cellBounds (int index) const
    {
        return { view.columnX[(size_t) index] - view.scrollX, 0, view.columnW[(size_t) index], kHeaderHeight };
    }

    void beginRename (int index)
    {
        const auto id = view.columnTrack[(size_t) index];
        const auto track = view.model.findTrack (id);
        view.openRenameEditor (*this, cellBounds (index), track.name(), [this, id] (const juce::String& text) {
            view.actions.renameTrack (id, text);
        });
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const int selectedTrack = (int) view.model.selectedTrack();
        for (int i = 0; i < view.numColumns(); ++i)
        {
            const auto b = cellBounds (i);
            if (b.getRight() < 0 || b.getX() > getWidth())
                continue;
            const auto track = view.model.findTrack (view.columnTrack[(size_t) i]);
            if (! track.isValid())
                continue;
            fillCell (g, b.toFloat(), t.palette (track.colour()));
            if (track.id() == selectedTrack)
                strokeCell (g, b.toFloat(), t.text, 2.0f);
            g.setColour (t.clipInk);
            g.setFont (t.uiFont (12.5f, 600));
            g.drawText (track.name(), b.reduced (9, 0), juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (edgeAt (e.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        resizing = -1;
        const int edge = e.mods.isPopupMenu() ? -1 : edgeAt (e.x);
        if (edge >= 0)
        {
            resizing = edge;
            view.model.undo.beginNewTransaction (Strings::undoTrackWidth);
            return;
        }
        const int i = columnAt (e.x);
        if (i < 0)
            return;
        const auto id = view.columnTrack[(size_t) i];
        view.commands.selectTrack (id);
        if (e.mods.isPopupMenu())
            view.showTrackMenu (e.getScreenPosition(), id);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (resizing < 0)
            return;
        const auto track = view.model.findTrack (view.columnTrack[(size_t) resizing]);
        if (! track.isValid())
            return;
        const int displayed = e.x + view.scrollX - view.columnX[(size_t) resizing];
        const int stored = juce::jlimit (60, 400, juce::roundToInt ((double) displayed / view.stretch));
        if (stored != track.width())
            track.setWidth (stored, &view.model.undo);
    }

    void mouseUp (const juce::MouseEvent&) override { resizing = -1; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (edgeAt (e.x) >= 0)
            return;
        if (const int i = columnAt (e.x); i >= 0)
            beginRename (i);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        view.mouseWheelMove (e.getEventRelativeTo (&view), w);
    }

private:
    int columnAt (int x) const
    {
        for (int i = 0; i < view.numColumns(); ++i)
            if (x >= view.columnX[(size_t) i] - view.scrollX && x < view.columnX[(size_t) i] - view.scrollX + view.columnW[(size_t) i])
                return i;
        return -1;
    }

    // The column whose right edge is under x (within a few pixels), or -1.
    int edgeAt (int x) const
    {
        for (int i = 0; i < view.numColumns(); ++i)
        {
            const int right = view.columnX[(size_t) i] - view.scrollX + view.columnW[(size_t) i];
            if (x >= right - 5 && x <= right + kGap)
                return i;
        }
        return -1;
    }

    SessionView& view;
    int resizing = -1;
};

//==============================================================================
class SessionView::Grid final : public juce::Component, public HelpProvider
{
public:
    explicit Grid (SessionView& v) : view (v) {}

    juce::Rectangle<int> cellBounds (int column, int scene) const
    {
        return { view.columnX[(size_t) column] - view.scrollX, scene * kStep - view.scrollY, view.columnW[(size_t) column], kSlotHeight };
    }
    juce::Rectangle<int> stopBounds (int column) const
    {
        return { view.columnX[(size_t) column] - view.scrollX, view.model.numScenes() * kStep - view.scrollY, view.columnW[(size_t) column], kStopHeight };
    }

    juce::String helpAt (juce::Point<int> p) const override
    {
        int column = -1, scene = -1;
        if (! hit (p, column, scene))
            return {};
        if (scene == stopRow)
            return Strings::help::stopAllClips;
        const auto track = view.model.findTrack (view.columnTrack[(size_t) column]);
        if (view.recorder != nullptr && view.recorder->isRecording (track.id(), scene))
            return Strings::help::recordingSlot;
        if (track.clipAt (scene).isValid())
            return Strings::help::clipSlot;
        const bool overCircle = p.x - (view.columnX[(size_t) column] - view.scrollX) < kLaunchWidth;
        return overCircle && receivesLiveMidi (track, anyArmed()) ? Strings::help::recordSlot : Strings::help::emptySlot;
    }

    bool anyArmed() const
    {
        for (const auto& t : view.model.tracks())
            if (t.arm())
                return true;
        return false;
    }

    // The rule of AudioEngine::acceptsLiveMidi: armed tracks, or the selected one while none is armed.
    bool receivesLiveMidi (const TrackRef& track, bool anyTrackArmed) const
    {
        if (! track.isValid() || track.type() != "midi" || track.recordSource() != "midi" || track.input() == "none")
            return false;
        return track.arm() || (! anyTrackArmed && track.id() == view.model.selectedTrack());
    }

    void beginRename (int column, int scene)
    {
        const auto id = view.columnTrack[(size_t) column];
        const auto clip = view.model.findTrack (id).clipAt (scene);
        if (! clip.isValid())
            return;
        view.openRenameEditor (*this, cellBounds (column, scene), clip.name(), [this, id, scene] (const juce::String& text) {
            view.actions.renameClip (id, scene, text);
        });
    }

    // Repaints the cells whose play state changed since the last call (30 Hz).
    void tick()
    {
        const bool animate = view.engine.getSettings().animations() && theme().glow > 0.0f;
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        const bool blink2 = (now / 250) % 2 == 0, blink4 = (now / 125) % 2 == 0;
        const bool playing = view.engine.isPlaying();
        const double beats = view.engine.getPositionBeats();

        states.resize ((size_t) view.numColumns());
        for (int c = 0; c < view.numColumns(); ++c)
        {
            auto& st = states[(size_t) c];
            State next;
            const auto id = view.columnTrack[(size_t) c];
            if (auto* rt = view.engine.findRuntime (id))
            {
                next.playing = rt->playingSlot.load();
                next.queued = rt->queuedSlot.load();
                if (next.playing >= 0 && playing)
                {
                    const auto clip = view.model.findTrack (id).clipAt (next.playing);
                    next.progress = (int) (clipProgress (clip, juce::jmax (0.0, beats - rt->playingStartBeat.load())) * 64.0);
                }
            }
            next.blink2 = next.playing >= 0 ? blink2 : false;
            next.blink4 = next.queued >= 0 ? blink4 : false;
            if (view.recorder != nullptr)
                if (const int recScene = view.recorder->recordingScene (id); recScene >= 0)
                {
                    next.recording = recScene;
                    next.blink4 = true; // keeps the pulse and the growing bar coming; the cell is repainted below
                    if (recScene < view.model.numScenes())
                        repaint (cellBounds (c, recScene));
                }
            if (st.recording >= 0 && st.recording != next.recording && st.recording < view.model.numScenes())
                repaint (cellBounds (c, st.recording).expanded (20));

            const bool changed = next.playing != st.playing || next.queued != st.queued || next.progress != st.progress
                                 || next.blink2 != st.blink2 || next.blink4 != st.blink4;
            if (changed || (animate && next.playing >= 0))
            {
                for (int scene : { st.playing, st.queued, next.playing, next.queued })
                    if (scene >= 0 && scene < view.model.numScenes())
                        repaint (cellBounds (c, scene).expanded (20));
                repaint (stopBounds (c));
            }
            st = next;
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const int numScenes = view.model.numScenes();
        const bool animate = view.engine.getSettings().animations();
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        const bool blink2 = (now / 250) % 2 == 0, blink4 = (now / 125) % 2 == 0;
        const auto clipBounds = g.getClipBounds();
        const auto& sel = view.selection.get();
        const bool anyTrackArmed = anyArmed();

        struct Visible
        {
            int column;
            TrackRef track;
        };
        std::vector<Visible> visible;
        for (int c = 0; c < view.numColumns(); ++c)
        {
            const auto b = cellBounds (c, 0);
            if (b.getRight() < clipBounds.getX() - 30 || b.getX() > clipBounds.getRight() + 30)
                continue;
            visible.push_back ({ c, view.model.findTrack (view.columnTrack[(size_t) c]) });
        }

        // A hairline in the middle of each gap between two tracks, down the whole column, so that a column
        // reads as one track rather than a row of loose cells.
        {
            const int bottom = numScenes * kStep - view.scrollY + kStopHeight;
            g.setColour (t.line);
            for (const auto& v : visible)
                if (v.column + 1 < view.numColumns())
                {
                    const auto b = cellBounds (v.column, 0);
                    g.fillRect (b.getRight() + kGap / 2, juce::jmax (0, b.getY()), 1, juce::jmax (0, bottom - juce::jmax (0, b.getY())));
                }
        }

        // glow first, so that neighbouring cells draw over it
        if (t.glow > 0.0f)
            for (const auto& v : visible)
            {
                if (! v.track.isValid())
                    continue;
                auto* rt = view.engine.findRuntime (v.track.id());
                const int playingScene = rt != nullptr ? rt->playingSlot.load() : -1;
                const auto clip = playingScene >= 0 ? v.track.clipAt (playingScene) : ClipRef();
                if (clip.isValid())
                {
                    const auto b = cellBounds (v.column, playingScene);
                    const int radius = juce::roundToInt (16.0f * t.glow);
                    g.drawImageAt (glowImage (t.palette (clip.colour()), b.getWidth(), b.getHeight(), t.glow), b.getX() - radius, b.getY() - radius);
                }
            }

        for (const auto& v : visible)
        {
            if (! v.track.isValid())
                continue;
            auto* rt = view.engine.findRuntime (v.track.id());
            const int playingScene = rt != nullptr ? rt->playingSlot.load() : -1;
            const int queuedScene = rt != nullptr ? rt->queuedSlot.load() : -1;
            const bool armed = v.track.arm();
            const bool live = receivesLiveMidi (v.track, anyTrackArmed);
            const int recScene = view.recorder != nullptr ? view.recorder->recordingScene (v.track.id()) : -1;
            SessionRecorder::Progress progress;
            const bool hasProgress = recScene >= 0 && view.recorder->getProgress (v.track.id(), progress);
            // empty slots carry a trace of the track colour, so neighbouring columns differ
            const auto emptyFill = t.panel.interpolatedWith (t.palette (v.track.colour()), 0.08f);

            for (int s = 0; s < numScenes; ++s)
            {
                const auto b = cellBounds (v.column, s);
                if (b.getBottom() < clipBounds.getY() || b.getY() > clipBounds.getBottom())
                    continue;
                const auto cell = b.toFloat();
                const auto clip = v.track.clipAt (s);
                const bool selected = sel.kind == Selection::Kind::clip && sel.trackId == v.track.id() && sel.scene == s;
                const bool dropTarget = dragging && dropColumn == v.column && dropScene == s && ! (dragColumn == v.column && dragScene == s);

                if (! clip.isValid())
                {
                    if (recScene == s)
                    {
                        paintRecording (g, cell, progress, hasProgress, v.track, animate, blink4, now, selected);
                        continue;
                    }
                    fillCell (g, cell, dropTarget ? t.accent.withAlpha (0.12f).overlaidWith (t.panel).interpolatedWith (t.panel, 0.0f) : emptyFill);
                    strokeCell (g, cell, dropTarget || selected ? t.accent : t.line, selected ? 1.5f : 1.0f);
                    const auto mark = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ cell.getX() + 12.5f, cell.getCentreY() });
                    g.setColour (live ? (armed ? t.record : t.record.withAlpha (0.6f)) : t.line);
                    if (live)
                        g.drawEllipse (mark, 1.0f);
                    else
                        g.drawRoundedRectangle (mark, 2.0f, 1.0f);
                    continue;
                }

                const bool isPlaying = playingScene == s, isQueued = queuedScene == s;
                const auto colour = t.palette (clip.colour());
                fillCell (g, cell, t.clipColour (clip.colour(), isPlaying));
                if (dropTarget || selected)
                    strokeCell (g, cell, dropTarget ? t.accent : t.text, 2.0f);
                if (recScene == s) // an overdub runs on this clip: red outline, pulsing, and the notes played so far
                {
                    strokeCell (g, cell, t.record.withAlpha (animate ? pulse (now) : 1.0f), 2.0f);
                    if (hasProgress)
                        paintNoteMarks (g, cell, progress.notes, clip.lengthBeats(), t.clipInk.withAlpha (0.45f));
                }

                // play marker
                const auto icon = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ cell.getX() + 12.5f, cell.getCentreY() });
                float iconAlpha = isPlaying ? 1.0f : 0.55f;
                if (isPlaying && blink2 && animate)
                    iconAlpha = 0.15f;
                if (isQueued && blink4)
                    iconAlpha = 0.15f;
                g.setColour (t.clipInk.withAlpha (iconAlpha));
                g.fillPath (icons::play (icon));

                g.setColour (t.clipInk);
                g.setFont (t.uiFont (12.0f, 500));
                g.drawText (clip.name(), b.withTrimmedLeft (26).withTrimmedRight (6), juce::Justification::centredLeft, true);

                if (isPlaying)
                {
                    const double pos = view.engine.isPlaying()
                                           ? clipProgress (clip, juce::jmax (0.0, view.engine.getPositionBeats() - rt->playingStartBeat.load()))
                                           : 0.0;
                    g.setColour (t.clipInk.withAlpha (0.6f));
                    g.fillRect (cell.getX(), cell.getBottom() - 3.0f, (float) cell.getWidth() * (float) pos, 3.0f);

                    if (animate && t.glow > 0.0f)
                        drawSheen (g, cell, now, t.glow);
                }
                juce::ignoreUnused (colour);
            }

            // stop button of the track
            const auto stop = stopBounds (v.column).toFloat();
            fillCell (g, stop, t.panel);
            strokeCell (g, stop, t.line);
            g.setColour (playingScene >= 0 || queuedScene >= 0 ? t.text : t.dimText);
            g.fillRect (juce::Rectangle<float> (8.0f, 8.0f).withCentre (stop.getCentre()));
        }

        // the clip being dragged
        if (dragging && dragColumn >= 0)
        {
            const auto track = view.model.findTrack (view.columnTrack[(size_t) dragColumn]);
            const auto clip = track.clipAt (dragScene);
            if (clip.isValid())
            {
                auto ghost = cellBounds (dragColumn, dragScene).toFloat().withCentre (dragPosition.toFloat());
                fillCell (g, ghost, t.clipColour (clip.colour(), true).withAlpha (0.75f));
                g.setColour (t.clipInk);
                g.setFont (t.uiFont (12.0f, 500));
                g.drawText (clip.name(), ghost.toNearestInt().withTrimmedLeft (26), juce::Justification::centredLeft, true);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        dragColumn = dragScene = -1;
        pressColumn = pressScene = -1;
        int column = -1, scene = -1;
        if (! hit (e.getPosition(), column, scene))
            return;
        if (scene == stopRow)
        {
            pressColumn = column;
            pressScene = stopRow;
            return;
        }
        const auto id = view.columnTrack[(size_t) column];
        view.commands.selectSlot (id, scene);
        pressColumn = column;
        pressScene = scene;
        if (e.mods.isPopupMenu())
            view.showClipMenu (e.getScreenPosition(), id, scene);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressColumn < 0 || pressScene == stopRow || e.mods.isPopupMenu())
            return;
        const auto track = view.model.findTrack (view.columnTrack[(size_t) pressColumn]);
        if (! track.clipAt (pressScene).isValid())
            return;
        if (! dragging && e.getDistanceFromDragStart() < 5)
            return;
        dragging = true;
        dragColumn = pressColumn;
        dragScene = pressScene;
        dragPosition = e.getPosition();
        int column = -1, scene = -1;
        if (hit (e.getPosition(), column, scene) && scene != stopRow)
        {
            dropColumn = column;
            dropScene = scene;
        }
        else
            dropColumn = dropScene = -1;
        setMouseCursor (e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::DraggingHandCursor);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (dragging)
        {
            dragging = false;
            if (dropColumn >= 0 && dragColumn >= 0)
                view.actions.moveClip (view.columnTrack[(size_t) dragColumn], dragScene, view.columnTrack[(size_t) dropColumn], dropScene, e.mods.isAltDown());
            if (dropColumn >= 0)
                view.commands.selectSlot (view.columnTrack[(size_t) dropColumn], dropScene);
            dragColumn = dragScene = dropColumn = dropScene = -1;
            repaint();
            return;
        }
        if (e.mods.isPopupMenu() || pressColumn < 0 || e.getNumberOfClicks() > 1)
            return;
        int column = -1, scene = -1;
        if (! hit (e.getPosition(), column, scene) || column != pressColumn || scene != pressScene)
            return;
        const auto id = view.columnTrack[(size_t) column];
        const bool atMarker = e.x - (view.columnX[(size_t) column] - view.scrollX) < kLaunchWidth;
        // A click selects; only the play marker at the left edge of the clip launches it.
        if (scene != stopRow && view.recorder != nullptr && view.recorder->isRecording (id, scene))
            view.recorder->stopRecording (id); // a click anywhere on the slot that records ends the recording
        else if (scene == stopRow)
            view.engine.stopTrack (id);
        else if (atMarker && view.model.findTrack (id).clipAt (scene).isValid())
            view.engine.launchClip (id, scene);
        else if (atMarker && view.recorder != nullptr && receivesLiveMidi (view.model.findTrack (id), anyArmed()))
            view.recorder->toggleRecording (id, scene); // the record circle of an empty slot
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        int column = -1, scene = -1;
        if (e.mods.isPopupMenu() || ! hit (e.getPosition(), column, scene) || scene == stopRow)
            return;
        const auto id = view.columnTrack[(size_t) column];
        if (view.model.findTrack (id).clipAt (scene).isValid())
        {
            // double-click on a clip: edit it
            view.commands.selectSlot (id, scene);
            if (view.onOpenClip)
                view.onOpenClip (id, scene);
            return;
        }
        if (view.actions.createEmptyClip (id, scene).isValid())
        {
            view.commands.selectSlot (id, scene);
            if (view.onOpenClip)
                view.onOpenClip (id, scene);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        view.mouseWheelMove (e.getEventRelativeTo (&view), w);
    }

private:
    static constexpr int stopRow = -2;
    static constexpr int kLaunchWidth = 26; // the play marker at the left edge of a clip

    struct State
    {
        int playing = -1, queued = -1, progress = -1, recording = -1;
        bool blink2 = false, blink4 = false;
    };

    bool hit (juce::Point<int> p, int& column, int& scene) const
    {
        for (int c = 0; c < view.numColumns(); ++c)
        {
            const int x = view.columnX[(size_t) c] - view.scrollX;
            if (p.x < x || p.x >= x + view.columnW[(size_t) c])
                continue;
            const int y = p.y + view.scrollY;
            if (y < 0)
                return false;
            const int row = y / kStep;
            if (row < view.model.numScenes())
            {
                if (y - row * kStep >= kSlotHeight)
                    return false; // in the gap
                column = c;
                scene = row;
                return true;
            }
            if (y >= view.model.numScenes() * kStep && y < view.model.numScenes() * kStep + kStopHeight)
            {
                column = c;
                scene = stopRow;
                return true;
            }
            return false;
        }
        return false;
    }

    static float pulse (juce::uint32 now) { return 0.6f + 0.4f * (0.5f + 0.5f * std::sin ((float) (now % 1200) / 1200.0f * juce::MathConstants<float>::twoPi)); }

    // A slot that is being recorded: red, with a pulsing circle, a bar that grows with the length and the notes so far.
    void paintRecording (juce::Graphics& g, juce::Rectangle<float> cell, const SessionRecorder::Progress& p, bool started, const TrackRef& track,
                         bool animate, bool blink, juce::uint32 now, bool selected) const
    {
        const auto& t = theme();
        const bool waiting = ! started || p.waiting;
        fillCell (g, cell, waiting ? t.panel.interpolatedWith (t.record, 0.18f) : t.panel.interpolatedWith (t.record, 0.78f));
        strokeCell (g, cell, selected ? t.text : t.record, selected ? 2.0f : 1.0f);

        const auto mark = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ cell.getX() + 12.5f, cell.getCentreY() });
        if (waiting)
        {
            g.setColour (t.record.withAlpha (blink || ! animate ? 1.0f : 0.2f));
            g.fillEllipse (mark);
            return;
        }
        g.setColour (t.clipInk.withAlpha (animate ? pulse (now) : 1.0f));
        g.fillEllipse (mark);

        // The bar grows through a number of bars that doubles when it fills up (or through the fixed length).
        const double bpb = view.model.beatsPerBar();
        const int fixedBars = view.model.recordFixedBars();
        int totalBars = fixedBars > 0 ? fixedBars : 1;
        while (fixedBars == 0 && (double) totalBars * bpb < p.lengthBeats + 1.0e-6)
            totalBars *= 2;
        const double total = (double) totalBars * bpb;
        g.setColour (t.clipInk.withAlpha (0.7f));
        g.fillRect (cell.getX(), cell.getBottom() - 3.0f, cell.getWidth() * (float) juce::jlimit (0.0, 1.0, p.lengthBeats / total), 3.0f);

        paintNoteMarks (g, cell, p.notes, total, t.clipInk.withAlpha (0.85f));
        juce::ignoreUnused (track);
    }

    // Notes as short strokes across the slot, lowest at the bottom; `total` beats span the width right of the marker.
    static void paintNoteMarks (juce::Graphics& g, juce::Rectangle<float> cell, const std::vector<daw::engine::Note>& notes, double total,
                                juce::Colour colour)
    {
        const auto area = cell.withTrimmedLeft (26.0f).withTrimmedRight (6.0f);
        if (notes.empty() || area.getWidth() <= 8.0f || total <= 0.0)
            return;
        int lo = 127, hi = 0;
        for (const auto& n : notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
        const float top = cell.getY() + 5.0f, height = cell.getHeight() - 12.0f;
        const float span = (float) juce::jmax (7, hi - lo);
        g.setColour (colour);
        for (const auto& n : notes)
        {
            const float x = area.getX() + (float) (n.start / total) * area.getWidth();
            if (x >= area.getRight())
                continue;
            const float w = juce::jmax (1.5f, (float) (n.length / total) * area.getWidth());
            const float y = top + height - (float) (n.pitch - lo) / span * height;
            g.fillRect (x, y - 1.0f, juce::jmin (w, area.getRight() - x), 2.0f);
        }
    }

    // The rainbow glint that runs across a playing clip (the "sheen" of the concept).
    static void drawSheen (juce::Graphics& g, juce::Rectangle<float> cell, juce::uint32 now, float glow)
    {
        const float phase = (float) (now % 2400) / 2400.0f;
        const float centre = cell.getX() + cell.getWidth() * (1.15f - 1.3f * phase);
        const float half = juce::jmax (12.0f, cell.getWidth() * 0.25f);
        juce::ColourGradient grad (juce::Colours::transparentWhite, centre - half, 0.0f, juce::Colours::transparentWhite, centre + half, 0.0f, false);
        const float a = juce::jlimit (0.0f, 1.0f, 0.55f * glow * 0.6f);
        grad.addColour (0.35, juce::Colours::white.withAlpha (a * 0.8f));
        grad.addColour (0.5, juce::Colour::fromRGB (255, 120, 220).withAlpha (a * 0.7f));
        grad.addColour (0.65, juce::Colour::fromRGB (120, 220, 255).withAlpha (a * 0.7f));
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (cell, theme().radius);
        g.reduceClipRegion (clip);
        g.setGradientFill (grad);
        g.fillRect (cell);
    }

    SessionView& view;
    std::vector<State> states;
    int pressColumn = -1, pressScene = -1;
    bool dragging = false;
    int dragColumn = -1, dragScene = -1, dropColumn = -1, dropScene = -1;
    juce::Point<int> dragPosition;
};

//==============================================================================
class SessionView::Scenes final : public juce::Component, public HelpProvider
{
public:
    explicit Scenes (SessionView& v) : view (v) {}

    juce::Rectangle<int> cellBounds (int scene) const { return { 0, scene * kStep - view.scrollY, getWidth(), kSlotHeight }; }
    juce::Rectangle<int> stopBounds() const { return { 0, view.model.numScenes() * kStep - view.scrollY, getWidth(), kStopHeight }; }

    juce::String helpAt (juce::Point<int> p) const override
    {
        if (stopBounds().contains (p))
            return Strings::help::stopAllClips;
        const int s = sceneAt (p);
        if (s < 0)
            return {};
        return p.x < kLaunchWidth ? Strings::help::sceneLaunch : Strings::help::sceneName;
    }

    void beginRename (int scene)
    {
        if (scene < 0 || scene >= view.model.numScenes())
            return;
        view.openRenameEditor (*this, cellBounds (scene).withTrimmedLeft (22), view.model.sceneAt (scene).name(),
                               [this, scene] (const juce::String& text) { view.actions.renameScene (scene, text); });
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto& sel = view.selection.get();
        const auto clipBounds = g.getClipBounds();
        for (int s = 0; s < view.model.numScenes(); ++s)
        {
            const auto b = cellBounds (s);
            if (b.getBottom() < clipBounds.getY() || b.getY() > clipBounds.getBottom())
                continue;
            const auto scene = view.model.sceneAt (s);
            const bool selected = sel.kind == Selection::Kind::scene && sel.scene == s;
            fillCell (g, b.toFloat(), t.panel2);
            strokeCell (g, b.toFloat(), selected ? t.accent : t.line, selected ? 1.5f : 1.0f);
            const bool hot = s == hoverScene && hoverLaunch;
            g.setColour (hot ? t.play : (scene.colour() >= 0 ? t.palette (scene.colour()) : t.dimText));
            g.fillPath (icons::play (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ (float) b.getX() + 13.0f, (float) b.getCentreY() })));
            g.setColour (t.text);
            g.setFont (t.uiFont (12.0f));
            g.drawText (scene.name(), b.withTrimmedLeft (28).withTrimmedRight (6), juce::Justification::centredLeft, true);
        }
        const auto stop = stopBounds().toFloat();
        fillCell (g, stop, t.panel);
        strokeCell (g, stop, t.line);
        g.setColour (t.dimText);
        g.fillRect (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ stop.getX() + 14.0f, stop.getCentreY() }));
        g.setColour (t.text);
        g.setFont (t.uiFont (11.5f));
        g.drawText (Strings::labelStopAll, stop.toNearestInt().withTrimmedLeft (28), juce::Justification::centredLeft, true);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int s = sceneAt (e.getPosition());
        const bool launch = s >= 0 && e.x < kLaunchWidth;
        if (s != hoverScene || launch != hoverLaunch)
        {
            hoverScene = s;
            hoverLaunch = launch;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hoverScene = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (stopBounds().contains (e.getPosition()))
        {
            if (! e.mods.isPopupMenu())
                view.engine.stopAllClips();
            return;
        }
        const int s = sceneAt (e.getPosition());
        if (s < 0)
            return;
        view.selection.selectScene (s);
        if (e.mods.isPopupMenu())
            view.showSceneMenu (e.getScreenPosition(), s);
        else if (e.x < kLaunchWidth)
            view.engine.launchScene (s);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int s = sceneAt (e.getPosition());
        if (s >= 0 && e.x >= kLaunchWidth && ! e.mods.isPopupMenu())
            beginRename (s);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        view.mouseWheelMove (e.getEventRelativeTo (&view), w);
    }

private:
    static constexpr int kLaunchWidth = 26;

    int sceneAt (juce::Point<int> p) const
    {
        const int y = p.y + view.scrollY;
        if (y < 0)
            return -1;
        const int row = y / kStep;
        return row < view.model.numScenes() && y - row * kStep < kSlotHeight ? row : -1;
    }

    SessionView& view;
    int hoverScene = -1;
    bool hoverLaunch = false;
};

//==============================================================================
class SessionView::TrackStrip final : public juce::Component
{
public:
    TrackStrip (SessionView& v, int64_t id) : view (v), trackId (id),
          activator (StripButton::Role::activator, {}, Strings::help::activator),
          solo (StripButton::Role::solo, "S", Strings::help::solo),
          arm (StripButton::Role::arm, {}, Strings::help::arm)
    {
        auto& model = view.model;
        const auto t = model.findTrack (trackId);

        for (auto* k : { &sendA, &sendB })
        {
            k->setEnabled (false);
            k->setHelpText (Strings::help::sendKnob);
            addAndMakeVisible (*k);
        }
        sendA.setCaption (Strings::labelSendA);
        sendB.setCaption (Strings::labelSendB);

        pan.setCaption (Strings::labelPan);
        pan.setBipolar (true);
        pan.setDefaultValue (0.5);
        pan.setHelpText (Strings::help::panKnob);
        pan.valueText = [] (double val) {
            const int p = juce::roundToInt ((val * 2.0 - 1.0) * 50.0);
            return p == 0 ? juce::String ("C") : juce::String (std::abs (p)) + (p < 0 ? "L" : "R");
        };
        pan.onGestureStart = [this] { view.model.undo.beginNewTransaction (Strings::undoTrackPan); };
        pan.onValueChange = [this] (double) {
            if (auto track = view.model.findTrack (trackId); track.isValid())
                track.setPan ((float) (pan.getValue() * 2.0 - 1.0), &view.model.undo);
        };
        addAndMakeVisible (pan);

        meter.setHelpText (Strings::help::meter);
        addAndMakeVisible (meter);

        fader.setHelpText (Strings::help::fader);
        fader.onGestureStart = [this] { view.model.undo.beginNewTransaction (Strings::undoTrackVolume); };
        fader.onValueChange = [this] (double db) {
            if (auto track = view.model.findTrack (trackId); track.isValid())
                track.setVolumeDb ((float) db, &view.model.undo);
            dbField.setValue (db);
        };
        addAndMakeVisible (fader);

        dbField.setRange (Fader::kMinDb, Fader::kMaxDb, 0.1);
        dbField.setDecimals (1);
        dbField.setBoxed (false);
        dbField.setFontHeight (11.0f);
        dbField.setJustification (juce::Justification::centredRight);
        dbField.setDragSensitivity (0.2);
        dbField.setWheelStep (0.1);
        dbField.setHelpText (Strings::help::volumeReadout);
        dbField.onGestureStart = [this] { view.model.undo.beginNewTransaction (Strings::undoTrackVolume); };
        dbField.onValueChange = [this] (double db) {
            if (auto track = view.model.findTrack (trackId); track.isValid())
                track.setVolumeDb ((float) db, &view.model.undo);
            fader.setValueDb (db);
        };
        addAndMakeVisible (dbField);

        activator.onClick = [this] (const juce::MouseEvent&) {
            if (auto track = view.model.findTrack (trackId); track.isValid())
                view.actions.setTrackMute (trackId, ! track.mute());
        };
        solo.onClick = [this] (const juce::MouseEvent& e) {
            if (auto track = view.model.findTrack (trackId); track.isValid())
                view.actions.setTrackSolo (trackId, ! track.solo(), ! e.mods.isCommandDown());
        };
        arm.onClick = [this] (const juce::MouseEvent& e) {
            if (auto track = view.model.findTrack (trackId); track.isValid())
                view.actions.setTrackArm (trackId, ! track.arm(), ! e.mods.isCommandDown());
        };
        for (auto* b : { &activator, &solo, &arm })
            addAndMakeVisible (*b);
        refresh (true);
        juce::ignoreUnused (t);
    }

    int64_t id() const { return trackId; }

    void refresh (bool force = false)
    {
        const auto track = view.model.findTrack (trackId);
        if (! track.isValid())
            return;
        if (force || ! pan.isDragging())
            pan.setValue (((double) track.pan() + 1.0) * 0.5);
        if (force || ! fader.isDragging())
            fader.setValueDb ((double) track.volumeDb());
        if (force || ! dbField.isEditing())
            dbField.setValue ((double) track.volumeDb());
        pan.setRingColour (theme().text);
        activator.setState (! track.mute());
        activator.setText (juce::String (view.model.indexOfTrack (trackId) + 1));
        solo.setState (track.solo());
        arm.setState (track.arm());
        sendA.setRingColour (theme().palette (track.colour()));
        sendB.setRingColour (theme().palette (track.colour()));
    }

    void updateMeter()
    {
        if (auto* rt = view.engine.findRuntime (trackId))
            meter.push (rt->meterL.exchange (0.0f), rt->meterR.exchange (0.0f));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = getLocalBounds().toFloat();
        fillCell (g, r, t.panel);
        strokeCell (g, r, t.line);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto buttons = r.removeFromBottom (22);
        const int bw = (buttons.getWidth() - 8) / 3;
        activator.setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (4);
        solo.setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (4);
        arm.setBounds (buttons);
        r.removeFromBottom (6);

        auto sends = r.removeFromTop (40);
        const bool narrow = getWidth() < 96;
        const int cell = juce::jmin (36, (sends.getWidth()) / (narrow ? 1 : 3));
        if (narrow)
        {
            sendA.setVisible (false);
            sendB.setVisible (false);
            pan.setBounds (sends.withSizeKeepingCentre (cell, 40));
        }
        else
        {
            sendA.setVisible (true);
            sendB.setVisible (true);
            const int gap = (sends.getWidth() - 3 * cell) / 2;
            sendA.setBounds (sends.getX(), sends.getY(), cell, 40);
            sendB.setBounds (sends.getX() + cell + gap, sends.getY(), cell, 40);
            pan.setBounds (sends.getRight() - cell, sends.getY(), cell, 40);
        }
        r.removeFromTop (6);

        dbField.setBounds (r.removeFromBottom (16).withTrimmedLeft (r.getWidth() / 3));
        meter.setBounds (r.getX(), r.getY(), 14, r.getHeight());
        fader.setBounds (r.getX() + 20, r.getY(), 24, r.getHeight());
    }

private:
    SessionView& view;
    int64_t trackId;
    Knob sendA, sendB, pan;
    Meter meter;
    Fader fader;
    NumberField dbField;
    StripButton activator, solo, arm;
};

class SessionView::MasterStrip final : public juce::Component
{
public:
    explicit MasterStrip (SessionView& v) : view (v)
    {
        cue.setCaption (Strings::labelCue);
        cue.setEnabled (false);
        cue.setHelpText (Strings::help::cueKnob);
        addAndMakeVisible (cue);
        meter.setHelpText (Strings::help::meter);
        addAndMakeVisible (meter);

        fader.setHelpText (Strings::help::fader);
        fader.onGestureStart = [this] { view.model.undo.beginNewTransaction (Strings::undoMasterVolume); };
        fader.onValueChange = [this] (double db) {
            view.model.setMasterVolumeDb ((float) db);
            dbField.setValue (db);
        };
        addAndMakeVisible (fader);

        dbField.setRange (Fader::kMinDb, Fader::kMaxDb, 0.1);
        dbField.setDecimals (1);
        dbField.setBoxed (false);
        dbField.setFontHeight (11.0f);
        dbField.setJustification (juce::Justification::centredRight);
        dbField.setDragSensitivity (0.2);
        dbField.setWheelStep (0.1);
        dbField.setHelpText (Strings::help::volumeReadout);
        dbField.onGestureStart = [this] { view.model.undo.beginNewTransaction (Strings::undoMasterVolume); };
        dbField.onValueChange = [this] (double db) {
            view.model.setMasterVolumeDb ((float) db);
            fader.setValueDb (db);
        };
        addAndMakeVisible (dbField);
        refresh (true);
    }

    void refresh (bool force = false)
    {
        if (force || ! fader.isDragging())
            fader.setValueDb ((double) view.model.masterVolumeDb());
        if (force || ! dbField.isEditing())
            dbField.setValue ((double) view.model.masterVolumeDb());
        cue.setRingColour (theme().text);
    }

    void updateMeter() { meter.push (view.engine.masterMeterL.exchange (0.0f), view.engine.masterMeterR.exchange (0.0f)); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = getLocalBounds().toFloat();
        fillCell (g, r, t.panel);
        strokeCell (g, r, t.line);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        r.removeFromBottom (28); // the buttons row of the track strips
        cue.setBounds (r.removeFromTop (40).withWidth (36));
        r.removeFromTop (6);
        dbField.setBounds (r.removeFromBottom (16).withTrimmedLeft (r.getWidth() / 3));
        meter.setBounds (r.getX(), r.getY(), 14, r.getHeight());
        fader.setBounds (r.getX() + 20, r.getY(), 24, r.getHeight());
    }

private:
    SessionView& view;
    Knob cue;
    Meter meter;
    Fader fader;
    NumberField dbField;
};

class SessionView::MixerRow final : public juce::Component
{
public:
    explicit MixerRow (SessionView& v) : view (v) {}

    void rebuild()
    {
        strips.clear();
        removeAllChildren();
        for (const auto id : view.columnTrack)
        {
            strips.push_back (std::make_unique<TrackStrip> (view, id));
            addAndMakeVisible (*strips.back());
        }
        layoutStrips();
    }

    void layoutStrips()
    {
        for (size_t i = 0; i < strips.size() && i < view.columnX.size(); ++i)
            strips[i]->setBounds (view.columnX[i] - view.scrollX, 0, view.columnW[i], getHeight());
    }

    void refresh()
    {
        for (auto& s : strips)
            s->refresh();
    }

    void updateMeters()
    {
        for (auto& s : strips)
            s->updateMeter();
    }

    void resized() override { layoutStrips(); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        view.mouseWheelMove (e.getEventRelativeTo (&view), w);
    }

private:
    SessionView& view;
    std::vector<std::unique_ptr<TrackStrip>> strips;
};

//==============================================================================
SessionView::SessionView (AudioEngine& e, ProjectModel& m, ProjectActions& a, SelectionState& s, EditCommands& c)
    : engine (e), model (m), actions (a), selection (s), commands (c)
{
    headers = std::make_unique<Headers> (*this);
    grid = std::make_unique<Grid> (*this);
    scenes = std::make_unique<Scenes> (*this);
    mixer = std::make_unique<MixerRow> (*this);
    masterStrip = std::make_unique<MasterStrip> (*this);
    for (auto* child : std::initializer_list<juce::Component*> { grid.get(), headers.get(), scenes.get(), mixer.get(), masterStrip.get() })
        addAndMakeVisible (child);

    hbar.setAutoHide (true);
    vbar.setAutoHide (true);
    hbar.addListener (this);
    vbar.addListener (this);
    hbar.setHelpText (Strings::help::hscroll);
    vbar.setHelpText (Strings::help::vscroll);
    addAndMakeVisible (hbar);
    addAndMakeVisible (vbar);

    mixerTab.setHelpText (Strings::help::splitterMixer);
    mixerTab.onClick = [this] { setMixerVisible (! mixerVisible); };
    mixerTab.setTooltips (Strings::tips::hideMixer, Strings::tips::showMixer);
    addAndMakeVisible (mixerTab);

    model.root.addListener (this);
    selection.addChangeListener (this);
}

SessionView::~SessionView()
{
    selection.removeChangeListener (this);
    model.root.removeListener (this);
    hbar.removeListener (this);
    vbar.removeListener (this);
}

//==============================================================================
void SessionView::structureChanged (const juce::ValueTree& parent)
{
    if (parent.hasType (ids::TRACKS) || parent.hasType (ids::SCENES) || parent.hasType (ids::SLOTS) || parent.hasType (ids::SLOT)
        || parent.hasType (ids::PROJECT))
        structureDirty = true;
    propertiesDirty = true;
}

void SessionView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    headers->repaint();
    grid->repaint();
    scenes->repaint();
}

void SessionView::setMixerVisible (bool visible)
{
    if (visible == mixerVisible)
        return;
    mixerVisible = visible;
    mixerTab.setOpen (visible);
    resized();
    repaint();
    if (onMixerVisibilityChanged)
        onMixerVisibilityChanged (visible);
}

int SessionView::contentHeight() const
{
    return model.numScenes() * kStep + kStopHeight;
}

void SessionView::updateLayout()
{
    columnX.clear();
    columnW.clear();
    columnTrack.clear();
    const int n = model.numTracks();
    int sum = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto track = model.trackAt (i);
        const int w = juce::jlimit (60, 400, track.width());
        columnW.push_back (w);
        columnTrack.push_back (track.id());
        sum += w;
    }
    const int available = gridArea.getWidth();
    const int gaps = juce::jmax (0, n - 1) * kGap;
    stretch = (n > 0 && sum + gaps < available) ? (double) (available - gaps) / (double) sum : 1.0;
    int x = 0;
    for (int i = 0; i < n; ++i)
    {
        int w = juce::roundToInt ((double) columnW[(size_t) i] * stretch);
        if (stretch > 1.0 && i == n - 1)
            w = juce::jmax (1, available - x); // absorb the rounding error: the last column ends at the master column
        columnX.push_back (x);
        columnW[(size_t) i] = w;
        x += w + kGap;
    }
    clampScroll();
}

void SessionView::clampScroll()
{
    scrollX = juce::jlimit (0, juce::jmax (0, contentWidth() - gridArea.getWidth()), scrollX);
    scrollY = juce::jlimit (0, juce::jmax (0, contentHeight() - gridArea.getHeight()), scrollY);
}

void SessionView::applyScroll()
{
    hbar.setRangeLimits (0.0, (double) juce::jmax (contentWidth(), gridArea.getWidth()));
    hbar.setCurrentRange ((double) scrollX, (double) gridArea.getWidth(), juce::dontSendNotification);
    vbar.setRangeLimits (0.0, (double) juce::jmax (contentHeight(), gridArea.getHeight()));
    vbar.setCurrentRange ((double) scrollY, (double) gridArea.getHeight(), juce::dontSendNotification);
    mixer->layoutStrips();
    repaintViews();
}

void SessionView::repaintViews()
{
    headers->repaint();
    grid->repaint();
    scenes->repaint();
}

void SessionView::setScrollPosition (int x, int y)
{
    scrollX = x;
    scrollY = y;
    clampScroll();
    applyScroll();
}

void SessionView::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hbar)
        scrollX = (int) start;
    else
        scrollY = (int) start;
    clampScroll();
    mixer->layoutStrips();
    repaintViews();
}

void SessionView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float scale = 480.0f * (w.isReversed ? -1.0f : 1.0f);
    int dx = juce::roundToInt (-w.deltaX * scale), dy = juce::roundToInt (-w.deltaY * scale);
    if (e.mods.isShiftDown() && dx == 0)
        std::swap (dx, dy);
    const int oldX = scrollX, oldY = scrollY;
    scrollX += dx;
    scrollY += dy;
    clampScroll();
    if (oldX != scrollX || oldY != scrollY)
        applyScroll();
}

void SessionView::resized()
{
    const int left = kPad, right = getWidth() - kPad, top = kTopPad, bottom = getHeight() - kPad;
    const int leftWidth = right - left - kMasterWidth - kGap;
    headerArea = { left, top, leftWidth, kHeaderHeight };
    masterHeaderArea = { right - kMasterWidth, top, kMasterWidth, kHeaderHeight };
    const int gridTop = top + kHeaderHeight + kGap;
    const int contentH = contentHeight();

    int mixerHeight = 0;
    int gridBottom = bottom - 2;
    if (mixerVisible)
    {
        const int free = bottom - gridTop - kMixerGap - contentH;
        mixerHeight = juce::jlimit (kMixerMinHeight, kMixerMaxHeight, free);
        gridBottom = bottom - mixerHeight - kMixerGap;
    }
    gridArea = { left, gridTop, leftWidth, juce::jmax (0, gridBottom - gridTop) };
    sceneArea = { right - kMasterWidth, gridTop, kMasterWidth, gridArea.getHeight() };
    mixerArea = { left, bottom - mixerHeight, leftWidth, mixerHeight };
    masterStripArea = { right - kMasterWidth, bottom - mixerHeight, kMasterWidth, mixerHeight };

    headers->setBounds (headerArea);
    grid->setBounds (gridArea);
    scenes->setBounds (sceneArea);
    mixer->setBounds (mixerArea);
    mixer->setVisible (mixerVisible);
    masterStrip->setBounds (masterStripArea);
    masterStrip->setVisible (mixerVisible);

    constexpr int bar = 9;
    hbar.setBounds (gridArea.getX(), gridArea.getBottom() - bar, gridArea.getWidth() - bar, bar);
    vbar.setBounds (gridArea.getRight() - bar, gridArea.getY(), bar, gridArea.getHeight());

    mixerTab.setBounds (juce::Rectangle<int> (30, 11).withCentre ({ sceneArea.getCentreX(), gridArea.getBottom() + (mixerVisible ? kMixerGap / 2 : 6) }));
    mixerTab.setOpen (mixerVisible);

    updateLayout();
    mixer->layoutStrips();
    applyScroll();
}

void SessionView::rebuildStrips()
{
    updateLayout();
    mixer->rebuild();
    masterStrip->refresh (true);
    resized();
}

void SessionView::tick()
{
    if (structureDirty)
    {
        structureDirty = propertiesDirty = false;
        rebuildStrips();
        repaintViews();
        repaint();
    }
    else if (propertiesDirty)
    {
        propertiesDirty = false;
        updateLayout();
        mixer->layoutStrips();
        mixer->refresh();
        masterStrip->refresh();
        applyScroll();
    }
    if (mixerVisible)
    {
        mixer->updateMeters();
        masterStrip->updateMeter();
    }
    grid->tick();
}

//==============================================================================
void SessionView::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);

    // the master column header
    fillCell (g, masterHeaderArea.toFloat(), t.panel2);
    g.setColour (t.text);
    g.setFont (t.uiFont (12.5f, 600));
    g.drawText (Strings::masterName, masterHeaderArea.reduced (9, 0), juce::Justification::centredLeft, true);

    if (model.numTracks() == 0)
    {
        g.setColour (t.dimText);
        g.setFont (t.uiFont (14.0f));
        g.drawText (Strings::labelPlaceholderProject, gridArea, juce::Justification::centred, true);
    }
}

//==============================================================================
// Drops
SessionView::DropSpot SessionView::spotAt (juce::Point<int> p) const
{
    DropSpot s;
    s.valid = true;
    auto columnAtX = [this] (int contentX) {
        for (int i = 0; i < numColumns(); ++i)
            if (contentX >= columnX[(size_t) i] && contentX < columnX[(size_t) i] + columnW[(size_t) i])
                return i;
        return -1;
    };
    int column = -1;
    if (headerArea.contains (p))
        column = columnAtX (p.x - headerArea.getX() + scrollX);
    else if (gridArea.contains (p))
    {
        column = columnAtX (p.x - gridArea.getX() + scrollX);
        const int y = p.y - gridArea.getY() + scrollY;
        const int row = y / (kSlotHeight + kGap);
        if (column >= 0 && y >= 0 && row < model.numScenes() && y - row * (kSlotHeight + kGap) < kSlotHeight)
        {
            s.target = browser::DropTarget::slot;
            s.trackId = columnTrack[(size_t) column];
            s.scene = row;
            return s;
        }
    }
    else if (mixerArea.contains (p))
        column = columnAtX (p.x - mixerArea.getX() + scrollX);

    if (column >= 0)
    {
        s.target = browser::DropTarget::track;
        s.trackId = columnTrack[(size_t) column];
    }
    else
        s.target = browser::DropTarget::newTrack;
    return s;
}

juce::Rectangle<int> SessionView::spotBounds (const DropSpot& s) const
{
    if (! s.valid)
        return {};
    if (s.target == browser::DropTarget::newTrack)
    {
        const int x = juce::jmin (gridArea.getRight() - 90, gridArea.getX() + juce::jmax (0, contentWidth() - scrollX) + (numColumns() > 0 ? kGap : 0));
        return { x, headerArea.getY(), gridArea.getRight() - x, gridArea.getBottom() - headerArea.getY() };
    }
    for (int i = 0; i < numColumns(); ++i)
        if (columnTrack[(size_t) i] == s.trackId)
        {
            const int x = gridArea.getX() + columnX[(size_t) i] - scrollX;
            if (s.target == browser::DropTarget::slot)
                return juce::Rectangle<int> (x, gridArea.getY() + s.scene * (kSlotHeight + kGap) - scrollY, columnW[(size_t) i], kSlotHeight).getIntersection (gridArea);
            return { x, headerArea.getY(), columnW[(size_t) i], gridArea.getBottom() - headerArea.getY() };
        }
    return {};
}

void SessionView::setDropSpot (const DropSpot& s)
{
    if (s == dropSpot)
        return;
    dropSpot = s;
    repaint();
}

bool SessionView::isInterestedInDragSource (const SourceDetails& d)
{
    browser::DragItem item;
    return dropActions != nullptr && browser::parseDrag (d.description, item);
}

void SessionView::itemDragMove (const SourceDetails& d)
{
    browser::DragItem item;
    if (dropActions == nullptr || ! browser::parseDrag (d.description, item))
        return;
    auto spot = spotAt (d.localPosition);
    // A track that does not take the item (a chain onto a slot is fine) falls back to a new track.
    if (! dropActions->accepts (item, spot.target, spot.trackId))
        spot.valid = false;
    setDropSpot (spot);
}

void SessionView::itemDropped (const SourceDetails& d)
{
    browser::DragItem item;
    const auto spot = spotAt (d.localPosition);
    setDropSpot ({});
    if (dropActions == nullptr || ! browser::parseDrag (d.description, item))
        return;
    const auto result = dropActions->drop (item, spot.target, spot.trackId, spot.scene);
    if (result.trackId >= 0)
        commands.selectTrack (result.trackId);
    if (onDropped)
        onDropped (result, item);
}

bool SessionView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (dropActions == nullptr)
        return false;
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            return true;
    return files.isEmpty(); // some systems ask before they know the files
}

void SessionView::fileDragMove (const juce::StringArray&, int x, int y)
{
    auto spot = spotAt ({ x, y });
    if (spot.target == browser::DropTarget::track)
        spot.valid = false; // a file needs a slot or a new track, not a whole track
    setDropSpot (spot);
}

void SessionView::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto spot = spotAt ({ x, y });
    setDropSpot ({});
    if (dropActions == nullptr)
        return;
    for (const auto& path : files)
    {
        if (! (path.endsWithIgnoreCase (".mid") || path.endsWithIgnoreCase (".midi")))
            continue;
        const auto target = spot.target == browser::DropTarget::slot ? browser::DropTarget::slot : browser::DropTarget::newTrack;
        const auto result = dropActions->dropMidiFile (juce::File (path), target, spot.trackId, spot.scene);
        if (result.trackId >= 0)
            commands.selectTrack (result.trackId);
        if (onDropped)
        {
            browser::DragItem item;
            item.kind = browser::kinds::midiFile;
            item.path = path;
            item.name = juce::File (path).getFileNameWithoutExtension();
            onDropped (result, item);
        }
        break; // one file per drop: each is one undo step and one new track
    }
}

void SessionView::paintOverChildren (juce::Graphics& g)
{
    if (! dropSpot.valid)
        return;
    const auto r = spotBounds (dropSpot);
    if (r.isEmpty())
        return;
    const auto& t = theme();
    if (dropSpot.target == browser::DropTarget::newTrack)
    {
        juce::Path outline;
        outline.addRoundedRectangle (r.toFloat().reduced (1.0f), t.radius);
        juce::Path dashed;
        const float dashes[] { 6.0f, 4.0f };
        juce::PathStrokeType (1.8f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (t.accent.withAlpha (0.10f));
        g.fillRoundedRectangle (r.toFloat().reduced (1.0f), t.radius);
        g.setColour (t.accent);
        g.fillPath (dashed);
        g.setFont (t.uiFont (12.0f, 500));
        g.drawFittedText (Strings::browser::dropNewTrack, r.reduced (10, 60), juce::Justification::centredTop, 2);
        return;
    }
    g.setColour (t.accent.withAlpha (0.16f));
    g.fillRoundedRectangle (r.toFloat(), t.radius);
    g.setColour (t.accent);
    g.drawRoundedRectangle (r.toFloat().reduced (1.0f), t.radius, 2.0f);
}

juce::String SessionView::helpAt (juce::Point<int> p) const
{
    if (masterHeaderArea.contains (p))
        return Strings::help::masterHeader;
    if (model.numTracks() == 0 && gridArea.contains (p))
        return Strings::help::emptyProject;
    return {};
}

juce::Rectangle<int> SessionView::columnBoundsInView (int index) const
{
    if (index < 0 || index >= numColumns())
        return {};
    return headers->cellBounds (index) + headerArea.getPosition();
}

//==============================================================================
void SessionView::openRenameEditor (juce::Component& host, juce::Rectangle<int> bounds, const juce::String& text,
                                    std::function<void (const juce::String&)> commit)
{
    if (renameEditor != nullptr)
    {
        auto* old = renameEditor.release();
        if (auto* parent = old->getParentComponent())
            parent->removeChildComponent (old);
        juce::MessageManager::callAsync ([old] { delete old; });
    }
    renameEditor = std::make_unique<InlineEditor>();
    auto* editor = renameEditor.get();
    editor->setText (text, false);
    auto finish = [this, editor] {
        if (renameEditor.get() != editor)
            return;
        auto* dying = renameEditor.release();
        if (auto* parent = dying->getParentComponent())
            parent->removeChildComponent (dying);
        juce::MessageManager::callAsync ([dying] { delete dying; });
    };
    editor->onCommit = [commit, finish] (const juce::String& value) {
        commit (value);
        finish();
    };
    editor->onCancel = finish;
    host.addAndMakeVisible (*editor);
    editor->setBounds (bounds.reduced (2));
    editor->grabKeyboardFocus();
    editor->selectAll();
}

void SessionView::startRename()
{
    // Deferred: when it comes from a popup menu, the menu gives focus back to the window as it closes,
    // which would take the focus from the editor that is opened right now.
    juce::Component::SafePointer<SessionView> safe (this);
    juce::Timer::callAfterDelay (60, [safe] {
        if (safe != nullptr)
            safe->startRenameNow();
    });
}

void SessionView::startRenameNow()
{
    const auto& s = selection.get();
    switch (s.kind)
    {
        case Selection::Kind::track:
            for (int i = 0; i < numColumns(); ++i)
                if (columnTrack[(size_t) i] == s.trackId)
                    headers->beginRename (i);
            break;
        case Selection::Kind::clip:
            for (int i = 0; i < numColumns(); ++i)
                if (columnTrack[(size_t) i] == s.trackId)
                    grid->beginRename (i, s.scene);
            break;
        case Selection::Kind::scene: scenes->beginRename (s.scene); break;
        case Selection::Kind::device:
        case Selection::Kind::none: break;
    }
}

//==============================================================================
juce::PopupMenu SessionView::colourSubMenu (int current, std::function<void (int)> pick) const
{
    return makePaletteMenu (current, std::move (pick));
}

std::vector<std::pair<int64_t, int>> SessionView::playingClips() const
{
    std::vector<std::pair<int64_t, int>> playing;
    for (const auto& t : model.tracks())
        if (auto* rt = engine.findRuntime (t.id()))
            playing.emplace_back (t.id(), rt->playingSlot.load());
    return playing;
}

void SessionView::showTrackMenu (juce::Point<int> screenPosition, int64_t trackId)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid())
        return;
    namespace M = Strings::menu;
    juce::PopupMenu menu;
    auto item = [] (int id, const juce::String& text, const juce::String& shortcut = {}) {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.shortcutKeyDescription = shortcut;
        return i;
    };
    menu.addItem (item (1, M::rename, shortcutText ('r')));
    menu.addSubMenu (M::colour, colourSubMenu (track.colour(), [this, trackId] (int index) { actions.setTrackColour (trackId, index); }));
    menu.addItem (item (3, M::duplicate, shortcutText ('d')));
    menu.addItem (item (4, M::remove, juce::KeyPress (juce::KeyPress::deleteKey).getTextDescriptionWithIcons()));
    menu.addSeparator();
    menu.addItem (item (5, M::insertTrackHere, shortcutText ('t')));
    menu.addSeparator();
    menu.addItem (item (6, Strings::browser::saveChain));

    juce::Component::SafePointer<SessionView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe, trackId] (int result) {
                            if (safe == nullptr || result == 0)
                                return;
                            safe->commands.selectTrack (trackId);
                            switch (result)
                            {
                                case 1: safe->startRename(); break;
                                case 3: safe->commands.duplicateSelection(); break;
                                case 4: safe->commands.deleteSelection(); break;
                                case 5: safe->commands.newTrack ((int) trackId); break;
                                case 6:
                                    if (safe->onSaveChain)
                                        safe->onSaveChain (trackId);
                                    break;
                                default: break;
                            }
                        });
}

void SessionView::showClipMenu (juce::Point<int> screenPosition, int64_t trackId, int scene)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid())
        return;
    const auto clip = track.clipAt (scene);
    namespace M = Strings::menu;
    juce::PopupMenu menu;
    auto item = [] (int id, const juce::String& text, bool enabled, const juce::String& shortcut = {}) {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.shortcutKeyDescription = shortcut;
        return i;
    };
    if (clip.isValid())
    {
        menu.addItem (item (10, Strings::menuLaunch, true));
        menu.addSeparator();
        menu.addItem (item (1, M::rename, true, shortcutText ('r')));
        menu.addSubMenu (M::colour, colourSubMenu (clip.colour(), [this, trackId, scene] (int index) { actions.setClipColour (trackId, scene, index); }));
        menu.addItem (item (3, M::duplicate, true, shortcutText ('d')));
        menu.addItem (item (4, M::remove, true, juce::KeyPress (juce::KeyPress::deleteKey).getTextDescriptionWithIcons()));
        menu.addSeparator();
        menu.addItem (item (6, M::copy, true, shortcutText ('c')));
        menu.addItem (item (7, M::cut, true, shortcutText ('x')));
        menu.addItem (item (8, M::paste, actions.hasClipboard(), shortcutText ('v')));
    }
    else
    {
        menu.addItem (item (11, M::createClipHere, true));
        menu.addItem (item (8, M::paste, actions.hasClipboard(), shortcutText ('v')));
    }

    juce::Component::SafePointer<SessionView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe, trackId, scene] (int result) {
                            if (safe == nullptr || result == 0 || result == 9999)
                                return;
                            safe->commands.selectSlot (trackId, scene);
                            switch (result)
                            {
                                case 1: safe->startRename(); break;
                                case 3: safe->commands.duplicateSelection(); break;
                                case 4: safe->commands.deleteSelection(); break;
                                case 6: safe->commands.copySelection(); break;
                                case 7: safe->commands.cutSelection(); break;
                                case 8: safe->commands.pasteSelection(); break;
                                case 10: safe->engine.launchClip (trackId, scene); break;
                                case 11: safe->actions.createEmptyClip (trackId, scene); break;
                                default: break;
                            }
                        });
}

void SessionView::showSceneMenu (juce::Point<int> screenPosition, int scene)
{
    if (scene < 0 || scene >= model.numScenes())
        return;
    namespace M = Strings::menu;
    juce::PopupMenu menu;
    auto item = [] (int id, const juce::String& text, bool enabled, const juce::String& shortcut = {}) {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.shortcutKeyDescription = shortcut;
        return i;
    };
    menu.addItem (item (10, Strings::menuLaunch, true));
    menu.addSeparator();
    menu.addItem (item (1, M::rename, true, shortcutText ('r')));
    menu.addSubMenu (M::colour, colourSubMenu (model.sceneAt (scene).colour(), [this, scene] (int index) { actions.setSceneColour (scene, index); }));
    menu.addItem (item (3, M::duplicate, true, shortcutText ('d')));
    menu.addItem (item (4, M::remove, model.numScenes() > 1, juce::KeyPress (juce::KeyPress::deleteKey).getTextDescriptionWithIcons()));
    menu.addSeparator();
    menu.addItem (item (5, M::newScene, true, shortcutText ('i')));
    menu.addItem (item (6, M::captureScene, true, shortcutText ('i', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)));

    juce::Component::SafePointer<SessionView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe, scene] (int result) {
                            if (safe == nullptr || result == 0 || result == 9999)
                                return;
                            safe->selection.selectScene (scene);
                            switch (result)
                            {
                                case 1: safe->startRename(); break;
                                case 3: safe->commands.duplicateSelection(); break;
                                case 4: safe->commands.deleteSelection(); break;
                                case 5: safe->commands.newScene(); break;
                                case 6: safe->commands.captureScene(); break;
                                case 10: safe->engine.launchScene (scene); break;
                                default: break;
                            }
                        });
}
} // namespace ui
