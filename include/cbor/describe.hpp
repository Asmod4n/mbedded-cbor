// Declares cbor::describe_members, which a specialization fills with
// the tag of a type and its members, each with a cbor::map_key.
// CBOR_DEFINE_TYPE writes this specialization. CBOR_MEMBER writes one
// member. CBOR_DEFINE_TYPE_MEMBERS writes it from the names of the
// members, and each key is the name of its member.
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

/// Indicates that a described type has no CBOR tag.
inline constexpr std::nullopt_t untagged = std::nullopt;

/// A compile-time integer or text key for a described member.
struct map_key {
    /// CBOR major type of the key.
    major_type major;
    /// Integer argument or text length in bytes.
    std::uint64_t argument;
    /// Text of a text key; the referenced characters must remain valid.
    std::string_view text;

    /// Makes an integer key at compile time.
    /// @param n The integer key; character and bool types are excluded.
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

    /// Makes a text key from an array, excluding its last character.
    /// @param t The character array.
    template <std::size_t N>
    consteval map_key(char const (&t)[N]) : major(major_type::text_string), argument(N - 1), text(t, N - 1)
    {
    }

    /// Makes a text key that refers to the characters in t.
    /// @param t The text key.
    consteval explicit map_key(std::string_view const t)
        : major(major_type::text_string), argument(t.size()), text(t)
    {
    }
};

/// Associates a map key with a non-static data member pointer.
template <class P>
struct member {
    static_assert(std::is_member_object_pointer_v<P>, "cbor::member takes a non-static data member only.");
    /// The key used for the member in a CBOR map.
    map_key key;
    /// Pointer to the described data member.
    P pointer;
};

/// Specialize for T to provide its tag and tuple of described members.
template <class T>
struct describe_members;

/// True when describe_members<T> provides members.
template <class T>
concept has_describe_members = requires { describe_members<T>::members; };

class describe
{
    template <class U>
    static constexpr std::size_t members_count =
        std::tuple_size_v<std::remove_cvref_t<decltype(describe_members<U>::members)>>;

    template <std::size_t I, class U>
    static constexpr auto &get(U &value)
    {
        return value.*std::get<I>(describe_members<std::remove_const_t<U>>::members).pointer;
    }

    template <class U, std::size_t I>
    using member_t = std::remove_cvref_t<decltype(get<I>(std::declval<U &>()))>;

    template <class U, std::size_t I>
    static constexpr map_key key_of = std::get<I>(describe_members<U>::members).key;

    friend class pfr;

    friend class reflection;
};

}

/// Creates a member description with its name as the text key.
#define CBOR_MEMBER(T, m) cbor::member{cbor::map_key(std::string_view{#m}), &T::m}

/// Defines describe_members<T> from a tag and member descriptions.
#define CBOR_DEFINE_TYPE(T, TAG, ...)                                                                                  \
    template <>                                                                                                        \
    struct cbor::describe_members<T> {                                                                                 \
        static constexpr std::optional<cbor::tag> tag = TAG;                                                           \
        static constexpr std::tuple members{__VA_ARGS__};                                                              \
    }

#define CBOR_PARENS ()

#define CBOR_EXPAND(...) CBOR_EXPAND4(CBOR_EXPAND4(CBOR_EXPAND4(CBOR_EXPAND4(__VA_ARGS__))))
#define CBOR_EXPAND4(...) CBOR_EXPAND3(CBOR_EXPAND3(CBOR_EXPAND3(CBOR_EXPAND3(__VA_ARGS__))))
#define CBOR_EXPAND3(...) CBOR_EXPAND2(CBOR_EXPAND2(CBOR_EXPAND2(CBOR_EXPAND2(__VA_ARGS__))))
#define CBOR_EXPAND2(...) CBOR_EXPAND1(CBOR_EXPAND1(CBOR_EXPAND1(CBOR_EXPAND1(__VA_ARGS__))))
#define CBOR_EXPAND1(...) __VA_ARGS__

#define CBOR_MEMBERS(T, ...) __VA_OPT__(CBOR_EXPAND(CBOR_MEMBERS_HELPER(T, __VA_ARGS__)))
#define CBOR_MEMBERS_HELPER(T, m, ...) CBOR_MEMBER(T, m) __VA_OPT__(, CBOR_MEMBERS_AGAIN CBOR_PARENS(T, __VA_ARGS__))
#define CBOR_MEMBERS_AGAIN() CBOR_MEMBERS_HELPER

/// Defines describe_members<T> from a tag and member names.
#define CBOR_DEFINE_TYPE_MEMBERS(T, TAG, ...) CBOR_DEFINE_TYPE(T, TAG, CBOR_MEMBERS(T, __VA_ARGS__))
