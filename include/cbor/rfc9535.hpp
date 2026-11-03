// Gives cbor::key, a map key for a lookup, and cbor::fixed_string, a
// path as a template argument. Declares the checks of a valid path and
// of a singular query, and the largest exact integer of JSONPath.
// https://www.rfc-editor.org/rfc/rfc9535
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#ifdef __cpp_impl_reflection
#include <meta>
#endif

namespace cbor
{

/// A text or integer map key for lookup.
struct key {
    /// Null-terminated text key when non-null; the characters must remain valid.
    char const *text = nullptr;
    /// Integer key when text is null.
    std::int64_t number = 0;

#ifdef __cpp_impl_reflection
    template <std::same_as<char> C>
    consteval explicit key(C const *const s) : text(std::define_static_string(std::string_view(s)))
    {
    }
#endif

    /// Creates an integer map key.
    constexpr explicit key(std::int64_t const n) : number(n)
    {
    }
};

/// Stores a character array as a compile-time template argument.
template <std::size_t N>
struct fixed_string {
    std::array<char, N> value;

    /// Copies the array into the compile-time value.
    /// @par Complexity
    /// O(N) at compile time; no run-time operation.
    consteval fixed_string(char const (&text)[N])
    {
        std::ranges::copy(text, value.begin());
    }

    /// Returns the stored characters except the final array element.
    /// @par Complexity
    /// O(1) at compile time; no run-time operation.
    consteval std::string_view view() const
    {
        return std::string_view(std::span(value).first(N - 1));
    }
};

template <fixed_string Path>
class is_valid_path;

/// True when Path is a supported JSONPath within the nesting limit.
/// The grammar includes EDN literals; match and search are not supported.
template <fixed_string Path>
inline constexpr bool is_valid_path_v = is_valid_path<Path>::value;

template <fixed_string Path>
class is_singular_query;

/// True when Path is a valid singular query. Each segment selects one
/// name or index, and no segment is a descendant segment.
template <fixed_string Path>
inline constexpr bool is_singular_query_v = is_singular_query<Path>::value;

class rfc9535
{
    static constexpr std::int64_t exact_integer_max =
        (std::int64_t{1} << std::numeric_limits<double>::digits) - 1;

    friend class jsonpath;
};

}
