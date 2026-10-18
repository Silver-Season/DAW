#include "ArrangementHeaders.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "PaletteMenu.h"

#include "../model/Automation.h"

namespace ui
{
namespace
{
constexpr int kStripe = 6, kEdgeGrab = 5;

juce::String panText (double pan)
{
    const int p = juce::roundToInt (pan * 50.0);
    return p == 0 ? juce::String ("C") : juce::String (std::abs (p)) + (p < 0 ? "L" : "R");
}
} // namespace

//==============================================================================
// A small button with a label or an icon.
class MiniButton final : public juce::Component
{
public:
    enum class Style
    {
        plain,
        activator,
        solo,
        arm
    };

    MiniButton (const juce::String& label, const juce::String& help, Style s = Style::plain) : text (label), style (s) { setHelpText (help); }
    MiniButton (std::function<juce::Path (juce::Rectangle<float>)> iconFn, const juce::String& help) : icon (std::move (iconFn)) { setHelpText (help); }

    std::function<void (const juce::MouseEvent&)> onClick;
    juce::String text;
    bool on = false;

    void setOn (bool v)
    {
        if (on != v)
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
        const auto r = getLocalBounds().toFloat();
        juce::Colour fill = t.panel2, ink = t.dimText;
        if (on)
        {
            fill = style == Style::arm ? t.record : (style == Style::solo ? t.accent2 : t.accent);
            ink = t.clipInk;
        }
        else if (isMouseOver())
        {
            fill = t.panel2.brighter (0.08f);
            ink = t.text;
        }
        fillCell (g, r, fill, 0.7f);
        if (! on)
            strokeCell (g, r, t.line, 1.0f, 0.7f);
        g.setColour (ink);
        if (icon)
            g.fillPath (icon (juce::Rectangle<float> (8.0f, 8.0f).withCentre (r.getCentre())));
        else if (style == Style::arm)
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
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick (e);
    }

private:
    std::function<juce::Path (juce::Rectangle<float>)> icon;
    Style style = Style::plain;
};

//==============================================================================

// Drag a number sideways (Shift: finer), double-click resets. One gesture, one undo transaction (the owner opens it).
class PanDrag final : public juce::Component
{
public:
    PanDrag() { setHelpText (Strings::help::panKnob); }

    std::function<void()> onGestureStart;
    std::function<void (double)> onValueChange;

    void setValue (double v)
    {
        if (std::abs (v - value) > 1.0e-6)
        {
            value = v;
            repaint();
        }
    }
    bool isDragging() const { return dragging; }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = getLocalBounds().toFloat();
        fillCell (g, r, t.panel2, 0.7f);
        strokeCell (g, r, t.line, 1.0f, 0.7f);
        // the position inside the box, so that the value reads at a glance
        g.setColour (t.dimText.withAlpha (0.5f));
        g.fillRect (r.getCentreX() - 0.5f, r.getY() + 3.0f, 1.0f, r.getHeight() - 6.0f);
        const float x = r.getCentreX() + (float) value * (r.getWidth() * 0.5f - 6.0f);
        g.setColour (t.accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, r.getHeight() - 6.0f).withCentre ({ x, r.getCentreY() }), 1.5f);
        g.setColour (t.text);
        g.setFont (t.numFont (10.5f));
        g.drawText (panText (value), getLocalBounds().reduced (5, 0), value > 0.0 ? juce::Justification::centredLeft : (value < 0.0 ? juce::Justification::centredRight : juce::Justification::centred), false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        lastX = e.x;
        dragging = false;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
        {
            if (e.getDistanceFromDragStart() < 2)
                return;
            dragging = true;
            if (onGestureStart)
                onGestureStart();
        }
        const double per = e.mods.isShiftDown() ? 0.0015 : 0.012;
        change (value + (double) (e.x - lastX) * per);
        lastX = e.x;
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onGestureStart)
            onGestureStart();
        change (0.0);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (onGestureStart)
            onGestureStart();
        change (value + (w.deltaY > 0 ? 0.02 : -0.02));
    }

