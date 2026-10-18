#include "AudioClipEditor.h"

#include "../model/ArrangementEdit.h"
#include "../model/ClipEdit.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "PaletteMenu.h"
#include "PianoRollParts.h"
#include "Waveform.h"

#include <algorithm>
#include <cmath>

namespace ui
{
namespace
{
constexpr int kRow = 22, kGap = 4, kMarginProps = 8;
constexpr int kBraceHeight = 11, kGrab = 5;
constexpr double kSteps[] { 1.0, 0.5, 0.25, 0.125 };
} // namespace

//==============================================================================
// The waveform with the ruler on top
//==============================================================================
class AudioClipEditor::Wave final : public juce::Component
{
public:
    enum class Part
    {
        none,
        startMarker,
        braceStart,
        braceEnd,
        warpMarker, // `hitMarker` is its index
        autoMarker  // a beat of the sound, `hitBeat`; becomes a marker when dragged
    };

    explicit Wave (AudioClipEditor& o) : owner (o)
    {
        setHelpText (Strings::help::audioWave);
        setWantsKeyboardFocus (true);
    }

    static constexpr int kTop = kRulerHeight + kWarpLane;
    juce::Rectangle<int> braceArea() const { return { 0, kRulerHeight - kBraceHeight, getWidth(), kBraceHeight }; }
    juce::Rectangle<int> laneArea() const { return { 0, kRulerHeight, getWidth(), kWarpLane }; }
    juce::Rectangle<int> bodyArea() const { return { 0, kTop, getWidth(), juce::jmax (0, getHeight() - kTop) }; }

    bool warping() const
    {
        const auto c = owner.clip();
        return c.isValid() && c.followTempo();
    }

    Part partAt (juce::Point<int> p)
    {
        const auto c = owner.clip();
        if (! c.isValid())
            return Part::none;
        if (p.y >= kRulerHeight)
        {
            if (! warping())
                return Part::none;
            // markers first, then the beats of the sound
            const auto markers = c.warpMarkers();
            int best = -1;
            double bestDist = kGrab + 1.0;
            for (size_t i = 0; i < markers.size(); ++i)
                if (const double d = std::abs (owner.beatToX (markers[i].beat) - p.x); d < bestDist)
                {
                    best = (int) i;
                    bestDist = d;
                }
            if (best >= 0)
            {
                hitMarker = best;
                return Part::warpMarker;
            }
            if (owner.beatToX (1.0) - owner.beatToX (0.0) >= 8.0)
            {
                const double b = std::round (owner.xToBeat (p.x));
                if (std::abs (owner.beatToX (b) - p.x) <= kGrab)
                {
                    hitBeat = b;
                    return Part::autoMarker;
                }
            }
            return Part::none;
        }
        if (braceArea().contains (p))
        {
            if (std::abs (p.x - owner.beatToX (c.lengthBeats())) <= kGrab)
                return Part::braceEnd;
            if (std::abs (p.x - owner.beatToX (c.loopStart())) <= kGrab)
                return Part::braceStart;
            return Part::none;
        }
        return std::abs (p.x - owner.beatToX (c.startOffset())) <= 7 ? Part::startMarker : Part::none;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.fillAll (t.background);
        const auto c = owner.clip();
        if (! c.isValid())
            return;
        paintBody (g, c);
        paintLane (g, c);
        paintRuler (g, c);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto over = partAt (e.getPosition());
        setMouseCursor (over == Part::none ? juce::MouseCursor::NormalCursor
                                           : (over == Part::startMarker ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::LeftRightResizeCursor));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        changed = false;
        downX = e.x;
        part = partAt (e.getPosition());
        const auto c = owner.clip();
        if (! c.isValid())
            return;
        if (e.mods.isPopupMenu())
        {
            const auto clicked = part;
            part = Part::none;
            showMenu (e, clicked);
            return;
        }
        if (part == Part::none)
            return;
        origStart = c.loopStart();
        origEnd = c.lengthBeats();
        owner.frozenView = owner.viewBeats();
        moveAudio = e.mods.isCommandDown() || e.mods.isCtrlDown();
        gestureMap = owner.warpMap();
        if (part == Part::warpMarker)
            owner.selectMarker (hitMarker);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto c = owner.clip();
        if (! c.isValid() || part == Part::none)
            return;
        // Shift, Alt and Cmd all switch the snapping off for the gesture
        const bool bypass = e.mods.isShiftDown() || e.mods.isAltDown() || e.mods.isCommandDown();
        const double beat = owner.snapBeat (owner.xToBeat (e.x), bypass);
        const double minBrace = owner.v.snap && ! bypass ? owner.v.step : kMinBraceBeats;
        auto& um = owner.model.undo;
        // One transaction for the whole drag, opened by the first change so that a click that moves nothing leaves no step.
        const auto open = [&] (const juce::String& name) {
            if (! changed)
                um.beginNewTransaction (name);
            changed = true;
        };
        switch (part)
        {
            case Part::autoMarker:
            {
                if (e.x == downX)
                    return;
                // the beat of the sound becomes a marker the moment it moves
                open (Strings::audioUi::undoWarpMarker);
                hitMarker = owner.addWarpMarker (hitBeat);
                owner.selectMarker (hitMarker);
                part = Part::warpMarker;
                [[fallthrough]];
            }
            case Part::warpMarker:
            {
                if (hitMarker < 0)
                    return;
                // Cmd: the wave slides under the marker, so the marker takes the audio that was the drag's distance before it
                double target = beat;
                if (moveAudio && hitMarker < (int) c.warpMarkers().size())
                    target = c.warpMarkers()[(size_t) hitMarker].beat - (owner.xToBeat (e.x) - owner.xToBeat (downX));
                const auto before = c.warpMarkers();
                if (hitMarker < (int) before.size() && std::abs (before[(size_t) hitMarker].beat - target) < 1.0e-12 && ! moveAudio)
                    return;
                open (Strings::audioUi::undoWarpMarker);
                owner.moveWarpMarker (hitMarker, target, moveAudio, gestureMap);
                break;
            }
            case Part::startMarker:
            {
                const double value = juce::jlimit (0.0, c.lengthBeats() - kMinBraceBeats, beat);
                if (std::abs (value - c.startOffset()) > 1.0e-12)
                {
                    open (Strings::audioUi::undoStartMarker);
                    c.setStartOffset (value, &um);
                }
                break;
            }
            case Part::braceStart:
            {
                const double value = juce::jlimit (0.0, origEnd - minBrace, beat);
                if (std::abs (value - c.loopStart()) > 1.0e-12)
                {
                    open (Strings::audioUi::undoLoopBrace);
                    c.setLoopStart (value, &um);
                }
                break;
            }
            case Part::braceEnd:
            {
                const double value = juce::jlimit (origStart + minBrace, juce::jmax (origEnd, owner.frozenView), beat);
                if (std::abs (value - c.lengthBeats()) > 1.0e-12)
                {
                    open (Strings::audioUi::undoLoopBrace);
                    c.setLengthBeats (value, &um);
                }
                break;
            }
            case Part::none: break;
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        part = Part::none;
        changed = false;
        owner.frozenView = 0.0;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y < kRulerHeight || ! warping())
            return;
        owner.model.undo.beginNewTransaction (Strings::audioUi::undoAddWarpMarker);
        owner.selectMarker (owner.addWarpMarker (owner.xToBeat (e.x)));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        // by key code, so it works in any keyboard layout
        if ((key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) && owner.selectedMarker() >= 0)
        {
            owner.removeWarpMarker (owner.selectedMarker());
            return true;
        }
        return false;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float dx = w.isReversed ? -w.deltaX : w.deltaX, dy = w.isReversed ? -w.deltaY : w.deltaY;
        const double shown = owner.viewBeats() / owner.zoomFactor();
        if (e.mods.isShiftDown() || std::abs (dx) > std::abs (dy))
            owner.scrollBy (-(double) (std::abs (dx) > std::abs (dy) ? dx : dy) * shown * 0.5);
        else if (std::abs (dy) > 0.0f)
            owner.zoomAround (e.x, std::pow (2.0, (double) dy * 2.0));
    }

