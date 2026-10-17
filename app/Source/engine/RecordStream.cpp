#include "RecordStream.h"

#include <algorithm>
#include <cstring>

RecordStream::RecordStream (int maxCh, int capacityFrames)
    : maxChannels (juce::jlimit (1, 2, maxCh)), capacity (juce::jmax (1024, capacityFrames)), fifo (capacity + 1) // a fifo of n holds n - 1
{
    storage.calloc ((size_t) maxChannels * (size_t) (capacity + 1));
    for (int c = 0; c < 2; ++c)
        planes[(size_t) c] = storage.get() + (size_t) juce::jmin (c, maxChannels - 1) * (size_t) (capacity + 1);
}

bool RecordStream::request (Mode m, int channels)
{
    if (state.load() != idle)
        return false;
    // Nobody else touches the stream while it is idle: the audio thread ignores it, the reader has finished with it.
    fifo.reset();
    gapFifo.reset();
    pendingGap = {};
    produced = 0;
    framesRead = 0;
    haveGap = false;
    nextGap = {};
    startSample = 0;
    endSample = 0;
    totalFrames = 0;
    framesWritten = 0;
    droppedFrames = 0;
    firstDropFrame = -1;
    mode = (int) m;
    channelCount = juce::jlimit (1, maxChannels, channels);
    state.store (requested, std::memory_order_release);
    return true;
}

bool RecordStream::cancel()
{
    int expected = requested;
    return state.compare_exchange_strong (expected, idle);
}

void RecordStream::reset()
{
    int expected = done;
    state.compare_exchange_strong (expected, idle);
}

//==============================================================================
bool RecordStream::flushPendingGap() noexcept
{
    if (pendingGap.frames <= 0)
        return true;
    const auto scope = gapFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
        return false;
    gaps[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = pendingGap;
    pendingGap = {};
    return true;
}

void RecordStream::write (const float* const* channels, int numFrames) noexcept
{
    if (numFrames <= 0)
        return;
    const auto lose = [this] (int frames) {
        if (pendingGap.frames <= 0)
            pendingGap.at = produced;
        pendingGap.frames += frames;
        if (firstDropFrame.load (std::memory_order_relaxed) < 0)
            firstDropFrame.store (produced, std::memory_order_relaxed);
        produced += frames;
        droppedFrames.fetch_add (frames, std::memory_order_relaxed);
    };

    // A gap must reach the reader before any frame that follows it; if its queue is full, these frames are lost, too.
    if (! flushPendingGap())
    {
        lose (numFrames);
        framesWritten.store (produced, std::memory_order_release);
        return;
    }

    const int numCh = getChannels();
    const auto scope = fifo.write (numFrames);
    const auto copy = [&] (int start, int size, int sourceOffset) {
        for (int c = 0; c < numCh && size > 0; ++c)
            std::memcpy (planes[(size_t) c] + start, channels[c] + sourceOffset, sizeof (float) * (size_t) size);
    };
    copy (scope.startIndex1, scope.blockSize1, 0);
    copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    const int stored = scope.blockSize1 + scope.blockSize2;
    produced += stored;
    if (stored < numFrames)
    {
        lose (numFrames - stored);
        flushPendingGap();
    }
    framesWritten.store (produced, std::memory_order_release);
}

void RecordStream::writeSilence (int numFrames) noexcept
{
    if (numFrames <= 0)
        return;
    if (pendingGap.frames > 0 && pendingGap.at + pendingGap.frames == produced)
        pendingGap.frames += numFrames;
    else
    {
        flushPendingGap();
        if (pendingGap.frames <= 0)
            pendingGap.at = produced;
        pendingGap.frames += numFrames;
    }
    produced += numFrames;
    flushPendingGap();
    framesWritten.store (produced, std::memory_order_release);
}

//==============================================================================
int RecordStream::read (float* const* dest, int maxFrames) noexcept
{
    const int numCh = getChannels();
    int doneFrames = 0;
    while (doneFrames < maxFrames)
    {
        // The data count is read before the gap queue: data that follows a gap was written after the gap was queued, so
        // whatever the count includes, the gaps before it are visible below.
        const int ready = fifo.getNumReady();
        if (! haveGap)
        {
            const auto scope = gapFifo.read (gapFifo.getNumReady() > 0 ? 1 : 0);
            if (scope.blockSize1 + scope.blockSize2 > 0)
            {
                nextGap = gaps[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
                haveGap = true;
            }
        }
        if (haveGap && nextGap.at <= framesRead)
        {
            const int n = (int) std::min ((int64_t) (maxFrames - doneFrames), nextGap.frames);
            for (int c = 0; c < numCh; ++c)
                std::fill (dest[c] + doneFrames, dest[c] + doneFrames + n, 0.0f);
            nextGap.frames -= n;
            nextGap.at += n;
            framesRead += n;
            doneFrames += n;
            if (nextGap.frames <= 0)
                haveGap = false;
            continue;
        }
        int n = std::min (maxFrames - doneFrames, ready);
        if (haveGap)
            n = (int) std::min ((int64_t) n, nextGap.at - framesRead);
        if (n <= 0)
            break;
        const auto scope = fifo.read (n);
        for (int c = 0; c < numCh; ++c)
        {
            if (scope.blockSize1 > 0)
                std::memcpy (dest[c] + doneFrames, planes[(size_t) c] + scope.startIndex1, sizeof (float) * (size_t) scope.blockSize1);
            if (scope.blockSize2 > 0)
                std::memcpy (dest[c] + doneFrames + scope.blockSize1, planes[(size_t) c] + scope.startIndex2, sizeof (float) * (size_t) scope.blockSize2);
        }
        framesRead += n;
        doneFrames += n;
    }
    return doneFrames;
}
