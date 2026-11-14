// Converts floating-point numbers between binary16, binary32 and binary64 and gives the preferred width of a number.
// https://www.rfc-editor.org/rfc/rfc8949#section-3.3
// https://www.rfc-editor.org/rfc/rfc8949#appendix-D
#pragma once

#include <bit>
#include <cstdint>
#include <limits>
#include <utility>

#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

#include "rfc8949.hpp"

namespace cbor
{

namespace detail
{

class ieee754
{
public:
    struct precision {
        int significand_bits;
        int exponent_bias;
        std::uint32_t exponent_max;
    };

    static constexpr precision half_precision{.significand_bits = 10, .exponent_bias = 15, .exponent_max = 31};
    static constexpr precision single_precision{std::numeric_limits<float>::digits - 1,
                                                std::numeric_limits<float>::max_exponent - 1,
                                                2 * std::numeric_limits<float>::max_exponent - 1};
    static constexpr precision double_precision{std::numeric_limits<double>::digits - 1,
                                                std::numeric_limits<double>::max_exponent - 1,
                                                2 * std::numeric_limits<double>::max_exponent - 1};

    static constexpr float float_decode_binary16(std::uint16_t half)
    {
        constexpr precision h = half_precision;
        constexpr precision f = single_precision;
        constexpr int widen = f.significand_bits - h.significand_bits;
        std::uint32_t const sign =
            static_cast<std::uint32_t>(half >> (h.significand_bits + std::bit_width(h.exponent_max)))
            << (f.significand_bits + std::bit_width(f.exponent_max));
        std::uint32_t const exp = half >> h.significand_bits & h.exponent_max;
        std::uint32_t const frac = half & ((1u << h.significand_bits) - 1u);
        if (exp == h.exponent_max)
            return std::bit_cast<float>(sign | f.exponent_max << f.significand_bits | frac << widen);
#if defined(__STDCPP_FLOAT16_T__)
        if !consteval {
            return static_cast<float>(std::bit_cast<std::float16_t>(half));
        }
#elif defined(__FLT16_MAX__) && !defined(_MSC_VER)
        if !consteval {
            return static_cast<float>(std::bit_cast<_Float16>(half));
        }
#endif
        if (exp != 0)
            return std::bit_cast<float>(
                sign | (exp + f.exponent_bias - h.exponent_bias) << f.significand_bits | frac << widen);
        if (frac == 0)
            return std::bit_cast<float>(sign);
        int const shift = std::countl_zero(static_cast<std::uint16_t>(
            frac << (std::numeric_limits<std::uint16_t>::digits - 1 - h.significand_bits)));
        std::uint32_t const mant = (frac << shift) & ((1u << h.significand_bits) - 1u);
        return std::bit_cast<float>(
            sign |
            static_cast<std::uint32_t>(f.exponent_bias - (h.exponent_bias - 1) - shift)
                << f.significand_bits |
            mant << widen);
    }

    static constexpr std::uint16_t float_encode_binary16(float value)
    {
        constexpr precision h = half_precision;
        constexpr precision f = single_precision;
        constexpr int narrow = f.significand_bits - h.significand_bits;
        std::uint32_t const bits = std::bit_cast<std::uint32_t>(value);
        std::uint32_t const sign = bits >> (f.significand_bits + std::bit_width(f.exponent_max))
                                               << (h.significand_bits + std::bit_width(h.exponent_max));
        std::uint32_t const exp32 = bits >> f.significand_bits & f.exponent_max;
        std::uint32_t const mant32 = bits & ((1u << f.significand_bits) - 1u);
        if (exp32 == f.exponent_max) {
            if (mant32 != 0)
                return static_cast<std::uint16_t>(h.exponent_max << h.significand_bits |
                                                  1u << (h.significand_bits - 1));
            return static_cast<std::uint16_t>(sign | h.exponent_max << h.significand_bits);
        }
        constexpr std::uint32_t subnormal_exponent_min =
            f.exponent_bias - (h.exponent_bias - 1) - h.significand_bits;
        if (exp32 < subnormal_exponent_min)
            return static_cast<std::uint16_t>(sign);
        if (exp32 > static_cast<std::uint32_t>(f.exponent_bias - h.exponent_bias))
            return static_cast<std::uint16_t>(
                sign | (exp32 - (f.exponent_bias - h.exponent_bias)) << h.significand_bits |
                mant32 >> narrow);
        return static_cast<std::uint16_t>(sign | ((1u << f.significand_bits) | mant32) >>
                                                     (f.exponent_bias - 1 - static_cast<int>(exp32)));
    }