    void mouseMagnify (const juce::MouseEvent& e, float scale) override { owner.zoomAround (e.x, (double) scale); }

private:
    void showMenu (const juce::MouseEvent& e, Part clicked)
    {
        const auto c = owner.clip();
        if (! c.isValid() || ! c.followTempo())
            return;
        const double beat = owner.xToBeat (e.x);
        const int marker = clicked == Part::warpMarker ? hitMarker : -1;
        const auto markers = c.warpMarkers();
        // the marker whose section holds the click, for "Tempo From Here"
        int section = -1;
        for (size_t i = 0; i + 1 < markers.size(); ++i)
            if (markers[i].beat <= beat && beat < markers[i + 1].beat)
                section = (int) i;
        if (marker >= 0 && marker + 1 < (int) markers.size())
            section = marker;

        juce::Component::SafePointer<AudioClipEditor> safe (&owner);
        juce::PopupMenu menu;
        menu.addItem (Strings::audioUi::barOneHere, [safe, beat] {
            if (safe != nullptr)
                safe->setBarOneAt (beat);
        });
        menu.addItem (Strings::audioUi::tempoFromHere, section >= 0, false, [this, safe, section, markers] {
            if (safe == nullptr)
                return;
            const auto& a = markers[(size_t) section];
            const auto& b = markers[(size_t) section + 1];
            const double seconds = (b.frame - a.frame) / juce::jmax (1.0, owner.file().sampleRate());
            askTempo (section, seconds > 0.0 ? (b.beat - a.beat) / seconds * 60.0 : owner.clipTempo());
        });
        menu.addItem (Strings::audioUi::matchProject, [safe] {
            if (safe != nullptr)
                safe->matchProjectTempo();
        });
        menu.addSeparator();
        menu.addItem (Strings::audioUi::markersAtOnsets, ! owner.onsetBeats().empty(), false, [safe] {
            if (safe != nullptr)
                safe->addMarkersAtOnsets();
        });
        menu.addItem (Strings::audioUi::quantizeMarkers, ! markers.empty(), false, [safe] {
            if (safe != nullptr)
                safe->quantizeMarkers();
        });
        menu.addItem (Strings::audioUi::deleteMarker, marker >= 0, false, [safe, marker] {
            if (safe != nullptr)
                safe->removeWarpMarker (marker);
        });
        menu.addItem (Strings::audioUi::clearWarpMarkers, ! markers.empty(), false, [safe] {
            if (safe != nullptr)
                safe->clearWarpMarkers();
        });
        if (c.state.getParent().hasType (ids::ACLIP))
        {
            menu.addSeparator();
            menu.addItem (Strings::audioUi::sliceAtOnsets, ! owner.onsetBeats().empty(), false, [safe] {
                if (safe != nullptr)
                    safe->sliceAtOnsets();
            });
            menu.addItem (Strings::audioUi::sliceOnGrid, [safe] {
                if (safe != nullptr)
                    safe->sliceOnGrid();
            });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void askTempo (int section, double current)
    {
        auto* w = new juce::AlertWindow (Strings::audioUi::tempoFromHere.trimCharactersAtEnd (juce::String::fromUTF8 ("\xe2\x80\xa6")),
                                         Strings::audioUi::tempoFromHereTitle, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("bpm", juce::String (current, 2));
        w->addButton (Strings::audioUi::ok, 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (Strings::audioUi::cancel, 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<AudioClipEditor> safe (&owner);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, safe, section] (int result) {
                                if (result == 1 && safe != nullptr)
                                {
                                    const double bpm = w->getTextEditorContents ("bpm").replaceCharacter (',', '.').getDoubleValue();
                                    if (bpm >= 20.0 && bpm <= 999.0)
                                        safe->tempoFromMarker (section, bpm);
                                }
                            }),
                            true);
    }

    void paintRuler (juce::Graphics& g, const ClipRef& c)
    {
        const auto& t = theme();
        g.setColour (t.chrome);
        g.fillRect (0, 0, getWidth(), kRulerHeight);
        g.setColour (t.line);
        g.fillRect (0, kRulerHeight - 1, getWidth(), 1);

        const double bar = owner.model.beatsPerBar();
        const double px = owner.beatToX (1.0) - owner.beatToX (0.0);
        const int labelEvery = px * bar < 34.0 ? (px * bar < 17.0 ? 4 : 2) : 1;
        g.setFont (t.numFont (10.5f));
        const double first = std::floor (owner.firstVisibleBeat());
        const double last = owner.xToBeat (getWidth());
        for (double b = juce::jmax (0.0, first); b <= juce::jmin (owner.viewBeats(), last) + 1.0e-9; b += 1.0)
        {
            const int x = juce::roundToInt (owner.beatToX (b));
            const double barIndex = b / bar;
            const bool isBar = std::abs (barIndex - std::round (barIndex)) < 1.0e-6;
            if (! isBar && px < 6.0)
                continue;
            g.setColour (t.line);
            g.fillRect (x, isBar ? 2 : kRulerHeight - kBraceHeight - 6, 1, isBar ? kRulerHeight - kBraceHeight - 2 : 5);
            if (isBar && ((int) std::lround (barIndex)) % labelEvery == 0)
            {
                g.setColour (t.dimText);
                g.drawText (juce::String ((int) std::lround (barIndex) + 1), x + 3, 1, 40, 14, juce::Justification::centredLeft, false);
            }
        }

        // the loop brace: from the loop start to the end of the content
        const float sx = (float) owner.beatToX (c.loopStart()), ex = (float) owner.beatToX (c.lengthBeats());
        const auto area = braceArea();
        const bool on = c.loopEnabled();
        const auto colour = on ? t.accent : t.dimText;
        const auto brace = juce::Rectangle<float>::leftTopRightBottom (sx, (float) area.getY() + 1.0f, ex, (float) area.getBottom() - 2.0f);
        if (brace.getWidth() > 0.0f)
        {
            g.setColour (colour.withAlpha (on ? 0.45f : 0.22f));
            g.fillRoundedRectangle (brace, 2.0f);
            g.setColour (colour);
            g.fillRect (brace.withWidth (3.0f));
            g.fillRect (brace.withLeft (brace.getRight() - 3.0f));
        }

        // the start marker: a flag above the brace
        const float px0 = (float) owner.beatToX (c.startOffset());
        juce::Path flag;
        flag.addTriangle (px0 - 5.0f, 15.0f, px0 + 5.0f, 15.0f, px0, 24.0f);
        g.setColour (t.play);
        g.fillPath (flag);
        g.fillRect (px0 - 0.5f, 15.0f, 1.0f, 2.0f);
    }

    // The warp lane: the beats of the sound as faint marks, the markers as solid triangles with a line down the wave.
    void paintLane (juce::Graphics& g, const ClipRef& c)
    {
        const auto& t = theme();
        const auto lane = laneArea().toFloat();
        g.setColour (t.chrome.interpolatedWith (t.background, 0.5f));
        g.fillRect (lane);
        g.setColour (t.line);
        g.fillRect (lane.getX(), lane.getBottom() - 1.0f, lane.getWidth(), 1.0f);
        if (! c.followTempo())
            return;
        const auto body = bodyArea().toFloat();
        const auto triangle = [&] (float x, bool solid) {
            juce::Path p;
            p.addTriangle (x - 4.5f, lane.getY() + 2.0f, x + 4.5f, lane.getY() + 2.0f, x, lane.getBottom() - 2.0f);
            if (solid)
                g.fillPath (p);
            else
                g.strokePath (p, juce::PathStrokeType (1.0f));
        };
        const double px = owner.beatToX (1.0) - owner.beatToX (0.0);
        if (px >= 8.0)
        {
            g.setColour (t.dimText.withAlpha (0.55f));
            const double first = std::floor (owner.firstVisibleBeat());
            const double last = owner.xToBeat (getWidth());
            for (double b = first; b <= last + 1.0e-9; b += 1.0)
                triangle ((float) owner.beatToX (b), false);
        }
        const auto markers = c.warpMarkers();
        for (size_t i = 0; i < markers.size(); ++i)
        {
            const float x = (float) owner.beatToX (markers[i].beat);
            const bool sel = (int) i == owner.selectedMarker();
            g.setColour (sel ? t.text : t.warning);
            triangle (x, true);
            g.setColour (t.warning.withAlpha (sel ? 0.9f : 0.55f));
            g.fillRect (x - 0.5f, body.getY(), 1.0f, body.getHeight());
        }
    }

    void paintBody (juce::Graphics& g, const ClipRef& c)
    {
        const auto& t = theme();
        const auto body = bodyArea().toFloat();
        const double view = owner.viewBeats();

        // beat and grid lines
        const double bar = owner.model.beatsPerBar();
        const double px = owner.beatToX (1.0) - owner.beatToX (0.0);
        const double step = owner.v.step * px >= 8.0 ? owner.v.step : 1.0;
        const double first = juce::jmax (0.0, std::floor (owner.firstVisibleBeat()));
        const double last = juce::jmin (view, owner.xToBeat (getWidth()));
        for (double b = first; b <= last + 1.0e-9; b += step)
        {
            const double barIndex = b / bar;
            const bool isBar = std::abs (barIndex - std::round (barIndex)) < 1.0e-6;
            const bool isBeat = std::abs (b - std::round (b)) < 1.0e-6;
            if (! isBeat && px * step < 8.0)
                continue;
            g.setColour (t.line.withAlpha (isBar ? 0.9f : (isBeat ? 0.45f : 0.2f)));
            g.fillRect ((float) owner.beatToX (b), body.getY(), 1.0f, body.getHeight());
        }

        // where the grid misses the beats the analysis heard: a tint, so the eye goes there first
        if (c.followTempo())
        {
            g.setColour (t.warning.withAlpha (0.12f));
            for (const double b : owner.uncertainBeats())
            {
                const float x0 = (float) owner.beatToX (b - 0.5), x1 = (float) owner.beatToX (b + 0.5);
                if (x1 >= 0.0f && x0 <= (float) getWidth())
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, body.getY(), x1, body.getBottom()));
            }
        }
        g.setColour (t.line.withAlpha (0.6f));
        g.fillRect (body.getX(), body.getCentreY(), body.getWidth(), 1.0f);

        const auto fileRef = owner.file();
        const auto source = fileRef.isValid() ? owner.engine.peekAudioSource (c.fileId()) : nullptr;
        if (source == nullptr)
        {
            g.setColour (t.warning);
            g.setFont (t.uiFont (13.0f, 600));
            g.drawText (Strings::audioUi::fileMissing, bodyArea().withTrimmedBottom (14), juce::Justification::centred, true);
            g.setColour (t.dimText);
            g.setFont (t.uiFont (12.0f));
            g.drawText (Strings::audioUi::noFile, bodyArea().withTrimmedTop (14), juce::Justification::centred, true);
            return;
        }

        const juce::Rectangle<float> area ((float) owner.beatToX (0.0), body.getY() + 4.0f, (float) (view * px), body.getHeight() - 8.0f);
        drawAudioWaveform (g, area, *source, c, 0.0, false, view, c.contentTempo (owner.model.tempo()), t.palette (c.colour()));

        // the onsets: thin lines over the wave
        g.setColour (t.text.withAlpha (0.4f));
        for (const double b : owner.onsetBeats())
        {
            const float x = (float) owner.beatToX (b);
            if (x >= 0.0f && x <= (float) getWidth())
                g.fillRect (x, body.getY(), 1.0f, 8.0f);
        }

        // what the clip does not play: before the start marker the content is dimmed, after the end there is nothing
        const float startX = (float) owner.beatToX (c.startOffset()), endX = (float) owner.beatToX (c.lengthBeats());
        g.setColour (t.background.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getX(), body.getY(), startX, body.getBottom()));
        g.setColour (t.play);
        g.fillRect (startX - 0.5f, body.getY(), 1.0f, body.getHeight());
        g.setColour (c.loopEnabled() ? t.accent : t.dimText);
        g.fillRect (endX - 0.5f, body.getY(), 1.0f, body.getHeight());
        g.fillRect ((float) owner.beatToX (c.loopStart()) - 0.5f, body.getY(), 1.0f, body.getHeight());
    }

