#include "TopBar.h"

#include <optional>

#include "../Strings.h"
#include "Chrome.h"
#include "Kit.h"
#include "ScalePicker.h"
#include "../engine/AudioEngine.h"
#include "../model/Automation.h"
#include "../engine/SessionRecorder.h"
#include "TransportMenus.h"
#include "ArrangementView.h"


namespace ui
{
namespace
{
// Line icons of the top row, turned into filled outlines so IconButton can fill them like the others.
juce::Path stroked (const juce::Path& lines, float thickness = 1.5f)
{
    juce::Path out;
    juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (out, lines);
    return out;
}
juce::Path undoIcon (juce::Rectangle<float> b, bool mirrored)
{
    juce::Path p;
    p.startNewSubPath (b.getX() + b.getWidth() * 0.88f, b.getY() + b.getHeight() * 0.85f);
    p.quadraticTo (b.getX() + b.getWidth() * 0.88f, b.getY() + b.getHeight() * 0.32f, b.getX() + b.getWidth() * 0.18f, b.getY() + b.getHeight() * 0.32f);
    p.startNewSubPath (b.getX() + b.getWidth() * 0.42f, b.getY() + b.getHeight() * 0.06f);
    p.lineTo (b.getX() + b.getWidth() * 0.16f, b.getY() + b.getHeight() * 0.32f);
    p.lineTo (b.getX() + b.getWidth() * 0.42f, b.getY() + b.getHeight() * 0.58f);
    if (mirrored)
        p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, b.getCentreX(), b.getCentreY()));
    return stroked (p);
}
juce::Path exportIcon (juce::Rectangle<float> b)
{
    juce::Path p;
    p.startNewSubPath (b.getX() + b.getWidth() * 0.12f, b.getY() + b.getHeight() * 0.55f);
    p.lineTo (b.getX() + b.getWidth() * 0.12f, b.getBottom() - b.getHeight() * 0.06f);
    p.lineTo (b.getRight() - b.getWidth() * 0.12f, b.getBottom() - b.getHeight() * 0.06f);
    p.lineTo (b.getRight() - b.getWidth() * 0.12f, b.getY() + b.getHeight() * 0.55f);
    p.startNewSubPath (b.getCentreX(), b.getY() + b.getHeight() * 0.68f);
    p.lineTo (b.getCentreX(), b.getY() + b.getHeight() * 0.04f);
    p.startNewSubPath (b.getCentreX() - b.getWidth() * 0.24f, b.getY() + b.getHeight() * 0.26f);
    p.lineTo (b.getCentreX(), b.getY() + b.getHeight() * 0.02f);
    p.lineTo (b.getCentreX() + b.getWidth() * 0.24f, b.getY() + b.getHeight() * 0.26f);
    return stroked (p);
}
juce::Path gearIcon (juce::Rectangle<float> b)
{
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    juce::Path p;
    p.addStar (b.getCentre(), 8, r * 0.74f, r, 0.0f);
    p.addEllipse (juce::Rectangle<float> (r * 0.72f, r * 0.72f).withCentre (b.getCentre()));
    p.setUsingNonZeroWinding (false);
    return p;
}
juce::Path libraryIcon (juce::Rectangle<float> b)
{
    juce::Path p;
    p.addRoundedRectangle (b.reduced (0.5f, 1.5f), 2.0f);
    p.startNewSubPath (b.getX() + b.getWidth() * 0.38f, b.getY() + 1.5f);
    p.lineTo (b.getX() + b.getWidth() * 0.38f, b.getBottom() - 1.5f);
    return stroked (p, 1.4f);
}
} // namespace

//==============================================================================
class TopBar::IconButton final : public juce::Component
{
public:
    enum class Role
    {
        normal,
        play,
        record,
        warning // lit in the orange of the theme ("Back to arrangement")
    };

    std::function<juce::Path (juce::Rectangle<float>)> icon;
    juce::String text;
    Role role = Role::normal;
    bool on = false;
    bool accentInk = false; // the label in the accent colour (the scale button while a scale is set)
    bool framed = false;    // glass themes: a button of its own (top row), not one of the capsule's row
    bool bare = false;      // glass themes: no pane of glass at rest (stop and record, as plain icons)
    float iconSize = 0.0f;  // 0: the default size
    bool leftAligned = false; // icon and label start at the left edge (the Library button)
    std::optional<juce::Colour> iconColour; // the icon in its own colour (the red dot of Capture)
    bool chevron = false; // glass: a small down arrow after the label (the button opens a list)
    std::function<void()> onClick;
    std::function<void()> onPopup; // right mouse button

private:
    std::unique_ptr<juce::VBlankAttachment> fade;
    double fadeStart = 0.0;
    float fadeFrom = 0.0f, amount = 0.0f; // how far the lit state has appeared
    bool fading = false;

public:

    IconButton (std::function<juce::Path (juce::Rectangle<float>)> iconFn, const juce::String& label, Role r, const juce::String& help)
        : icon (std::move (iconFn)), text (label), role (r)
    {
        setHelpText (help);
    }

