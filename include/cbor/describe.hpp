/**
 * @file
 * Declares cbor::describe_members, which a specialization fills with
 * the tag of a type and its members, each with a cbor::map_key.
 * CBOR_DEFINE_TYPE writes this specialization. CBOR_MEMBER writes one
 * member. CBOR_DEFINE_TYPE_MEMBERS writes it from the names of the
 * members, and each key is the name of its member. The DERIVED forms also
 * name the direct base class of the type.
 */
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

/**
 * @brief One entry of cbor::describe_members: the key of a member and a pointer to that member.
 *
 * The key is a text literal or an integer from -2^63 to 2^64 - 1.
 * The text is the name as it is written.
 * A pointer to a static member or to a member function does not compile.
 * A `bool` or a character type as the key does not compile.
 *
 * @code{.callseq}
 * cbor::member{0, &senml::n}          → cbor::member<std::string senml::*>
 * cbor::member{"name", &person::name} → cbor::member<std::string person::*>
 * @endcode
 * @code{.example}
 * bytes: a2 00 61 74 02 f9 3e 00
 * call: auto const encoded = cbor::structs<senml>::encode(senml{"t", 1.5})
 * result: 8 bytes: a2 00 61 74 02 f9 3e 00
 * call: cbor::lazy::adopt(*encoded)
 * result: lazy of {0: "t", 2: 1.5}
 * @endcode
 *
 * @snippet doc_types.hpp member
 * @snippet doc_reference_structs.cpp member use
 * @see CBOR_MEMBER
 * @see CBOR_DEFINE_TYPE_MEMBERS
 * @see cbor::describe_members
 *
 * @ingroup structs
 */
template <class P>
struct member {
    static_assert(std::is_member_object_pointer_v<P>, "cbor::member takes a non-static data member only.");
    /** @brief The key of the member in the map. */
    map_key key;
    /** @brief The pointer to the member. */
    P pointer;
};

/**
 * @brief Describes how cbor::structs and cbor::schema write and read `T`.
 *
 * A specialization holds `tag`, a `std::optional<cbor::tag>`, and `members`, a `std::tuple` of cbor::member entries.
 * `cbor::untagged` gives a map with no tag.
 * The specialization stands outside every namespace, after `T`.
 * `cbor::has_describe_members<T>` is true when `T` has a specialization.
 *
 * A class that is not an aggregate, or that has a base class, needs a description.
 * cbor::structs then reads the members, the keys and the tag from the description only.
 * `using base = B;` in the specialization names the direct base class `B`; cbor::schema takes a class with a base
 * class only so, and the members of `B` stand in `members` too.
 * `CBOR_DEFINE_DERIVED_TYPE` and `CBOR_DEFINE_DERIVED_TYPE_MEMBERS` write such a specialization; they take `BASE`
 * after `T`.
 *
 * `CBOR_DEFINE_TYPE(T, TAG, members...)` writes this specialization.
 * The macro stands at global scope.
 * A type whose name holds a comma goes through an alias.
 * Where each key is the name of its member, CBOR_DEFINE_TYPE_MEMBERS takes the names only.
 *
 * `decode` gives `incorrect_type` for a missing tag or another tag.
 * A description does not compile when it lists a member twice, gives two members one key, names a member of another
 * class, or has no `tag`.
 * A tag of a standard type or an invalid tag does not compile.
 * With reflection, a list that differs from the public data members does not compile.
 * An annotation on `T` does not compile.
 *
 * @code{.callseq}
 * cbor::has_describe_members<T>   → bool
 * cbor::structs<T>::encode(value) → std::expected<std::string, cbor::error>
 * @endcode
 * @code{.example}
 * message: {"content-type": "text/plain"}
 * call: cbor::has_describe_members<header>
 * result: true
 * call: cbor::has_describe_members<person>
 * result: false
 * call: auto const h = cbor::structs<header>::encode(header{"text/plain"})
 * result: 30 bytes: da 00 02 71 3c a1 6c 63 6f 6e 74 65 6e 74 2d 74 79 70 65 6a 74 65 78 74 2f 70 6c 61 69 6e
 * call: cbor::lazy::adopt(*h)
 * result: lazy of 160060({"content-type": "text/plain"})
 * call: cbor::structs<header>::decode(*h).has_value()
 * result: true
 * call: auto const t = cbor::structs<trailer>::encode(trailer{7})
 * result: 16 bytes: da 00 02 71 3d a1 68 63 68 65 63 6b 73 75 6d 07
 * call: cbor::lazy::adopt(*t)
 * result: lazy of 160061({"checksum": 7})
 * @endcode
 *
 * @snippet doc_types.hpp describe_members
 * @snippet doc_reference_structs.cpp describe_members use
 * @snippet doc_types.hpp CBOR_DEFINE_TYPE
 * @snippet doc_reference_structs.cpp CBOR_DEFINE_TYPE use
 * @see cbor::member
 * @see CBOR_MEMBER
 * @see CBOR_DEFINE_TYPE_MEMBERS
 *
 * @ingroup structs
 */