private:
    void change (double v)
    {
        v = juce::jlimit (-1.0, 1.0, std::abs (v) < 0.02 ? 0.0 : v);
        if (std::abs (v - value) < 1.0e-9)
            return;
        value = v;
        repaint();
        if (onValueChange)
            onValueChange (value);
    }

    double value = 0.0;
    int lastX = 0;
    bool dragging = false;
};

//==============================================================================
class ArrangementHeaders::Row final : public juce::Component, public HelpProvider
{
public:
    Row (ArrangementView& v, int64_t id)
        : view (v), trackId (id),
          activator (juce::String(), Strings::help::activator, MiniButton::Style::activator),
          solo ("S", Strings::help::solo, MiniButton::Style::solo), arm (juce::String(), Strings::help::arm, MiniButton::Style::arm),
          chooser (Strings::arrangement::autoVolume, Strings::help::arrAutomationChooser)
    {
        chooser.onClick = [this] (const juce::MouseEvent&) {
            view.model.setSelectedTrack (trackId);
            view.automationMenu (trackId).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chooser));
        };
        addChildComponent (chooser);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setDecimals (1);
        volume.setSuffix (" dB");
        volume.setFontHeight (11.0f);
        volume.setDragSensitivity (0.2);
        volume.setWheelStep (0.1);
        volume.setHelpText (Strings::help::arrHeaderVolume);
        volume.onGestureStart = [this] { view.model.undo.beginNewTransaction (Strings::undoTrackVolume); };
        volume.onValueChange = [this] (double db) {
            if (auto track = view.model.findTrack (trackId); track.isValid())
                track.setVolumeDb ((float) db, &view.model.undo);
        };
        pan.onGestureStart = [this] { view.model.undo.beginNewTransaction (Strings::undoTrackPan); };
        pan.onValueChange = [this] (double value) {
            if (auto track = view.model.findTrack (trackId); track.isValid())
                track.setPan ((float) value, &view.model.undo);
        };

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
        for (auto* c : std::initializer_list<juce::Component*> { &activator, &solo, &arm, &volume, &pan })
            addAndMakeVisible (c);
        refreshValues (true);
    }

    int64_t id() const { return trackId; }
    juce::Rectangle<int> nameArea() const { return nameBounds; }

    void refreshValues (bool force = false)
    {
        const auto track = view.model.findTrack (trackId);
        if (! track.isValid())
            return;
        if (force || ! volume.isEditing())
            volume.setValue ((double) track.volumeDb());
        if (force || ! pan.isDragging())
            pan.setValue ((double) track.pan());
        // automation mode swaps the volume and pan for the chooser of the envelope's target
        const auto targetName = view.automationMode() ? view.automationTargetName (track, view.automationTarget (trackId)) : juce::String();
        if (view.automationMode() != shownAutomation)
        {
            shownAutomation = view.automationMode();
            resized();
        }
        chooser.setText (targetName);
        activator.setOn (! track.mute());
        activator.setText (juce::String (view.model.indexOfTrack (trackId) + 1));
        solo.setOn (track.solo());
        arm.setOn (track.arm());
        const auto summary = track.name() + "|" + juce::String (track.colour());
        if (summary != shown)
        {
            shown = summary;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto track = view.model.findTrack (trackId);
        if (! track.isValid())
            return;
        auto r = getLocalBounds();
        const bool selected = track.id() == view.model.selectedTrack();
        g.setColour (selected ? t.panel2 : t.panel);
        g.fillRect (r.withTrimmedBottom (1));
        g.setColour (t.line);
        g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
        g.setColour (t.palette (track.colour()));
        g.fillRect (r.getX(), r.getY(), kStripe, r.getHeight() - 1);
        if (selected)
        {
            g.setColour (t.text.withAlpha (0.9f));
            g.fillRect (r.getX() + kStripe, r.getY(), 2, r.getHeight() - 1);
        }
        if (editor == nullptr)
        {
            g.setColour (t.text);
            g.setFont (t.uiFont (12.5f, 600));
            g.drawText (track.name(), nameBounds, juce::Justification::centredLeft, true);
        }
        auto* rt = view.engine.findRuntime (trackId);
        if (rt != nullptr && rt->inSession.load()) // the track plays Session View: same grey bar as over its lane
        {
            g.setColour (t.dimText.withAlpha (0.55f));
            g.fillRect (r.getX(), r.getY(), r.getWidth(), 3);
        }
    }

    void resized() override
    {
        const int h = getHeight();
        const bool compact = h < 52;
        const int pad = 8, btn = compact ? 18 : 20;
        auto r = getLocalBounds().withTrimmedLeft (kStripe + pad).withTrimmedRight (pad).withTrimmedBottom (1);
        auto top = compact ? r.withSizeKeepingCentre (r.getWidth(), btn) : r.removeFromTop (juce::jmin (h - 2, 6 + btn)).withTrimmedTop (5).withHeight (btn);
        // right to left: record, solo, on
        arm.setBounds (top.removeFromRight (btn));
        top.removeFromRight (3);
        solo.setBounds (top.removeFromRight (btn));
        top.removeFromRight (3);
        activator.setBounds (top.removeFromLeft (btn + 2));
        top.removeFromLeft (6);
        nameBounds = top;

        const bool second = h >= 52;
        const bool automating = view.automationMode();
        volume.setVisible (second && ! automating);
        pan.setVisible (second && ! automating);
        chooser.setVisible (second && automating);
        if (second)
        {
            auto line = juce::Rectangle<int> (r.getX(), r.getY() + 5, r.getWidth(), 20);
            if (h >= 76)
                line.setY (r.getY() + 7);
            chooser.setBounds (line);
            pan.setBounds (line.removeFromRight (juce::jmin (64, line.getWidth() / 2 - 2)));
            line.removeFromRight (4);
            volume.setBounds (line);
        }
        if (editor != nullptr)
            editor->setBounds (nameBounds.expanded (2, 0));
    }

    void beginRename()
    {
        const auto track = view.model.findTrack (trackId);
        if (! track.isValid() || editor != nullptr)
            return;
        editor = std::make_unique<InlineEditor>();
        editor->setText (track.name(), false);
        editor->onCommit = [this] (const juce::String& text) { finishRename (text); };
        editor->onCancel = [this] { finishRename ({}); };
        addAndMakeVisible (*editor);
        editor->setBounds (nameBounds.expanded (2, 0));
        editor->grabKeyboardFocus();
        editor->selectAll();
        repaint();
    }

    juce::String helpAt (juce::Point<int> p) const override
    {
        return p.y >= getHeight() - kEdgeGrab ? Strings::help::arrHeaderEdge : Strings::help::arrHeader;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.y >= getHeight() - kEdgeGrab ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        resizing = false;
        view.takeFocus();
        if (! e.mods.isPopupMenu() && e.y >= getHeight() - kEdgeGrab)
        {
            resizing = true;
            firstResizeStep = true;
            return;
        }
        view.model.setSelectedTrack (trackId);
        view.selectionState.selectTrack (trackId);
        repaint();
        view.headersPart->repaint();
        if (e.mods.isPopupMenu())
            showMenu (e.getScreenPosition());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! resizing)
            return;
        const auto inView = e.getEventRelativeTo (view.headersPart.get());
        const int top = getY();
        const int height = inView.y - top + 1;
        view.setTrackHeight (trackId, height, firstResizeStep);
        firstResizeStep = false;
    }

    void mouseUp (const juce::MouseEvent&) override { resizing = false; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y < getHeight() - kEdgeGrab && nameBounds.contains (e.getPosition()))
            beginRename();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        view.headersPart->mouseWheelMove (e.getEventRelativeTo (view.headersPart.get()), w);
    }

