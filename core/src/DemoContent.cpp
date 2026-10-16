#include "daw/engine/DemoContent.h"

namespace daw::engine::demo
{

namespace
{
// A minor: Am F C G, one bar each.
const int kChords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
const int kRoots[4] = { 33, 29, 36, 31 };

} // namespace

MidiClip padClip()
{
    MidiClip c { "Pad", 0, 16, {} };
    for (int bar = 0; bar < 4; ++bar)
        for (int n : kChords[bar])
            c.notes.push_back ({ bar * 4.0, 3.9, n, 80 });
    return c;
}

MidiClip arpClip()
{
    MidiClip c { "Arp", 0, 16, {} };
    for (int bar = 0; bar < 4; ++bar)
        for (int step = 0; step < 16; ++step)
        {
            const int pattern[] = { 0, 1, 2, 1 };
            const int note = kChords[bar][pattern[step % 4]] + 12 + (step >= 8 ? 12 : 0);
            c.notes.push_back ({ bar * 4.0 + step * 0.25, 0.2, note, 70 + (step % 4 == 0 ? 30 : 0) });
        }
    return c;
}

MidiClip bassClip (bool sustained)
{
    MidiClip c { "Bass", 0, 16, {} };
    for (int bar = 0; bar < 4; ++bar)
    {
        if (sustained)
            c.notes.push_back ({ bar * 4.0, 3.8, kRoots[bar], 110 });
        else
            for (int e = 0; e < 8; ++e)
                c.notes.push_back ({ bar * 4.0 + e * 0.5, 0.4, kRoots[bar] + (e == 7 ? 12 : 0), 100 });
    }
    return c;
}

MidiClip leadClip()
{
    MidiClip c { "Lead", 0, 16, {} };
    const struct
    {
        double start, len;
        int note;
    } melody[] = { { 0, 1.5, 76 }, { 1.5, 0.5, 74 }, { 2, 2, 72 }, { 4, 1.5, 72 }, { 5.5, 0.5, 74 }, { 6, 2, 76 },
                   { 8, 1, 79 }, { 9, 1, 76 }, { 10, 2, 74 }, { 12, 1.5, 74 }, { 13.5, 0.5, 72 }, { 14, 2, 71 } };
    for (auto& m : melody)
        c.notes.push_back ({ m.start, m.len - 0.05, m.note, 105 });
    return c;
}

} // namespace daw::engine::demo
