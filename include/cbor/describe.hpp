#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "rfc8949.hpp"

namespace cbor
{

inline constexpr std::size_t describe_entries_max = 342;

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
struct member_descriptor {
    static_assert(std::is_member_object_pointer_v<P>,
                  "CBOR_DESCRIBE_STRUCT lists a non-static data member only.");
    P pointer;
    std::string_view name;
    map_key key;
};

template <class T>
struct describe_members;

template <class T>
concept has_describe_members = requires { describe_members<T>::members; };

}

#define CBOR_PARENS ()
#define CBOR_EXPAND(...) CBOR_EXPAND4(CBOR_EXPAND4(CBOR_EXPAND4(CBOR_EXPAND4(__VA_ARGS__))))
#define CBOR_EXPAND4(...) CBOR_EXPAND3(CBOR_EXPAND3(CBOR_EXPAND3(CBOR_EXPAND3(__VA_ARGS__))))
#define CBOR_EXPAND3(...) CBOR_EXPAND2(CBOR_EXPAND2(CBOR_EXPAND2(CBOR_EXPAND2(__VA_ARGS__))))
#define CBOR_EXPAND2(...) CBOR_EXPAND1(CBOR_EXPAND1(CBOR_EXPAND1(CBOR_EXPAND1(__VA_ARGS__))))
#define CBOR_EXPAND1(...) __VA_ARGS__
#define CBOR_REMOVE_PARENS(...) __VA_ARGS__
#define CBOR_DESCRIBE_MEMBER(T, entry) CBOR_DESCRIBE_MEMBER_HELPER(T, CBOR_REMOVE_PARENS entry)
#define CBOR_DESCRIBE_MEMBER_HELPER(T, ...) CBOR_DESCRIBE_MEMBER_DESCRIPTOR(T, __VA_ARGS__)
#define CBOR_DESCRIBE_MEMBER_DESCRIPTOR(T, m, ...)                                                           \
    cbor::member_descriptor<decltype(&T::m)>                                                                 \
    {                                                                                                        \
        &T::m, #m, cbor::reflection::key_or(#m __VA_OPT__(, __VA_ARGS__))                                    \
    }
#define CBOR_FOR_EACH(T, ...) __VA_OPT__(CBOR_EXPAND(CBOR_FOR_EACH_HELPER(T, __VA_ARGS__)))
#define CBOR_FOR_EACH_HELPER(T, a, ...)                                                                      \
    CBOR_DESCRIBE_MEMBER(T, a) __VA_OPT__(, CBOR_FOR_EACH_AGAIN CBOR_PARENS(T, __VA_ARGS__))
#define CBOR_FOR_EACH_AGAIN() CBOR_FOR_EACH_HELPER
#define CBOR_DESCRIBE_STRUCT(T, TAG, ...)                                                                    \
    template <>                                                                                              \
    struct cbor::describe_members<T> {                                                                       \
        static_assert(cbor::reflection::entries_count(#__VA_ARGS__) <= cbor::describe_entries_max,           \
                      "CBOR_DESCRIBE_STRUCT takes at most cbor::describe_entries_max members.");             \
        static constexpr std::uint64_t tag = cbor::tag{CBOR_REMOVE_PARENS TAG}.number;                       \
        static constexpr std::uint16_t generation = cbor::tag{CBOR_REMOVE_PARENS TAG}.generation;            \
        static constexpr auto members = std::tuple{CBOR_FOR_EACH(T, __VA_ARGS__)};                           \
        static_assert(!cbor::reflection::tag_taken_by_standard_type(tag),                                    \
                      "CBOR_DESCRIBE_STRUCT takes a tag that no standard type has.");                        \
        static_assert(cbor::reflection::names_unique(members),                                               \
                      "CBOR_DESCRIBE_STRUCT lists a member twice.");                                         \
        static_assert(cbor::reflection::keys_unique(members),                                                \
                      "CBOR_DESCRIBE_STRUCT gives two members the same key.");                               \
        static_assert(                                                                                       \
            cbor::reflection::annotations_absent<T>(),                                                       \
            "a type with CBOR_DESCRIBE_STRUCT takes its members, keys and tag from the macro only.");        \
        static_assert(                                                                                       \
            cbor::reflection::reflected_members_listed<T>(members),                                          \
            "CBOR_DESCRIBE_STRUCT lists other members than reflection reads with unprivileged().");          \
    };
