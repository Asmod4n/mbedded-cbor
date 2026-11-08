#pragma once

#include "../hex.hpp"
#include "../owned_copy.hpp"
#include "../reference/path.hpp"
#include "../reference/value.hpp"
#include <cbor/error.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace path_edn
{

// The library converts a decimal float with std::from_chars where the standard library has it for double: libstdc++
// says so with __cpp_lib_to_chars, libc++ from LLVM 20 with its availability macro, which on Apple depends on the
// deployment target.
#if defined(__cpp_lib_to_chars) || (defined(_LIBCPP_VERSION) && _LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT)
inline constexpr bool from_chars_reads_double = true;
#else
inline constexpr bool from_chars_reads_double = false;
#endif

// A map with one entry: the key given in hex, the value the text "found".
inline cbor::lazy keyed(std::string_view const key_hex)
{
    auto const l = test::lazy_of_copy("\xa1"s + test::unhexlify(key_hex) + "\x65" "found"s);
    REQUIRE(l.has_value());
    return *l;
}

// The literal, compiled at run time as the key of a name selector, selects the entry whose key has its value.
inline void selects(std::string_view const literal, std::string_view const key_hex)
{
    CAPTURE(literal);
    CAPTURE(key_hex);
    auto const v = test::at_path("$[" + std::string(literal) + "]", keyed(key_hex));
    REQUIRE(v.has_value());
    CHECK(*v == V("found"s));
}

inline void refused(std::string_view const literal)
{
    CAPTURE(literal);
    auto const q = cbor::path::compile<cbor::lazy>("$[" + std::string(literal) + "]");
    REQUIRE_FALSE(q.has_value());
    CHECK_EQ(q.error(), error::invalid_path);
}

} // namespace path_edn

using namespace path_edn;
