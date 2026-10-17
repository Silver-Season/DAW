#include "daw/engine/Recording.h"

#include <algorithm>

namespace daw::engine
{

bool NoteAssembler::isRecordedController (const uint8_t* b, int size) noexcept
{
    if (size < 2)
        return false;
    const int type = b[0] & 0xF0;
    if (type == 0xB0)
        return size >= 3 && (b[1] == 1 || b[1] == 64);
    return (type == 0xE0 && size >= 3) || type == 0xD0;
}

void NoteAssembler::noteOn (double time, int pitch, int velocity)
{
    open.push_back ({ time, pitch, std::clamp (velocity, 1, 127) });
}

void NoteAssembler::noteOff (double time, int pitch)
{
    for (size_t i = 0; i < open.size(); ++i)
        if (open[i].pitch == pitch)
        {
            Note n;
            n.start = open[i].start;
            n.length = std::max (time - open[i].start, kMinLength);
            n.pitch = pitch;
            n.velocity = open[i].velocity;
            finished.push_back (n);
            open.erase (open.begin() + (std::ptrdiff_t) i);
            return;
        }
    // a note-off for a note that began before the recording: ignored
}

void NoteAssembler::feed (double time, const uint8_t* b, int size)
{
    if (size < 1)
        return;
    const int type = b[0] & 0xF0;
    if (type == 0x90 && size >= 3)
    {
        if (b[2] > 0)
            noteOn (time, b[1], b[2]);
        else
            noteOff (time, b[1]);
    }
    else if (type == 0x80 && size >= 3)
        noteOff (time, b[1]);
    else if (isRecordedController (b, size))
    {
        ControllerEvent e;
        e.time = time;
        e.status = (uint8_t) (type == 0xB0 ? 0xB0 : type);
        e.data1 = b[1];
        e.data2 = size >= 3 ? b[2] : (uint8_t) 0;
        int slot = 0, value = 0;
        if (type == 0xB0)
        {
            slot = b[1] == 1 ? 0 : 1;
            value = b[2];
        }
        else if (type == 0xE0)
        {
            slot = 2;
            value = (b[2] << 7) | b[1];
        }
        else
        {
            slot = 3;
            value = b[1];
        }
        if (lastValue[slot] == value)
            return;
        lastValue[slot] = value;
        controllerEvents.push_back (e);
    }
}

void NoteAssembler::closeAll (double endTime)
{
    for (const auto& o : open)
    {
        Note n;
        n.start = o.start;
        n.length = std::max (endTime - o.start, kMinLength);
        n.pitch = o.pitch;
        n.velocity = o.velocity;
        finished.push_back (n);
    }
    open.clear();
}

void NoteAssembler::clear()
{
    open.clear();
    finished.clear();
    controllerEvents.clear();
    for (auto& v : lastValue)
        v = -1;
}

std::vector<Note> NoteAssembler::takeNotes()
{
    std::vector<Note> out;
    out.swap (finished);
    return out;
}

std::vector<ControllerEvent> NoteAssembler::takeControllers()
{
    std::vector<ControllerEvent> out;
    out.swap (controllerEvents);
    return out;
}

} // namespace daw::engine
