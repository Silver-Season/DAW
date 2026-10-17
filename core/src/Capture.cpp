#include "daw/engine/Capture.h"

#include "daw/engine/Recording.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

//==============================================================================
void CaptureBuffer::push (const InputEvent& e)
{
    if (count == kCapacity)
    {
        head = (head + 1) % kCapacity;
        --count;
    }
    storage[(head + count) % kCapacity] = e;
    ++count;
}

void CaptureBuffer::evictOlderThan (int64_t engineSample)
{
    while (count > 0 && storage[head].engineSample < engineSample)
    {
        head = (head + 1) % kCapacity;
        --count;
    }
}

std::vector<InputEvent> CaptureBuffer::snapshot() const
{
    std::vector<InputEvent> out;
    out.reserve (count);
    for (size_t i = 0; i < count; ++i)
        out.push_back ((*this)[i]);
    return out;
}

//==============================================================================
namespace
{
constexpr double kTolerance = 0.030; // seconds

// 1 at a grid line, 0 at kTolerance away from it.
double gridScore (double interval, double grid)
{
    const double k = std::round (interval / grid);
    if (k < 1.0)
        return 0.0;
    return std::max (0.0, 1.0 - std::abs (interval - k * grid) / kTolerance);
}
} // namespace

TempoEstimate estimateTempo (const std::vector<double>& onsetsIn, double currentBpm)
{
    TempoEstimate result;
    result.bpm = currentBpm;

    // Notes of a chord land within a few milliseconds of each other: they count once.
    std::vector<double> t;
    t.reserve (onsetsIn.size());
    for (const double v : onsetsIn)
        if (t.empty() || v - t.back() > kTolerance)
            t.push_back (v);
    if (t.size() < 5)
        return result;

    std::vector<double> intervals;
    intervals.reserve (t.size() * 2);
    for (size_t i = 1; i < t.size(); ++i)
        intervals.push_back (t[i] - t[i - 1]);
    for (size_t i = 2; i < t.size(); ++i)
        intervals.push_back (t[i] - t[i - 2]);

    constexpr double kMinBpm = 60.0, kMaxBpm = 200.0, kStep = 0.25;
    struct Candidate
    {
        double bpm, score;
    };
    std::vector<Candidate> candidates;
    double best = 0.0;
    for (double bpm = kMinBpm; bpm <= kMaxBpm + 1.0e-9; bpm += kStep)
    {
        const double grid = 15.0 / bpm; // one sixteenth in seconds
        double score = 0.0;
        for (const double d : intervals)
            score += gridScore (d, grid);
        score /= (double) intervals.size();
        candidates.push_back ({ bpm, score });
        best = std::max (best, score);
    }

    // Several tempi may explain the playing about equally well (a half or double tempo, say). Among the real peaks of the
    // score curve that come close to the best one, the tempo nearest to the project's wins.
    const auto isPeak = [&] (size_t index) {
        for (size_t k = 0; k < candidates.size(); ++k)
            if (std::abs (candidates[k].bpm - candidates[index].bpm) <= candidates[index].bpm * 0.02 && candidates[k].score > candidates[index].score)
                return false;
        return true;
    };
    double chosen = currentBpm;
    double chosenDistance = 1.0e9;
    for (size_t k = 0; k < candidates.size(); ++k)
        if (candidates[k].score >= best * 0.98 && isPeak (k))
        {
            const double distance = std::abs (std::log2 (candidates[k].bpm / std::max (currentBpm, 1.0)));
            if (distance < chosenDistance)
            {
                chosenDistance = distance;
                chosen = candidates[k].bpm;
            }
        }

    // Refine: put every onset on its grid line and fit time = t0 + k * grid by least squares. A better grid moves
    // the lines, so the fit is repeated with it.
    double grid = 15.0 / chosen;
    size_t onGrid = 0;
    for (int pass = 0; pass < 4; ++pass)
    {
        double sumK = 0.0, sumT = 0.0;
        std::vector<std::pair<double, double>> points;
        for (const double v : t)
        {
            const double k = std::round ((v - t.front()) / grid);
            if (std::abs (v - t.front() - k * grid) <= kTolerance)
            {
                points.emplace_back (k, v);
                sumK += k;
                sumT += v;
            }
        }
        onGrid = points.size();
        if (points.size() < 5)
            return result;
        const double meanK = sumK / (double) points.size(), meanT = sumT / (double) points.size();
        double num = 0.0, den = 0.0;
        for (const auto& [k, v] : points)
        {
            num += (k - meanK) * (v - meanT);
            den += (k - meanK) * (k - meanK);
        }
        if (den < 1.0e-9 || num <= 0.0)
            return result;
        grid = num / den;
    }

    // Playing that does not sit on any grid is not a tempo: most onsets must land on the lines and the fit must be good.
    const double bpm = 15.0 / grid;
    if (best < 0.6 || (double) onGrid < 0.8 * (double) t.size() || bpm < kMinBpm - 2.0 || bpm > kMaxBpm + 2.0)
        return result;
    result.bpm = bpm;
    result.confident = true;
    return result;
}