    void setOn (bool v)
    {
        if (on == v)
            return;
        on = v;
        // glass: the lit pane appears (and goes) over a quarter of a second instead of switching
        if (theme().glass && isShowing())
        {
            fadeStart = juce::Time::getMillisecondCounterHiRes();
            fadeFrom = amount;
            fading = true;
            if (fade == nullptr) // kept for the button's life; idle between fades
                fade = std::make_unique<juce::VBlankAttachment> (this, [this] {
                    if (! fading)
                        return;
                    const float p = (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - fadeStart) / 260.0);
                    const float eased = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
                    amount = fadeFrom + ((on ? 1.0f : 0.0f) - fadeFrom) * eased;
                    fading = p < 1.0f;
                    repaint();
                });
        }
        else
            amount = on ? 1.0f : 0.0f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // the look comes from the kit: a framed button, a capsule switch, record/punch in red, play round in glass
        const auto& t = theme();
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        // glass: the icon floats a little above the glass, so it moves a little with the pointer (parallax)
        std::optional<glass::Lit> lit;
        if (t.glass)
            lit.emplace (*this);
        const auto floatBy = t.glass ? glass::parallax (*this, 1.2f) : juce::Point<float>();
        using K = kit::Kind;
        const K idle = framed || ! t.glass ? K::plain : K::ghost; // how a button of this place looks while off
        // while the pane fades out the button still draws its lit state, fading
        const bool shownOn = on || amount > 0.001f;
        const K kind = role == Role::play && icon && text.isEmpty() ? K::play
                     : role == Role::record ? (shownOn && (framed || ! t.glass) ? K::danger : idle)
                     : role == Role::warning ? (shownOn ? K::toggle : idle)
                     : idle;
        kit::State st;
        st.on = shownOn;
        st.amount = amount;
        st.bare = bare;
        st.hover = isMouseOver() && isEnabled();
        st.enabled = isEnabled();
        auto ink = kit::button (g, r, kind, st, role == Role::warning ? std::optional<juce::Colour> (t.warning)
                                             : (role == Role::record && kind == K::ghost ? std::optional<juce::Colour> (t.record) : std::nullopt));
        if (! on && isEnabled() && (role == Role::record || accentInk))
            ink = role == Role::record ? t.record : t.accent;
        if (kind == K::play && t.glass)
        {
            const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.36f;
            g.setColour (ink);
            g.fillPath (icon (juce::Rectangle<float> (s, s).withCentre (r.getCentre().translated (s * 0.08f, 0.0f) + floatBy)));
            return;
        }
        kit::LabelStyle style;
        style.size = getWidth() < 34 ? 10.5f : (framed && t.glass ? 13.5f : 12.5f);
        style.iconSize = iconSize > 0.0f ? iconSize : (text.isEmpty() ? 11.0f : 10.0f);
        style.iconColour = iconColour.has_value() && isEnabled() ? iconColour : std::nullopt;
        style.leftAligned = leftAligned;
        style.chevron = chevron && t.glass;
        kit::label (g, r + floatBy, text, ink, icon ? kit::Icon (icon) : kit::Icon(), style);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! isEnabled() || ! getLocalBounds().contains (e.getPosition()))
            return;
        if (e.mods.isPopupMenu())
        {
            if (onPopup)
                onPopup();
        }
        else if (onClick)
            onClick();
    }
};

//==============================================================================
class TopBar::Lamp final : public juce::Component
{
public:
    Lamp (const juce::String& label, const juce::String& help) : text (label)
    {
        setHelpText (help);
    }

    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }

    void setLit (bool v, bool useGreen)
    {
        if (v != lit || useGreen != green)
        {
            lit = v;
            green = useGreen;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto colour = lit ? (green ? t.play : t.accent) : t.line;
        g.setColour (colour);
        const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ 8.0f, (float) getHeight() * 0.5f });
        g.fillEllipse (dot);
        if (lit && t.glow > 0.0f)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (dot.expanded (3.0f));
        }
        g.setColour (lit ? t.text : t.dimText);
        g.setFont (t.uiFont (10.5f, 500));
        g.drawText (text, getLocalBounds().withTrimmedLeft (18), juce::Justification::centredLeft, false);
    }

private:
    juce::String text;
    bool lit = false, green = false;
};

//==============================================================================
class TopBar::CpuMeter final : public juce::Component
{
public:
    CpuMeter()
    {
        setHelpText (Strings::help::cpu);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setLoad (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (std::abs (v - load) > 0.004f)
        {
            load = v;
            repaint();
        }
    }

    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        if (t.glass)
        {
            // glass: "CPU 14%" and a slim bar
            g.setColour (t.text);
            g.setFont (t.uiFont (12.0f, 500));
            const auto label = Strings::transport::cpu + " " + juce::String (juce::roundToInt (load * 100.0f)) + "%";
            g.drawText (label, getLocalBounds().removeFromLeft (64), juce::Justification::centredLeft, false);
            const auto bar = juce::Rectangle<float> ((float) getWidth() - 64.0f, (float) getHeight() * 0.5f - 3.0f, 60.0f, 6.0f);
            g.setColour (t.line.withAlpha (0.7f));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (load > 0.85f ? t.record : t.play);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * load)), 3.0f);
            return;
        }
        g.setColour (t.dimText);
        g.setFont (t.uiFont (10.5f, 500));
        g.drawText (Strings::transport::cpu, getLocalBounds().removeFromLeft (26), juce::Justification::centredLeft, false);
        const auto bar = juce::Rectangle<float> (46.0f, 6.0f).withCentre ({ 31.0f + 23.0f, (float) getHeight() * 0.5f });
        g.setColour (t.panel2);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (load > 0.85f ? t.record : t.play);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * load)), 3.0f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