    static constexpr rfc8949::simple_information preferred_float_info(double value)
    {
        constexpr precision h = half_precision;
        constexpr precision f = single_precision;
        constexpr precision d = double_precision;
        std::uint64_t const bits = std::bit_cast<std::uint64_t>(value);
        std::uint32_t const exp = static_cast<std::uint32_t>(bits >> d.significand_bits & d.exponent_max);
        constexpr int narrow = d.significand_bits - f.significand_bits;
        std::uint64_t const mant = bits & ((std::uint64_t{1} << d.significand_bits) - 1u);
        if (is_nan(d, bits)) [[unlikely]]
            return preferred_nan_info(bits);
        if (exp == d.exponent_max)
            return rfc8949::simple_information::half_precision_float;
        if ((mant & ((std::uint64_t{1} << narrow) - 1u)) != 0)
            return rfc8949::simple_information::double_precision_float;
        if (exp == 0)
            return mant == 0 ? rfc8949::simple_information::half_precision_float
                             : rfc8949::simple_information::double_precision_float;
        constexpr std::uint32_t normal_min = d.exponent_bias - f.exponent_bias + 1;
        if (exp < normal_min)
            return exp + f.significand_bits >= normal_min &&
                           (mant & ((std::uint64_t{1} << (narrow + normal_min - exp)) - 1u)) == 0
                       ? rfc8949::simple_information::single_precision_float
                       : rfc8949::simple_information::double_precision_float;
        if (exp > static_cast<std::uint32_t>(d.exponent_bias + f.exponent_bias))
            return rfc8949::simple_information::double_precision_float;
        std::uint32_t const mant32 = static_cast<std::uint32_t>(mant >> narrow);
        if (exp >= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias + 1) &&
            exp <= static_cast<std::uint32_t>(d.exponent_bias + h.exponent_bias))
            return (mant32 & ((1u << (f.significand_bits - h.significand_bits)) - 1u)) == 0
                       ? rfc8949::simple_information::half_precision_float
                       : rfc8949::simple_information::single_precision_float;
        if (exp >= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias - h.significand_bits + 1) &&
            exp <= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias))
            return (mant32 & ((1u << (d.exponent_bias - 1 - static_cast<int>(exp))) - 1u)) == 0
                       ? rfc8949::simple_information::half_precision_float
                       : rfc8949::simple_information::single_precision_float;
        return rfc8949::simple_information::single_precision_float;
    }

    static constexpr rfc8949::simple_information preferred_float_info(float const value)
    {
        std::uint32_t const bits = std::bit_cast<std::uint32_t>(value);
        if (is_nan(single_precision, bits)) [[unlikely]]
            return preferred_nan_info(std::bit_cast<std::uint64_t>(decode_nan(single_precision, bits)));
        return preferred_float_info(static_cast<double>(value));
    }

    static constexpr rfc8949::simple_information preferred_nan_info(std::uint64_t const bits)
    {
        constexpr precision d = double_precision;
        auto const low_bits_zero = [bits](precision const p) {
            return (bits & ((std::uint64_t{1} << (d.significand_bits - p.significand_bits)) - 1u)) == 0;
        };
        if (low_bits_zero(half_precision))
            return rfc8949::simple_information::half_precision_float;
        if (low_bits_zero(single_precision))
            return rfc8949::simple_information::single_precision_float;
        return rfc8949::simple_information::double_precision_float;
    }

    static constexpr std::uint64_t encode_nan(rfc8949::simple_information const info, std::uint64_t const bits)
    {
        constexpr precision d = double_precision;
        if (info == rfc8949::simple_information::double_precision_float)
            return bits;
        precision const p =
            info == rfc8949::simple_information::half_precision_float ? half_precision : single_precision;
        return bits >> (d.significand_bits + std::bit_width(d.exponent_max))
                           << (p.significand_bits + std::bit_width(p.exponent_max)) |
               std::uint64_t{p.exponent_max} << p.significand_bits |
               (bits & ((std::uint64_t{1} << d.significand_bits) - 1u)) >> (d.significand_bits - p.significand_bits);
    }

    static constexpr std::uint64_t encode_float(rfc8949::simple_information const info,
                                                float const value)
    {
        std::uint32_t const bits = std::bit_cast<std::uint32_t>(value);
        if (is_nan(single_precision, bits)) [[unlikely]]
            return encode_nan(info, std::bit_cast<std::uint64_t>(decode_nan(single_precision, bits)));
        return encode_float(info, static_cast<double>(value));
    }

    static constexpr std::uint64_t encode_float(rfc8949::simple_information const info,
                                                double const value)
    {
        constexpr precision d = double_precision;
        std::uint64_t const bits = std::bit_cast<std::uint64_t>(value);
        if (is_nan(d, bits)) [[unlikely]]
            return encode_nan(info, bits);
        switch (info) {
        case rfc8949::simple_information::half_precision_float:
            return float_encode_binary16(static_cast<float>(value));
        case rfc8949::simple_information::single_precision_float:
            return std::bit_cast<std::uint32_t>(static_cast<float>(value));
        default:
            return std::bit_cast<std::uint64_t>(value);
        }
    }

    static constexpr bool is_nan(precision const p, std::uint64_t const argument)
    {
        return (argument >> p.significand_bits & p.exponent_max) == p.exponent_max &&
               (argument & ((std::uint64_t{1} << p.significand_bits) - 1u)) != 0;
    }

    static constexpr std::uint64_t significand_zero_pad(precision const p, std::uint64_t const argument)
    {
        constexpr precision d = double_precision;
        return (argument >> (p.significand_bits + std::bit_width(p.exponent_max)))
                   << (d.significand_bits + std::bit_width(d.exponent_max)) |
               (argument & ((std::uint64_t{1} << p.significand_bits) - 1u))
                   << (d.significand_bits - p.significand_bits);
    }

    static constexpr double decode_nan(precision const p, std::uint64_t const argument)
    {
        constexpr precision d = double_precision;
        constexpr std::uint64_t exponent = std::uint64_t{d.exponent_max} << d.significand_bits;
        return std::bit_cast<double>(significand_zero_pad(p, argument) | exponent);
    }

    static constexpr double decode_float(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info == std::to_underlying(rfc8949::simple_information::half_precision_float)) {
            if (is_nan(half_precision, argument)) [[unlikely]]
                return decode_nan(half_precision, argument);
            return static_cast<double>(float_decode_binary16(static_cast<std::uint16_t>(argument)));
        }
        if (info == std::to_underlying(rfc8949::simple_information::single_precision_float)) {
            if (is_nan(single_precision, argument)) [[unlikely]]
                return decode_nan(single_precision, argument);
            return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(argument)));
        }
        return std::bit_cast<double>(argument);
    }

    struct float_key {
        bool nan;
        std::uint64_t widened;
        double value;
    };

    static constexpr float_key float_key_of(std::uint8_t const info, std::uint64_t const argument)
    {
        precision const p =
            info == std::to_underlying(rfc8949::simple_information::half_precision_float)
                ? half_precision
            : info == std::to_underlying(rfc8949::simple_information::single_precision_float)
                ? single_precision
                : double_precision;
        return {is_nan(p, argument), significand_zero_pad(p, argument), decode_float(info, argument)};
    }
};

}

}