    AudioClipEditor& owner;
    Part part = Part::none;
    bool changed = false, moveAudio = false;
    double origStart = 0.0, origEnd = 0.0, hitBeat = 0.0;
    int hitMarker = -1, downX = 0;
    daw::engine::WarpMap gestureMap;
};

//==============================================================================
// The decision strip above the waveform: what the analysis found, and the fixes for its usual mistakes.
//==============================================================================
class AudioClipEditor::Strip final : public juce::Component
{
public:
    explicit Strip (AudioClipEditor& o) : owner (o)
    {
        setHelpText (Strings::help::audioWarpStrip);
        for (auto* c : std::initializer_list<Chip*> { &doubleChip, &halveChip, &matchChip, &clickChip })
            addAndMakeVisible (c);
        doubleChip.onClick = [this] { owner.scaleTempoBy (2.0); };
        halveChip.onClick = [this] { owner.scaleTempoBy (0.5); };
        matchChip.onClick = [this] { owner.matchProjectTempo(); };
        clickChip.onClick = [this] { owner.model.setMetronome (! owner.model.metronome()); };
    }

    void refresh()
    {
        const auto c = owner.clip();
        const bool on = c.isValid() && c.followTempo();
        for (auto* chip : std::initializer_list<Chip*> { &doubleChip, &halveChip, &matchChip })
            chip->setEnabled (on);
        clickChip.setOn (owner.model.metronome());
        juce::String text;
        if (c.isValid())
        {
            const auto f = owner.file();
            if (on)
                text = Strings::audioUi::tempoFound (owner.clipTempo(), f.isValid() ? f.tempoConfidence() : 1.0);
            else if (c.originalTempo() <= 0.0 && f.isValid() && ! f.analysed() && f.tempo() <= 0.0)
                text = Strings::audioUi::analysing;
            else
                text = Strings::audioUi::warpOff;
        }
        if (text != label)
        {
            label = text;
            repaint();
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 3);
        r.removeFromLeft (juce::jmin (260, r.getWidth() / 3));
        doubleChip.setBounds (r.removeFromLeft (34));
        r.removeFromLeft (4);
        halveChip.setBounds (r.removeFromLeft (34));
        r.removeFromLeft (4);
        matchChip.setBounds (r.removeFromLeft (124));
        r.removeFromLeft (4);
        clickChip.setBounds (r.removeFromLeft (64));
        hintArea = r.withTrimmedLeft (10);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.fillAll (t.chrome);
        g.setColour (t.line);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        g.setColour (t.text);
        g.setFont (t.uiFont (12.5f, 600));
        g.drawText (label, getLocalBounds().reduced (8, 0).withWidth (juce::jmin (260, getWidth() / 3) - 4), juce::Justification::centredLeft, true);
        const auto c = owner.clip();
        if (c.isValid() && c.followTempo())
        {
            g.setColour (t.dimText);
            g.setFont (t.uiFont (11.5f));
            g.drawText (Strings::audioUi::stripHint, hintArea, juce::Justification::centredLeft, true);
        }
    }

private:
    AudioClipEditor& owner;
    Chip doubleChip { juce::String::fromUTF8 ("\xc3\x97" "2"), Strings::help::audioTempoDouble, false };
    Chip halveChip { juce::String::fromUTF8 ("\xc3\xb7" "2"), Strings::help::audioTempoHalve, false };
    Chip matchChip { Strings::audioUi::matchProject, Strings::help::audioWarpStrip, false };
    Chip clickChip { Strings::audioUi::click, Strings::help::audioClick, true };
    juce::String label;
    juce::Rectangle<int> hintArea;
};

