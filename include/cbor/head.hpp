// Reads and writes the head of a CBOR data item: the initial byte
// and the argument. Reads the magnitude of a bignum and one element of
// a typed array. Skips the self-described CBOR tag.
// https://www.rfc-editor.org/rfc/rfc8949#section-3
// https://www.rfc-editor.org/rfc/rfc8746#section-2
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
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

#include "ieee754.hpp"
#include "rfc8746.hpp"
#include "error.hpp"
#include "rfc8949.hpp"
#include "validity.hpp"

namespace cbor
{

namespace detail
{

class encoding;
class extended_diagnostic_notation;
class generic;
class reflection;
class well_formedness;

}

template <class T>
class schema;

#if defined(__SIZEOF_INT128__) && !defined(_MSVC_STL_VERSION)
#define CBOR_HAS_INT128 1
#endif

#ifdef CBOR_HAS_INT128
__extension__ typedef __int128 int128;
__extension__ typedef unsigned __int128 uint128;
#endif

struct lazy;
struct item;
struct lazy_elements;
struct lazy_entries;
class path;
class sequence_reader;

template <class>
class structs;

template <class>
class owning;

namespace detail
{

template <bool>
struct tree_decoder;

}

std::expected<owning<item>, error> decode(std::string_view encoded);

std::expected<std::size_t, error> item_size(std::string_view encoded);

std::expected<std::string, error> diagnostic_notation(std::string_view encoded);

namespace detail
{

class heads
{
    template <class V>
    static constexpr V read_unsigned(std::span<char const, sizeof(V)> const field)
    {
        std::array<char, sizeof(V)> big;
        std::copy(field.begin(), field.end(), big.begin());
        return std::byteswap(std::bit_cast<V>(big));
    }

    static constexpr std::size_t initial_byte_size = 1;

    static constexpr std::uint8_t additional_information_of(std::uint8_t const initial)
    {
        return initial & ((1 << rfc8949::additional_information_bits) - 1);
    }

    static constexpr char initial_byte(major_type const major, std::uint64_t const info)
    {
        return static_cast<char>(std::uint64_t{std::to_underlying(major)} << rfc8949::additional_information_bits | info);
    }

    static constexpr std::uint8_t preferred_argument_info(std::uint64_t const argument)
    {
        if (argument < std::to_underlying(rfc8949::additional_information::one_byte_argument))
            return static_cast<std::uint8_t>(argument);
        return static_cast<std::uint8_t>(
            std::to_underlying(rfc8949::additional_information::one_byte_argument) +
            std::countr_zero(std::bit_ceil(static_cast<unsigned>(
                (std::bit_width(argument) + std::numeric_limits<std::uint8_t>::digits - 1) /
                std::numeric_limits<std::uint8_t>::digits))));
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

    static constexpr std::expected<std::size_t, error> padded_size(std::size_t const size)
    {
        auto const sum = detail::validity::checked_add(size, head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(sum.error()));
        return *sum;
    }

    static constexpr std::size_t head_size_max = initial_byte_size + sizeof(std::uint64_t);

    CBOR_ALWAYS_INLINE static constexpr std::size_t head_encode(std::span<char, head_size_max> const field,
                                                                major_type const major, std::uint8_t const info,
                                                                std::uint64_t const argument)
    {
        static_assert(argument_size(std::to_underlying(
                          rfc8949::additional_information::eight_byte_argument)) <= sizeof(std::uint64_t),
                      "The widest argument must fit in the bytes of one std::uint64_t.");
        std::size_t const width = argument_size(info);
        auto const bytes = big_endian(argument << ((std::numeric_limits<std::uint64_t>::digits -
                                                    std::numeric_limits<std::uint8_t>::digits * width) &
                                                   (std::numeric_limits<std::uint64_t>::digits - 1)));
        field.front() = initial_byte(major, info);
        std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(std::uint64_t)>().begin());
        return initial_byte_size + width;
    }

