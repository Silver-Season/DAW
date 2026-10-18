#include "EffectViews.h"

#include "../Strings.h"
#include "../engine/BuiltInEffectProcessor.h"

#include <cmath>
#include <map>
#include <optional>
#include <set>

namespace ui
{
namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Font rowFont() { return theme().uiFont (11.5f, 500); }
juce::Font rowLabelFont() { return theme().uiFont (10.5f); }
juce::Font valueFont() { return theme().numFont (10.5f); }

int textWidth (const juce::Font& font, const juce::String& text)
{
    return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
}

juce::StringArray choicesOf (const juce::RangedAudioParameter& p)
{
    if (auto* c = dynamic_cast<const juce::AudioParameterChoice*> (&p))
        return c->choices;
    return {};
}

juce::String valueTextOf (const juce::RangedAudioParameter& p, float normalised)
{
    return p.getText (normalised, 12).trim();
}

// Shared look of the small boxed controls.
void paintBox (juce::Graphics& g, juce::Rectangle<float> r, bool hot, bool lit, juce::Colour litColour)
{
    const auto& t = theme();
    fillCell (g, r, lit ? litColour.withAlpha (0.18f) : t.panel2, 0.6f);
    strokeCell (g, r, lit ? litColour.withAlpha (0.75f) : (hot ? t.dimText.withAlpha (0.7f) : t.line), 1.0f, 0.6f);
}

// The colour of EQ band b: spread over the palette so that neighbours differ.
juce::Colour bandColour (int b) { return theme().palette ((b * 2) % Theme::kPaletteSize); }

// Note length of a division name ("1/8", "1/16T", "1/8."), in quarter notes.
double divisionBeats (const juce::String& name)
{
    const auto core = name.upToFirstOccurrenceOf ("T", false, false).upToFirstOccurrenceOf (".", false, false);
    const double num = core.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
    const double den = core.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
    if (den <= 0.0)
        return 1.0;
    double beats = 4.0 * num / den;
    if (name.endsWithChar ('T'))
        beats *= 2.0 / 3.0;
    else if (name.endsWithChar ('.'))
        beats *= 1.5;
    return beats;
}

// The EQ band types (Eq8.cpp): 0-3 low cuts, 4 low shelf, 5 bell, 6 notch, 7 high shelf, 8-11 high cuts.
bool bandHasGain (int kind) { return kind == 4 || kind == 5 || kind == 7; }

std::map<const void*, int>& rememberedBands()
{
    static std::map<const void*, int> bands; // the selected EQ band survives the chain being rebuilt
    return bands;
}
} // namespace

//==============================================================================
void ParamControl::setNormalised (float normalised)
{
    param.beginChangeGesture();
    param.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    param.endChangeGesture();
}

int ParamControl::choiceIndex() const
{
    return juce::roundToInt (param.convertFrom0to1 (param.getValue()));
}

//==============================================================================
ParamKnob::ParamKnob (juce::RangedAudioParameter& p, const juce::String& caption, juce::Colour ring) : ParamControl (p)
{
    auto* raw = &p;
    knob.setCaption (caption);
    knob.setRingColour (ring);
    const auto range = p.getNormalisableRange();
    knob.setBipolar (range.start < 0.0f && range.end > 0.0f);
    knob.setValue ((double) p.getValue());
    knob.setDefaultValue ((double) p.getDefaultValue());
    knob.setHelpText (Strings::help::deviceKnob);
    knob.valueText = [raw] (double v) { return valueTextOf (*raw, (float) v); };
    // Plugin parameters are not part of the undo history (see 00-rules, section 3); a gesture is one host gesture.
    knob.onGestureStart = [raw] { raw->beginChangeGesture(); };
    knob.onValueChange = [raw] (double v) { raw->setValueNotifyingHost ((float) v); };
    knob.onGestureEnd = [raw] { raw->endChangeGesture(); };
    addAndMakeVisible (knob);
}

void ParamKnob::refresh()
{
    if (! knob.isDragging())
        knob.setValue ((double) param.getValue());
}

//==============================================================================
ChoiceBox::ChoiceBox (juce::RangedAudioParameter& p, const juce::String& l) : ParamControl (p), label (l)
{
    setHelpText (Strings::help::fxChoice);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
}

void ChoiceBox::refresh()
{
    if (const int index = choiceIndex(); index != shown)
    {
        shown = index;
        repaint();
    }
}

int ChoiceBox::idealWidth() const
{
    int widest = 0;
    for (const auto& c : choicesOf (param))
        widest = juce::jmax (widest, textWidth (rowFont(), c));
    return widest + (label.isNotEmpty() ? textWidth (rowLabelFont(), label) + 6 : 0) + 30;
}

void ChoiceBox::choose (int index)
{
    const int count = choicesOf (param).size();
    if (index < 0 || index >= count)
        return;
    setNormalised (param.convertTo0to1 ((float) index));
    refresh();
}

void ChoiceBox::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    paintBox (g, r, isMouseOver (true), false, t.accent);
    const auto choices = choicesOf (param);
    const auto text = juce::isPositiveAndBelow (shown, choices.size()) ? choices[shown] : juce::String();
    auto area = getLocalBounds().reduced (8, 0);
    const auto chevron = area.removeFromRight (8).toFloat().withSizeKeepingCentre (7.0f, 4.0f);
    g.setColour (t.dimText);
    g.fillPath (icons::chevronDown (chevron));
    area.removeFromRight (4);
    // The label goes first when there is room for it next to the value.
    const int labelW = label.isNotEmpty() ? textWidth (rowLabelFont(), label) + 6 : 0;
    if (labelW > 0 && labelW + textWidth (rowFont(), text) <= area.getWidth())
    {
        g.setFont (rowLabelFont());
        g.drawText (label, area.removeFromLeft (labelW), juce::Justification::centredLeft, false);
    }
    g.setColour (isEnabled() ? t.text : t.dimText);
    g.setFont (rowFont());
    g.drawText (text, area, juce::Justification::centredLeft, true);
}

void ChoiceBox::mouseDown (const juce::MouseEvent&)
{
    if (! isEnabled())
        return;
    juce::PopupMenu menu;
    const auto choices = choicesOf (param);
    for (int i = 0; i < choices.size(); ++i)
        menu.addItem (i + 1, choices[i], true, i == shown);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe = juce::Component::SafePointer<ChoiceBox> (this)] (int result) {
                            if (safe != nullptr && result > 0)
                                safe->choose (result - 1);
                        });
}

//==============================================================================
SegmentedChoice::SegmentedChoice (juce::RangedAudioParameter& p, const juce::String& l) : ParamControl (p), label (l), items (choicesOf (p))
{
    setHelpText (Strings::help::fxChoice);
    refresh();
}

void SegmentedChoice::refresh()
{
    if (const int index = choiceIndex(); index != shown)
    {
        shown = index;
        repaint();
    }
}

int SegmentedChoice::labelWidth() const { return label.isNotEmpty() ? textWidth (rowLabelFont(), label) + 8 : 0; }

int SegmentedChoice::idealWidth() const
{
    int w = labelWidth();
    for (const auto& item : items)
        w += juce::jmax (22, textWidth (rowFont(), item) + 14);
    return w;
}

juce::Rectangle<int> SegmentedChoice::segment (int index) const
{
    auto area = getLocalBounds().withTrimmedLeft (labelWidth());
    if (items.isEmpty())
        return {};
    const int w = area.getWidth() / items.size();
    return { area.getX() + index * w, area.getY(), index == items.size() - 1 ? area.getRight() - (area.getX() + index * w) : w, area.getHeight() };
}

void SegmentedChoice::choose (int index)
{
    if (! juce::isPositiveAndBelow (index, items.size()))
        return;
    setNormalised (param.convertTo0to1 ((float) index));
    refresh();
}

void SegmentedChoice::paint (juce::Graphics& g)
{
    const auto& t = theme();
    if (label.isNotEmpty())
    {
        g.setColour (t.dimText);
        g.setFont (rowLabelFont());
        g.drawText (label, getLocalBounds().withWidth (labelWidth()), juce::Justification::centredLeft, false);
    }
    const auto whole = getLocalBounds().withTrimmedLeft (labelWidth()).toFloat().reduced (0.5f);
    fillCell (g, whole, t.panel2, 0.6f);
    for (int i = 0; i < items.size(); ++i)
    {
        const auto s = segment (i).toFloat().reduced (0.5f);
        if (i == shown)
        {
            fillCell (g, s.reduced (1.5f), t.accent.withAlpha (0.28f), 0.5f);
            strokeCell (g, s.reduced (1.5f), t.accent.withAlpha (0.8f), 1.0f, 0.5f);
        }
        else if (i > 0)
        {
            g.setColour (t.line);
            g.fillRect (s.getX(), s.getY() + 5.0f, 1.0f, s.getHeight() - 10.0f);
        }
        g.setColour (i == shown ? t.text : t.dimText);
        g.setFont (rowFont());
        g.drawText (items[i], s, juce::Justification::centred, false);
    }
    strokeCell (g, whole, t.line, 1.0f, 0.6f);
}

