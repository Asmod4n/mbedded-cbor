#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "binding.hpp"
#include "error.hpp"
#include "rfc8746.hpp"
#include "rfc8949.hpp"
#include "validity.hpp"

namespace cbor
{

#ifdef __SIZEOF_INT128__
__extension__ typedef __int128 int128;
__extension__ typedef unsigned __int128 uint128;
#endif

enum class pass;

struct lazy;

template <std::size_t DepthMax = validity::nesting_depth_default>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<std::string, error> inspect(std::string_view encoded);

class heads
{
    template <class V>
    static V unsigned_read(std::span<char const, sizeof(V)> const field)
    {
        std::array<char, sizeof(V)> big;
        std::ranges::copy(field, big.begin());
        return std::byteswap(std::bit_cast<V>(big));
    }

    static constexpr std::size_t initial_byte_size = 1;

    static constexpr char initial_byte(major_type const major, std::uint64_t const info)
    {
        return static_cast<char>(std::uint64_t{std::to_underlying(major)} << 5 | info);
    }

    static constexpr std::uint8_t preferred_argument_info(std::uint64_t const argument)
    {
        if (argument < std::to_underlying(rfc8949::additional_information::one_byte_argument))
            return static_cast<std::uint8_t>(argument);
        return static_cast<std::uint8_t>(
            std::to_underlying(rfc8949::additional_information::one_byte_argument) +
            std::countr_zero(std::bit_ceil(static_cast<unsigned>((std::bit_width(argument) + 7) / 8))));
    }

    static constexpr std::size_t argument_size(std::uint8_t const info)
    {
        if (info < std::to_underlying(rfc8949::additional_information::one_byte_argument))
            return 0;
        return std::size_t{1} << (info -
                                  std::to_underlying(rfc8949::additional_information::one_byte_argument));
    }

    static constexpr std::size_t head_size(std::uint64_t const argument)
    {
        return initial_byte_size + argument_size(preferred_argument_info(argument));
    }

    template <std::unsigned_integral V>
    static constexpr std::array<char, sizeof(V)> big_endian(V const value)
    {
        return std::bit_cast<std::array<char, sizeof(V)>>(std::byteswap(value));
    }

    static constexpr std::size_t head_padding = sizeof(std::uint64_t);

    template <bool Exact = false>
    CBOR_ALWAYS_INLINE static std::size_t head_write(std::span<char> const out, std::size_t const at, major_type const major,
                                                     std::uint8_t const info, std::uint64_t const argument)
    {
        static_assert(argument_size(std::to_underlying(
                          rfc8949::additional_information::eight_byte_argument)) <= sizeof(std::uint64_t),
                      "The widest argument must fit in the bytes of one std::uint64_t.");
        static_assert(head_padding >= sizeof(std::uint64_t),
                      "The padding after the last head must hold the bytes that a head writes past its own size.");
        std::size_t const width = argument_size(info);
        auto const bytes = big_endian(argument << ((64 - 8 * width) & 63));
        if constexpr (Exact) {
            if (out.size() - at < initial_byte_size + sizeof(std::uint64_t)) [[unlikely]] {
                auto const field = out.subspan(at, initial_byte_size + width);
                field.front() = initial_byte(major, info);
                std::copy_n(bytes.begin(), width, field.subspan(initial_byte_size).begin());
                return initial_byte_size + width;
            }
        }
        auto const field = out.subspan(at).template first<initial_byte_size + sizeof(std::uint64_t)>();
        field.front() = initial_byte(major, info);
        std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(std::uint64_t)>().begin());
        return initial_byte_size + width;
    }

    template <bool Exact = false>
    CBOR_ALWAYS_INLINE static std::size_t head_write(std::span<char> const out, std::size_t const at, major_type const major,
                                                     std::uint64_t const argument)
    {
        return head_write<Exact>(out, at, major, preferred_argument_info(argument), argument);
    }

    template <class Container>
    static constexpr void head_append(Container &out, major_type const major, std::uint8_t const info,
                                      std::uint64_t const argument)
    {
        out.push_back(initial_byte(major, info));
        for (std::size_t i = argument_size(info); i-- > 0;)
            out.push_back(static_cast<char>(argument >> (8 * i)));
    }

    template <class Container>
    static constexpr void head_append(Container &out, major_type const major, std::uint64_t const argument)
    {
        head_append(out, major, preferred_argument_info(argument), argument);
    }

#ifdef __cpp_impl_reflection
    static constexpr int extended_precision_digits = 64;

#ifdef __SIZEOF_INT128__
    static constexpr std::array<char, sizeof(uint128)> big_endian(uint128 const value)
    {
        auto const high = big_endian(static_cast<std::uint64_t>(value >> 64));
        auto const low = big_endian(static_cast<std::uint64_t>(value));
        std::array<char, sizeof(uint128)> bytes;
        std::ranges::copy(high, bytes.begin());
        std::ranges::copy(low, std::ranges::next(bytes.begin(), sizeof(std::uint64_t)));
        return bytes;
    }
#endif

