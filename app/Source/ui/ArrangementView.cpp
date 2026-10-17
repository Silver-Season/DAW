#include "ArrangementView.h"

#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "../project/AudioImport.h"
#include "ArrangementHeaders.h"
#include "ArrangementLanes.h"
#include "ArrangementMinimap.h"
#include "ArrangementPlayhead.h"
#include "ArrangementRuler.h"
#include "TransportMenus.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ui
{
namespace
{
constexpr double kEpsilon = 1.0e-6;

// The key as a lower-case letter, by key code: with a Russian layout the typed character is Cyrillic, but the
// key code stays Latin (the same rule as ComputerKeys and the Prisma keyboard panel).
int codeOf (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    return code > 0 && code < 128 ? (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code) : code;
}
} // namespace

ArrangementView::ArrangementView (AudioEngine& e, ProjectModel& m, ProjectActions& a, SelectionState& s)
    : engine (e), model (m), actions (a), selectionState (s)
{
    setHelpText (Strings::help::arrangementView);
    setWantsKeyboardFocus (true);

    lanesPart = std::make_unique<ArrangementLanes> (*this);
    rulerPart = std::make_unique<ArrangementRuler> (*this);
    headersPart = std::make_unique<ArrangementHeaders> (*this);
    cornerPart = std::make_unique<ArrangementCorner> (*this);
    minimapPart = std::make_unique<ArrangementMinimap> (*this);
    playheadPart = std::make_unique<ArrangementPlayhead> (*this);

    vbar.setAutoHide (false);
    vbar.setHelpText (Strings::help::vscroll);
    vbar.addListener (this);

    for (auto* c : std::initializer_list<juce::Component*> { lanesPart.get(), headersPart.get(), rulerPart.get(), cornerPart.get(), minimapPart.get(),
                                                              &vbar, playheadPart.get() })
        addAndMakeVisible (c);
    model.root.addListener (this);
    rebuildRows();
}

ArrangementView::~ArrangementView()
{
    model.root.removeListener (this);
    vbar.removeListener (this);
}

//==============================================================================
int ArrangementView::lanesWidth() const
{
    return juce::jmax (1, getWidth() - kHeaderWidth - kScrollBar);
}

int ArrangementView::lanesHeight() const
{
    return juce::jmax (1, getHeight() - kRulerHeight - kMinimapHeight);
}

juce::Rectangle<int> ArrangementView::lanesBounds() const
{
    return { kHeaderWidth, kRulerHeight, lanesWidth(), lanesHeight() };
}

double ArrangementView::totalBeats() const
{
    return arrangement::songLengthBeats (model) + (double) arrangement::kEmptyBarsAfterEnd * beatsPerBar();
}

int ArrangementView::contentHeight() const
{
    return rowsTotalHeight;
}

int ArrangementView::rowIndexOfTrack (int64_t trackId) const
{
    for (size_t i = 0; i < layoutRows.size(); ++i)
        if (layoutRows[i].trackId == trackId)
            return (int) i;
    return -1;
}

int ArrangementView::rowAtY (int y) const
{
    const int content = y + v.scrollY;
    for (size_t i = 0; i < layoutRows.size(); ++i)
        if (content >= layoutRows[i].y && content < layoutRows[i].y + layoutRows[i].height)
            return (int) i;
    return -1;
}

//==============================================================================
// Track heights
int ArrangementView::fittedHeight (const TrackRef& track) const
{
    int lowest = 128, highest = -1;
    for (const auto& c : track.arrangementClips())
        if (c.clip().isValid())
            for (const auto& n : c.clip().notes())
            {
                lowest = std::min (lowest, n.pitch);
                highest = std::max (highest, n.pitch);
            }
    if (highest < lowest)
        return 56;
    return juce::jlimit (56, 200, 34 + (highest - lowest + 1) * 4);
}

int ArrangementView::trackHeight (const TrackRef& track) const
{
    if (v.uniformHeights)
        return v.uniformHeight;
    const int own = track.arrangementHeight();
    return juce::jlimit (kMinTrackHeight, kMaxTrackHeight, own > 0 ? own : fittedHeight (track));
}

void ArrangementView::setTrackHeight (int64_t trackId, int height, bool newTransaction)
{
    height = juce::jlimit (kMinTrackHeight, kMaxTrackHeight, height);
    if (v.uniformHeights)
        v.uniformHeight = height;
    else if (const auto track = model.findTrack (trackId); track.isValid() && track.arrangementHeight() != height)
    {
        if (newTransaction)
            model.undo.beginNewTransaction (Strings::undoArrTrackHeight);
        track.setArrangementHeight (height, &model.undo);
    }
    rebuildRows();
}

void ArrangementView::toggleHeightMode()
{
    v.uniformHeights = ! v.uniformHeights;
    rebuildRows();
    message (v.uniformHeights ? Strings::arrangement::heightsUniformMessage : Strings::arrangement::heightsByContentMessage);
}

void ArrangementView::rebuildRows()
{
    layoutRows.clear();
    int y = 0;
    for (const auto& track : model.tracks())
    {
        const int h = trackHeight (track);
        layoutRows.push_back ({ track.id(), y, h });
        y += h;
    }
    rowsTotalHeight = y;

    // forget what is gone
    std::vector<int64_t> alive;
    for (const auto id : selectedAClips)
        if (arrangement::findClip (model, id).isValid())
            alive.push_back (id);
    const bool selectionLost = alive.size() != selectedAClips.size();
    selectedAClips = alive;
    if (markerSel >= 0)
    {
        bool found = false;
        for (const auto& mk : model.markers())
            found = found || mk.id() == markerSel;
        if (! found)
            markerSel = -1;
    }
    for (size_t i = timeSel.tracks.size(); i-- > 0;)
        if (! model.findTrack (timeSel.tracks[i]).isValid())
            timeSel.tracks.erase (timeSel.tracks.begin() + (std::ptrdiff_t) i);
    juce::ignoreUnused (selectionLost);

    clampView();
    headersPart->refreshRows();
    updateScrollBar();
    repaintAll();
}

//==============================================================================
// Zoom, scroll, grid
void ArrangementView::clampView()
{
    const double total = totalBeats();
    const double minPx = juce::jmax (kMinPxPerBeat, (double) lanesWidth() / total);
    v.pxPerBeat = juce::jlimit (minPx, kMaxPxPerBeat, v.pxPerBeat);
    v.scrollBeats = juce::jlimit (0.0, juce::jmax (0.0, total - visibleBeats()), v.scrollBeats);
    v.scrollY = juce::jlimit (0, juce::jmax (0, rowsTotalHeight - lanesHeight()), v.scrollY);
}

void ArrangementView::updateScrollBar()
{
    vbar.setRangeLimits (0.0, (double) juce::jmax (rowsTotalHeight, lanesHeight()));
    vbar.setCurrentRange ((double) v.scrollY, (double) lanesHeight(), juce::dontSendNotification);
}

void ArrangementView::scrollBarMoved (juce::ScrollBar*, double start)
{
    setScrollY (juce::roundToInt (start));
}

void ArrangementView::repaintAll()
{
    lanesPart->repaint();
    rulerPart->repaint();
    minimapPart->repaint();
    headersPart->repaint();
    cornerPart->repaint();
}

void ArrangementView::repaintTimeParts()
{
    lanesPart->repaint();
    rulerPart->repaint();
    minimapPart->repaint();
}

void ArrangementView::setZoom (double pxPerBeat, double anchorX)
{
    autoZoom = false;
    const double anchorBeat = xToBeat (anchorX);
    v.pxPerBeat = pxPerBeat;
    clampView(); // fixes the zoom
    v.scrollBeats = anchorBeat - anchorX / v.pxPerBeat;
    clampView();
    repaintTimeParts();
}

void ArrangementView::zoomBy (double factor, double anchorX)
{
    setZoom (v.pxPerBeat * factor, anchorX);
}

void ArrangementView::setScrollBeats (double beats)
{
    v.scrollBeats = beats;
    clampView();
    repaintTimeParts();
}

void ArrangementView::setScrollY (int y)
{
    if (y == v.scrollY)
        return;
    v.scrollY = y;
    clampView();
    headersPart->refreshRows();
    updateScrollBar();
    lanesPart->repaint();
}

void ArrangementView::scrollIntoView (double beat)
{
    if (beat < v.scrollBeats || beat > v.scrollBeats + visibleBeats() * 0.95)
        setScrollBeats (beat - visibleBeats() * 0.1);
}

void ArrangementView::remember()
{
    previousZoom = Zoom { v.pxPerBeat, v.scrollBeats };
}

void ArrangementView::zoomToSelection()
{
    autoZoom = false;
    double from = 0.0, to = 0.0;
    if (! timeSel.isEmpty())
    {
        from = timeSel.start;
        to = timeSel.end;
    }
    else if (const auto clips = selectedClipRefs(); ! clips.empty())
    {
        from = clips.front().start();
        to = clips.front().end();
        for (const auto& c : clips)
        {
            from = std::min (from, c.start());
            to = std::max (to, c.end());
        }
    }
    else
    {
        remember();
        zoomToSong();
        return;
    }
    remember();
    const double span = juce::jmax (0.25, to - from);
    v.pxPerBeat = (double) lanesWidth() / (span * 1.1);
    clampView();
    v.scrollBeats = from - span * 0.05;
    clampView();
    repaintTimeParts();
}

void ArrangementView::zoomBack()
{
    if (! previousZoom)
        return;
    autoZoom = false;
    const auto z = *previousZoom;
    remember();
    v.pxPerBeat = z.px;
    v.scrollBeats = z.scroll;
    clampView();
    repaintTimeParts();
}

void ArrangementView::zoomToSong()
{
    autoZoom = false;
    fitToSong();
}

void ArrangementView::fitToSong()
{
    const double shown = juce::jmin (totalBeats(), 48.0 * beatsPerBar());
    v.pxPerBeat = (double) lanesWidth() / shown;
    v.scrollBeats = 0.0;
    clampView();
    repaintTimeParts();
}

double ArrangementView::gridStep() const
{
    const double tri = v.triplet ? 2.0 / 3.0 : 1.0;
    if (! v.adaptiveGrid)
        return v.fixedStep * tri;
    static constexpr double steps[] { 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5, 1.0, 2.0 };
    for (const double s : steps)
        if (s * tri * v.pxPerBeat >= kMinGridPixels)
            return s * tri;
    const double bar = juce::jmax (1.0, beatsPerBar());
    for (int bars = 1; bars <= 1024; bars *= 2)
        if (bar * bars * v.pxPerBeat >= kMinGridPixels)
            return bar * bars;
    return bar * 1024.0;
}

double ArrangementView::snapBeat (double beat, bool bypass) const
{
    if (bypass || ! v.snap)
        return juce::jmax (0.0, beat);
    const double step = gridStep();
    return juce::jmax (0.0, std::round (beat / step) * step);
}

double ArrangementView::snapFloor (double beat) const
{
    const double step = gridStep();
    return juce::jmax (0.0, std::floor (beat / step + kEpsilon) * step);
}

juce::String ArrangementView::gridText() const
{
    const double step = gridStep() / (v.triplet ? 2.0 / 3.0 : 1.0);
    juce::String text;
    if (step >= beatsPerBar() - kEpsilon)
    {
        const int bars = juce::roundToInt (step / beatsPerBar());
        text = bars == 1 ? Strings::arrangement::gridBar : Strings::arrangement::gridBars (bars);
    }
    else if (step >= 1.0 - kEpsilon)
        text = step < 1.5 ? Strings::arrangement::gridBeat : Strings::arrangement::gridBeats (juce::roundToInt (step));
    else
        text = "1/" + juce::String (juce::roundToInt (4.0 / step));
    if (v.triplet)
        text << "T";
    return text;
}

int ArrangementView::barLabelStride() const
{
    int stride = 1;
    while (stride < 4096 && beatsPerBar() * stride * v.pxPerBeat < 56.0)
        stride *= 2;
    return stride;
}

int ArrangementView::barLineStride() const
{
    int stride = 1;
    while (stride < 4096 && beatsPerBar() * stride * v.pxPerBeat < 7.0)
        stride *= 2;
    return stride;
}

void ArrangementView::stepGrid (int direction)
{
    if (v.adaptiveGrid)
    {
        v.fixedStep = gridStep() / (v.triplet ? 2.0 / 3.0 : 1.0);
        v.adaptiveGrid = false;
    }
    const double bar = juce::jmax (1.0, beatsPerBar());
    v.fixedStep = direction > 0 ? v.fixedStep * 0.5 : v.fixedStep * 2.0;
    v.fixedStep = juce::jlimit (1.0 / 16.0, bar * 64.0, v.fixedStep);
    repaintAll();
}

void ArrangementView::toggleTriplet()
{
    v.triplet = ! v.triplet;
    repaintAll();
}

void ArrangementView::toggleSnap()
{
    v.snap = ! v.snap;
    repaintAll();
    message (v.snap ? Strings::arrangement::snapOn : Strings::arrangement::snapOff);
}

//==============================================================================
// Selection
bool ArrangementView::isClipSelected (int64_t aclipId) const
{
    return std::find (selectedAClips.begin(), selectedAClips.end(), aclipId) != selectedAClips.end();
}

std::vector<ArrangementClipRef> ArrangementView::selectedClipRefs() const
{
    std::vector<ArrangementClipRef> out;
    for (const auto id : selectedAClips)
        if (const auto c = arrangement::findClip (model, id); c.isValid())
            out.push_back (c);
    return out;
}

void ArrangementView::selectionChanged()
{
    if (! selectedAClips.empty())
    {
        const auto last = arrangement::findClip (model, selectedAClips.back());
        if (last.isValid())
        {
            const auto track = arrangement::trackOf (model, last);
            model.setSelectedTrack (track.id());
            selectionState.selectArrangementClip (track.id(), last.id());
        }
    }
    repaintTimeParts();
}

void ArrangementView::selectClip (int64_t aclipId, bool add)
{
    timeSel = {};
    markerSel = -1;
    if (add)
    {
        auto it = std::find (selectedAClips.begin(), selectedAClips.end(), aclipId);
        if (it != selectedAClips.end())
            selectedAClips.erase (it);
        else
            selectedAClips.push_back (aclipId);
    }
    else
        selectedAClips = { aclipId };
    selectionChanged();
}

void ArrangementView::selectClips (const std::vector<int64_t>& ids)
{
    timeSel = {};
    markerSel = -1;
    selectedAClips = ids;
    selectionChanged();
}

void ArrangementView::selectAllClips()
{
    std::vector<int64_t> all;
    for (const auto& t : model.tracks())
        for (const auto& c : t.arrangementClips())
            all.push_back (c.id());
    selectClips (all);
}

void ArrangementView::setTimeSelection (double start, double end, const std::vector<int64_t>& trackIds)
{
    selectedAClips.clear();
    markerSel = -1;
    timeSel.start = juce::jmax (0.0, std::min (start, end));
    timeSel.end = juce::jmax (0.0, std::max (start, end));
    timeSel.tracks = trackIds;
    if (! trackIds.empty())
    {
        model.setSelectedTrack (trackIds.front());
        selectionState.selectTrack (trackIds.front());
    }
    repaintTimeParts();
}

void ArrangementView::selectMarker (int64_t markerId)
{
    selectedAClips.clear();
    timeSel = {};
    markerSel = markerId;
    repaintTimeParts();
}

void ArrangementView::clearSelection()
{
    selectedAClips.clear();
    timeSel = {};
    markerSel = -1;
    repaintTimeParts();
}

std::vector<TrackRef> ArrangementView::targetTracks() const
{
    std::vector<TrackRef> out;
    const auto add = [&] (const TrackRef& t) {
        if (t.isValid() && std::find (out.begin(), out.end(), t) == out.end())
            out.push_back (t);
    };
    if (! timeSel.isEmpty())
        for (const auto id : timeSel.tracks)
            add (model.findTrack (id));
    else if (! selectedAClips.empty())
        for (const auto& c : selectedClipRefs())
            add (arrangement::trackOf (model, c));
    else
        add (model.findTrack (model.selectedTrack()));
    return out;
}

//==============================================================================
// Time
double ArrangementView::cursorBeat() const
{
    return engine.isPlaying() ? engine.getSongPositionBeats() : model.arrangementPlayStart();
}

void ArrangementView::jumpToBeat (double beat, bool immediate)
{
    beat = juce::jmax (0.0, beat);
    if (engine.isPlaying() && ! immediate)
        engine.seekQuantised (beat);
    else
        engine.seek (beat);
}

void ArrangementView::setCursor (double beat, bool immediate)
{
    beat = juce::jmax (0.0, beat);
    model.setArrangementPlayStart (beat);
    jumpToBeat (beat, immediate);
    rulerPart->repaint();
}

void ArrangementView::placeCursor (double beat)
{
    beat = juce::jmax (0.0, beat);
    model.setArrangementPlayStart (beat);
    if (! engine.isPlaying())
        engine.seek (beat);
    rulerPart->repaint();
}

void ArrangementView::playHome()
{
    setCursor (0.0);
    setScrollBeats (0.0);
}

void ArrangementView::playEnd()
{
    double end = 0.0;
    for (const auto& t : model.tracks())
        for (const auto& c : t.arrangementClips())
            end = std::max (end, c.end());
    for (const auto& mk : model.markers())
        end = std::max (end, mk.beat());
    setCursor (end);
    scrollIntoView (end);
}

void ArrangementView::jumpToMarker (bool next)
{
    const double from = cursorBeat();
    const auto m = next ? arrangement::nextMarker (model, from) : arrangement::previousMarker (model, from);
    if (! m.isValid())
        return;
    setCursor (m.beat());
    scrollIntoView (m.beat());
}

void ArrangementView::toggleFollow()
{
    model.setFollowPlayhead (! model.followPlayhead());
    message (model.followPlayhead() ? Strings::arrangement::followOn : Strings::arrangement::followOff);
}

void ArrangementView::followPlayhead()
{
    if (! engine.isPlaying() || ! model.followPlayhead())
        return;
    const double beat = engine.getSongPositionBeats();
    const double visible = visibleBeats();
    // The page flips when the beam leaves the right edge (or a loop jump took it left of the view).
    if (beat >= v.scrollBeats + visible || beat < v.scrollBeats - kEpsilon)
        setScrollBeats (beat);
}

//==============================================================================
// Model changes and the 30 Hz tick
void ArrangementView::applyModelChanges()
{
    if (structureDirty)
    {
        structureDirty = propertiesDirty = false;
        rebuildRows();
        return;
    }
    if (propertiesDirty)
    {
        propertiesDirty = false;
        // A property can change a lane height (mode "by content" reads the notes) or the song length.
        rebuildRows();
    }
}

void ArrangementView::tick()
{
    applyModelChanges();

    // the grey bar of a track that plays Session View: repaint only when one changed
    std::vector<uint8_t> states;
    for (const auto& r : layoutRows)
    {
        auto* rt = engine.findRuntime (r.trackId);
        states.push_back (rt != nullptr && rt->inSession.load() ? 1 : 0);
    }
    if (states != lastInSession)
    {
        lastInSession = states;
        lanesPart->repaint();
        headersPart->repaint();
    }

    followPlayhead();
    headersPart->refreshValues();
    cornerPart->refresh();
    if (engine.isPlaying())
        minimapPart->repaint();
}

//==============================================================================
// Commands
bool ArrangementView::deleteSelection()
{
    if (markerSel >= 0)
    {
        for (const auto& mk : model.markers())
            if (mk.id() == markerSel)
            {
                model.undo.beginNewTransaction (Strings::undoArrMarkerDelete);
                arrangement::deleteMarker (model, mk);
                markerSel = -1;
                return true;
            }
        return false;
    }
    if (const auto clips = selectedClipRefs(); ! clips.empty())
    {
        model.undo.beginNewTransaction (Strings::undoArrDelete);
        arrangement::deleteClips (model, clips);
        selectedAClips.clear();
        repaintTimeParts();
        return true;
    }
    if (! timeSel.isEmpty())
    {
        model.undo.beginNewTransaction (Strings::undoArrDelete);
        for (const auto id : timeSel.tracks)
            if (const auto track = model.findTrack (id); track.isValid())
                arrangement::clearRange (model, track, timeSel.start, timeSel.end);
        return true;
    }
    return false;
}

bool ArrangementView::duplicateSelection()
{
    if (! timeSel.isEmpty())
    {
        std::vector<TrackRef> tracks;
        for (const auto id : timeSel.tracks)
            if (const auto t = model.findTrack (id); t.isValid())
                tracks.push_back (t);
        model.undo.beginNewTransaction (Strings::undoArrDuplicate);
        arrangement::duplicateRange (model, tracks, timeSel.start, timeSel.end);
        const double length = timeSel.end - timeSel.start;
        timeSel.start += length; // the copy is selected next, so that Cmd+D again continues the row
        timeSel.end += length;
        repaintTimeParts();
        return true;
    }
    const auto clips = selectedClipRefs();
    if (clips.empty())
        return false;
    model.undo.beginNewTransaction (Strings::undoArrDuplicate);
    if (clips.size() == 1)
    {
        const auto copy = arrangement::duplicateAfter (model, clips.front());
        if (copy.isValid())
            selectedAClips = { copy.id() };
    }
    else
    {
        // Several clips: the whole stretch they span is copied behind itself, so that the copies keep their distances.
        double from = clips.front().start(), to = clips.front().end();
        std::vector<TrackRef> tracks;
        for (const auto& c : clips)
        {
            from = std::min (from, c.start());
            to = std::max (to, c.end());
            const auto t = arrangement::trackOf (model, c);
            if (std::find (tracks.begin(), tracks.end(), t) == tracks.end())
                tracks.push_back (t);
        }
        arrangement::duplicateRange (model, tracks, from, to);
        selectedAClips.clear();
        timeSel.start = to;
        timeSel.end = to + (to - from);
        for (const auto& t : tracks)
            timeSel.tracks.push_back (t.id());
    }
    selectionChanged();
    return true;
}

bool ArrangementView::copySelection (bool cut)
{
    const auto clips = selectedClipRefs();
    if (clips.empty())
        return false;
    int firstRow = 1 << 20;
    double firstStart = clips.front().start();
    for (const auto& c : clips)
    {
        firstRow = std::min (firstRow, juce::jmax (0, rowIndexOfTrack (arrangement::trackOf (model, c).id())));
        firstStart = std::min (firstStart, c.start());
    }
    clipboardEntries.clear();
    for (const auto& c : clips)
    {
        ClipboardEntry e;
        e.snapshot = c.state.createCopy();
        e.trackOffset = juce::jmax (0, rowIndexOfTrack (arrangement::trackOf (model, c).id())) - firstRow;
        e.beatOffset = c.start() - firstStart;
        clipboardEntries.push_back (std::move (e));
    }
    clipboard = juce::ValueTree ("CLIPBOARD");
    if (cut)
        deleteSelection();
    return true;
}

bool ArrangementView::pasteAtCursor()
{
    if (clipboardEntries.empty())
        return false;
    int row0 = 0;
    if (! timeSel.isEmpty())
        row0 = juce::jmax (0, rowIndexOfTrack (timeSel.tracks.front()));
    else
        row0 = juce::jmax (0, rowIndexOfTrack (model.selectedTrack()));
    const double start = cursorBeat();
    model.undo.beginNewTransaction (Strings::undoArrPasteClips);
    std::vector<int64_t> made;
    for (const auto& e : clipboardEntries)
    {
        const int row = row0 + e.trackOffset;
        if (row < 0 || row >= (int) layoutRows.size())
            continue;
        const auto track = model.findTrack (layoutRows[(size_t) row].trackId);
        const auto pasted = arrangement::pasteClip (model, track, e.snapshot, start + e.beatOffset);
        if (pasted.isValid())
            made.push_back (pasted.id());
    }
    if (made.empty())
        return false;
    selectClips (made);
    return true;
}

bool ArrangementView::splitAtCursor()
{
    int cuts = 0;
    if (! timeSel.isEmpty())
    {
        std::vector<TrackRef> tracks;
        for (const auto id : timeSel.tracks)
            tracks.push_back (model.findTrack (id));
        model.undo.beginNewTransaction (Strings::undoArrSplit);
        cuts = arrangement::splitTracks (model, tracks, { timeSel.start, timeSel.end });
    }
    else if (const auto clips = selectedClipRefs(); ! clips.empty())
    {
        const double beat = cursorBeat();
        model.undo.beginNewTransaction (Strings::undoArrSplit);
        for (const auto& c : clips)
            if (arrangement::splitAt (model, c, beat).isValid())
                ++cuts;
    }
    else
    {
        model.undo.beginNewTransaction (Strings::undoArrSplit);
        cuts = arrangement::splitTracks (model, targetTracks(), { cursorBeat() });
    }
    return cuts > 0;
}

bool ArrangementView::joinSelection()
{
    bool did = false;
    const auto clips = selectedClipRefs();
    if (clips.size() >= 2)
    {
        std::vector<TrackRef> tracks;
        for (const auto& c : clips)
        {
            const auto t = arrangement::trackOf (model, c);
            if (std::find (tracks.begin(), tracks.end(), t) == tracks.end())
                tracks.push_back (t);
        }
        model.undo.beginNewTransaction (Strings::undoArrJoin);
        std::vector<int64_t> made;
        for (const auto& t : tracks)
        {
            arrangement::Clips group;
            for (const auto& c : clips)
                if (arrangement::trackOf (model, c) == t)
                    group.push_back (c);
            if (const auto joined = arrangement::joinClips (model, group); joined.isValid())
            {
                made.push_back (joined.id());
                did = true;
            }
        }
        if (did)
            selectClips (made);
        return did;
    }
    if (! timeSel.isEmpty())
    {
        // Consolidate: cut at the edges of the selection, then every track gets one clip out of what lies inside.
        std::vector<TrackRef> tracks;
        for (const auto id : timeSel.tracks)
            tracks.push_back (model.findTrack (id));
        model.undo.beginNewTransaction (Strings::undoArrJoin);
        arrangement::splitTracks (model, tracks, { timeSel.start, timeSel.end });
        for (const auto& t : tracks)
        {
            arrangement::Clips inside;
            for (const auto& c : arrangement::clipsIn (t, timeSel.start, timeSel.end))
                if (c.start() >= timeSel.start - kEpsilon && c.end() <= timeSel.end + kEpsilon)
                    inside.push_back (c);
            if (arrangement::joinClips (model, inside).isValid())
                did = true;
        }
    }
    return did;
}

bool ArrangementView::loopSelection()
{
    double from = 0.0, to = 0.0;
    if (! timeSel.isEmpty())
    {
        from = timeSel.start;
        to = timeSel.end;
    }
    else if (const auto clips = selectedClipRefs(); ! clips.empty())
    {
        from = clips.front().start();
        to = clips.front().end();
        for (const auto& c : clips)
        {
            from = std::min (from, c.start());
            to = std::max (to, c.end());
        }
    }
    else
        return false;
    setLoopBrace (from, to, true, true);
    return true;
}

bool ArrangementView::insertTimeAtSelection()
{
    double from = 0.0, to = 0.0;
    if (! timeSel.isEmpty())
    {
        from = timeSel.start;
        to = timeSel.end;
    }
    else if (const auto clips = selectedClipRefs(); ! clips.empty())
    {
        from = clips.front().start();
        to = clips.front().end();
        for (const auto& c : clips)
        {
            from = std::min (from, c.start());
            to = std::max (to, c.end());
        }
    }
    else
        return false;
    model.undo.beginNewTransaction (Strings::undoArrInsertTime);
    arrangement::insertTime (model, from, to - from);
    setTimeSelection (from, to, [&] {
        std::vector<int64_t> all;
        for (const auto& r : layoutRows)
            all.push_back (r.trackId);
        return all;
    }());
    return true;
}

bool ArrangementView::removeTimeAtSelection()
{
    if (timeSel.isEmpty())
        return false;
    model.undo.beginNewTransaction (Strings::undoArrRemoveTime);
    const double from = timeSel.start, to = timeSel.end;
    arrangement::removeTime (model, from, to);
    clearSelection();
    return true;
}

bool ArrangementView::addMarkerAtCursor()
{
    const double beat = cursorBeat();
    for (const auto& mk : model.markers())
        if (std::abs (mk.beat() - beat) < kEpsilon)
        {
            selectMarker (mk.id()); // there is one already
            return true;
        }
    model.undo.beginNewTransaction (Strings::undoArrMarkerAdd);
    const auto m = arrangement::addMarker (model, beat);
    if (m.isValid())
        selectMarker (m.id());
    return m.isValid();
}

bool ArrangementView::renameSelection()
{
    if (markerSel >= 0)
    {
        rulerPart->beginRenameMarker (markerSel);
        return true;
    }
    if (selectedAClips.size() == 1)
    {
        lanesPart->beginRenameClip (selectedAClips.front());
        return true;
    }
    if (const auto track = model.findTrack (model.selectedTrack()); track.isValid())
    {
        headersPart->beginRename (track.id());
        return true;
    }
    return false;
}

bool ArrangementView::nudgeSelection (int direction, bool extend)
{
    const double step = gridStep() * direction;
    if (! timeSel.isEmpty())
    {
        if (extend)
            timeSel.end = juce::jmax (timeSel.start + gridStep(), timeSel.end + step);
        else
        {
            const double shift = juce::jmax (-timeSel.start, step);
            timeSel.start += shift;
            timeSel.end += shift;
        }
        scrollIntoView (direction > 0 ? timeSel.end : timeSel.start);
        repaintTimeParts();
        return true;
    }
    if (! selectedAClips.empty())
    {
        std::vector<Dragged> list;
        for (const auto& c : selectedClipRefs())
            list.push_back ({ c.id(), arrangement::trackOf (model, c).id(), c.start() });
        commitMove (list, step, 0, false);
        return true;
    }
    setCursor (snapBeat (cursorBeat() + step, false));
    scrollIntoView (model.arrangementPlayStart());
    return true;
}

void ArrangementView::sendSelectionToSession()
{
    const auto clips = selectedClipRefs();
    if (clips.empty())
        return;
    const auto& sel = selectionState.get();
    const int fromScene = (sel.kind == Selection::Kind::clip || sel.kind == Selection::Kind::scene) && sel.scene >= 0 ? sel.scene : 0;
    model.undo.beginNewTransaction (Strings::undoArrSendToSession);
    int scene = -1;
    for (const auto& c : clips)
        scene = arrangement::sendToSession (model, c, fromScene);
    if (scene >= 0)
        message (Strings::arrangement::sentToSession (scene + 1));
}

bool ArrangementView::switchTake (int64_t aclipId, int number)
{
    const auto clip = arrangement::findClip (model, aclipId);
    if (! clip.isValid())
        return false;
    model.undo.beginNewTransaction (Strings::arrRec::undoSwitchTake);
    if (! arrangement::switchTake (model, clip, number))
        return false;
    message (Strings::arrRec::takeSelected (number));
    return true;
}

//==============================================================================
// Edits of the gestures
void ArrangementView::commitMove (const std::vector<Dragged>& clips, double deltaBeats, int deltaTracks, bool copy)
{
    if (clips.empty() || (std::abs (deltaBeats) < kEpsilon && deltaTracks == 0))
        return;
    std::vector<arrangement::Move> moves;
    for (const auto& d : clips)
    {
        const auto clip = arrangement::findClip (model, d.clipId);
        const int row = rowIndexOfTrack (d.trackId) + deltaTracks;
        if (! clip.isValid() || row < 0 || row >= (int) layoutRows.size())
            continue;
        moves.push_back ({ clip, model.findTrack (layoutRows[(size_t) row].trackId), juce::jmax (0.0, d.start + deltaBeats) });
    }
    if (moves.empty())
        return;
    model.undo.beginNewTransaction (copy ? Strings::undoArrCopyClips : Strings::undoArrMoveClips);
    if (copy)
    {
        std::vector<int64_t> made;
        for (const auto& m : moves)
            if (const auto c = arrangement::copyClip (model, m.clip, m.track, m.start); c.isValid())
                made.push_back (c.id());
        selectClips (made);
    }
    else
        arrangement::moveClips (model, moves);
    repaintTimeParts();
}

void ArrangementView::commitResizeRight (int64_t aclipId, double newEnd)
{
    const auto clip = arrangement::findClip (model, aclipId);
    if (! clip.isValid() || std::abs (newEnd - clip.end()) < kEpsilon)
        return;
    model.undo.beginNewTransaction (Strings::undoArrResizeClip);
    arrangement::resizeRight (model, clip, newEnd);
}

void ArrangementView::commitResizeLeft (int64_t aclipId, double newStart)
{
    const auto clip = arrangement::findClip (model, aclipId);
    if (! clip.isValid() || std::abs (newStart - clip.start()) < kEpsilon)
        return;
    model.undo.beginNewTransaction (Strings::undoArrResizeClip);
    arrangement::resizeLeft (model, clip, newStart);
}

int64_t ArrangementView::drawClipAt (int64_t trackId, double beat)
{
    const auto track = model.findTrack (trackId);
    if (! track.isValid())
        return -1;
    const double bar = beatsPerBar();
    const double start = std::floor (beat / bar + kEpsilon) * bar;
    model.undo.beginNewTransaction (Strings::undoArrNewClip);
    const auto clip = arrangement::addEmptyClip (model, track, start, bar);
    if (! clip.isValid())
        return -1;
    selectClip (clip.id(), false);
    return clip.id();
}

void ArrangementView::setLoopBrace (double start, double end, bool on, bool newTransaction)
{
    if (newTransaction)
        model.undo.beginNewTransaction (Strings::undoArrLoop);
    model.setArrangementLoop (start, end, on);
    rulerPart->repaint();
    lanesPart->repaint();
}

void ArrangementView::moveMarkerTo (int64_t markerId, double beat)
{
    for (const auto& mk : model.markers())
        if (mk.id() == markerId)
        {
            arrangement::moveMarker (model, mk, beat);
            break;
        }
    rulerPart->repaint();
}

//==============================================================================
// Keyboard
bool ArrangementView::isLetterKey (const juce::KeyPress& key, char letter) const
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    if (lettersReserved && lettersReserved())
        return false;
    return codeOf (key) == (int) letter;
}

