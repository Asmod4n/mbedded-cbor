/**
 * @file
 * Gives cbor::result, the value or the error that a read of the library gives.
 * It holds a std::expected and gives its members on a named result only.
 */
#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "error.hpp"
#include "head.hpp"
#include "validity.hpp"

namespace cbor
{

result<item> encoded_cbor_data_item(owning<item> const &tree, item tagged);

/**
 * @brief The value of a read, or its error, as `std::expected<T, E>` holds them.
 *
 * Only the library makes a result; a program copies, moves, compares and reads it.
 * The members that give a reference or a pointer into the result, and the monadic forms, take a named result
 * only. On a temporary they do not compile: `*doc.at("a")`, `doc.at("a")->size()` and
 * `doc.at("a").value()` are ill-formed. Each step is a named variable, and the next call goes on that
 * variable.
 * No conversion to `std::expected` exists.
 *
 * @code{.example}
 * message: {"a": [1, 2]}
 * call: auto const a = doc->at("a")
 * result: lazy of [1, 2]
 * call: a->size()
 * result: 2
 * @endcode
 *
 * @ingroup errors
 */
template <class T, class E>
class result {
    std::expected<T, E> held;

public:
    using value_type = T;
    using error_type = E;

private:
    template <class... A>
        requires std::is_constructible_v<std::expected<T, E>, A...>
    constexpr result(A &&...a) : held(std::forward<A>(a)...)
    {
    }

    template <class R>
    static constexpr bool is_result = false;

    template <class U, class G>
    static constexpr bool is_result<result<U, G>> = true;

    template <class, class>
    friend class result;

    friend struct lazy;

    friend struct item;

    friend result<owning<item>> decode(std::string_view encoded);

    friend result<owning<item>> decode_cycles(std::string_view encoded);

    friend result<item> encoded_cbor_data_item(owning<item> const &tree, item tagged);

    friend result<std::size_t> item_size(std::string_view encoded);

public:
    constexpr result(result const &) = default;
    constexpr result(result &&) = default;
    constexpr result &operator=(result const &) = default;
    constexpr result &operator=(result &&) = default;

    constexpr bool has_value() const noexcept
    {
        return held.has_value();
    }

    /**
     * @brief Gives cbor::result::has_value().
     */
    constexpr explicit operator bool() const noexcept
    {
        return held.has_value();
    }

    /**
     * @brief Gives the value; the result holds a value.
     */
    constexpr T const &operator*() const & CBOR_LIFETIMEBOUND
    {
        return *held;
    }

    constexpr T &operator*() & CBOR_LIFETIMEBOUND
    {
        return *held;
    }

    T const &&operator*() const && = delete;
    T &&operator*() && = delete;

    /**
     * @brief Gives the address of the value; the result holds a value.
     */
    constexpr T const *operator->() const & CBOR_LIFETIMEBOUND
    {
        return held.operator->();
    }

    constexpr T *operator->() & CBOR_LIFETIMEBOUND
    {
        return held.operator->();
    }

    T const *operator->() const && = delete;
    T *operator->() && = delete;

    /**
     * @brief Gives the value.
     *
     * @throws std::bad_expected_access<E> the result holds an error
     */
    constexpr T const &value() const & CBOR_LIFETIMEBOUND
    {
        return held.value();
    }

    constexpr T &value() & CBOR_LIFETIMEBOUND
    {
        return held.value();
    }

    T const &&value() const && = delete;
    T &&value() && = delete;

    /**
     * @brief Gives the error; the result holds an error.
     */
    constexpr E const &error() const & CBOR_LIFETIMEBOUND
    {
        return held.error();
    }

    E const &&error() const && = delete;

    /**
     * @brief Gives a copy of the value, or `u` when the result holds an error.
     */
    template <class U>
    constexpr T value_or(U &&u) const &
    {
        return held.value_or(std::forward<U>(u));
    }