private:
    void finishRename (const juce::String& text)
    {
        auto* dying = editor.release();
        removeChildComponent (dying);
        juce::MessageManager::callAsync ([dying] { delete dying; });
        if (text.trim().isNotEmpty())
            view.actions.renameTrack (trackId, text.trim());
        view.takeFocus();
        repaint();
    }

    void showMenu (juce::Point<int> screenPosition)
    {
        juce::PopupMenu menu;
        const juce::Component::SafePointer<Row> safe (this);
        const auto id = trackId;
        menu.addItem (Strings::menu::rename, [safe] {
            if (safe != nullptr)
                safe->beginRename();
        });
        const auto track = view.model.findTrack (trackId);
        menu.addSubMenu (Strings::menu::colour, makePaletteMenu (track.colour(), [safe, id] (int index) {
            if (safe != nullptr)
                safe->view.actions.setTrackColour (id, index);
        }));
        menu.addItem (Strings::menu::duplicate, [safe, id] {
            if (safe != nullptr)
                safe->view.actions.duplicateTrack (id);
        });
        menu.addItem (Strings::arrangement::menuDeleteTrack, [safe, id] {
            if (safe != nullptr)
                safe->view.actions.deleteTrack (id);
        });
        menu.addSeparator();
        const int insertAt = view.model.indexOfTrack (id) + 1;
        menu.addItem (Strings::arrangement::menuAddMidiTrack, [safe, insertAt] {
            if (safe != nullptr)
                safe->view.actions.addMidiTrack (insertAt);
        });
        menu.addItem (Strings::arrangement::menuAddAudioTrack, [safe, insertAt] {
            if (safe != nullptr)
                safe->view.actions.addAudioTrack (insertAt);
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPosition.x, screenPosition.y, 1, 1)));
    }

    ArrangementView& view;
    int64_t trackId;
    MiniButton activator, solo, arm, chooser;
    bool shownAutomation = false;
    NumberField volume;
    PanDrag pan;
    std::unique_ptr<InlineEditor> editor;
    juce::Rectangle<int> nameBounds;
    juce::String shown;
    bool resizing = false, firstResizeStep = true;
};

