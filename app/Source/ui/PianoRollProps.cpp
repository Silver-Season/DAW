#include "PianoRoll.h"

#include "PaletteMenu.h"
#include "PianoRollParts.h"
#include "ScalePicker.h"

namespace ui
{
namespace
{
constexpr int kMargin = 8, kRow = 22, kGap = 4;
}

//==============================================================================
// The name of the clip with its colour swatch: click the swatch for the palette, double-click the name to rename.
class PianoRoll::Props::NameField final : public juce::Component
{
public:
    explicit NameField (PianoRoll& o) : owner (o) { setHelpText (Strings::help::editorName); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat();
        fillCell (g, r, t.panel2, 0.8f);
        strokeCell (g, r, t.line, 1.0f, 0.8f);
        const auto swatch = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ 14.0f, r.getCentreY() });
        g.setColour (owner.clipColour());
        g.fillRoundedRectangle (swatch, 3.0f);
        if (editor != nullptr)
            return;
        g.setColour (t.text);
        g.setFont (t.uiFont (12.5f, 600));
        g.drawText (owner.clip().name(), getLocalBounds().withTrimmedLeft (28).withTrimmedRight (4), juce::Justification::centredLeft, true);
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
        auto menu = makePaletteMenu (owner.clip().colour(), [safe = juce::Component::SafePointer<PianoRoll> (&owner), id] (int index) {
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

    PianoRoll& owner;
    std::unique_ptr<InlineEditor> editor;
};

//==============================================================================
class PianoRoll::Props::Content final : public juce::Component
{
public:
    explicit Content (PianoRoll& o)
        : owner (o),
          nameField (o),
          loopChip (Strings::editor::loop, Strings::help::editorLoop, true),
          snapChip (Strings::editor::snap, Strings::help::editorSnap, true),
          gridChip ({}, Strings::help::editorGrid, false),
          ownScaleChip (Strings::editor::ownScale, Strings::help::editorOwnScale, true),
          scaleChip ({}, Strings::help::editorScale, false),
          scaleOnlyChip (Strings::editor::scaleOnly, Strings::help::editorScaleOnly, true),
          foldChip (Strings::editor::fold, Strings::help::editorFold, true),
          pencilChip (Strings::editor::pencil, Strings::help::editorPencil, true),
          previewChip (Strings::editor::preview, Strings::help::editorPreview, true),
          quantChip (Strings::editor::menuQuantise, Strings::help::editorQuantise, false),
          quantOptionsChip (juce::String::fromUTF8 ("\xe2\x80\xa6"), Strings::help::editorQuantise, false)
    {
        const char* labels[4] = { "\xe2\x88\x92" "12", "\xe2\x88\x92" "1", "+1", "+12" };
        for (int i = 0; i < 4; ++i)
            transposeChips[i] = std::make_unique<Chip> (juce::String::fromUTF8 (labels[i]), Strings::help::editorTranspose, false);

        for (auto* c : std::initializer_list<juce::Component*> { &nameField, &loopChip, &snapChip, &gridChip, &ownScaleChip, &scaleChip, &scaleOnlyChip,
                                                                  &foldChip, &pencilChip, &previewChip, &quantChip, &quantOptionsChip, &loopStartField,
                                                                  &loopEndField, &startField })
            addAndMakeVisible (c);
        for (auto& c : transposeChips)
            addAndMakeVisible (*c);

        loopChip.onClick = [this] { owner.setLoopEnabled (! owner.clip().loopEnabled()); };
        snapChip.onClick = [this] { owner.v.snap = ! owner.v.snap; refresh(); };
        gridChip.onClick = [this] { showGridMenu(); };
        pencilChip.onClick = [this] { owner.v.pencil = ! owner.v.pencil; refresh(); };
        previewChip.onClick = [this] { owner.v.preview = ! owner.v.preview; refresh(); };
        scaleOnlyChip.onClick = [this] { owner.v.scaleOnly = ! owner.v.scaleOnly; refresh(); };
        foldChip.onClick = [this] {
            owner.v.fold = ! owner.v.fold;
            owner.rebuildRows();
            owner.showNoteRange (true);
            owner.grid->repaint();
            owner.keys->repaint();
            refresh();
        };
        ownScaleChip.onClick = [this] {
            const auto c = owner.clip();
            if (! c.isValid())
                return;
            const bool turnOn = ! c.ownScale();
            owner.actions.setClipScale (c.id(), turnOn, turnOn ? owner.model.scaleRoot() : c.scaleRoot(), turnOn ? owner.model.scaleMode() : c.scaleMode());
        };
        scaleChip.onClick = [this] {
            const auto c = owner.clip();
            if (! c.isValid())
                return;
            const int root = c.ownScale() ? c.scaleRoot() : owner.model.scaleRoot();
            const auto mode = c.ownScale() ? c.scaleMode() : owner.model.scaleMode();
            showScalePicker (scaleChip, root, mode, [safe = juce::Component::SafePointer<PianoRoll> (&owner)] (int r, const juce::String& m) {
                if (safe != nullptr && safe->hasClip())
                    safe->actions.setClipScale (safe->currentClip, true, r, m);
            });
        };
        quantChip.onClick = [this] { owner.quantizeSelected(); };
        quantOptionsChip.onClick = [this] { owner.showQuantizeWindow(); };
        const int steps[4] = { -12, -1, 1, 12 };
        for (int i = 0; i < 4; ++i)
            transposeChips[i]->onClick = [this, step = steps[i]] { owner.transposeSelected (step, std::abs (step) == 1 && owner.v.scaleOnly); };

        const auto setup = [] (NumberField& f, const juce::String& help) {
            f.setHelpText (help);
            f.setRange (0.0, 4096.0, 1.0 / 64.0);
            f.setDecimals (3);
            f.setDragSensitivity (0.1);
            f.setWheelStep (0.25);
            f.setFontHeight (12.0f);
        };
        setup (loopStartField, Strings::help::editorLoopStart);
        setup (loopEndField, Strings::help::editorLoopEnd);
        setup (startField, Strings::help::editorStart);
        loopEndField.setRange (1.0 / 64.0, 4096.0, 1.0 / 64.0);
        const auto transaction = [this] { owner.model.undo.beginNewTransaction (Strings::undoLoopRange); };
        loopStartField.onGestureStart = loopEndField.onGestureStart = transaction;
        startField.onGestureStart = [this] { owner.model.undo.beginNewTransaction (Strings::undoStartPoint); };
        loopStartField.onValueChange = [this] (double value) {
            if (const auto c = owner.clip(); c.isValid())
                c.setLoopStart (juce::jmin (value, c.lengthBeats() - kMinNoteLength), &owner.model.undo);
        };
        loopEndField.onValueChange = [this] (double value) {
            if (const auto c = owner.clip(); c.isValid())
                c.setLengthBeats (juce::jmax (value, c.loopStart() + kMinNoteLength), &owner.model.undo);
        };
        startField.onValueChange = [this] (double value) {
            if (const auto c = owner.clip(); c.isValid())
                c.setStartOffset (juce::jmin (value, c.lengthBeats() - kMinNoteLength), &owner.model.undo);
        };
    }

    void refresh()
    {
        const auto c = owner.clip();
        if (! c.isValid())
            return;
        loopChip.setOn (c.loopEnabled());
        snapChip.setOn (owner.v.snap);
        gridChip.setText (Strings::editor::grid + ": " + owner.gridText());
        ownScaleChip.setOn (c.ownScale());
        const int root = c.ownScale() ? c.scaleRoot() : owner.model.scaleRoot();
        const auto mode = c.ownScale() ? c.scaleMode() : owner.model.scaleMode();
        scaleChip.setText (scaleLabel (root, mode));
        scaleOnlyChip.setOn (owner.v.scaleOnly);
        scaleOnlyChip.setEnabled (mode.isNotEmpty());
        foldChip.setOn (owner.v.fold);
        pencilChip.setOn (owner.v.pencil);
        previewChip.setOn (owner.v.preview);
        loopStartField.setValue (c.loopStart());
        loopEndField.setValue (c.lengthBeats());
        startField.setValue (c.startOffset());
        nameField.repaint();
    }

    void resized() override
    {
        const int w = getWidth() - 2 * kMargin;
        int y = kMargin;
        const auto row = [&] { return juce::Rectangle<int> (kMargin, y, w, kRow); };
        const auto next = [&] { y += kRow + kGap; };
        const auto halves = [&] (juce::Component& a, juce::Component& b) {
            a.setBounds (kMargin, y, w / 2 - 2, kRow);
            b.setBounds (kMargin + w / 2 + 2, y, w - w / 2 - 2, kRow);
            next();
        };
        nameField.setBounds (row());
        next();
        halves (loopChip, snapChip);
        labelY[0] = y;
        for (auto* f : { &loopStartField, &loopEndField, &startField })
        {
            f->setBounds (kMargin + w - 76, y, 76, kRow);
            next();
        }
        gridChip.setBounds (row());
        next();
        halves (ownScaleChip, scaleOnlyChip);
        scaleChip.setBounds (row());
        next();
        halves (foldChip, pencilChip);
        previewChip.setBounds (kMargin, y, w, kRow);
        next();
        quantChip.setBounds (kMargin, y, w - 34, kRow);
        quantOptionsChip.setBounds (kMargin + w - 30, y, 30, kRow);
        next();
        labelY[1] = y;
        y += 14;
        const int cw = (w - 3 * 4) / 4;
        for (int i = 0; i < 4; ++i)
            transposeChips[i]->setBounds (kMargin + i * (cw + 4), y, cw, kRow);
        y += kRow + kMargin;
        contentBottom = y;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        g.setColour (t.dimText);
        g.setFont (t.uiFont (11.5f));
        const juce::String* names[3] = { &Strings::editor::loopStart, &Strings::editor::loopEnd, &Strings::editor::startPoint };
        for (int i = 0; i < 3; ++i)
            g.drawText (*names[i], kMargin + 2, labelY[0] + i * (kRow + kGap), getWidth() - 2 * kMargin - 80, kRow, juce::Justification::centredLeft, true);
        g.drawText (Strings::editor::transpose, kMargin + 2, labelY[1], getWidth() - 2 * kMargin, 14, juce::Justification::centredLeft, false);
        if (owner.numSelected() > 0)
        {
            g.setColour (t.text);
            g.drawText (Strings::editor::selectedCount (owner.numSelected(), owner.clip().numNotes()), kMargin, contentBottom, getWidth() - 2 * kMargin, 16,
                        juce::Justification::centredLeft, false);
        }
    }

    int heightForWidth() const { return contentBottom + 20; }

private:
    void showGridMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, Strings::editor::gridAdaptive, true, owner.v.adaptiveGrid);
        static constexpr int denominators[] = { 4, 8, 16, 32, 64 };
        for (int i = 0; i < 5; ++i)
            menu.addItem (10 + i, Strings::editor::gridDivisionNames[i], true, ! owner.v.adaptiveGrid && owner.v.denominator == denominators[i]);
        menu.addSeparator();
        menu.addItem (20, juce::String::fromUTF8 ("\xd0\xa2\xd1\x80\xd0\xb8\xd0\xbe\xd0\xbb\xd0\xb8"), true, owner.v.triplet);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridChip), [this, safe = juce::Component::SafePointer<PianoRoll> (&owner)] (int result) {
            if (safe == nullptr || result == 0)
                return;
            auto& vw = safe->v;
            if (result == 1)
                vw.adaptiveGrid = true;
            else if (result == 20)
                vw.triplet = ! vw.triplet;
            else
            {
                vw.adaptiveGrid = false;
                vw.denominator = denominators[result - 10];
            }
            juce::ignoreUnused (this);
            safe->props->refresh();
            safe->grid->repaint();
            safe->ruler->repaint();
            safe->velocityLane->repaint();
        });
    }

    PianoRoll& owner;
    NameField nameField;
    Chip loopChip, snapChip, gridChip, ownScaleChip, scaleChip, scaleOnlyChip, foldChip, pencilChip, previewChip, quantChip, quantOptionsChip;
    std::unique_ptr<Chip> transposeChips[4];
    NumberField loopStartField, loopEndField, startField;
    int labelY[2] { 0, 0 };
    int contentBottom = 0;

    friend class PianoRoll::Props;
};

//==============================================================================
PianoRoll::Props::Props (PianoRoll& o)
{
    setHelpText (Strings::help::editorProps);
    content = std::make_unique<Content> (o);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

PianoRoll::Props::~Props() = default;

void PianoRoll::Props::refresh()
{
    content->refresh();
    content->repaint();
}

void PianoRoll::Props::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedRight (1));
    const int w = viewport.getMaximumVisibleWidth();
    content->setSize (w, 600);
    content->resized();
    content->setSize (w, juce::jmax (content->heightForWidth(), getHeight()));
}

void PianoRoll::Props::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);
    g.setColour (t.line);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}
} // namespace ui
