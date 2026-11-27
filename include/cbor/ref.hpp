/**
 * @file
 * Gives cbor::ref, a value with views into encoded bytes, together
 * with a shared owner of those bytes. Gives cbor::owning, a value that
 * holds its own memory. ref::clone copies the views into new memory.
 */
#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <ranges>
#include <type_traits>
#include <utility>

#include "string_saver.hpp"
#include "rfc8746.hpp"
#include "validity.hpp"

#if defined(__clang__)
#define CBOR_OWNER [[gsl::Owner]]
#else
#define CBOR_OWNER
#endif

namespace cbor
{

namespace detail
{

class encoding;

}

template <class T>
class schema;

template <class T>
class structs;

struct lazy;

class path;

template <class T>
inline constexpr bool is_view = std::ranges::view<T>;

template <>
inline constexpr bool is_view<typed_array> = true;

template <class T>
concept copied_value = std::same_as<T, std::string> || std::same_as<T, std::vector<std::byte>>;

namespace detail
{

template <class T>
concept byte_like = std::same_as<T, char> || std::same_as<T, unsigned char> || std::same_as<T, std::byte>;

}

template <class R>
concept owning_range_of_bytes = std::ranges::contiguous_range<R> && !std::ranges::borrowed_range<R> && !std::is_const_v<R> &&
                       detail::byte_like<std::ranges::range_value_t<R>>;

template <class R>
concept borrowed_range_of_bytes = std::ranges::contiguous_range<R> && std::ranges::borrowed_range<R> &&
                         detail::byte_like<std::ranges::range_value_t<R>>;

namespace detail
{

template <std::ranges::contiguous_range R>
    requires byte_like<std::ranges::range_value_t<R>>
std::string_view chars_of(R const &bytes) noexcept
{
    return {reinterpret_cast<char const *>(std::ranges::data(bytes)), std::ranges::size(bytes)};
}

}

template <class T>
class owning;

namespace detail
{

template <class T, class Kept>
void copy_views_into_zone(T &value, string_saver &zone, Kept &kept);

using kept_objects = std::vector<std::shared_ptr<void>>;

struct no_kept_objects {
};

template <class T>
struct ref_targets {
    using kept = no_kept_objects;
};

}

template <class T>
class ref;

namespace detail
{

template <class T>
ref<T> copy_with_cycles(T const &value);

}

/**
 * @brief A value and a `std::shared_ptr` to the owner of the bytes that it views.
 *
 * `*r` gives a view by value and any other `T` as `T const &`.
 * `r->` reaches the members.
 * `==` compares the values.
 * A copy shares the owner and copies no bytes.
 * A move leaves `T{}` and no owner behind.
 *
 * A view in the value stays valid while one `ref` with that owner lives.
 * A view copied out of the `ref` does not keep the bytes.
 * The same holds for the target of a `U*` in the value: the owner keeps it, and a copy of the `ref` shares it.
 * A `ref` from cbor::structs::decode_cycles() keeps its value in the owner, so a copy and a move give the same
 * address, and a `U*` to the value or into it stays valid.
 * `clone` copies the bytes: into a `std::string`, a byte vector, or a cbor::owning.
 * Only the library makes a `ref`.
 * `*` and `->` on a temporary `ref` do not compile.
 *
 * @code{.example}
 * message: "abc"
 * call: auto const r = cbor::structs<std::string_view>::decode(std::move(bytes))
 * result: ref to "abc"
 * call: std::string_view const v = **r
 * result: "abc"
 * call: r->clone()
 * result: "abc"
 * @endcode
 *
 * @snippet doc_reference_ref.cpp ref
 * @see cbor::owning
 *
 * @ingroup ref
 */
template <class T>
class CBOR_OWNER ref
{
    std::shared_ptr<void const> owner;
    T value;
    [[no_unique_address]] detail::ref_targets<T> targets;

    ref(std::shared_ptr<void const> o, T v) : owner(std::move(o)), value(std::move(v)), targets{}
    {
    }

    T const &stored() const
    {
        if constexpr (requires { targets.in_block; })
            if (targets.in_block != nullptr)
                return *targets.in_block;
        return value;
    }

    template <class>
    friend class schema;

    template <class>
    friend class structs;

    friend struct lazy;

    friend class path;

    friend class detail::encoding;

public:
    /** @brief Shares the owner and copies the value. */
    ref(ref const &other) = default;

    /** @brief Shares the owner and copies the value. */
    ref &operator=(ref const &other) = default;

    /** @brief Takes the owner and the value of @p o, and leaves `T{}` and no owner in @p o. */
    ref(ref &&o) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                        std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
        : owner(std::move(o.owner)), value(std::exchange(o.value, T{})), targets(std::exchange(o.targets, {}))
    {
    }