template <class T>
struct describe_members;

template <class T>
concept has_describe_members = requires { describe_members<T>::members; };

template <class T>
concept has_described_base = requires { typename describe_members<T>::base; };

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

/**
 * @brief Makes the cbor::member entry of `T::m` with the text of the name `m` as its key.
 *
 * A type whose name holds a comma needs a type alias.
 * The errors of cbor::member apply.
 *
 * @code{.callseq}
 * CBOR_MEMBER(account, user) → cbor::member<std::string account::*>
 * @endcode
 * @code{.example}
 * message: {"user": "ann", "level": 2}
 * call: auto const encoded = cbor::structs<account>::encode(account{"ann", 2})
 * result: 17 bytes: a2 64 75 73 65 72 63 61 6e 6e 65 6c 65 76 65 6c 02
 * call: cbor::lazy::adopt(*encoded)
 * result: lazy of {"user": "ann", "level": 2}
 * @endcode
 *
 * @param T the type that holds the member
 * @param m the name of the member
 * @snippet doc_types.hpp CBOR_MEMBER
 * @snippet doc_reference_structs.cpp CBOR_MEMBER use
 * @see cbor::member
 * @see cbor::describe_members
 * @see CBOR_DEFINE_TYPE_MEMBERS
 *
 * @ingroup structs
 */
#define CBOR_MEMBER(T, m) cbor::member{cbor::map_key(std::string_view{#m}), &T::m}

#define CBOR_DEFINE_TYPE(T, TAG, ...)                                                                                  \
    template <>                                                                                                        \
    struct cbor::describe_members<T> {                                                                                 \
        static constexpr std::optional<cbor::tag> tag = TAG;                                                           \
        static constexpr std::tuple members{__VA_ARGS__};                                                              \
    }

#define CBOR_DEFINE_DERIVED_TYPE(T, BASE, TAG, ...)                                                                    \
    template <>                                                                                                        \
    struct cbor::describe_members<T> {                                                                                 \
        using base = BASE;                                                                                             \
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

/**
 * @brief Writes the cbor::describe_members specialization of `T` with the tag `TAG` and one CBOR_MEMBER entry for
 * each name in the list.
 *
 * The key of each member is its name.
 * The list takes up to 343 names.
 * The macro stands outside every namespace, after `T`.
 * A type whose name holds a comma needs a type alias.
 * The macro expands each name before it makes the key.
 * A name that is also an object-like macro gives the key of what the macro expands to.
 * A name that is not a member of `T` does not compile.
 * A list of more than 343 names does not compile.
 * The errors of cbor::describe_members apply.
 *
 * @code{.callseq}
 * CBOR_DEFINE_TYPE_MEMBERS(device, cbor::untagged, name, port)
 * @endcode
 * @code{.example}
 * message: {"name": "lamp", "port": 3}
 * call: cbor::has_describe_members<device>
 * result: true
 * call: auto const encoded = cbor::structs<device>::encode(device{"lamp", 3})
 * result: 17 bytes: a2 64 6e 61 6d 65 64 6c 61 6d 70 64 70 6f 72 74 03
 * call: cbor::lazy::adopt(*encoded)
 * result: lazy of {"name": "lamp", "port": 3}
 * @endcode
 *
 * @param T the type to describe
 * @param TAG the tag of the type, or `cbor::untagged`
 * @param ... the names of the members
 * @snippet doc_types.hpp CBOR_DEFINE_TYPE_MEMBERS
 * @snippet doc_reference_structs.cpp CBOR_DEFINE_TYPE_MEMBERS use
 * @see CBOR_MEMBER
 * @see cbor::member
 * @see cbor::describe_members
 *
 * @ingroup structs
 */
#define CBOR_DEFINE_TYPE_MEMBERS(T, TAG, ...) CBOR_DEFINE_TYPE(T, TAG, CBOR_MEMBERS(T, __VA_ARGS__))

#define CBOR_DEFINE_DERIVED_TYPE_MEMBERS(T, BASE, TAG, ...)                                                            \
    CBOR_DEFINE_DERIVED_TYPE(T, BASE, TAG, CBOR_MEMBERS(T, __VA_ARGS__))
