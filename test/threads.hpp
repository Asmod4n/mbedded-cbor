#pragma once

namespace test
{

// WASI preview 1 has no way to start a thread: std::thread throws there. The libc++ of wasi-sdk still says it has
// threads, so the test reads the target: a WASI build has threads only with -pthread, which defines _REENTRANT.
#if defined(__wasi__) && !defined(_REENTRANT)
inline constexpr bool threads_available = false;
#else
inline constexpr bool threads_available = true;
#endif

}