    /**
     * @brief Moves the value out, or gives `u` when the result holds an error.
     */
    template <class U>
    constexpr T value_or(U &&u) &&
    {
        return std::move(held).value_or(std::forward<U>(u));
    }

    /**
     * @brief Gives the result of `f(value)`, or the error.
     *
     * `f` gives a cbor::result with the same error type.
     */
    template <class F>
        requires is_result<std::remove_cvref_t<std::invoke_result_t<F, T const &>>> &&
                 std::is_same_v<
                     typename std::remove_cvref_t<std::invoke_result_t<F, T const &>>::error_type, E>
    constexpr auto and_then(F &&f) const & -> std::remove_cvref_t<std::invoke_result_t<F, T const &>>
    {
        if (held)
            return std::invoke(std::forward<F>(f), *held);
        return {std::unexpect, held.error()};
    }

    template <class F>
    auto and_then(F &&) && = delete;
    template <class F>
    auto and_then(F &&) const && = delete;

    /**
     * @brief Gives a result that holds `f(value)`, or the error.
     */
    template <class F>
    constexpr auto transform(F &&f) const & -> result<std::remove_cv_t<std::invoke_result_t<F, T const &>>, E>
    {
        using U = std::remove_cv_t<std::invoke_result_t<F, T const &>>;
        if (!held)
            return {std::unexpect, held.error()};
        if constexpr (std::is_void_v<U>) {
            std::invoke(std::forward<F>(f), *held);
            return {};
        } else {
            return {std::in_place, std::invoke(std::forward<F>(f), *held)};
        }
    }

    template <class F>
    auto transform(F &&) && = delete;
    template <class F>
    auto transform(F &&) const && = delete;

    /**
     * @brief Gives a copy of the value, or the result of `f(error)`.
     *
     * `f` gives a cbor::result with the same value type.
     */
    template <class F>
        requires is_result<std::remove_cvref_t<std::invoke_result_t<F, E const &>>> &&
                 std::is_same_v<
                     typename std::remove_cvref_t<std::invoke_result_t<F, E const &>>::value_type, T>
    constexpr auto or_else(F &&f) const & -> std::remove_cvref_t<std::invoke_result_t<F, E const &>>
    {
        if (held)
            return {std::in_place, *held};
        return std::invoke(std::forward<F>(f), held.error());
    }

    template <class F>
    auto or_else(F &&) && = delete;
    template <class F>
    auto or_else(F &&) const && = delete;

    /**
     * @brief Gives a copy of the value, or a result that holds `f(error)`.
     */
    template <class F>
    constexpr auto transform_error(F &&f) const &
        -> result<T, std::remove_cv_t<std::invoke_result_t<F, E const &>>>
    {
        if (held)
            return {std::in_place, *held};
        return {std::unexpect, std::invoke(std::forward<F>(f), held.error())};
    }

    template <class F>
    auto transform_error(F &&) && = delete;
    template <class F>
    auto transform_error(F &&) const && = delete;

    /**
     * @brief Compares as two `std::expected` compare.
     */
    template <class U, class G>
    friend constexpr bool operator==(result const &a, result<U, G> const &b)
    {
        return a.held == b.held;
    }

    /**
     * @brief Gives `true` when `a` holds a value equal to `v`.
     */
    template <class U>
        requires(!is_result<U>)
    friend constexpr bool operator==(result const &a, U const &v)
    {
        return a.held == v;
    }

    /**
     * @brief Gives `true` when `a` holds an error equal to `e.error()`.
     */
    template <class G>
    friend constexpr bool operator==(result const &a, std::unexpected<G> const &e)
    {
        return a.held == e;
    }
};

/**
 * @brief The success of a read that gives no value, or its error.
 *
 * The rules of cbor::result apply.
 *
 * @ingroup errors
 */
template <class E>
class result<void, E> {
    std::expected<void, E> held;

public:
    using value_type = void;
    using error_type = E;

private:
    template <class... A>
        requires std::is_constructible_v<std::expected<void, E>, A...>
    constexpr result(A &&...a) : held(std::forward<A>(a)...)
    {
    }