void SegmentedChoice::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < items.size(); ++i)
        if (segment (i).contains (e.getPosition()))
            choose (i);
}

//==============================================================================
ParamToggle::ParamToggle (juce::RangedAudioParameter& p, const juce::String& l) : ParamControl (p), label (l)
{
    setHelpText (Strings::help::fxToggle);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
}

void ParamToggle::refresh()
{
    if (const bool now = choiceIndex() > 0; now != on)
    {
        on = now;
        repaint();
    }
}

int ParamToggle::idealWidth() const { return textWidth (rowFont(), label) + 30; }

void ParamToggle::toggle()
{
    if (! isEnabled())
        return;
    setNormalised (on ? 0.0f : 1.0f);
    refresh();
}

void ParamToggle::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto colour = lamp.value_or (t.accent);
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    paintBox (g, r, isMouseOver (true), on, colour);
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 11.0f, r.getCentreY() });
    if (on && t.glow > 0.0f)
    {
        g.setColour (colour.withAlpha (0.3f));
        g.fillEllipse (dot.expanded (2.5f * t.glow));
    }
    g.setColour (on ? colour : t.line);
    g.fillEllipse (dot);
    g.setColour (on ? t.text : t.dimText);
    g.setFont (rowFont());
    g.drawText (label, getLocalBounds().withTrimmedLeft (20).withTrimmedRight (6), juce::Justification::centredLeft, true);
}

//==============================================================================
ValueBox::ValueBox (juce::RangedAudioParameter& p, const juce::String& c) : ParamControl (p), caption (c)
{
    setHelpText (Strings::help::fxValue);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    value = p.getValue();
}

void ValueBox::refresh()
{
    if (! gestureOpen && ! juce::exactlyEqual (value, param.getValue()))
    {
        value = param.getValue();
        repaint();
    }
}

int ValueBox::idealWidth() const
{
    // Room for the widest value: the ends of the range and the default.
    int widest = 0;
    for (float n : { 0.0f, 1.0f, param.getDefaultValue() })
        widest = juce::jmax (widest, textWidth (valueFont(), valueTextOf (param, n)));
    return widest + (caption.isNotEmpty() ? textWidth (rowLabelFont(), caption) + 6 : 0) + 16;
}

void ValueBox::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    paintBox (g, r, isMouseOver (true) || gestureOpen, false, t.accent);
    // A thin bar along the bottom shows where the value sits in its range.
    if (isEnabled())
    {
        g.setColour (t.accent.withAlpha (0.55f));
        g.fillRect (r.getX() + 3.0f, r.getBottom() - 2.5f, (r.getWidth() - 6.0f) * value, 1.5f);
    }
    auto area = getLocalBounds().reduced (7, 0);
    if (caption.isNotEmpty())
    {
        g.setColour (t.dimText);
        g.setFont (rowLabelFont());
        g.drawText (caption, area.removeFromLeft (textWidth (rowLabelFont(), caption) + 6), juce::Justification::centredLeft, false);
    }
    g.setColour (isEnabled() ? t.text : t.dimText);
    g.setFont (valueFont());
    g.drawText (valueTextOf (param, value), area, caption.isNotEmpty() ? juce::Justification::centredRight : juce::Justification::centred, false);
}

void ValueBox::change (float normalised)
{
    normalised = juce::jlimit (0.0f, 1.0f, normalised);
    if (std::abs (normalised - value) < 1.0e-7f)
        return;
    value = normalised;
    param.setValueNotifyingHost (value);
    repaint();
}

void ValueBox::mouseDown (const juce::MouseEvent& e)
{
    lastY = e.y;
}

void ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || (! gestureOpen && e.getDistanceFromDragStart() < 3))
        return;
    if (! gestureOpen)
    {
        gestureOpen = true;
        param.beginChangeGesture();
    }
    const int dy = lastY - e.y;
    lastY = e.y;
    change (value + (float) dy / 200.0f * (e.mods.isShiftDown() ? 0.1f : 1.0f));
}

void ValueBox::mouseUp (const juce::MouseEvent&)
{
    if (gestureOpen)
    {
        gestureOpen = false;
        param.endChangeGesture();
        repaint();
    }
}

void ValueBox::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! isEnabled())
        return;
    setNormalised (param.getDefaultValue());
    value = param.getValue();
    repaint();
}

void ValueBox::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! isEnabled())
        return;
    const float delta = (w.isReversed ? -w.deltaY : w.deltaY) * (e.mods.isShiftDown() ? 0.02f : 0.15f);
    param.beginChangeGesture();
    change (value + delta);
    param.endChangeGesture();
}

//==============================================================================
// What one control of a form shows.
enum class ControlKind
{
    knob,
    choice,
    segmented,
    toggle,
    box
};

struct EffectView::Item
{
    juce::String id;
    ControlKind kind = ControlKind::knob;
    juce::String label;          // empty: Strings::fx::caption (id)
    bool bare = false;           // no caption at all (EQ frequency and gain boxes)
    juce::String showIf;         // shown only while this choice parameter is at showValue
    int showValue = 1;
    juce::String dimIf;          // greyed out while this choice parameter is at dimValue
    int dimValue = 1;
    int band = -1;               // compact EQ: shown only while this band is selected

    Item& when (const char* id_, int v) { showIf = id_; showValue = v; return *this; }
    Item& dim (const char* id_, int v) { dimIf = id_; dimValue = v; return *this; }
    Item& named (const juce::String& l) { label = l; return *this; }
    Item& noCaption() { bare = true; return *this; }
    Item& ofBand (int b) { band = b; return *this; }
};

namespace
{
using ItemT = EffectView::Item;
ItemT item (const juce::String& id, ControlKind kind)
{
    ItemT i;
    i.id = id;
    i.kind = kind;
    return i;
}
ItemT K (const juce::String& id) { return item (id, ControlKind::knob); }
ItemT C (const juce::String& id) { return item (id, ControlKind::choice); }
ItemT S (const juce::String& id) { return item (id, ControlKind::segmented); }
ItemT T (const juce::String& id) { return item (id, ControlKind::toggle); }
ItemT B (const juce::String& id) { return item (id, ControlKind::box); }
} // namespace

struct EffectView::Group
{
    juce::String title;
    std::vector<Item> items;
    int width = 0;                 // column width, 0 = two knob cells
    std::optional<Item> titleItem; // a switch in place of the title (EQ band on/off)
};

namespace
{
EffectView::Group group (const juce::String& title, std::vector<EffectView::Item> items, int width = 0)
{
    EffectView::Group g;
    g.title = title;
    g.items = std::move (items);
    g.width = width;
    return g;
}
} // namespace

struct EffectView::Spec
{
    enum class Picture
    {
        none,
        eq,
        dynamics,
        reverb,
        delay,
        drive,
        crush,
        filter,
        lfo
    };
    Picture picture = Picture::none;
    int pictureWidth = 120, pictureWidthExpanded = 150;
    int columns = 3;             // of the compact knob grid
    std::vector<Item> top, knobs, bottom;
    std::vector<Group> groups;
};

struct EffectView::Placed
{
    Item item;
    int section = 0; // 0 top, 1 knobs, 2 bottom, 3 a group (index in group), 4 a group's title
    int group = -1;
};

