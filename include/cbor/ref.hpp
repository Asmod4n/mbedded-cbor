#pragma once

#include <concepts>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

namespace cbor
{

template <class T>
class packed;

template <class T>
class structs;

struct lazy;

class jsonpath;

class encoding;

class iov_writer;

template <class R>
concept owned_bytes = std::ranges::contiguous_range<R> && !std::is_lvalue_reference_v<R> && !std::is_const_v<R> &&
                      !std::ranges::borrowed_range<R> && std::same_as<std::ranges::range_value_t<R>, char>;

template <class R>
concept owning_bytes = std::ranges::contiguous_range<R> && !std::ranges::borrowed_range<R> &&
                       std::same_as<std::ranges::range_value_t<R>, char>;

template <class R>
concept borrowed_bytes = std::ranges::contiguous_range<R> && std::ranges::borrowed_range<R> &&
                         std::same_as<std::ranges::range_value_t<R>, char>;

template <class T>
class ref
{
    std::shared_ptr<void const> owner;
    T value;

    ref(std::shared_ptr<void const> o, T v) : owner(std::move(o)), value(std::move(v))
    {
    }

    template <class>
    friend class packed;

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

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    T const &operator*(this Self &&self)
    {
        return self.value;
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    T const *operator->(this Self &&self)
    {
        return &self.value;
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

}
