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
#include <string_view>
#ifdef __cpp_impl_reflection
#include <meta>
#endif

namespace cbor
{

struct key {
    char const *text = nullptr;
    std::int64_t number = 0;

#ifdef __cpp_impl_reflection
    template <std::same_as<char> C>
    consteval explicit key(C const *const s) : text(std::define_static_string(std::string_view(s)))
    {
    }
#endif

    constexpr explicit key(std::int64_t const n) : number(n)
    {
    }
};

template <std::size_t N>
struct fixed_string {
    std::array<char, N> value;

    consteval fixed_string(char const (&text)[N])
    {
        std::ranges::copy(text, value.begin());
    }

    consteval std::string_view view() const
    {
        return {value.data(), N - 1};
    }
};

template <fixed_string Path>
class is_valid_path;

template <fixed_string Path>
inline constexpr bool is_valid_path_v = is_valid_path<Path>::value;

template <fixed_string Path>
class is_singular_query;

template <fixed_string Path>
inline constexpr bool is_singular_query_v = is_singular_query<Path>::value;

class rfc9535
{
    static constexpr std::int64_t exact_integer_max =
        (std::int64_t{1} << std::numeric_limits<double>::digits) - 1;

    friend class jsonpath;
};

}
