#include "SessionView.h"
#include "Kit.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../engine/SessionRecorder.h"
#include "../engine/BuiltInEffectProcessor.h"
#include "../Settings.h"
#include "../project/AudioImport.h"
#include "ArrangementView.h"
#include "PaletteMenu.h"
#include "Waveform.h"

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
        if (t.glass)
        {
            // glass, from the kit: the number on a glass button (dim and struck through when muted), solo in the
            // second accent, arm in red
            kit::State st;
            st.on = on && role != Role::activator;
            st.hover = isMouseOver();
            const auto ink = kit::button (g, r, role == Role::arm ? kit::Kind::danger : kit::Kind::toggle, st,
                                          role == Role::solo ? std::optional<juce::Colour> (t.accentFill2) : std::nullopt);
            g.setColour (role == Role::activator && ! on ? t.dimText : ink);
            if (role == Role::arm)
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (r.getCentre()));
            else
            {
                g.setFont (t.uiFont (t.metrics.small, 600));
                g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
                if (role == Role::activator && ! on)
                    g.fillRect (r.getCentreX() - 7.0f, r.getCentreY(), 14.0f, 1.2f);
            }
            return;
        }
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
            if (t.glass)
                glass::accent (g, b.toFloat (), t.palette (track.colour ()), t.radius);
            else
                fillCell (g, b.toFloat (), t.palette (track.colour ()));
            if (track.id() == selectedTrack)
                strokeCell (g, b.toFloat(), t.text, 2.0f);
            g.setColour (t.glass ? t.inkOn (t.palette (track.colour ())) : t.clipInk);
            g.setFont (t.uiFont (12.5f, 600));
            auto textArea = b.reduced (9, 0);
            if (track.isAudio())
            {
                // A small waveform at the right edge tells an audio track from a MIDI one (MIDI tracks have no mark).
                const auto glyph = juce::Rectangle<float> (16.0f, 12.0f).withCentre ({ (float) b.getRight() - 17.0f, (float) b.getCentreY() });
                static constexpr float heights[] { 0.35f, 0.8f, 0.55f, 1.0f, 0.45f };
                g.setColour (t.clipInk.withAlpha (0.75f));
                for (int k = 0; k < 5; ++k)
                {
                    const float h = glyph.getHeight() * heights[k];
                    g.fillRoundedRectangle (glyph.getX() + (float) k * 3.4f, glyph.getCentreY() - h * 0.5f, 2.0f, h, 1.0f);
                }
                textArea.removeFromRight (24);
                g.setColour (t.glass ? t.inkOn (t.palette (track.colour ())) : t.clipInk);
            }
            if (t.glass)
            {
                // the track number before the name
                const auto ink = t.inkOn (t.palette (track.colour ()));
                const auto number = juce::String (view.model.indexOfTrack (track.id()) + 1);
                g.setColour (ink.withAlpha (0.7f));
                g.setFont (t.uiFont (12.0f, 500));
                g.drawText (number, textArea.removeFromLeft (18), juce::Justification::centredLeft, false);
                g.setColour (ink);
                g.setFont (t.uiFont (12.5f, 600));
            }
            g.drawText (track.name(), textArea, juce::Justification::centredLeft, true);
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
        if (track.isAudio())
            return overCircle && recordsSound (track) ? Strings::help::recordAudioSlot : Strings::help::emptyAudioSlot;
        return overCircle && canRecord (track, anyArmed()) ? Strings::help::recordSlot : Strings::help::emptySlot;
    }

    // A track that records sound (an audio track with an input, a MIDI track with the record source "audio"): its record
    // circle works whether or not it is armed.
    bool recordsSound (const TrackRef& track) const
    {
        return view.recorder != nullptr && view.recorder->sound().recordsSound (track);
    }

    bool canRecord (const TrackRef& track, bool anyTrackArmed) const
    {
        return recordsSound (track) || receivesLiveMidi (track, anyTrackArmed);
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

        if (peaksPending) // a sound that was still being decoded when the grid was painted
        {
            peaksPending = false;
            repaint();
        }

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
        // the glass mixer shows this grid blurred: it follows every repaint of the grid
        if (view.underGlass > 0 && ! glass::capturing())
            view.repaintGlassOver (false);
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
            if (! v.track.isValid() || v.track.isReturn())
                continue; // a return track has no clips, so its column has no slots
            auto* rt = view.engine.findRuntime (v.track.id());
            const int playingScene = rt != nullptr ? rt->playingSlot.load() : -1;
            const int queuedScene = rt != nullptr ? rt->queuedSlot.load() : -1;
            const bool armed = v.track.arm();
            const bool live = canRecord (v.track, anyTrackArmed);
            const int recScene = view.recorder != nullptr ? view.recorder->recordingScene (v.track.id()) : -1;
            SessionRecorder::Progress progress;
            const bool hasProgress = recScene >= 0 && view.recorder->getProgress (v.track.id(), progress);
            // empty slots carry a trace of the track colour, so neighbouring columns differ
            const auto emptyFill = t.glass ? t.panel2.interpolatedWith (t.panel, 0.25f)
                                           : t.panel.interpolatedWith (t.palette (v.track.colour()), 0.08f);

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
                    if (t.glass)
                    {
                        // Glass, as the design: a quiet grey cell; a faint plus in the middle (double-click makes a
                        // clip), or the red record ring when the track records.
                        fillCell (g, cell, dropTarget ? t.accentFill.withAlpha (0.2f).overlaidWith (emptyFill) : emptyFill);
                        if (dropTarget || selected)
                            strokeCell (g, cell, t.accent, 1.5f);
                        const auto c = cell.getCentre();
                        if (live)
                        {
                            const auto ring = juce::Rectangle<float> (14.0f, 14.0f).withCentre (c);
                            g.setColour (t.record.withAlpha (armed ? 1.0f : 0.55f));
                            g.drawEllipse (ring, 1.4f);
                            if (armed)
                                g.fillEllipse (ring.reduced (3.5f));
                        }
                        else
                        {
                            g.setColour (t.dimText.withAlpha (0.45f));
                            g.fillRect (c.x - 5.0f, c.y - 0.6f, 10.0f, 1.2f);
                            g.fillRect (c.x - 0.6f, c.y - 5.0f, 1.2f, 10.0f);
                        }
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
                // A sound shows its waveform; one whose file is gone is greyed out and says so.
                std::shared_ptr<daw::engine::AudioSource> audioSource;
                const bool audioClip = clip.isAudio();
                if (audioClip)
                    audioSource = view.engine.peekAudioSource (clip.fileId());
                const bool missing = audioClip && audioSource == nullptr;
                // Glass: a pastel cell; notes and waveform in the deeper track colour, label in a contrasting ink.
                const auto glassFill = t.panel.interpolatedWith (colour, isPlaying ? 0.62f : (t.isLight() ? 0.40f : 0.34f));
                const auto deep = t.isLight() ? colour.interpolatedWith (t.text, 0.22f) : colour.interpolatedWith (t.highlight(), 0.15f);
                const auto ink = missing ? t.dimText : (t.glass ? t.inkOn (glassFill) : t.clipInk);
                if (t.glass && !missing)
                    fillCell (g, cell, glassFill);
                else
                    fillCell (g,
                              cell,
                              missing ? t.panel2.interpolatedWith (t.dimText, 0.16f)
                                      : t.clipColour (clip.colour (), isPlaying));
                if (audioSource != nullptr)
                {
                    const auto area = cell.withTrimmedLeft (26.0f).withTrimmedRight (4.0f).withTrimmedTop (15.0f).withTrimmedBottom (3.0f); // under the name
                    juce::Graphics::ScopedSaveState save (g);
                    g.reduceClipRegion (cell.toNearestInt());
                    drawAudioWaveform (g,
                                       area,
                                       *audioSource,
                                       clip,
                                       0.0,
                                       false,
                                       clip.lengthBeats (),
                                       clip.contentTempo (view.model.tempo ()),
                                       t.glass ? deep.withAlpha (0.9f) : ink.withAlpha (0.7f));
                    if (audioSource->peaksAvailable.load (std::memory_order_acquire) < audioSource->numPeaks)
                        peaksPending = true; // still decoding: repaint on the next tick to show more
                }
                if (dropTarget || selected)
                    strokeCell (g, cell, dropTarget ? t.accent : t.text, 2.0f);
                if (recScene == s) // an overdub runs on this clip: red outline, pulsing
                    strokeCell (g, cell, t.record.withAlpha (animate ? pulse (now) : 1.0f), 2.0f);

                // play marker
                const auto icon = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ cell.getX() + 12.5f, cell.getCentreY() });
                float iconAlpha = isPlaying ? 1.0f : 0.55f;
                if (isPlaying && blink2 && animate)
                    iconAlpha = 0.15f;
                if (isQueued && blink4)
                    iconAlpha = 0.15f;
                g.setColour (ink.withAlpha (iconAlpha));
                g.fillPath (icons::play (icon));

                g.setColour (missing ? t.text : ink);
                g.setFont (t.uiFont (12.0f, 500));
                if (missing)
                {
                    auto text = b.withTrimmedLeft (26).withTrimmedRight (6);
                    g.drawText (clip.name(), text.removeFromTop (kSlotHeight / 2 + 1), juce::Justification::bottomLeft, true);
                    g.setColour (t.warning);
                    g.setFont (t.uiFont (10.5f, 500));
                    g.drawText (Strings::sessionAudio::fileNotFound, text, juce::Justification::topLeft, true);
                }
                else if (audioSource != nullptr)
                {
                    g.setFont (t.uiFont (11.0f, 500));
                    g.drawText (clip.name(), b.withTrimmedLeft (26).withTrimmedRight (6).withHeight (16), juce::Justification::centredLeft, true);
                }
                else if (t.glass && cell.getHeight() >= 32.0f)
                {
                    // the name on top, a miniature of the notes below it
                    g.setFont (t.uiFont (11.5f, 500));
                    g.drawText (clip.name(), b.withTrimmedLeft (26).withTrimmedRight (6).withHeight (18).translated (0, 1), juce::Justification::centredLeft, true);
                    paintNotePreview (g, clip, cell.withTrimmedLeft (26.0f).withTrimmedRight (7.0f).withTrimmedTop (19.0f).withTrimmedBottom (5.0f), deep);
                }
                else
                    g.drawText (clip.name(), b.withTrimmedLeft (26).withTrimmedRight (6), juce::Justification::centredLeft, true);

                if (isPlaying)
                {
                    const double pos = view.engine.isPlaying()
                                           ? clipProgress (clip, juce::jmax (0.0, view.engine.getPositionBeats() - rt->playingStartBeat.load()))
                                           : 0.0;
                    g.setColour (t.glass ? deep : t.clipInk.withAlpha (0.6f));
                    if (t.glass) // the progress runs along the bottom inside the rounded cell
                        g.fillRoundedRectangle (cell.getX() + 6.0f, cell.getBottom() - 4.0f, (cell.getWidth() - 12.0f) * (float) pos, 2.0f, 1.0f);
                    else
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
        if (view.model.findTrack (id).isReturn())
        {
            view.commands.selectTrack (id);
            return;
        }
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
        // A clip carried out of the grid (up to the transport) goes on as a drag-and-drop: dropped on the "Arrangement"
        // tab it switches the view, and the timeline takes the drop.
        if (! getLocalBounds().contains (e.getPosition()))
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            {
                const auto id = view.columnTrack[(size_t) pressColumn];
                juce::Image ghost (juce::Image::ARGB, 150, 28, true);
                {
                    const auto& t = theme();
                    juce::Graphics g (ghost);
                    g.setColour (t.clipColour (track.clipAt (pressScene).colour(), true).withAlpha (0.92f));
                    g.fillRoundedRectangle (0.0f, 0.0f, 150.0f, 28.0f, t.radius);
                    g.setColour (t.clipInk);
                    g.setFont (t.uiFont (12.0f, 500));
                    g.drawText (track.clipAt (pressScene).name(), 10, 0, 130, 28, juce::Justification::centredLeft, true);
                }
                const int scene = pressScene;
                dragging = false;
                dragColumn = dragScene = dropColumn = dropScene = pressColumn = pressScene = -1;
                setMouseCursor (juce::MouseCursor::NormalCursor);
                repaint();
                container->startDragging (ArrangementView::sessionClipDescription (id, scene), this, juce::ScaledImage (ghost), true);
                return;
            }
        dragging = true;
        dragColumn = pressColumn;
        dragScene = pressScene;
        dragPosition = e.getPosition();
        int column = -1, scene = -1;
        if (hit (e.getPosition(), column, scene) && scene != stopRow && ! view.model.findTrack (view.columnTrack[(size_t) column]).isReturn())
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
        else if (atMarker && view.recorder != nullptr && canRecord (view.model.findTrack (id), anyArmed()))
            view.recorder->toggleRecording (id, scene); // the record circle of an empty slot
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        int column = -1, scene = -1;
        if (e.mods.isPopupMenu() || ! hit (e.getPosition(), column, scene) || scene == stopRow)
            return;
        const auto id = view.columnTrack[(size_t) column];
        if (view.model.findTrack (id).isReturn())
            return;
        if (view.model.findTrack (id).clipAt (scene).isValid())
        {
            // double-click on a clip: edit it
            view.commands.selectSlot (id, scene);
            if (view.onOpenClip)
                view.onOpenClip (id, scene);
            return;
        }
        if (view.model.findTrack (id).isAudio())
            return; // sounds come by drag and drop (and later by recording), not as empty clips
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
        const auto area = cell.withTrimmedLeft (26.0f).withTrimmedRight (6.0f);
        g.setColour (t.clipInk.withAlpha (0.7f));
        g.fillRect (cell.getX(), cell.getBottom() - 3.0f, cell.getWidth() * (float) juce::jlimit (0.0, 1.0, p.lengthBeats / total), 3.0f);

        // Sound: the waveform as it comes in, from the writer's overview (the file is not read while it grows).
        if (recordsSound (track) && area.getWidth() > 8.0f)
        {
            std::vector<float> peaks;
            int64_t frames = 0;
            double rate = 48000.0;
            if (view.recorder->sound().getLivePeaks (track.id(), peaks, frames, rate) && ! peaks.empty())
            {
                const double framesPerBeat = rate * 60.0 / juce::jmax (1.0, view.model.tempo());
                const double totalFrames = total * framesPerBeat;
                const float mid = cell.getCentreY() + 2.0f, half = cell.getHeight() * 0.5f - 6.0f;
                g.setColour (t.clipInk.withAlpha (0.8f));
                const int numPairs = (int) peaks.size() / 2;
                for (float x = area.getX(); x < area.getRight(); x += 1.0f)
                {
                    const double f0 = (x - area.getX()) / area.getWidth() * totalFrames, f1 = (x + 1.0f - area.getX()) / area.getWidth() * totalFrames;
                    const int p0 = (int) (f0 / AudioRecorder::kPeakFrames), p1 = juce::jmax (p0 + 1, (int) (f1 / AudioRecorder::kPeakFrames));
                    if (p0 >= numPairs)
                        break;
                    float lo = 0.0f, hi = 0.0f;
                    for (int i = p0; i < juce::jmin (p1, numPairs); ++i)
                    {
                        lo = juce::jmin (lo, peaks[(size_t) (2 * i)]);
                        hi = juce::jmax (hi, peaks[(size_t) (2 * i + 1)]);
                    }
                    g.fillRect (x, mid - hi * half, 1.0f, juce::jmax (1.0f, (hi - lo) * half));
                }
            }
            return;
        }
        if (! p.notes.empty() && area.getWidth() > 8.0f)
        {
            int lo = 127, hi = 0;
            for (const auto& n : p.notes)
            {
                lo = juce::jmin (lo, n.pitch);
                hi = juce::jmax (hi, n.pitch);
            }
            const float top = cell.getY() + 5.0f, height = cell.getHeight() - 12.0f;
            const float span = (float) juce::jmax (7, hi - lo);
            g.setColour (t.clipInk.withAlpha (0.85f));
            for (const auto& n : p.notes)
            {
                const float x = area.getX() + (float) (n.start / total) * area.getWidth();
                const float w = juce::jmax (1.5f, (float) (n.length / total) * area.getWidth());
                const float y = top + height - (float) (n.pitch - lo) / span * height;
                g.fillRect (x, y - 1.0f, juce::jmin (w, area.getRight() - x), 2.0f);
            }
        }
    }

    // Glass: a miniature of a MIDI clip's notes, one short bar per note across the clip's length.
    static void paintNotePreview (juce::Graphics& g, const ClipRef& clip, juce::Rectangle<float> area, juce::Colour colour)
    {
        if (area.getWidth() < 8.0f || area.getHeight() < 4.0f)
            return;
        const auto midi = clip.toMidiClip();
        const double total = juce::jmax (0.25, midi.lengthBeats);
        if (midi.notes.empty())
            return;
        int lo = 127, hi = 0;
        for (const auto& n : midi.notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
        const float span = (float) juce::jmax (6, hi - lo);
        const float barH = juce::jlimit (1.5f, 2.5f, area.getHeight() / (span + 1.0f) + 0.5f);
        g.setColour (colour.withAlpha (0.9f));
        for (const auto& n : midi.notes)
        {
            if (n.start >= total)
                continue;
            const float x = area.getX() + (float) (n.start / total) * area.getWidth();
            const float w = juce::jmax (2.0f, (float) (n.length / total) * area.getWidth() - 1.0f);
            const float y = area.getBottom() - barH - (float) (n.pitch - lo) / span * (area.getHeight() - barH);
            g.fillRoundedRectangle (x, y, juce::jmin (w, area.getRight() - x), barH, barH * 0.5f);
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
    bool peaksPending = false;
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
        if (view.underGlass > 0 && ! glass::capturing())
            view.repaintGlassOver (true);
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
            if (t.glass)
            {
                // glass: a light list row with a hairline under it; a selected scene is a tinted row
                if (selected || s == hoverScene)
                {
                    g.setColour (selected ? t.accentFill.withAlpha (t.isLight() ? 0.2f : 0.26f) : t.panel2.withAlpha (0.6f));
                    g.fillRoundedRectangle (b.toFloat().reduced (0.0f, 1.0f), 8.0f);
                }
                g.setColour (t.line.withAlpha (0.5f));
                g.fillRect ((float) b.getX() + 8.0f, (float) b.getBottom() - 0.5f, (float) b.getWidth() - 16.0f, 1.0f);
            }
            else
            {
                fillCell (g, b.toFloat(), t.panel2);
                strokeCell (g, b.toFloat(), selected ? t.accent : t.line, selected ? 1.5f : 1.0f);
            }
            const bool hot = s == hoverScene && hoverLaunch;
            g.setColour (hot ? t.play : (scene.colour() >= 0 ? t.palette (scene.colour()) : t.dimText));
            g.fillPath (icons::play (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ (float) b.getX() + 13.0f, (float) b.getCentreY() })));
            g.setColour (t.text);
            g.setFont (t.uiFont (t.glass ? 13.0f : 12.0f));
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

        for (int i = 0; i < 2; ++i)
        {
            auto* k = i == 0 ? &sendA : &sendB;
            k->setHelpText (Strings::help::sendKnob);
            k->valueText = [] (double knob) { return sendText (knob); };
            k->setDefaultValue (0.0);
            k->onGestureStart = [this, i] {
                view.model.undo.beginNewTransaction (Strings::undoSend);
                ensureReturn (view.model, i); // the first touch of A or B makes its return track
            };
            k->onValueChange = [this, i, k] (double knob) {
                if (auto track = view.model.findTrack (trackId); track.isValid())
                    track.setSendDb (i, sendDbOf (knob), &view.model.undo);
                juce::ignoreUnused (k);
            };
            k->setEnabled (! t.isReturn());
            addAndMakeVisible (*k);
        }
        sendA.setCaption (Strings::labelSendA);
        sendB.setCaption (Strings::labelSendB);

        pan.setCaption (Strings::labelPan);
        pan.setBipolar (true);
        pan.setDefaultValue (0.5);
        pan.setHelpText (Strings::help::panKnob);
        pan.setWheelChangesValue (false);
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
        if (force || ! sendA.isDragging())
            sendA.setValue (sendKnobOf (track.sendDb (0)));
        if (force || ! sendB.isDragging())
            sendB.setValue (sendKnobOf (track.sendDb (1)));
    }

    // Send knob: 0 = off, the rest -60..+6 dB with the middle of the travel at -12 dB.
    static float sendDbOf (double v) { return v <= 0.001 ? TrackRef::kMinSendDb : (float) (TrackRef::kMinSendDb + std::pow (v, 0.5) * 66.0); }
    static double sendKnobOf (float db) { return db <= TrackRef::kMinSendDb ? 0.0 : std::pow ((db - TrackRef::kMinSendDb) / 66.0, 2.0); }
    static juce::String sendText (double v)
    {
        const float db = sendDbOf (v);
        return db <= TrackRef::kMinSendDb ? juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e") : juce::String (db, 1) + " dB";
    }

    // Return A or B, made on demand: A with a reverb, B with a delay, both fully wet.
    static void ensureReturn (ProjectModel& model, int which)
    {
        int count = 0;
        for (const auto& t : model.tracks())
            count += t.isReturn() ? 1 : 0;
        while (count <= which)
        {
            const bool reverb = count == 0;
            auto ret = model.addTrack ("return");
            ret.setName (reverb ? Strings::returns::reverbName : Strings::returns::delayName, &model.undo);
            auto fx = model.addDevice (ret, "fx", Strings::fx::effectName (reverb ? "reverb" : "delay"));
            fx.setPluginDesc (reverb ? "reverb" : "delay", &model.undo);
            // fully wet: the dry sound is in the track already
            fx.setStateBase64 (BuiltInEffectProcessor::stateBase64 (reverb ? "reverb" : "delay", { { "mix", 100.0f } }), &model.undo);
            ++count;
        }
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
        if (view.underGlass > 0)
        {
            // a calm glass card over the clip grid (the blurred grid showing through moved under the mouse and read
            // as stray shadows)
            glass::surface (g, r, t.panel, t.metrics.panelRadius);
            g.setColour (t.line.withAlpha (t.isLight() ? 0.5f : 0.6f));
            g.drawRoundedRectangle (r.reduced (0.5f), t.metrics.panelRadius, 1.0f);
            return;
        }
        fillCell (g, r, t.panel);
        strokeCell (g, r, t.line);
    }

    void lookAndFeelChanged() override { resized(); } // the glass themes lay the strip out differently

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

        if (theme().glass && getWidth() >= 96)
        {
            // Glass: the sends and the pan as larger glass knobs on top, the meter and the fader with its scale below.
            sendA.setVisible (true);
            sendB.setVisible (true);
            auto sendsRow = r.removeFromTop (50);
            const int cellW = sendsRow.getWidth() / 3;
            sendA.setBounds (sendsRow.removeFromLeft (cellW));
            sendB.setBounds (sendsRow.removeFromLeft (cellW));
            pan.setBounds (sendsRow);
            r.removeFromTop (4);
            dbField.setBounds (r.removeFromBottom (16).withTrimmedLeft (r.getWidth() / 3));
            meter.setBounds (r.getX(), r.getY(), 14, r.getHeight());
            fader.setBounds (r.getX() + 20, r.getY(), juce::jmax (24, juce::jmin (64, r.getWidth() - 20)), r.getHeight());
            return;
        }
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
        // the glass fader draws a dB scale in the room on its right
        fader.setBounds (r.getX() + 20, r.getY(), juce::jmax (24, juce::jmin (64, r.getWidth() - 20)), r.getHeight());
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

    void mouseDown (const juce::MouseEvent&) override { view.commands.selectTrack (ProjectModel::kMasterTrackId); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = getLocalBounds().toFloat();
        if (view.underGlass > 0)
        {
            // a calm glass card over the clip grid (the blurred grid showing through moved under the mouse and read
            // as stray shadows)
            glass::surface (g, r, t.panel, t.metrics.panelRadius);
            g.setColour (t.line.withAlpha (t.isLight() ? 0.5f : 0.6f));
            g.drawRoundedRectangle (r.reduced (0.5f), t.metrics.panelRadius, 1.0f);
            return;
        }
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
        // the glass fader draws a dB scale in the room on its right
        fader.setBounds (r.getX() + 20, r.getY(), juce::jmax (24, juce::jmin (64, r.getWidth() - 20)), r.getHeight());
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
    repaint (masterHeaderArea);
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

void SessionView::repaintGlassOver (bool scenesColumn)
{
    if (scenesColumn)
        masterStrip->repaint();
    else
        mixer->repaint();
}

int SessionView::contentHeight() const
{
    // under a glass mixer the last row can be scrolled above it
    return model.numScenes() * kStep + kStopHeight + underGlass;
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

void SessionView::mouseDown (const juce::MouseEvent& e)
{
    // the master header selects the master: its chain opens in the bottom panel
    if (masterHeaderArea.contains (e.getPosition()))
        commands.selectTrack (ProjectModel::kMasterTrackId);
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
    // Glass: the grid and the scenes go on under the mixer, which is glass over them.
    underGlass = theme().glass && mixerVisible ? mixerHeight + kMixerGap : 0;
    gridArea = { left, gridTop, leftWidth, juce::jmax (0, gridBottom - gridTop + underGlass) };
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
    const int visibleBottom = gridArea.getBottom() - underGlass; // where the grid shows without glass over it
    hbar.setBounds (gridArea.getX(), visibleBottom - bar, gridArea.getWidth() - bar, bar);
    vbar.setBounds (gridArea.getRight() - bar, gridArea.getY(), bar, visibleBottom - gridArea.getY());

    mixerTab.setBounds (juce::Rectangle<int> (30, 11).withCentre ({ sceneArea.getCentreX(), visibleBottom + (mixerVisible ? kMixerGap / 2 : 6) }));
    mixerTab.setOpen (mixerVisible);

    mixer->toFront (false);
    masterStrip->toFront (false);
    mixerTab.toFront (false);
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
    if (model.selectedTrack() == ProjectModel::kMasterTrackId)
        strokeCell (g, masterHeaderArea.toFloat(), t.text, 2.0f);
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
    auto result = dropActions->drop (item, spot.target, spot.trackId, spot.scene);
    if (! result.ok && result.message.isEmpty() && item.kind == browser::kinds::audioFile)
        result.message = Strings::errorAudioOnMidi; // a sound let go over a MIDI track
    if (result.trackId >= 0)
        commands.selectTrack (result.trackId);
    if (onDropped)
        onDropped (result, item);
}

namespace
{
// What a Finder drag carries that the Session View can use: sound files, and the first MIDI file.
struct DraggedFiles
{
    juce::Array<juce::File> sounds;
    juce::File midi;
    bool unknown = false; // some systems ask before they know the files
};

DraggedFiles classifyFiles (const juce::StringArray& paths)
{
    DraggedFiles out;
    out.unknown = paths.isEmpty();
    for (const auto& path : paths)
    {
        const juce::File f (path);
        if (audioimport::isSoundFile (f))
            out.sounds.add (f);
        else if (out.midi == juce::File() && (path.endsWithIgnoreCase (".mid") || path.endsWithIgnoreCase (".midi")))
            out.midi = f;
    }
    return out;
}

browser::DragItem fileItem (const juce::File& f, const juce::String& kind)
{
    browser::DragItem item;
    item.kind = kind;
    item.path = f.getFullPathName();
    item.name = f.getFileNameWithoutExtension();
    return item;
}
} // namespace

bool SessionView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (dropActions == nullptr)
        return false;
    const auto dragged = classifyFiles (files);
    return dragged.unknown || ! dragged.sounds.isEmpty() || dragged.midi != juce::File();
}

void SessionView::fileDragMove (const juce::StringArray& files, int x, int y)
{
    auto spot = spotAt ({ x, y });
    const auto dragged = classifyFiles (files);
    bool ok = false;
    if (dragged.unknown)
        ok = spot.target != browser::DropTarget::track; // no files to ask about yet: the old rule
    else if (dropActions != nullptr)
    {
        if (! dragged.sounds.isEmpty())
            ok = dropActions->accepts (fileItem (dragged.sounds[0], browser::kinds::audioFile), spot.target, spot.trackId);
        // A MIDI file needs a slot or a new track, not a whole track.
        if (! ok && dragged.midi != juce::File() && spot.target != browser::DropTarget::track)
            ok = dropActions->accepts (fileItem (dragged.midi, browser::kinds::midiFile), spot.target, spot.trackId);
    }
    spot.valid = ok;
    setDropSpot (spot);
}

void SessionView::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto spot = spotAt ({ x, y });
    setDropSpot ({});
    if (dropActions == nullptr)
        return;
    const auto dragged = classifyFiles (files);

    if (! dragged.sounds.isEmpty())
    {
        const auto item = fileItem (dragged.sounds[0], browser::kinds::audioFile);
        if (dropActions->accepts (item, spot.target, spot.trackId))
        {
            // Cmd: every file on a track of its own; otherwise they go one under another on one track.
            const bool alternate = juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown();
            const auto result = dropActions->dropAudioFiles (dragged.sounds, spot.target, spot.trackId,
                                                             spot.target == browser::DropTarget::slot ? spot.scene : -1, alternate);
            if (result.trackId >= 0)
                commands.selectTrack (result.trackId);
            if (onDropped)
                onDropped (result, item);
            return; // one drop is one undo step: a MIDI file in the same drag is left alone
        }
        if (dragged.midi == juce::File())
        {
            browser::DropResult refused;
            refused.message = Strings::errorAudioOnMidi;
            if (onDropped)
                onDropped (refused, item);
            return;
        }
    }

    if (dragged.midi != juce::File())
    {
        const auto target = spot.target == browser::DropTarget::slot ? browser::DropTarget::slot : browser::DropTarget::newTrack;
        const auto item = fileItem (dragged.midi, browser::kinds::midiFile);
        if (! dropActions->accepts (item, target, spot.trackId))
            return;
        const auto result = dropActions->dropMidiFile (dragged.midi, target, spot.trackId, spot.scene);
        if (result.trackId >= 0)
            commands.selectTrack (result.trackId);
        if (onDropped)
            onDropped (result, item);
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
        case Selection::Kind::aclip:
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
    menu.addItem (item (7, M::insertAudioTrackHere, shortcutText ('t', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier)));
    menu.addSeparator();
    if (! track.isAudio()) // a chain begins with an instrument
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
                                case 7: safe->commands.newAudioTrack ((int) trackId); break;
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
        menu.addItem (item (11, M::createClipHere, ! track.isAudio())); // an empty MIDI clip makes no sense on an audio track
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
