#include "TopBar.h"

#include "../Strings.h"
#include "Chrome.h"
#include "ScalePicker.h"
#include "../engine/AudioEngine.h"
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
    float iconSize = 0.0f;  // 0: the default size
    bool leftAligned = false; // icon and label start at the left edge (the Library button)
    std::function<void()> onClick;
    std::function<void()> onPopup; // right mouse button

    IconButton (std::function<juce::Path (juce::Rectangle<float>)> iconFn, const juce::String& label, Role r, const juce::String& help)
        : icon (std::move (iconFn)), text (label), role (r)
    {
        setHelpText (help);
    }

    void setOn (bool v)
    {
        if (on != v)
        {
            on = v;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        juce::Colour fill = t.panel, ink = t.text, border = t.line;
        if (role == Role::record)
            ink = t.record;
        else if (accentInk)
            ink = t.accent;
        if (on)
        {
            const auto accent = role == Role::play ? t.play : (role == Role::record ? t.record : (role == Role::warning ? t.warning : t.accent));
            fill = accent;
            border = accent;
            ink = t.inkOn (accent);
            if ((role == Role::play || role == Role::warning) && t.glow > 0.0f)
                juce::DropShadow (accent.withAlpha (0.6f), juce::roundToInt (14.0f * t.glow), {}).drawForRectangle (g, getLocalBounds().reduced (3));
        }
        else if (isMouseOver() && isEnabled())
            fill = t.panel2;
        if (! isEnabled())
        {
            fill = fill.withMultipliedAlpha (0.5f);
            ink = t.dimText.withAlpha (0.6f);
            border = border.withMultipliedAlpha (0.6f);
        }
        if (t.glass && role == Role::play && icon && text.isEmpty())
        {
            // Glass: play is a round green button at all times, glowing while the song plays.
            const auto circle = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
            juce::DropShadow (t.play.withAlpha (on ? 0.55f : 0.25f), on ? 12 : 6, { 0, 1 }).drawForRectangle (g, circle.toNearestInt().reduced (3));
            g.setGradientFill (juce::ColourGradient (t.play.interpolatedWith (t.highlight(), 0.25f), circle.getX(), circle.getY(),
                                                     t.play.interpolatedWith (t.shadow(), 0.12f), circle.getX(), circle.getBottom(), false));
            g.fillEllipse (circle);
            g.setColour (t.highlight().withAlpha (0.55f));
            g.drawEllipse (circle.reduced (0.75f), 1.0f);
            g.setColour (juce::Colours::white);
            const float s = circle.getHeight() * 0.36f;
            g.fillPath (icon (juce::Rectangle<float> (s, s).withCentre (circle.getCentre().translated (s * 0.08f, 0.0f))));
            return;
        }
        if (t.glass)
        {
            // Inside the transport capsule a button is only its icon until it is lit or under the mouse.
            const float pill = r.getHeight () * 0.5f;
            if (on)
                glass::accent (g, r, fill, pill);
            else if (framed)
            {
                glass::surface (g, r, fill, pill);
                g.setColour (border.withMultipliedAlpha (0.8f));
                g.drawRoundedRectangle (r, pill, 1.0f);
            }
            else if (isMouseOver () && isEnabled ())
            {
                g.setColour (t.panel2.withAlpha (0.8f));
                g.fillRoundedRectangle (r, pill);
            }
        }
        else
        {
            fillCell (g, r, fill);
            strokeCell (g, r, border);
        }

        g.setColour (ink);
        const auto font = t.uiFont (getWidth() < 34 ? 10.5f : (framed && t.glass ? 13.5f : 12.5f), 500);
        const float s = iconSize > 0.0f ? iconSize : (text.isEmpty() ? 11.0f : 10.0f);
        if (icon && text.isNotEmpty() && iconSize > 0.0f)
        {
            // icon and label as one group: centred, or from the left edge
            const float textWidth = juce::GlyphArrangement::getStringWidth (font, text);
            const float groupWidth = s + 9.0f + textWidth;
            const float x0 = leftAligned ? r.getX() + 12.0f : r.getCentreX() - groupWidth * 0.5f;
            g.fillPath (icon (juce::Rectangle<float> (s, s).withCentre ({ x0 + s * 0.5f, r.getCentreY() })));
            g.setFont (font);
            g.drawText (text, juce::Rectangle<float> (x0 + s + 9.0f, r.getY(), textWidth + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
            return;
        }
        if (icon)
        {
            const auto box = juce::Rectangle<float> (s, s).withCentre (text.isEmpty() ? r.getCentre() : juce::Point<float> (r.getX() + 8.0f + s * 0.5f, r.getCentreY()));
            g.fillPath (icon (box));
        }
        if (text.isNotEmpty())
        {
            g.setFont (font);
            const int iconRoom = icon ? juce::roundToInt (s + 12.0f) : 0;
            g.drawText (text, getLocalBounds().withTrimmedLeft (iconRoom), juce::Justification::centred, false);
        }
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
        const bool overArrangement = d.localPosition.x >= getWidth() / 2 && ! arrangement;
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
            onSwitch (true);
    }

    std::function<void (bool arrangement)> onSwitch;

    void setArrangement (bool a)
    {
        if (a != arrangement)
        {
            arrangement = a;
            repaint();
        }
    }

    juce::String helpAt (juce::Point<int> p) const override
    {
        return p.x < getWidth() / 2 ? Strings::help::tabSession : Strings::help::tabArrangement;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const bool right = e.x >= getWidth() / 2;
        if (right != arrangement && onSwitch)
            onSwitch (right);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (t.glass)
        {
            // A sunken pill track; the shown view is a raised glass pill in it.
            const float pill = r.getHeight() * 0.5f;
            g.setColour (t.panel2.withAlpha (t.isLight() ? 0.85f : 0.7f));
            g.fillRoundedRectangle (r, pill);
            g.setColour (t.line.withAlpha (0.6f));
            g.drawRoundedRectangle (r, pill, 1.0f);
            const auto half = r.getWidth() * 0.5f;
            const auto active = (arrangement ? r.withTrimmedLeft (half) : r.withWidth (half)).reduced (3.0f);
            juce::DropShadow (t.shadow().withAlpha (t.isLight() ? 0.12f : 0.4f), 6, { 0, 1 }).drawForRectangle (g, active.toNearestInt());
            glass::surface (g, active, t.isLight() ? t.highlight() : t.panel, active.getHeight() * 0.5f);
            for (int i = 0; i < 2; ++i)
            {
                const bool on = (i == 1) == arrangement;
                g.setColour (on ? t.text : t.dimText);
                g.setFont (t.uiFont (14.0f, on ? 600 : 500));
                g.drawText (i == 0 ? Strings::transport::sessionTab : Strings::transport::arrangementTab,
                            (i == 0 ? r.withWidth (half) : r.withTrimmedLeft (half)).toNearestInt(), juce::Justification::centred, false);
            }
            if (hovering)
            {
                g.setColour (t.accent.withAlpha (0.2f));
                g.fillRoundedRectangle (r.withTrimmedLeft (half), pill);
            }
            return;
        }
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, t.radius);
        auto left = r.removeFromLeft (r.getWidth() * 0.5f);
        const auto drawTab = [&] (juce::Rectangle<float> area, const juce::String& text, bool active) {
            if (active)
            {
                g.setColour (t.panel2);
                g.fillRoundedRectangle (area, t.radius);
                g.setColour (t.accent);
                g.fillRect (area.removeFromBottom (2.0f).reduced (1.0f, 0.0f));
            }
            g.setColour (active ? t.text : t.dimText);
            g.setFont (t.uiFont (12.0f, 500));
            g.drawText (text, area.toNearestInt(), juce::Justification::centred, false);
        };
        drawTab (left, Strings::transport::sessionTab, ! arrangement);
        drawTab (r, Strings::transport::arrangementTab, arrangement);
        if (hovering) // a clip is held over the Arrangement tab
        {
            g.setColour (t.accent.withAlpha (0.25f));
            g.fillRoundedRectangle (r, t.radius);
        }
        strokeCell (g, getLocalBounds().toFloat(), t.line);
    }

private:
    bool arrangement = false, hovering = false;
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
    tempo.onGestureStart = [this] { model.undo.beginNewTransaction (Strings::undoTempo); };
    tempo.onValueChange = [this] (double) { model.setTempo (tempo.getValue()); };
    addAndMakeVisible (tempo);

    namespace I = icons;
    using Role = IconButton::Role;
    metronome = std::make_unique<IconButton> (I::metronome, juce::String(), Role::normal, Strings::help::metronome);
    play = std::make_unique<IconButton> (I::play, juce::String(), Role::play, Strings::help::play);
    stop = std::make_unique<IconButton> (I::stop, juce::String(), Role::normal, Strings::help::stop);
    record = std::make_unique<IconButton> (I::record, juce::String(), Role::record, Strings::help::record);
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
    tabs->onSwitch = [this] (bool arrangement) {
        if (onViewSwitched)
            onViewSwitched (arrangement);
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

void TopBar::setArrangementShown (bool shown)
{
    tabs->setArrangement (shown);
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

    if (! tempo.isEditing() && std::abs (tempo.getValue() - model.tempo()) > 0.004)
        tempo.setValue (model.tempo());

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
        glass::capsule (g, capsuleArea.toFloat ());
        g.setColour (t.line.withAlpha (0.8f));
        for (const int x : separators)
            g.fillRect ((float) x, (float) capsuleArea.getY () + 11.0f, 1.0f, (float) capsuleArea.getHeight () - 22.0f);
    }
    ui::drawLogo (g, logoArea.toFloat());
    g.setColour (t.text);
    g.setFont (t.uiFont (22.0f, 600));
    g.drawText (Strings::labelAppName, juce::Rectangle<int> (logoArea.getRight() + 9, 9, 96, 34), juce::Justification::centredLeft, false);
    // the project name, after a thin divider
    const int nameX = logoArea.getRight() + 9 + 96 + 14;
    g.setColour (t.line);
    g.fillRect (nameX - 14, 17, 1, 18);
    g.setColour (t.text);
    g.setFont (t.uiFont (15.0f, 500));
    g.drawFittedText (projectName, juce::Rectangle<int> (nameX, 9, juce::jmax (40, tabs->getX() - nameX - 12), 34),
                      juce::Justification::centredLeft, 1, 1.0f);

    auto field = [&] (juce::Rectangle<int> area, const juce::String& text) {
        if (! t.glass)
        {
            fillCell (g, area.toFloat(), t.panel);
            strokeCell (g, area.toFloat(), t.line);
        }
        g.setColour (t.text);
        g.setFont (t.numFont (13.0f));
        g.drawText (text, area, juce::Justification::centred, false);
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
    logoArea = { 16, 10, 34, 34 };
    tabs->setBounds (getWidth () / 2 - 120, 9, 240, 34);
    settingsButton->setBounds (getWidth () - 14 - 34, 10, 34, 32);
    exportButton->setBounds (settingsButton->getX () - 8 - 104, 10, 104, 32);
    redoButton->setBounds (exportButton->getX () - 10 - 32, 10, 32, 32);
    undoButton->setBounds (redoButton->getX () - 2 - 32, 10, 32, 32);
    version->setBounds (undoButton->getX () - 10 - 96, 10, 96, 32);
    appearanceButton->setBounds (version->getX (), 10, 0, 0);
    for (auto* b : { exportButton.get (), version.get (), capture.get (), libraryButton.get () })
        b->framed = true;
    // the top row's icons are plain until hovered
    for (auto* b : { undoButton.get (), redoButton.get (), settingsButton.get () })
        b->framed = false;

    // The transport needs about 1090 px; below that the Library button shrinks to its icon and gives the capsule room.
    const int libraryFull = juce::jmax (110, leftColumn - 10);
    const bool libraryCompact = getWidth () - 10 - (10 + libraryFull + 8) < 1090;
    libraryButton->text = libraryCompact ? juce::String () : Strings::transport::library;
    libraryButton->setBounds (10, cy - 20, libraryCompact ? 40 : libraryFull, 40);
    capsuleArea = { libraryButton->getRight () + 8, cy - 21, getWidth () - 10 - (libraryButton->getRight () + 8), 42 };

    separators.clear ();
    int x = capsuleArea.getX () + 12;
    auto place = [&] (juce::Component& c, int width, int height = h)
    {
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
    take (*cpu, 80);
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
    else if (right - x >= 82 + 90 + groupGap)
    {
        capture->setVisible (true);
        capture->setBounds (x, cy - 13, 82, 26);
        x += 82;
        separate ();
    }
    if (right - x >= 90)
    {
        spectrum->setVisible (true);
        spectrum->setBounds (x, cy - 15, juce::jmin (right - x, 300), 30);
    }
}
} // namespace ui
