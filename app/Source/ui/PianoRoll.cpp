#include "PianoRoll.h"

#include "PianoRollParts.h"
#include "QuantiseWindow.h"

#include "../engine/AudioEngine.h"

#include <set>

namespace ui
{
namespace
{
constexpr int kAuditionMs = 220;
}

std::vector<daw::engine::Note>& PianoRoll::clipboard()
{
    static std::vector<daw::engine::Note> notes;
    return notes;
}

//==============================================================================
PianoRoll::PianoRoll (AudioEngine& e, ProjectModel& m, ProjectActions& a, SelectionState& s)
    : engine (e), model (m), actions (a), selectionState (s)
{
    setHelpText (Strings::help::editorProps);
    pitchToRow.fill (-1);
    props = std::make_unique<Props> (*this);
    ruler = std::make_unique<Ruler> (*this);
    keys = std::make_unique<Keys> (*this);
    grid = std::make_unique<NoteGrid> (*this);
    velocityLane = std::make_unique<VelocityLane> (*this);
    controllers = std::make_unique<ControllerStrip> (*this);
    for (auto* c : std::initializer_list<juce::Component*> { props.get(), ruler.get(), keys.get(), grid.get(), velocityLane.get(), controllers.get(), &hbar, &vbar })
        addAndMakeVisible (c);
    hbar.addListener (this);
    vbar.addListener (this);
    hbar.setAutoHide (false);
    vbar.setAutoHide (false);
    hbar.setHelpText (Strings::help::hscroll);
    vbar.setHelpText (Strings::help::vscroll);
    setWantsKeyboardFocus (true);
    model.root.addListener (this);
    startTimerHz (30);
    rebuildRows();
}

PianoRoll::~PianoRoll()
{
    stopTimer();
    model.root.removeListener (this);
    for (const auto& s : sounding)
        engine.getKeyboardState().noteOff (1, s.pitch, 0.0f);
}

//==============================================================================
ClipRef PianoRoll::clip() const
{
    return currentClip > 0 ? model.findClip (currentClip) : ClipRef();
}

bool PianoRoll::hasClip() const
{
    return clip().isValid();
}

clipedit::Scale PianoRoll::scale() const
{
    return clipedit::effectiveScale (clip(), model.scaleRoot(), model.scaleMode());
}

int64_t PianoRoll::trackOfClip() const
{
    const auto c = clip();
    if (! c.isValid())
        return -1;
    const auto slot = c.state.getParent(), slots = slot.getParent(), trackNode = slots.getParent();
    return (int64_t) (juce::int64) trackNode.getProperty (ids::id, -1);
}

int PianoRoll::sceneOfClip() const
{
    const auto c = clip();
    return c.isValid() ? c.state.getParent().getParent().indexOf (c.state.getParent()) : -1;
}

juce::Colour PianoRoll::clipColour() const
{
    const auto c = clip();
    return theme().palette (c.isValid() ? c.colour() : 0);
}

void PianoRoll::setClip (int64_t id)
{
    if (id == currentClip)
        return;
    for (const auto& s : sounding)
        engine.getKeyboardState().noteOff (1, s.pitch, 0.0f);
    sounding.clear();
    currentClip = id;
    selected.clear();
    viewInitialised = false;
    dirty = rowsDirty = true;
    const bool shown = hasClip();
    for (auto* c : std::initializer_list<juce::Component*> { props.get(), ruler.get(), keys.get(), grid.get(), velocityLane.get(), controllers.get(), &hbar, &vbar })
        c->setVisible (shown);
    rebuildRows();
    if (shown)
    {
        if (grid->getWidth() > 0)
        {
            zoomToClip();
            showNoteRange (true);
            viewInitialised = true;
        }
        props->refresh();
    }
    repaint();
}

//==============================================================================
void PianoRoll::tick()
{
    if (dirty)
    {
        dirty = false;
        if (currentClip > 0 && ! hasClip())
            setClip (0);
        pruneSelection();
        if (rowsDirty || shownNotes != (hasClip() ? clip().numNotes() : -1))
        {
            rowsDirty = false;
            shownNotes = hasClip() ? clip().numNotes() : -1;
            rebuildRows();
        }
        clampView();
        updateScrollBars();
        props->refresh();
        repaint();
        for (auto* c : std::initializer_list<juce::Component*> { ruler.get(), keys.get(), grid.get(), velocityLane.get() })
            c->repaint();
    }

    // the auditions that have run out
    const auto now = juce::Time::getMillisecondCounter();
    for (auto it = sounding.begin(); it != sounding.end();)
    {
        if (it->offAt != 0 && (juce::int32) (now - it->offAt) >= 0)
        {
            engine.getKeyboardState().noteOff (1, it->pitch, 0.0f);
            it = sounding.erase (it);
        }
        else
            ++it;
    }

    // the cursor of the playing clip
    double beat = -1.0;
    if (hasClip() && engine.isPlaying())
    {
        const int scene = sceneOfClip();
        if (auto* rt = engine.findRuntime (trackOfClip()); rt != nullptr && scene >= 0 && rt->playingSlot.load() == scene)
        {
            const auto c = clip();
            daw::engine::MidiClip shape;
            shape.lengthBeats = c.lengthBeats();
            shape.loopStart = c.loopStart();
            shape.startOffset = c.startOffset();
            shape.loop = c.loopEnabled();
            beat = shape.positionAfter (juce::jmax (0.0, engine.getPositionBeats() - rt->playingStartBeat.load()));
        }
    }
    grid->setCursorBeat (beat);

    if (loopFlashUntil != 0 && (juce::int32) (juce::Time::getMillisecondCounter() - loopFlashUntil) >= 0)
    {
        loopFlashUntil = 0;
        ruler->repaint();
    }
}

void PianoRoll::timerCallback()
{
    if (isShowing())
        tick();
}

//==============================================================================
double PianoRoll::gridStep() const
{
    const double tri = v.triplet ? 2.0 / 3.0 : 1.0;
    if (! v.adaptiveGrid)
        return 4.0 / (double) v.denominator * tri;
    static constexpr double steps[] { 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5, 1.0, 2.0 };
    for (const double s : steps)
        if (s * tri * v.pxPerBeat >= 12.0)
            return s * tri;
    return juce::jmax (1.0, beatsPerBar()) * tri;
}

double PianoRoll::snapBeat (double beat, bool bypass) const
{
    if (! v.snap || bypass)
        return beat;
    const double step = gridStep();
    return std::floor (beat / step + 0.5) * step;
}

double PianoRoll::floorBeat (double beat) const
{
    if (! v.snap)
        return beat;
    const double step = gridStep();
    return std::floor (beat / step + 1.0e-6) * step;
}

juce::String PianoRoll::gridText() const
{
    return v.adaptiveGrid ? Strings::editor::gridAdaptive : Strings::editor::gridName (v.denominator, v.triplet);
}

void PianoRoll::stepGrid (int direction)
{
    if (v.adaptiveGrid)
    {
        // start from the step that is in use now
        const double step = gridStep() / (v.triplet ? 2.0 / 3.0 : 1.0);
        v.denominator = juce::jlimit (4, 64, (int) std::lround (4.0 / step));
        v.adaptiveGrid = false;
    }
    v.denominator = juce::jlimit (4, 64, direction > 0 ? v.denominator * 2 : v.denominator / 2);
    props->refresh();
    repaint();
    grid->repaint();
    ruler->repaint();
    velocityLane->repaint();
}

//==============================================================================
int PianoRoll::rowOfPitch (int pitch) const
{
    return pitchToRow[(size_t) juce::jlimit (0, 127, pitch)];
}

int PianoRoll::pitchOfRow (int row) const
{
    return rowPitches.empty() ? 60 : rowPitches[(size_t) juce::jlimit (0, numRows() - 1, row)];
}

void PianoRoll::rebuildRows()
{
    rowPitches.clear();
    if (v.fold && hasClip())
    {
        std::set<int> pitches;
        const auto c = clip();
        for (int i = 0; i < c.numNotes(); ++i)
            pitches.insert (c.note (i).pitch);
        if (pitches.empty())
            if (const auto s = scale(); s.scale != nullptr)
                for (int p = 0; p < 128; ++p)
                    if (daw::music::inScale (p, s.root, *s.scale))
                        pitches.insert (p);
        for (auto it = pitches.rbegin(); it != pitches.rend(); ++it)
            rowPitches.push_back (*it);
    }
    if (rowPitches.empty())
        for (int p = 127; p >= 0; --p)
            rowPitches.push_back (p);
    pitchToRow.fill (-1);
    for (int i = 0; i < numRows(); ++i)
        pitchToRow[(size_t) rowPitches[(size_t) i]] = i;
    clampView();
    updateScrollBars();
}

void PianoRoll::clampView()
{
    if (grid == nullptr)
        return;
    const int totalH = numRows() * v.rowHeight;
    v.scrollY = juce::jlimit (0, juce::jmax (0, totalH - grid->getHeight()), v.scrollY);
    v.scrollBeats = juce::jmax (0.0, v.scrollBeats);
}

void PianoRoll::zoomHorizontally (double factor, double anchorX)
{
    const double anchorBeat = xToBeat (anchorX);
    v.pxPerBeat = juce::jlimit (4.0, 1200.0, v.pxPerBeat * factor);
    v.scrollBeats = juce::jmax (0.0, anchorBeat - anchorX / v.pxPerBeat);
    clampView();
    updateScrollBars();
    props->refresh();
    for (auto* c : std::initializer_list<juce::Component*> { ruler.get(), grid.get(), velocityLane.get() })
        c->repaint();
}

void PianoRoll::setRowHeight (int h, int anchorY)
{
    h = juce::jlimit (kMinRowHeight, kMaxRowHeight, h);
    if (h == v.rowHeight)
        return;
    const double anchorRow = (double) (anchorY + v.scrollY) / v.rowHeight;
    v.rowHeight = h;
    v.scrollY = juce::roundToInt (anchorRow * h - anchorY);
    clampView();
    updateScrollBars();
    keys->repaint();
    grid->repaint();
}

void PianoRoll::zoomToClip()
{
    const auto c = clip();
    if (! c.isValid() || grid->getWidth() <= 0)
        return;
    const double length = juce::jmax (c.lengthBeats(), beatsPerBar());
    v.pxPerBeat = juce::jlimit (8.0, 400.0, (grid->getWidth() - 24.0) / length);
    v.scrollBeats = 0.0;
    clampView();
}

void PianoRoll::showNoteRange (bool)
{
    const auto c = clip();
    if (! c.isValid() || grid->getHeight() <= 0)
        return;
    int lo = 127, hi = 0;
    for (int i = 0; i < c.numNotes(); ++i)
    {
        lo = juce::jmin (lo, c.note (i).pitch);
        hi = juce::jmax (hi, c.note (i).pitch);
    }
    if (c.numNotes() == 0)
    {
        lo = 60; // C3..C5 for an empty clip
        hi = 84;
    }
    const int rowHi = rowOfPitch (hi) >= 0 ? rowOfPitch (hi) : 0;
    int rowLo = rowOfPitch (lo) >= 0 ? rowOfPitch (lo) : numRows() - 1;
    rowLo = juce::jmax (rowLo, rowHi);
    const int spanH = (rowLo - rowHi + 1) * v.rowHeight;
    if (spanH + 2 * v.rowHeight >= grid->getHeight())
        v.scrollY = rowHi * v.rowHeight - v.rowHeight; // too tall to centre: show the top of the range
    else
        v.scrollY = rowHi * v.rowHeight - (grid->getHeight() - spanH) / 2;
    clampView();
    updateScrollBars();
}

void PianoRoll::updateScrollBars()
{
    if (grid == nullptr || ! hasClip())
        return;
    const double length = juce::jmax (clip().lengthBeats(), 1.0);
    const double visible = grid->getWidth() / v.pxPerBeat;
    hbar.setRangeLimits (0.0, juce::jmax (length + 2.0, v.scrollBeats + visible));
    hbar.setCurrentRange (v.scrollBeats, visible, juce::dontSendNotification);
    vbar.setRangeLimits (0.0, (double) numRows() * v.rowHeight);
    vbar.setCurrentRange ((double) v.scrollY, (double) grid->getHeight(), juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hbar)
        v.scrollBeats = juce::jmax (0.0, start);
    else
        v.scrollY = juce::roundToInt (start);
    clampView();
    for (auto* c : std::initializer_list<juce::Component*> { ruler.get(), keys.get(), grid.get(), velocityLane.get() })
        c->repaint();
}

//==============================================================================
bool PianoRoll::isSelected (const juce::ValueTree& n) const
{
    return std::find (selected.begin(), selected.end(), n) != selected.end();
}

void PianoRoll::setSelection (clipedit::Nodes nodes)
{
    selected = std::move (nodes);
    grid->repaint();
    velocityLane->repaint();
    props->refresh();
}

void PianoRoll::pruneSelection()
{
    const auto c = clip();
    if (! c.isValid())
    {
        selected.clear();
        return;
    }
    auto valid = clipedit::onlyValid (c, selected);
    if (valid.size() != selected.size())
        selected = std::move (valid);
}

void PianoRoll::selectAllNotes()
{
    if (hasClip())
        setSelection (clipedit::allNodes (clip()));
}

void PianoRoll::invertSelection()
{
    clipedit::Nodes inverted;
    for (const auto& n : clipedit::allNodes (clip()))
        if (! isSelected (n))
            inverted.push_back (n);
    setSelection (std::move (inverted));
}

void PianoRoll::selectPitch (int pitch)
{
    clipedit::Nodes same;
    for (const auto& n : clipedit::allNodes (clip()))
        if (ClipRef::readNote (n).pitch == pitch)
            same.push_back (n);
    setSelection (std::move (same));
}

//==============================================================================
void PianoRoll::deleteSelected()
{
    const auto c = clip();
    if (! c.isValid() || clipedit::onlyValid (c, selected).empty())
        return;
    model.undo.beginNewTransaction (Strings::undoDeleteNotes);
    clipedit::deleteNotes (c, selected, &model.undo);
    setSelection ({});
}

void PianoRoll::duplicateSelected()
{
    const auto c = clip();
    const auto nodes = clipedit::onlyValid (c, selected);
    if (nodes.empty())
        return;
    double first = 1.0e9, last = 0.0;
    for (const auto& n : nodes)
    {
        const auto note = ClipRef::readNote (n);
        first = juce::jmin (first, note.start);
        last = juce::jmax (last, note.start + note.length);
    }
    model.undo.beginNewTransaction (Strings::undoDuplicateNotes);
    auto copies = clipedit::duplicateNotes (c, nodes, last - first, &model.undo);
    extendClipFor (clipedit::lastNoteEnd (c));
    setSelection (std::move (copies));
}

void PianoRoll::copySelected (bool cut)
{
    const auto c = clip();
    const auto nodes = clipedit::onlyValid (c, selected);
    if (nodes.empty())
        return;
    double first = 1.0e9;
    std::vector<daw::engine::Note> notes;
    for (const auto& n : nodes)
    {
        notes.push_back (ClipRef::readNote (n));
        first = juce::jmin (first, notes.back().start);
    }
    for (auto& n : notes)
        n.start -= first;
    clipboard() = std::move (notes);
    if (cut)
    {
        model.undo.beginNewTransaction (Strings::undoCutNotes);
        clipedit::deleteNotes (c, nodes, &model.undo);
        setSelection ({});
    }
}

void PianoRoll::pasteAt (double beat)
{
    const auto c = clip();
    if (! c.isValid() || clipboard().empty())
        return;
    auto notes = clipboard();
    for (auto& n : notes)
        n.start += juce::jmax (0.0, beat);
    model.undo.beginNewTransaction (Strings::undoPasteNotes);
    auto pasted = clipedit::insertNotes (c, notes, &model.undo);
    extendClipFor (clipedit::lastNoteEnd (c));
    setSelection (std::move (pasted));
}

void PianoRoll::splitAt (double beat)
{
    const auto c = clip();
    if (! c.isValid())
        return;
    const auto nodes = clipedit::selectionOrAll (c, selected);
    model.undo.beginNewTransaction (Strings::undoSplitNotes);
    auto created = clipedit::splitNotes (c, nodes, beat, &model.undo);
    if (created.empty())
        return;
    auto sel = clipedit::onlyValid (c, selected);
    sel.insert (sel.end(), created.begin(), created.end());
    setSelection (std::move (sel));
}

void PianoRoll::joinSelected()
{
    const auto c = clip();
    const auto nodes = clipedit::onlyValid (c, selected);
    if (nodes.size() < 2)
        return;
    model.undo.beginNewTransaction (Strings::undoJoinNotes);
    clipedit::joinNotes (c, nodes, &model.undo);
    pruneSelection();
    setSelection (selected);
}

void PianoRoll::transposeSelected (int semitones, bool scaleStep)
{
    const auto c = clip();
    if (! c.isValid())
        return;
    const auto nodes = clipedit::selectionOrAll (c, selected);
    if (nodes.empty())
        return;
    model.undo.beginNewTransaction (Strings::undoTransposeNotes);
    clipedit::transposeNotes (c, nodes, semitones, scaleStep ? scale() : clipedit::Scale {}, &model.undo);
    if (nodes.size() == 1)
        audition (ClipRef::readNote (nodes.front()).pitch, ClipRef::readNote (nodes.front()).velocity);
    grid->repaint();
}

void PianoRoll::nudgeSelected (double dBeats, int dSemitones)
{
    const auto c = clip();
    const auto nodes = clipedit::onlyValid (c, selected);
    if (nodes.empty())
        return;
    model.undo.beginNewTransaction (Strings::undoMoveNotes);
    if (dSemitones != 0 && v.scaleOnly)
    {
        // along the scale: one step up or down for the semitone keys, octaves stay octaves
        if (std::abs (dSemitones) == 1)
            clipedit::transposeNotes (c, nodes, dSemitones, scale(), &model.undo);
        else
            clipedit::shiftNotes (c, nodes, dBeats, dSemitones, &model.undo);
    }
    else
        clipedit::shiftNotes (c, nodes, dBeats, dSemitones, &model.undo);
    if (dBeats > 0.0)
        extendClipFor (clipedit::lastNoteEnd (c));
    if (dSemitones != 0 && nodes.size() == 1)
        audition (ClipRef::readNote (nodes.front()).pitch, ClipRef::readNote (nodes.front()).velocity);
}

void PianoRoll::resizeSelected (double dBeats)
{
    const auto c = clip();
    const auto nodes = clipedit::onlyValid (c, selected);
    if (nodes.empty())
        return;
    model.undo.beginNewTransaction (Strings::undoResizeNotes);
    clipedit::resizeNotes (c, nodes, dBeats, kMinNoteLength, &model.undo);
    if (dBeats > 0.0)
        extendClipFor (clipedit::lastNoteEnd (c));
}

void PianoRoll::loopAroundSelection()
{
    const auto c = clip();
    const auto nodes = clipedit::onlyValid (c, selected);
    if (nodes.empty())
        return;
    double first = 1.0e9, last = 0.0;
    for (const auto& n : nodes)
    {
        const auto note = ClipRef::readNote (n);
        first = juce::jmin (first, note.start);
        last = juce::jmax (last, note.start + note.length);
    }
    const double step = gridStep();
    const double start = std::floor (first / step + 1.0e-6) * step;
    double end = std::ceil (last / step - 1.0e-6) * step;
    end = juce::jmax (end, start + step);
    model.undo.beginNewTransaction (Strings::undoLoopRange);
    c.setLoopStart (start, &model.undo);
    c.setLengthBeats (end, &model.undo);
    c.setStartOffset (start, &model.undo);
    c.setLoopEnabled (true, &model.undo);
    flashLoop();
}

void PianoRoll::extendClipFor (double endBeat)
{
    const auto c = clip();
    if (! c.isValid())
        return;
    const double fit = clipedit::lengthToFit (c, endBeat, beatsPerBar());
    if (fit > c.lengthBeats() + 1.0e-9)
    {
        c.setLengthBeats (fit, &model.undo);
        flashLoop();
    }
}

void PianoRoll::flashLoop()
{
    loopFlashUntil = juce::Time::getMillisecondCounter() + 900;
    if (loopFlashUntil == 0)
        loopFlashUntil = 1;
    ruler->repaint();
    if (onClipExtended)
        onClipExtended (clip().isValid() ? clip().lengthBeats() : 0.0);
}

void PianoRoll::setLoopEnabled (bool on)
{
    actions.setClipLoopEnabled (currentClip, on);
}

//==============================================================================
bool PianoRoll::quantizeSelected()
{
    const auto c = clip();
    if (! c.isValid())
        return false;
    const auto q = model.quantiseSettings();
    const auto nodes = clipedit::selectionOrAll (c, selected);
    if (nodes.empty())
        return true;
    model.undo.beginNewTransaction (Strings::undoQuantise);
    clipedit::quantizeNotes (c, nodes, q.grid, q.strength, q.swing, q.ends, &model.undo);
    return true;
}

void PianoRoll::showQuantizeWindow()
{
    if (! hasClip())
        return;
    if (quantiseWindow == nullptr)
    {
        QuantiseWindow::Hooks hooks;
        hooks.settings = [this] { return model.quantiseSettings(); };
        hooks.apply = [this] (const ProjectModel::QuantiseSettings& q) {
            model.undo.beginNewTransaction (Strings::undoQuantise);
            model.setQuantiseSettings (q);
            const auto c = clip();
            if (c.isValid())
                clipedit::quantizeNotes (c, clipedit::selectionOrAll (c, selected), q.grid, q.strength, q.swing, q.ends, &model.undo);
        };
        hooks.closed = [safe = juce::Component::SafePointer<PianoRoll> (this)] {
            juce::MessageManager::callAsync ([safe] {
                if (safe != nullptr)
                    safe->quantiseWindow.reset();
            });
        };
        quantiseWindow = std::make_unique<QuantiseWindow> (std::move (hooks));
    }
    quantiseWindow->toFront (true);
}

//==============================================================================
void PianoRoll::audition (int pitch, int velocity)
{
    if (! v.preview)
        return;
    auto& state = engine.getKeyboardState();
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& s : sounding)
        if (s.pitch == pitch && s.offAt != 0)
        {
            state.noteOff (1, pitch, 0.0f);
            state.noteOn (1, pitch, (float) velocity / 127.0f);
            s.offAt = now + kAuditionMs;
            return;
        }
    state.noteOn (1, pitch, (float) velocity / 127.0f);
    sounding.push_back ({ pitch, now + kAuditionMs });
}