//==============================================================================
// The properties column
//==============================================================================
class AudioClipEditor::Props final : public juce::Component
{
public:
    explicit Props (AudioClipEditor& o) : owner (o), content (o)
    {
        setHelpText (Strings::help::audioProps);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
    }

    void refresh()
    {
        content.refresh();
        resized();
    }

    void scaleTempo (double factor) { content.scaleTempo (factor); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds().withTrimmedRight (1));
        const int w = viewport.getMaximumVisibleWidth();
        content.setSize (w, 800);
        content.layout();
        content.setSize (w, juce::jmax (content.contentHeight(), getHeight()));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme().chrome);
        g.setColour (theme().line);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
    }

private:
    class NameField final : public juce::Component
    {
    public:
        explicit NameField (AudioClipEditor& o) : owner (o) { setHelpText (Strings::help::editorName); }

        void paint (juce::Graphics& g) override
        {
            const auto& t = theme();
            auto r = getLocalBounds().toFloat();
            fillCell (g, r, t.panel2, 0.8f);
            strokeCell (g, r, t.line, 1.0f, 0.8f);
            const auto c = owner.clip();
            g.setColour (t.palette (c.isValid() ? c.colour() : 0));
            g.fillRoundedRectangle (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ 14.0f, r.getCentreY() }), 3.0f);
            if (editor != nullptr)
                return;
            g.setColour (t.text);
            g.setFont (t.uiFont (12.5f, 600));
            g.drawText (c.isValid() ? c.name() : juce::String(), getLocalBounds().withTrimmedLeft (28).withTrimmedRight (4), juce::Justification::centredLeft, true);
        }
        void resized() override
        {
            if (editor != nullptr)
                editor->setBounds (getLocalBounds().withTrimmedLeft (26).reduced (1));
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || e.getNumberOfClicks() != 1 || e.x > 26 || e.getDistanceFromDragStart() > 3)
                return;
            const auto id = owner.clipId();
            auto menu = makePaletteMenu (owner.clip().colour(), [safe = juce::Component::SafePointer<AudioClipEditor> (&owner), id] (int index) {
                if (safe != nullptr)
                    safe->actions.setClipColourById (id, index);
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.x <= 26 || editor != nullptr)
                return;
            editor = std::make_unique<InlineEditor>();
            editor->setText (owner.clip().name(), false);
            editor->onCommit = [this] (const juce::String& text) { finish (text); };
            editor->onCancel = [this] { finish ({}); };
            addAndMakeVisible (*editor);
            resized();
            editor->grabKeyboardFocus();
            editor->selectAll();
        }

    private:
        void finish (const juce::String& text)
        {
            auto* dying = editor.release();
            removeChildComponent (dying);
            juce::MessageManager::callAsync ([dying] { delete dying; });
            if (text.isNotEmpty())
                owner.actions.renameClipById (owner.clipId(), text);
            repaint();
        }

        AudioClipEditor& owner;
        std::unique_ptr<InlineEditor> editor;
    };

    class Content final : public juce::Component
    {
    public:
        explicit Content (AudioClipEditor& o)
            : owner (o),
              nameField (o),
              reverseChip (Strings::audioUi::reverse, Strings::help::audioReverse, true),
              loopChip (Strings::audioUi::loop, Strings::help::audioLoop, true),
              snapChip (Strings::audioUi::snap, Strings::help::editorSnap, true),
              gridChip ({}, Strings::help::editorGrid, false)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &nameField, &reverseChip, &loopChip, &snapChip, &gridChip })
                addAndMakeVisible (c);
            for (auto* f : fields())
                addAndMakeVisible (f);

            reverseChip.onClick = [this] { owner.setReverse (! owner.clip().reverse()); };
            // Warp: on/off, the mode, the tempo of the audio with x2 and /2 for a tempo found an octave off.
            tempoChip.onClick = [this] {
                if (const auto c = owner.clip(); c.isValid())
                {
                    owner.model.undo.beginNewTransaction (Strings::audioUi::undoFollowTempo);
                    if (c.originalTempo() <= 0.0) // unknown: start from the project's, the user corrects it with the field
                        c.setOriginalTempo (owner.model.tempo(), &owner.model.undo);
                    c.setFollowTempo (! c.warpOn(), &owner.model.undo);
                }
            };
            modeChip.onClick = [this] { showModeMenu(); };
            doubleChip.onClick = [this] { scaleTempo (2.0); };
            halveChip.onClick = [this] { scaleTempo (0.5); };
            for (auto* c : std::initializer_list<juce::Component*> { &tempoChip, &modeChip, &doubleChip, &halveChip, &tempoField })
                addAndMakeVisible (c);
            tempoField.setHelpText (Strings::help::audioOriginalTempo);
            tempoField.setRange (20.0, 999.0, 0.01);
            tempoField.setDecimals (2);
            tempoField.setDragSensitivity (0.05);
            tempoField.setWheelStep (0.1);
            tempoField.setFontHeight (12.0f);
            tempoField.onGestureStart = [this] { owner.model.undo.beginNewTransaction (Strings::audioUi::undoClipTempo); };
            tempoField.onValueChange = [this] (double bpm) {
                if (const auto c = owner.clip(); c.isValid())
                    setClipTempo (c, bpm);
            };
            loopChip.onClick = [this] { owner.setLoopEnabled (! owner.clip().loopEnabled()); };
            snapChip.onClick = [this] {
                owner.v.snap = ! owner.v.snap;
                refresh();
            };
            gridChip.onClick = [this] { showGridMenu(); };

            const auto setup = [] (NumberField& f, const juce::String& help, double lo, double hi, double interval, int decimals) {
                f.setHelpText (help);
                f.setRange (lo, hi, interval);
                f.setDecimals (decimals);
                f.setDragSensitivity (decimals == 0 ? 0.15 : 0.1);
                f.setWheelStep (interval);
                f.setFontHeight (12.0f);
            };
            setup (gainField, Strings::help::audioGain, -70.0, 24.0, 0.1, 1);
            gainField.setSuffix (" dB");
            setup (transposeField, Strings::help::audioTranspose, -48.0, 48.0, 1.0, 0);
            setup (detuneField, Strings::help::audioDetune, -50.0, 50.0, 1.0, 0);
            setup (loopStartField, Strings::help::editorLoopStart, 0.0, 4096.0, 1.0 / 64.0, 3);
            setup (loopEndField, Strings::help::editorLoopEnd, 1.0 / 64.0, 4096.0, 1.0 / 64.0, 3);
            setup (startField, Strings::help::editorStart, 0.0, 4096.0, 1.0 / 64.0, 3);
            setup (fadeInField, Strings::help::audioFadeIn, 0.0, 64.0, 1.0 / 64.0, 3);
            setup (fadeOutField, Strings::help::audioFadeOut, 0.0, 64.0, 1.0 / 64.0, 3);
            setup (decayField, Strings::help::audioDecay, 0.0, 100.0, 1.0, 0);
            decayField.setSuffix (" %");
            addAndMakeVisible (decayField);

            const auto bind = [this] (NumberField& f, const juce::String& undoName, std::function<void (const ClipRef&, double)> apply) {
                f.onGestureStart = [this, undoName] { owner.model.undo.beginNewTransaction (undoName); };
                f.onValueChange = [this, apply] (double value) {
                    if (const auto c = owner.clip(); c.isValid())
                        apply (c, value);
                };
            };
            auto* um = &owner.model.undo;
            bind (gainField, Strings::audioUi::undoGain, [um] (const ClipRef& c, double v) { c.setGainDb (v, um); });
            bind (transposeField, Strings::audioUi::undoTranspose, [um] (const ClipRef& c, double v) { c.setTranspose ((int) std::lround (v), um); });
            bind (detuneField, Strings::audioUi::undoDetune, [um] (const ClipRef& c, double v) { c.setDetune ((int) std::lround (v), um); });
            bind (loopStartField, Strings::audioUi::undoLoopBrace, [um] (const ClipRef& c, double v) {
                c.setLoopStart (juce::jmin (v, c.lengthBeats() - kMinBraceBeats), um);
            });
            bind (loopEndField, Strings::audioUi::undoLoopBrace, [um] (const ClipRef& c, double v) {
                c.setLengthBeats (juce::jmax (v, c.loopStart() + kMinBraceBeats), um);
            });
            bind (startField, Strings::audioUi::undoStartMarker, [um] (const ClipRef& c, double v) {
                c.setStartOffset (juce::jmin (v, c.lengthBeats() - kMinBraceBeats), um);
            });
            bind (fadeInField, Strings::audioUi::undoFadeIn, [um] (const ClipRef& c, double v) { c.setFadeIn (v, um); });
            bind (fadeOutField, Strings::audioUi::undoFadeOut, [um] (const ClipRef& c, double v) { c.setFadeOut (v, um); });
            bind (decayField, Strings::audioUi::undoDecay, [um] (const ClipRef& c, double v) { c.setBeatsDecay (v / 100.0, um); });
        }

        std::vector<NumberField*> fields()
        {
            return { &gainField, &transposeField, &detuneField, &loopStartField, &loopEndField, &startField, &fadeInField, &fadeOutField };
        }

        void refresh()
        {
            const auto c = owner.clip();
            if (! c.isValid())
                return;
            const auto set = [] (NumberField& f, double value) {
                if (! f.isEditing())
                    f.setValue (value);
            };
            set (gainField, c.gainDb());
            set (transposeField, c.transpose());
            set (detuneField, c.detune());
            set (loopStartField, c.loopStart());
            set (loopEndField, c.lengthBeats());
            set (startField, c.startOffset());
            set (fadeInField, c.fadeIn());
            set (fadeOutField, c.fadeOut());
            reverseChip.setOn (c.reverse());
            {
                const auto file = owner.model.findAudioFile (c.fileId());
                const bool analysing = c.originalTempo() <= 0.0 && ! file.analysed() && file.tempo() <= 0.0;
                tempoChip.setOn (c.followTempo());
                tempoChip.setText (analysing ? Strings::audioUi::analysing : Strings::audioUi::warp);
                modeChip.setText (Strings::audioUi::warpModeName (c.warpMode()));
                modeChip.setEnabled (c.followTempo());
                if (! tempoField.isEditing())
                    tempoField.setValue (c.originalTempo() > 0.0 ? c.originalTempo() : owner.model.tempo());
                for (auto* comp : std::initializer_list<juce::Component*> { &tempoField, &doubleChip, &halveChip })
                    comp->setEnabled (c.followTempo());
                set (decayField, c.beatsDecay() * 100.0);
                decayField.setEnabled (c.followTempo() && c.warpMode() == "beats");
            }
            loopChip.setOn (c.loopEnabled());
            snapChip.setOn (owner.v.snap);
            gridChip.setText (Strings::audioUi::grid + ": " + owner.gridText());

            const auto f = owner.file();
            info = f.isValid() ? Strings::audioUi::fileInfo (f.name(), f.sampleRate(), f.channels(),
                                                              f.sampleRate() > 0.0 ? (double) f.lengthSamples() / f.sampleRate() : 0.0)
                               : Strings::audioUi::fileMissing;
            const double engineRate = owner.engine.getSampleRate();
            const juce::String warning = f.isValid() && engineRate > 0.0 && std::abs (f.sampleRate() - engineRate) > 0.5
                                             ? Strings::audioUi::rateWarning (f.sampleRate())
                                             : juce::String();
            if (warning != rateWarning)
            {
                rateWarning = warning;
                layoutDirty = true;
            }
            nameField.repaint();
            repaint();
        }

        bool needsLayout() const { return layoutDirty; }

        void layout()
        {
            layoutDirty = false;
            const int w = getWidth() - 2 * kMarginProps;
            int y = kMarginProps;
            nameField.setBounds (kMarginProps, y, w, kRow);
            y += kRow + kGap;
            infoY = y;
            y += 32 + kGap;
            if (rateWarning.isNotEmpty())
            {
                warningY = y;
                y += 30 + kGap;
            }
            else
                warningY = -1;
            labelY = y;
            for (auto* f : { &gainField, &transposeField, &detuneField })
            {
                f->setBounds (kMarginProps + w - 76, y, 76, kRow);
                y += kRow + kGap;
            }
            reverseChip.setBounds (kMarginProps, y, w / 2 - 2, kRow);
            loopChip.setBounds (kMarginProps + w / 2 + 2, y, w - w / 2 - 2, kRow);
            y += kRow + kGap;
            braceY = y;
            for (auto* f : { &loopStartField, &loopEndField, &startField, &fadeInField, &fadeOutField })
            {
                f->setBounds (kMarginProps + w - 76, y, 76, kRow);
                y += kRow + kGap;
            }
            snapChip.setBounds (kMarginProps, y, w / 2 - 2, kRow);
            gridChip.setBounds (kMarginProps + w / 2 + 2, y, w - w / 2 - 2, kRow);
            y += kRow + kGap;
            tempoChip.setBounds (kMarginProps, y, w / 2 - 2, kRow);
            modeChip.setBounds (kMarginProps + w / 2 + 2, y, w - w / 2 - 2, kRow);
            y += kRow + kGap;
            tempoField.setBounds (kMarginProps, y, w - 2 * 34 - 4, kRow);
            doubleChip.setBounds (kMarginProps + w - 2 * 34 - 2, y, 32, kRow);
            halveChip.setBounds (kMarginProps + w - 34, y, 34, kRow);
            y += kRow + kGap;
            decayY = y;
            decayField.setBounds (kMarginProps + w - 76, y, 76, kRow);
            y += kRow + kMarginProps;
            height = y;
        }

        int contentHeight() const { return height; }

        void paint (juce::Graphics& g) override
        {
            const auto& t = theme();
            const int w = getWidth() - 2 * kMarginProps;
            g.setColour (t.dimText);
            g.setFont (t.uiFont (11.5f));
            g.drawFittedText (info, kMarginProps + 2, infoY, w - 4, 32, juce::Justification::topLeft, 2);
            if (warningY >= 0)
            {
                g.setColour (t.warning);
                g.drawFittedText (rateWarning, kMarginProps + 2, warningY, w - 4, 30, juce::Justification::topLeft, 2);
            }
            g.setColour (t.dimText);
            const juce::String* names[3] = { &Strings::audioUi::gain, &Strings::audioUi::transpose, &Strings::audioUi::detune };
            for (int i = 0; i < 3; ++i)
                g.drawText (*names[i], kMarginProps + 2, labelY + i * (kRow + kGap), w - 80, kRow, juce::Justification::centredLeft, true);
            const juce::String* more[5] = { &Strings::audioUi::loopStart, &Strings::audioUi::loopEnd, &Strings::audioUi::startPoint,
                                            &Strings::audioUi::fadeIn, &Strings::audioUi::fadeOut };
            for (int i = 0; i < 5; ++i)
                g.drawText (*more[i], kMarginProps + 2, braceY + i * (kRow + kGap), w - 80, kRow, juce::Justification::centredLeft, true);
            g.drawText (Strings::audioUi::decay, kMarginProps + 2, decayY, w - 80, kRow, juce::Justification::centredLeft, true);
        }

    private:
        void showGridMenu()
        {
            juce::PopupMenu menu;
            static const char* names[] = { "1/4", "1/8", "1/16", "1/32" };
            for (int i = 0; i < 4; ++i)
                menu.addItem (10 + i, names[i], true, std::abs (owner.v.step - kSteps[i]) < 1.0e-9);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridChip),
                                [safe = juce::Component::SafePointer<AudioClipEditor> (&owner)] (int result) {
                                    if (safe == nullptr || result < 10)
                                        return;
                                    safe->v.step = kSteps[result - 10];
                                    safe->props->refresh();
                                });
        }

        AudioClipEditor& owner;
        NameField nameField;
        Chip reverseChip, loopChip, snapChip, gridChip;
        Chip tempoChip { Strings::audioUi::warp, Strings::help::audioFollowTempo, true };
        Chip modeChip { {}, Strings::help::audioWarpMode, false };
        Chip doubleChip { juce::String::fromUTF8 ("\xc3\x97" "2"), Strings::help::audioTempoDouble, false };
        Chip halveChip { juce::String::fromUTF8 ("\xc3\xb7" "2"), Strings::help::audioTempoHalve, false };
        NumberField tempoField;

        // The audio's tempo changes how many beats it lasts: the content stretches with it (its length, loop and
        // start in beats), so the same sound stays under the same part of the clip.
        void setClipTempo (const ClipRef& c, double bpm)
        {
            const double old = c.originalTempo() > 0.0 ? c.originalTempo() : owner.model.tempo();
            bpm = juce::jlimit (20.0, 999.0, bpm);
            if (std::abs (bpm - old) < 1.0e-6)
                return;
            clipedit::scaleTime (c, bpm / old, &owner.model.undo);
            c.setOriginalTempo (bpm, &owner.model.undo);
        }

    public:
        void scaleTempo (double factor)
        {
            if (const auto c = owner.clip(); c.isValid() && c.followTempo())
            {
                owner.model.undo.beginNewTransaction (Strings::audioUi::undoClipTempo);
                setClipTempo (c, c.originalTempo() * factor);
            }
        }

    private:
        void showModeMenu()
        {
            const auto c = owner.clip();
            if (! c.isValid())
                return;
            juce::PopupMenu menu;
            static const char* modes[] = { "beats", "tones", "texture", "repitch", "complex", "complexpro" };
            for (const auto* m : modes)
            {
                const juce::String mode (m);
                menu.addItem (Strings::audioUi::warpModeName (mode), true, c.warpMode() == mode, [this, mode] {
                    if (const auto cl = owner.clip(); cl.isValid() && cl.warpMode() != mode)
                    {
                        owner.model.undo.beginNewTransaction (Strings::audioUi::undoWarpMode);
                        cl.setWarpMode (mode, &owner.model.undo);
                    }
                });
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&modeChip));
        }
        NumberField gainField, transposeField, detuneField, loopStartField, loopEndField, startField, fadeInField, fadeOutField, decayField;
        juce::String info, rateWarning;
        int infoY = 0, warningY = -1, labelY = 0, braceY = 0, decayY = 0, height = 0;
        bool layoutDirty = true;
    };

    AudioClipEditor& owner;
    Content content;
    juce::Viewport viewport;

    friend class AudioClipEditor;
};

