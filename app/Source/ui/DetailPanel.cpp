#include "DetailPanel.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "InputMenus.h"

#include "../../../plugins/prisma/Source/PrismaProcessor.h"
#include "../engine/BuiltInEffectProcessor.h"
#include "../engine/DrumKitProcessor.h"
#include "DrumPads.h"
#include "daw/dsp/Wavetable.h"
#include "daw/synth/PrismaParams.h"

namespace ui
{
namespace
{
constexpr int kDeviceHeaderHeight = 26;
constexpr int kKnobCellWidth = 54, kKnobCellHeight = 50;
constexpr int kArrowGap = 26;
constexpr int kFoldedWidth = 34;

// The eight knobs of the compact Prisma view (task 1.4): parameter id and caption.
struct MacroSpec
{
    const char* paramId;
    const juce::String* caption;
};

const MacroSpec* prismaMacros()
{
    static const MacroSpec specs[8] {
        { "oscA_pos", &Strings::macro::wtPos },     { "oscA_detune", &Strings::macro::detune }, { "flt_cutoff", &Strings::macro::cutoff },
        { "flt_res", &Strings::macro::resonance },  { "flt_drive", &Strings::macro::drive },    { "env1_a", &Strings::macro::attack },
        { "lfo1_rate", &Strings::macro::lfoRate },  { "oscA_spread", &Strings::macro::width },
    };
    return specs;
}

juce::AudioProcessorParameter* findParameter (juce::AudioProcessor& p, const juce::String& id)
{
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param); withId != nullptr && withId->paramID == id)
            return param;
    return nullptr;
}

// Indices from DEVICE.macroParams ("3,7,..."); empty = the first eight automatable parameters.
std::vector<int> macroIndices (const DeviceRef& device, juce::AudioProcessor& processor)
{
    std::vector<int> result;
    juce::StringArray tokens;
    tokens.addTokens (device.macroParams(), ",", "");
    tokens.removeEmptyStrings();
    for (const auto& t : tokens)
        result.push_back (t.getIntValue());
    if (result.empty())
        if (auto* fx = dynamic_cast<BuiltInEffectProcessor*> (&processor))
            return fx->defaultMacros();
    if (result.empty())
    {
        const auto& params = processor.getParameters();
        for (int i = 0; i < params.size() && (int) result.size() < 8; ++i)
            if (params[i]->isAutomatable() && ! params[i]->isMetaParameter())
                result.push_back (i);
    }
    return result;
}

juce::String missingDescription (const DeviceRef& device)
{
    juce::String manufacturer;
    if (auto xml = juce::XmlDocument::parse (device.pluginDesc()))
        manufacturer = xml->getStringAttribute ("manufacturer");
    auto text = Strings::detail::missingPlugin (device.name());
    return manufacturer.isEmpty() ? text : text + " (" + manufacturer + ")";
}
} // namespace

//==============================================================================
// Theme-aware miniature of the Prisma wavetable (150 x 120): a stack of frames with the current one highlighted.
class WavetableMini final : public juce::Component
{
public:
    WavetableMini (PrismaProcessor& p, juce::Colour trackColour) : processor (p), colour (trackColour)
    {
        tableParam = p.apvts.getRawParameterValue ("oscA_table");
        posParam = p.apvts.getRawParameterValue ("oscA_pos");
        onParam = p.apvts.getRawParameterValue ("oscA_on");
        setHelpText (Strings::help::wavetable);
    }

    void tick()
    {
        if (! juce::exactlyEqual (tableParam->load(), lastTable) || ! juce::exactlyEqual (posParam->load(), lastPos)
            || ! juce::exactlyEqual (onParam->load(), lastOn))
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        lastTable = tableParam->load();
        lastPos = posParam->load();
        lastOn = onParam->load();

        auto r = getLocalBounds().toFloat();
        fillCell (g, r, t.panel2, 0.7f);
        const auto* table = processor.getTable ((int) std::lround (lastTable));
        if (table == nullptr)
            return;

        const int frames = table->numFrames();
        const int active = (int) std::lround (lastPos * (float) (frames - 1));
        const float depthX = r.getWidth() * 0.18f, depthY = r.getHeight() * 0.26f;
        const auto area = r.reduced (10.0f).withTrimmedRight (depthX).withTrimmedTop (depthY);
        constexpr int points = 80;
        constexpr int displayMip = 3;

        auto pathFor = [&] (int frame, float offX, float offY, float morph) {
            juce::Path p;
            for (int i = 0; i <= points; ++i)
            {
                const double phase = (double) i / (points + 1);
                const float v = morph >= 0.0f ? table->sample (morph, phase, displayMip)
                                              : table->data (frame, displayMip)[(int) (phase * daw::dsp::Wavetable::kFrameSize)];
                const float px = area.getX() + offX + area.getWidth() * (float) i / points;
                const float py = area.getCentreY() - offY - v * area.getHeight() * 0.42f;
                if (i == 0)
                    p.startNewSubPath (px, py);
                else
                    p.lineTo (px, py);
            }
            return p;
        };

        for (int f = frames - 1; f >= 0; f -= 2)
        {
            const float k = (float) f / (float) juce::jmax (1, frames - 1);
            g.setColour (t.dimText.withAlpha (0.28f));
            g.strokePath (pathFor (f, k * depthX, k * depthY, -1.0f), juce::PathStrokeType (1.0f));
        }
        const float k = (float) active / (float) juce::jmax (1, frames - 1);
        g.setColour ((lastOn > 0.5f ? colour : t.dimText).withAlpha (lastOn > 0.5f ? 1.0f : 0.6f));
        g.strokePath (pathFor (active, k * depthX, k * depthY, lastPos), juce::PathStrokeType (1.7f));
    }

private:
    PrismaProcessor& processor;
    juce::Colour colour;
    std::atomic<float>*tableParam, *posParam, *onParam;
    float lastTable = -1.0f, lastPos = -1.0f, lastOn = -1.0f;
};

