#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace cbor
{

template <class T>
class schema;

template <class T>
class databind;

struct lazy;

class jsonpath;

template <class T>
class owning_ref
{
    std::shared_ptr<void const> owner;
    T value;

    owning_ref(std::shared_ptr<void const> o, T v) : owner(std::move(o)), value(std::move(v))
    {
    }

    template <class>
    friend class schema;

    template <class>
    friend class databind;

    friend struct lazy;

    friend class jsonpath;

public:
    owning_ref(owning_ref const &) = default;
    owning_ref &operator=(owning_ref const &) = default;

    owning_ref(owning_ref &&o) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                        std::is_nothrow_default_constructible_v<T>)
        : owner(std::move(o.owner)), value(std::exchange(o.value, T{}))
    {
    }

    owning_ref &operator=(owning_ref &&o) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                   std::is_nothrow_default_constructible_v<T>)
    {
        owner = std::move(o.owner);
        value = std::exchange(o.value, T{});
        return *this;
    }

    T const &operator*() const &
    {
        return value;
    }

    T const *operator->() const &
    {
        return &value;
    }

    T const &operator*() const && = delete;
    T const *operator->() const && = delete;
};

template <class T>
using oref = owning_ref<T>;

}