//==============================================================================
AudioClipEditor::AudioClipEditor (AudioEngine& e, ProjectModel& m, ProjectActions& a) : engine (e), model (m), actions (a)
{
    props = std::make_unique<Props> (*this);
    waveView = std::make_unique<Wave> (*this);
    strip = std::make_unique<Strip> (*this);
    addAndMakeVisible (*props);
    addAndMakeVisible (*waveView);
    addAndMakeVisible (*strip);
    model.root.addListener (this);
    startTimerHz (30);
}

AudioClipEditor::~AudioClipEditor()
{
    stopTimer();
    model.root.removeListener (this);
}

juce::Component& AudioClipEditor::wave()
{
    return *waveView;
}

ClipRef AudioClipEditor::clip() const
{
    return currentClip > 0 ? model.findClip (currentClip) : ClipRef();
}

AudioFileRef AudioClipEditor::file() const
{
    const auto c = clip();
    return c.isValid() ? model.findAudioFile (c.fileId()) : AudioFileRef();
}

bool AudioClipEditor::hasClip() const
{
    const auto c = clip();
    return c.isValid() && c.isAudio();
}

void AudioClipEditor::setClip (int64_t id)
{
    if (id == currentClip)
        return;
    currentClip = id;
    selected = -1;
    dirty = true;
    shownPeaks = -1;
    zoom = 1.0;
    scrollBeats = 0.0;
    tick();
}