private:
    float load = 0.0f;
};

//==============================================================================
// A small disk that shows for a moment when the disk could not keep up with playback (AudioEngine::getDiskMisses).
class TopBar::DiskLamp final : public juce::Component
{
public:
    DiskLamp()
    {
        setHelpText (Strings::sessionAudio::diskBehind); // shown in the info bar like every other hint
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto box = juce::Rectangle<float> (14.0f, 14.0f).withCentre (getLocalBounds().toFloat().getCentre());
        juce::Path body; // a floppy disk: a square with a cut corner, a shutter on top and a label below
        body.startNewSubPath (box.getX(), box.getY());
        body.lineTo (box.getRight() - 3.5f, box.getY());
        body.lineTo (box.getRight(), box.getY() + 3.5f);
        body.lineTo (box.getRight(), box.getBottom());
        body.lineTo (box.getX(), box.getBottom());
        body.closeSubPath();
        g.setColour (t.warning);
        g.strokePath (body, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
        g.fillRect (box.getX() + 3.5f, box.getY(), 5.0f, 3.5f);
        g.drawRect (juce::Rectangle<float> (box.getX() + 3.0f, box.getBottom() - 5.5f, box.getWidth() - 6.0f, 5.5f), 1.2f);
    }
};

//==============================================================================
// The A <-> B morph slider; it only looks like the final control (stage 6).
class TopBar::FakeSlider final : public juce::Component
{
public:
    FakeSlider() { setHelpText (Strings::help::morph); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const float alpha = 0.55f;
        auto r = getLocalBounds().toFloat();
        auto letter = [&] (juce::Rectangle<float> box, const juce::String& s) {
            g.setColour (t.panel2.withAlpha (alpha));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (t.line);
            g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (t.dimText);
            g.setFont (t.uiFont (11.0f, 600));
            g.drawText (s, box.toNearestInt(), juce::Justification::centred, false);
        };
        const auto a = r.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
        const auto b = r.removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
        letter (a, "A");
        letter (b, "B");
        const auto track = r.reduced (7.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 14.0f, 4.0f);
        g.setColour (t.line);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (t.accent2.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ track.getX() + 6.0f, track.getCentreY() }));
    }
};

//==============================================================================
class TopBar::Tabs final : public juce::Component, public HelpProvider, public juce::DragAndDropTarget, private juce::Timer
{
public:
    // Session | Arrangement | Mix. The window switches the views; this only draws which one is shown.
    enum View { session = 0, arrangementView = 1, mix = 2 };

    Tabs() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    // A Session View clip dragged onto "Arrangement" switches the view after a short hold; the drag then goes on over the
    // timeline, which is a drop target of its own.
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        int64_t track = 0;
        int scene = 0;
        return ArrangementView::parseSessionClip (d.description, track, scene);
    }
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails& d) override
    {
        const bool overArrangement = segmentAt (d.localPosition.x) == arrangementView && view != arrangementView;
        if (overArrangement != hovering)
        {
            hovering = overArrangement;
            hovering ? startTimer (350) : stopTimer();
            repaint();
        }
    }
    void itemDragExit (const SourceDetails&) override { clearHover(); }
    void itemDropped (const SourceDetails&) override { clearHover(); }
    void clearHover()
    {
        hovering = false;
        stopTimer();
        repaint();
    }
    void timerCallback() override
    {
        stopTimer();
        hovering = false;
        if (onSwitch)
            onSwitch (arrangementView);
    }

    std::function<void (int view)> onSwitch;

    std::function<bool()> animationsEnabled; // Settings → animations; the lens jumps when they are off

    void setView (int v)
    {
        if (v == view)
            return;
        from = slidePosition();
        view = v;
        if (animationsEnabled != nullptr && ! animationsEnabled())
        {
            sliding = false;
            repaint();
            return;
        }
        slideStart = juce::Time::getMillisecondCounterHiRes();
        if (slide == nullptr)
            slide = std::make_unique<juce::VBlankAttachment> (this, [this] {
                // kept alive between slides (destroying it inside its own callback is not allowed); idle once settled
                if (! sliding)
                    return;
                sliding = slideProgress() < 1.0f;
                repaint();
            });
        sliding = true;
        repaint();
    }

    juce::String helpAt (juce::Point<int> p) const override
    {
        const int s = segmentAt (p.x);
        return s == session ? Strings::help::tabSession : (s == arrangementView ? Strings::help::tabArrangement : Strings::help::tabMix);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int s = segmentAt (e.x);
        if (s != view && onSwitch)
            onSwitch (s);
    }

    void paint (juce::Graphics& g) override
    {
        glass::Lit lit (*this);
        const float p = slideProgress();
        kit::segmented (g, getLocalBounds().toFloat().reduced (0.5f),
                        { Strings::transport::sessionTab, Strings::transport::arrangementTab, Strings::transport::mixTab },
                        view, -1, hovering ? (int) arrangementView : -1, slidePosition(), std::sin (juce::MathConstants<float>::pi * p), true);
    }