void PianoRoll::noteOnKey (int pitch)
{
    engine.getKeyboardState().noteOn (1, pitch, 0.8f);
    sounding.push_back ({ pitch, 0 });
}

void PianoRoll::noteOffKey (int pitch)
{
    for (auto it = sounding.begin(); it != sounding.end(); ++it)
        if (it->pitch == pitch && it->offAt == 0)
        {
            engine.getKeyboardState().noteOff (1, pitch, 0.0f);
            sounding.erase (it);
            return;
        }
}

//==============================================================================
void PianoRoll::showContextMenu (juce::Point<int> screenPosition, double beatUnderMouse, int pitchUnderMouse)
{
    namespace E = Strings::editor;
    const bool any = ! clipedit::onlyValid (clip(), selected).empty();
    const auto cmd = juce::ModifierKeys::commandModifier;
    juce::PopupMenu menu;
    const auto item = [&] (int id, const juce::String& text, bool enabled, const juce::KeyPress& key) {
        juce::PopupMenu::Item it (text);
        it.itemID = id;
        it.isEnabled = enabled;
        it.shortcutKeyDescription = key.getTextDescription();
        menu.addItem (it);
    };
    const auto key = [] (juce::juce_wchar c, int mods) { return juce::KeyPress (c, juce::ModifierKeys (mods), 0); };
    item (1, E::menuSelectAll, true, key ('a', cmd));
    item (2, E::menuInvert, true, key ('a', cmd | juce::ModifierKeys::shiftModifier));
    item (3, E::menuSelectPitch, pitchUnderMouse >= 0, {});
    menu.addSeparator();
    item (4, E::menuCopy, any, key ('c', cmd));
    item (5, E::menuPaste, ! clipboard().empty(), key ('v', cmd));
    item (6, E::menuDuplicate, any, key ('d', cmd));
    item (7, E::menuDelete, any, juce::KeyPress (juce::KeyPress::deleteKey));
    menu.addSeparator();
    item (8, E::menuSplit, true, key ('e', cmd));
    item (9, E::menuJoin, selected.size() > 1, key ('j', cmd));
    item (10, E::menuQuantise, true, key ('u', cmd));
    item (11, E::menuQuantiseOptions, true, key ('u', cmd | juce::ModifierKeys::shiftModifier));
    item (12, E::menuLoopSelection, any, key ('l', cmd));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe = juce::Component::SafePointer<PianoRoll> (this), beatUnderMouse, pitchUnderMouse] (int result) {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& s = *safe;
                            switch (result)
                            {
                                case 1: s.selectAllNotes(); break;
                                case 2: s.invertSelection(); break;
                                case 3: s.selectPitch (pitchUnderMouse); break;
                                case 4: s.copySelected (false); break;
                                case 5: s.pasteAt (s.snapBeat (beatUnderMouse, false)); break;
                                case 6: s.duplicateSelected(); break;
                                case 7: s.deleteSelected(); break;
                                case 8: s.splitAt (s.snapBeat (beatUnderMouse, false)); break;
                                case 9: s.joinSelected(); break;
                                case 10: s.quantizeSelected(); break;
                                case 11: s.showQuantizeWindow(); break;
                                case 12: s.loopAroundSelection(); break;
                                default: break;
                            }
                        });
}

