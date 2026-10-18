#include "DrumPads.h"

#include "../Strings.h"
#include "../browser/BrowserActions.h"
#include "../browser/BrowserCatalog.h"
#include "../engine/AudioEngine.h"
#include "../engine/DrumKitProcessor.h"
#include "../project/AudioImport.h"
#include "Theme.h"
#include "UiCommon.h"

#include "daw/drums/DrumKit.h"

namespace ui
{
namespace
{
using daw::drums::PadParam;

struct KnobSpec
{
    PadParam param;
    const juce::String* caption;
};

const std::vector<KnobSpec>& knobSpecs()
{
    static const std::vector<KnobSpec> specs { { PadParam::tune, &Strings::drums::tune },   { PadParam::decay, &Strings::drums::decay },
                                               { PadParam::cutoff, &Strings::drums::cutoff }, { PadParam::drive, &Strings::drums::drive },
                                               { PadParam::volumeDb, &Strings::drums::volume }, { PadParam::pan, &Strings::drums::pan } };
    return specs;
}

double normalise (PadParam p, float v)
{
    const auto& info = daw::drums::padParamInfo (p);
    if (p == PadParam::cutoff || p == PadParam::decay) // frequencies and times feel right on a log scale
        return std::log (v / info.minValue) / std::log (info.maxValue / info.minValue);
    return (v - info.minValue) / (info.maxValue - info.minValue);
}

float denormalise (PadParam p, double n)
{
    const auto& info = daw::drums::padParamInfo (p);
    if (p == PadParam::cutoff || p == PadParam::decay)
        return (float) (info.minValue * std::pow (info.maxValue / info.minValue, n));
    return (float) (info.minValue + n * (info.maxValue - info.minValue));
}

juce::String textOf (PadParam p, float v)
{
    switch (p)
    {
        case PadParam::tune: return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st";
        case PadParam::decay: return v >= 9.99f ? juce::String ("max") : juce::String (v, v < 1.0f ? 2 : 1) + " s";
        case PadParam::cutoff: return v >= 19999.0f ? juce::String ("off") : juce::String (v >= 1000.0f ? v / 1000.0f : v, v >= 1000.0f ? 1 : 0) + (v >= 1000.0f ? " kHz" : " Hz");
        case PadParam::volumeDb:
        case PadParam::drive: return juce::String (v, 1) + " dB";
        case PadParam::pan: return std::abs (v) < 0.01f ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
        case PadParam::fine:
        case PadParam::attack:
        case PadParam::resonance:
        case PadParam::startOffset:
        case PadParam::reverse:
        case PadParam::velocitySens:
        case PadParam::chokeGroup:
        case PadParam::mute:
        case PadParam::solo:
        case PadParam::oneShot:
        case PadParam::polyphony:
        case PadParam::count: break;
    }
    return juce::String (v, 2);
}
} // namespace

DrumPads::DrumPads (AudioEngine& e, DrumKitProcessor& k, juce::Colour c) : engine (e), kit (k), colour (c)
{
    for (const auto& spec : knobSpecs())
    {
        auto pk = std::make_unique<PadKnob>();
        pk->param = (int) spec.param;
        pk->knob.setCaption (*spec.caption);
        pk->knob.setRingColour (colour);
        pk->knob.setBipolar (spec.param == PadParam::tune || spec.param == PadParam::pan);
        const auto p = spec.param;
        pk->knob.valueText = [p] (double n) { return textOf (p, denormalise (p, n)); };
        pk->knob.setDefaultValue (normalise (p, daw::drums::padParamInfo (p).defaultValue));
        pk->knob.onValueChange = [this, p] (double n) {
            auto params = kit.getKit().pads[(size_t) selected].params;
            daw::drums::setParam (params, p, denormalise (p, n));
            kit.setPadParams (selected, params);
        };
        addAndMakeVisible (pk->knob);
        knobs.push_back (std::move (pk));
    }
    updateKnobs();
}

juce::Rectangle<int> DrumPads::padBounds (int pad) const
{
    const auto grid = getLocalBounds().removeFromLeft (getHeight() * 4 / 3).reduced (6);
    const int col = pad % 4, row = 3 - pad / 4; // the first pad sits bottom left
    const int w = grid.getWidth() / 4, h = grid.getHeight() / 4;
    return { grid.getX() + col * w, grid.getY() + row * h, w - 4, h - 4 };
}

int DrumPads::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < daw::drums::kNumPads; ++i)
        if (padBounds (i).contains (p))
            return i;
    return -1;
}

void DrumPads::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (getHeight() * 4 / 3 + 8).reduced (4, 6);
    area.removeFromTop (18); // the name of the selected pad
    const int cols = 3, w = juce::jmin (58, area.getWidth() / cols), h = juce::jmin (54, area.getHeight() / 2);
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->knob.setBounds (area.getX() + (int) (i % cols) * w, area.getY() + (int) (i / cols) * h, w, h);
}