private:
    static constexpr double kSlideMs = 320.0;
    int segmentAt (int x) const { return juce::jlimit (0, 2, x * 3 / juce::jmax (1, getWidth())); }
    float slideProgress() const
    {
        return ! sliding ? 1.0f : (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - slideStart) / kSlideMs);
    }
    // where the lens is now: eased out with a slight overshoot, like glass that slides and settles
    float slidePosition() const
    {
        const float p = slideProgress();
        if (p >= 1.0f)
            return (float) view;
        const float c = 1.4f, q = p - 1.0f;
        const float eased = 1.0f + (c + 1.0f) * q * q * q + c * q * q;
        return from + ((float) view - from) * eased;
    }
    std::unique_ptr<juce::VBlankAttachment> slide;
    bool sliding = false;
    double slideStart = 0.0;
    float from = 0.0f;
    int view = session;
    bool hovering = false;
};

//==============================================================================
TopBar::TopBar (AudioEngine& e, ProjectModel& m) : engine (e), model (m)
{
    setHelpText (Strings::help::logo);

    tempo.setRange (20.0, 999.0, 0.01);
    tempo.setValue (model.tempo());
    tempo.setDecimals (2);
    tempo.setDragSensitivity (0.5);
    tempo.setWheelStep (0.1);
    tempo.setHelpText (Strings::help::tempo);
    tempo.setBoxedInGlass (false);
    tempo.setInterfaceFontInGlass (true);
    // With a tempo envelope on the master the field scales the whole envelope: every point by the same factor.
    tempo.onGestureStart = [this] {
        model.undo.beginNewTransaction (Strings::undoTempo);
        tempoScalePoints.clear();
        tempoScaleFrom = tempo.getValue();
        const auto lane = automation::findLane (model.masterTrack(), automation::tempoTarget);
        if (lane.isValid())
            for (const auto& point : lane)
                tempoScalePoints.push_back ({ point, automation::tempoFromNormalised ((float) point.getProperty (ids::value)) });
    };
    tempo.onValueChange = [this] (double) {
        if (tempoScalePoints.empty() || tempoScaleFrom <= 0.0)
        {
            model.setTempo (tempo.getValue());
            return;
        }
        const double factor = tempo.getValue() / tempoScaleFrom;
        for (const auto& [point, bpm] : tempoScalePoints)
            if (point.getParent().isValid())
                automation::movePoint (model, point, (double) point.getProperty (ids::beat), automation::normalisedTempo (bpm * factor));
    };
    tempo.onGestureEnd = [this] { tempoScalePoints.clear(); };
    addAndMakeVisible (tempo);

    namespace I = icons;
    using Role = IconButton::Role;
    metronome = std::make_unique<IconButton> (I::metronome, juce::String(), Role::normal, Strings::help::metronome);
    play = std::make_unique<IconButton> (I::play, juce::String(), Role::play, Strings::help::play);
    stop = std::make_unique<IconButton> (I::stop, juce::String(), Role::normal, Strings::help::stop);
    record = std::make_unique<IconButton> (I::record, juce::String(), Role::record, Strings::help::record);
    stop->bare = record->bare = true; // plain icons in the row, as in the design; a pane appears when touched or on
    overdub = std::make_unique<IconButton> (nullptr, Strings::recording::ovr, Role::normal, Strings::help::overdub);
    loop = std::make_unique<IconButton> (I::loop, juce::String(), Role::normal, Strings::help::loop);
    punchIn = std::make_unique<IconButton> (nullptr, Strings::arrRec::punchIn, Role::record, Strings::help::punchIn);
    punchOut = std::make_unique<IconButton> (nullptr, Strings::arrRec::punchOut, Role::record, Strings::help::punchOut);
    follow = std::make_unique<IconButton> (I::follow, juce::String(), Role::normal, Strings::help::followPlayhead);
    backToArrangement = std::make_unique<IconButton> (I::backToArrangement, juce::String(), Role::warning, Strings::help::backToArrangement);
    capture = std::make_unique<IconButton> (nullptr, Strings::transport::capture, Role::normal, Strings::help::ribbon);
    version = std::make_unique<IconButton> (nullptr, Strings::transport::versionNone, Role::normal, Strings::help::version);
    appearanceButton =
      std::make_unique<IconButton> (nullptr, Strings::settingsui::tabAppearance, Role::normal, Strings::menu::settings);
    exportButton =
      std::make_unique<IconButton> (nullptr, Strings::menu::exportAudio, Role::normal, Strings::menu::exportAudio);
    appearanceButton->onClick = [this]
    {
        if (onAppearanceClicked)
            onAppearanceClicked ();
    };
    exportButton->onClick = [this]
    {
        if (onExportClicked)
            onExportClicked ();
    };
    addAndMakeVisible (*appearanceButton);
    addAndMakeVisible (*exportButton);
    exportButton->icon = exportIcon;
    exportButton->iconSize = 14.0f;
    exportButton->text = Strings::transport::exportShort;
    exportButton->setHelpText (Strings::help::exportButton);
    undoButton = std::make_unique<IconButton> ([] (juce::Rectangle<float> b) { return undoIcon (b, false); }, juce::String(), Role::normal, Strings::help::undoButton);
    redoButton = std::make_unique<IconButton> ([] (juce::Rectangle<float> b) { return undoIcon (b, true); }, juce::String(), Role::normal, Strings::help::redoButton);
    settingsButton = std::make_unique<IconButton> (gearIcon, juce::String(), Role::normal, Strings::help::settingsButton);
    libraryButton = std::make_unique<IconButton> (libraryIcon, Strings::transport::library, Role::normal, Strings::help::libraryButton);
    for (auto* b : { undoButton.get(), redoButton.get(), settingsButton.get(), libraryButton.get() })
    {
        b->iconSize = 15.0f;
        addAndMakeVisible (*b);
    }
    undoButton->onClick = [this] { if (onUndoClicked) onUndoClicked(); };
    redoButton->onClick = [this] { if (onRedoClicked) onRedoClicked(); };
    settingsButton->onClick = [this] { if (onSettingsClicked) onSettingsClicked(); };
    libraryButton->onClick = [this] { if (onLibraryClicked) onLibraryClicked(); };
    libraryButton->leftAligned = true;
    appearanceButton->setVisible (false); // Settings (the gear) opens Appearance among the other tabs
    scaleButton = std::make_unique<IconButton> (nullptr, Strings::transport::scaleOff, Role::normal, Strings::help::scaleButton);
    scaleButton->onClick = [this] {
        showScalePicker (*scaleButton, model.scaleRoot(), model.scaleMode(), [this] (int root, const juce::String& mode) {
            model.undo.beginNewTransaction (Strings::undoProjectScale);
            model.setScale (root, mode);
        });
    };
    loop->onClick = [this] {
        model.undo.beginNewTransaction (Strings::undoArrLoop);
        model.setArrangementLoopOn (! model.arrangementLoopOn());
    };
    punchIn->onClick = [this] { model.setPunchIn (! model.punchIn()); };
    punchOut->onClick = [this] { model.setPunchOut (! model.punchOut()); };
    follow->onClick = [this] { model.setFollowPlayhead (! model.followPlayhead()); };
    backToArrangement->onClick = [this] { engine.backToArrangement(); };
    metronome->onClick = [this] { model.setMetronome (! model.metronome()); };
    metronome->onPopup = [this] {
        makeMetronomeMenu (model).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (metronome.get()));
    };
    record->onClick = [this] {
        if (onRecordClicked)
            onRecordClicked();
    };
    record->onPopup = [this] {
        auto menu = makeRecordMenu (model);
        menu.addSeparator();
        menu.addItem (Strings::arrRec::menuToArrangement, [this] {
            if (onRecordToArrangement)
                onRecordToArrangement();
        });
        menu.addItem (Strings::arrRec::menuToSlot, [this] {
            if (onRecordToSlot)
                onRecordToSlot();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (record.get()));
    };
    overdub->onClick = [this] { model.setOverdub (! model.overdub()); };
    capture->onClick = [this] {
        if (onCaptureClicked)
            onCaptureClicked();
    };
    version->onClick = [this] {
        if (onVersionClicked)
            onVersionClicked();
    };
    play->onClick = [this] {
        if (engine.isPlaying())
            engine.stop();
        else
            engine.play();
    };
    // A second press while stopped brings the song back to its start (and the play start with it).
    stop->onClick = [this] {
        if (engine.isPlaying())
            engine.stop();
        else
        {
            engine.rewindSong();
            model.setArrangementPlayStart (0.0);
        }
    };

    morph = std::make_unique<FakeSlider>();
    spectrum = std::make_unique<Spectrum> (engine);
    addChildComponent (*spectrum);
    tabs = std::make_unique<Tabs>();
    tabs->animationsEnabled = [this] { return engine.getSettings().animations(); };
    tabs->onSwitch = [this] (int view) {
        if (view == Tabs::mix)
        {
            if (onMixClicked)
                onMixClicked();
        }
        else if (onViewSwitched)
            onViewSwitched (view == Tabs::arrangementView);
    };
    midiLamp = std::make_unique<Lamp> (Strings::transport::midi, Strings::help::midiIn);
    keysLamp = std::make_unique<Lamp> (Strings::transport::keysOn, Strings::help::keysIndicator);
    diskLamp = std::make_unique<DiskLamp>();
    lastDiskMisses = engine.getDiskMisses();
    cpu = std::make_unique<CpuMeter>();
    cpu->onClick = [this] {
        if (onCpuClicked)
            onCpuClicked();
    };

    for (auto* c : std::initializer_list<juce::Component*> { metronome.get(), play.get(), stop.get(), record.get(), overdub.get(), loop.get(), punchIn.get(), punchOut.get(), follow.get(), backToArrangement.get(), capture.get(),
                                                              version.get(), scaleButton.get(), morph.get(), tabs.get(), midiLamp.get(), keysLamp.get(), diskLamp.get(), cpu.get() })
        addAndMakeVisible (c);
    diskLamp->setVisible (false);
    lastMidiCount = engine.getHardwareMidiCount();
    tick();
}

