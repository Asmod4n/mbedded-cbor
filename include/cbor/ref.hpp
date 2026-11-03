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

namespace detail
{

template <class T>
void copy_views_into_zone(T &value, std::pmr::memory_resource &zone);

}

/// Holds a value and shared ownership of the bytes its views refer to.
/// Views copied out do not keep the bytes alive.
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
    /// Copies the value and shares the owner.
    /// Its cost includes the copy of T.
    ref(ref const &) = default;
    /// Copies the value and shares the owner.
    /// Its cost includes assignment of T and release of the previous owner.
    ref &operator=(ref const &) = default;

    /// Moves the owner and value, leaving the source value default-initialized.
    /// Its cost includes the operations of T.
    ref(ref &&o) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                        std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
        : owner(std::move(o.owner)), value(std::exchange(o.value, T{}))
    {
    }

    /// Moves the owner and value, leaving the source value default-initialized.
    /// Its cost includes the operations of T and release of the previous owner.
    ref &operator=(ref &&o) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                   std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
    {
        owner = std::move(o.owner);
        value = std::exchange(o.value, T{});
        return *this;
    }

    /// Returns a view by value or another T by const reference.
    /// Its cost includes copying T when T is a view.
    std::conditional_t<is_view<T>, T, T const &> operator*() const & CBOR_LIFETIMEBOUND
    {
        return value;
    }

    /// Dereferencing a temporary is disabled to prevent dangling views.
    void operator*() const && = delete;

    /// Returns a pointer to the value. The ref must outlive its use.
    /// @par Complexity
    /// O(1); allocates nothing.
    T const *operator->() const & CBOR_LIFETIMEBOUND
    {
        return &value;
    }

    /// Access through a temporary is disabled to prevent dangling references.
    void operator->() const && = delete;

    /// Copies a text view into an owning string.
    /// @par Complexity
    /// O(n), where n is the number of bytes; may allocate.
    std::string clone() const
        requires std::same_as<T, std::string_view>
    {
        return std::string(value);
    }

    /// Copies a byte view into an owning vector.
    /// @par Complexity
    /// O(n), where n is the number of bytes; may allocate.
    std::vector<std::byte> clone() const
        requires std::same_as<T, std::span<std::byte const>>
    {
        return std::vector<std::byte>(value.begin(), value.end());
    }

    /// Copies the value and the bytes of its views into owned storage.
    /// The cost includes copying T, traversing its views and copying their
    /// bytes. Maps and sets are rebuilt with their allocator. This allocates.
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
        detail::copy_views_into_zone(h->value, h->zone);
        return owning<T>(std::shared_ptr<T const>(h, &h->value));
    }

    /// Compares the values using T's equality operator.
    friend bool operator==(ref const &a, ref const &b)
        requires std::equality_comparable<T>
    {
        return a.value == b.value;
    }

    /// Compares the value with b using T's equality operator.
    friend bool operator==(ref const &a, T const &b)
        requires std::equality_comparable<T>
    {
        return a.value == b;
    }
};

namespace detail
{

struct zone_owner;

}

/// Shares a const value and owned storage for the bytes of its views.
/// A copy shares that storage. Views remain valid while an owner lives.
template <class T>
class CBOR_OWNER owning
{
    std::shared_ptr<T const> held;

    explicit owning(std::shared_ptr<T const> h) : held(std::move(h))
    {
    }

    friend struct detail::zone_owner;

    template <class>
    friend class ref;

public:
    /// Returns the value by const reference. The owner must outlive its use.
    /// @par Complexity
    /// O(1); allocates nothing.
    T const &operator*() const & CBOR_LIFETIMEBOUND
    {
        return *held;
    }

    /// Dereferencing a temporary is disabled to prevent dangling views.
    void operator*() const && = delete;

    T const *operator->() const & CBOR_LIFETIMEBOUND
    {
        return held.get();
    }

    /// Access through a temporary is disabled to prevent dangling references.
    void operator->() const && = delete;
};

}
