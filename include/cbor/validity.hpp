#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <limits>
#include <memory>
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

namespace cbor
{

enum class pass;

struct lazy;

class validity
{
    enum class simple_float_information : std::uint8_t {
        simple_value_follows = 24,
        half_precision_float,
        single_precision_float,
        double_precision_float,
        break_stop_code = 31
    };

    static constexpr std::uint8_t simple_value_one_byte_min = 32;

    enum class tag_number : std::uint64_t {
        unsigned_bignum = 2,
        negative_bignum = 3,
        encoded_cbor_data_item = 24,
        shareable = 28,
        sharedref = 29,
        typed_array_first = 64,
        typed_array_reserved = 76,
        float128_big_endian = 83,
        typed_array_last = 87,
        reference = 6,
        basic_packed_cbor = 113,
        record_function = 114,
        straight_argument_first = 128,
        straight_argument_last = 135,
        inverted_argument_first = 136,
        inverted_argument_last = 143,
        split_basic_packed_cbor = 1113
    };

public:
    static constexpr std::size_t nesting_depth_limit = 1024;

    static constexpr std::size_t nesting_depth_default = 128;

    static constexpr std::expected<void, error> check_nesting_depth(std::size_t const depth, std::size_t const depth_max)
    {
        if (depth > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        return {};
    }

    [[noreturn]] static void throw_logic_error(char const *const what)
    {
#if defined(__cpp_exceptions)
        throw std::logic_error(what);
#else
        (void)what;
        std::abort();
#endif
    }

    template <class T>
    static void throw_logic_error_if_empty(std::shared_ptr<T> const &owner, char const *const what)
    {
        if (owner.use_count() == 0) [[unlikely]]
            throw_logic_error(what);
    }

    template <class T>
    static void throw_logic_error_if_null(std::shared_ptr<T> const &pointer, char const *const what)
    {
        if (!pointer) [[unlikely]]
            throw_logic_error(what);
    }

    static constexpr std::expected<std::size_t, std::errc> checked_mul(std::size_t const a, std::size_t const b)
    {
        std::size_t product;
        if consteval {
            if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            product = a * b;
            return product;
        } else {
#ifdef __STDC_VERSION_STDCKDINT_H__
            if (ckd_mul(&product, a, b)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
#elif defined(__has_builtin)
#if __has_builtin(__builtin_mul_overflow)
            if (__builtin_mul_overflow(a, b, &product)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
#else
            if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            product = a * b;
#endif
#else
            if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            product = a * b;
#endif
            return product;
        }
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
        if (info == std::to_underlying(simple_float_information::simple_value_follows) &&
            argument < simple_value_one_byte_min) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    static constexpr std::expected<void, error> typed_array_check(std::uint64_t const tag, std::size_t const size)
    {
        if (tag < std::to_underlying(tag_number::typed_array_first) ||
            tag > std::to_underlying(tag_number::typed_array_last) ||
            tag == std::to_underlying(tag_number::typed_array_reserved)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::uint64_t const f = tag >> 4 & 1;
        std::uint64_t const ll = tag & 3;
        if (size % (std::uint64_t{1} << (f + ll)) != 0) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return {};
    }

    friend class heads;

    friend class decoding;

    friend struct lazy;

    friend class well_formedness;

    friend class value_sharing;

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