void AudioClipEditor::tick()
{
    if (! isShowing() && ! dirty)
        return;
    int64_t peaks = 0;
    if (const auto c = clip(); c.isValid())
    {
        if (const auto source = engine.peekAudioSource (c.fileId()))
            peaks = source->peaksAvailable.load (std::memory_order_acquire);
        // the onsets and beats of the file arrive from the background analysis
        const bool hasAnalysis = analysis() != nullptr;
        if (hasAnalysis != shownAnalysis)
        {
            shownAnalysis = hasAnalysis;
            dirty = true;
        }
    }
    if (! dirty && peaks == shownPeaks)
        return;
    dirty = false;
    shownPeaks = peaks;
    props->content.refresh();
    strip->refresh();
    if (props->content.needsLayout())
        props->resized();
    waveView->repaint();
}

//==============================================================================
double AudioClipEditor::snapBeat (double beat, bool bypass) const
{
    if (! v.snap || bypass)
        return beat;
    return std::floor (beat / v.step + 0.5) * v.step;
}

juce::String AudioClipEditor::gridText() const
{
    return "1/" + juce::String (juce::roundToInt (4.0 / v.step));
}

double AudioClipEditor::viewBeats() const
{
    if (frozenView > 0.0)
        return frozenView;
    const auto c = clip();
    if (! c.isValid())
        return 4.0;
    double fileBeats = 0.0;
    if (const auto f = file(); f.isValid())
    {
        // the content beat where the file ends, through the warp map and the transposition
        const auto map = warpMap();
        const double origin = map.frameAt (0.0);
        fileBeats = map.beatAt (origin + ((double) f.lengthSamples() - origin) / juce::jmax (1.0e-3, c.pitchRatio()));
    }
    return juce::jmax (1.0, c.lengthBeats(), fileBeats);
}

