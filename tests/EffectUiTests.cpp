// Stage 6, task 6.1 of docs/spec/06-effects-mixing.md, the interface side: every built-in effect has its own compact face in
// the device chain, an expanded form with all its parameters, and the same face in its window. Runs headless with fixed
// bounds; picks from dropdowns go through the call the menu makes.
#include "TestSupport.h"

#include "../app/Source/Strings.h"
#include "../app/Source/engine/BuiltInEffectProcessor.h"
#include "../app/Source/model/ProjectActions.h"
#include "../app/Source/ui/DetailPanel.h"
#include "../app/Source/ui/EffectViews.h"
#include "../app/Source/ui/Selection.h"
#include "../app/Source/ui/Theme.h"

#include "daw/fx/Effect.h"

using namespace tests;

namespace
{
float paramValue (juce::AudioProcessor& p, const juce::String& id)
{
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param); r != nullptr && r->paramID == id)
            return r->convertFrom0to1 (r->getValue());
    return -1.0f;
}

bool paints (juce::Component& c)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds());
    return image.isValid() && image.getWidth() == c.getWidth();
}

void testEveryEffectHasAFace()
{
    std::printf ("[6.1] every built-in effect builds its compact and expanded view\n");
    ThemeManager::get().reload();
    constexpr int height = 165;
    for (const auto& type : daw::fx::effectTypes())
    {
        const juce::String name (type.id);
        BuiltInEffectProcessor p (name);
        ui::EffectView compact (p, theme().accent, false);
        const int compactWidth = compact.preferredWidth (height);
        compact.setBounds (0, 0, compactWidth, height);
        compact.tick();
        check (compact.numControls() > 0, name + ": the compact view has controls");
        check (compactWidth >= 176 && compactWidth <= 336, name + ": compact width within the card's 200-360 px (" + juce::String (compactWidth) + ")");
        check (paints (compact), name + ": the compact view paints");

        ui::EffectView full (p, theme().accent, true);
        const int fullWidth = full.preferredWidth (height);
        full.setBounds (0, 0, fullWidth, height);
        full.tick();
        check (fullWidth > compactWidth, name + ": the expanded form is wider");
        check (paints (full), name + ": the expanded view paints");
        const auto ids = full.controlIds();
        for (const auto& info : p.getEffect()->params())
            check (ids.contains (juce::String (info.id)), name + ": the expanded form shows " + juce::String (info.id));
        // Every shown control lies inside the view: nothing is cut off at the bottom or the right.
        for (int i = 0; i < full.getNumChildComponents(); ++i)
            if (auto* c = full.getChildComponent (i); c != nullptr && c->isVisible())
                check (full.getLocalBounds().contains (c->getBounds()), name + ": a control of the expanded form fits");
        for (int i = 0; i < compact.getNumChildComponents(); ++i)
            if (auto* c = compact.getChildComponent (i); c != nullptr && c->isVisible())
                check (compact.getLocalBounds().contains (c->getBounds()), name + ": a control of the compact form fits");

        // Captions are whole words that fit the knob cell, not cut-off names.
        for (const auto& info : p.getEffect()->params())
        {
            const auto caption = Strings::fx::caption (juce::String (info.id));
            const float w = juce::GlyphArrangement::getStringWidth (theme().uiFont (10.5f), caption);
            check (w <= (float) ui::EffectView::kKnobWidth + 10.0f, name + ": caption '" + caption + "' fits its knob");
        }

        // The window shows the same view, not a generic editor.
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* themed = dynamic_cast<ui::EffectEditor*> (editor.get());
        check (themed != nullptr && themed->view().isExpanded(), name + ": the window hosts the expanded view");
        if (themed != nullptr)
            check (themed->getWidth() >= themed->view().getWidth(), name + ": the window is as wide as its view");
    }
}