TopBar::~TopBar() = default;

void TopBar::setVersionText (const juce::String& text)
{
    if (version->text != text)
    {
        version->text = text;
        version->repaint();
    }
}

void TopBar::mouseUp (const juce::MouseEvent& e)
{
    if (nameArea.contains (e.getPosition()) && onProjectClicked)
        onProjectClicked();
}

void TopBar::setArrangementShown (bool shown)
{
    tabs->setView (shown ? Tabs::arrangementView : Tabs::session);
}

void TopBar::setMixShown()
{
    tabs->setView (Tabs::mix);
}

void TopBar::setRecorder (SessionRecorder* r)
{
    recorder = r;
    ribbonVersion = r != nullptr ? r->getCaptureVersion() - 1 : 0;
    updateRibbon();
}

void TopBar::setComputerKeysOn (bool on)
{
    keysLamp->setLit (on, true);
}

void TopBar::setKeysOctave (const juce::String& octave)
{
    keysLamp->setText (Strings::transport::keysOn + " " + octave);
}

void TopBar::tick()
{
    spectrum->tick();
    const bool playing = engine.isPlaying();
    play->setOn (playing);

    // With a tempo envelope on the master the field shows the tempo at the cursor and cannot be edited.
    const auto tempoLane = automation::findLane (model.masterTrack(), automation::tempoTarget);
    const bool mapped = tempoLane.isValid() && tempoLane.getNumChildren() > 0;
    if (mapped != tempoMapped)
    {
        tempoMapped = mapped;
        tempo.setHelpText (mapped ? Strings::help::tempoMapped : Strings::help::tempo);
        tempo.repaint();
    }
    const double shownTempo = ! mapped ? model.tempo()
                              : playing ? engine.getCurrentTempo()
                                        : automation::tempoFromNormalised (daw::engine::automationValueAt (automation::points (tempoLane), engine.getSongPositionBeats()));
    if (! tempo.isEditing() && std::abs (tempo.getValue() - shownTempo) > 0.004)
        tempo.setValue (shownTempo);

    // The song position (the arrangement's); the count-in shows its own run from -4 to 0. 6/8 is three quarter notes long.
    const auto text = formatPosition (engine.isCountingIn() ? engine.getPositionBeats() : engine.getSongPositionBeats(), model.beatsPerBar());
    if (text != positionText)
    {
        positionText = text;
        repaint (positionArea);
    }

    metronome->setOn (model.metronome());
    overdub->setOn (model.overdub());
    loop->setOn (model.arrangementLoopOn());
    punchIn->setOn (model.punchIn());
    punchOut->setOn (model.punchOut());
    follow->setOn (model.followPlayhead());
    {
        const bool inSession = engine.anyTrackInSession();
        backToArrangement->setOn (inSession);
        backToArrangement->setEnabled (inSession);
    }
    {
        // Lit while a recording runs, blinking while one waits for its start or the count-in plays.
        bool recording = false, waiting = engine.isCountingIn();
        if (recorder != nullptr && recorder->arrangement().isActive())
            (recorder->arrangement().isWaiting() ? waiting : recording) = true; // recording onto the timeline (F9)
        if (recorder != nullptr)
            for (const auto& track : model.tracks())
            {
                SessionRecorder::Progress p;
                if (recorder->getProgress (track.id(), p))
                    (p.waiting ? waiting : recording) = true;
                else if (recorder->isTrackRecording (track.id()))
                    waiting = true;
            }
        const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0 || ! engine.getSettings().animations();
        recordLit = recording || (waiting && blink);
        record->setOn (recordLit);
    }
    updateRibbon();

    const auto count = engine.getHardwareMidiCount();
    if (count != lastMidiCount)
    {
        lastMidiCount = count;
        midiFlashUntil = juce::Time::getMillisecondCounter() + 120;
    }
    midiLamp->setLit (juce::Time::getMillisecondCounter() < midiFlashUntil, false);
    const auto label = scaleLabel (model.scaleRoot(), model.scaleMode());
    if (label != scaleText)
    {
        scaleText = label;
        scaleButton->text = label;
        scaleButton->accentInk = model.scaleMode().isNotEmpty();
        scaleButton->repaint();
    }
    {
        // The icon stays for two seconds after the last miss, so a glitch that is over before the eye turns still shows.
        const auto misses = engine.getDiskMisses();
        const auto now = juce::Time::getMillisecondCounter();
        if (misses != lastDiskMisses)
        {
            lastDiskMisses = misses;
            diskShownUntil = now + 2000;
        }
        diskLamp->setVisible (now < diskShownUntil);
    }
    cpu->setLoad ((float) engine.getDeviceManager().getCpuUsage());
    wasPlaying = playing;
}