bool ArrangementView::handleKey (const juce::KeyPress& key)
{
    if (textFieldHasFocus())
        return false;
    const auto mods = key.getModifiers();
    const bool cmd = mods.isCommandDown(), shift = mods.isShiftDown(), alt = mods.isAltDown();
    const int code = codeOf (key);

    // transport
    if (key.isKeyCode (juce::KeyPress::spaceKey) && ! cmd && ! alt)
    {
        if (engine.isPlaying())
            engine.stop();
        else if (shift)
            engine.playFromPosition();
        else
            engine.play();
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::homeKey))
    {
        playHome();
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::endKey))
    {
        playEnd();
        return true;
    }
    if ((key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey)) && ! cmd && ! alt)
        return nudgeSelection (key.isKeyCode (juce::KeyPress::rightKey) ? 1 : -1, shift);

    // delete
    if (key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey))
    {
        if (cmd && shift)
            return removeTimeAtSelection();
        return ! cmd && ! alt && deleteSelection();
    }
    if (key.isKeyCode (juce::KeyPress::escapeKey) && ! cmd)
    {
        const bool any = ! selectedAClips.empty() || ! timeSel.isEmpty() || markerSel >= 0;
        clearSelection();
        return any;
    }

    if (cmd && ! alt)
    {
        switch (code)
        {
            case 'c': return copySelection (false);
            case 'x': return copySelection (true);
            case 'v': return pasteAtCursor();
            case 'd': return duplicateSelection();
            case 'e': return splitAtCursor();
            case 'j': return joinSelection();
            case 'l': return loopSelection();
            case 'm': return addMarkerAtCursor();
            case 'i': return insertTimeAtSelection();
            case 'r': return renameSelection();
            case 'a':
                selectAllClips();
                return true;
            case 'f':
                if (shift)
                {
                    toggleFollow();
                    return true;
                }
                return false;
            case '1': stepGrid (1); return true;
            case '2': stepGrid (-1); return true;
            case '3': toggleTriplet(); return true;
            case '4': toggleSnap(); return true;
            case '=':
            case '+':
                zoomBy (1.25, lanesWidth() * 0.5);
                return true;
            case '-':
                zoomBy (0.8, lanesWidth() * 0.5);
                return true;
            default: break;
        }
        return false;
    }

    // plain keys
    if (! cmd && ! alt && ! mods.isCtrlDown())
    {
        if (code == '=' || code == '+' || key.isKeyCode (juce::KeyPress::numberPadAdd))
        {
            zoomBy (1.25, lanesWidth() * 0.5);
            return true;
        }
        if (code == '-' || key.isKeyCode (juce::KeyPress::numberPadSubtract))
        {
            zoomBy (0.8, lanesWidth() * 0.5);
            return true;
        }
    }
    if (isLetterKey (key, 'h'))
    {
        toggleHeightMode();
        return true;
    }
    if (isLetterKey (key, 'z'))
    {
        zoomToSelection();
        return true;
    }
    if (isLetterKey (key, 'x'))
    {
        zoomBack();
        return true;
    }
    return false;
}