namespace
{
std::unique_ptr<EffectView::Spec> specFor (const juce::String& type)
{
    using Spec = EffectView::Spec;
    using P = Spec::Picture;
    namespace G = Strings::fx::groups;
    auto s = std::make_unique<Spec>();
    const auto out = group (G::output, { K ("mix"), K ("output") });
    // Rate as a box while free, the division while synced: they take turns in the same place.
    const auto rate = [] (const char* sync, const char* r, const char* div) {
        return std::vector<EffectView::Item> { B (r).when (sync, 0), C (div).when (sync, 1) };
    };

    if (type == "eq8")
    {
        s->picture = P::eq;
        s->pictureWidthExpanded = 300;
        for (int b = 0; b < 8; ++b)
        {
            const auto n = "b" + juce::String (b + 1);
            s->bottom.push_back (T (n + "on").named (juce::String (b + 1)).ofBand (b));
            s->bottom.push_back (C (n + "type").noCaption().ofBand (b));
            s->bottom.push_back (B (n + "freq").noCaption().ofBand (b));
            s->bottom.push_back (B (n + "gain").noCaption().ofBand (b));
            s->bottom.push_back (B (n + "q").named ("Q").ofBand (b));
            auto g = group ({}, { C (n + "type").noCaption(), B (n + "freq").noCaption(), B (n + "gain").noCaption(), B (n + "q").named ("Q"),
                                  C (n + "ch").noCaption() },
                            98);
            g.titleItem = T (n + "on").named (Strings::fx::groups::band (b + 1));
            s->groups.push_back (g);
        }
        s->groups.push_back (group (G::common, { C ("mode").noCaption(), K ("mix"), K ("output") }));
    }
    else if (type == "comp")
    {
        s->picture = P::dynamics;
        s->knobs = { K ("threshold"), K ("ratio"), K ("attack"), K ("release"), K ("makeup"), K ("mix") };
        s->groups = { group (G::dynamics, { K ("threshold"), K ("ratio"), K ("knee"), C ("detector") }),
                      group (G::time, { K ("attack"), K ("release"), K ("lookahead"), T ("autoRelease") }),
                      group (G::output, { K ("makeup"), K ("mix"), T ("autoMakeup"), K ("output") }),
                      group (G::sidechain, { K ("scHpf"), T ("scListen") }) };
    }
    else if (type == "gate")
    {
        s->picture = P::dynamics;
        s->knobs = { K ("threshold"), K ("floor"), K ("attack"), K ("hold"), K ("release"), K ("mix") };
        s->groups = { group (G::gate, { K ("threshold"), K ("hysteresis"), K ("floor") }),
                      group (G::time, { K ("attack"), K ("hold"), K ("release"), K ("lookahead") }),
                      group (G::sidechain, { K ("scHpf") }), out };
    }
    else if (type == "limiter")
    {
        s->picture = P::dynamics;
        s->top = { T ("truePeak"), T ("autoRelease") };
        s->knobs = { K ("gain"), K ("ceiling"), K ("release"), K ("lookahead"), K ("mix"), K ("output") };
        s->groups = { group (G::limiter, { K ("gain"), K ("ceiling"), T ("truePeak") }),
                      group (G::time, { K ("release"), K ("lookahead"), T ("autoRelease") }), out };
    }
    else if (type == "reverb")
    {
        s->picture = P::reverb;
        s->pictureWidth = 130;
        s->pictureWidthExpanded = 190;
        s->knobs = { K ("size"), K ("decay"), K ("predelay"), K ("damping"), K ("lowCut"), K ("mix") };
        s->groups = { group (G::space, { K ("size"), K ("decay"), K ("diffusion"), K ("width") }),
                      group (G::time, { K ("predelay"), C ("predelaySync"), K ("modulation") }),
                      group (G::tone, { K ("damping"), K ("lowCut") }),
                      group (G::output, { T ("freeze"), K ("mix"), K ("output") }) };
    }
    else if (type == "delay")
    {
        s->picture = P::delay;
        s->pictureWidth = 150;
        s->pictureWidthExpanded = 200;
        s->columns = 2;
        s->top = { T ("sync"), C ("divL").when ("sync", 1), B ("timeL").when ("sync", 0), C ("divR").when ("sync", 1).dim ("link", 1),
                   B ("timeR").when ("sync", 0).dim ("link", 1), T ("link") };
        s->knobs = { K ("feedback"), K ("mix"), K ("hp"), K ("lp") };
        s->bottom = { T ("pingPong"), T ("freeze") };
        s->groups = { group (G::time, { T ("sync"), C ("divL").when ("sync", 1), B ("timeL").when ("sync", 0), T ("link"),
                                        C ("divR").when ("sync", 1).dim ("link", 1), B ("timeR").when ("sync", 0).dim ("link", 1) }),
                      group (G::echo, { K ("feedback"), T ("pingPong"), K ("hp"), K ("lp") }),
                      group (G::modulation, { K ("modRate"), K ("modDepth") }),
                      group (G::output, { T ("freeze"), K ("ducking"), K ("mix"), K ("output") }) };
    }
    else if (type == "drive")
    {
        s->picture = P::drive;
        s->top = { C ("mode") };
        s->knobs = { K ("drive"), K ("tone"), K ("bias"), K ("input"), K ("mix"), K ("output") };
        s->groups = { group (G::drive, { C ("mode"), K ("drive"), K ("input"), K ("bias"), K ("tone") }), out };
    }
    else if (type == "crush")
    {
        s->picture = P::crush;
        s->pictureWidth = 130;
        s->columns = 2;
        s->top = { T ("dither") };
        s->knobs = { K ("bits"), K ("rate"), K ("mix"), K ("output") };
        s->groups = { group (G::crush, { K ("bits"), K ("rate"), T ("dither") }), out };
    }
    else if (type == "filter")
    {
        s->picture = P::filter;
        s->top = { C ("type") };
        s->knobs = { K ("freq"), K ("resonance"), K ("drive"), K ("lfoDepth"), K ("envDepth"), K ("mix") };
        auto lfo = std::vector<EffectView::Item> { C ("lfoShape"), T ("lfoSync"), K ("lfoRate").when ("lfoSync", 0), C ("lfoDiv").when ("lfoSync", 1), K ("lfoDepth") };
        s->groups = { group (G::filter, { C ("type"), K ("freq"), K ("resonance"), K ("drive") }), group (G::lfo, lfo),
                      group (G::envelope, { K ("envDepth"), K ("envAttack"), K ("envRelease") }), out };
    }
    else if (type == "chorus")
    {
        s->picture = P::lfo;
        s->top = { S ("voices") };
        s->knobs = { K ("rate"), K ("depth"), K ("delay"), K ("width"), K ("feedback"), K ("mix") };
        s->groups = { group (G::modulation, { S ("voices"), K ("rate"), K ("depth"), K ("delay") }),
                      group (G::sound, { K ("width"), K ("feedback") }), out };
    }
    else if (type == "flanger" || type == "phaser" || type == "autopan")
    {
        s->picture = P::lfo;
        std::vector<EffectView::Item> lfo;
        if (type == "phaser")
            s->top.push_back (C ("stages"));
        if (type == "autopan")
        {
            s->top.push_back (C ("shape"));
            lfo.push_back (C ("shape"));
        }
        s->top.push_back (T ("sync"));
        lfo.push_back (T ("sync"));
        for (auto& i : rate ("sync", "rate", "div"))
        {
            s->top.push_back (i);
            lfo.push_back (i);
        }
        lfo.push_back (K ("depth"));
        lfo.push_back (K ("phase"));
        if (type == "flanger")
        {
            s->knobs = { K ("depth"), K ("delay"), K ("feedback"), K ("phase"), K ("mix"), K ("output") };
            s->groups = { group (G::lfo, lfo), group (G::sound, { K ("delay"), K ("feedback") }), out };
        }
        else if (type == "phaser")
        {
            s->knobs = { K ("depth"), K ("center"), K ("feedback"), K ("phase"), K ("mix"), K ("output") };
            s->groups = { group (G::lfo, lfo), group (G::sound, { C ("stages"), K ("center"), K ("feedback") }), out };
        }
        else
        {
            s->knobs = { K ("depth"), K ("phase"), K ("offset"), K ("mix"), K ("output") };
            s->groups = { group (G::lfo, lfo), group (G::sound, { K ("offset") }), out };
        }
    }
    return s;
}
} // namespace

//==============================================================================
// The picture of an effect. Repaints only when a parameter or a display value changed.
class EffectView::Display : public juce::Component
{
public:
    explicit Display (EffectView& owner) : view (owner) { setHelpText (Strings::help::fxDisplay); }

    virtual void tick()
    {
        if (paramsChanged())
            repaint();
    }

protected:
    bool paramsChanged()
    {
        const auto& ps = view.processor().getParameters();
        bool changed = cache.size() != (size_t) ps.size();
        cache.resize ((size_t) ps.size());
        for (int i = 0; i < ps.size(); ++i)
        {
            const float v = ps[i]->getValue();
            if (! juce::exactlyEqual (v, cache[(size_t) i]))
            {
                cache[(size_t) i] = v;
                changed = true;
            }
        }
        return changed;
    }

    juce::Rectangle<float> background (juce::Graphics& g) const
    {
        const auto r = getLocalBounds().toFloat();
        fillCell (g, r, theme().panel2, 0.7f);
        return r;
    }

    float v (const char* id) const { return view.value (id); }

    EffectView& view;
    std::vector<float> cache;
};

namespace
{
using Display = EffectView::Display;

//==============================================================================
// The curve of EQ Eight from the effect's own response(), with a dot per band to drag.
class EqDisplay final : public Display
{
public:
    explicit EqDisplay (EffectView& owner) : Display (owner), shadow (daw::fx::createEffect ("eq8"))
    {
        setHelpText (Strings::help::eqCurve);
        // The curve comes from a copy of the effect that only the message thread touches: it is given the same values and
        // asked for its response, so the audio thread's instance is never read here.
        if (shadow != nullptr)
            shadow->prepare (48000.0, 64);
    }