void TopBar::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.glass ? t.background : t.chrome); // glass: the window background, the capsule and cards float on it
    if (! t.glass)
    {
        g.setColour (t.line);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    if (t.glass)
    {
        if (! libraryPlate.isEmpty ())
            glass::capsule (g, libraryPlate.toFloat ());
        glass::capsule (g, capsuleArea.toFloat ());
        g.setColour (t.line.withAlpha (0.8f));
        for (const int x : separators)
            g.fillRect ((float) x, (float) capsuleArea.getY () + 11.0f, 1.0f, (float) capsuleArea.getHeight () - 22.0f);
    }
    if (t.glass)
        ui::drawAppIcon (g, logoArea.toFloat()); // glass: the icon tile, as on the dock
    else
        ui::drawLogo (g, logoArea.toFloat());
    g.setColour (t.text);
    g.setFont (t.uiFont (22.0f, 600));
    g.drawText (Strings::labelAppName, juce::Rectangle<int> (logoArea.getRight() + 9, 9, 96, 34), juce::Justification::centredLeft, false);
    // the project name, after a thin divider, with a chevron: a click opens the project properties
    g.setColour (t.line);
    g.fillRect (nameArea.getX() - 14, 17, 1, 18);
    g.setColour (t.text);
    g.setFont (t.uiFont (t.glass ? 16.0f : 15.0f, 500));
    const float nameWidth = juce::jmin ((float) nameArea.getWidth() - 18.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), projectName) + 2.0f);
    g.drawFittedText (projectName, nameArea.withWidth (juce::roundToInt (nameWidth)), juce::Justification::centredLeft, 1, 1.0f);
    {
        juce::Path v;
        const juce::Point<float> c ((float) nameArea.getX() + nameWidth + 10.0f, (float) nameArea.getCentreY() + 1.0f);
        v.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
        v.lineTo (c.x, c.y + 2.0f);
        v.lineTo (c.x + 4.0f, c.y - 2.0f);
        g.setColour (t.dimText);
        g.strokePath (v, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto field = [&] (juce::Rectangle<int> area, const juce::String& text) {
        if (! t.glass)
        {
            fillCell (g, area.toFloat(), t.panel);
            strokeCell (g, area.toFloat(), t.line);
        }
        g.setColour (t.text);
        // glass: the numbers in the interface face, a size up, as the design sets them ("33.1.1", "4/4")
        g.setFont (t.glass ? t.uiFont (15.5f, 500) : t.numFont (13.0f));
        g.drawText (t.glass ? text.removeCharacters (" ") : text, area, juce::Justification::centred, false);
    };
    const int num = model.sigNum(), den = model.sigDen();
    field (signatureArea, juce::String (num) + " / " + juce::String (den));
    field (positionArea, positionText);

    if (! ribbonArea.isEmpty())
        paintRibbon (g);
}

void TopBar::updateRibbon()
{
    if (recorder == nullptr || ribbonStripArea.isEmpty())
        return;
    // 15 frames a second at most, and only when new notes arrived or the strip has moved by a pixel
    if ((++tickCounter & 1) != 0)
        return;
    const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 44100.0;
    const int64_t pixel = (int64_t) std::floor ((double) engine.getEngineSample() / sr * (double) ribbonStripArea.getWidth()
                                                / daw::engine::CaptureBuffer::kWindowSeconds);
    const auto captureVersion = recorder->getCaptureVersion();
    if (captureVersion == ribbonVersion && pixel == ribbonPixel)
        return;
    if (captureVersion != ribbonVersion)
    {
        std::vector<SessionRecorder::NoteMark> marks;
        recorder->getRecentNoteOns (marks);
        markSamples.clear();
        markVelocities.clear();
        markTracks.clear();
        for (const auto& m : marks)
        {
            markSamples.push_back (m.engineSample);
            markVelocities.push_back (m.velocity);
            markTracks.push_back (m.trackId);
        }
    }
    ribbonVersion = captureVersion;
    ribbonPixel = pixel;
    repaint (ribbonStripArea);
}

void TopBar::paintRibbon (juce::Graphics& g)
{
    // The "10 min" capture ribbon: the last ten minutes of playing, newest at the right edge. Every note-on is a
    // stroke in the colour of its track, as tall as it was loud; the red line is "now".
    const auto& t = theme();
    const auto r = ribbonArea.toFloat();
    if (! t.glass)
    {
        fillCell (g, r, t.panel);
        strokeCell (g, r, t.line);
    }
    g.setColour (t.dimText);
    g.setFont (t.uiFont (10.5f, 500));
    g.drawText (Strings::transport::capture10.toUpperCase(), ribbonArea.withTrimmedLeft (9).withWidth (46), juce::Justification::centredLeft, false);

    const auto strip = ribbonStripArea.toFloat();
    g.setColour (t.panel2.withAlpha (0.7f));
    g.fillRoundedRectangle (strip, 3.0f);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (ribbonStripArea);
        const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 44100.0;
        const double now = (double) engine.getEngineSample();
        const double window = daw::engine::CaptureBuffer::kWindowSeconds * sr;
        const float right = strip.getRight() - 2.0f, width = strip.getWidth() - 2.0f;
        const float maxHeight = strip.getHeight() - 4.0f;
        for (size_t i = 0; i < markSamples.size(); ++i)
        {
            const double age = now - (double) markSamples[i];
            if (age < 0.0 || age > window)
                continue;
            const float x = right - (float) (age / window) * width;
            const float h = 3.0f + (float) juce::jlimit (1, 127, markVelocities[i]) / 127.0f * (maxHeight - 3.0f);
            const auto track = markTracks[i] != 0 ? model.findTrack (markTracks[i]) : TrackRef();
            g.setColour (track.isValid() ? t.palette (track.colour()) : t.dimText);
            g.fillRect (x, strip.getCentreY() - h * 0.5f, 1.5f, h);
        }
        g.setColour (t.record);
        g.fillRect (strip.getRight() - 2.0f, strip.getY(), 2.0f, strip.getHeight());
    }
}