//==============================================================================
bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (! hasClip() || textFieldHasFocus())
        return false;

    const int cmd = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier;
    const auto is = [&key] (juce::juce_wchar c, int mods) { return key == juce::KeyPress (c, juce::ModifierKeys (mods), 0); };
    const double step = gridStep();
    const auto mouseBeat = [this] {
        const auto p = grid->getMouseXYRelative();
        return grid->getLocalBounds().contains (p) ? snapBeat (xToBeat ((double) p.x), false) : v.editCursor;
    };

    if (is (juce::KeyPress::deleteKey, 0) || is (juce::KeyPress::backspaceKey, 0))
    {
        if (selected.empty())
            return false;
        deleteSelected();
        return true;
    }
    if (is ('a', cmd))
    {
        selectAllNotes();
        return true;
    }
    if (is ('a', cmd | shift))
    {
        invertSelection();
        return true;
    }
    if (is ('c', cmd))
    {
        copySelected (false);
        return true;
    }
    if (is ('x', cmd))
    {
        copySelected (true);
        return true;
    }
    if (is ('v', cmd))
    {
        pasteAt (mouseBeat());
        return true;
    }
    if (is ('d', cmd))
    {
        duplicateSelected();
        return true;
    }
    if (is ('e', cmd))
    {
        splitAt (mouseBeat());
        return true;
    }
    if (is ('j', cmd))
    {
        joinSelected();
        return true;
    }
    if (is ('l', cmd))
    {
        loopAroundSelection();
        return true;
    }
    if (is ('1', cmd))
    {
        stepGrid (1);
        return true;
    }
    if (is ('2', cmd))
    {
        stepGrid (-1);
        return true;
    }
    if (is ('3', cmd))
    {
        v.triplet = ! v.triplet;
        props->refresh();
        repaint();
        grid->repaint();
        ruler->repaint();
        return true;
    }
    if (is ('4', cmd))
    {
        v.snap = ! v.snap;
        props->refresh();
        return true;
    }
    if (is ('=', cmd) || is ('+', cmd | shift))
    {
        zoomHorizontally (1.25, grid->getWidth() * 0.5);
        return true;
    }
    if (is ('-', cmd))
    {
        zoomHorizontally (0.8, grid->getWidth() * 0.5);
        return true;
    }
    if (is ('b', 0))
    {
        v.pencil = ! v.pencil;
        props->refresh();
        return true;
    }
    if (is (juce::KeyPress::escapeKey, 0) && ! selected.empty())
    {
        setSelection ({});
        return true;
    }

    // arrows act on the selection
    const bool left = key.isKeyCode (juce::KeyPress::leftKey), right = key.isKeyCode (juce::KeyPress::rightKey);
    const bool up = key.isKeyCode (juce::KeyPress::upKey), down = key.isKeyCode (juce::KeyPress::downKey);
    if ((left || right || up || down) && ! selected.empty())
    {
        const auto mods = key.getModifiers();
        const bool withCmd = mods.isCommandDown(), withShift = mods.isShiftDown(), withAlt = mods.isAltDown();
        if (withCmd && withShift && (up || down))
        {
            transposeSelected (up ? 1 : -1, true); // keeps the notes in the scale
            return true;
        }
        if (withCmd || withAlt)
            return false;
        if (left || right)
        {
            const double sign = right ? 1.0 : -1.0;
            if (withShift)
                resizeSelected (sign * step);
            else
                nudgeSelected (sign * step, 0);
        }
        else
            nudgeSelected (0.0, (up ? 1 : -1) * (withShift ? 12 : 1));
        return true;
    }
    return false;
}