    void tick() override
    {
        if (paramsChanged())
        {
            recompute();
            repaint();
        }
    }

    void resized() override { recompute(); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        background (g);
        const auto plot = plotArea();

        // grid: decades and the 0 / +-12 dB lines
        g.setFont (t.numFont (9.0f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xFor (f);
            const bool major = juce::exactlyEqual (f, 100.0f) || juce::exactlyEqual (f, 1000.0f) || juce::exactlyEqual (f, 10000.0f);
            g.setColour (t.line.withAlpha (major ? 0.9f : 0.45f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            if (major && getHeight() > 90)
            {
                g.setColour (t.dimText.withAlpha (0.8f));
                g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                            juce::Rectangle<float> (x + 3.0f, plot.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
            }
        }
        for (float db : { -12.0f, 12.0f })
        {
            g.setColour (t.line.withAlpha (0.6f));
            g.fillRect (plot.getX(), yFor (db), plot.getWidth(), 1.0f);
        }
        g.setColour (t.line);
        g.fillRect (plot.getX(), yFor (0.0f), plot.getWidth(), 1.0f);

        // the curve
        if (curve.size() >= 2)
        {
            juce::Path line;
            for (size_t i = 0; i < curve.size(); ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) (curve.size() - 1);
                const float y = juce::jlimit (plot.getY(), plot.getBottom(), yFor (curve[i]));
                if (i == 0)
                    line.startNewSubPath (x, y);
                else
                    line.lineTo (x, y);
            }
            auto fill = line;
            fill.lineTo (plot.getRight(), yFor (0.0f));
            fill.lineTo (plot.getX(), yFor (0.0f));
            fill.closeSubPath();
            g.setColour (view.accentColour().withAlpha (0.16f));
            g.fillPath (fill);
            if (t.glow > 0.0f)
            {
                g.setColour (view.accentColour().withAlpha (0.25f));
                g.strokePath (line, juce::PathStrokeType (4.0f * t.glow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (view.accentColour());
            g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // band dots, the selected one last so that it is on top
        for (int pass = 0; pass < 2; ++pass)
            for (int b = 0; b < 8; ++b)
            {
                const bool selected = b == view.selectedBand();
                if (selected != (pass == 1))
                    continue;
                const auto c = point (b);
                const bool on = bandOn (b);
                const float radius = selected ? 7.5f : 6.5f;
                const auto dot = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
                if (on)
                {
                    g.setColour (bandColour (b));
                    g.fillEllipse (dot);
                    g.setColour (t.background.withAlpha (0.9f));
                }
                else
                {
                    g.setColour (t.panel2);
                    g.fillEllipse (dot);
                    g.setColour (t.dimText.withAlpha (0.8f));
                    g.drawEllipse (dot.reduced (0.5f), 1.0f);
                }
                g.setFont (t.numFont (9.0f, 600));
                g.drawText (juce::String (b + 1), dot, juce::Justification::centred, false);
                if (selected)
                {
                    g.setColour (t.text);
                    g.drawEllipse (dot.expanded (2.5f), 1.3f);
                }
            }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBand = bandAt (e.position);
        if (dragBand < 0)
            return;
        view.selectBand (dragBand);
        freqParam = param (dragBand, "freq");
        gainParam = param (dragBand, "gain");
        dragOpen = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0 || freqParam == nullptr || gainParam == nullptr || e.getDistanceFromDragStart() < 2)
            return;
        if (! dragOpen)
        {
            dragOpen = true;
            freqParam->beginChangeGesture();
            gainParam->beginChangeGesture();
        }
        const auto plot = plotArea();
        const float x = juce::jlimit (plot.getX(), plot.getRight(), e.position.x);
        const float f = 20.0f * std::pow (1000.0f, (x - plot.getX()) / plot.getWidth());
        freqParam->setValueNotifyingHost (freqParam->convertTo0to1 (juce::jlimit (20.0f, 20000.0f, f)));
        if (bandHasGain (kind (dragBand)))
        {
            const float db = (plot.getCentreY() - e.position.y) / (plot.getHeight() * 0.5f) * kRange;
            gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, db)));
        }
        tick();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragOpen)
        {
            freqParam->endChangeGesture();
            gainParam->endChangeGesture();
        }
        dragOpen = false;
        dragBand = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = bandAt (e.position);
        if (auto* on = b >= 0 ? param (b, "on") : nullptr)
        {
            on->beginChangeGesture();
            on->setValueNotifyingHost (on->getValue() > 0.5f ? 0.0f : 1.0f);
            on->endChangeGesture();
            tick();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        int b = bandAt (e.position);
        if (b < 0)
            b = view.selectedBand();
        if (auto* q = param (b, "q"))
        {
            view.selectBand (b);
            q->beginChangeGesture();
            q->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, q->getValue() + (w.isReversed ? -w.deltaY : w.deltaY) * 0.12f));
            q->endChangeGesture();
            tick();
        }
    }

private:
    static constexpr float kRange = 18.0f; // dB from the middle to the top of the plot

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (2.0f, 4.0f); }
    float xFor (float f) const
    {
        const auto plot = plotArea();
        return plot.getX() + plot.getWidth() * std::log (juce::jmax (20.0f, f) / 20.0f) / std::log (1000.0f);
    }
    float yFor (float db) const
    {
        const auto plot = plotArea();
        return plot.getCentreY() - db / kRange * plot.getHeight() * 0.5f;
    }

    juce::RangedAudioParameter* param (int b, const char* what) const
    {
        return view.param (("b" + juce::String (b + 1) + what).toRawUTF8());
    }
    float bandValue (int b, const char* what) const
    {
        auto* p = param (b, what);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }
    bool bandOn (int b) const { return bandValue (b, "on") > 0.5f; }
    int kind (int b) const { return juce::roundToInt (bandValue (b, "type")); }

    juce::Point<float> point (int b) const
    {
        const auto plot = plotArea();
        const float db = bandHasGain (kind (b)) ? bandValue (b, "gain") : 0.0f;
        return { xFor (bandValue (b, "freq")), juce::jlimit (plot.getY() + 7.0f, plot.getBottom() - 7.0f, yFor (db)) };
    }

    int bandAt (juce::Point<float> p) const
    {
        int best = -1;
        float bestDistance = 11.0f;
        for (int b = 0; b < 8; ++b)
            if (const float d = point (b).getDistanceFrom (p); d < bestDistance)
            {
                best = b;
                bestDistance = d;
            }
        return best;
    }

    void recompute()
    {
        const int n = juce::jmax (2, getWidth() / 2);
        if (shadow == nullptr)
            return;
        const auto& ps = view.processor().getParameters();
        for (int i = 0; i < ps.size(); ++i)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (ps[i]))
                shadow->setParam (i, r->convertFrom0to1 (r->getValue()));
        freqs.resize ((size_t) n);
        curve.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            freqs[(size_t) i] = 20.0f * std::pow (1000.0f, (float) i / (float) (n - 1));
        if (! shadow->response (freqs.data(), curve.data(), n))
            std::fill (curve.begin(), curve.end(), 0.0f);
    }

    std::unique_ptr<daw::fx::Effect> shadow;
    std::vector<float> freqs, curve;
    int dragBand = -1;
    bool dragOpen = false;
    juce::RangedAudioParameter *freqParam = nullptr, *gainParam = nullptr;
};

//==============================================================================
// Compressor, gate and limiter: the transfer curve with the input level riding on it, and a gain reduction meter.
class DynamicsDisplay final : public Display
{
public:
    explicit DynamicsDisplay (EffectView& owner) : Display (owner)
    {
        setHelpText (Strings::help::dynamicsDisplay);
        const auto& type = view.type();
        lo = type == "gate" ? -80.0f : (type == "limiter" ? -36.0f : -60.0f);
    }