//==============================================================================
juce::PopupMenu ArrangementView::markersMenu()
{
    juce::PopupMenu menu;
    const auto markers = model.markers();
    if (markers.empty())
        menu.addItem (Strings::arrangement::noMarkers, false, false, nullptr);
    for (const auto& mk : markers)
    {
        const double beat = mk.beat();
        const auto name = mk.name();
        menu.addItem (name + "   " + formatPosition (beat, beatsPerBar()), [this, beat] {
            setCursor (beat);
            scrollIntoView (beat);
        });
    }
    return menu;
}

//==============================================================================
// Session View -> Arrangement
juce::String ArrangementView::sessionClipDescription (int64_t trackId, int scene)
{
    return "sessionclip:" + juce::String (trackId) + ":" + juce::String (scene);
}

bool ArrangementView::parseSessionClip (const juce::var& description, int64_t& trackId, int& scene)
{
    const auto text = description.toString();
    if (! text.startsWith ("sessionclip:"))
        return false;
    juce::StringArray parts;
    parts.addTokens (text, ":", "");
    if (parts.size() != 3)
        return false;
    trackId = parts[1].getLargeIntValue();
    scene = parts[2].getIntValue();
    return true;
}

bool ArrangementView::isInterestedInDragSource (const SourceDetails& d)
{
    int64_t track = 0;
    int scene = 0;
    if (parseSessionClip (d.description, track, scene))
        return true;
    browser::DragItem item;
    return dropActions != nullptr && browser::parseDrag (d.description, item) && item.kind == browser::kinds::audioFile;
}