//==============================================================================
class DeviceChainView::DeviceView final : public juce::Component, public HelpProvider
{
public:
    DeviceView (DeviceChainView& o, const DeviceRef& d) : owner (o), deviceId (d.id()), kind (d.kind()), name (d.name())
    {
        const auto track = owner.model.findTrack (owner.trackId);
        trackColour = theme().palette (track.isValid() ? track.colour() : 0);
        enabled = d.enabled();
        folded = d.folded();
        processor = owner.engine.findProcessor (deviceId);
        presetName = {};
        loading = kind != "missing" && owner.engine.isDeviceLoading (deviceId);

        if (kind == "missing")
            missingText = missingDescription (d);

        if (processor != nullptr && kind != "missing")
        {
            std::vector<std::pair<juce::AudioProcessorParameter*, juce::String>> bindings;
            if (auto* fx = dynamic_cast<BuiltInEffectProcessor*> (processor); fx != nullptr && fx->getEffect() != nullptr)
                keyed = fx->getEffect()->acceptsSidechain();
            if (auto* drums = dynamic_cast<DrumKitProcessor*> (processor))
            {
                pads = std::make_unique<DrumPads> (owner.engine, *drums, trackColour);
                addAndMakeVisible (*pads);
            }
            else if (auto* prisma = dynamic_cast<PrismaProcessor*> (processor))
            {
                wavetable = std::make_unique<WavetableMini> (*prisma, trackColour);
                addAndMakeVisible (*wavetable);
                for (int i = 0; i < 8; ++i)
                    if (auto* p = findParameter (*processor, prismaMacros()[i].paramId))
                        bindings.emplace_back (p, *prismaMacros()[i].caption);
            }
            else
            {
                const auto& params = processor->getParameters();
                for (int index : macroIndices (d, *processor))
                    if (index >= 0 && index < params.size())
                        bindings.emplace_back (params[index], params[index]->getName (10));
            }
            for (auto& [param, caption] : bindings)
                addKnob (*param, caption);
        }
        updateTitle();
    }

    std::function<void (int xInContent)> onDragMoved;
    std::function<void()> onDragDropped;

    int64_t id() const { return deviceId; }
    bool isFolded() const { return folded; }
    void setHighlight (bool selected)
    {
        if (selected != isSelected)
        {
            isSelected = selected;
            repaint();
        }
    }

    int preferredWidth() const
    {
        if (folded)
            return kFoldedWidth;
        if (wavetable != nullptr)
            return 470;
        if (pads != nullptr)
            return DrumPads::kPreferredWidth;
        if (kind == "missing")
            return 230;
        return 4 * kKnobCellWidth + 24;
    }

    void tick()
    {
        if (kind != "missing")
        {
            // A hosted plugin that was still loading when this view was made: build the view again once it is there.
            if (processor == nullptr && owner.engine.findProcessor (deviceId) != nullptr)
                owner.dirty = true;
            const bool loadingNow = owner.engine.isDeviceLoading (deviceId);
            if (loadingNow != loading)
            {
                loading = loadingNow;
                repaint();
            }
            if (loading)
                repaint (getLocalBounds().withTrimmedTop (kDeviceHeaderHeight)); // the spinner turns
        }
        for (auto& k : knobs)
        {
            if (k->knob.isDragging())
                continue;
            k->knob.setValue ((double) k->param->getValue());
        }
        if (wavetable != nullptr)
            wavetable->tick();
        if (pads != nullptr)
            pads->tick();
        updateTitle();
    }

