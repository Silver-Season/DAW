#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Lock-free queue for live MIDI: any number of producer threads (MIDI device callbacks, the message
// thread for the on-screen keyboard), one consumer (the audio thread). Bounded ring of POD cells, no
// allocation, no locks (Vyukov's bounded queue). A full queue drops the new event.
class MidiInputQueue
{
public:
    enum Source : uint8_t
    {
        hardware = 0,
        keyboard = 1
    };

    struct Event
    {
        uint8_t bytes[3] {};
        uint8_t size = 0;
        Source source = hardware;
        double timeMs = 0.0; // juce::Time::getMillisecondCounterHiRes() when the event happened
    };

    MidiInputQueue()
    {
        for (size_t i = 0; i < kCapacity; ++i)
            cells[i].sequence.store (i, std::memory_order_relaxed);
    }

    bool push (const Event& e) noexcept
    {
        size_t pos = enqueuePos.load (std::memory_order_relaxed);
        for (;;)
        {
            Cell& c = cells[pos & kMask];
            const size_t seq = c.sequence.load (std::memory_order_acquire);
            const auto diff = (std::intptr_t) seq - (std::intptr_t) pos;
            if (diff == 0)
            {
                if (enqueuePos.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed))
                {
                    c.event = e;
                    c.sequence.store (pos + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (diff < 0)
                return false; // full
            else
                pos = enqueuePos.load (std::memory_order_relaxed);
        }
    }

    // Consumer side only.
    bool pop (Event& out) noexcept
    {
        Cell& c = cells[dequeuePos & kMask];
        if (c.sequence.load (std::memory_order_acquire) != dequeuePos + 1)
            return false;
        out = c.event;
        c.sequence.store (dequeuePos + kCapacity, std::memory_order_release);
        ++dequeuePos;
        return true;
    }

private:
    static constexpr size_t kCapacity = 1024; // power of two
    static constexpr size_t kMask = kCapacity - 1;

    struct Cell
    {
        std::atomic<size_t> sequence { 0 };
        Event event;
    };

    std::array<Cell, kCapacity> cells;
    std::atomic<size_t> enqueuePos { 0 };
    size_t dequeuePos = 0;
};