void ArrangementView::setDropHint (const DropHint& h)
{
    if (h == drop)
        return;
    drop = h;
    lanesPart->repaint();
}

ArrangementView::DropHint ArrangementView::soundDropHintAt (juce::Point<int> viewPoint) const
{
    DropHint hint;
    const auto p = lanesPart->getLocalPoint (this, viewPoint);
    if (dropActions == nullptr || ! lanesPart->getLocalBounds().contains (p))
        return hint;
    const double beat = xToBeat ((double) p.x);
    hint.audio = true;
    hint.beat = v.snap ? snapFloor (beat) : juce::jmax (0.0, beat);
    const int row = rowAtY (p.y);
    if (row >= 0)
    {
        hint.row = row;
        hint.valid = model.findTrack (layoutRows[(size_t) row].trackId).isAudio(); // a MIDI lane does not light up
    }
    else
    {
        hint.newTracks = true;
        hint.valid = true;
    }
    return hint;
}

void ArrangementView::itemDragMove (const SourceDetails& d)
{
    int64_t trackId = 0;
    int scene = 0;
    browser::DragItem item;
    if (! parseSessionClip (d.description, trackId, scene))
    {
        if (dropActions != nullptr && browser::parseDrag (d.description, item) && item.kind == browser::kinds::audioFile)
            setDropHint (soundDropHintAt (d.localPosition));
        return;
    }
    const auto source = model.findTrack (trackId).clipAt (scene);
    const auto p = lanesPart->getLocalPoint (this, d.localPosition);
    DropHint hint;
    const int row = rowAtY (p.y);
    if (source.isValid() && row >= 0 && lanesPart->getLocalBounds().contains (p))
    {
        hint.valid = true;
        hint.row = row;
        hint.beat = snapFloor (xToBeat ((double) p.x));
        hint.length = source.lengthBeats();
    }
    setDropHint (hint);
}