void testExpandAndDelayDivision()
{
    std::printf ("[6.1] the device card expands in place; the delay's division is a dropdown\n");
    ThemeManager::get().reload();
    Fixture f;
    FakeDevice dev (256, 48000.0);
    ProjectActions actions (f.model, &f.engine);
    ui::SelectionState selection;
    auto track = f.model.addTrack ("audio");
    auto delay = f.model.addDevice (track, "fx", Strings::fx::effectName ("delay"));
    delay.setPluginDesc ("delay", nullptr);
    f.engine.audioDeviceAboutToStart (&dev);
    f.engine.rebuildGraphIfNeeded();
    f.model.undo.clearUndoHistory();
    auto* p = f.engine.findProcessor (delay.id());
    check (p != nullptr, "the engine made the delay");
    if (p == nullptr)
        return;

    ui::DeviceChainView chain (f.engine, f.model, actions, selection);
    chain.setBounds (0, 0, 1600, 231);
    chain.setTrack (track.id());
    chain.tick();
    int index = -1;
    for (int i = 0; i < chain.numDeviceViews(); ++i)
        if (chain.effectView (i) != nullptr)
            index = i;
    check (index >= 0 && chain.effectView (index)->type() == "delay", "the card of the delay has the delay's face");
    if (index < 0)
        return;
    const int compactWidth = chain.deviceView (index)->getWidth();
    check (compactWidth >= 200 && compactWidth <= 360, "the compact card is 200-360 px wide (" + juce::String (compactWidth) + ")");
    check (! chain.effectView (index)->isExpanded(), "a new device starts compact");

    // A division is a dropdown in the card itself.
    auto* divL = dynamic_cast<ui::ChoiceBox*> (chain.effectView (index)->controlFor ("divL"));
    check (divL != nullptr && divL->isVisible(), "the left division is a dropdown in the compact card");
    if (divL != nullptr)
    {
        divL->choose (6);
        check (std::abs (paramValue (*p, "divL") - 6.0f) < 1.0e-4f, "a pick from the dropdown sets the parameter");
        check (divL->selectedIndex() == 6, "and the box shows it");
    }
    check (! f.model.undo.canUndo(), "parameter changes stay out of the undo history");

    // Sync off: the dropdowns give way to the times in ms.
    auto* sync = dynamic_cast<ui::ParamToggle*> (chain.effectView (index)->controlFor ("sync"));
    check (sync != nullptr && sync->isOn(), "the sync switch is in the card, on");
    if (sync != nullptr)
        sync->toggle();
    chain.tick();
    check (paramValue (*p, "sync") < 0.5f, "the switch turns sync off");
    check (chain.effectView (index)->controlFor ("divL") == nullptr, "without sync the division is not shown");
    auto* timeL = chain.effectView (index)->controlFor ("timeL");
    check (dynamic_cast<ui::ValueBox*> (timeL) != nullptr && timeL->isVisible() && timeL->getWidth() > 0, "the time in ms takes its place");

    // Expand: the card gets wider and shows every parameter; the setting is saved in the model, not undoable.
    delay.setExpanded (true);
    chain.tick();
    check (chain.effectView (index) != nullptr && chain.effectView (index)->isExpanded(), "the expanded flag of the device expands its face");
    const int expandedWidth = chain.deviceView (index)->getWidth();
    check (expandedWidth > compactWidth + 150, "the expanded card is wider (" + juce::String (compactWidth) + " -> " + juce::String (expandedWidth) + ")");
    check (chain.effectView (index)->controlFor ("modDepth") != nullptr, "and shows what the compact one leaves out");
    check (! f.model.undo.canUndo(), "expanding is not an undo step");
    check (delay.state.getProperty (ids::expanded), "the expanded state is stored on the device");

    delay.setExpanded (false);
    chain.tick();
    check (chain.deviceView (index)->getWidth() == compactWidth, "collapsing gives the compact width back");

    // The compressor keeps its sidechain line under its face.
    auto comp = f.model.addDevice (track, "fx", Strings::fx::effectName ("comp"));
    comp.setPluginDesc ("comp", nullptr);
    f.engine.rebuildGraphIfNeeded();
    chain.tick();
    auto* compView = chain.effectView (chain.numDeviceViews() - 1);
    check (compView != nullptr && compView->type() == "comp", "a compressor gets its face");
    if (compView != nullptr)
        check (compView->getBottom() <= chain.deviceView (chain.numDeviceViews() - 1)->getHeight() - 18, "above the sidechain line");
    f.engine.audioDeviceStopped();
}

void testEqBands()
{
    std::printf ("[6.1] the EQ shows the selected band's controls under its curve\n");
    ThemeManager::get().reload();
    BuiltInEffectProcessor p ("eq8");
    ui::EffectView view (p, theme().accent, false);
    view.setBounds (0, 0, view.preferredWidth (165), 165);
    view.selectBand (4);
    view.tick();
    check (view.controlFor ("b5freq") != nullptr && view.controlFor ("b3freq") == nullptr, "only the selected band's row is shown");
    auto* type = dynamic_cast<ui::ChoiceBox*> (view.controlFor ("b5type"));
    check (type != nullptr, "the band type is a dropdown");
    if (type != nullptr)
    {
        type->choose (7);
        check (std::abs (paramValue (p, "b5type") - 7.0f) < 1.0e-4f, "choosing a type sets it");
    }
    view.tick();
    check (view.headerNote() == Strings::fx::bands (6), "the header counts the bands that are on");
}
} // namespace

namespace tests
{
void runEffectUiTests()
{
    testEveryEffectHasAFace();
    testExpandAndDelayDivision();
    testEqBands();
}
} // namespace tests
