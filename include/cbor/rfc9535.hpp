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

class rfc9535
{
    static constexpr std::int64_t exact_integer_max =
        (std::int64_t{1} << std::numeric_limits<double>::digits) - 1;

    friend class jsonpath;
};

}