    juce::String helpAt (juce::Point<int> p) const override
    {
        if (p.y < kDeviceHeaderHeight)
        {
            if (powerRect().contains (p))
                return Strings::help::devicePower;
            if (closeRect().contains (p))
                return Strings::help::deviceClose;
            if (openRect().contains (p))
                return Strings::help::deviceOpen;
            if (foldRect().contains (p))
                return Strings::help::deviceFold;
            if (presetRect().contains (p))
                return isInstrument() ? Strings::help::devicePresetsInstrument : Strings::help::devicePresets;
            return Strings::help::deviceHeader;
        }
        if (kind == "missing")
            return Strings::help::missingDevice;
        return {};
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat();
        const bool missing = kind == "missing";
        fillCell (g, r, missing ? t.panel2.withAlpha (0.6f) : t.panel);
        strokeCell (g, r, isSelected ? t.accent : t.line, isSelected ? 1.5f : 1.0f);

        // header
        g.setColour (t.line);
        g.fillRect (1, kDeviceHeaderHeight - 1, getWidth() - 2, 1);

        const auto power = powerRect().toFloat();
        const auto dot = power.withSizeKeepingCentre (10.0f, 10.0f);
        const auto powerColour = missing ? t.dimText : (enabled ? t.play : t.line);
        if (enabled && ! missing && t.glow > 0.0f)
        {
            g.setColour (powerColour.withAlpha (0.28f));
            g.fillEllipse (dot.expanded (3.0f * t.glow));
        }
        g.setColour (powerColour);
        g.fillEllipse (dot);

        if (folded)
        {
            // Rotated name in the narrow strip.
            g.saveState();
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, 17.0f, (float) kDeviceHeaderHeight + 4.0f));
            g.setColour (t.text);
            g.setFont (t.uiFont (12.0f, 600));
            g.drawText (title, juce::Rectangle<int> (17, kDeviceHeaderHeight + 4 - 8, getHeight() - kDeviceHeaderHeight - 12, 16), juce::Justification::centredLeft, true);
            g.restoreState();
        }
        else
        {
            const auto textArea = juce::Rectangle<int> (powerRect().getRight() + 2, 0, presetRect().getX() - powerRect().getRight() - 6, kDeviceHeaderHeight);
            g.setColour (missing ? t.dimText : t.text);
            g.setFont (t.uiFont (12.0f, 600));
            g.drawText (title, textArea, juce::Justification::centredLeft, true);

            auto drawIcon = [&] (juce::Rectangle<int> box, const juce::Path& icon, bool enabledIcon, bool hot, bool stroked) {
                if (hot)
                {
                    g.setColour (t.panel2);
                    g.fillRoundedRectangle (box.toFloat(), 3.0f);
                }
                g.setColour ((hot ? t.text : t.dimText).withMultipliedAlpha (enabledIcon ? 1.0f : 0.45f));
                juce::ignoreUnused (stroked);
                g.fillPath (icon);
            };
            const auto inner = [] (juce::Rectangle<int> b, float size) { return juce::Rectangle<float> (size, size).withCentre (b.toFloat().getCentre()); };
            drawIcon (presetRect(), icons::triangle (inner (presetRect(), 6.0f), 2), true, hoverPart == Part::preset, false);
            drawIcon (foldRect(), icons::triangle (inner (foldRect(), 6.0f), 3), true, hoverPart == Part::fold, false);
            drawIcon (openRect(), icons::openExternal (inner (openRect(), 9.0f)), processor != nullptr, hoverPart == Part::open, true);
            drawIcon (closeRect(), icons::cross (inner (closeRect(), 8.0f)), true, hoverPart == Part::close, true);
        }

        if (loading && ! folded)
        {
            const auto body = getLocalBounds().withTrimmedTop (kDeviceHeaderHeight);
            const auto centre = body.getCentre().toFloat().translated (0.0f, -8.0f);
            const float phase = (float) (juce::Time::getMillisecondCounter() % 900) / 900.0f * juce::MathConstants<float>::twoPi;
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, 11.0f, 11.0f, 0.0f, phase, phase + 4.2f, true);
            g.setColour (t.line);
            g.drawEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (centre), 2.0f);
            g.setColour (t.accent);
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (t.dimText);
            g.setFont (t.uiFont (12.0f));
            g.drawText (Strings::detail::loading, body.withTrimmedTop (body.getHeight() / 2 + 10), juce::Justification::centredTop, true);
        }

        if (keyed && ! folded)
        {
            const auto line = sidechainRect();
            const auto source = owner.model.findTrack (owner.model.findDevice (deviceId).sidechainTrack());
            g.setFont (t.uiFont (11.0f));
            g.setColour (t.dimText);
            g.drawText (Strings::returns::sidechain + ": ", line, juce::Justification::centredLeft, false);
            g.setColour (source.isValid() ? t.textPalette (source.colour()) : t.text);
            g.setFont (t.uiFont (11.0f, 600));
            g.drawText ((source.isValid() ? source.name() : Strings::returns::sidechainNone) + juce::String::fromUTF8 (" \xe2\x96\xbe"),
                        line.withTrimmedLeft (74), juce::Justification::centredLeft, true);
        }

        if (missing && ! folded)
        {
            g.setColour (t.dimText);
            g.setFont (t.uiFont (12.0f));
            g.drawFittedText (missingText, getLocalBounds().withTrimmedTop (kDeviceHeaderHeight).reduced (14), juce::Justification::centred, 4);
        }
    }

    void resized() override
    {
        const bool show = ! folded;
        for (auto& k : knobs)
            k->knob.setVisible (show);
        if (wavetable != nullptr)
            wavetable->setVisible (show);
        if (pads != nullptr)
            pads->setVisible (show);
        if (folded)
            return;
        if (pads != nullptr)
        {
            pads->setBounds (getLocalBounds().withTrimmedTop (kDeviceHeaderHeight).reduced (8, 6));
            return;
        }

        auto body = getLocalBounds().withTrimmedTop (kDeviceHeaderHeight).reduced (12, 10);
        const int rows = 2, cols = 4;
        const int gridW = cols * kKnobCellWidth, gridH = rows * kKnobCellHeight + 8;
        if (wavetable != nullptr)
        {
            wavetable->setBounds (juce::Rectangle<int> (150, 120).withCentre ({ body.getX() + 75, body.getCentreY() }));
            body.removeFromLeft (150 + 12);
        }
        auto grid = juce::Rectangle<int> (gridW, gridH).withCentre (body.getCentre());
        if (wavetable == nullptr)
            grid.setX (body.getX() + (body.getWidth() - gridW) / 2);
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int col = (int) i % cols, row = (int) i / cols;
            knobs[i]->knob.setBounds (grid.getX() + col * kKnobCellWidth, grid.getY() + row * (kKnobCellHeight + 8), kKnobCellWidth, kKnobCellHeight);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (partAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override { setHover (Part::none); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.selection.selectDevice (owner.trackId, deviceId);
        if (keyed && ! folded && sidechainRect().contains (e.getPosition()))
        {
            showSidechainMenu();
            pressedPart = Part::none;
            return;
        }
        pressedPart = partAt (e.getPosition());
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressedPart != Part::header || e.getDistanceFromDragStart() < 6)
            return;
        dragging = true;
        if (onDragMoved)
            onDragMoved (e.getEventRelativeTo (getParentComponent()).x);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging)
        {
            if (onDragDropped)
                onDragDropped();
            dragging = false;
            return;
        }
        if (! getLocalBounds().contains (e.getPosition()) || partAt (e.getPosition()) != pressedPart)
            return;
        switch (pressedPart)
        {
            case Part::power: owner.actions.setDeviceEnabled (deviceId, ! enabled); break;
            case Part::fold: owner.actions.setDeviceFolded (deviceId, ! folded); break;
            case Part::open:
                if (processor != nullptr && owner.onOpenDevice)
                    owner.onOpenDevice (deviceId);
                break;
            case Part::close: owner.actions.removeDevice (deviceId); break;
            case Part::preset:
                if (owner.onPresetMenu)
                    owner.onPresetMenu (deviceId, e.getScreenPosition());
                break;
            case Part::header:
            case Part::none: break;
        }
    }