    void tick() override
    {
        const float gr = view.processor().readout (0), in = view.processor().readout (1), open = view.processor().readout (2);
        const bool moved = std::abs (gr - shownGr) > 0.05f || std::abs (in - shownIn) > 0.2f || ! juce::exactlyEqual (open, shownOpen);
        if (paramsChanged() || moved)
        {
            shownGr = gr;
            shownIn = in;
            shownOpen = open;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = background (g).reduced (6.0f);
        auto meter = r.removeFromRight (9.0f);
        r.removeFromRight (7.0f);
        const auto label = meter.removeFromBottom (12.0f);
        const float side = juce::jmin (r.getWidth(), r.getHeight());
        const auto plot = juce::Rectangle<float> (side, side).withCentre (r.getCentre());

        const auto xFor = [&] (float db) { return plot.getX() + (db - lo) / -lo * plot.getWidth(); };
        const auto yFor = [&] (float db) { return plot.getBottom() - (juce::jlimit (lo, 0.0f, db) - lo) / -lo * plot.getHeight(); };

        // frame, unity line, threshold
        g.setColour (t.line.withAlpha (0.6f));
        g.drawRect (plot, 1.0f);
        juce::Path unity;
        unity.startNewSubPath (plot.getBottomLeft());
        unity.lineTo (plot.getTopRight());
        const float dashes[] { 3.0f, 3.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dashes, 2);
        g.fillPath (dashed);
        const float knee = threshold();
        g.setColour (view.accentColour().withAlpha (0.45f));
        if (view.type() == "limiter")
            g.fillRect (plot.getX(), yFor (knee), plot.getWidth(), 1.0f);
        else
            g.fillRect (xFor (knee), plot.getY(), 1.0f, plot.getHeight());

        juce::Path curve;
        constexpr int steps = 96;
        for (int i = 0; i <= steps; ++i)
        {
            const float in = lo + (0.0f - lo) * (float) i / (float) steps;
            const auto p = juce::Point<float> (xFor (in), yFor (transfer (in)));
            if (i == 0)
                curve.startNewSubPath (p);
            else
                curve.lineTo (p);
        }
        auto under = curve;
        under.lineTo (plot.getBottomRight());
        under.lineTo (plot.getBottomLeft());
        under.closeSubPath();
        g.setColour (view.accentColour().withAlpha (0.12f));
        g.fillPath (under);
        g.setColour (t.text);
        g.strokePath (curve, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // the input level on the curve
        if (shownIn > lo + 0.5f)
        {
            const auto dot = juce::Point<float> (xFor (juce::jmin (0.0f, shownIn)), yFor (transfer (juce::jmin (0.0f, shownIn))));
            if (t.glow > 0.0f)
            {
                g.setColour (view.accentColour().withAlpha (0.3f));
                g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (dot));
            }
            g.setColour (view.accentColour());
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
        }

        // gate: open lamp
        if (view.type() == "gate")
        {
            const auto lamp = juce::Rectangle<float> (7.0f, 7.0f).withPosition (plot.getX() + 5.0f, plot.getY() + 5.0f);
            g.setColour (shownOpen > 0.5f ? t.play : t.line);
            g.fillEllipse (lamp);
        }

        // gain reduction meter, from the top down
        fillCell (g, meter, t.panel, 0.4f);
        const float range = view.type() == "gate" ? 40.0f : 24.0f;
        const float fill = juce::jlimit (0.0f, 1.0f, shownGr / range);
        if (fill > 0.0f)
        {
            g.setColour (t.record);
            g.fillRoundedRectangle (meter.withHeight (meter.getHeight() * fill).reduced (1.0f, 0.0f), 1.5f);
        }
        g.setColour (t.dimText);
        g.setFont (t.numFont (9.0f));
        g.drawText (Strings::fx::gr, label.expanded (6.0f, 0.0f), juce::Justification::centredBottom, false);
    }

private:
    float threshold() const
    {
        if (view.type() == "limiter")
            return v ("ceiling");
        return v ("threshold");
    }

    float transfer (float in) const
    {
        const auto& type = view.type();
        if (type == "limiter")
            return juce::jmin (in + v ("gain"), v ("ceiling"));
        if (type == "gate")
            return in >= v ("threshold") ? in : in + v ("floor");
        const float t = v ("threshold"), ratio = juce::jmax (1.0f, v ("ratio")), knee = v ("knee");
        float out = in;
        if (knee > 0.0f && std::abs (in - t) <= knee * 0.5f)
        {
            const float d = in - t + knee * 0.5f;
            out = in + (1.0f / ratio - 1.0f) * d * d / (2.0f * knee);
        }
        else if (in > t)
            out = t + (in - t) / ratio;
        return out + v ("makeup");
    }

    float lo = -60.0f;
    float shownGr = 0.0f, shownIn = -100.0f, shownOpen = 0.0f;
};

//==============================================================================
// Reverb: the decay of the tail after the pre-delay, denser with diffusion, with early reflections that grow with size.
class ReverbDisplay final : public Display
{
public:
    using Display::Display;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = background (g).reduced (6.0f, 6.0f);
        const float decay = juce::jmax (0.2f, v ("decay")), predelay = v ("predelay") / 1000.0f;
        const float size = v ("size") / 100.0f, diffusion = v ("diffusion") / 100.0f, width = v ("width") / 100.0f;
        const bool frozen = v ("freeze") > 0.5f;
        // A window that fits the tail, in whole seconds so that the scale reads easily.
        float window = 1.0f;
        for (float w : { 1.0f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f, 20.0f })
        {
            window = w;
            if (predelay + decay * 1.1f <= w)
                break;
        }
        const auto plot = r.withTrimmedBottom (12.0f);
        const float mid = plot.getCentreY(), half = plot.getHeight() * 0.5f;

        g.setColour (t.line.withAlpha (0.7f));
        g.fillRect (plot.getX(), mid, plot.getWidth(), 1.0f);

        juce::Random random (7);
        const auto colour = t.accent2;
        for (float x = 0.0f; x < plot.getWidth(); x += 2.0f)
        {
            const float time = x / plot.getWidth() * window;
            float a = 0.0f;
            if (time >= predelay)
            {
                const float tail = time - predelay;
                a = frozen ? 0.85f : std::pow (10.0f, -3.0f * tail / decay); // -60 dB at the decay time
                a = std::sqrt (a);
                // early reflections: sparse spikes over the first 20-80 ms, longer in a bigger room
                if (tail < 0.02f + 0.06f * size)
                    a *= random.nextFloat() < 0.35f ? 1.0f : 0.25f;
            }
            const float grain = 1.0f - (1.0f - diffusion) * random.nextFloat();
            const float up = a * grain * (0.55f + 0.45f * random.nextFloat());
            const float down = a * grain * (1.0f - (1.0f - width) * 0.6f) * (0.55f + 0.45f * random.nextFloat());
            if (up <= 0.0f)
                continue;
            g.setColour (colour.withAlpha (0.35f + 0.5f * a));
            g.fillRect (plot.getX() + x, mid - up * half, 1.4f, up * half + down * half);
        }

        // the direct sound and the pre-delay
        g.setColour (t.text.withAlpha (0.8f));
        g.fillRect (plot.getX(), plot.getY() + 4.0f, 1.5f, plot.getHeight() - 8.0f);

        // time scale
        g.setColour (t.dimText);
        g.setFont (t.numFont (9.0f));
        const auto scale = r.withTop (plot.getBottom());
        g.drawText ("0", scale, juce::Justification::bottomLeft, false);
        g.drawText (Strings::fx::seconds (window), scale, juce::Justification::bottomRight, false);
    }
};

//==============================================================================
// Delay: the echoes as bars, left channel up and right channel down, fading with the feedback.
class DelayDisplay final : public Display
{
public:
    using Display::Display;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = background (g).reduced (6.0f);
        const bool sync = v ("sync") > 0.5f, link = v ("link") > 0.5f, ping = v ("pingPong") > 0.5f, frozen = v ("freeze") > 0.5f;
        // times in beats when synced, in ms otherwise: the picture only needs the proportions
        float left = sync ? (float) division ("divL") : v ("timeL");
        float right = link ? left : (sync ? (float) division ("divR") : v ("timeR"));
        left = juce::jmax (left, 1.0e-3f);
        right = juce::jmax (right, 1.0e-3f);
        const float feedback = frozen ? 1.0f : v ("feedback") / 100.0f;
        const float window = juce::jmax (left, right) * (ping ? 7.0f : 6.5f);
        const float mid = r.getCentreY(), half = r.getHeight() * 0.5f - 2.0f;

        g.setColour (t.line.withAlpha (0.7f));
        g.fillRect (r.getX(), mid, r.getWidth(), 1.0f);
        g.setColour (t.text.withAlpha (0.85f));
        g.fillRect (r.getX(), mid - half, 2.0f, half * 2.0f);