//==============================================================================
ArrangementHeaders::ArrangementHeaders (ArrangementView& v) : view (v)
{
    setOpaque (true);
}

ArrangementHeaders::~ArrangementHeaders() = default;

int ArrangementHeaders::numRows() const
{
    return (int) rowComponents.size();
}

juce::Rectangle<int> ArrangementHeaders::rowBounds (int64_t trackId) const
{
    for (const auto& r : rowComponents)
        if (r->id() == trackId)
            return r->getBounds();
    return {};
}

void ArrangementHeaders::refreshRows()
{
    // drop headers of tracks that are gone, add the new ones
    for (size_t i = rowComponents.size(); i-- > 0;)
        if (view.rowIndexOfTrack (rowComponents[i]->id()) < 0)
            rowComponents.erase (rowComponents.begin() + (std::ptrdiff_t) i);
    for (const auto& lane : view.rows())
    {
        bool have = false;
        for (const auto& r : rowComponents)
            have = have || r->id() == lane.trackId;
        if (! have)
        {
            rowComponents.push_back (std::make_unique<Row> (view, lane.trackId));
            addAndMakeVisible (*rowComponents.back());
        }
    }
    for (const auto& r : rowComponents)
    {
        const auto& lane = view.rows()[(size_t) view.rowIndexOfTrack (r->id())];
        r->setBounds (0, lane.y - view.view().scrollY, getWidth(), lane.height);
        r->refreshValues();
    }
}

void ArrangementHeaders::refreshValues()
{
    for (const auto& r : rowComponents)
        r->refreshValues();
}

void ArrangementHeaders::beginRename (int64_t trackId)
{
    for (const auto& r : rowComponents)
        if (r->id() == trackId)
            r->beginRename();
}