    CBOR_ALWAYS_INLINE static std::size_t write_head(std::span<char> const out, std::size_t const at, major_type const major,
                                                     std::uint8_t const info, std::uint64_t const argument)
    {
        static_assert(head_padding >= sizeof(std::uint64_t),
                      "The padding after the last head must hold the bytes that a head writes past its own size.");
        if (detail::validity::bytes_fit(head_size_max, out.size() - at)) [[likely]]
            return head_encode(out.subspan(at).template first<head_size_max>(), major, info, argument);
        std::size_t const width = argument_size(info);
        std::span<char> const field = out.subspan(at, initial_byte_size + width);
        field.front() = initial_byte(major, info);
        auto const bytes = big_endian(argument);
        std::ranges::copy(std::span(bytes).last(width), field.subspan(initial_byte_size).begin());
        return initial_byte_size + width;
    }

    CBOR_ALWAYS_INLINE static std::size_t write_head(std::span<char> const out, std::size_t const at, major_type const major,
                                                     std::uint64_t const argument)
    {
        return write_head(out, at, major, preferred_argument_info(argument), argument);
    }

    template <class Container>
    static constexpr void append_head(Container &out, major_type const major, std::uint8_t const info,
                                      std::uint64_t const argument)
    {
        out.push_back(initial_byte(major, info));
        for (std::size_t i = argument_size(info); i-- > 0;)
            out.push_back(static_cast<char>(argument >> (std::numeric_limits<std::uint8_t>::digits * i)));
    }

    template <class Container>
    static constexpr void append_head(Container &out, major_type const major, std::uint64_t const argument)
    {
        append_head(out, major, preferred_argument_info(argument), argument);
    }

#ifdef CBOR_HAS_INT128
    static constexpr std::array<char, sizeof(uint128)> big_endian(uint128 const value)
    {
        auto const high =
            big_endian(static_cast<std::uint64_t>(value >> std::numeric_limits<std::uint64_t>::digits));
        auto const low = big_endian(static_cast<std::uint64_t>(value));
        std::array<char, sizeof(uint128)> bytes;
        std::ranges::copy(high, bytes.begin());
        std::ranges::copy(low, std::ranges::next(bytes.begin(), sizeof(std::uint64_t)));
        return bytes;
    }
#endif

    static constexpr int extended_precision_digits = std::numeric_limits<std::uint64_t>::digits;

    CBOR_ALWAYS_INLINE static void write_u32(std::span<char> const out, std::size_t const at, std::size_t const value)
    {
        auto const b = big_endian(static_cast<std::uint32_t>(value));
        std::ranges::copy(b, out.subspan(at).template first<sizeof(std::uint32_t)>().begin());
    }

    CBOR_ALWAYS_INLINE static void write_item_head(std::span<char> const out, std::size_t const at, major_type const major,
                                                  std::size_t const length)
    {
        out[at] =
            initial_byte(major, std::to_underlying(rfc8949::additional_information::four_byte_argument));
        write_u32(out, at + initial_byte_size, length);
    }

#ifdef CBOR_HAS_INT128
    static uint128 read_unsigned128(std::span<char const, sizeof(uint128)> const field)
    {
        auto const high = static_cast<uint128>(read_unsigned<std::uint64_t>(field.first<sizeof(std::uint64_t)>()));
        auto const low = static_cast<uint128>(read_unsigned<std::uint64_t>(field.last<sizeof(std::uint64_t)>()));
        return high << std::numeric_limits<std::uint64_t>::digits | low;
    }
#endif

    template <class E>
        requires std::same_as<E, char> || std::same_as<E, std::byte>
    static std::span<E const> magnitude_without_leading_zeros(std::span<E const> const magnitude)
    {
        return std::span<E const>(
            std::ranges::subrange(std::ranges::find_if(magnitude, [](E const digit) { return digit != E{}; }), magnitude.end()));
    }

