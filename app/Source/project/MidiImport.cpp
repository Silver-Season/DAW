#include "MidiImport.h"

#include "../Strings.h"

namespace project
{
bool importMidiStream (juce::InputStream& stream, const juce::String& name, double beatsPerBar, ImportedClip& result, juce::String& error)
{
    juce::MidiFile file;
    if (! file.readFrom (stream))
    {
        error = Strings::errorMidiRead (name);
        return false;
    }
    const int ticksPerQuarter = file.getTimeFormat();
    if (ticksPerQuarter <= 0)
    {
        error = Strings::errorMidiTiming (name);
        return false;
    }

    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        const auto* track = file.getTrack (t);
        std::vector<daw::engine::Note> notes;
        struct Open
        {
            int channel, pitch;
            double start;
            int velocity;
        };
        std::vector<Open> open;
        double lastTick = 0.0;

        const auto close = [&] (int channel, int pitch, double tick) {
            for (size_t i = 0; i < open.size(); ++i)
                if (open[i].channel == channel && open[i].pitch == pitch)
                {
                    notes.push_back ({ open[i].start / ticksPerQuarter, juce::jmax (0.05, (tick - open[i].start) / ticksPerQuarter), pitch, open[i].velocity });
                    open.erase (open.begin() + (std::ptrdiff_t) i);
                    return;
                }
        };
        for (const auto* e : *track)
        {
            const auto& m = e->message;
            const double tick = m.getTimeStamp();
            lastTick = juce::jmax (lastTick, tick);
            if (m.isNoteOn())
                open.push_back ({ m.getChannel(), m.getNoteNumber(), tick, (int) m.getVelocity() });
            else if (m.isNoteOff())
                close (m.getChannel(), m.getNoteNumber(), tick);
        }
        for (const auto& o : open) // notes that never ended last until the end of the track
            notes.push_back ({ o.start / ticksPerQuarter, juce::jmax (0.05, (lastTick - o.start) / ticksPerQuarter), o.pitch, o.velocity });

        if (notes.empty())
            continue;

        double end = 0.0;
        for (const auto& n : notes)
            end = juce::jmax (end, n.start + n.length);
        const double bar = juce::jmax (1.0, beatsPerBar);
        result.name = name;
        result.notes = std::move (notes);
        result.lengthBeats = juce::jmax (bar, std::ceil (end / bar - 1e-9) * bar);
        return true;
    }

    error = Strings::errorMidiNoNotes (name);
    return false;
}

bool importMidiFile (const juce::File& file, double beatsPerBar, ImportedClip& result, juce::String& error)
{
    juce::FileInputStream stream (file);
    if (! stream.openedOk())
    {
        error = Strings::errorFileMissing (file.getFullPathName());
        return false;
    }
    return importMidiStream (stream, file.getFileNameWithoutExtension(), beatsPerBar, result, error);
}
} // namespace project