browser::DropResult ArrangementView::dropSoundFiles (const juce::Array<juce::File>& files, juce::Point<int> viewPoint, bool alternate)
{
    browser::DropResult result;
    const auto hint = soundDropHintAt (viewPoint);
    if (dropActions == nullptr || files.isEmpty() || ! hint.audio)
        return result;
    if (! hint.valid) // a MIDI lane
    {
        result.message = Strings::errorAudioOnMidi;
        return result;
    }
    result = dropActions->dropAudioOnTimeline (files, hint.newTracks ? -1 : layoutRows[(size_t) hint.row].trackId, hint.beat, alternate);
    if (result.ok && result.trackId >= 0)
        selectionState.selectTrack (result.trackId);
    return result;
}

void ArrangementView::itemDropped (const SourceDetails& d)
{
    int64_t trackId = 0;
    int scene = 0;
    browser::DragItem item;
    if (! parseSessionClip (d.description, trackId, scene))
    {
        setDropHint ({});
        if (dropActions != nullptr && browser::parseDrag (d.description, item) && item.kind == browser::kinds::audioFile)
        {
            const auto result = dropSoundFiles ({ juce::File (item.path) }, d.localPosition, juce::ModifierKeys::currentModifiers.isCommandDown());
            if (! result.ok && result.message.isNotEmpty())
                message (result.message);
        }
        return;
    }
    const auto hint = drop;
    setDropHint ({});
    if (! hint.valid || hint.row >= (int) layoutRows.size())
        return;
    const auto source = model.findTrack (trackId).clipAt (scene);
    const auto track = model.findTrack (layoutRows[(size_t) hint.row].trackId);
    if (! source.isValid() || ! track.isValid())
        return;
    model.undo.beginNewTransaction (Strings::undoArrNewClip);
    if (const auto clip = arrangement::addSlotClip (model, track, source, hint.beat); clip.isValid())
        selectClip (clip.id(), false);
}

bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (dropActions == nullptr)
        return false;
    for (const auto& f : files)
        if (audioimport::isSoundFile (juce::File (f)))
            return true;
    return files.isEmpty(); // some systems ask before they know the files
}

void ArrangementView::fileDragMove (const juce::StringArray&, int x, int y)
{
    setDropHint (soundDropHintAt ({ x, y }));
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    setDropHint ({});
    juce::Array<juce::File> sounds;
    for (const auto& f : files)
        if (audioimport::isSoundFile (juce::File (f)))
            sounds.add (juce::File (f));
    if (sounds.isEmpty())
        return;
    const auto result = dropSoundFiles (sounds, { x, y }, juce::ModifierKeys::currentModifiers.isCommandDown());
    if (! result.ok && result.message.isNotEmpty())
        message (result.message);
}

//==============================================================================
void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (theme().background);
}

void ArrangementView::resized()
{
    auto r = getLocalBounds();
    minimapPart->setBounds (r.removeFromBottom (kMinimapHeight));
    auto top = r.removeFromTop (kRulerHeight);
    cornerPart->setBounds (top.removeFromLeft (kHeaderWidth));
    rulerPart->setBounds (top.withTrimmedRight (kScrollBar));
    headersPart->setBounds (r.removeFromLeft (kHeaderWidth));
    vbar.setBounds (r.removeFromRight (kScrollBar));
    lanesPart->setBounds (r);
    playheadPart->setBounds (kHeaderWidth, 0, lanesWidth(), kRulerHeight + lanesHeight());
    playheadPart->toFront (false);
    if (autoZoom && lanesWidth() > 1)
        fitToSong();
    clampView();
    headersPart->refreshRows();
    updateScrollBar();
}

juce::String ArrangementView::helpAt (juce::Point<int>) const
{
    return {};
}
} // namespace ui
