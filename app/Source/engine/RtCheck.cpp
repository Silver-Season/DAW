#include "RtCheck.h"

#if DAW_RT_CHECKS

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <new>

namespace daw::rt
{
thread_local bool inAudioThread = false;
std::atomic<int64_t> violations { 0 };

namespace
{
void report() noexcept
{
    violations.fetch_add (1, std::memory_order_relaxed);
    inAudioThread = false; // the assertion handler itself may allocate
    jassertfalse; // heap use on the audio thread
    inAudioThread = true;
}

void* allocate (std::size_t size) noexcept
{
    if (inAudioThread)
        report();
    return std::malloc (size == 0 ? 1 : size);
}

void* allocateAligned (std::size_t size, std::size_t alignment) noexcept
{
    if (inAudioThread)
        report();
    void* p = nullptr;
    if (alignment < sizeof (void*))
        alignment = sizeof (void*);
    return posix_memalign (&p, alignment, size == 0 ? 1 : size) == 0 ? p : nullptr;
}

void release (void* p) noexcept
{
    if (p == nullptr)
        return;
    if (inAudioThread)
        report();
    std::free (p);
}
} // namespace
} // namespace daw::rt

void* operator new (std::size_t size)
{
    if (void* p = daw::rt::allocate (size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size)
{
    if (void* p = daw::rt::allocate (size))
        return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t size, const std::nothrow_t&) noexcept { return daw::rt::allocate (size); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { return daw::rt::allocate (size); }
void* operator new (std::size_t size, std::align_val_t a)
{
    if (void* p = daw::rt::allocateAligned (size, (std::size_t) a))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size, std::align_val_t a)
{
    if (void* p = daw::rt::allocateAligned (size, (std::size_t) a))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { daw::rt::release (p); }
void operator delete[] (void* p) noexcept { daw::rt::release (p); }
void operator delete (void* p, std::size_t) noexcept { daw::rt::release (p); }
void operator delete[] (void* p, std::size_t) noexcept { daw::rt::release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { daw::rt::release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { daw::rt::release (p); }
void operator delete (void* p, std::align_val_t) noexcept { daw::rt::release (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { daw::rt::release (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { daw::rt::release (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { daw::rt::release (p); }

#endif
