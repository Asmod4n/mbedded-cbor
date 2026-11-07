#pragma once

#include "threads.hpp"

#include <doctest/doctest.h>

#include <cbor/error.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

#if defined(_WIN64)
#include <process.h>
#define NOMINMAX
#include <windows.h>
#elif __has_include(<pthread.h>)
#include <pthread.h>
#endif

namespace test
{

// A recursive reader stops with nesting_depth_exceeded where the stack of its thread ends. The tests run the
// deep reads on a thread with a small stack, because the stack of the main thread is as large as the platform
// gives it, and a test does not make it larger (owner, 2026-11-07). A test may make a stack smaller to test the
// bound.
inline constexpr std::size_t small_stack_bytes = 64 * 1024;

// Each level of a recursion takes at least one call frame, and a call frame takes at least 16 bytes on the listed
// targets (the alignment of the stack). An input with as many levels as the small stack has bytes is deeper than that
// stack holds for every reader.
inline constexpr std::size_t smallest_frame_bytes = 16;
inline constexpr std::size_t levels_beyond_small_stack = small_stack_bytes;

// A depth that the stack of the main thread holds for a decode in a debug build with a sanitizer on Linux and macOS,
// and that is deeper than the small stack holds for the frames of a release build. The main thread of a Windows debug
// build does not hold it: there decode stops with nesting_depth_exceeded.
inline constexpr std::size_t levels_main_thread_holds = 1024;

// A depth that the stack of every thread of every build holds: a read of it gives its value.
inline constexpr std::size_t levels_every_stack_holds = 32;

namespace small_stack_detail
{

#if defined(_WIN64)
// The flag of _beginthreadex that makes the size the reservation of the stack, not its first commit.
inline constexpr unsigned stack_size_param_is_a_reservation = 0x00010000;

inline unsigned __stdcall run(void *const work)
{
    (*static_cast<std::function<void()> *>(work))();
    return 0;
}
#elif __has_include(<pthread.h>)
inline void *run(void *const work)
{
    (*static_cast<std::function<void()> *>(work))();
    return nullptr;
}
#endif

} // namespace small_stack_detail

// Runs the work on a thread with a stack of the given size. std::thread takes no stack size; _beginthreadex and the
// attributes of POSIX threads do. A target without threads runs the work on the calling thread: there the stack of
// the program is the small stack (WebAssembly gives 64 KiB unless the link sets another).
inline void on_stack_of(std::size_t const bytes, std::function<void()> work)
{
    if constexpr (!threads_available) {
        static_cast<void>(bytes);
        work();
    } else {
#if defined(_WIN64)
        auto const thread = _beginthreadex(nullptr, static_cast<unsigned>(bytes), small_stack_detail::run, &work,
                                           small_stack_detail::stack_size_param_is_a_reservation, nullptr);
        REQUIRE(thread != 0);
        auto const handle = reinterpret_cast<HANDLE>(thread);
        REQUIRE_EQ(WaitForSingleObject(handle, INFINITE), WAIT_OBJECT_0);
        CloseHandle(handle);
#elif __has_include(<pthread.h>)
        pthread_attr_t attributes;
        REQUIRE_EQ(pthread_attr_init(&attributes), 0);
        REQUIRE_EQ(pthread_attr_setstacksize(&attributes, bytes), 0);
        pthread_t thread;
        REQUIRE_EQ(pthread_create(&thread, &attributes, small_stack_detail::run, &work), 0);
        REQUIRE_EQ(pthread_join(thread, nullptr), 0);
        pthread_attr_destroy(&attributes);
#else
        static_cast<void>(bytes);
        work();
#endif
    }
}

inline void on_small_stack(std::function<void()> work)
{
    on_stack_of(small_stack_bytes, std::move(work));
}

// Runs a read on the small stack and gives its error, or nothing when it gave a value. A value that the read gives
// ends inside the thread.
template <class Read>
std::optional<cbor::error> error_on_small_stack(Read const &read)
{
    std::optional<cbor::error> failure;
    on_small_stack([&read, &failure] {
        auto const r = read();
        if (!r)
            failure = r.error();
    });
    return failure;
}

} // namespace test
