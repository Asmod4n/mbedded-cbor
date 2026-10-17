#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "rfc8949.hpp"

namespace cbor
{

inline constexpr std::nullopt_t untagged = std::nullopt;

struct map_key {
    major_type major;
    std::uint64_t argument;
    std::string_view text;

    template <std::integral I>
        requires(sizeof(I) <= sizeof(std::uint64_t) && !std::same_as<I, bool> && !std::same_as<I, char> &&
                 !std::same_as<I, wchar_t> && !std::same_as<I, char8_t> && !std::same_as<I, char16_t> &&
                 !std::same_as<I, char32_t>)
    consteval map_key(I const n)
        : major(std::is_signed_v<I> && n < 0 ? major_type::negative_integer : major_type::unsigned_integer),
          argument(std::is_signed_v<I> && n < 0
                       ? static_cast<std::uint64_t>(-1 - static_cast<std::int64_t>(n))
                       : static_cast<std::uint64_t>(n))
    {
    }

    template <std::size_t N>
    consteval map_key(char const (&t)[N]) : major(major_type::text_string), argument(N - 1), text(t, N - 1)
    {
    }

    consteval explicit map_key(std::string_view const t)
        : major(major_type::text_string), argument(t.size()), text(t)
    {
    }
};

template <class P>
struct member {
    static_assert(std::is_member_object_pointer_v<P>, "cbor::member takes a non-static data member only.");
    map_key key;
    P pointer;
};

template <class T>
struct describe_members;

template <class T>
concept has_describe_members = requires { describe_members<T>::members; };

}

#define CBOR_MEMBER(T, m) cbor::member{cbor::map_key(std::string_view{#m}), &T::m}