private:
    enum class Part
    {
        none,
        header,
        power,
        preset,
        fold,
        open,
        close
    };

    struct BoundKnob
    {
        BoundKnob (juce::AudioProcessorParameter& p) : param (&p) {}
        juce::AudioProcessorParameter* param;
        Knob knob;
    };

    // The first device of a track is its instrument.
    bool isInstrument() const { return owner.model.findDevice (deviceId).state.getParent().indexOf (owner.model.findDevice (deviceId).state) == 0; }

    juce::Rectangle<int> powerRect() const { return { 4, 3, 20, 20 }; }
    juce::Rectangle<int> sidechainRect() const { return { 12, getHeight() - 20, getWidth() - 24, 16 }; }

    void showSidechainMenu()
    {
        juce::PopupMenu menu;
        const auto device = owner.model.findDevice (deviceId);
        std::vector<int64_t> ids { -1 };
        menu.addItem (1, Strings::returns::sidechainNone, true, device.sidechainTrack() < 0);
        for (const auto& t : owner.model.tracks())
            if (t.id() != owner.trackId)
            {
                ids.push_back (t.id());
                menu.addItem ((int) ids.size(), t.name(), true, device.sidechainTrack() == t.id());
            }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [&model = owner.model, id = deviceId, ids] (int r) {
            const auto d = model.findDevice (id);
            if (r < 1 || ! d.isValid() || d.sidechainTrack() == ids[(size_t) r - 1])
                return;
            model.undo.beginNewTransaction (Strings::returns::undoSidechain);
            d.setSidechainTrack (ids[(size_t) r - 1], &model.undo);
        });
    }
    juce::Rectangle<int> closeRect() const { return { getWidth() - 22, 4, 18, 18 }; }
    juce::Rectangle<int> openRect() const { return { getWidth() - 42, 4, 18, 18 }; }
    juce::Rectangle<int> foldRect() const { return { getWidth() - 62, 4, 18, 18 }; }
    juce::Rectangle<int> presetRect() const { return { getWidth() - 82, 4, 18, 18 }; }

    Part partAt (juce::Point<int> p) const
    {
        if (p.y >= kDeviceHeaderHeight)
            return Part::none;
        if (powerRect().contains (p))
            return Part::power;
        if (folded)
            return Part::header;
        if (closeRect().contains (p))
            return Part::close;
        if (openRect().contains (p))
            return Part::open;
        if (foldRect().contains (p))
            return Part::fold;
        if (presetRect().contains (p))
            return Part::preset;
        return Part::header;
    }

    void setHover (Part part)
    {
        if (part != hoverPart)
        {
            hoverPart = part;
            repaint (0, 0, getWidth(), kDeviceHeaderHeight);
        }
    }

    void updateTitle()
    {
        juce::String text = name;
        if (kind == "prisma" && processor != nullptr)
            text += juce::String::fromUTF8 (" \xc2\xb7 ") + processor->getProgramName (processor->getCurrentProgram());
        if (text != title)
        {
            title = text;
            repaint (0, 0, getWidth(), kDeviceHeaderHeight);
        }
    }

    void addKnob (juce::AudioProcessorParameter& param, const juce::String& caption)
    {
        auto bound = std::make_unique<BoundKnob> (param);
        auto& knob = bound->knob;
        auto* p = &param;
        knob.setCaption (caption);
        knob.setRingColour (trackColour);
        knob.setValue ((double) p->getValue());
        knob.setDefaultValue ((double) p->getDefaultValue());
        knob.setHelpText (Strings::help::deviceKnob);
        knob.valueText = [p] (double v) { return (p->getText ((float) v, 8) + " " + p->getLabel()).trim(); };
        // Plugin parameters are not part of the undo history in this stage (see 00-rules, section 3).
        knob.onGestureStart = [p] { p->beginChangeGesture(); };
        knob.onValueChange = [p] (double v) { p->setValueNotifyingHost ((float) v); };
        knob.onGestureEnd = [p] { p->endChangeGesture(); };
        addAndMakeVisible (knob);
        knobs.push_back (std::move (bound));
    }

    DeviceChainView& owner;
    int64_t deviceId;
    juce::String kind, name, title, missingText;
    juce::String presetName;
    bool loading = false;
    juce::Colour trackColour;
    bool enabled = true, folded = false, isSelected = false, dragging = false;
    juce::AudioProcessor* processor = nullptr;
    std::unique_ptr<WavetableMini> wavetable;
    std::unique_ptr<DrumPads> pads;
    bool keyed = false; // a compressor or gate: the line "Сайдчейн: ..." at the bottom chooses its key track
    std::vector<std::unique_ptr<BoundKnob>> knobs;
    Part hoverPart = Part::none, pressedPart = Part::none;
};