//==============================================================================
namespace
{
int powerOfTwoAtLeast (int n)
{
    int p = 1;
    while (p < n && p < 32)
        p *= 2;
    return p;
}

// How alike the notes of two windows are: the share of notes of one that have a note of the same pitch within a
// sixteenth in the other. `a` and `b` hold notes already moved to the start of their window.
double similarity (const std::vector<Note>& a, const std::vector<Note>& b)
{
    if (a.empty() || b.empty())
        return 0.0;
    std::vector<bool> used (b.size(), false);
    int matches = 0;
    for (const auto& n : a)
        for (size_t j = 0; j < b.size(); ++j)
            if (! used[j] && b[j].pitch == n.pitch && std::abs (b[j].start - n.start) <= 0.0625)
            {
                used[j] = true;
                ++matches;
                break;
            }
    return (double) matches / (double) std::max (a.size(), b.size());
}

std::vector<Note> notesInWindow (const std::vector<Note>& notes, double from, double to)
{
    std::vector<Note> out;
    for (const auto& n : notes)
        if (n.start >= from - 1.0e-9 && n.start < to - 1.0e-9)
        {
            Note moved = n;
            moved.start -= from;
            out.push_back (moved);
        }
    return out;
}
} // namespace

CaptureResult capturePhrase (const std::vector<InputEvent>& events, int64_t targetTrackId, int64_t nowEngineSample, double sampleRate,
                             double beatsPerBar, double projectBpm)
{
    CaptureResult result;
    result.bpm = projectBpm;
    if (sampleRate <= 0.0 || beatsPerBar <= 0.0)
        return result;

    // 1. the events of the target track; if none ever went there, those that went nowhere
    bool anyOnTarget = false;
    for (const auto& e : events)
        if (e.trackId == targetTrackId && e.engineSample <= nowEngineSample)
        {
            anyOnTarget = true;
            break;
        }
    std::vector<const InputEvent*> mine;
    for (const auto& e : events)
        if (e.engineSample <= nowEngineSample && (anyOnTarget ? e.trackId == targetTrackId : e.trackId == 0))
            mine.push_back (&e);

    std::vector<size_t> onsets; // indices into `mine`
    for (size_t i = 0; i < mine.size(); ++i)
        if (mine[i]->isNoteOn())
            onsets.push_back (i);
    if (onsets.empty())
        return result;

    // 2. the last phrase: walk back while the gap between note-ons stays short
    size_t firstOnset = onsets.size() - 1;
    while (firstOnset > 0)
    {
        const auto& cur = *mine[onsets[firstOnset]];
        const auto& prev = *mine[onsets[firstOnset - 1]];
        const double gapSeconds = (double) (cur.engineSample - prev.engineSample) / sampleRate;
        const bool bothPlaying = cur.playing && prev.playing;
        if (gapSeconds < 4.0 || (bothPlaying && cur.beat - prev.beat < 2.0 * beatsPerBar))
            --firstOnset;
        else
            break;
    }
    const size_t phraseBegin = onsets[firstOnset];
    const auto& firstEvent = *mine[phraseBegin];
    const auto& lastOnsetEvent = *mine[onsets.back()];
    const bool playing = firstEvent.playing && lastOnsetEvent.playing;
    result.transportWasPlaying = playing;

    // 3. / 4. the notes, in beats from a bar line (playing) or from the first note (stopped)
    NoteAssembler assembler;
    double endTime = 0.0;
    if (playing)
    {
        const double originBeat = std::floor (firstEvent.beat / beatsPerBar + 1.0e-9) * beatsPerBar;
        for (size_t i = phraseBegin; i < mine.size(); ++i)
        {
            const auto& e = *mine[i];
            if (! e.playing)
                continue;
            const double time = e.beat - originBeat;
            assembler.feed (time, e.bytes, e.size);
            endTime = std::max (endTime, time);
        }
    }
    else
    {
        std::vector<double> seconds;
        for (size_t k = firstOnset; k < onsets.size(); ++k)
            seconds.push_back ((double) mine[onsets[k]]->engineSample / sampleRate);
        const auto estimate = estimateTempo (seconds, projectBpm);
        result.bpm = estimate.confident ? estimate.bpm : projectBpm;
        result.tempoFromPhrase = estimate.confident;
        for (size_t i = phraseBegin; i < mine.size(); ++i)
        {
            const auto& e = *mine[i];
            const double time = (double) (e.engineSample - firstEvent.engineSample) / sampleRate * result.bpm / 60.0;
            assembler.feed (time, e.bytes, e.size);
            endTime = std::max (endTime, time);
        }
    }
    assembler.closeAll (endTime);
    auto notes = assembler.takeNotes();
    auto controllers = assembler.takeControllers();
    if (notes.empty())
        return result;
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });

    double phraseEnd = 0.0;
    for (const auto& n : notes)
        phraseEnd = std::max (phraseEnd, n.start + n.length);
    // a note that runs a sixteenth over the bar line does not cost another bar
    const int phraseBars = std::max (1, (int) std::ceil ((phraseEnd - 0.0625) / beatsPerBar - 1.0e-9));

    // Has the last stretch been played twice? The longest repeating unit wins.
    int repeatBars = 0;
    for (int n = phraseBars / 2; n >= 1; --n)
    {
        const double lastFrom = (double) (phraseBars - n) * beatsPerBar, prevFrom = (double) (phraseBars - 2 * n) * beatsPerBar;
        const auto last = notesInWindow (notes, lastFrom, lastFrom + n * beatsPerBar);
        const auto prev = notesInWindow (notes, prevFrom, prevFrom + n * beatsPerBar);
        if (similarity (last, prev) > 0.8)
        {
            repeatBars = n;
            break;
        }
    }

    int lengthBars;
    double fromBeat;
    if (repeatBars > 0)
    {
        lengthBars = powerOfTwoAtLeast (repeatBars);
        fromBeat = (double) (phraseBars - repeatBars) * beatsPerBar;
    }
    else
    {
        lengthBars = powerOfTwoAtLeast (phraseBars);
        fromBeat = phraseBars > lengthBars ? (double) (phraseBars - lengthBars) * beatsPerBar : 0.0; // longer than 32 bars: the last 32
    }
    const double length = (double) lengthBars * beatsPerBar;

    for (auto n : notes)
        if (n.start >= fromBeat - 1.0e-9 && n.start < fromBeat + length - 1.0e-9)
        {
            n.start = std::max (0.0, n.start - fromBeat);
            n.length = std::min (n.length, length - n.start);
            result.notes.push_back (n);
        }
    for (auto c : controllers)
        if (c.time >= fromBeat - 1.0e-9 && c.time < fromBeat + length - 1.0e-9)
        {
            c.time = std::max (0.0, c.time - fromBeat);
            result.controllers.push_back (c);
        }
    result.lengthBeats = length;
    result.numBars = lengthBars;
    result.numNotes = (int) result.notes.size();
    result.ok = result.numNotes > 0;
    return result;
}

} // namespace daw::engine