void ArrangementHeaders::paint (juce::Graphics& g)
{
    g.fillAll (theme().background);
}

void ArrangementHeaders::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isAltDown())
    {
        const int row = view.rowAtY (e.y);
        if (row >= 0)
        {
            const auto& lane = view.rows()[(size_t) row];
            view.setTrackHeight (lane.trackId, lane.height + (w.deltaY > 0 ? 8 : -8), true);
        }
        return;
    }
    view.setScrollY (view.view().scrollY - juce::roundToInt (w.deltaY * 300.0f));
}

//==============================================================================
ArrangementCorner::ArrangementCorner (ArrangementView& v) : view (v)
{
    setOpaque (true);
    namespace A = Strings::arrangement;
    addMarker = std::make_unique<MiniButton> (A::addMarker, Strings::help::arrAddMarker);
    previous = std::make_unique<MiniButton> ([] (juce::Rectangle<float> a) { return icons::triangle (a, 3); }, Strings::help::arrPrevMarker);
    next = std::make_unique<MiniButton> ([] (juce::Rectangle<float> a) { return icons::triangle (a, 1); }, Strings::help::arrNextMarker);
    list = std::make_unique<MiniButton> ([] (juce::Rectangle<float> a) { return icons::chevronDown (a); }, Strings::help::arrMarkerList);
    heightChip = std::make_unique<MiniButton> (A::heightsUniform, Strings::help::arrHeightMode);
    addMarker->onClick = [this] (const juce::MouseEvent&) { view.addMarkerAtCursor(); };
    previous->onClick = [this] (const juce::MouseEvent&) { view.jumpToMarker (false); };
    next->onClick = [this] (const juce::MouseEvent&) { view.jumpToMarker (true); };
    list->onClick = [this] (const juce::MouseEvent&) {
        view.markersMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (list.get()));
    };
    heightChip->onClick = [this] (const juce::MouseEvent&) { view.toggleHeightMode(); };
    for (auto* c : { addMarker.get(), previous.get(), next.get(), list.get(), heightChip.get() })
        addAndMakeVisible (c);
}

ArrangementCorner::~ArrangementCorner() = default;

void ArrangementCorner::resized()
{
    auto top = juce::Rectangle<int> (8, 5, getWidth() - 16, 20);
    addMarker->setBounds (top.removeFromLeft (84));
    top.removeFromLeft (4);
    previous->setBounds (top.removeFromLeft (24));
    top.removeFromLeft (3);
    next->setBounds (top.removeFromLeft (24));
    top.removeFromLeft (3);
    list->setBounds (top.removeFromLeft (24));
    heightChip->setBounds (juce::Rectangle<int> (getWidth() - 8 - 66, 29, 66, 18));
}

void ArrangementCorner::refresh()
{
    const auto grid = view.gridText() + (view.view().snap ? "" : "|off") + (view.view().adaptiveGrid ? "" : "|fixed");
    heightChip->setText (view.view().uniformHeights ? Strings::arrangement::heightsUniform : Strings::arrangement::heightsByContent);
    if (grid != shownGrid)
    {
        shownGrid = grid;
        repaint();
    }
}

void ArrangementCorner::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
    g.setColour (t.dimText);
    g.setFont (t.numFont (10.5f));
    juce::String text = Strings::arrangement::gridLabel (view.gridText(), view.view().adaptiveGrid);
    if (! view.view().snap)
        text << "  " << Strings::arrangement::snapOffShort;
    g.drawText (text, juce::Rectangle<int> (8, 29, getWidth() - 8 - 66 - 12, 18), juce::Justification::centredLeft, true);
}

juce::String ArrangementCorner::helpAt (juce::Point<int> p) const
{
    return p.y > 26 && p.x < getWidth() - 80 ? Strings::help::arrGridLabel : Strings::help::arrCorner;
}
} // namespace ui