//==============================================================================
class DeviceChainView::Content final : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget
{
public:
    explicit Content (DeviceChainView& o) : owner (o)
    {
        setHelpText (Strings::help::dropZone);
    }

    void rebuild (const TrackRef& track)
    {
        views.clear();
        removeAllChildren();
        if (track.isValid())
            for (const auto& d : track.devices())
            {
                views.push_back (std::make_unique<DeviceView> (owner, d));
                auto* view = views.back().get();
                view->onDragMoved = [this, view] (int x) { dragMoved (*view, x); };
                view->onDragDropped = [this, view] { dragDropped (*view); };
                addAndMakeVisible (*view);
            }
        layoutViews();
    }

    void layoutViews()
    {
        constexpr int pad = 12;
        int x = pad;
        const int h = juce::jmax (40, getHeight() > 0 ? getHeight() : owner.viewport.getMaximumVisibleHeight());
        for (auto& v : views)
        {
            const int w = v->preferredWidth();
            v->setBounds (x, pad, w, h - 2 * pad);
            x += w + kArrowGap;
        }
        dropZone = { x, pad, 180, h - 2 * pad };
        setSize (juce::jmax (x + 180 + pad, owner.viewport.getMaximumVisibleWidth()), h);
    }

    void tick()
    {
        for (auto& v : views)
            v->tick();
    }

    void applySelection (const Selection& s)
    {
        for (auto& v : views)
            v->setHighlight (s.kind == Selection::Kind::device && s.deviceId == v->id());
    }

    int numViews() const { return (int) views.size(); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.setColour (t.dimText);
        for (size_t i = 0; i < views.size(); ++i)
        {
            const auto b = views[i]->getBounds();
            const auto centre = juce::Point<float> ((float) b.getRight() + kArrowGap * 0.5f, (float) b.getCentreY());
            juce::Path arrow;
            arrow.startNewSubPath (centre.x - 6.0f, centre.y);
            arrow.lineTo (centre.x + 5.0f, centre.y);
            arrow.startNewSubPath (centre.x + 1.5f, centre.y - 3.5f);
            arrow.lineTo (centre.x + 5.0f, centre.y);
            arrow.lineTo (centre.x + 1.5f, centre.y + 3.5f);
            g.strokePath (arrow, juce::PathStrokeType (1.3f));
        }

        // drop zone
        juce::Path outline;
        outline.addRoundedRectangle (dropZone.toFloat().reduced (0.75f), t.radius);
        juce::Path dashed;
        const float dashes[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (t.line);
        g.fillPath (dashed);
        g.setColour (t.dimText);
        g.setFont (t.uiFont (12.0f));
        g.drawFittedText (Strings::detail::dropHere, dropZone.reduced (14), juce::Justification::centred, 4);

        if (dropMarker >= 0)
        {
            g.setColour (t.accent);
            g.fillRect (dropMarker - 1, 8, 3, getHeight() - 16);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const auto& t = theme();
        if (highlightDevice >= 0)
            for (const auto& v : views)
                if (v->id() == highlightDevice)
                {
                    g.setColour (t.accent.withAlpha (0.18f));
                    g.fillRoundedRectangle (v->getBounds().toFloat(), t.radius);
                    g.setColour (t.accent);
                    g.drawRoundedRectangle (v->getBounds().toFloat().reduced (0.75f), t.radius, 2.0f);
                }
    }

    void dragMoved (DeviceView& view, int xInContent)
    {
        dropIndex = slotForX (xInContent);
        dropMarker = markerForSlot (dropIndex, view);
        repaint();
    }

    void dragDropped (DeviceView& view)
    {
        const int from = indexOf (view);
        int to = dropIndex;
        dropMarker = -1;
        repaint();
        if (from < 0 || to < 0)
            return;
        if (to > from)
            --to; // removing the dragged device shifts the slots after it
        // The first device of a track is its instrument: effects never move in front of it.
        to = juce::jmax (to, 1);
        if (from > 0 && to != from)
            owner.actions.moveDevice (view.id(), to);
    }

    //==========================================================================
    // Drops from the browser
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        browser::DragItem item;
        if (owner.dropActions == nullptr || ! browser::parseDrag (d.description, item))
            return false;
        return owner.dropActions->accepts (item, browser::DropTarget::chainGap, owner.trackId)
               || owner.dropActions->accepts (item, browser::DropTarget::device, owner.trackId, deviceIdAt (d.localPosition.x));
    }
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails& d) override
    {
        browser::DragItem item;
        browser::parseDrag (d.description, item);
        const auto spot = spotAt (item, d.localPosition.x);
        if (spot.marker != dropMarker || spot.deviceId != highlightDevice)
        {
            dropMarker = spot.marker;
            highlightDevice = spot.deviceId;
            repaint();
        }
    }
    void itemDragExit (const SourceDetails&) override { clearDropMarks(); }
    void itemDropped (const SourceDetails& d) override
    {
        browser::DragItem item;
        const auto spot = browser::parseDrag (d.description, item) ? spotAt (item, d.localPosition.x) : Spot();
        clearDropMarks();
        if (owner.dropActions == nullptr || item.kind.isEmpty())
            return;
        const auto result = spot.deviceId >= 0 ? owner.dropActions->drop (item, browser::DropTarget::device, owner.trackId, -1, -1, spot.deviceId)
                                               : owner.dropActions->drop (item, browser::DropTarget::chainGap, owner.trackId, -1, spot.index);
        if (owner.onDropped)
            owner.onDropped (result, item);
    }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return false; } // MIDI files go to the Session View
    void filesDropped (const juce::StringArray&, int, int) override {}