        const auto tap = [&] (float time, float amp, bool up) {
            if (time > window || amp < 0.02f)
                return;
            const float x = r.getX() + time / window * r.getWidth();
            const float h = half * juce::jlimit (0.0f, 1.0f, amp);
            const auto c = up ? view.accentColour() : t.accent2;
            g.setColour (c.withAlpha (0.35f + 0.65f * amp));
            g.fillRect (x - 1.0f, up ? mid - h : mid + 1.0f, 2.5f, h);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x + 0.25f, up ? mid - h : mid + 1.0f + h }));
        };
        if (ping)
        {
            float time = 0.0f, amp = 1.0f;
            for (int n = 0; n < 24; ++n)
            {
                time += n % 2 == 0 ? left : right;
                tap (time, amp, n % 2 == 0);
                amp *= feedback;
            }
        }
        else
        {
            float amp = 1.0f;
            for (int n = 1; n <= 24; ++n)
            {
                tap (left * (float) n, amp, true);
                tap (right * (float) n, amp, false);
                amp *= feedback;
            }
        }
        g.setColour (t.dimText);
        g.setFont (t.numFont (9.0f));
        g.drawText (Strings::fx::channelL, r.withHeight (12.0f), juce::Justification::topRight, false);
        g.drawText (Strings::fx::channelR, r.withTrimmedTop (r.getHeight() - 12.0f), juce::Justification::bottomRight, false);
    }

private:
    double division (const char* id) const
    {
        auto* p = view.param (id);
        if (p == nullptr)
            return 1.0;
        const auto names = choicesOf (*p);
        const int index = juce::roundToInt (v (id));
        return juce::isPositiveAndBelow (index, names.size()) ? divisionBeats (names[index]) : 1.0;
    }
};

//==============================================================================
// Drive: the shaping curve of the chosen mode at the current drive and bias (the same curves as the effect, Drive.cpp).
class DriveDisplay final : public Display
{
public:
    using Display::Display;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = background (g).reduced (8.0f);
        const float side = juce::jmin (r.getWidth(), r.getHeight());
        const auto plot = juce::Rectangle<float> (side, side).withCentre (r.getCentre());
        g.setColour (t.line.withAlpha (0.6f));
        g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);
        g.fillRect (plot.getCentreX(), plot.getY(), 1.0f, plot.getHeight());
        juce::Path unity;
        unity.startNewSubPath (plot.getBottomLeft());
        unity.lineTo (plot.getTopRight());
        juce::Path dashed;
        const float dashes[] { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dashes, 2);
        g.fillPath (dashed);

        const int mode = juce::roundToInt (v ("mode"));
        const double gain = std::pow (10.0, (double) (v ("drive") + v ("input")) / 20.0);
        const double bias = (double) v ("bias") * 0.005;
        const double mix = (double) v ("mix") / 100.0;
        juce::Path curve;
        constexpr int steps = 160;
        for (int i = 0; i <= steps; ++i)
        {
            const double x = -1.0 + 2.0 * i / steps;
            const double wet = shape (mode, x * gain + bias) - shape (mode, bias);
            const double y = juce::jlimit (-1.0, 1.0, mix * wet + (1.0 - mix) * x);
            const auto p = juce::Point<float> (plot.getCentreX() + (float) x * side * 0.5f, plot.getCentreY() - (float) y * side * 0.5f);
            if (i == 0)
                curve.startNewSubPath (p);
            else
                curve.lineTo (p);
        }
        if (t.glow > 0.0f)
        {
            g.setColour (view.accentColour().withAlpha (0.25f));
            g.strokePath (curve, juce::PathStrokeType (4.0f * t.glow));
        }
        g.setColour (view.accentColour());
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

private:
    static double shape (int mode, double x)
    {
        switch (mode)
        {
            case 1: return 0.6 * std::tanh (4.0 / 3.0 * x) + 0.4 * std::tanh (0.5 * x); // tape
            case 2: return x >= 0.0 ? std::tanh (x) : 0.7 * std::tanh (x / 0.7);         // tube
            case 3: return juce::jlimit (-1.0, 1.0, x);                                  // hard
            case 4:                                                                      // fold
            {
                const double u = x - 4.0 * std::floor ((x + 1.0) * 0.25);
                return u <= 1.0 ? u : 2.0 - u;
            }
            case 5: return juce::jlimit (-1.0, 1.0, std::round (x * 8.0) / 8.0);         // crush
            default: return std::tanh (x);                                               // soft
        }
    }
};

//==============================================================================
// Bitcrusher: one cycle of a sine and what the bit depth and the sample rate make of it.
class CrushDisplay final : public Display
{
public:
    using Display::Display;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = background (g).reduced (8.0f);
        const float bits = juce::jmax (1.0f, v ("bits")), rate = juce::jmax (100.0f, v ("rate"));
        const double levels = std::pow (2.0, (double) bits - 1.0);
        const double hold = juce::jmax (1.0, 48000.0 / (double) rate);
        constexpr int cycle = 240; // samples of the drawn cycle at 48 kHz (200 Hz)
        juce::Path clean, crushed;
        double held = 0.0;
        for (int n = 0; n <= cycle; ++n)
        {
            const double s = 0.85 * std::sin (2.0 * juce::MathConstants<double>::pi * n / cycle);
            if (n == 0 || std::floor (n / hold) > std::floor ((n - 1) / hold))
                held = std::round (s * levels) / levels;
            const float x = r.getX() + r.getWidth() * (float) n / (float) cycle;
            const auto pc = juce::Point<float> (x, r.getCentreY() - (float) s * r.getHeight() * 0.5f);
            const auto pk = juce::Point<float> (x, r.getCentreY() - (float) held * r.getHeight() * 0.5f);
            if (n == 0)
            {
                clean.startNewSubPath (pc);
                crushed.startNewSubPath (pk);
            }
            else
            {
                clean.lineTo (pc);
                crushed.lineTo (pk);
            }
        }
        g.setColour (t.line);
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
        g.setColour (t.dimText.withAlpha (0.6f));
        g.strokePath (clean, juce::PathStrokeType (1.0f));
        g.setColour (view.accentColour());
        g.strokePath (crushed, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    }
};

//==============================================================================
// Filter: the magnitude response of the chosen type at the cutoff the effect is at right now (LFO and envelope included).
class FilterDisplay final : public Display
{
public:
    using Display::Display;

    void tick() override
    {
        const float cutoff = view.processor().readout (0);
        if (paramsChanged() || std::abs (cutoff - shownCutoff) > shownCutoff * 0.003f)
        {
            shownCutoff = cutoff;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = background (g).reduced (2.0f, 4.0f);
        const float cutoff = shownCutoff > 1.0f ? shownCutoff : v ("freq");
        const float q = 0.6f + v ("resonance") / 100.0f * 7.0f;
        const int type = juce::roundToInt (v ("type"));
        constexpr float top = 18.0f, bottom = -36.0f;
        const auto yFor = [&] (float db) { return r.getY() + (top - juce::jlimit (bottom, top, db)) / (top - bottom) * r.getHeight(); };
        const auto xFor = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };

        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            g.setColour (t.line.withAlpha (0.8f));
            g.fillRect (xFor (f), r.getY(), 1.0f, r.getHeight());
        }
        g.setColour (t.line);
        g.fillRect (r.getX(), yFor (0.0f), r.getWidth(), 1.0f);

        juce::Path curve;
        const int steps = juce::jmax (2, (int) r.getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / (float) steps);
            const auto p = juce::Point<float> (r.getX() + r.getWidth() * (float) i / (float) steps, yFor (magnitudeDb (type, f / cutoff, q)));
            if (i == 0)
                curve.startNewSubPath (p);
            else
                curve.lineTo (p);
        }
        auto fill = curve;
        fill.lineTo (r.getBottomRight());
        fill.lineTo (r.getBottomLeft());
        fill.closeSubPath();
        g.setColour (view.accentColour().withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (view.accentColour());
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (t.text.withAlpha (0.5f));
        g.fillRect (xFor (cutoff), r.getY(), 1.0f, r.getHeight());
    }

private:
    // Second-order sections in closed form; 24 dB types and the ladder are two of them. x = f / cutoff.
    static float magnitudeDb (int type, float x, float q)
    {
        const auto denom = [] (float xx, float qq) { return std::sqrt ((1.0f - xx * xx) * (1.0f - xx * xx) + (xx / qq) * (xx / qq)); };
        const auto lp = [&] (float qq) { return 1.0f / denom (x, qq); };
        const auto hp = [&] (float qq) { return x * x / denom (x, qq); };
        float m = 1.0f;
        switch (type)
        {
            case 0: m = lp (q); break;
            case 1: m = lp (0.54f) * lp (q * 1.3f); break;
            case 2: m = hp (q); break;
            case 3: m = hp (0.54f) * hp (q * 1.3f); break;
            case 4: m = (x / q) / denom (x, q); break;
            case 5: m = std::abs (1.0f - x * x) / denom (x, q); break;
            default: m = lp (0.54f) * lp (q * 1.5f); break; // ladder
        }
        return 20.0f * std::log10 (juce::jmax (1.0e-5f, m));
    }

    float shownCutoff = 0.0f;
};