    CBOR_ALWAYS_INLINE static void u32_write(std::span<char> const out, std::size_t const at, std::size_t const value)
    {
        auto const b = big_endian(static_cast<std::uint32_t>(value));
        std::ranges::copy(b, out.subspan(at).template first<sizeof(std::uint32_t)>().begin());
    }

    CBOR_ALWAYS_INLINE static void item_head_write(std::span<char> const out, std::size_t const at, major_type const major,
                                                  std::size_t const length)
    {
        out[at] =
            initial_byte(major, std::to_underlying(rfc8949::additional_information::four_byte_argument));
        u32_write(out, at + 1, length);
    }

#ifdef __SIZEOF_INT128__
    static uint128 unsigned128_read(std::span<char const, sizeof(uint128)> const field)
    {
        auto const high = static_cast<uint128>(unsigned_read<std::uint64_t>(field.first<sizeof(std::uint64_t)>()));
        auto const low = static_cast<uint128>(unsigned_read<std::uint64_t>(field.last<sizeof(std::uint64_t)>()));
        return high << 64 | low;
    }
#endif
#endif

    static std::string_view magnitude_without_leading_zeros(std::string_view const magnitude)
    {
        std::size_t const first = magnitude.find_first_not_of('\0');
        return first == std::string_view::npos ? std::string_view{} : std::string_view(std::span(magnitude).subspan(first));
    }

    static std::uint64_t magnitude_value(std::string_view const magnitude)
    {
        std::uint64_t n = 0;
        for (char const c : magnitude)
            n = n << 8 | static_cast<std::uint8_t>(c);
        return n;
    }

    static std::string magnitude_plus_one(std::string_view const magnitude)
    {
        std::string sum(1, '\0');
        sum.append(magnitude);
        for (auto digit = sum.rbegin(); digit != sum.rend(); ++digit) {
            *digit = static_cast<char>(static_cast<std::uint8_t>(*digit) + 1);
            if (*digit != '\0')
                break;
        }
        return std::string(magnitude_without_leading_zeros(sum));
    }

    static std::string magnitude_minus_one(std::string_view const magnitude)
    {
        std::string difference(magnitude);
        for (auto digit = difference.rbegin(); digit != difference.rend(); ++digit) {
            *digit = static_cast<char>(static_cast<std::uint8_t>(*digit) - 1);
            if (*digit != '\xff')
                break;
        }
        return std::string(magnitude_without_leading_zeros(difference));
    }

    struct head {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
    };

    struct decoder {
        std::string_view encoded;
        CBOR_ALWAYS_INLINE std::expected<head, error> head_decode()
        {
            if (encoded.empty()) [[unlikely]]
                return std::unexpected(error::too_little_data);
            auto const initial = static_cast<std::uint8_t>(encoded.front());
            auto const major = static_cast<major_type>(initial >> 5);
            std::uint8_t const info = initial & 0x1f;
            if (info < std::to_underlying(rfc8949::additional_information::one_byte_argument)) {
                encoded.remove_prefix(1);
                return head{major, info, info};
            }
            if (error const r = validity::check_definite_length(major, info).error_or(error{}); r != error{})
                [[unlikely]]
                return std::unexpected(r);
            static_assert(argument_size(std::to_underlying(
                              rfc8949::additional_information::two_byte_argument)) >= sizeof(std::uint16_t),
                          "A two-byte argument must cover the bytes of one std::uint16_t.");
            static_assert(argument_size(std::to_underlying(
                              rfc8949::additional_information::four_byte_argument)) >= sizeof(std::uint32_t),
                          "A four-byte argument must cover the bytes of one std::uint32_t.");
            static_assert(argument_size(std::to_underlying(
                              rfc8949::additional_information::eight_byte_argument)) >= sizeof(std::uint64_t),
                          "An eight-byte argument must cover the bytes of one std::uint64_t.");
            std::size_t const size = argument_size(info);
            if (encoded.size() < 1 + size) [[unlikely]]
                return std::unexpected(error::too_little_data);
            std::span<char const> const rest = std::span(encoded).subspan(1, size);
            std::uint64_t argument;
            switch (static_cast<rfc8949::additional_information>(info)) {
            case rfc8949::additional_information::one_byte_argument:
                argument = static_cast<std::uint8_t>(rest.front());
                break;
            case rfc8949::additional_information::two_byte_argument:
                argument = unsigned_read<std::uint16_t>(rest.first<2>());
                break;
            case rfc8949::additional_information::four_byte_argument:
                argument = unsigned_read<std::uint32_t>(rest.first<4>());
                break;
            default:
                argument = unsigned_read<std::uint64_t>(rest.first<8>());
                break;
            }
            encoded.remove_prefix(1 + size);
            return head{major, info, argument};
        }