private:
    struct Spot
    {
        int64_t deviceId = -1;
        int index = -1;
        int marker = -1;
    };

    int64_t deviceIdAt (int x) const
    {
        for (const auto& v : views)
            if (x >= v->getX() && x < v->getRight())
                return v->id();
        return -1;
    }

    // A preset over a device of the same type is applied to it; everything else goes into the gap nearest to x.
    Spot spotAt (const browser::DragItem& item, int x) const
    {
        Spot s;
        if (const auto id = deviceIdAt (x); id >= 0 && owner.dropActions != nullptr
                                                 && owner.dropActions->accepts (item, browser::DropTarget::device, owner.trackId, id))
        {
            s.deviceId = id;
            return s;
        }
        s.index = slotForX (x);
        if (! views.empty())
            s.marker = s.index <= 0 ? views.front()->getX() - kArrowGap / 2 : views[(size_t) s.index - 1]->getRight() + kArrowGap / 2;
        else
            s.marker = 6;
        return s;
    }

    void clearDropMarks()
    {
        dropMarker = -1;
        highlightDevice = -1;
        repaint();
    }

    int indexOf (const DeviceView& v) const
    {
        for (size_t i = 0; i < views.size(); ++i)
            if (views[i].get() == &v)
                return (int) i;
        return -1;
    }

    // The slot (0 = before the first device) nearest to x.
    int slotForX (int x) const
    {
        int slot = 0;
        for (size_t i = 0; i < views.size(); ++i)
            if (x > views[i]->getBounds().getCentreX())
                slot = (int) i + 1;
        return slot;
    }

    int markerForSlot (int slot, const DeviceView&) const
    {
        if (views.empty())
            return -1;
        if (slot <= 0)
            return views.front()->getX() - kArrowGap / 2;
        return views[(size_t) slot - 1]->getRight() + kArrowGap / 2;
    }

    DeviceChainView& owner;
    std::vector<std::unique_ptr<DeviceView>> views;
    juce::Rectangle<int> dropZone;
    int dropIndex = -1, dropMarker = -1;
    int64_t highlightDevice = -1;
};

//==============================================================================
DeviceChainView::DeviceChainView (AudioEngine& e, ProjectModel& m, ProjectActions& a, SelectionState& s)
    : engine (e), model (m), actions (a), selection (s)
{
    content = std::make_unique<Content> (*this);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (9);
    addAndMakeVisible (viewport);
    model.root.addListener (this);
}

DeviceChainView::~DeviceChainView()
{
    model.root.removeListener (this);
    viewport.setViewedComponent (nullptr, false);
}

void DeviceChainView::setTrack (int64_t id)
{
    trackId = id;
    dirty = true;
}

int DeviceChainView::numDeviceViews() const
{
    return content->numViews();
}

void DeviceChainView::resized()
{
    viewport.setBounds (getLocalBounds());
    content->layoutViews();
}

void DeviceChainView::rebuild()
{
    dirty = false;
    const auto viewPosition = viewport.getViewPosition();
    content->rebuild (model.findTrack (trackId));
    viewport.setViewPosition (viewPosition);
    content->applySelection (selection.get());
}

void DeviceChainView::tick()
{
    if (dirty)
        rebuild();
    content->applySelection (selection.get());
    content->tick();
}

//==============================================================================
// "MIDI | Звук": what the selected track records. The audio half waits for stage 5 and is drawn disabled.
class RecordSourceSwitch final : public juce::Component, public HelpProvider
{
public:
    RecordSourceSwitch() { setHelpText (Strings::help::trackRecordSource); }

    std::function<void (const juce::String& source)> onChoose;

    void setSource (const juce::String& s)
    {
        if (s != source)
        {
            source = s;
            repaint();
        }
    }