double AudioClipEditor::beatToX (double beat) const
{
    const double usable = juce::jmax (1, waveView->getWidth() - 2 * kMargin);
    return kMargin + (beat - scrollBeats) / (viewBeats() / zoom) * usable;
}

double AudioClipEditor::xToBeat (double x) const
{
    const double usable = juce::jmax (1, waveView->getWidth() - 2 * kMargin);
    return scrollBeats + (x - kMargin) / usable * (viewBeats() / zoom);
}

void AudioClipEditor::zoomAround (double x, double factor)
{
    const double anchor = xToBeat (x);
    const double usable = juce::jmax (1, waveView->getWidth() - 2 * kMargin);
    // From the whole content in down to single samples (a 4-bar loop at 20000x is a few pixels per sample).
    zoom = juce::jlimit (1.0, 20000.0, zoom * factor);
    scrollBeats = anchor - (x - kMargin) / usable * (viewBeats() / zoom);
    scrollBy (0.0);
}

void AudioClipEditor::scrollBy (double beats)
{
    const double shown = viewBeats() / zoom;
    scrollBeats = juce::jlimit (0.0, juce::jmax (0.0, viewBeats() - shown), scrollBeats + beats);
    waveView->repaint();
}

//==============================================================================
void AudioClipEditor::setGainDb (double db)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoGain);
        c.setGainDb (db, &model.undo);
    }
}

void AudioClipEditor::setTranspose (int semitones)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoTranspose);
        c.setTranspose (semitones, &model.undo);
    }
}

void AudioClipEditor::setDetune (int cents)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoDetune);
        c.setDetune (cents, &model.undo);
    }
}

void AudioClipEditor::setReverse (bool on)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoReverse);
        c.setReverse (on, &model.undo);
    }
}

void AudioClipEditor::setLoopEnabled (bool on)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::undoLoopSwitch);
        c.setLoopEnabled (on, &model.undo);
    }
}

void AudioClipEditor::setFadeIn (double beats)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoFadeIn);
        c.setFadeIn (beats, &model.undo);
    }
}

void AudioClipEditor::setFadeOut (double beats)
{
    if (const auto c = clip(); c.isValid())
    {
        model.undo.beginNewTransaction (Strings::audioUi::undoFadeOut);
        c.setFadeOut (beats, &model.undo);
    }
}

//==============================================================================
// Warp markers
//==============================================================================
std::shared_ptr<const WarpService::Analysis> AudioClipEditor::analysis() const
{
    const auto f = file();
    if (! f.isValid())
        return nullptr;
    return engine.getWarp().analysisFor (engine.resolveAudioFile (f), f.hash());
}

daw::engine::WarpMap AudioClipEditor::warpMap() const
{
    const auto c = clip();
    const auto f = file();
    return c.isValid() ? c.warpMap (f.isValid() ? f.sampleRate() : engine.getSampleRate(), model.tempo()) : daw::engine::WarpMap();
}

double AudioClipEditor::fileFrames() const
{
    const auto f = file();
    return f.isValid() ? (double) f.lengthSamples() : 0.0;
}

std::vector<daw::engine::WarpMarker> AudioClipEditor::materializedMarkers() const
{
    const auto c = clip();
    if (! c.isValid())
        return {};
    auto markers = c.warpMarkers();
    if (markers.size() >= 2)
        return markers;
    // No markers yet: the ends of the file on the straight line of the clip.
    const auto map = warpMap();
    const double n = juce::jmax (1.0, fileFrames());
    return { { 0.0, map.beatAt (0.0) }, { n, map.beatAt (n) } };
}

void AudioClipEditor::setMarkers (std::vector<daw::engine::WarpMarker> markers, const juce::String& undoName)
{
    const auto c = clip();
    if (! c.isValid())
        return;
    if (undoName.isNotEmpty())
        model.undo.beginNewTransaction (undoName);
    // through the map's own filter: sorted, no segment that runs backwards or is too short
    const auto map = warpMap();
    const daw::engine::WarpMap clean (std::move (markers), map.frameAt (0.0), map.defaultFramesPerBeat());
    c.setWarpMarkers (clean.markers(), &model.undo);
    selected = juce::jmin (selected, (int) clean.markers().size() - 1);
    dirty = true;
}

int AudioClipEditor::addWarpMarker (double beat)
{
    const auto c = clip();
    if (! c.isValid() || ! c.followTempo())
        return -1;
    const auto map = warpMap();
    auto markers = materializedMarkers();
    const double frame = juce::jlimit (0.0, fileFrames(), map.frameAt (beat));
    for (size_t i = 0; i < markers.size(); ++i)
        if (std::abs (markers[i].beat - beat) < daw::engine::WarpMap::kMinBeats * 2.0)
            return (int) i; // already pinned there
    markers.push_back ({ frame, map.beatAt (frame) });
    std::sort (markers.begin(), markers.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; });
    setMarkers (markers, {});
    const auto stored = c.warpMarkers();
    for (size_t i = 0; i < stored.size(); ++i)
        if (std::abs (stored[i].frame - frame) < 1.0e-6)
            return (int) i;
    return -1;
}

void AudioClipEditor::moveWarpMarker (int index, double beat, bool moveAudio, const daw::engine::WarpMap& before)
{
    const auto c = clip();
    auto markers = c.isValid() ? c.warpMarkers() : std::vector<daw::engine::WarpMarker>();
    if (index < 0 || index >= (int) markers.size())
        return;
    auto& m = markers[(size_t) index];
    constexpr double minBeats = daw::engine::WarpMap::kMinBeats * 2.0, minFrames = daw::engine::WarpMap::kMinFrames * 2.0;
    if (moveAudio)
    {
        // the marker stays at its beat; the audio that played at `beat` when the drag began comes under it
        const double lo = index > 0 ? markers[(size_t) index - 1].frame + minFrames : 0.0;
        const double hi = index + 1 < (int) markers.size() ? markers[(size_t) index + 1].frame - minFrames : fileFrames();
        m.frame = juce::jlimit (lo, juce::jmax (lo, hi), before.frameAt (beat));
    }
    else
    {
        const double lo = index > 0 ? markers[(size_t) index - 1].beat + minBeats : -1.0e9;
        const double hi = index + 1 < (int) markers.size() ? markers[(size_t) index + 1].beat - minBeats : 1.0e9;
        m.beat = juce::jlimit (lo, juce::jmax (lo, hi), beat);
    }
    c.setWarpMarkers (markers, &model.undo);
    dirty = true;
}

void AudioClipEditor::removeWarpMarker (int index)
{
    const auto c = clip();
    auto markers = c.isValid() ? c.warpMarkers() : std::vector<daw::engine::WarpMarker>();
    if (index < 0 || index >= (int) markers.size())
        return;
    markers.erase (markers.begin() + index);
    if (markers.size() < 2)
        markers.clear();
    selected = -1;
    setMarkers (markers, Strings::audioUi::undoRemoveWarpMarker);
}

void AudioClipEditor::clearWarpMarkers()
{
    selected = -1;
    setMarkers ({}, Strings::audioUi::clearWarpMarkers);
}