    template <class U, class E>
        requires std::same_as<E, char> || std::same_as<E, std::byte>
    static U magnitude_value(std::span<E const> const magnitude)
    {
        U n = 0;
        for (E const c : magnitude)
            n = n << std::numeric_limits<std::uint8_t>::digits | static_cast<std::uint8_t>(c);
        return n;
    }

    template <class T>
    CBOR_ALWAYS_INLINE static constexpr T subtract_argument_from_minus_one(std::uint64_t const argument)
    {
        if constexpr (std::same_as<T, double>)
            return -static_cast<double>(argument == std::numeric_limits<std::uint64_t>::max() ? argument
                                                                                              : argument + 1);
        else
            return static_cast<T>(~static_cast<T>(argument));
    }

    struct head {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
    };

    template <class E>
        requires std::same_as<E, char> || std::same_as<E, std::byte>
    CBOR_ALWAYS_INLINE static std::optional<head> convert_bignum_to_integer(std::uint64_t const tag, std::span<E const> const magnitude)
    {
        std::span<E const> const digits = magnitude_without_leading_zeros(magnitude);
        if (!detail::validity::check_magnitude_size(digits.size(), sizeof(std::uint64_t)))
            return std::nullopt;
        std::uint64_t const n = magnitude_value<std::uint64_t>(digits);
        return head{tag == std::to_underlying(rfc8949::tag_number::negative_bignum) ? major_type::negative_integer
                                                                                   : major_type::unsigned_integer,
                    preferred_argument_info(n), n};
    }

    template <bool Checked>
    struct decoder {
        std::string_view encoded;

