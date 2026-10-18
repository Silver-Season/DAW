#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

// One recording of sound on its way from the audio thread to the disk (docs/spec/05-audio-recording.md, task 5.4).
//
// The audio thread is the only writer of the samples, the disk thread of AudioRecorder the only reader. Everything is
// allocated when the stream is made (2 seconds per channel), so writing never allocates. When the ring is full (the disk
// does not keep up) the frames that do not fit are counted as a gap: the reader writes silence there, so the file keeps
// its length and everything after the gap stays on its beat.
//
// The state is the handshake between the threads:
//   idle -> requested     message thread: a recording should begin with the next record start of the track
//   requested -> writing  audio thread: the start beat came; frame 0 of the file is engine sample `startSample`
//   writing -> tail       audio thread: the end beat came; `endSample` is where the file ends (the end + the latency)
//   tail -> done          audio thread: the last frame is in the ring, `totalFrames` holds the length
//   done -> idle          message thread, after the reader wrote everything (reset())
//   requested -> idle     message thread: cancelled before it began (compare-exchange, so a start cannot slip in)
class RecordStream
{
public:
    enum State : int
    {
        idle = 0,
        requested,
        writing,
        tail,
        done
    };

    // What decides when the recording begins and ends: the record state of the track (a slot) or the arrangement recording.
    enum class Mode : int
    {
        slot = 0,
        arrangement
    };

    RecordStream (int maxChannels, int capacityFrames);

    int getMaxChannels() const { return maxChannels; }
    int getCapacity() const { return capacity; }

    //==========================================================================
    // Message thread
    // Arms the stream for a recording with `channels` channels (1 or 2). False if it is busy.
    bool request (Mode, int channels);
    // Takes back a request that has not begun. False if the audio thread began writing already.
    bool cancel();
    // Back to idle after the reader took everything. Only when the state is done (or idle).
    void reset();

    //==========================================================================
    // Audio thread
    // Appends frames; what does not fit becomes a gap. `channels` holds getChannels() pointers.
    void write (const float* const* channels, int numFrames) noexcept;
    // Appends silence (the input is missing for a moment); counts as written frames.
    void writeSilence (int numFrames) noexcept;

    //==========================================================================
    // Reader (disk thread). Reads up to `maxFrames` frames into `dest` (getChannels() planes); a gap comes out as zeros.
    // Returns the number of frames read.
    int read (float* const* dest, int maxFrames) noexcept;
    // Frames handed out by read() so far (data and silence).
    int64_t getFramesRead() const noexcept { return framesRead; }

    //==========================================================================
    std::atomic<int> state { idle };
    std::atomic<int> mode { (int) Mode::slot };
    std::atomic<int> channelCount { 2 };
    std::atomic<int64_t> startSample { 0 }, endSample { 0 }; // engine samples
    std::atomic<int> latency { 0 };                         // frames the input lags behind the output, taken at the start
    std::atomic<int64_t> totalFrames { 0 };                 // the length of the file, set together with `done`
    std::atomic<int64_t> framesWritten { 0 };               // data and gaps, as the audio thread counts them
    std::atomic<int64_t> droppedFrames { 0 };               // frames lost to a full ring
    std::atomic<int64_t> firstDropFrame { -1 };             // where the first loss happened (for the warning)

    int getChannels() const noexcept { return channelCount.load (std::memory_order_relaxed); }

private:
    struct Gap
    {
        int64_t at = 0;    // frame of the file where the silence begins
        int64_t frames = 0;
    };

    bool flushPendingGap() noexcept;

    const int maxChannels, capacity;
    juce::HeapBlock<float> storage;
    std::array<float*, 2> planes {};
    juce::AbstractFifo fifo;

    static constexpr int kGapQueueSize = 64;
    juce::AbstractFifo gapFifo { kGapQueueSize };
    std::array<Gap, kGapQueueSize> gaps {};
    Gap pendingGap;          // audio thread: a loss not yet in the gap queue
    int64_t produced = 0;    // audio thread: frames of the file so far, gaps included

    // reader
    int64_t framesRead = 0;
    Gap currentGap;          // the gap the reader is in (frames left)
    bool haveGap = false;
    Gap nextGap;
};