void TopBar::resized()
{
    // Two rows. The upper one: the mark and the project name, the Session/Arrangement switch in the middle, then the
    // version, undo/redo, export and settings on the right. The lower one: the Library button over the browser column
    // and a capsule holding the transport in groups, with the lamps and the CPU at its right end; the ribbon and the
    // spectrum take the room left in between.
    constexpr int h = 30, gap = 6, groupGap = 15, cy = 78;
    logoArea = theme ().glass ? juce::Rectangle<int> (14, 8, 38, 38) : juce::Rectangle<int> (16, 10, 34, 34);
    tabs->setBounds (getWidth () / 2 - 165, 9, 330, 34);
    nameArea = { logoArea.getRight () + 9 + 96 + 14, 9, juce::jmax (40, tabs->getX () - (logoArea.getRight () + 9 + 96 + 14) - 12), 34 };
    settingsButton->setBounds (getWidth () - 14 - 34, 10, 34, 32);
    exportButton->setBounds (settingsButton->getX () - 8 - 104, 10, 104, 32);
    redoButton->setBounds (exportButton->getX () - 10 - 32, 10, 32, 32);
    undoButton->setBounds (redoButton->getX () - 2 - 32, 10, 32, 32);
    version->setBounds (undoButton->getX () - 10 - 96, 10, 96, 32);
    appearanceButton->setBounds (version->getX (), 10, 0, 0);
    for (auto* b : { exportButton.get (), version.get (), capture.get (), libraryButton.get () })
        b->framed = true;
    // glass: the capture button carries a red dot, as in the design
    capture->icon = theme ().glass ? std::function<juce::Path (juce::Rectangle<float>)> ([] (juce::Rectangle<float> b) {
        juce::Path p;
        p.addEllipse (b.reduced (1.0f));
        return p;
    }) : nullptr;
    capture->iconSize = theme ().glass ? 10.0f : 0.0f;
    capture->iconColour = theme ().record;
    scaleButton->chevron = version->chevron = true;
    // the top row's icons are plain until hovered
    for (auto* b : { undoButton.get (), redoButton.get (), settingsButton.get () })
        b->framed = false;

    // The transport needs about 1090 px; below that the Library button shrinks to its icon and gives the capsule room.
    const int libraryFull = juce::jmax (110, leftColumn - 10);
    const bool libraryCompact = getWidth () - 10 - (10 + libraryFull + 8) < 1090;
    libraryButton->text = libraryCompact ? juce::String () : Strings::transport::library;
    // glass: a plate the width of the library column, the Library button a pane resting on it, as in the design
    const bool plated = theme ().glass && ! libraryCompact;
    libraryPlate = plated ? juce::Rectangle<int> (10, cy - 21, libraryFull, 42) : juce::Rectangle<int>();
    if (plated)
        libraryButton->setBounds (libraryPlate.getX () + 6, cy - 15, 112, 30);
    else
        libraryButton->setBounds (10, cy - 20, libraryCompact ? 40 : libraryFull, 40);
    const int capsuleLeft = juce::jmax (libraryButton->getRight (), libraryPlate.getRight ()) + 8;
    capsuleArea = { capsuleLeft, cy - 21, getWidth () - 10 - capsuleLeft, 42 };

    separators.clear ();
    int x = capsuleArea.getX () + 12;
    // A local cannot be a default argument, so 0 stands for the row height.
    auto place = [&] (juce::Component& c, int width, int height = 0)
    {
        if (height == 0)
            height = h;
        c.setBounds (x, cy - height / 2, width, height);
        x += width + gap;
    };
    auto separate = [&]
    {
        x += groupGap - gap;
        separators.push_back (x - groupGap / 2 - 1);
    };
    place (*stop, h);
    place (*play, 36, 36);
    place (*record, h);
    place (*overdub, 40);
    separate ();
    positionArea = { x, cy - h / 2, 80, h };
    x += 80 + gap;
    place (tempo, 70);
    signatureArea = { x, cy - h / 2, 48, h };
    x += 48 + gap;
    separate ();
    place (*scaleButton, 100);
    place (*metronome, h);
    place (*loop, h);
    place (*punchIn, 28);
    place (*punchOut, 28);
    place (*follow, h);
    place (*backToArrangement, h);
    separate ();

    auto right = capsuleArea.getRight () - 12;
    auto take = [&] (juce::Component& c, int width)
    {
        right -= width;
        c.setBounds (right, cy - 10, width, 20);
        right -= gap;
    };
    take (*cpu, theme ().glass ? 112 : 80);
    take (*diskLamp, 20);
    take (*keysLamp, 78);
    take (*midiLamp, 48);
    right -= gap;

    ribbonArea = {};
    ribbonStripArea = {};
    capture->setVisible (false);
    morph->setVisible (false);
    spectrum->setVisible (false);
    if (right - x >= 286 + 120 + groupGap)
    {
        ribbonArea = { x, cy - h / 2, 286, h };
        ribbonStripArea = { x + 58, cy - 10, 130, 20 };
        capture->setVisible (true);
        capture->setBounds (x + 198, cy - 13, 82, 26);
        x += 286;
        separate ();
    }
    else if (right - x >= 82 + 60 + groupGap)
    {
        capture->setVisible (true);
        capture->setBounds (x, cy - 13, 82, 26);
        x += 82;
        separate ();
    }
    if (right - x >= 60)
    {
        spectrum->setVisible (true);
        spectrum->setBounds (x, cy - 15, juce::jmin (right - x, 300), 30);
    }
}
} // namespace ui