    juce::String helpAt (juce::Point<int> p) const override
    {
        return p.x >= getWidth() / 2 ? Strings::help::trackRecordAudio : Strings::help::trackRecordSource;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat();
        fillCell (g, r, t.panel);
        auto left = r.removeFromLeft (r.getWidth() * 0.5f);
        const auto cell = [&] (juce::Rectangle<float> area, const juce::String& text, bool active, bool enabled) {
            if (active)
            {
                fillCell (g, area.reduced (1.5f), t.panel2);
                g.setColour (t.accent);
                g.fillRect (area.reduced (4.0f, 0.0f).removeFromBottom (2.0f));
            }
            g.setColour (active ? t.text : (enabled ? t.dimText : t.dimText.withAlpha (0.5f)));
            g.setFont (t.uiFont (11.5f, 500));
            g.drawText (text, area.toNearestInt(), juce::Justification::centred, false);
        };
        cell (left, Strings::recording::sourceMidi, source != "audio", true);
        cell (r, Strings::recording::sourceAudio, source == "audio", false);
        strokeCell (g, getLocalBounds().toFloat(), t.line);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < getWidth() / 2 && onChoose)
            onChoose ("midi"); // audio is not selectable before stage 5
    }

private:
    juce::String source = "midi";
};

//==============================================================================
DetailPanel::DetailPanel (AudioEngine& e, ProjectModel& m, ProjectActions& a, SelectionState& s) : engine (e), model (m), actions (a), selection (s)
{
    recordSwitch = std::make_unique<RecordSourceSwitch>();
    recordSwitch->onChoose = [this] (const juce::String& source) {
        if (model.findTrack (shownTrack).isValid())
            actions.setTrackRecordSource (shownTrack, source);
    };
    addAndMakeVisible (*recordSwitch);

    chainView = std::make_unique<DeviceChainView> (e, m, a, s);
    addAndMakeVisible (*chainView);
    editor = std::make_unique<PianoRoll> (e, m, a, s);
    addChildComponent (*editor);
    audioClipEditor = std::make_unique<AudioClipEditor> (e, m, a);
    addChildComponent (*audioClipEditor);
    setHelpText (Strings::help::detailInfo);

    transposeField.setRange (-48.0, 48.0, 1.0);
    transposeField.setDecimals (0);
    transposeField.setDragSensitivity (0.15);
    transposeField.setHelpText (Strings::help::trackTranspose);
    transposeField.onGestureStart = [this] { model.undo.beginNewTransaction (Strings::undoTrackTranspose); };
    transposeField.onValueChange = [this] (double v) {
        if (const auto track = model.findTrack (shownTrack); track.isValid())
            track.setTranspose ((int) std::lround (v), &model.undo);
    };
    addAndMakeVisible (transposeField);
}

void DetailPanel::setEditorMode (bool on)
{
    editorOn = on;
    updateEditor();
}

void DetailPanel::updateEditor()
{
    int64_t clipId = 0;
    if (editorOn && selection.get().kind == Selection::Kind::clip)
        if (const auto track = model.findTrack (selection.get().trackId); track.isValid())
            if (const auto clip = track.clipAt (selection.get().scene); clip.isValid())
                clipId = clip.id();
    // A clip of the Arrangement: the editor shows its own copy of the content.
    if (editorOn && selection.get().kind == Selection::Kind::aclip)
        for (const auto& a : model.findTrack (selection.get().trackId).arrangementClips())
            if (a.id() == selection.get().aclipId && a.clip().isValid())
                clipId = a.clip().id();
    // With the editor on but no clip selected the chain stays: the editor has nothing to show.
    const bool shown = clipId != 0;
    const bool audio = shown && model.findClip (clipId).isAudio();
    if (shown != editorShown || audio != audioShown)
    {
        editorShown = shown;
        audioShown = audio;
        editor->setVisible (shown && ! audio);
        audioClipEditor->setVisible (shown && audio);
        chainView->setVisible (! shown);
        transposeField.setVisible (! shown);
        recordSwitch->setVisible (! shown);
        resized();
        repaint();
        if (onEditorShownChanged)
            onEditorShownChanged();
    }
    if (shown && audio)
        audioClipEditor->setClip (clipId);
    else if (shown && editor->clipId() != clipId)
        editor->setClip (clipId);
}

DetailPanel::~DetailPanel() = default;

void DetailPanel::setOpenDeviceHandler (std::function<void (int64_t)> handler)
{
    chainView->onOpenDevice = std::move (handler);
}

void DetailPanel::tick()
{
    updateEditor();
    const auto selected = model.selectedTrack();
    if (selected != shownTrack || ! model.findTrack (shownTrack).isValid())
    {
        shownTrack = model.findTrack (selected).isValid() ? selected : -1;
        chainView->setTrack (shownTrack);
    }
    chainView->tick();

    const auto track = model.findTrack (shownTrack);
    if (! transposeField.isEditing())
        transposeField.setValue (track.isValid() ? track.transpose() : 0);
    // without a track the summary card is empty, so its two controls go too
    const bool cardShown = track.isValid() && ! editorShown;
    transposeField.setVisible (cardShown);
    recordSwitch->setVisible (cardShown);
    recordSwitch->setSource (track.isValid() ? track.recordSource() : juce::String ("midi"));
    const auto summary = track.isValid() ? track.name() + "|" + juce::String (track.colour()) + "|" + track.input() + "|" + track.monitor()
                                               + "|" + juce::String (track.numDevices()) + "|" + juce::String ((int) track.isAudio())
                                         : juce::String();
    if (summary != shownSummary)
    {
        shownSummary = summary;
        repaint (getLocalBounds().removeFromLeft (kInfoWidth));
    }

    // The input meter of an audio track: peak hold that falls 1.5 dB per tick, red after a clip until clicked.
    if (cardShown && track.isAudio())
        if (auto* rt = engine.findRuntime (track.id()))
        {
            const float peak = rt->inputMeter.exchange (0.0f);
            const float level = juce::jmax (peak, inputLevel * 0.84f);
            const bool clip = rt->inputClipped.load();
            if (std::abs (level - inputLevel) > 1.0e-4f || clip != inputClipShown)
            {
                inputLevel = level;
                inputClipShown = clip;
                repaint (meterBounds().expanded (2));
            }
        }
}