    template <class R>
    static constexpr bool is_result = false;

    template <class U, class G>
    static constexpr bool is_result<result<U, G>> = true;

    template <class, class>
    friend class result;

    friend struct lazy;

public:
    constexpr result(result const &) = default;
    constexpr result(result &&) = default;
    constexpr result &operator=(result const &) = default;
    constexpr result &operator=(result &&) = default;

    constexpr bool has_value() const noexcept
    {
        return held.has_value();
    }

    /**
     * @brief Gives cbor::result::has_value().
     */
    constexpr explicit operator bool() const noexcept
    {
        return held.has_value();
    }

    /**
     * @brief Checks that the result holds no error.
     *
     * @throws std::bad_expected_access<E> the result holds an error
     */
    constexpr void value() const
    {
        held.value();
    }

    /**
     * @brief Gives the error; the result holds an error.
     */
    constexpr E const &error() const & CBOR_LIFETIMEBOUND
    {
        return held.error();
    }

    E const &&error() const && = delete;

    /**
     * @brief Gives the result of `f()`, or the error.
     *
     * `f` gives a cbor::result with the same error type.
     */
    template <class F>
        requires is_result<std::remove_cvref_t<std::invoke_result_t<F>>> &&
                 std::is_same_v<typename std::remove_cvref_t<std::invoke_result_t<F>>::error_type, E>
    constexpr auto and_then(F &&f) const & -> std::remove_cvref_t<std::invoke_result_t<F>>
    {
        if (held)
            return std::invoke(std::forward<F>(f));
        return {std::unexpect, held.error()};
    }

    template <class F>
    auto and_then(F &&) && = delete;
    template <class F>
    auto and_then(F &&) const && = delete;

    /**
     * @brief Gives a result that holds `f()`, or the error.
     */
    template <class F>
    constexpr auto transform(F &&f) const & -> result<std::remove_cv_t<std::invoke_result_t<F>>, E>
    {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (!held)
            return {std::unexpect, held.error()};
        if constexpr (std::is_void_v<U>) {
            std::invoke(std::forward<F>(f));
            return {};
        } else {
            return {std::in_place, std::invoke(std::forward<F>(f))};
        }
    }

    template <class F>
    auto transform(F &&) && = delete;
    template <class F>
    auto transform(F &&) const && = delete;

    /**
     * @brief Gives a success, or the result of `f(error)`.
     *
     * `f` gives a cbor::result of `void`.
     */
    template <class F>
        requires is_result<std::remove_cvref_t<std::invoke_result_t<F, E const &>>> &&
                 std::is_void_v<typename std::remove_cvref_t<std::invoke_result_t<F, E const &>>::value_type>
    constexpr auto or_else(F &&f) const & -> std::remove_cvref_t<std::invoke_result_t<F, E const &>>
    {
        if (held)
            return {};
        return std::invoke(std::forward<F>(f), held.error());
    }

    template <class F>
    auto or_else(F &&) && = delete;
    template <class F>
    auto or_else(F &&) const && = delete;

    /**
     * @brief Gives a success, or a result that holds `f(error)`.
     */
    template <class F>
    constexpr auto transform_error(F &&f) const &
        -> result<void, std::remove_cv_t<std::invoke_result_t<F, E const &>>>
    {
        if (held)
            return {};
        return {std::unexpect, std::invoke(std::forward<F>(f), held.error())};
    }

    template <class F>
    auto transform_error(F &&) && = delete;
    template <class F>
    auto transform_error(F &&) const && = delete;

    /**
     * @brief Compares as two `std::expected` compare.
     */
    friend constexpr bool operator==(result const &a, result const &b)
    {
        return a.held == b.held;
    }

    /**
     * @brief Gives `true` when `a` holds an error equal to `e.error()`.
     */
    template <class G>
    friend constexpr bool operator==(result const &a, std::unexpected<G> const &e)
    {
        return a.held == e;
    }
};

}
