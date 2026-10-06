#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#if defined(__cpp_exceptions)
#include <stdexcept>
#endif
#include <system_error>
#include <utility>
#if __has_include(<stdckdint.h>)
#include <stdckdint.h>
#endif

#include "binding.hpp"
#include "error.hpp"
#include "head.hpp"

namespace cbor
{

enum class pass;

struct lazy;

class validity
{
public:
    [[noreturn]] static void throw_logic_error(char const *const what)
    {
#if defined(__cpp_exceptions)
        throw std::logic_error(what);
#else
        (void)what;
        std::abort();
#endif
    }

    static constexpr error writer_error(std::errc const e) noexcept
    {
        if (e == std::errc::no_buffer_space)
            return error::no_buffer_space;
        if (e == std::errc::value_too_large)
            return error::value_too_large;
        if (e == std::errc::not_enough_memory)
            return error::not_enough_memory;
        return error::io_error;
    }

private:
    static constexpr std::expected<void, error> check_nesting_depth(std::size_t const depth, std::size_t const depth_max)
    {
        if (depth > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        return {};
    }

    static constexpr std::expected<std::size_t, std::errc> checked_add(std::size_t const a, std::size_t const b)
    {
        std::size_t sum;
        if consteval {
            sum = a + b;
            if (sum < a) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            return sum;
        } else {
#ifdef __STDC_VERSION_STDCKDINT_H__
            if (ckd_add(&sum, a, b)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
#elif defined(__has_builtin)
#if __has_builtin(__builtin_add_overflow)
            if (__builtin_add_overflow(a, b, &sum)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
#else
            sum = a + b;
            if (sum < a) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
#endif
#else
            sum = a + b;
            if (sum < a) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
#endif
            return sum;
        }
    }

    static constexpr std::expected<void, error> check_simple_value(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info == std::to_underlying(heads::simple_float_information::simple_value_follows) &&
            argument < heads::simple_value_one_byte_min) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    friend class decoding;

    friend class well_formedness;

    friend class diagnostic_notation;

    friend class jsonpath;

    template <std::size_t, class, class, pass>
    friend class walker;

#ifdef __cpp_impl_reflection
    friend class packed;

    friend class generic;

    template <class>
    friend class schema;

    template <class>
    friend class databind;
#endif
};

}
