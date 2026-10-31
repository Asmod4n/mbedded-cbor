// Gives cbor::ref, a value with views into encoded bytes, together
// with a shared owner of those bytes. Gives cbor::owning, a value that
// holds its own memory. ref::clone copies the views into new memory.
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

#include "rfc8746.hpp"

#if defined(__clang__)
#define CBOR_LIFETIMEBOUND [[clang::lifetimebound]]
#define CBOR_OWNER [[gsl::Owner]]
#else
#define CBOR_LIFETIMEBOUND
#define CBOR_OWNER
#endif

namespace cbor
{

template <class T>
class schema;

template <class T>
class structs;

struct lazy;

class jsonpath;

class encoding;

class iov_writer;

template <class T>
inline constexpr bool is_view = std::ranges::view<T>;

template <>
inline constexpr bool is_view<typed_array> = true;

template <class T>
concept copied_value = std::same_as<T, std::string> || std::same_as<T, std::vector<std::byte>>;

template <class R>
concept owning_range_of_chars = std::ranges::contiguous_range<R> && !std::ranges::borrowed_range<R> && !std::is_const_v<R> &&
                       std::same_as<std::ranges::range_value_t<R>, char>;

template <class R>
concept borrowed_range_of_chars = std::ranges::contiguous_range<R> && std::ranges::borrowed_range<R> &&
                         std::same_as<std::ranges::range_value_t<R>, char>;

template <class T>
class owning;

template <class T>
void copy_views_into_zone(T &value, std::pmr::memory_resource &zone);

template <class T>
class CBOR_OWNER ref
{
    std::shared_ptr<void const> owner;
    T value;

    ref(std::shared_ptr<void const> o, T v) : owner(std::move(o)), value(std::move(v))
    {
    }

    template <class>
    friend class schema;

    template <class>
    friend class structs;

    friend struct lazy;

    friend class jsonpath;

    friend class encoding;

    friend class iov_writer;

public:
    ref(ref const &) = default;
    ref &operator=(ref const &) = default;

    ref(ref &&o) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                        std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
        : owner(std::move(o.owner)), value(std::exchange(o.value, T{}))
    {
    }

    ref &operator=(ref &&o) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                   std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
    {
        owner = std::move(o.owner);
        value = std::exchange(o.value, T{});
        return *this;
    }

    std::conditional_t<is_view<T>, T, T const &> operator*() const & CBOR_LIFETIMEBOUND
    {
        return value;
    }

    void operator*() const && = delete;

    T const *operator->() const & CBOR_LIFETIMEBOUND
    {
        return &value;
    }

    void operator->() const && = delete;

    std::string clone() const
        requires std::same_as<T, std::string_view>
    {
        return std::string(value);
    }

    std::vector<std::byte> clone() const
        requires std::same_as<T, std::span<std::byte const>>
    {
        return std::vector<std::byte>(value.begin(), value.end());
    }

    owning<T> clone() const
        requires(!std::same_as<T, std::string_view> && !std::same_as<T, std::span<std::byte const>>)
    {
        struct held {
            std::pmr::monotonic_buffer_resource zone;
            T value;

            explicit held(T const &v) : value(v)
            {
            }
        };
        auto const h = std::make_shared<held>(value);
        copy_views_into_zone(h->value, h->zone);
        return owning<T>(std::shared_ptr<T const>(h, &h->value));
    }

    friend bool operator==(ref const &a, ref const &b)
        requires std::equality_comparable<T>
    {
        return a.value == b.value;
    }

    friend bool operator==(ref const &a, T const &b)
        requires std::equality_comparable<T>
    {
        return a.value == b;
    }
};

struct zone_owner;

template <class T>
class CBOR_OWNER owning
{
    std::shared_ptr<T const> held;

    explicit owning(std::shared_ptr<T const> h) : held(std::move(h))
    {
    }

    friend struct zone_owner;

    template <class>
    friend class ref;

public:
    T const &operator*() const & CBOR_LIFETIMEBOUND
    {
        return *held;
    }

    void operator*() const && = delete;

    T const *operator->() const & CBOR_LIFETIMEBOUND
    {
        return held.get();
    }

    void operator->() const && = delete;
};

}
