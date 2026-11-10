#pragma once

#include <cstdint>

// The tag numbers of RFC 8746 that the tests count over.
namespace test::rfc8746
{

// 2.1: the typed arrays are the tags 64 to 87; 76 is reserved.
inline constexpr std::uint64_t first_typed_array_tag = 64;
inline constexpr std::uint64_t last_typed_array_tag = 87;
inline constexpr std::uint64_t reserved_typed_array_tag = 76;

}