    /** @brief Takes the owner and the value of @p o, and leaves `T{}` and no owner in @p o. */
    ref &operator=(ref &&o) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                   std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
    {
        owner = std::move(o.owner);
        value = std::exchange(o.value, T{});
        targets = std::exchange(o.targets, {});
        return *this;
    }

    /** @brief Gives the value: a view by value, any other `T` as `T const &`. */
    std::conditional_t<is_view<T>, T, T const &> operator*() const & CBOR_LIFETIMEBOUND
    {
        return stored();
    }

    void operator*() const && = delete;

    /** @brief Gives the address of the value, so that `r->` reaches its members. */
    T const *operator->() const & CBOR_LIFETIMEBOUND
    {
        return &stored();
    }

    void operator->() const && = delete;

    /** @brief Copies the text into a `std::string`. */
    std::string clone() const
        requires std::same_as<T, std::string_view>
    {
        return std::string(value);
    }

    /** @brief Copies the bytes into a byte vector. */
    std::vector<std::byte> clone() const
        requires std::same_as<T, std::span<std::byte const>>
    {
        return std::vector<std::byte>(value.begin(), value.end());
    }

    /**
     * @brief Copies the value and the bytes of each view in it into a cbor::owning.
     *
     * The target of each `U*` and `std::shared_ptr<U>` in the value is copied once for each pointer, and the copy
     * belongs to the cbor::owning. A `ref` from cbor::structs::decode_cycles() is copied with each cycle: a `U*` that
     * points to an object of the value points to the copy of that object.
     *
     * @throws std::system_error a `ref` from cbor::structs::decode_cycles() whose value is deeper than the stack of
     * the thread holds, with cbor::error::nesting_depth_exceeded
     */
    owning<T> clone() const
        requires(!std::same_as<T, std::string_view> && !std::same_as<T, std::span<std::byte const>>)
    {
        if constexpr (requires { targets.in_block; })
            if (targets.in_block != nullptr) {
                ref const copy = detail::copy_with_cycles(*targets.in_block);
                return owning<T>(std::shared_ptr<T const>(copy.owner, copy.targets.in_block));
            }
        struct held {
            std::pmr::monotonic_buffer_resource memory;
            detail::string_saver zone{memory};
            [[no_unique_address]] typename detail::ref_targets<T>::kept objects;
            T value;

            explicit held(T const &v) : objects{}, value(v)
            {
            }
        };
        auto const h = std::make_shared<held>(value);
        detail::copy_views_into_zone(h->value, h->zone, h->objects);
        return owning<T>(std::shared_ptr<T const>(h, &h->value));
    }

    /** @brief Compares the values of two refs. */
    friend bool operator==(ref const &a, ref const &b)
        requires std::equality_comparable<T>
    {
        return a.stored() == b.stored();
    }

    /** @brief Compares the value of a ref with a value. */
    friend bool operator==(ref const &a, T const &b)
        requires std::equality_comparable<T>
    {
        return a.stored() == b;
    }
};

namespace detail
{

struct tree_owner;

}

/**
 * @brief A value that holds its own copy of the bytes of each view in it.
 *
 * cbor::ref::clone() makes it for a `T` that is not a single text or byte view.
 * The copy and the value share one `std::shared_ptr`, so a view in it stays valid after the bytes of the `ref` end.
 * A copy of an `owning` shares the value and copies nothing.
 * It has no `==` and no public constructor.
 * The value is `const`.
 * `*` and `->` on a temporary `owning` do not compile.
 *
 * @code{.example}
 * message: {"name": "ann", "age": 30}
 * call: auto const decoded = cbor::structs<person>::decode(std::move(bytes))
 * result: ref to person{"ann", 30}
 * call: auto const own = decoded->clone()
 * result: owning of person{"ann", 30}
 * call: own->name
 * result: "ann"
 * @endcode
 *
 * @snippet doc_reference_ref.cpp owning
 * @see cbor::ref
 *
 * @ingroup ref
 */
template <class T>
class CBOR_OWNER owning
{
    std::shared_ptr<T const> held;

    explicit owning(std::shared_ptr<T const> h) : held(std::move(h))
    {
    }

    friend struct detail::tree_owner;

    friend class detail::encoding;

    template <class>
    friend class ref;

public:
    /** @brief Gives the value. */
    T const &operator*() const & CBOR_LIFETIMEBOUND
    {
        return *held;
    }

    void operator*() const && = delete;

    /** @brief Gives the address of the value, so that `own->` reaches its members. */
    T const *operator->() const & CBOR_LIFETIMEBOUND
    {
        return held.get();
    }

    void operator->() const && = delete;
};

}