void DrumPads::updateKnobs()
{
    const auto& params = kit.getKit().pads[(size_t) selected].params;
    for (auto& k : knobs)
        if (! k->knob.isDragging())
            k->knob.setValue (normalise ((PadParam) k->param, daw::drums::getParam (params, (PadParam) k->param)));
}

void DrumPads::selectPad (int pad)
{
    selected = juce::jlimit (0, daw::drums::kNumPads - 1, pad);
    updateKnobs();
    repaint();
}

void DrumPads::tick()
{
    bool dirty = false;
    for (int i = 0; i < daw::drums::kNumPads; ++i)
    {
        const float a = kit.padActivity (i);
        if (std::abs (a - shown[(size_t) i]) > 0.02f)
        {
            shown[(size_t) i] = a;
            dirty = true;
        }
    }
    if (kit.getKitVersion() != seenVersion)
    {
        seenVersion = kit.getKitVersion();
        updateKnobs();
        dirty = true;
    }
    if (dirty)
        repaint();
}

juce::String DrumPads::helpAt (juce::Point<int>) const { return Strings::drums::help; }

void DrumPads::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto& state = kit.getKit();
    for (int i = 0; i < daw::drums::kNumPads; ++i)
    {
        const auto b = padBounds (i).toFloat();
        const auto& pad = state.pads[(size_t) i];
        const bool empty = pad.source.empty();
        const float flash = shown[(size_t) i];
        auto fill = empty ? t.panel2 : t.panel2.interpolatedWith (colour, 0.35f + 0.6f * flash);
        if (i == hoverDrop)
            fill = t.accent.withAlpha (0.5f);
        g.setColour (fill);
        g.fillRoundedRectangle (b, 4.0f);
        if (flash > 0.05f && t.glow > 0.0f)
        {
            g.setColour (colour.withAlpha (0.35f * flash * t.glow));
            g.drawRoundedRectangle (b.expanded (1.5f), 5.0f, 2.0f);
        }
        g.setColour (i == selected ? t.text : t.line);
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, i == selected ? 1.5f : 1.0f);
        g.setColour (empty ? t.dimText : t.clipInk);
        g.setFont (t.uiFont (10.5f, 500));
        g.drawFittedText (empty ? juce::String (daw::drums::padMap()[(size_t) i].noteName) : juce::String (pad.name), b.toNearestInt().reduced (4),
                          juce::Justification::centred, 2, 0.8f);
    }
    auto title = getLocalBounds().withTrimmedLeft (getHeight() * 4 / 3 + 8).reduced (4, 6).removeFromTop (16);
    g.setColour (t.text);
    g.setFont (t.uiFont (12.0f, 600));
    const auto& pad = state.pads[(size_t) selected];
    g.drawText (juce::String (daw::drums::padMap()[(size_t) selected].noteName) + "  " + juce::String (pad.name), title, juce::Justification::centredLeft, true);
}

void DrumPads::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAt (e.getPosition());
    if (pad < 0)
        return;
    selectPad (pad);
    pressed = pad;
    const uint8_t on[3] { 0x90, (uint8_t) daw::drums::noteForPad (pad), 100 };
    engine.pushLiveMidi (on, 3, true, juce::Time::getMillisecondCounterHiRes());
}

void DrumPads::mouseUp (const juce::MouseEvent&)
{
    if (pressed < 0)
        return;
    const uint8_t off[3] { 0x80, (uint8_t) daw::drums::noteForPad (pressed), 0 };
    engine.pushLiveMidi (off, 3, true, juce::Time::getMillisecondCounterHiRes());
    pressed = -1;
}

void DrumPads::dropFile (int pad, const juce::File& file)
{
    if (pad >= 0 && kit.setPadFile (pad, file))
        selectPad (pad);
    hoverDrop = -1;
    repaint();
}

bool DrumPads::isInterestedInDragSource (const SourceDetails& d)
{
    browser::DragItem item;
    return browser::parseDrag (d.description, item) && item.kind == browser::kinds::audioFile;
}

void DrumPads::itemDragMove (const SourceDetails& d)
{
    hoverDrop = padAt (d.localPosition);
    repaint();
}

void DrumPads::itemDragExit (const SourceDetails&)
{
    hoverDrop = -1;
    repaint();
}

void DrumPads::itemDropped (const SourceDetails& d)
{
    browser::DragItem item;
    if (browser::parseDrag (d.description, item))
        dropFile (padAt (d.localPosition), juce::File (item.path));
}

bool DrumPads::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() >= 1 && audioimport::isSoundFile (juce::File (files[0]));
}

void DrumPads::fileDragMove (const juce::StringArray&, int x, int y)
{
    hoverDrop = padAt ({ x, y });
    repaint();
}

void DrumPads::fileDragExit (const juce::StringArray&)
{
    hoverDrop = -1;
    repaint();
}

void DrumPads::filesDropped (const juce::StringArray& files, int x, int y)
{
    // Several files fill the pads from the one they were dropped on.
    const int first = padAt ({ x, y });
    for (int i = 0; i < files.size() && first >= 0 && first + i < daw::drums::kNumPads; ++i)
        dropFile (first + i, juce::File (files[i]));
}
} // namespace ui