//==============================================================================
// Chorus, flanger, phaser and auto-pan: two cycles of the LFO, left channel and right channel (offset by the stereo phase).
class LfoDisplay final : public Display
{
public:
    using Display::Display;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto r = background (g).reduced (6.0f, 8.0f);
        const auto& type = view.type();
        const int shapeIndex = type == "autopan" ? juce::roundToInt (v ("shape")) : 0;
        const float depth = juce::jlimit (0.05f, 1.0f, v ("depth") / 100.0f);
        const float offset = type == "chorus" ? 0.25f * v ("width") / 100.0f : v ("phase") / 360.0f;
        const float start = type == "autopan" ? v ("offset") / 360.0f : 0.0f;
        const int voices = type == "chorus" ? juce::roundToInt (v ("voices")) + 1 : 1;

        g.setColour (t.line.withAlpha (0.7f));
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

        const auto wave = [&] (float phaseOffset, juce::Colour c, float thickness) {
            juce::Path p;
            constexpr int steps = 180;
            for (int i = 0; i <= steps; ++i)
            {
                const float phase = 2.0f * (float) i / (float) steps + start + phaseOffset;
                const float y = shape (shapeIndex, phase - std::floor (phase), (int) std::floor (phase));
                const auto pt = juce::Point<float> (r.getX() + r.getWidth() * (float) i / (float) steps, r.getCentreY() - y * depth * r.getHeight() * 0.5f);
                if (i == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        for (int voice = voices - 1; voice >= 1; --voice)
            wave ((float) voice / (float) voices, t.dimText.withAlpha (0.45f), 1.0f);
        if (offset > 0.001f)
            wave (offset, t.accent2.withAlpha (0.85f), 1.4f);
        wave (0.0f, view.accentColour(), 1.8f);
        g.setColour (t.dimText);
        g.setFont (t.numFont (9.0f));
        g.drawText (Strings::fx::channelL, r.withHeight (11.0f), juce::Justification::topLeft, false);
        if (offset > 0.001f)
        {
            g.setColour (t.accent2);
            g.drawText (Strings::fx::channelR, r.withHeight (11.0f).translated (10.0f, 0.0f), juce::Justification::topLeft, false);
        }
    }

private:
    // One LFO cycle, phase 0..1, value -1..1 (Sine, Triangle, Saw, Square, S&H)
    static float shape (int index, float phase, int cycle)
    {
        switch (index)
        {
            case 1: return 1.0f - 4.0f * std::abs (phase - 0.5f);
            case 2: return 2.0f * phase - 1.0f;
            case 3: return phase < 0.5f ? 1.0f : -1.0f;
            case 4:
            {
                juce::Random random (cycle * 4 + (int) (phase * 4.0f) + 11);
                return random.nextFloat() * 2.0f - 1.0f;
            }
            default: return std::sin (2.0f * kPi * phase);
        }
    }
};
} // namespace

//==============================================================================
EffectView::EffectView (BuiltInEffectProcessor& p, juce::Colour ring, bool expandedForm)
    : proc (p), effectType (p.getType()), accent (ring), expanded (expandedForm)
{
    if (const auto it = rememberedBands().find (&proc); it != rememberedBands().end())
        band = it->second;
    build();
}

EffectView::~EffectView() = default;

juce::RangedAudioParameter* EffectView::param (const char* id) const
{
    auto* effect = proc.getEffect();
    if (effect == nullptr)
        return nullptr;
    const int index = effect->paramIndex (id);
    if (index < 0 || index >= proc.getParameters().size())
        return nullptr;
    return dynamic_cast<juce::RangedAudioParameter*> (proc.getParameters()[index]);
}

float EffectView::value (const char* id) const
{
    auto* p = param (id);
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

ParamControl* EffectView::make (const Item& item)
{
    auto* p = param (item.id.toRawUTF8());
    if (p == nullptr)
        return nullptr;
    const auto caption = item.bare ? juce::String() : (item.label.isNotEmpty() ? item.label : Strings::fx::caption (item.id));
    std::unique_ptr<ParamControl> control;
    switch (item.kind)
    {
        case ControlKind::knob: control = std::make_unique<ParamKnob> (*p, caption, accent); break;
        case ControlKind::choice: control = std::make_unique<ChoiceBox> (*p, caption); break;
        case ControlKind::segmented: control = std::make_unique<SegmentedChoice> (*p, caption); break;
        case ControlKind::toggle: control = std::make_unique<ParamToggle> (*p, caption); break;
        case ControlKind::box: control = std::make_unique<ValueBox> (*p, caption); break;
    }
    if (item.kind == ControlKind::toggle && effectType == "eq8" && item.id.startsWithChar ('b'))
        static_cast<ParamToggle*> (control.get())->setLampColour (bandColour (item.id.substring (1, 2).getIntValue() - 1));
    addChildComponent (*control);
    controls.push_back (std::move (control));
    return controls.back().get();
}

void EffectView::build()
{
    spec = specFor (effectType);
    using P = Spec::Picture;
    switch (spec->picture)
    {
        case P::eq: display = std::make_unique<EqDisplay> (*this); break;
        case P::dynamics: display = std::make_unique<DynamicsDisplay> (*this); break;
        case P::reverb: display = std::make_unique<ReverbDisplay> (*this); break;
        case P::delay: display = std::make_unique<DelayDisplay> (*this); break;
        case P::drive: display = std::make_unique<DriveDisplay> (*this); break;
        case P::crush: display = std::make_unique<CrushDisplay> (*this); break;
        case P::filter: display = std::make_unique<FilterDisplay> (*this); break;
        case P::lfo: display = std::make_unique<LfoDisplay> (*this); break;
        case P::none: break;
    }
    if (display != nullptr)
        addAndMakeVisible (*display);

    const auto add = [this] (const Item& item, int section, int groupIndex) {
        if (make (item) != nullptr)
            placed.push_back ({ item, section, groupIndex });
    };
    if (! expanded)
    {
        for (const auto& i : spec->top)
            add (i, 0, -1);
        for (const auto& i : spec->knobs)
            add (i, 1, -1);
        for (const auto& i : spec->bottom)
            add (i, 2, -1);
    }
    else
    {
        // Every parameter has a place: what the groups leave out goes into a last group.
        std::set<juce::String> listed;
        for (const auto& g : spec->groups)
        {
            if (g.titleItem)
                listed.insert (g.titleItem->id);
            for (const auto& i : g.items)
                listed.insert (i.id);
        }
        Group rest = group (Strings::fx::groups::other, {});
        if (auto* effect = proc.getEffect())
            for (const auto& info : effect->params())
                if (listed.count (juce::String (info.id)) == 0)
                    rest.items.push_back (info.stepped() ? (info.choices.size() == 2 ? T (info.id) : C (info.id)) : K (info.id));
        if (! rest.items.empty())
            spec->groups.push_back (rest);

        for (int gi = 0; gi < (int) spec->groups.size(); ++gi)
        {
            const auto& g = spec->groups[(size_t) gi];
            if (g.titleItem)
                add (*g.titleItem, 4, gi);
            for (const auto& i : g.items)
                add (i, 3, gi);
        }
    }
    lastConditions = conditionState();
}

bool EffectView::shown (const Item& item) const
{
    if (item.band >= 0 && item.band != band)
        return false;
    return item.showIf.isEmpty() || juce::roundToInt (value (item.showIf.toRawUTF8())) == item.showValue;
}

bool EffectView::dimmed (const Item& item) const
{
    return item.dimIf.isNotEmpty() && juce::roundToInt (value (item.dimIf.toRawUTF8())) == item.dimValue;
}

juce::String EffectView::conditionState() const
{
    juce::String s;
    for (const auto& p : placed)
        s << (shown (p.item) ? '1' : '0') << (dimmed (p.item) ? '1' : '0');
    return s;
}

void EffectView::selectBand (int b)
{
    b = juce::jlimit (0, 7, b);
    rememberedBands()[&proc] = b;
    if (b == band)
        return;
    band = b;
    resized();
    if (display != nullptr)
        display->repaint();
}

ParamControl* EffectView::controlFor (const juce::String& paramId) const
{
    for (size_t i = 0; i < controls.size(); ++i)
        if (controls[i]->paramId() == paramId && shown (placed[i].item))
            return controls[i].get();
    return nullptr;
}

void EffectView::tick()
{
    for (auto& c : controls)
        c->refresh();
    if (display != nullptr)
        display->tick();
    if (const auto state = conditionState(); state != lastConditions)
    {
        lastConditions = state;
        resized();
    }
}

juce::String EffectView::headerNote() const
{
    if (effectType == "comp" || effectType == "gate" || effectType == "limiter")
    {
        const float gr = proc.readout (0);
        return gr >= 0.1f ? Strings::fx::decibels (-gr) : juce::String();
    }
    if (effectType == "eq8")
    {
        int on = 0;
        for (int b = 1; b <= 8; ++b)
            if (value (("b" + juce::String (b) + "on").toRawUTF8()) > 0.5f)
                ++on;
        return Strings::fx::bands (on);
    }
    if (effectType == "reverb")
        return Strings::fx::seconds (value ("decay"));
    return {};
}

//==============================================================================
namespace
{
// Lays out a row of controls left to right at their ideal widths; what is left over goes to the boxes and the choices.
void layoutRow (juce::Rectangle<int> row, const std::vector<ParamControl*>& items)
{
    if (items.empty())
        return;
    int total = 0, stretchable = 0;
    for (auto* c : items)
    {
        total += c->idealWidth();
        if (dynamic_cast<ValueBox*> (c) != nullptr || dynamic_cast<ChoiceBox*> (c) != nullptr)
            ++stretchable;
    }
    total += EffectView::kGap * (int) (items.size() - 1);
    const int spare = row.getWidth() - total;
    // Boxes grow a little to even out a row, but not so much that the row looks stretched.
    const int extra = spare > 0 && stretchable > 0 ? juce::jmin (spare / stretchable, 18) : 0;
    const float squeeze = spare < 0 ? (float) (row.getWidth() - EffectView::kGap * (int) (items.size() - 1)) / (float) (total - EffectView::kGap * (int) (items.size() - 1)) : 1.0f;
    int x = row.getX();
    for (auto* c : items)
    {
        const bool stretch = dynamic_cast<ValueBox*> (c) != nullptr || dynamic_cast<ChoiceBox*> (c) != nullptr;
        const int w = (int) ((float) (c->idealWidth() + (stretch ? extra : 0)) * squeeze);
        c->setBounds (x, row.getY(), w, row.getHeight());
        x += w + EffectView::kGap;
    }
}
} // namespace

int EffectView::preferredWidth (int height) const
{
    if (expanded)
        return layoutExpanded ({ 0, 0, 100000, juce::jmax (60, height) }, false);
    if (effectType == "eq8")
        return 336;
    int knobsWidth = 0, topWidth = 0, bottomWidth = 0, topCount = 0, bottomCount = 0;
    int knobCount = 0;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        const auto& p = placed[i];
        if (! shown (p.item))
            continue;
        if (p.section == 0)
        {
            topWidth += controls[i]->idealWidth();
            ++topCount;
        }
        else if (p.section == 2)
        {
            bottomWidth += controls[i]->idealWidth();
            ++bottomCount;
        }
        else if (p.section == 1)
            ++knobCount;
    }
    if (knobCount > 0)
        knobsWidth = juce::jmin (knobCount, spec->columns) * kKnobWidth;
    topWidth += kGap * juce::jmax (0, topCount - 1);
    bottomWidth += kGap * juce::jmax (0, bottomCount - 1);
    const int body = (display != nullptr ? spec->pictureWidth : 0) + (display != nullptr && knobsWidth > 0 ? kGap + 4 : 0) + knobsWidth;
    return juce::jlimit (176, 336, juce::jmax (body, topWidth, bottomWidth));
}

void EffectView::resized()
{
    for (size_t i = 0; i < controls.size(); ++i)
    {
        controls[i]->setVisible (shown (placed[i].item));
        controls[i]->setEnabled (! dimmed (placed[i].item));
    }
    if (expanded)
        layoutExpanded (getLocalBounds(), true);
    else
        layoutCompact (getLocalBounds());
    repaint();
}

void EffectView::layoutCompact (juce::Rectangle<int> area)
{
    std::vector<ParamControl*> top, bottom, knobs;
    for (size_t i = 0; i < placed.size(); ++i)
        if (shown (placed[i].item))
            (placed[i].section == 0 ? top : (placed[i].section == 2 ? bottom : knobs)).push_back (controls[i].get());
    auto r = area;
    if (! top.empty())
    {
        layoutRow (r.removeFromTop (kRowHeight), top);
        r.removeFromTop (kGap);
    }
    if (! bottom.empty())
    {
        layoutRow (r.removeFromBottom (kRowHeight), bottom);
        r.removeFromBottom (kGap);
    }
    if (knobs.empty())
    {
        if (display != nullptr)
            display->setBounds (r);
        return;
    }
    const int cols = juce::jmin ((int) knobs.size(), spec->columns);
    const int rows = ((int) knobs.size() + cols - 1) / cols;
    const int gridW = cols * kKnobWidth;
    if (display != nullptr)
    {
        display->setBounds (r.removeFromLeft (juce::jmax (60, r.getWidth() - gridW - kGap - 4)));
        r.removeFromLeft (kGap + 4);
    }
    const int rowGap = 4;
    const int cellH = juce::jlimit (36, kKnobHeight, (r.getHeight() - rowGap * (rows - 1)) / rows);
    const int gridH = rows * cellH + rowGap * (rows - 1);
    const int y0 = r.getY() + juce::jmax (0, (r.getHeight() - gridH) / 2);
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int col = (int) i % cols, row = (int) i / cols;
        knobs[i]->setBounds (r.getX() + col * kKnobWidth, y0 + row * (cellH + rowGap), kKnobWidth, cellH);
    }
}

