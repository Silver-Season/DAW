#pragma once

#include <atomic>
#include <cstdint>

// DAW_RT_CHECKS: tells the tests whether the audio thread touched the heap.
//
// With -DDAW_RT_CHECKS=ON (always on for the engine tests) RtCheck.cpp replaces the global operator
// new/delete. While a ScopedAudioThread is alive on a thread, every allocation or deallocation on that
// thread bumps a counter and trips jassertfalse (jassertfalse only fires in Debug builds, the counter
// works everywhere). Without the option everything here compiles to nothing.
namespace daw::rt
{
#if DAW_RT_CHECKS
extern thread_local bool inAudioThread;
extern std::atomic<int64_t> violations;

struct ScopedAudioThread
{
    ScopedAudioThread() noexcept : previous (inAudioThread) { inAudioThread = true; }
    ~ScopedAudioThread() noexcept { inAudioThread = previous; }
    bool previous;
};

inline int64_t violationCount() noexcept { return violations.load(); }
inline void resetViolations() noexcept { violations.store (0); }
constexpr bool enabled = true;
#else
struct ScopedAudioThread
{
    // not trivially empty on purpose: keeps "unused variable" warnings away
    ScopedAudioThread() noexcept {}
};
inline int64_t violationCount() noexcept { return 0; }
inline void resetViolations() noexcept {}
constexpr bool enabled = false;
#endif
} // namespace daw::rt