void AudioClipEditor::selectMarker (int index)
{
    selected = index;
    waveView->repaint();
}

void AudioClipEditor::setBarOneAt (double beat)
{
    const auto c = clip();
    if (! c.isValid())
        return;
    const double bar = juce::jmax (1.0, model.beatsPerBar());
    const double delta = beat - std::round (beat / bar) * bar;
    if (std::abs (delta) < 1.0e-9)
        return;
    model.undo.beginNewTransaction (Strings::audioUi::barOneHere);
    // the whole grid of the sound moves by delta: what played at `beat` now plays on the bar line
    const auto map = warpMap();
    c.setSampleStart (map.frameAt (delta), &model.undo);
    auto markers = c.warpMarkers();
    for (auto& m : markers)
        m.beat -= delta;
    c.setWarpMarkers (markers, &model.undo);
    dirty = true;
}

void AudioClipEditor::tempoFromMarker (int index, double bpm)
{
    const auto c = clip();
    const auto f = file();
    auto markers = c.isValid() ? c.warpMarkers() : std::vector<daw::engine::WarpMarker>();
    if (! f.isValid() || index < 0 || index + 1 >= (int) markers.size() || bpm <= 0.0)
        return;
    const auto& a = markers[(size_t) index];
    const double newNext = a.beat + (markers[(size_t) index + 1].frame - a.frame) / f.sampleRate() * bpm / 60.0;
    const double shift = newNext - markers[(size_t) index + 1].beat;
    for (size_t i = (size_t) index + 1; i < markers.size(); ++i)
        markers[i].beat += shift;
    setMarkers (markers, Strings::audioUi::undoClipTempo);
}

void AudioClipEditor::quantizeMarkers()
{
    const auto c = clip();
    auto markers = c.isValid() ? c.warpMarkers() : std::vector<daw::engine::WarpMarker>();
    if (markers.empty())
        return;
    // the ends of the file stay where they are; the markers inside go to the nearest grid line
    const double n = fileFrames();
    for (auto& m : markers)
        if (m.frame > 0.5 && m.frame < n - 0.5)
            m.beat = std::round (m.beat / v.step) * v.step;
    setMarkers (markers, Strings::audioUi::quantizeMarkers);
}

std::vector<double> AudioClipEditor::onsetBeats() const
{
    std::vector<double> out;
    const auto a = analysis();
    const auto c = clip();
    const auto f = file();
    if (a == nullptr || ! c.isValid() || ! f.isValid())
        return out;
    const auto map = warpMap();
    const double origin = map.frameAt (0.0), pitch = juce::jmax (1.0e-3, c.pitchRatio());
    for (const double t : a->onsets)
        out.push_back (map.beatAt (origin + (t * f.sampleRate() - origin) / pitch));
    return out;
}

std::vector<double> AudioClipEditor::uncertainBeats() const
{
    std::vector<double> out;
    const auto a = analysis();
    const auto c = clip();
    const auto f = file();
    if (a == nullptr || ! c.isValid() || ! c.followTempo() || ! f.isValid())
        return out;
    // A beat the analysis heard that the clip's grid places more than a 32nd note away from a beat line.
    const auto map = warpMap();
    for (const double t : a->beats)
    {
        const double b = map.beatAt (t * f.sampleRate());
        if (std::abs (b - std::round (b)) > 0.125 && (out.empty() || out.back() != std::round (b)))
            out.push_back (std::round (b));
    }
    return out;
}

void AudioClipEditor::addMarkersAtOnsets()
{
    const auto c = clip();
    const auto a = analysis();
    const auto f = file();
    if (! c.isValid() || ! c.followTempo() || a == nullptr || ! f.isValid())
        return;
    const auto map = warpMap();
    auto markers = materializedMarkers();
    for (const double t : a->onsets)
    {
        const double frame = t * f.sampleRate();
        if (frame > 0.0 && frame < fileFrames())
            markers.push_back ({ frame, map.beatAt (frame) });
    }
    setMarkers (markers, Strings::audioUi::markersAtOnsets);
}

double AudioClipEditor::clipTempo() const
{
    const auto c = clip();
    const auto f = file();
    if (! c.isValid())
        return model.tempo();
    const auto markers = c.warpMarkers();
    if (markers.size() >= 2 && f.isValid() && markers.back().frame > markers.front().frame)
        return (markers.back().beat - markers.front().beat) / ((markers.back().frame - markers.front().frame) / f.sampleRate()) * 60.0;
    return c.originalTempo() > 0.0 ? c.originalTempo() : model.tempo();
}

void AudioClipEditor::matchProjectTempo()
{
    const auto c = clip();
    if (! c.isValid() || ! c.followTempo())
        return;
    const double bpm = std::round (clipTempo() * 100.0) / 100.0;
    if (std::abs (bpm - model.tempo()) < 1.0e-6)
        return;
    model.undo.beginNewTransaction (Strings::audioUi::undoMatchProject);
    model.setTempo (juce::jlimit (20.0, 999.0, bpm));
}

void AudioClipEditor::scaleTempoBy (double factor)
{
    props->scaleTempo (factor);
}

namespace
{
// Cuts an arrangement clip at content beats (one transaction); returns how many cuts were made.
int sliceArrangementClip (ProjectModel& model, const ClipRef& c, std::vector<double> contentBeats)
{
    const auto holder = c.state.getParent();
    if (! holder.hasType (ids::ACLIP) || contentBeats.empty())
        return 0;
    const ArrangementClipRef a (holder);
    // timeline beats inside the clip, right to left, so the left part keeps being `a`
    std::vector<double> at;
    for (const double b : contentBeats)
    {
        const double t = a.start() + (b - a.offset());
        if (t > a.start() + 1.0e-3 && t < a.end() - 1.0e-3)
            at.push_back (t);
    }
    std::sort (at.begin(), at.end(), std::greater<>());
    at.erase (std::unique (at.begin(), at.end(), [] (double x, double y) { return std::abs (x - y) < 1.0e-3; }), at.end());
    if (at.empty())
        return 0;
    model.undo.beginNewTransaction (Strings::audioUi::undoSlice);
    int cuts = 0;
    for (const double t : at)
        if (arrangement::splitAt (model, a, t).isValid())
            ++cuts;
    return cuts;
}
} // namespace

int AudioClipEditor::sliceAtOnsets()
{
    std::vector<double> beats;
    for (const auto& s : daw::engine::sliceByOnsets (onsetBeats(), -1.0e9, 1.0e9, 1.0 / 16.0))
        beats.push_back (s.start);
    return sliceArrangementClip (model, clip(), beats);
}

int AudioClipEditor::sliceOnGrid()
{
    const auto c = clip();
    if (! c.isValid())
        return 0;
    std::vector<double> beats;
    for (const auto& s : daw::engine::sliceByGrid (0.0, c.lengthBeats(), v.step))
        beats.push_back (s.start);
    return sliceArrangementClip (model, c, beats);
}

//==============================================================================
void AudioClipEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme().background);
    if (! hasClip())
    {
        const auto& t = theme();
        g.setColour (t.text);
        g.setFont (t.uiFont (16.0f, 600));
        g.drawText (Strings::editor::noClip, getLocalBounds().withTrimmedBottom (24), juce::Justification::centred, false);
    }
}

void AudioClipEditor::resized()
{
    auto r = getLocalBounds();
    props->setBounds (r.removeFromLeft (kPropsWidth));
    strip->setBounds (r.removeFromTop (kStripHeight));
    waveView->setBounds (r);
    dirty = true;
}
} // namespace ui