        CBOR_ALWAYS_INLINE std::expected<head, error> decode_head()
        {
            if (auto const fits = detail::validity::check_bytes_left(initial_byte_size, encoded.size());
                !fits) [[unlikely]]
                return std::unexpected(fits.error());
            auto const initial = static_cast<std::uint8_t>(encoded.front());
            auto const major = static_cast<major_type>(initial >> rfc8949::additional_information_bits);
            std::uint8_t const info = additional_information_of(initial);
            if (info < std::to_underlying(rfc8949::additional_information::one_byte_argument)) {
                if (error const r = detail::validity::check_argument<Checked>(major, info).error_or(error{});
                    r != error{}) [[unlikely]]
                    return std::unexpected(r);
                encoded.remove_prefix(initial_byte_size);
                return head{major, info, info};
            }
            if (error const r = detail::validity::check_definite_length(major, info).error_or(error{});
                r != error{}) [[unlikely]]
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
            if (auto const fits =
                    detail::validity::check_bytes_left(initial_byte_size + size, encoded.size());
                !fits) [[unlikely]]
                return std::unexpected(fits.error());
            std::uint64_t const argument =
                read_argument(info, std::span(encoded).subspan(initial_byte_size, size));
            if (error const r = detail::validity::check_argument<Checked>(major, argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            if (major == major_type::simple)
                if (error const r = detail::validity::check_simple_value(info, argument).error_or(error{});
                    r != error{}) [[unlikely]]
                    return std::unexpected(r);
            encoded.remove_prefix(initial_byte_size + size);
            return head{major, info, argument};
        }

        CBOR_ALWAYS_INLINE std::optional<head> try_decode_head()
        {
            std::uint8_t const initial = detail::validity::bytes_fit(initial_byte_size, encoded.size())
                                             ? static_cast<std::uint8_t>(encoded.front())
                                             : 0xff;
            auto const major = static_cast<major_type>(initial >> rfc8949::additional_information_bits);
            std::uint8_t const info = additional_information_of(initial);
            if (detail::validity::bytes_fit(initial_byte_size + sizeof(std::uint64_t), encoded.size()) &&
                info <= std::to_underlying(rfc8949::additional_information::eight_byte_argument) &&
                major != major_type::tag &&
                (major != major_type::simple ||
                 info != std::to_underlying(rfc8949::additional_information::one_byte_argument))) [[likely]] {
                bool const immediate =
                    info < std::to_underlying(rfc8949::additional_information::one_byte_argument);
                std::size_t const size = argument_size(info);
                std::uint64_t argument = info;
                if (info == std::to_underlying(rfc8949::additional_information::one_byte_argument)) {
                    argument = static_cast<std::uint8_t>(encoded[initial_byte_size]);
                } else if (!immediate) {
                    argument = read_unsigned<std::uint64_t>(
                                   std::span<char const>(encoded)
                                       .subspan<initial_byte_size, sizeof(std::uint64_t)>()) >>
                               ((std::numeric_limits<std::uint64_t>::digits -
                                 std::numeric_limits<std::uint8_t>::digits * size) &
                                (std::numeric_limits<std::uint64_t>::digits - 1));
                }
                if (!detail::validity::check_argument<Checked>(major, argument)) [[unlikely]]
                    return std::nullopt;
                encoded.remove_prefix(initial_byte_size + size);
                return head{major, info, argument};
            }
            return std::nullopt;
        }

        constexpr std::expected<std::string_view, error> decode_string(std::uint64_t const length)
        {
            if (auto const fits = detail::validity::check_bytes_left(length, encoded.size()); !fits)
                [[unlikely]]
                return std::unexpected(fits.error());
            std::size_t const size = static_cast<std::size_t>(length);
            std::string_view const string{std::span(encoded).first(size)};
            encoded.remove_prefix(size);
            return string;
        }

        CBOR_ALWAYS_INLINE std::expected<std::optional<head>, error> decode_bignum(std::uint64_t const tag)
        {
            auto const r = decode_head();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (error const c = detail::validity::check_tag_content(tag, r->major).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = decode_string(r->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            return convert_bignum_to_integer(tag, std::span(*bytes));
        }

        template <class T>
            requires(std::integral<T> || std::same_as<T, double> || std::same_as<T, simple_value> ||
                     std::same_as<T, std::nullptr_t> || std::same_as<T, std::string_view> ||
                     std::same_as<T, std::span<std::byte const>>)
        CBOR_ALWAYS_INLINE std::expected<T, error> decode_value(head h)
        {
            if constexpr ((std::integral<T> && !std::same_as<T, bool>) || std::same_as<T, double>) {
                if (is_bignum(h.major, h.argument)) [[unlikely]] {
                    auto const read = decode_bignum(h.argument);
                    if (!read) [[unlikely]]
                        return std::unexpected(read.error());
                    if (!*read) [[unlikely]]
                        return std::unexpected(error::number_out_of_range);
                    h = **read;
                }
            }
            if constexpr (std::integral<T> && !std::same_as<T, bool>) {
                bool const negative = h.major == major_type::negative_integer;
                if (h.major != major_type::unsigned_integer && !negative) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                if (error const c = detail::validity::check_number_range<T>(negative, h.argument).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                return negative ? subtract_argument_from_minus_one<T>(h.argument) : static_cast<T>(h.argument);
            } else if constexpr (std::same_as<T, double>) {
                if (h.major == major_type::unsigned_integer || h.major == major_type::negative_integer) {
                    bool const negative = h.major == major_type::negative_integer;
                    if (error const c =
                            detail::validity::check_number_range<double>(negative, h.argument).error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::unexpected(c);
                    if (!negative)
                        return static_cast<double>(h.argument);
                    return subtract_argument_from_minus_one<double>(h.argument);
                }
                if (h.major != major_type::simple) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                switch (static_cast<rfc8949::simple_information>(h.info)) {
                case rfc8949::simple_information::half_precision_float:
                case rfc8949::simple_information::single_precision_float:
                case rfc8949::simple_information::double_precision_float:
                    return detail::ieee754::decode_float(h.info, h.argument);
                [[unlikely]] default:
                    return std::unexpected(error::incorrect_type);
                }
            } else if constexpr (std::same_as<T, bool>) {
                if (!is_boolean(h)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return h.info == std::to_underlying(simple_value::true_value);
            } else if constexpr (std::same_as<T, simple_value>) {
                if (!is_simple_value(h)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                if (error const c = detail::validity::check_simple_value(h.info, h.argument).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                return static_cast<simple_value>(h.argument);
            } else if constexpr (std::same_as<T, std::nullptr_t>) {
                if (!is_null(h)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return nullptr;
            } else if constexpr (std::same_as<T, std::string_view>) {
                if (h.major != major_type::text_string) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return decode_string(h.argument);
            } else {
                if (h.major != major_type::byte_string) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                auto const bytes = decode_string(h.argument);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                return std::as_bytes(std::span(*bytes));
            }
        }
    };

    static constexpr bool is_typed_array(head const h)
    {
        return h.major == major_type::tag && detail::validity::is_typed_array_tag(h.argument);
    }

    static constexpr bool is_floating_point(major_type const major, std::uint8_t const info)
    {
        return major == major_type::simple &&
               info >= std::to_underlying(rfc8949::simple_information::half_precision_float) &&
               info <= std::to_underlying(rfc8949::simple_information::double_precision_float);
    }

    static constexpr bool is_bignum(major_type const major, std::uint64_t const argument)
    {
        return major == major_type::tag &&
               (argument == std::to_underlying(rfc8949::tag_number::unsigned_bignum) ||
                argument == std::to_underlying(rfc8949::tag_number::negative_bignum));
    }

    static constexpr bool is_boolean(head const &h)
    {
        return h.major == major_type::simple &&
               (h.info == std::to_underlying(simple_value::false_value) || h.info == std::to_underlying(simple_value::true_value));
    }

    static constexpr bool is_simple_value(head const &h)
    {
        return h.major == major_type::simple &&
               h.info <= std::to_underlying(rfc8949::simple_information::simple_value_follows);
    }

    static constexpr bool is_null(head const &h)
    {
        return h.major == major_type::simple && h.info == std::to_underlying(simple_value::null);
    }

    static constexpr cbor::type type_of(major_type const major, std::uint8_t const info, std::uint64_t const argument)
    {
        switch (major) {
        case major_type::unsigned_integer:
            return cbor::type::unsigned_integer;
        case major_type::negative_integer:
            return cbor::type::negative_integer;
        case major_type::byte_string:
            return cbor::type::byte_string;
        case major_type::text_string:
            return cbor::type::text_string;
        case major_type::array:
            return cbor::type::array;
        case major_type::map:
            return cbor::type::map;
        case major_type::tag:
            if (argument == std::to_underlying(rfc8949::tag_number::unsigned_bignum))
                return cbor::type::unsigned_bignum;
            if (argument == std::to_underlying(rfc8949::tag_number::negative_bignum))
                return cbor::type::negative_bignum;
            if (detail::validity::is_typed_array_tag(argument))
                return cbor::type::typed_array;
            return cbor::type::tag;
        case major_type::simple:
            break;
        }
        switch (info) {
        case std::to_underlying(simple_value::false_value):
            return cbor::type::false_value;
        case std::to_underlying(simple_value::true_value):
            return cbor::type::true_value;
        case std::to_underlying(simple_value::null):
            return cbor::type::null;
        case std::to_underlying(simple_value::undefined):
            return cbor::type::undefined;
        case std::to_underlying(rfc8949::simple_information::half_precision_float):
            return cbor::type::half_float;
        case std::to_underlying(rfc8949::simple_information::single_precision_float):
            return cbor::type::single_float;
        case std::to_underlying(rfc8949::simple_information::double_precision_float):
            return cbor::type::double_float;
        default:
            return cbor::type::simple_value;
        }
    }

    static constexpr major_type major_type_of(cbor::type const t)
    {
        switch (t) {
        case cbor::type::unsigned_integer:
            return major_type::unsigned_integer;
        case cbor::type::negative_integer:
            return major_type::negative_integer;
        case cbor::type::byte_string:
            return major_type::byte_string;
        case cbor::type::text_string:
            return major_type::text_string;
        case cbor::type::array:
            return major_type::array;
        case cbor::type::map:
            return major_type::map;
        case cbor::type::tag:
        case cbor::type::unsigned_bignum:
        case cbor::type::negative_bignum:
        case cbor::type::typed_array:
        case cbor::type::shared_reference:
            return major_type::tag;
        case cbor::type::false_value:
        case cbor::type::true_value:
        case cbor::type::null:
        case cbor::type::undefined:
        case cbor::type::simple_value:
        case cbor::type::half_float:
        case cbor::type::single_float:
        case cbor::type::double_float:
            return major_type::simple;
        }
        detail::validity::throw_logic_error("cbor::item: the type is not a member of cbor::type");
    }

    static constexpr bool is_tag_passed(std::uint64_t const tag)
    {
        return tag != std::to_underlying(rfc8949::tag_number::unsigned_bignum) &&
               tag != std::to_underlying(rfc8949::tag_number::negative_bignum) &&
               tag != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item) &&
               !detail::validity::is_typed_array_tag(tag);
    }

    static constexpr std::expected<head, error>
    decode_typed_array_element(std::uint64_t const tag, std::string_view const elements,
                               detail::validity::index const position)
    {
        std::size_t const size = detail::validity::typed_array_element_size(tag);
        bool const floating = (tag >> std::to_underlying(rfc8746::bit_field::f) & 1) != 0;
        bool const signed_integer = (tag >> std::to_underlying(rfc8746::bit_field::s) & 1) != 0;
        bool const little_endian = (tag >> std::to_underlying(rfc8746::bit_field::e) & 1) != 0;
        if (size > sizeof(std::uint64_t)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const element = detail::validity::element_span(elements, position, size);
        std::uint64_t bits = 0;
        for (std::size_t i = 0; i < size; ++i)
            bits = bits << std::numeric_limits<std::uint8_t>::digits |
                   static_cast<std::uint8_t>(element[little_endian ? size - 1 - i : i]);
        if (floating) {
            auto const info = size == sizeof(std::uint16_t)
                                  ? rfc8949::simple_information::half_precision_float
                              : size == sizeof(std::uint32_t)
                                  ? rfc8949::simple_information::single_precision_float
                                  : rfc8949::simple_information::double_precision_float;
            return head{major_type::simple, std::to_underlying(info), bits};
        }
        std::uint64_t const sign = std::uint64_t{1} << (size * std::numeric_limits<std::uint8_t>::digits - 1);
        if (signed_integer && (bits & sign) != 0) {
            std::uint64_t const argument = ~(bits | ~(sign - 1));
            return head{major_type::negative_integer, preferred_argument_info(argument), argument};
        }
        return head{major_type::unsigned_integer, preferred_argument_info(bits), bits};
    }

    struct raw_head {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
        detail::validity::offset at;
    };

    CBOR_ALWAYS_INLINE static constexpr std::uint64_t read_argument(std::uint8_t const info,
                                                                    std::span<char const> const bytes)
    {
        switch (static_cast<rfc8949::additional_information>(info)) {
        case rfc8949::additional_information::one_byte_argument:
            return static_cast<std::uint8_t>(bytes.front());
        case rfc8949::additional_information::two_byte_argument:
            return read_unsigned<std::uint16_t>(bytes.first<sizeof(std::uint16_t)>());
        case rfc8949::additional_information::four_byte_argument:
            return read_unsigned<std::uint32_t>(bytes.first<sizeof(std::uint32_t)>());
        default:
            return read_unsigned<std::uint64_t>(bytes.first<sizeof(std::uint64_t)>());
        }
    }

    template <bool Checked>
    CBOR_ALWAYS_INLINE static constexpr std::expected<raw_head, error>
    read_raw_head(std::string_view const encoded, detail::validity::offset const at)
    {
        std::string_view const rest = detail::validity::subspan(encoded, at);
        if (auto const fits = detail::validity::check_bytes_left(initial_byte_size, rest.size()); !fits)
            [[unlikely]]
            return std::unexpected(fits.error());
        auto const initial = static_cast<std::uint8_t>(rest.front());
        auto const major = static_cast<major_type>(initial >> rfc8949::additional_information_bits);
        std::uint8_t const info = additional_information_of(initial);
        if (info < std::to_underlying(rfc8949::additional_information::one_byte_argument)) {
            if (error const r = detail::validity::check_argument<Checked>(major, info).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            return raw_head{major, info, info, detail::validity::next(at, initial_byte_size)};
        }
        if (error const r = detail::validity::check_additional_information(major, info).error_or(error{});
            r != error{}) [[unlikely]]
            return std::unexpected(r);
        if (info == std::to_underlying(rfc8949::additional_information::indefinite_length))
            return raw_head{major, info, info, detail::validity::next(at, initial_byte_size)};
        std::size_t const size = argument_size(info);
        if (auto const fits = detail::validity::check_bytes_left(size, rest.size() - initial_byte_size);
            !fits) [[unlikely]]
            return std::unexpected(fits.error());
        std::uint64_t const argument = read_argument(
            info, std::span(detail::validity::subspan(encoded, detail::validity::next(at, initial_byte_size))).first(size));
        if (error const r = detail::validity::check_argument<Checked>(major, argument).error_or(error{});
            r != error{}) [[unlikely]]
            return std::unexpected(r);
        if (major == major_type::simple)
            if (error const r = detail::validity::check_simple_value(info, argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
        return raw_head{major, info, argument, detail::validity::next(at, initial_byte_size + size)};
    }

    static std::expected<bool, error> starts_with_definite_byte_string(std::string_view const encoded)
    {
        auto const c = read_raw_head<true>(encoded, detail::validity::offset{});
        if (!c) [[unlikely]]
            return std::unexpected(c.error());
        return c->major == major_type::byte_string &&
               detail::validity::check_definite_length(c->major, c->info).has_value();
    }

    CBOR_ALWAYS_INLINE static constexpr std::string_view self_described_cbor_content(std::string_view const encoded)
    {
        if (encoded.empty() ||
            static_cast<major_type>(static_cast<std::uint8_t>(encoded.front()) >> rfc8949::additional_information_bits) !=
                major_type::tag ||
            additional_information_of(static_cast<std::uint8_t>(encoded.front())) <
                std::to_underlying(rfc8949::additional_information::two_byte_argument)) [[likely]]
            return encoded;
        auto const h = read_raw_head<false>(encoded, detail::validity::offset{});
        if (h && h->major == major_type::tag && detail::validity::is_self_described_cbor_tag(h->argument))
            return detail::validity::subspan(encoded, h->at);
        return encoded;
    }

    static constexpr detail::validity::offset self_described_cbor_content_at(std::string_view const encoded,
                                                                             detail::validity::offset at)
    {
        for (;;) {
            auto const h = read_raw_head<false>(encoded, at);
            if (!h || h->major != major_type::tag ||
                !detail::validity::is_self_described_cbor_tag(h->argument))
                return at;
            at = h->at;
        }
    }

    friend class detail::validity;

    friend class cbor::sequence_reader;

    friend class detail::encoding;

    friend class detail::well_formedness;

    friend class detail::extended_diagnostic_notation;

    friend class cbor::path;

    friend class detail::canonical_ids;

    friend struct cbor::lazy;

    template <bool>
    friend struct detail::tree_decoder;

    friend std::expected<owning<item>, error> cbor::decode(std::string_view encoded);

    friend struct cbor::item;

    friend struct cbor::lazy_elements;

    friend struct cbor::lazy_entries;

    friend std::expected<std::size_t, error> cbor::item_size(std::string_view encoded);

    friend std::expected<std::string, error> cbor::diagnostic_notation(std::string_view encoded);

    friend class detail::reflection;

    friend class detail::generic;

    template <class>
    friend class cbor::structs;

    template <class>
    friend class cbor::schema;
};

}

}