int EffectView::layoutExpanded (juce::Rectangle<int> area, bool apply) const
{
    constexpr int titleHeight = 18, rowGap = 6, columnGap = 10, groupGap = 22;
    int x = area.getX();
    if (display != nullptr)
    {
        if (apply)
            display->setBounds (x, area.getY(), spec->pictureWidthExpanded, area.getHeight());
        x += spec->pictureWidthExpanded + groupGap;
    }
    if (apply)
    {
        titles.clear();
        separators.clear();
    }
    const int top = area.getY(), bottom = area.getBottom();
    for (int gi = 0; gi < (int) spec->groups.size(); ++gi)
    {
        const auto& g = spec->groups[(size_t) gi];
        const int colW = g.width > 0 ? g.width : 2 * kKnobWidth;
        int y = top, columnX = x, right = x + colW;
        // the title, or the switch that stands in for it
        bool titled = false;
        for (size_t i = 0; i < placed.size(); ++i)
            if (placed[i].group == gi && placed[i].section == 4)
            {
                if (apply)
                    controls[i]->setBounds (x, y, colW, kRowHeight);
                y += kRowHeight + rowGap;
                titled = true;
            }
        if (! titled)
        {
            if (apply)
                titles.push_back ({ { x, y, colW, 14 }, g.title });
            y += titleHeight;
        }
        const int firstY = y;
        int pairX = -1, pairY = 0; // a knob waiting for its neighbour on the right
        for (size_t i = 0; i < placed.size(); ++i)
        {
            const auto& p = placed[i];
            if (p.group != gi || p.section != 3 || ! shown (p.item))
                continue;
            const bool knob = p.item.kind == ControlKind::knob;
            if (knob && pairX >= 0 && colW >= 2 * kKnobWidth)
            {
                if (apply)
                    controls[i]->setBounds (pairX + kKnobWidth, pairY, kKnobWidth, kKnobHeight);
                pairX = -1;
                continue;
            }
            pairX = -1;
            const int h = knob ? kKnobHeight : kRowHeight;
            if (y + h > bottom && y > firstY)
            {
                columnX = right + columnGap;
                right = columnX + colW;
                y = firstY;
            }
            if (apply)
                controls[i]->setBounds (columnX, y, knob ? kKnobWidth : colW, h);
            if (knob)
            {
                pairX = columnX;
                pairY = y;
            }
            y += h + (knob ? 4 : rowGap);
        }
        x = right;
        if (gi + 1 < (int) spec->groups.size())
        {
            if (apply)
                separators.push_back (x + groupGap / 2);
            x += groupGap;
        }
    }
    return x - area.getX();
}

void EffectView::paint (juce::Graphics& g)
{
    if (! expanded)
        return;
    const auto& t = theme();
    g.setFont (t.uiFont (10.0f, 600));
    for (const auto& [r, text] : titles)
    {
        g.setColour (t.dimText);
        g.drawText (text.toUpperCase(), r, juce::Justification::centredLeft, true);
    }
    g.setColour (t.line);
    for (int x : separators)
        g.fillRect (x, 2, 1, getHeight() - 4);
}

//==============================================================================
EffectEditor::EffectEditor (BuiltInEffectProcessor& p) : AudioProcessorEditor (p), effectView (p, theme().accent, true)
{
    addAndMakeVisible (effectView);
    constexpr int height = 300, margin = 18;
    setSize (juce::jmax (360, effectView.preferredWidth (height - 2 * margin) + 2 * margin), height);
    startTimerHz (30);
}

EffectEditor::~EffectEditor() = default;

void EffectEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme().panel);
}

void EffectEditor::resized()
{
    effectView.setBounds (getLocalBounds().reduced (18));
}
} // namespace ui