//==============================================================================
void PianoRoll::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.background);
    if (! hasClip())
    {
        g.setColour (t.text);
        g.setFont (t.uiFont (16.0f, 600));
        g.drawText (Strings::editor::noClip, getLocalBounds().withTrimmedBottom (24), juce::Justification::centred, false);
        g.setColour (t.dimText);
        g.setFont (t.uiFont (12.5f));
        g.drawText (Strings::editor::noClipHint, getLocalBounds().reduced (40, 0).withTrimmedTop (getHeight() / 2 + 4), juce::Justification::centredTop, true);
    }
}

void PianoRoll::resized()
{
    layoutParts();
}

void PianoRoll::layoutParts()
{
    auto r = getLocalBounds();
    props->setBounds (r.removeFromLeft (kPropsWidth));
    auto ctrl = r.removeFromBottom (v.controllersOpen ? kControllerHeader + kControllerBody : kControllerHeader);
    controllers->setBounds (ctrl);
    auto vel = r.removeFromBottom (kVelocityHeight);
    velocityLane->setBounds (vel.withTrimmedRight (kScrollBar));
    ruler->setBounds (r.removeFromTop (kRulerHeight).withTrimmedLeft (kKeysWidth).withTrimmedRight (kScrollBar));
    auto bar = r.removeFromBottom (kScrollBar);
    hbar.setBounds (bar.withTrimmedLeft (kKeysWidth).withTrimmedRight (kScrollBar));
    keys->setBounds (r.removeFromLeft (kKeysWidth));
    vbar.setBounds (r.removeFromRight (kScrollBar));
    grid->setBounds (r);

    if (hasClip() && ! viewInitialised && grid->getWidth() > 0 && grid->getHeight() > 0)
    {
        zoomToClip();
        showNoteRange (true);
        viewInitialised = true;
    }
    clampView();
    updateScrollBars();
}

juce::Rectangle<int> PianoRoll::gridBounds() const
{
    return grid->getBounds();
}

juce::Rectangle<int> PianoRoll::noteBounds (const juce::ValueTree& noteNode) const
{
    const auto n = ClipRef::readNote (noteNode);
    const int row = rowOfPitch (n.pitch);
    if (row < 0)
        return {};
    const int x = juce::roundToInt (beatToX (n.start)), x2 = juce::roundToInt (beatToX (n.start + n.length));
    return juce::Rectangle<int> (x, rowToY (row), juce::jmax (2, x2 - x), v.rowHeight).translated (grid->getX(), grid->getY());
}
} // namespace ui
