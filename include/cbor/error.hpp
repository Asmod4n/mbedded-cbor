#pragma once

#include <concepts>
#include <expected>
#include <functional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace cbor
{

enum class error {
    too_little_data = 1,
    syntax_error,
    indefinite_length,
    nesting_depth_exceeded,
    inadmissible_type_for_tag_content,
    sharedref_index_not_marked,
    sharedref_index_out_of_range,
    sharedref_not_complete,
    reserved_simple_value,
    unsupported_value,
    not_indexable,
    index_out_of_bounds,
    key_not_found,
    invalid_path,
    incorrect_type,
    number_out_of_range,
    cyclic_data_structure,
    unpopulated_table_index,
    nodelist_too_long,
    duplicate_key,
    no_buffer_space,
    value_too_large,
    not_enough_memory,
    io_error,
    string_length_exceeded,
    container_elements_exceeded,
    not_singular_query
};

class error_category : public std::error_category
{
public:
    char const *name() const noexcept override
    {
        return "cbor";
    }

    std::string message(int const condition) const override
    {
        switch (static_cast<error>(condition)) {
        case error::too_little_data:
            return "too little data";
        case error::syntax_error:
            return "syntax error";
        case error::indefinite_length:
            return "indefinite length";
        case error::nesting_depth_exceeded:
            return "nesting depth exceeded";
        case error::inadmissible_type_for_tag_content:
            return "inadmissible type for tag content";
        case error::sharedref_index_not_marked:
            return "sharedref index not marked";
        case error::sharedref_index_out_of_range:
            return "sharedref index out of range";
        case error::sharedref_not_complete:
            return "sharedref not complete";
        case error::reserved_simple_value:
            return "reserved simple value";
        case error::unsupported_value:
            return "unsupported value";
        case error::not_indexable:
            return "not indexable";
        case error::index_out_of_bounds:
            return "index outside of array bounds";
        case error::key_not_found:
            return "key not found";
        case error::invalid_path:
            return "invalid path";
        case error::incorrect_type:
            return "incorrect type";
        case error::number_out_of_range:
            return "number out of range";
        case error::cyclic_data_structure:
            return "cyclic data structure";
        case error::unpopulated_table_index:
            return "unpopulated table index";
        case error::nodelist_too_long:
            return "nodelist too long";
        case error::duplicate_key:
            return "duplicate key";
        case error::no_buffer_space:
            return "no buffer space";
        case error::value_too_large:
            return "value too large";
        case error::not_enough_memory:
            return "not enough memory";
        case error::io_error:
            return "io error";
        case error::string_length_exceeded:
            return "string length exceeded";
        case error::container_elements_exceeded:
            return "container elements exceeded";
        case error::not_singular_query:
            return "not a singular query";
        }
        return "unknown cbor error";
    }

    std::error_condition default_error_condition(int const condition) const noexcept override
    {
        switch (static_cast<error>(condition)) {
        case error::no_buffer_space:
            return std::make_error_condition(std::errc::no_buffer_space);
        case error::value_too_large:
            return std::make_error_condition(std::errc::value_too_large);
        case error::not_enough_memory:
            return std::make_error_condition(std::errc::not_enough_memory);
        case error::io_error:
            return std::make_error_condition(std::errc::io_error);
        default:
            return std::error_condition(condition, *this);
        }
    }
};

inline std::error_category const &category() noexcept
{
    static error_category const instance;
    return instance;
}

inline std::error_code make_error_code(error const e) noexcept
{
    return {static_cast<int>(e), category()};
}

template <class T>
class result;

template <class T>
class result_base : public std::expected<T, error>
{
    using base = std::expected<T, error>;

    template <class Self>
    using base_like = std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>,
                                         std::conditional_t<std::is_lvalue_reference_v<Self>, base const &, base const &&>,
                                         std::conditional_t<std::is_lvalue_reference_v<Self>, base &, base &&>>;

    template <class R>
    static constexpr std::expected<typename std::remove_cvref_t<R>::value_type, typename std::remove_cvref_t<R>::error_type>
    plain(R &&r)
    {
        return std::forward<R>(r);
    }

    template <class R>
    static constexpr auto wrapped(R &&r)
    {
        using X = std::remove_cvref_t<R>;
        if constexpr (std::same_as<typename X::error_type, error>)
            return result<typename X::value_type>(std::forward<R>(r));
        else
            return X(std::forward<R>(r));
    }

    template <class U>
    static constexpr bool is_result_or_expected = false;
    template <class U>
    static constexpr bool is_result_or_expected<result<U>> = true;
    template <class U, class G>
    static constexpr bool is_result_or_expected<std::expected<U, G>> = true;
    template <class G>
    static constexpr bool is_result_or_expected<std::unexpected<G>> = true;

    template <class>
    friend class result;

public:
    using base::base;

    constexpr result_base(base &&b) noexcept(std::is_nothrow_move_constructible_v<base>) : base(std::move(b))
    {
    }

    constexpr result_base(base const &b) noexcept(std::is_nothrow_copy_constructible_v<base>) : base(b)
    {
    }

    template <class Self, class F>
    constexpr auto and_then(this Self &&self, F &&f)
    {
        return wrapped(static_cast<base_like<Self>>(self).and_then([&f](auto &&...v) {
            return plain(std::invoke(std::forward<F>(f), std::forward<decltype(v)>(v)...));
        }));
    }

    template <class Self, class F>
    constexpr auto or_else(this Self &&self, F &&f)
    {
        return wrapped(static_cast<base_like<Self>>(self).or_else(
            [&f](auto &&e) { return plain(std::invoke(std::forward<F>(f), std::forward<decltype(e)>(e))); }));
    }

    template <class Self, class F>
    constexpr auto transform(this Self &&self, F &&f)
    {
        return wrapped(static_cast<base_like<Self>>(self).transform(std::forward<F>(f)));
    }

    template <class Self, class F>
    constexpr auto transform_error(this Self &&self, F &&f)
    {
        return wrapped(static_cast<base_like<Self>>(self).transform_error(std::forward<F>(f)));
    }

    template <std::same_as<result<T>> R>
    friend constexpr bool operator==(R const &x, R const &y)
    {
        return static_cast<base const &>(x) == static_cast<base const &>(y);
    }

    template <std::same_as<result<T>> R, class U, class G>
    friend constexpr bool operator==(R const &x, std::expected<U, G> const &y)
    {
        return static_cast<base const &>(x) == y;
    }

    template <std::same_as<result<T>> R, class G>
    friend constexpr bool operator==(R const &x, std::unexpected<G> const &e)
    {
        return static_cast<base const &>(x) == e;
    }

    template <class A, class B>
    static constexpr bool value_comparable = requires(A const &a, B const &b) {
        { a == b } -> std::convertible_to<bool>;
    };

    template <std::same_as<result<T>> R, class U>
        requires(!std::is_void_v<T> && !is_result_or_expected<U> && value_comparable<T, U>)
    friend constexpr bool operator==(R const &x, U const &v)
    {
        return static_cast<base const &>(x) == v;
    }
};

template <class T>
class result : public result_base<T>
{
    using base = std::expected<T, error>;

public:
    using result_base<T>::result_base;
    using base::operator*;
    using base::value;

    constexpr T operator*() && noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(*static_cast<base &>(*this));
    }

    constexpr T operator*() const && noexcept(std::is_nothrow_copy_constructible_v<T>)
    {
        return *static_cast<base const &>(*this);
    }

    constexpr T value() &&
    {
        return std::move(static_cast<base &>(*this)).value();
    }

    constexpr T value() const &&
    {
        return static_cast<base const &>(*this).value();
    }
};

template <>
class result<void> : public result_base<void>
{
public:
    using result_base<void>::result_base;
};

} // namespace cbor

template <>
struct std::is_error_code_enum<cbor::error> : std::true_type {};
