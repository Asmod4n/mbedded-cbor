/**
 * @file
 * Writes one encoded CBOR data item as text in diagnostic notation,
 * with its encoding indicators. Reads a literal in this notation into
 * CBOR for the path parser.
 * https://www.rfc-editor.org/rfc/rfc8949#section-8
 * https://datatracker.ietf.org/doc/html/draft-ietf-cbor-edn-literals-28
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "rfc2781.hpp"
#include "rfc3629.hpp"
#include "rfc4648.hpp"

namespace cbor
{

class extended_diagnostic_notation
{
    struct parsed {
        std::size_t at;
        bool separated;
    };

    struct literal_cursor {
        std::string_view text;
        std::size_t at;
    };

    struct canonical_pair {
        std::size_t key;
        std::size_t value;
        std::size_t end;
    };

    struct string_literal {
        std::size_t at;
        major_type major;
        std::string content;
    };

    static constexpr int no_indicator = -1;
    static constexpr int immediate_indicator = -2;

    static constexpr int binary = 2;
    static constexpr int octal = 8;
    static constexpr int decimal = 10;
    static constexpr int hexadecimal = 16;

    static constexpr unsigned char unescaped_first = 0x20;

    static constexpr bool blank(char const c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    static constexpr std::size_t blank_end(std::string_view const text, std::size_t at)
    {
        while (at < text.size() && blank(text[at]))
            ++at;
        return at;
    }

    static constexpr bool eol_comment_starts(std::string_view const text, std::size_t const at)
    {
        return std::string_view{std::span(text).subspan(at)}.starts_with('#') ||
               std::string_view{std::span(text).subspan(at)}.starts_with("//");
    }

    static constexpr std::size_t eol_comment_end(std::string_view const text, std::size_t const at)
    {
        std::size_t const lf = text.find('\n', at);
        return lf == std::string_view::npos ? text.size() : lf + 1;
    }

    static constexpr std::expected<std::size_t, error> comment_end(std::string_view const text, std::size_t const at)
    {
        if (eol_comment_starts(text, at)) {
            std::size_t const end = eol_comment_end(text, at);
            if (text[end - 1] != '\n') [[unlikely]]
                return std::unexpected(error::invalid_path);
            return end;
        }
        bool const block = std::string_view{std::span(text).subspan(at)}.starts_with("/*");
        std::size_t const close = block ? text.find("*/", at + std::string_view("/*").size()) : text.find('/', at + 1);
        if (close == std::string_view::npos) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return close + (block ? std::string_view("*/").size() : std::string_view("/").size());
    }

    static constexpr std::expected<std::size_t, error> optional_space_end(std::string_view const text, std::size_t at)
    {
        for (;;) {
            at = blank_end(text, at);
            if (!detail::validity::check_text_left(text, at, 1) || (text[at] != '/' && text[at] != '#'))
                return at;
            auto const end = comment_end(text, at);
            if (!end) [[unlikely]]
                return end;
            at = *end;
        }
    }

    static constexpr bool digit(char const c)
    {
        return c >= '0' && c <= '9';
    }

    static constexpr int hex_digit_value(char const c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + std::integral_constant<int, rfc4648::base16_alphabet.find('A')>::value;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + std::integral_constant<int, rfc4648::base16_alphabet.find('A')>::value;
        return -1;
    }

    static constexpr std::expected<void, error> append_head(std::string &out, major_type const major,
                                                            std::uint64_t const argument, int const indicator)
    {
        int info;
        if (indicator == no_indicator)
            info = argument < static_cast<std::uint64_t>(rfc8949::additional_information::one_byte_argument)
                       ? 0
                       : heads::preferred_argument_info(argument);
        else if (indicator == immediate_indicator)
            info = 0;
        else
            info = indicator;
        if (info == 0) {
            if (argument >= static_cast<std::uint64_t>(rfc8949::additional_information::one_byte_argument))
                [[unlikely]]
                return std::unexpected(error::invalid_path);
            heads::append_head(out, major, static_cast<std::uint8_t>(argument), argument);
            return {};
        }
        if (info < static_cast<int>(rfc8949::additional_information::one_byte_argument) ||
            info > static_cast<int>(rfc8949::additional_information::eight_byte_argument)) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t const size =
            std::size_t{1} << (info - static_cast<int>(rfc8949::additional_information::one_byte_argument));
        if (size < sizeof(std::uint64_t) &&
            argument >> (std::numeric_limits<std::uint8_t>::digits * size) != 0) [[unlikely]]
            return std::unexpected(error::invalid_path);
        heads::append_head(out, major, static_cast<std::uint8_t>(info), argument);
        return {};
    }

    struct indicated {
        std::size_t at;
        int indicator;
    };

    static constexpr std::expected<indicated, error> parse_indicator(std::string_view const text, std::size_t const at)
    {
        if (!detail::validity::check_text_left(text, at, 1) || text[at] != '_')
            return indicated{at, no_indicator};
        if (at + 1 < text.size() && text[at + 1] == 'i')
            return indicated{at + std::string_view("_i").size(), immediate_indicator};
        if (at + 1 < text.size() && text[at + 1] >= '0' && text[at + 1] <= '3')
            return indicated{at + 2, std::to_underlying(rfc8949::additional_information::one_byte_argument) +
                                         (text[at + 1] - '0')};
        if (at + 1 < text.size() && digit(text[at + 1])) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return indicated{at + 1, std::to_underlying(rfc8949::additional_information::indefinite_length)};
    }

    static constexpr void append_utf8(std::string &out, std::uint32_t const c)
    {
        constexpr int bits = rfc3629::following_octet_bits;
        auto const following = [](std::uint32_t const v) {
            return static_cast<char>(rfc3629::following_octet | (v & rfc3629::following_octet_mask));
        };
        if (c <= rfc3629::one_octet_last) {
            out.push_back(static_cast<char>(c));
        } else if (c <= rfc3629::two_octets_last) {
            out.push_back(static_cast<char>(rfc3629::two_octets_initial | c >> bits));
            out.push_back(following(c));
        } else if (c <= rfc3629::three_octets_last) {
            out.push_back(static_cast<char>(rfc3629::three_octets_initial | c >> bits >> bits));
            out.push_back(following(c >> bits));
            out.push_back(following(c));
        } else {
            out.push_back(static_cast<char>(rfc3629::four_octets_initial | c >> bits >> bits >> bits));
            out.push_back(following(c >> bits >> bits));
            out.push_back(following(c >> bits));
            out.push_back(following(c));
        }
    }

    static constexpr std::array<std::uint64_t, 3> quiet_nan{0x7e00, 0x7fc00000, 0x7ff8000000000000};
    static constexpr std::size_t hex4_digits = 4;
    static constexpr std::string_view escape_prefix = "\\u";

    struct code_unit {
        std::size_t at;
        std::uint32_t value;
    };

    static constexpr std::expected<code_unit, error> parse_hex4(std::string_view const text, std::size_t const at)
    {
        if (auto const left = detail::validity::check_text_left(text, at, hex4_digits); !left) [[unlikely]]
            return std::unexpected(left.error());
        std::uint32_t value = 0;
        for (char const c : std::span(text).subspan(at, hex4_digits)) {
            int const v = hex_digit_value(c);
            if (v < 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            value = value << rfc4648::base16_group_bits | static_cast<std::uint32_t>(v);
        }
        return code_unit{at + hex4_digits, value};
    }

    static constexpr std::expected<std::size_t, error> parse_quoted(std::string_view const text, std::size_t at,
                                                                    std::string &out)
    {
        char const quote = text[at++];
        for (;;) {
            if (auto const left = detail::validity::check_text_left(text, at, 1); !left) [[unlikely]]
                return std::unexpected(left.error());
            char const c = text[at++];
            if (c == quote)
                return at;
            if (c == '\r')
                continue;
            if (c != '\n' && static_cast<unsigned char>(c) < unescaped_first) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (auto const left = detail::validity::check_text_left(text, at, 1); !left) [[unlikely]]
                return std::unexpected(left.error());
            char const e = text[at++];
            switch (e) {
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case '/':
            case '\\':
            case '\'':
            case '"':
                out.push_back(e);
                break;
            case 'u': {
                std::uint32_t c1 = 0;
                if (at < text.size() && text[at] == '{') {
                    auto const rest = std::span(text).subspan(at);
                    auto const brace = std::ranges::find(rest, '}');
                    std::size_t const close = at + static_cast<std::size_t>(std::ranges::distance(rest.begin(), brace));
                    constexpr std::size_t braced_hex_digits_max = 8;
                    if (brace == rest.end() || close == at + 1 || close > at + 1 + braced_hex_digits_max)
                        [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    for (char const h : std::span(text).subspan(at + 1, close - at - 1)) {
                        int const v = hex_digit_value(h);
                        if (v < 0) [[unlikely]]
                            return std::unexpected(error::invalid_path);
                        c1 = c1 << rfc4648::base16_group_bits | static_cast<std::uint32_t>(v);
                    }
                    at = close + 1;
                    if (c1 > rfc3629::char_number_last || (c1 >= rfc2781::high_surrogate_first && c1 <= rfc2781::low_surrogate_last)) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    append_utf8(out, c1);
                    break;
                }
                auto const next = parse_hex4(text, at);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                at = next->at;
                c1 = next->value;
                if (c1 >= rfc2781::low_surrogate_first && c1 <= rfc2781::low_surrogate_last) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (c1 >= rfc2781::high_surrogate_first && c1 < rfc2781::low_surrogate_first) {
                    if (!detail::validity::check_text_left(text, at, escape_prefix.size()) ||
                        !std::string_view(std::span(text).subspan(at)).starts_with(escape_prefix)) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    auto const low = parse_hex4(text, at + escape_prefix.size());
                    if (!low || low->value < rfc2781::low_surrogate_first || low->value > rfc2781::low_surrogate_last) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    std::uint32_t const c2 = low->value;
                    at = low->at;
                    c1 = rfc2781::surrogate_pair_first + ((c1 - rfc2781::high_surrogate_first) << rfc2781::surrogate_bits) +
                         (c2 - rfc2781::low_surrogate_first);
                }
                append_utf8(out, c1);
                break;
            }
            [[unlikely]] default:
                return std::unexpected(error::invalid_path);
            }
        }
    }

    static constexpr std::expected<void, error> parse_hex_string(std::string_view const content, std::string &out)
    {
        int high = -1;
        std::size_t at = 0;
        while (at < content.size()) {
            char const c = content[at];
            if (blank(c)) {
                ++at;
                continue;
            }
            if (eol_comment_starts(content, at)) {
                at = eol_comment_end(content, at);
                continue;
            }
            if (c == '/') {
                auto const end = comment_end(content, at);
                if (!end) [[unlikely]]
                    return std::unexpected(end.error());
                at = *end;
                continue;
            }
            int const v = hex_digit_value(c);
            if (v < 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (high < 0) {
                high = v;
            } else {
                out.push_back(static_cast<char>(high << rfc4648::base16_group_bits | v));
                high = -1;
            }
            ++at;
        }
        if (high >= 0) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return {};
    }

    static constexpr std::expected<void, error> parse_base64_string(std::string_view const content, std::string &out)
    {
        std::uint32_t bits = 0;
        int count = 0;
        int padding = 0;
        for (std::size_t at = 0; at < content.size(); ++at) {
            char const c = content[at];
            if (blank(c))
                continue;
            if (c == '#') {
                at = eol_comment_end(content, at) - 1;
                continue;
            }
            int v;
            if (c >= 'A' && c <= 'Z')
                v = c - 'A';
            else if (c >= 'a' && c <= 'z')
                v = c - 'a' + std::integral_constant<int, rfc4648::base64_alphabet.find('a')>::value;
            else if (digit(c))
                v = c - '0' + std::integral_constant<int, rfc4648::base64_alphabet.find('0')>::value;
            else if (c == '+' || c == '-')
                v = std::integral_constant<int, rfc4648::base64_alphabet.find('+')>::value;
            else if (c == '/' || c == '_')
                v = std::integral_constant<int, rfc4648::base64_alphabet.find('/')>::value;
            else if (c == '=') {
                ++padding;
                continue;
            } else [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (padding != 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            bits = bits << rfc4648::base64_group_bits | static_cast<std::uint32_t>(v);
            if (++count == rfc4648::base64_input_group_bits / rfc4648::base64_group_bits) {
                out.push_back(static_cast<char>(bits >> (rfc4648::base64_input_group_bits - std::numeric_limits<std::uint8_t>::digits)));
                out.push_back(static_cast<char>(bits >> std::numeric_limits<std::uint8_t>::digits));
                out.push_back(static_cast<char>(bits));
                bits = 0;
                count = 0;
            }
        }
        if (count == 1 || (padding != 0 && count + padding != rfc4648::base64_input_group_bits / rfc4648::base64_group_bits)) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (count == 2)
            out.push_back(static_cast<char>(bits >> 4));
        if (count == 3) {
            out.push_back(static_cast<char>(bits >> 10));
            out.push_back(static_cast<char>(bits >> 2));
        }
        return {};
    }

    static constexpr std::expected<void, error> append_float(std::string &out, double const value, int const indicator)
    {
        bool const nan = value != value;
        bool const infinite = !nan && (value > std::numeric_limits<double>::max() || value < -std::numeric_limits<double>::max());
        rfc8949::simple_float_information const preferred = heads::preferred_float_info(value);
        int info;
        if (indicator == no_indicator)
            info = std::to_underlying(preferred);
        else if (indicator >= std::to_underlying(rfc8949::simple_float_information::half_precision_float) &&
                 indicator <= std::to_underlying(rfc8949::simple_float_information::double_precision_float))
            info = indicator;
        else [[unlikely]]
            return std::unexpected(error::invalid_path);
        bool const exact =
            nan || infinite || info >= std::to_underlying(preferred) ||
            (info == std::to_underlying(rfc8949::simple_float_information::single_precision_float) &&
             static_cast<double>(static_cast<float>(value)) == value);
        if (!exact) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::uint64_t const argument =
            nan ? quiet_nan[static_cast<std::size_t>(
                      info - std::to_underlying(rfc8949::simple_float_information::half_precision_float))]
                : heads::encode_float(static_cast<rfc8949::simple_float_information>(info), value);
        return append_head(out, major_type::simple_float, argument, info);
    }

    static constexpr std::expected<double, error> decimal_exact_value(std::uint64_t mantissa, int exponent, bool const overflow)
    {
        constexpr std::uint64_t exact_max = std::uint64_t{1} << std::numeric_limits<double>::digits;
        constexpr std::array<double, 23> powers{1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                                1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
        constexpr int exponent_exact_max = static_cast<int>(powers.size()) - 1;
        if (overflow) [[unlikely]]
            return std::unexpected(error::invalid_path);
        while (exponent > exponent_exact_max && mantissa <= exact_max / decimal) {
            mantissa *= decimal;
            --exponent;
        }
        while (exponent < 0 && mantissa % decimal == 0) {
            mantissa /= decimal;
            ++exponent;
        }
        if (mantissa > exact_max || exponent > exponent_exact_max || exponent < -exponent_exact_max) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return exponent >= 0 ? static_cast<double>(mantissa) * powers[static_cast<std::size_t>(exponent)]
                             : static_cast<double>(mantissa) / powers[static_cast<std::size_t>(-exponent)];
    }

    static constexpr std::expected<std::size_t, error> parse_number(std::string_view const text, std::size_t at,
                                                                    std::string &out)
    {
        bool negative = false;
        char const sign = text[at];
        if (sign == '+' || sign == '-')
            negative = text[at++] == '-';
        auto const word = [&](std::string_view const w) { return std::string_view{std::span(text).subspan(at)}.starts_with(w); };
        if (word("Infinity") || word("NaN")) {
            bool const nan = word("NaN");
            if (sign == '+' || (nan && sign == '-')) [[unlikely]]
                return std::unexpected(error::invalid_path);
            at += nan ? std::string_view("NaN").size() : std::string_view("Infinity").size();
            auto const next = parse_indicator(text, at);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            int const indicator = next->indicator;
            double const infinity = std::numeric_limits<double>::infinity();
            auto const r = append_float(out, nan ? std::numeric_limits<double>::quiet_NaN() : negative ? -infinity : infinity,
                                        indicator);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            return next->at;
        }
        int base = decimal;
        if (word("0x") || word("0X")) {
            base = hexadecimal;
            at += std::string_view("0x").size();
        } else if (word("0o")) {
            base = octal;
            at += std::string_view("0o").size();
        } else if (word("0b")) {
            base = binary;
            at += std::string_view("0b").size();
        }
        std::uint64_t mantissa = 0;
        std::size_t digits = 0;
        int exponent = 0;
        bool overflow = false;
        bool fraction = false;
        bool real = false;
        std::size_t const first = at;
        for (; at < text.size(); ++at) {
            char const c = text[at];
            if (c == '.' && !fraction && (base == decimal || base == hexadecimal)) {
                fraction = true;
                real = true;
                continue;
            }
            int const v = hex_digit_value(c);
            if (v < 0 || v >= base)
                break;
            ++digits;
            auto const next = detail::validity::checked_mul(mantissa, static_cast<std::uint64_t>(base))
                                  .and_then([v](std::uint64_t const m) {
                                      return detail::validity::checked_add(m, static_cast<std::uint64_t>(v));
                                  });
            if (!next) {
                overflow = true;
                continue;
            }
            mantissa = *next;
            if (fraction)
                --exponent;
        }
        if (digits == 0) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (base == decimal && text[first] == '0' && first + 1 < at && digit(text[first + 1])) [[unlikely]]
            return std::unexpected(error::invalid_path);
        bool const exponent_part = at < text.size() && ((base == decimal && (text[at] == 'e' || text[at] == 'E')) ||
                                                        (base == hexadecimal && (text[at] == 'p' || text[at] == 'P')));
        if (base == hexadecimal && real && !exponent_part) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (exponent_part) {
            real = true;
            ++at;
            bool exponent_negative = false;
            if (at < text.size() && (text[at] == '+' || text[at] == '-'))
                exponent_negative = text[at++] == '-';
            int e = 0;
            std::size_t const e_first = at;
            constexpr int exponent_beyond_double = 100000;
            while (at < text.size() && digit(text[at]) && e < exponent_beyond_double)
                e = e * decimal + (text[at++] - '0');
            if (at == e_first || (at < text.size() && digit(text[at]))) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (base == hexadecimal)
                exponent = rfc4648::base16_group_bits * exponent + (exponent_negative ? -e : e);
            else
                exponent += exponent_negative ? -e : e;
        } else if (base == hexadecimal) {
            exponent *= rfc4648::base16_group_bits;
        }
        auto const next = parse_indicator(text, at);
        if (!next) [[unlikely]]
            return std::unexpected(next.error());
        int const indicator = next->indicator;
        if (!real) {
            if (overflow) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (negative && mantissa != 0) {
                if (auto const r = append_head(out, major_type::negative_integer, mantissa - 1, indicator); !r) [[unlikely]]
                    return std::unexpected(r.error());
            } else if (auto const r = append_head(out, major_type::unsigned_integer, mantissa, indicator); !r) [[unlikely]] {
                return std::unexpected(r.error());
            }
            return next->at;
        }
        constexpr std::uint64_t exact_max = std::uint64_t{1} << std::numeric_limits<double>::digits;
        double value;
        if (mantissa == 0) {
            value = 0.0;
        } else if (base == hexadecimal) {
            if (overflow || mantissa > exact_max) [[unlikely]]
                return std::unexpected(error::invalid_path);
            constexpr int highest = std::numeric_limits<double>::max_exponent - 1;
            constexpr int lowest_normal = std::numeric_limits<double>::min_exponent - 1;
            constexpr int lowest = std::numeric_limits<double>::min_exponent - std::numeric_limits<double>::digits;
            auto const power_of_two = [](int const e) {
                double power = 1.0;
                for (int i = 0; i < e; ++i)
                    power *= 2.0;
                for (int i = 0; i > e; --i)
                    power *= 0.5;
                return power;
            };
            int const exact_step = std::clamp(exponent, lowest_normal, highest);
            int const rounded_step = exponent - exact_step;
            value = static_cast<double>(mantissa) * power_of_two(exact_step);
            if (rounded_step < lowest)
                value = 0.0;
            else if (rounded_step > highest)
                value = std::numeric_limits<double>::infinity();
            else
                value *= power_of_two(rounded_step);
        } else {
#if defined(__cpp_lib_to_chars) || (defined(_LIBCPP_VERSION) && _LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT)
            if !consteval {
                std::string_view const number{std::span(text).subspan(first, at - first)};
                if (std::from_chars(std::to_address(number.begin()), std::to_address(number.end()), value).ec != std::errc{})
                    [[unlikely]]
                    return std::unexpected(error::invalid_path);
            } else {
                auto const exact = decimal_exact_value(mantissa, exponent, overflow);
                if (!exact) [[unlikely]]
                    return std::unexpected(exact.error());
                value = *exact;
            }
#else
            auto const exact = decimal_exact_value(mantissa, exponent, overflow);
            if (!exact) [[unlikely]]
                return std::unexpected(exact.error());
            value = *exact;
#endif
        }
        if (auto const r = append_float(out, negative ? -value : value, indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        return next->at;
    }

    static constexpr std::expected<parsed, error> parse_separator(std::string_view const text, std::size_t const at)
    {
        auto next = optional_space_end(text, at);
        if (!next) [[unlikely]]
            return std::unexpected(next.error());
        bool separated = *next != at;
        if (*next < text.size() && text[*next] == ',') {
            next = optional_space_end(text, *next + 1);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            separated = true;
        }
        return parsed{*next, separated};
    }

    static constexpr std::expected<std::size_t, error> parse_container(literal_cursor const cursor, std::string &out)
    {
        std::string_view const text = cursor.text;
        std::size_t at = cursor.at;
        bool const map = text[at++] == '{';
        char const close = map ? '}' : ']';
        auto const after = parse_indicator(text, at);
        if (!after) [[unlikely]]
            return std::unexpected(after.error());
        int const indicator = after->indicator;
        auto const first = optional_space_end(text, after->at);
        if (!first) [[unlikely]]
            return first;
        at = *first;
        major_type const major = map ? major_type::map : major_type::array;
        bool const indefinite = indicator == std::to_underlying(rfc8949::additional_information::indefinite_length);
        if (indefinite)
            out.push_back(heads::initial_byte(major, static_cast<std::uint64_t>(indicator)));
        std::size_t const start = out.size();
        std::uint64_t count = 0;
        bool separated = true;
        while (at < text.size() && text[at] != close) {
            if (!separated) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto next = parse_literal({text, at}, out);
            if (!next) [[unlikely]]
                return next;
            if (map) {
                auto const colon = optional_space_end(text, *next);
                if (!colon) [[unlikely]]
                    return colon;
                if (!detail::validity::check_text_left(text, *colon, 1) || text[*colon] != ':') [[unlikely]]
                    return std::unexpected(error::invalid_path);
                auto const value = optional_space_end(text, *colon + 1);
                if (!value) [[unlikely]]
                    return value;
                next = parse_literal({text, *value}, out);
                if (!next) [[unlikely]]
                    return next;
            }
            ++count;
            auto const s = parse_separator(text, *next);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            at = s->at;
            separated = s->separated;
        }
        if (auto const left = detail::validity::check_text_left(text, at, 1); !left) [[unlikely]]
            return std::unexpected(left.error());
        if (indefinite) {
            out.push_back(
                heads::initial_byte(major_type::simple_float,
                                    std::to_underlying(rfc8949::simple_float_information::break_stop_code)));
        } else {
            std::string head;
            if (auto const r = append_head(head, major, count, indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            out.insert(start, head);
        }
        return at + 1;
    }

    static constexpr std::expected<std::size_t, error> string_finish(std::string_view const text, std::size_t const at,
                                                                     std::string &out, major_type const major,
                                                                     std::string_view const content)
    {
        auto const next = parse_indicator(text, at);
        if (!next) [[unlikely]]
            return std::unexpected(next.error());
        int const indicator = next->indicator;
        if (indicator == std::to_underlying(rfc8949::additional_information::indefinite_length)) {
            if (!content.empty()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            out.push_back(heads::initial_byte(major, static_cast<std::uint64_t>(indicator)));
            out.push_back(
                heads::initial_byte(major_type::simple_float,
                                    std::to_underlying(rfc8949::simple_float_information::break_stop_code)));
            return next->at;
        }
        if (auto const r = append_head(out, major, content.size(), indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        out += content;
        return next->at;
    }

    static constexpr bool string_literal_starts(std::string_view const rest)
    {
        return rest.starts_with('"') || rest.starts_with('\'') || rest.starts_with("h'") || rest.starts_with("b64'") ||
               rest.starts_with("<<");
    }

    static constexpr std::expected<string_literal, error> parse_string_literal(literal_cursor const cursor)
    {
        std::string_view const text = cursor.text;
        std::size_t const at = cursor.at;
        std::string_view const rest{std::span(text).subspan(at)};
        if (rest.starts_with('"') || rest.starts_with('\'')) {
            string_literal string{0, rest.front() == '"' ? major_type::text_string : major_type::byte_string, {}};
            auto const next = parse_quoted(text, at, string.content);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            string.at = *next;
            return string;
        }
        if (rest.starts_with("h'") || rest.starts_with("b64'")) {
            bool const hex = rest.front() == 'h';
            std::string quoted;
            auto const next = parse_quoted(text, at + (hex ? std::string_view("h").size() : std::string_view("b64").size()), quoted);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            string_literal string{*next, major_type::byte_string, {}};
            auto const r = hex ? parse_hex_string(quoted, string.content) : parse_base64_string(quoted, string.content);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            return string;
        }
        string_literal string{0, major_type::byte_string, {}};
        auto first = optional_space_end(text, at + std::string_view("<<").size());
        if (!first) [[unlikely]]
            return std::unexpected(first.error());
        std::size_t next = *first;
        bool separated = true;
        while (!std::string_view{std::span(text).subspan(next)}.starts_with(std::string_view(">>"))) {
            if (!separated || !detail::validity::check_text_left(text, next, 1)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto const item = parse_literal({text, next}, string.content);
            if (!item) [[unlikely]]
                return std::unexpected(item.error());
            auto const s = parse_separator(text, *item);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            next = s->at;
            separated = s->separated;
        }
        string.at = next + std::string_view(">>").size();
        return string;
    }

    static constexpr std::expected<std::size_t, error> parse_streamstring(literal_cursor const cursor, std::string &out)
    {
        std::string_view const text = cursor.text;
        std::size_t const open = cursor.at + std::string_view("(_").size();
        auto const first = optional_space_end(text, open);
        if (!first) [[unlikely]]
            return first;
        if (*first == open) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t at = *first;
        std::string chunks;
        major_type major{};
        bool separated = true;
        bool empty = true;
        while (at < text.size() && text[at] != ')') {
            if (!separated || !string_literal_starts(std::string_view{std::span(text).subspan(at)})) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto const chunk = parse_string_literal({text, at});
            if (!chunk) [[unlikely]]
                return std::unexpected(chunk.error());
            if (chunk->at < text.size() && text[chunk->at] == '_') [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (empty)
                major = chunk->major;
            if (!detail::validity::check_chunk(
                    major, chunk->major, heads::preferred_argument_info(chunk->content.size()))) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (auto const r = append_head(chunks, chunk->major, chunk->content.size(), no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            chunks += chunk->content;
            empty = false;
            auto const s = parse_separator(text, chunk->at);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            at = s->at;
            separated = s->separated;
        }
        if (!detail::validity::check_text_left(text, at, 1) || empty) [[unlikely]]
            return std::unexpected(error::invalid_path);
        out.push_back(heads::initial_byte(major, std::to_underlying(rfc8949::additional_information::indefinite_length)));
        out += chunks;
        out.push_back(heads::initial_byte(major_type::simple_float,
                                          std::to_underlying(rfc8949::simple_float_information::break_stop_code)));
        return at + 1;
    }

    static constexpr std::expected<std::size_t, error> parse_literal(literal_cursor const cursor, std::string &out)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::string_view const text = cursor.text;
        std::size_t const at = cursor.at;
        if (auto const left = detail::validity::check_text_left(text, at, 1); !left) [[unlikely]]
            return std::unexpected(left.error());
        std::string_view const rest{std::span(text).subspan(at)};
        char const c = rest.front();
        if (c == '[' || c == '{')
            return parse_container(cursor, out);
        if (string_literal_starts(rest)) {
            auto const string = parse_string_literal(cursor);
            if (!string) [[unlikely]]
                return std::unexpected(string.error());
            return string_finish(text, string->at, out, string->major, string->content);
        }
        if (rest.starts_with("(_"))
            return parse_streamstring(cursor, out);
        constexpr auto names = std::to_array<std::string_view>({"false", "true", "null", "undefined"});
        for (std::size_t i = 0; i < names.size(); ++i)
            if (rest.starts_with(names[i])) {
                out.push_back(heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::false_value) + i));
                return at + names[i].size();
            }
        if (rest.starts_with("simple(")) {
            auto const opened = optional_space_end(text, at + std::string_view("simple(").size());
            if (!opened) [[unlikely]]
                return opened;
            std::size_t next = *opened;
            std::size_t const first = next;
            unsigned value = 0;
            constexpr unsigned simple_value_beyond_byte = 1000;
            while (next < text.size() && digit(text[next]) && value < simple_value_beyond_byte)
                value = value * decimal + static_cast<unsigned>(text[next++] - '0');
            if (next == first || (text[first] == '0' && next > first + 1)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto const closed = optional_space_end(text, next);
            if (!closed) [[unlikely]]
                return closed;
            next = *closed;
            if (!detail::validity::check_text_left(text, next, 1) || text[next] != ')' ||
                value > std::numeric_limits<std::uint8_t>::max() ||
                !detail::validity::check_simple_value(heads::preferred_argument_info(value), value))
                [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (auto const r = append_head(out, major_type::simple_float, value, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next + 1;
        }
        if (!(digit(c) || c == '-' || c == '+' || c == '.' || rest.starts_with("Infinity") || rest.starts_with("NaN")))
            [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t digits_end = at;
        while (digits_end < text.size() && digit(text[digits_end]))
            ++digits_end;
        auto const tag_open = parse_indicator(text, digits_end);
        int const indicator = tag_open ? tag_open->indicator : no_indicator;
        if (digits_end != at && tag_open && tag_open->at < text.size() && text[tag_open->at] == '(' &&
            indicator != std::to_underlying(rfc8949::additional_information::indefinite_length)) {
            if (text[at] == '0' && digits_end > at + 1) [[unlikely]]
                return std::unexpected(error::invalid_path);
            std::uint64_t number = 0;
            for (char const d : std::span(text).subspan(at, digits_end - at)) {
                auto const next =
                    detail::validity::checked_mul(number, std::uint64_t{decimal})
                        .and_then([d](std::uint64_t const m) {
                            return detail::validity::checked_add(m, static_cast<std::uint64_t>(d - '0'));
                        });
                if (!next) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                number = *next;
            }
            if (auto const r = append_head(out, major_type::tag, number, indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const opened = optional_space_end(text, tag_open->at + 1);
            if (!opened) [[unlikely]]
                return opened;
            auto const content = parse_literal({text, *opened}, out);
            if (!content) [[unlikely]]
                return content;
            auto const close = optional_space_end(text, *content);
            if (!close) [[unlikely]]
                return close;
            if (!detail::validity::check_text_left(text, *close, 1) || text[*close] != ')') [[unlikely]]
                return std::unexpected(error::invalid_path);
            return *close + 1;
        }
        return parse_number(text, at, out);
    }

    static constexpr std::expected<detail::validity::offset, error>
    append_canonical(std::string &out, std::string_view const encoded, detail::validity::offset const at)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto h = heads::read_raw_head<false>(encoded, at);
        for (;;) {
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                break;
            if (auto const r = append_head(out, major_type::tag, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            h = heads::read_raw_head<false>(encoded, h->at);
        }
        bool const indefinite =
            h->info == std::to_underlying(rfc8949::additional_information::indefinite_length);
        detail::validity::offset next = h->at;
        switch (h->major) {
        case major_type::unsigned_integer:
        case major_type::negative_integer:
            if (auto const r = append_head(out, h->major, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        case major_type::byte_string:
        case major_type::text_string: {
            std::string content;
            if (!indefinite) {
                heads::decoder<false> d{detail::validity::subspan(encoded, next)};
                auto const s = d.decode_string(h->argument);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                content = *s;
                next = detail::validity::next(next, s->size());
            } else {
                while (!heads::break_at(encoded, next)) {
                    auto const chunk = heads::read_raw_head<false>(encoded, next);
                    if (!chunk) [[unlikely]]
                        return std::unexpected(chunk.error());
                    if (error const c = detail::validity::check_chunk(h->major, chunk->major, chunk->info)
                                            .error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::unexpected(c);
                    if (!std::in_range<std::size_t>(chunk->argument)) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    auto const piece = detail::validity::substr(encoded, chunk->at,
                                                                static_cast<std::size_t>(chunk->argument));
                    if (!piece) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    content += *piece;
                    next = detail::validity::next(chunk->at, piece->size());
                }
                next = detail::validity::next(next, heads::initial_byte_size);
            }
            if (auto const r = append_head(out, h->major, content.size(), no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            out += content;
            return next;
        }
        case major_type::array:
        case major_type::map: {
            bool const map = h->major == major_type::map;
            if (auto const r = detail::validity::check_pending_items(
                    h->major, h->info, h->argument, detail::validity::subspan(encoded, next).size());
                !r) [[unlikely]]
                return std::unexpected(r.error());
            std::size_t const start = out.size();
            std::vector<canonical_pair> pairs;
            std::uint64_t count = 0;
            for (; indefinite ? !heads::break_at(encoded, next) : count < h->argument; ++count) {
                std::size_t const key_at = out.size();
                auto const first = append_canonical(out, encoded, next);
                if (!first) [[unlikely]]
                    return first;
                next = *first;
                if (map) {
                    std::size_t const value_at = out.size();
                    auto const second = append_canonical(out, encoded, next);
                    if (!second) [[unlikely]]
                        return second;
                    next = *second;
                    pairs.push_back({key_at - start, value_at - start, out.size() - start});
                }
            }
            if (indefinite)
                next = detail::validity::next(next, heads::initial_byte_size);
            if (map) {
                std::string const region{std::string_view{std::span(out).subspan(start)}};
                auto const key = [&region](canonical_pair const &p) {
                    return std::string_view(std::span(region).subspan(p.key, p.value - p.key));
                };
                std::ranges::sort(pairs, {}, key);
                if (error const c =
                        detail::validity::check_key_unique(std::ranges::adjacent_find(pairs, {}, key) != pairs.end())
                            .error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                out.resize(start);
                for (canonical_pair const &p : pairs)
                    out += std::string_view(std::span(region).subspan(p.key, p.end - p.key));
            }
            std::string head;
            if (auto const r = append_head(head, h->major, count, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            out.insert(start, head);
            return next;
        }
        default:
            break;
        }
        constexpr std::uint8_t half =
            std::to_underlying(rfc8949::simple_float_information::half_precision_float);
        constexpr std::uint8_t twice =
            std::to_underlying(rfc8949::simple_float_information::double_precision_float);
        if (h->info < half) {
            if (error const r = detail::validity::check_simple_value(h->info, h->argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            if (auto const r = append_head(out, major_type::simple_float, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }
        if (h->info > twice) [[unlikely]]
            return std::unexpected(error::syntax_error);
        heads::float_key const key = heads::float_key_of(h->info, h->argument);
        if (key.nan) {
            if (auto const r = append_head(out, major_type::simple_float,
                                           key.widened | std::uint64_t{heads::double_precision.exponent_max}
                                                             << heads::double_precision.significand_bits,
                                           twice);
                !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }
        if (auto const r = append_float(out, key.value == 0 ? 0.0 : key.value, no_indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        return next;
    }

    static std::string encoding_indicator(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info < std::to_underlying(rfc8949::additional_information::one_byte_argument))
            return {};
        if (info == heads::preferred_argument_info(argument))
            return {};
        return {'_',
                static_cast<char>('0' + info -
                                  std::to_underlying(rfc8949::additional_information::one_byte_argument))};
    }

    static std::string hex_of(std::string_view const bytes)
    {
        constexpr std::string_view digits = "0123456789abcdef";
        std::string out;
        out.reserve(std::numeric_limits<std::uint8_t>::digits / rfc4648::base16_group_bits * bytes.size());
        for (char const c : bytes) {
            out.push_back(digits[static_cast<std::uint8_t>(c) >> rfc4648::base16_group_bits]);
            out.push_back(digits[static_cast<std::uint8_t>(c) & ((1 << rfc4648::base16_group_bits) - 1)]);
        }
        return out;
    }

    static std::string quoted_of(std::string_view const text)
    {
        constexpr std::string_view digits = "0123456789abcdef";
        std::string out = "\"";
        for (char const c : text) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<std::uint8_t>(c) < unescaped_first) {
                    out += "\\u00";
                    out.push_back(digits[static_cast<std::uint8_t>(c) >> rfc4648::base16_group_bits]);
                    out.push_back(digits[static_cast<std::uint8_t>(c) & ((1 << rfc4648::base16_group_bits) - 1)]);
                } else {
                    out.push_back(c);
                }
                break;
            }
        }
        out.push_back('"');
        return out;
    }

    static std::string number_of(double const value)
    {
        if (std::isinf(value))
            return value > 0 ? "Infinity" : "-Infinity";
        std::array<char, 400> text{};
        double const magnitude = std::fabs(value);
        auto const written =
            std::to_chars(text.data(), std::to_address(text.end()), value,
                          magnitude == 0 || (magnitude >= 1e-7 && magnitude < 1e21) ? std::chars_format::fixed
                                                                                  : std::chars_format::scientific);
        detail::validity::throw_logic_error_if(written.ec != std::errc{},
                                               "cbor::diagnostic_notation: the text of a double does not fit");
        std::string out(text.data(), written.ptr);
        std::size_t const e = out.find('e');
        std::string mantissa(std::string_view(out).substr(0, e));
        if (mantissa.find('.') == std::string::npos)
            mantissa += ".0";
        if (e == std::string::npos)
            return mantissa;
        std::string_view exponent{std::span(out).subspan(e + 1)};
        char const sign = exponent.front();
        exponent.remove_prefix(1);
        exponent.remove_prefix(std::min(exponent.find_first_not_of('0'), exponent.size() - 1));
        return mantissa + "e" + sign + std::string(exponent);
    }

    static std::string float_of(std::uint8_t const info, std::uint64_t const argument)
    {
        std::size_t const width =
            info - std::to_underlying(rfc8949::simple_float_information::half_precision_float);
        double const value = heads::decode_float(info, argument);
        std::string const indicator =
            width == 0 ? std::string{} : std::string{'_', static_cast<char>('1' + width)};
        if (std::isnan(value)) {
            if (argument == quiet_nan[width])
                return "NaN" + indicator;
            std::string bytes(sizeof(std::uint16_t) << width, '\0');
            for (std::size_t i = 0; i < bytes.size(); ++i)
                bytes[i] = static_cast<char>(
                    argument >> (std::numeric_limits<std::uint8_t>::digits * (bytes.size() - 1 - i)));
            return "float'" + hex_of(bytes) + "'";
        }
        std::size_t const preferred =
            std::to_underlying(heads::preferred_float_info(value)) -
            std::to_underlying(rfc8949::simple_float_information::half_precision_float);
        return number_of(value) + (width == preferred ? std::string{} : indicator);
    }

    template <bool Checked>
    static std::expected<heads::head, error> decode_diagnostic_head(heads::decoder<Checked> &d)
    {
        if (auto const r = detail::validity::check_pending_items(heads::initial_byte_size, d.encoded.size());
            !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const initial = static_cast<std::uint8_t>(d.encoded.front());
        auto const major = static_cast<major_type>(initial >> rfc8949::additional_information_bits);
        std::uint8_t const info = heads::additional_information_of(initial);
        if (error const r = detail::validity::check_additional_information(major, info).error_or(error{});
            r != error{}) [[unlikely]]
            return std::unexpected(r);
        if (info != std::to_underlying(rfc8949::additional_information::indefinite_length))
            return d.decode_head();
        d.encoded.remove_prefix(1);
        return heads::head{major, info, 0};
    }

    template <bool Checked>
    static bool break_consumed(heads::decoder<Checked> &d)
    {
        if (!detail::validity::bytes_fit(heads::initial_byte_size, d.encoded.size()) ||
            d.encoded.front() !=
                heads::initial_byte(major_type::simple_float,
                                    std::to_underlying(rfc8949::simple_float_information::break_stop_code)))
            return false;
        d.encoded.remove_prefix(1);
        return true;
    }

    template <bool Checked>
    static std::expected<void, error>
    write_diagnostic(std::string &out, heads::decoder<Checked> &d)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const h = decode_diagnostic_head(d);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        bool const indefinite =
            h->info == std::to_underlying(rfc8949::additional_information::indefinite_length);
        switch (h->major) {
        case major_type::unsigned_integer:
            out += std::to_string(h->argument) + encoding_indicator(h->info, h->argument);
            return {};
        case major_type::negative_integer:
            out += h->argument == std::numeric_limits<std::uint64_t>::max() ? std::string("-18446744073709551616")
                                                                            : "-" + std::to_string(h->argument + 1);
            out += encoding_indicator(h->info, h->argument);
            return {};
        case major_type::byte_string:
        case major_type::text_string: {
            bool const text = h->major == major_type::text_string;
            if (!indefinite) {
                auto const s = d.decode_string(h->argument);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                out += text ? quoted_of(*s) : "h'" + hex_of(*s) + "'";
                out += encoding_indicator(h->info, h->argument);
                return {};
            }
            if (break_consumed(d)) {
                out += text ? "\"\"_" : "''_";
                return {};
            }
            out += "(_ ";
            for (bool first = true; !break_consumed(d); first = false) {
                if (!first)
                    out += ", ";
                if (auto const r =
                        detail::validity::check_pending_items(heads::initial_byte_size, d.encoded.size());
                    !r) [[unlikely]]
                    return std::unexpected(r.error());
                auto const initial = static_cast<std::uint8_t>(d.encoded.front());
                if (error const c =
                        detail::validity::check_chunk(
                            h->major,
                            static_cast<major_type>(initial >> rfc8949::additional_information_bits),
                            heads::additional_information_of(initial))
                            .error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                if (auto const r = write_diagnostic(out, d); !r) [[unlikely]]
                    return r;
            }
            out += ")";
            return {};
        }
        case major_type::array:
        case major_type::map: {
            bool const map = h->major == major_type::map;
            if (auto const r =
                    detail::validity::check_pending_items(h->major, h->info, h->argument, d.encoded.size());
                !r) [[unlikely]]
                return r;
            out += map ? "{" : "[";
            std::string const indicator = indefinite ? std::string("_") : encoding_indicator(h->info, h->argument);
            if (!indicator.empty())
                out += indicator + " ";
            for (std::uint64_t i = 0; indefinite ? !break_consumed(d) : i < h->argument; ++i) {
                if (i != 0)
                    out += ", ";
                if (auto const r = write_diagnostic(out, d); !r) [[unlikely]]
                    return r;
                if (map) {
                    out += ": ";
                    if (auto const r = write_diagnostic(out, d); !r) [[unlikely]]
                        return r;
                }
            }
            out += map ? "}" : "]";
            return {};
        }
        case major_type::tag: {
            out += std::to_string(h->argument) + encoding_indicator(h->info, h->argument) + "(";
            if (auto const r = write_diagnostic(out, d); !r) [[unlikely]]
                return r;
            out += ")";
            return {};
        }
        default:
            break;
        }
        switch (h->info) {
        case std::to_underlying(simple_value::false_value):
            out += "false";
            return {};
        case std::to_underlying(simple_value::true_value):
            out += "true";
            return {};
        case std::to_underlying(simple_value::null):
            out += "null";
            return {};
        case std::to_underlying(simple_value::undefined):
            out += "undefined";
            return {};
        case std::to_underlying(rfc8949::simple_float_information::simple_value_follows):
            if (error const r = detail::validity::check_simple_value(h->info, h->argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            out += "simple(" + std::to_string(h->argument) + ")";
            return {};
        case std::to_underlying(rfc8949::simple_float_information::half_precision_float):
        case std::to_underlying(rfc8949::simple_float_information::single_precision_float):
        case std::to_underlying(rfc8949::simple_float_information::double_precision_float):
            out += float_of(h->info, h->argument);
            return {};
        [[unlikely]] case std::to_underlying(rfc8949::additional_information::indefinite_length):
            return std::unexpected(error::syntax_error);
        default:
            out += "simple(" + std::to_string(h->argument) + ")";
            return {};
        }
    }

    friend class path;

    friend std::expected<std::string, error> diagnostic_notation(std::string_view encoded);
};

/**
 * @brief Gives the text of the item in @p encoded in the diagnostic notation of RFC 8949 section 8.
 *
 * A head that is not in preferred form carries its encoding indicator, for example `1_0`.
 * Indefinite length is printed, for example `[_ 1]`.
 * A tag is printed as its number and its content; tag 29 is not resolved.
 * @p encoded holds exactly one item.
 * The call does not check tag content, UTF-8 or duplicate keys.
 *
 * @code{.callseq}
 * cbor::diagnostic_notation(bytes) → std::expected<std::string, error>
 * @endcode
 * @code{.example}
 * bytes: a1 61 61 83 01 f9 41 00 41 01
 * call: cbor::diagnostic_notation(bytes)
 * result: "{\"a\": [1, 2.5, h'01']}"
 * @endcode
 * @code{.example}
 * bytes: 9f 01 ff
 * call: cbor::diagnostic_notation(bytes)
 * result: "[_ 1]"
 * @endcode
 * @code{.example}
 * bytes: c1 00
 * call: cbor::diagnostic_notation(bytes)
 * result: "1(0)"
 * @endcode
 *
 * @retval too_little_data the bytes end before the item ends, or a count is larger than the bytes that are left
 * @retval syntax_error a head is not well-formed, a chunk of an indefinite string has the wrong type, or bytes follow
 * the item
 * @retval nesting_depth_exceeded the item is deeper than the stack of the thread holds
 * @retval string_length_exceeded a string is longer than `limits::string_length`
 * @retval container_elements_exceeded the count of an array or a map is greater than `limits::container_elements`
 * @snippet doc_reference_tools.cpp diagnostic_notation
 * @see cbor::item_size()
 *
 * @ingroup tools
 */
inline std::expected<std::string, error> diagnostic_notation(std::string_view const encoded)
{
    heads::decoder<true> d{encoded};
    std::string out;
    if (auto const r =
            extended_diagnostic_notation::write_diagnostic(out, d);
        !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const one = detail::validity::check_one_data_item(d.encoded); !one) [[unlikely]]
        return std::unexpected(one.error());
    return out;
}

}
