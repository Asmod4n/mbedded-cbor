#pragma once

#include <cbor/error.hpp>

#include <expected>

namespace test
{

// Windows gives the main thread 1 MiB of stack, and the frames of a debug build at the hard bound of 512 levels need
// more; the stack bound of the library then stops the read with nesting_depth_exceeded before the stack ends. The
// stack size stays as the platform gives it (owner, 2026-11-07), so there a read at the bound gives its value or that
// error, and every other build reads the bound.
#if defined(_WIN32) && !defined(NDEBUG)
inline constexpr bool stack_holds_bound = false;
#else
inline constexpr bool stack_holds_bound = true;
#endif

template <class R>
bool reads_bound(R const &r)
{
    return r.has_value() || (!stack_holds_bound && r.error() == cbor::error::nesting_depth_exceeded);
}

} // namespace test