        constexpr std::expected<std::string_view, error> byte_string_decode(std::uint64_t const length)
        {
            if (encoded.size() < length) [[unlikely]]
                return std::unexpected(error::too_little_data);
            std::string_view const string{std::span(encoded).first(length)};
            encoded.remove_prefix(length);
            return string;
        }
    };

    struct precision {
        int significand_bits;
        int exponent_bias;
        std::uint32_t exponent_max;
    };

    static constexpr precision half_precision{10, 15, 31};
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
        if (exp32 == 0)
            return static_cast<std::uint16_t>(sign);
        if (exp32 > static_cast<std::uint32_t>(f.exponent_bias - h.exponent_bias))
            return static_cast<std::uint16_t>(
                sign | (exp32 - (f.exponent_bias - h.exponent_bias)) << h.significand_bits |
                mant32 >> narrow);
        return static_cast<std::uint16_t>(sign | ((1u << f.significand_bits) | mant32) >>
                                                     (f.exponent_bias - 1 - static_cast<int>(exp32)));
    }

    static constexpr rfc8949::simple_float_information preferred_float_info(double value)
    {
        constexpr precision h = half_precision;
        constexpr precision f = single_precision;
        constexpr precision d = double_precision;
        std::uint64_t const bits = std::bit_cast<std::uint64_t>(value);
        std::uint32_t const exp = static_cast<std::uint32_t>(bits >> d.significand_bits & d.exponent_max);
        constexpr int narrow = d.significand_bits - f.significand_bits;
        std::uint64_t const mant = bits & ((std::uint64_t{1} << d.significand_bits) - 1u);
        if (exp == d.exponent_max)
            return rfc8949::simple_float_information::half_precision_float;
        if ((mant & ((std::uint64_t{1} << narrow) - 1u)) != 0)
            return rfc8949::simple_float_information::double_precision_float;
        if (exp == 0)
            return mant == 0 ? rfc8949::simple_float_information::half_precision_float
                             : rfc8949::simple_float_information::double_precision_float;
        constexpr std::uint32_t normal_min = d.exponent_bias - f.exponent_bias + 1;
        if (exp < normal_min)
            return exp + f.significand_bits >= normal_min &&
                           (mant & ((std::uint64_t{1} << (narrow + normal_min - exp)) - 1u)) == 0
                       ? rfc8949::simple_float_information::single_precision_float
                       : rfc8949::simple_float_information::double_precision_float;
        if (exp > static_cast<std::uint32_t>(d.exponent_bias + f.exponent_bias))
            return rfc8949::simple_float_information::double_precision_float;
        std::uint32_t const mant32 = static_cast<std::uint32_t>(mant >> narrow);
        if (exp >= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias + 1) &&
            exp <= static_cast<std::uint32_t>(d.exponent_bias + h.exponent_bias))
            return (mant32 & ((1u << (f.significand_bits - h.significand_bits)) - 1u)) == 0
                       ? rfc8949::simple_float_information::half_precision_float
                       : rfc8949::simple_float_information::single_precision_float;
        if (exp >= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias - h.significand_bits + 1) &&
            exp <= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias))
            return (mant32 & ((1u << (d.exponent_bias - 1 - static_cast<int>(exp))) - 1u)) == 0
                       ? rfc8949::simple_float_information::half_precision_float
                       : rfc8949::simple_float_information::single_precision_float;
        return rfc8949::simple_float_information::single_precision_float;
    }

    static constexpr std::uint64_t float_encode(rfc8949::simple_float_information const info,
                                                double const value)
    {
        switch (info) {
        case rfc8949::simple_float_information::half_precision_float:
            return float_encode_binary16(static_cast<float>(value));
        case rfc8949::simple_float_information::single_precision_float:
            return std::bit_cast<std::uint32_t>(static_cast<float>(value));
        default:
            return std::bit_cast<std::uint64_t>(value);
        }
    }

    static constexpr double float_decode(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info == std::to_underlying(rfc8949::simple_float_information::half_precision_float))
            return static_cast<double>(float_decode_binary16(static_cast<std::uint16_t>(argument)));
        if (info == std::to_underlying(rfc8949::simple_float_information::single_precision_float))
            return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(argument)));
        return std::bit_cast<double>(argument);
    }

    struct float_key {
        bool nan;
        std::uint64_t widened;
        double value;
    };

    static constexpr float_key float_key_of(std::uint8_t const info, std::uint64_t const argument)
    {
        constexpr precision h = half_precision;
        constexpr precision f = single_precision;
        constexpr precision d = double_precision;
        double const value = float_decode(info, argument);
        if (info == std::to_underlying(rfc8949::simple_float_information::half_precision_float))
            return {(argument >> h.significand_bits & h.exponent_max) == h.exponent_max &&
                        (argument & ((std::uint64_t{1} << h.significand_bits) - 1u)) != 0,
                    (argument >> (h.significand_bits + std::bit_width(h.exponent_max)))
                            << (d.significand_bits + std::bit_width(d.exponent_max)) |
                        (argument & ((std::uint64_t{1} << h.significand_bits) - 1u))
                            << (d.significand_bits - h.significand_bits),
                    value};
        if (info == std::to_underlying(rfc8949::simple_float_information::single_precision_float))
            return {(argument >> f.significand_bits & f.exponent_max) == f.exponent_max &&
                        (argument & ((std::uint64_t{1} << f.significand_bits) - 1u)) != 0,
                    (argument >> (f.significand_bits + std::bit_width(f.exponent_max)))
                            << (d.significand_bits + std::bit_width(d.exponent_max)) |
                        (argument & ((std::uint64_t{1} << f.significand_bits) - 1u))
                            << (d.significand_bits - f.significand_bits),
                    value};
        return {(argument >> d.significand_bits & d.exponent_max) == d.exponent_max &&
                    (argument & ((std::uint64_t{1} << d.significand_bits) - 1u)) != 0,
                (argument >> (d.significand_bits + std::bit_width(d.exponent_max)))
                        << (d.significand_bits + std::bit_width(d.exponent_max)) |
                    (argument & ((std::uint64_t{1} << d.significand_bits) - 1u)),
                value};
    }

    static constexpr bool is_boolean(head const &h)
    {
        return h.major == major_type::simple_float &&
               (h.info == std::to_underlying(simple_value::false_value) || h.info == std::to_underlying(simple_value::true_value));
    }

    static constexpr bool is_null(head const &h)
    {
        return h.major == major_type::simple_float && h.info == std::to_underlying(simple_value::null);
    }

    struct raw_head {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
        std::size_t at;
    };

    CBOR_ALWAYS_INLINE static constexpr std::expected<raw_head, error>
    raw_head_read(std::string_view const encoded, std::size_t const at)
    {
        if (at >= encoded.size()) [[unlikely]]
            return std::unexpected(error::too_little_data);
        auto const initial = static_cast<std::uint8_t>(encoded[at]);
        auto const major = static_cast<major_type>(initial >> 5);
        std::uint8_t const info = initial & 0x1f;
        if (info < std::to_underlying(rfc8949::additional_information::one_byte_argument))
            return raw_head{major, info, info, at + 1};
        if (error const r = validity::check_additional_information(major, info).error_or(error{});
            r != error{}) [[unlikely]]
            return std::unexpected(r);
        if (info == std::to_underlying(rfc8949::additional_information::indefinite_length))
            return raw_head{major, info, info, at + 1};
        std::size_t const size = argument_size(info);
        if (encoded.size() - at - 1 < size) [[unlikely]]
            return std::unexpected(error::too_little_data);
        std::uint64_t argument = 0;
        for (char const c : std::span(encoded).subspan(at + 1, size))
            argument = argument << 8 | static_cast<std::uint8_t>(c);
        return raw_head{major, info, argument, at + 1 + size};
    }

    static constexpr bool break_at(std::string_view const encoded, std::size_t const at)
    {
        return at < encoded.size() &&
               encoded[at] ==
                   initial_byte(major_type::simple_float,
                                std::to_underlying(rfc8949::simple_float_information::break_stop_code));
    }

    friend class decoding;

    friend class encoding;

    friend class well_formedness;

    friend class value_sharing;

    friend class diagnostic_notation;

    friend class jsonpath;

    template <class Writer>
    friend struct encoder;

    template <std::size_t, class, class, pass>
    friend class walker;

    friend struct lazy;

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

    template <std::size_t DepthMax>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    friend std::expected<std::size_t, error> item_end(std::string_view encoded);

    template <std::size_t DepthMax>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    friend std::expected<std::string, error> inspect(std::string_view encoded);

    template <std::size_t DepthMax, class Binding>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    friend std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l);

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