juce::Rectangle<int> DetailPanel::cardRow (int index) const
{
    // the summary card: 13 px margin, the "Дорожка" caption (16), the name (22), a gap (6), then rows of 22
    return { 14, 13 + 16 + 22 + 6 + index * 22, kInfoWidth - 28, 22 };
}

juce::Rectangle<int> DetailPanel::meterBounds() const
{
    return cardRow (0).removeFromBottom (3).translated (0, 1);
}

void DetailPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto track = model.findTrack (shownTrack);
    const bool overMenu = ! editorShown && track.isValid() && (cardRow (0).contains (e.getPosition()) || cardRow (1).contains (e.getPosition()));
    setMouseCursor (overMenu ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void DetailPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto track = model.findTrack (shownTrack);
    if (editorShown || ! track.isValid())
        return;
    if (track.isAudio() && meterBounds().expanded (0, 3).contains (e.getPosition()))
    {
        if (auto* rt = engine.findRuntime (track.id()))
            rt->inputClipped = false;
        return;
    }
    if (cardRow (0).contains (e.getPosition()))
        inputmenus::showInputMenu (engine, model, track, *this);
    else if (cardRow (1).contains (e.getPosition()))
        inputmenus::showMonitorMenu (model, track, *this);
}

namespace
{
// The small arrow that says "this opens a menu"
juce::String utf8Arrow() { return juce::String::fromUTF8 (" \xe2\x96\xbe"); }
} // namespace

void DetailPanel::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (0, 0, getWidth(), 1);
    if (editorShown)
        return;
    g.fillRect (kInfoWidth - 1, 1, 1, getHeight() - 1);

    auto area = juce::Rectangle<int> (0, 1, kInfoWidth - 1, getHeight() - 1).reduced (14, 12);
    const auto track = model.findTrack (shownTrack);
    if (! track.isValid())
    {
        g.setColour (t.dimText);
        g.setFont (t.uiFont (12.5f));
        g.drawFittedText (Strings::detail::noTrack, area, juce::Justification::topLeft, 4);
        return;
    }

    g.setColour (t.dimText);
    g.setFont (t.uiFont (10.5f, 500));
    g.drawText (Strings::detail::track.toUpperCase(), area.removeFromTop (16), juce::Justification::centredLeft, false);
    g.setColour (t.textPalette (track.colour()));
    g.setFont (t.uiFont (14.0f, 600));
    g.drawText (track.name(), area.removeFromTop (22), juce::Justification::centredLeft, true);
    area.removeFromTop (6);

    auto line = [&] (const juce::String& label, const juce::String& value) {
        auto row = area.removeFromTop (22);
        g.setFont (t.uiFont (12.5f));
        g.setColour (t.dimText);
        const auto labelText = label + ": ";
        const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), labelText));
        g.drawText (labelText, row.removeFromLeft (w), juce::Justification::centredLeft, false);
        g.setColour (t.text);
        g.setFont (t.uiFont (12.5f, 600));
        g.drawText (value, row, juce::Justification::centredLeft, true);
    };
    line (Strings::detail::input, inputmenus::inputLabel (engine, track) + utf8Arrow());
    if (track.isAudio())
    {
        // under the input row: the level of the input, there before anything records
        const auto m = meterBounds().toFloat();
        g.setColour (t.panel2);
        g.fillRoundedRectangle (m, 1.5f);
        const float db = juce::Decibels::gainToDecibels (inputLevel, -60.0f);
        const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (inputClipShown ? t.record : (db > -6.0f ? t.warning : t.accent));
        g.fillRoundedRectangle (m.withWidth (m.getWidth() * fill), 1.5f);
    }
    line (Strings::detail::monitor, inputmenus::monitorLabel (track) + utf8Arrow());
    line (Strings::detail::chain, Strings::detail::devices (track.numDevices()));
    g.setColour (t.dimText);
    g.setFont (t.uiFont (12.5f));
    g.drawText (Strings::detail::transpose + ": ", area.removeFromTop (22).withWidth (110), juce::Justification::centredLeft, false);
    g.drawText (Strings::recording::recordSourceLabel + ": ", area.removeFromTop (22).withWidth (110), juce::Justification::centredLeft, false);
}

void DetailPanel::resized()
{
    chainView->setBounds (getLocalBounds().withTrimmedLeft (kInfoWidth).withTrimmedTop (1));
    editor->setBounds (getLocalBounds().withTrimmedTop (1));
    audioClipEditor->setBounds (getLocalBounds().withTrimmedTop (1));
    // below the three lines of the summary card: 14 + 16 + 22 + 6 + 3 * 22 + 13
    transposeField.setBounds (14 + 112, 13 + 16 + 22 + 6 + 3 * 22, 76, 20);
    recordSwitch->setBounds (14 + 112, 13 + 16 + 22 + 6 + 4 * 22, 76, 20);
}
} // namespace ui
