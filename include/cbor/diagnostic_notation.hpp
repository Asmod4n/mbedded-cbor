/**
 * @file
 * Writes one encoded CBOR data item as text in diagnostic notation,
 * with its encoding indicators. Reads a literal in this notation into
 * CBOR for the path parser.
 * https://www.rfc-editor.org/rfc/rfc8949#section-8
 * https://datatracker.ietf.org/doc/html/draft-ietf-cbor-edn-literals-28
 * Parts of the JSON reader are ported from yyjson 0.13.0, Copyright (c) 2020 YaoYuan, MIT License.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "decode.hpp"
#include "encode.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "path.hpp"
#include "rfc2781.hpp"
#include "rfc3629.hpp"
#include "rfc4648.hpp"

namespace cbor
{

std::expected<owning<item>, error> parse_edn(std::string_view text);
constexpr std::expected<std::string, error> diag2cbor(std::string_view text);
std::expected<std::string, error> to_edn(item const &root);
std::expected<std::string, error> to_edn(owning<item> const &tree);

namespace detail
{

class edn_text;

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

    struct encoded_pair {
        std::size_t key;
        std::size_t value;
        std::size_t end;
    };

    struct string_literal {
        std::size_t at;
        major_type major;
        std::string content;
    };

    struct backpatch {
        std::size_t at;
        std::size_t unused;
    };

    class backpatches
    {
        backpatch outermost{};
        bool opened = false;
        std::vector<backpatch> inner;
        std::size_t unused_total = 0;

    public:
        constexpr std::size_t open(std::string &out)
        {
            std::size_t const at = out.size();
            out.append(detail::heads::head_size_max, '\0');
            if (!opened) {
                outermost = {at, 0};
                opened = true;
                return 0;
            }
            inner.push_back({at, 0});
            return inner.size();
        }

        constexpr std::size_t unused_so_far() const
        {
            return unused_total;
        }

        constexpr std::size_t content_start(std::size_t const index) const
        {
            return (index == 0 ? outermost.at : inner[index - 1].at) + detail::heads::head_size_max;
        }

        constexpr void close(std::string &out, std::size_t const index, std::string_view const head)
        {
            backpatch &b = index == 0 ? outermost : inner[index - 1];
            b.unused = detail::heads::head_size_max - head.size();
            std::ranges::copy(head, std::span(out).subspan(b.at + b.unused, head.size()).begin());
            unused_total += b.unused;
        }

        constexpr void apply(std::string &out) const
        {
            if (!opened)
                return;
            std::size_t write = outermost.at;
            std::size_t read = outermost.at + outermost.unused;
            auto const move_to = [&out, &write, &read](std::size_t const end) {
                std::span<char> const bytes(out);
                std::ranges::copy(bytes.subspan(read, end - read), bytes.subspan(write).begin());
                write += end - read;
            };
            for (backpatch const &b : inner) {
                move_to(b.at);
                read = b.at + b.unused;
            }
            move_to(out.size());
            out.resize(write);
        }
    };

    static constexpr int no_indicator = -1;
    static constexpr int immediate_indicator = -2;

    static constexpr int binary = 2;
    static constexpr int octal = 8;
    static constexpr int decimal = 10;
    static constexpr int hexadecimal = 16;

    static constexpr unsigned char unescaped_first = 0x20;

    static constexpr std::size_t repeat16 = 16;

    static constexpr std::uint8_t char_type_ascii = 1 << 0;
    static constexpr std::uint8_t char_type_space = 1 << 2;
    static constexpr std::uint8_t char_type_num = 1 << 4;
    static constexpr std::uint8_t char_type_comment = 1 << 5;

    static constexpr std::uint8_t char_type_sign = 1 << 0;
    static constexpr std::uint8_t char_type_digit = 1 << 1;
    static constexpr std::uint8_t char_type_nonzero = 1 << 2;
    static constexpr std::uint8_t char_type_exp = 1 << 3;
    static constexpr std::uint8_t char_type_dot = 1 << 4;

    static constexpr std::array<std::uint8_t, std::numeric_limits<unsigned char>::max() + 1> char_table1 = [] {
        std::array<std::uint8_t, std::numeric_limits<unsigned char>::max() + 1> t{};
        for (std::size_t c = 0; c < t.size(); ++c) {
            t[c] = static_cast<std::uint8_t>(
                (c >= unescaped_first && c <= rfc3629::one_octet_last && c != '"' && c != '\\' ? char_type_ascii : 0) |
                (c == ' ' || c == '\t' || c == '\n' || c == '\r' ? char_type_space : 0) |
                (c == '.' || c == '-' || c == '+' || (c >= '0' && c <= '9') ? char_type_num : 0) |
                (c == '/' || c == '#' ? char_type_comment : 0));
        }
        return t;
    }();

    static constexpr std::array<std::uint8_t, std::numeric_limits<unsigned char>::max() + 1> char_table3 = [] {
        std::array<std::uint8_t, std::numeric_limits<unsigned char>::max() + 1> t{};
        for (std::size_t c = 0; c < t.size(); ++c) {
            t[c] = static_cast<std::uint8_t>(
                (c == '+' || c == '-' ? char_type_sign : 0) | (c >= '0' && c <= '9' ? char_type_digit : 0) |
                (c >= '1' && c <= '9' ? char_type_nonzero : 0) | (c == 'e' || c == 'E' ? char_type_exp : 0) |
                (c == '.' ? char_type_dot : 0));
        }
        return t;
    }();

    static constexpr bool char_is_space(unsigned char const c)
    {
        return (char_table1[c] & char_type_space) != 0;
    }

    static constexpr bool char_is_num(unsigned char const c)
    {
        return (char_table1[c] & char_type_num) != 0;
    }

    static constexpr bool char_is_ascii_skip(unsigned char const c)
    {
        return (char_table1[c] & char_type_ascii) != 0;
    }

    static constexpr bool char_is_trivia(unsigned char const c)
    {
        return (char_table1[c] & (char_type_space | char_type_comment)) != 0;
    }

    static constexpr bool char_is_sign(unsigned char const d)
    {
        return (char_table3[d] & char_type_sign) != 0;
    }

    static constexpr bool char_is_nonzero(unsigned char const d)
    {
        return (char_table3[d] & char_type_nonzero) != 0;
    }

    static constexpr bool char_is_digit(unsigned char const d)
    {
        return (char_table3[d] & char_type_digit) != 0;
    }

    static constexpr bool char_is_exp(unsigned char const d)
    {
        return (char_table3[d] & char_type_exp) != 0;
    }

    static constexpr bool char_is_fp(unsigned char const d)
    {
        return (char_table3[d] & (char_type_dot | char_type_exp)) != 0;
    }

    static constexpr bool char_is_digit_or_fp(unsigned char const d)
    {
        return (char_table3[d] & (char_type_digit | char_type_dot | char_type_exp)) != 0;
    }

    static constexpr bool char_is_ctn(unsigned char const c)
    {
        return c == '[' || c == '{';
    }

    static constexpr unsigned char padded_char(std::string_view const text, std::size_t const at)
    {
        if (!detail::validity::check_text_left(text, at, 1))
            return 0;
        return static_cast<unsigned char>(text[at]);
    }

    static constexpr std::size_t skip_ascii(std::string_view const text, std::size_t at)
    {
        while (detail::validity::check_text_left(text, at, repeat16)) {
            for (std::size_t i = 0; i < repeat16; ++i)
                if (!char_is_ascii_skip(static_cast<unsigned char>(text[at + i])))
                    return at + i;
            at += repeat16;
        }
        while (char_is_ascii_skip(padded_char(text, at)))
            ++at;
        return at;
    }

    static constexpr std::size_t skip_utf8(std::string_view const text, std::size_t at)
    {
        while (padded_char(text, at) > rfc3629::one_octet_last)
            ++at;
        return at;
    }

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
                       : detail::heads::preferred_argument_info(argument);
        else if (indicator == immediate_indicator)
            info = 0;
        else
            info = indicator;
        if (info == 0) {
            if (argument >= static_cast<std::uint64_t>(rfc8949::additional_information::one_byte_argument))
                [[unlikely]]
                return std::unexpected(error::invalid_path);
            detail::heads::append_head(out, major, static_cast<std::uint8_t>(argument), argument);
            return {};
        }
        if (info < static_cast<int>(rfc8949::additional_information::one_byte_argument) ||
            info > static_cast<int>(rfc8949::additional_information::eight_byte_argument)) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t const size = detail::heads::argument_size(static_cast<std::uint8_t>(info));
        if (size < sizeof(std::uint64_t) &&
            argument >> (std::numeric_limits<std::uint8_t>::digits * size) != 0) [[unlikely]]
            return std::unexpected(error::invalid_path);
        detail::heads::append_head(out, major, static_cast<std::uint8_t>(info), argument);
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
            if (quote == '"') {
                std::size_t run_end = skip_ascii(text, at);
                while (padded_char(text, run_end) > rfc3629::one_octet_last)
                    run_end = skip_ascii(text, skip_utf8(text, run_end));
                out.append(detail::validity::checked_substr(text, at, run_end - at));
                at = run_end;
            }
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
        rfc8949::simple_information const preferred = detail::ieee754::preferred_float_info(value);
        int info;
        if (indicator == no_indicator)
            info = std::to_underlying(preferred);
        else if (indicator >= std::to_underlying(rfc8949::simple_information::half_precision_float) &&
                 indicator <= std::to_underlying(rfc8949::simple_information::double_precision_float))
            info = indicator;
        else [[unlikely]]
            return std::unexpected(error::invalid_path);
        bool const exact =
            nan || infinite || info >= std::to_underlying(preferred) ||
            (info == std::to_underlying(rfc8949::simple_information::single_precision_float) &&
             static_cast<double>(static_cast<float>(value)) == value);
        if (!exact) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::uint64_t const argument =
            nan ? quiet_nan[static_cast<std::size_t>(
                      info - std::to_underlying(rfc8949::simple_information::half_precision_float))]
                : detail::ieee754::encode_float(static_cast<rfc8949::simple_information>(info), value);
        return append_head(out, major_type::simple, argument, info);
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

    static constexpr std::expected<void, error> append_bignum(std::string &out, std::string_view const digits, int const base,
                                                              bool const negative)
    {
        constexpr int byte_bits = std::numeric_limits<std::uint8_t>::digits;
        constexpr unsigned byte_mask = std::numeric_limits<std::uint8_t>::max();
        std::vector<std::uint8_t> magnitude;
        for (char const c : digits) {
            unsigned carry = static_cast<unsigned>(hex_digit_value(c));
            for (std::uint8_t &b : magnitude) {
                unsigned const x = b * static_cast<unsigned>(base) + carry;
                b = static_cast<std::uint8_t>(x & byte_mask);
                carry = x >> byte_bits;
            }
            for (; carry != 0; carry >>= byte_bits)
                magnitude.push_back(static_cast<std::uint8_t>(carry & byte_mask));
        }
        if (negative)
            for (std::uint8_t &b : magnitude)
                if (b-- != 0)
                    break;
        while (!magnitude.empty() && magnitude.back() == 0)
            magnitude.pop_back();
        auto const tag = negative ? rfc8949::tag_number::negative_bignum : rfc8949::tag_number::unsigned_bignum;
        if (auto const r = append_head(out, major_type::tag, std::to_underlying(tag), no_indicator); !r) [[unlikely]]
            return r;
        if (auto const r = append_head(out, major_type::byte_string, magnitude.size(), no_indicator); !r) [[unlikely]]
            return r;
        for (std::uint8_t const b : magnitude | std::views::reverse)
            out.push_back(static_cast<char>(b));
        return {};
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
            if (overflow) {
                if (indicator != no_indicator) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (auto const r = append_bignum(out, detail::validity::checked_substr(text, first, at - first), base, negative); !r) [[unlikely]]
                    return std::unexpected(r.error());
                return next->at;
            }
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

    static constexpr std::expected<std::size_t, error> parse_container(literal_cursor const cursor, std::string &out,
                                                                       backpatches &patches)
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
        if (indicator == std::to_underlying(rfc8949::additional_information::indefinite_length)) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t const patch = patches.open(out);
        std::uint64_t count = 0;
        bool separated = true;
        while (at < text.size() && text[at] != close) {
            if (!separated) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto next = parse_literal({text, at}, out, patches);
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
                next = parse_literal({text, *value}, out, patches);
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
        std::string head;
        if (auto const r = append_head(head, major, count, indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        patches.close(out, patch, head);
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
        if (indicator == std::to_underlying(rfc8949::additional_information::indefinite_length)) [[unlikely]]
            return std::unexpected(error::invalid_path);
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

    static constexpr std::expected<std::size_t, error> parse_embedded(literal_cursor const cursor, std::string &out,
                                                                      backpatches &patches)
    {
        std::string_view const text = cursor.text;
        auto const first = optional_space_end(text, cursor.at + std::string_view("<<").size());
        if (!first) [[unlikely]]
            return first;
        std::size_t const patch = patches.open(out);
        std::size_t const unused_before = patches.unused_so_far();
        std::size_t next = *first;
        bool separated = true;
        while (!std::string_view{std::span(text).subspan(next)}.starts_with(std::string_view(">>"))) {
            if (!separated || !detail::validity::check_text_left(text, next, 1)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto const item = parse_literal({text, next}, out, patches);
            if (!item) [[unlikely]]
                return item;
            auto const s = parse_separator(text, *item);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            next = s->at;
            separated = s->separated;
        }
        auto const after = parse_indicator(text, next + std::string_view(">>").size());
        if (!after) [[unlikely]]
            return std::unexpected(after.error());
        if (after->indicator == std::to_underlying(rfc8949::additional_information::indefinite_length)) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t const length =
            out.size() - patches.content_start(patch) - (patches.unused_so_far() - unused_before);
        std::string head;
        if (auto const r = append_head(head, major_type::byte_string, length, after->indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        patches.close(out, patch, head);
        return after->at;
    }

    static constexpr std::expected<std::size_t, error> parse_literal(literal_cursor const cursor, std::string &out,
                                                                     backpatches &patches)
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
            return parse_container(cursor, out, patches);
        if (rest.starts_with("<<"))
            return parse_embedded(cursor, out, patches);
        if (string_literal_starts(rest)) {
            auto const string = parse_string_literal(cursor);
            if (!string) [[unlikely]]
                return std::unexpected(string.error());
            return string_finish(text, string->at, out, string->major, string->content);
        }
        constexpr auto names = std::to_array<std::string_view>({"false", "true", "null", "undefined"});
        for (std::size_t i = 0; i < names.size(); ++i)
            if (rest.starts_with(names[i])) {
                out.push_back(detail::heads::initial_byte(major_type::simple, std::to_underlying(simple_value::false_value) + i));
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
                !detail::validity::check_simple_value(detail::heads::preferred_argument_info(value), value))
                [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (auto const r = append_head(out, major_type::simple, value, no_indicator); !r) [[unlikely]]
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
            auto const content = parse_literal({text, *opened}, out, patches);
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

    static item scalar_item(major_type const major, std::uint64_t const argument)
    {
        std::uint8_t const info = detail::heads::preferred_argument_info(argument);
        return item{detail::heads::type_of(major, info, argument), info, {.u64 = argument}};
    }

    struct sax_dom_parser {
        zone &decoded;
        std::vector<item> &values;

        std::expected<void, error> null()
        {
            item simple = scalar_item(major_type::simple, std::to_underlying(simple_value::null));
            simple.via.simple_value = simple_value::null;
            values.push_back(simple);
            return {};
        }

        std::expected<void, error> boolean(bool const value)
        {
            simple_value const v = value ? simple_value::true_value : simple_value::false_value;
            item simple = scalar_item(major_type::simple, std::to_underlying(v));
            simple.via.simple_value = v;
            values.push_back(simple);
            return {};
        }

        std::expected<void, error> number_unsigned(std::uint64_t const value)
        {
            values.push_back(scalar_item(major_type::unsigned_integer, value));
            return {};
        }

        std::expected<void, error> number_integer(std::uint64_t const argument)
        {
            values.push_back(scalar_item(major_type::negative_integer, argument));
            return {};
        }

        std::expected<void, error> number_float(double const value)
        {
            std::uint8_t const info = std::to_underlying(detail::ieee754::preferred_float_info(value));
            values.push_back(item{detail::heads::type_of(major_type::simple, info, 0), info, {.f64 = value}});
            return {};
        }

        std::expected<void, error> string(std::string_view const content, std::size_t const text_left)
        {
            if (!detail::validity::check_argument<true>(major_type::text_string, content.size())) [[unlikely]]
                return std::unexpected(error::syntax_error);
            item string = scalar_item(major_type::text_string, content.size());
            string.via.str = decoded.strings.save(
                content, content.size() + std::min(detail::validity::string_saver_chunk_size, text_left));
            values.push_back(string);
            return {};
        }

        std::size_t start_object() const
        {
            return values.size();
        }

        std::size_t start_array() const
        {
            return values.size();
        }

        std::expected<void, error> end_object(std::size_t const start, std::size_t const pairs)
        {
            return end(start, major_type::map, pairs);
        }

        std::expected<void, error> end_array(std::size_t const start, std::size_t const elements)
        {
            return end(start, major_type::array, elements);
        }

        std::expected<void, error> end(std::size_t const start, major_type const major, std::size_t const count)
        {
            if (!detail::validity::check_argument<true>(major, count)) [[unlikely]]
                return std::unexpected(error::syntax_error);
            std::size_t const n = values.size() - start;
            std::span<item const> const children = std::span<item const>(values).subspan(start);
            std::pmr::vector<item> &block = decoded.items_reserved(n);
            std::ranges::copy(children, std::back_inserter(block));
            while (values.size() != start)
                values.pop_back();
            item container = scalar_item(major, count);
            if (major == major_type::map)
                container.via.map = block;
            else
                container.via.array = block;
            values.push_back(container);
            return {};
        }
    };

    struct writer {
        std::string &out;
        backpatches &patches;

        constexpr std::expected<void, error> null()
        {
            out.push_back(detail::heads::initial_byte(major_type::simple, std::to_underlying(simple_value::null)));
            return {};
        }

        constexpr std::expected<void, error> boolean(bool const value)
        {
            simple_value const v = value ? simple_value::true_value : simple_value::false_value;
            out.push_back(detail::heads::initial_byte(major_type::simple, std::to_underlying(v)));
            return {};
        }

        constexpr std::expected<void, error> number_unsigned(std::uint64_t const value)
        {
            return append_head(out, major_type::unsigned_integer, value, no_indicator);
        }

        constexpr std::expected<void, error> number_integer(std::uint64_t const argument)
        {
            return append_head(out, major_type::negative_integer, argument, no_indicator);
        }

        constexpr std::expected<void, error> number_float(double const value)
        {
            return append_float(out, value, no_indicator);
        }

        constexpr std::expected<void, error> string(std::string_view const content, std::size_t const text_left)
        {
            static_cast<void>(text_left);
            if (auto const r = append_head(out, major_type::text_string, content.size(), no_indicator); !r) [[unlikely]]
                return r;
            out += content;
            return {};
        }

        constexpr std::size_t start_object()
        {
            return patches.open(out);
        }

        constexpr std::size_t start_array()
        {
            return patches.open(out);
        }

        constexpr std::expected<void, error> end_object(std::size_t const patch, std::size_t const pairs)
        {
            return end(patch, major_type::map, pairs);
        }

        constexpr std::expected<void, error> end_array(std::size_t const patch, std::size_t const elements)
        {
            return end(patch, major_type::array, elements);
        }

        constexpr std::expected<void, error> end(std::size_t const patch, major_type const major, std::size_t const count)
        {
            std::string head;
            if (auto const r = append_head(head, major, count, no_indicator); !r) [[unlikely]]
                return r;
            patches.close(out, patch, head);
            return {};
        }
    };

    static constexpr bool value_ends(std::string_view const text, std::size_t const at)
    {
        auto const closes = [text](std::size_t const p) {
            unsigned char const c = padded_char(text, p);
            return !detail::validity::check_text_left(text, p, 1) || c == ',' || c == ']' || c == '}' || c == ':';
        };
        if (closes(at))
            return true;
        if (!char_is_trivia(padded_char(text, at)))
            return false;
        auto const next = optional_space_end(text, at);
        return next && closes(*next);
    }

    struct uint128 {
        std::uint64_t hi;
        std::uint64_t lo;
    };

    struct diy_fp {
        std::uint64_t sig;
        std::int32_t exp;
    };

    static constexpr int u64_safe_dig = std::numeric_limits<std::uint64_t>::digits10;
    static constexpr std::int32_t f64_max_dec_dig = 768;
    static constexpr std::int32_t f64_max_dec_exp = std::numeric_limits<double>::max_exponent10;
    static constexpr std::int32_t f64_min_dec_exp = -324;
    static constexpr std::int32_t f64_max_bin_exp = std::numeric_limits<double>::max_exponent;
    static constexpr std::int32_t f64_min_bin_exp = std::numeric_limits<double>::min_exponent;
    static constexpr std::int32_t f64_bits = std::numeric_limits<std::uint64_t>::digits;
    static constexpr std::int32_t f64_sig_bits = std::numeric_limits<double>::digits - 1;
    static constexpr std::int32_t f64_sig_full_bits = std::numeric_limits<double>::digits;
    static constexpr std::uint64_t f64_sig_mask = (std::uint64_t{1} << f64_sig_bits) - 1;
    static constexpr std::uint64_t f64_exp_mask = ~f64_sig_mask & ~(std::uint64_t{1} << (f64_bits - 1));
    static constexpr std::int32_t f64_exp_bias = std::numeric_limits<double>::max_exponent - 1;
    static constexpr std::uint64_t f64_bits_inf = std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity());

#if defined(FLT_EVAL_METHOD)
    static constexpr bool double_math_correct = FLT_EVAL_METHOD == 0 || FLT_EVAL_METHOD == 1;
#else
    static constexpr bool double_math_correct = false;
#endif

    static constexpr std::int32_t f64_pow10_max_exact_exp = 22;
    static constexpr std::array<double, f64_pow10_max_exact_exp + 1> f64_pow10_table = [] {
        std::array<double, f64_pow10_max_exact_exp + 1> t{};
        double p = 1.0;
        for (double &v : t) {
            v = p;
            p *= decimal;
        }
        return t;
    }();

    static constexpr std::int32_t u64_pow10_max_exact_exp = 19;
    static constexpr std::array<std::uint64_t, u64_pow10_max_exact_exp + 1> u64_pow10_table = [] {
        std::array<std::uint64_t, u64_pow10_max_exact_exp + 1> t{};
        std::uint64_t p = 1;
        for (std::uint64_t &v : t) {
            v = p;
            p *= decimal;
        }
        return t;
    }();

    static constexpr int exponent_beyond_double = 100000;

    static constexpr std::uint8_t hex_conv_invalid = 0xf0;
    static constexpr std::array<std::uint8_t, std::numeric_limits<unsigned char>::max() + 1> hex_conv_table = [] {
        std::array<std::uint8_t, std::numeric_limits<unsigned char>::max() + 1> t{};
        for (std::size_t c = 0; c < t.size(); ++c) {
            char const upper = c >= 'a' && c <= 'f' ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
            std::size_t const v = c <= rfc3629::one_octet_last ? rfc4648::base16_alphabet.find(upper) : std::string_view::npos;
            t[c] = v == std::string_view::npos ? hex_conv_invalid : static_cast<std::uint8_t>(v);
        }
        return t;
    }();

    static constexpr std::int32_t pow10_sig_table_min_exp = -343;
    static constexpr std::int32_t pow10_sig_table_max_exp = 324;
    static constexpr std::array<uint128, pow10_sig_table_max_exp - pow10_sig_table_min_exp + 1> pow10_sig_table{{
        {0xbf29dcaba82fdeae, 0x7432ee873880fc33}, {0xeef453d6923bd65a, 0x113faa2906a13b3f},
        {0x9558b4661b6565f8, 0x4ac7ca59a424c507}, {0xbaaee17fa23ebf76, 0x5d79bcf00d2df649},
        {0xe95a99df8ace6f53, 0xf4d82c2c107973dc}, {0x91d8a02bb6c10594, 0x79071b9b8a4be869},
        {0xb64ec836a47146f9, 0x9748e2826cdee284}, {0xe3e27a444d8d98b7, 0xfd1b1b2308169b25},
        {0x8e6d8c6ab0787f72, 0xfe30f0f5e50e20f7}, {0xb208ef855c969f4f, 0xbdbd2d335e51a935},
        {0xde8b2b66b3bc4723, 0xad2c788035e61382}, {0x8b16fb203055ac76, 0x4c3bcb5021afcc31},
        {0xaddcb9e83c6b1793, 0xdf4abe242a1bbf3d}, {0xd953e8624b85dd78, 0xd71d6dad34a2af0d},
        {0x87d4713d6f33aa6b, 0x8672648c40e5ad68}, {0xa9c98d8ccb009506, 0x680efdaf511f18c2},
        {0xd43bf0effdc0ba48, 0x0212bd1b2566def2}, {0x84a57695fe98746d, 0x014bb630f7604b57},
        {0xa5ced43b7e3e9188, 0x419ea3bd35385e2d}, {0xcf42894a5dce35ea, 0x52064cac828675b9},
        {0x818995ce7aa0e1b2, 0x7343efebd1940993}, {0xa1ebfb4219491a1f, 0x1014ebe6c5f90bf8},
        {0xca66fa129f9b60a6, 0xd41a26e077774ef6}, {0xfd00b897478238d0, 0x8920b098955522b4},
        {0x9e20735e8cb16382, 0x55b46e5f5d5535b0}, {0xc5a890362fddbc62, 0xeb2189f734aa831d},
        {0xf712b443bbd52b7b, 0xa5e9ec7501d523e4}, {0x9a6bb0aa55653b2d, 0x47b233c92125366e},
        {0xc1069cd4eabe89f8, 0x999ec0bb696e840a}, {0xf148440a256e2c76, 0xc00670ea43ca250d},
        {0x96cd2a865764dbca, 0x380406926a5e5728}, {0xbc807527ed3e12bc, 0xc605083704f5ecf2},
        {0xeba09271e88d976b, 0xf7864a44c633682e}, {0x93445b8731587ea3, 0x7ab3ee6afbe0211d},
        {0xb8157268fdae9e4c, 0x5960ea05bad82964}, {0xe61acf033d1a45df, 0x6fb92487298e33bd},
        {0x8fd0c16206306bab, 0xa5d3b6d479f8e056}, {0xb3c4f1ba87bc8696, 0x8f48a4899877186c},
        {0xe0b62e2929aba83c, 0x331acdabfe94de87}, {0x8c71dcd9ba0b4925, 0x9ff0c08b7f1d0b14},
        {0xaf8e5410288e1b6f, 0x07ecf0ae5ee44dd9}, {0xdb71e91432b1a24a, 0xc9e82cd9f69d6150},
        {0x892731ac9faf056e, 0xbe311c083a225cd2}, {0xab70fe17c79ac6ca, 0x6dbd630a48aaf406},
        {0xd64d3d9db981787d, 0x092cbbccdad5b108}, {0x85f0468293f0eb4e, 0x25bbf56008c58ea5},
        {0xa76c582338ed2621, 0xaf2af2b80af6f24e}, {0xd1476e2c07286faa, 0x1af5af660db4aee1},
        {0x82cca4db847945ca, 0x50d98d9fc890ed4d}, {0xa37fce126597973c, 0xe50ff107bab528a0},
        {0xcc5fc196fefd7d0c, 0x1e53ed49a96272c8}, {0xff77b1fcbebcdc4f, 0x25e8e89c13bb0f7a},
        {0x9faacf3df73609b1, 0x77b191618c54e9ac}, {0xc795830d75038c1d, 0xd59df5b9ef6a2417},
        {0xf97ae3d0d2446f25, 0x4b0573286b44ad1d}, {0x9becce62836ac577, 0x4ee367f9430aec32},
        {0xc2e801fb244576d5, 0x229c41f793cda73f}, {0xf3a20279ed56d48a, 0x6b43527578c1110f},
        {0x9845418c345644d6, 0x830a13896b78aaa9}, {0xbe5691ef416bd60c, 0x23cc986bc656d553},
        {0xedec366b11c6cb8f, 0x2cbfbe86b7ec8aa8}, {0x94b3a202eb1c3f39, 0x7bf7d71432f3d6a9},
        {0xb9e08a83a5e34f07, 0xdaf5ccd93fb0cc53}, {0xe858ad248f5c22c9, 0xd1b3400f8f9cff68},
        {0x91376c36d99995be, 0x23100809b9c21fa1}, {0xb58547448ffffb2d, 0xabd40a0c2832a78a},
        {0xe2e69915b3fff9f9, 0x16c90c8f323f516c}, {0x8dd01fad907ffc3b, 0xae3da7d97f6792e3},
        {0xb1442798f49ffb4a, 0x99cd11cfdf41779c}, {0xdd95317f31c7fa1d, 0x40405643d711d583},
        {0x8a7d3eef7f1cfc52, 0x482835ea666b2572}, {0xad1c8eab5ee43b66, 0xda3243650005eecf},
        {0xd863b256369d4a40, 0x90bed43e40076a82}, {0x873e4f75e2224e68, 0x5a7744a6e804a291},
        {0xa90de3535aaae202, 0x711515d0a205cb36}, {0xd3515c2831559a83, 0x0d5a5b44ca873e03},
        {0x8412d9991ed58091, 0xe858790afe9486c2}, {0xa5178fff668ae0b6, 0x626e974dbe39a872},
        {0xce5d73ff402d98e3, 0xfb0a3d212dc8128f}, {0x80fa687f881c7f8e, 0x7ce66634bc9d0b99},
        {0xa139029f6a239f72, 0x1c1fffc1ebc44e80}, {0xc987434744ac874e, 0xa327ffb266b56220},
        {0xfbe9141915d7a922, 0x4bf1ff9f0062baa8}, {0x9d71ac8fada6c9b5, 0x6f773fc3603db4a9},
        {0xc4ce17b399107c22, 0xcb550fb4384d21d3}, {0xf6019da07f549b2b, 0x7e2a53a146606a48},
        {0x99c102844f94e0fb, 0x2eda7444cbfc426d}, {0xc0314325637a1939, 0xfa911155fefb5308},
        {0xf03d93eebc589f88, 0x793555ab7eba27ca}, {0x96267c7535b763b5, 0x4bc1558b2f3458de},
        {0xbbb01b9283253ca2, 0x9eb1aaedfb016f16}, {0xea9c227723ee8bcb, 0x465e15a979c1cadc},
        {0x92a1958a7675175f, 0x0bfacd89ec191ec9}, {0xb749faed14125d36, 0xcef980ec671f667b},
        {0xe51c79a85916f484, 0x82b7e12780e7401a}, {0x8f31cc0937ae58d2, 0xd1b2ecb8b0908810},
        {0xb2fe3f0b8599ef07, 0x861fa7e6dcb4aa15}, {0xdfbdcece67006ac9, 0x67a791e093e1d49a},
        {0x8bd6a141006042bd, 0xe0c8bb2c5c6d24e0}, {0xaecc49914078536d, 0x58fae9f773886e18},
        {0xda7f5bf590966848, 0xaf39a475506a899e}, {0x888f99797a5e012d, 0x6d8406c952429603},
        {0xaab37fd7d8f58178, 0xc8e5087ba6d33b83}, {0xd5605fcdcf32e1d6, 0xfb1e4a9a90880a64},
        {0x855c3be0a17fcd26, 0x5cf2eea09a55067f}, {0xa6b34ad8c9dfc06f, 0xf42faa48c0ea481e},
        {0xd0601d8efc57b08b, 0xf13b94daf124da26}, {0x823c12795db6ce57, 0x76c53d08d6b70858},
        {0xa2cb1717b52481ed, 0x54768c4b0c64ca6e}, {0xcb7ddcdda26da268, 0xa9942f5dcf7dfd09},
        {0xfe5d54150b090b02, 0xd3f93b35435d7c4c}, {0x9efa548d26e5a6e1, 0xc47bc5014a1a6daf},
        {0xc6b8e9b0709f109a, 0x359ab6419ca1091b}, {0xf867241c8cc6d4c0, 0xc30163d203c94b62},
        {0x9b407691d7fc44f8, 0x79e0de63425dcf1d}, {0xc21094364dfb5636, 0x985915fc12f542e4},
        {0xf294b943e17a2bc4, 0x3e6f5b7b17b2939d}, {0x979cf3ca6cec5b5a, 0xa705992ceecf9c42},
        {0xbd8430bd08277231, 0x50c6ff782a838353}, {0xece53cec4a314ebd, 0xa4f8bf5635246428},
        {0x940f4613ae5ed136, 0x871b7795e136be99}, {0xb913179899f68584, 0x28e2557b59846e3f},
        {0xe757dd7ec07426e5, 0x331aeada2fe589cf}, {0x9096ea6f3848984f, 0x3ff0d2c85def7621},
        {0xb4bca50b065abe63, 0x0fed077a756b53a9}, {0xe1ebce4dc7f16dfb, 0xd3e8495912c62894},
        {0x8d3360f09cf6e4bd, 0x64712dd7abbbd95c}, {0xb080392cc4349dec, 0xbd8d794d96aacfb3},
        {0xdca04777f541c567, 0xecf0d7a0fc5583a0}, {0x89e42caaf9491b60, 0xf41686c49db57244},
        {0xac5d37d5b79b6239, 0x311c2875c522ced5}, {0xd77485cb25823ac7, 0x7d633293366b828b},
        {0x86a8d39ef77164bc, 0xae5dff9c02033197}, {0xa8530886b54dbdeb, 0xd9f57f830283fdfc},
        {0xd267caa862a12d66, 0xd072df63c324fd7b}, {0x8380dea93da4bc60, 0x4247cb9e59f71e6d},
        {0xa46116538d0deb78, 0x52d9be85f074e608}, {0xcd795be870516656, 0x67902e276c921f8b},
        {0x806bd9714632dff6, 0x00ba1cd8a3db53b6}, {0xa086cfcd97bf97f3, 0x80e8a40eccd228a4},
        {0xc8a883c0fdaf7df0, 0x6122cd128006b2cd}, {0xfad2a4b13d1b5d6c, 0x796b805720085f81},
        {0x9cc3a6eec6311a63, 0xcbe3303674053bb0}, {0xc3f490aa77bd60fc, 0xbedbfc4411068a9c},
        {0xf4f1b4d515acb93b, 0xee92fb5515482d44}, {0x991711052d8bf3c5, 0x751bdd152d4d1c4a},
        {0xbf5cd54678eef0b6, 0xd262d45a78a0635d}, {0xef340a98172aace4, 0x86fb897116c87c34},
        {0x9580869f0e7aac0e, 0xd45d35e6ae3d4da0}, {0xbae0a846d2195712, 0x8974836059cca109},
        {0xe998d258869facd7, 0x2bd1a438703fc94b}, {0x91ff83775423cc06, 0x7b6306a34627ddcf},
        {0xb67f6455292cbf08, 0x1a3bc84c17b1d542}, {0xe41f3d6a7377eeca, 0x20caba5f1d9e4a93},
        {0x8e938662882af53e, 0x547eb47b7282ee9c}, {0xb23867fb2a35b28d, 0xe99e619a4f23aa43},
        {0xdec681f9f4c31f31, 0x6405fa00e2ec94d4}, {0x8b3c113c38f9f37e, 0xde83bc408dd3dd04},
        {0xae0b158b4738705e, 0x9624ab50b148d445}, {0xd98ddaee19068c76, 0x3badd624dd9b0957},
        {0x87f8a8d4cfa417c9, 0xe54ca5d70a80e5d6}, {0xa9f6d30a038d1dbc, 0x5e9fcf4ccd211f4c},
        {0xd47487cc8470652b, 0x7647c3200069671f}, {0x84c8d4dfd2c63f3b, 0x29ecd9f40041e073},
        {0xa5fb0a17c777cf09, 0xf468107100525890}, {0xcf79cc9db955c2cc, 0x7182148d4066eeb4},
        {0x81ac1fe293d599bf, 0xc6f14cd848405530}, {0xa21727db38cb002f, 0xb8ada00e5a506a7c},
        {0xca9cf1d206fdc03b, 0xa6d90811f0e4851c}, {0xfd442e4688bd304a, 0x908f4a166d1da663},
        {0x9e4a9cec15763e2e, 0x9a598e4e043287fe}, {0xc5dd44271ad3cdba, 0x40eff1e1853f29fd},
        {0xf7549530e188c128, 0xd12bee59e68ef47c}, {0x9a94dd3e8cf578b9, 0x82bb74f8301958ce},
        {0xc13a148e3032d6e7, 0xe36a52363c1faf01}, {0xf18899b1bc3f8ca1, 0xdc44e6c3cb279ac1},
        {0x96f5600f15a7b7e5, 0x29ab103a5ef8c0b9}, {0xbcb2b812db11a5de, 0x7415d448f6b6f0e7},
        {0xebdf661791d60f56, 0x111b495b3464ad21}, {0x936b9fcebb25c995, 0xcab10dd900beec34},
        {0xb84687c269ef3bfb, 0x3d5d514f40eea742}, {0xe65829b3046b0afa, 0x0cb4a5a3112a5112},
        {0x8ff71a0fe2c2e6dc, 0x47f0e785eaba72ab}, {0xb3f4e093db73a093, 0x59ed216765690f56},
        {0xe0f218b8d25088b8, 0x306869c13ec3532c}, {0x8c974f7383725573, 0x1e414218c73a13fb},
        {0xafbd2350644eeacf, 0xe5d1929ef90898fa}, {0xdbac6c247d62a583, 0xdf45f746b74abf39},
        {0x894bc396ce5da772, 0x6b8bba8c328eb783}, {0xab9eb47c81f5114f, 0x066ea92f3f326564},
        {0xd686619ba27255a2, 0xc80a537b0efefebd}, {0x8613fd0145877585, 0xbd06742ce95f5f36},
        {0xa798fc4196e952e7, 0x2c48113823b73704}, {0xd17f3b51fca3a7a0, 0xf75a15862ca504c5},
        {0x82ef85133de648c4, 0x9a984d73dbe722fb}, {0xa3ab66580d5fdaf5, 0xc13e60d0d2e0ebba},
        {0xcc963fee10b7d1b3, 0x318df905079926a8}, {0xffbbcfe994e5c61f, 0xfdf17746497f7052},
        {0x9fd561f1fd0f9bd3, 0xfeb6ea8bedefa633}, {0xc7caba6e7c5382c8, 0xfe64a52ee96b8fc0},
        {0xf9bd690a1b68637b, 0x3dfdce7aa3c673b0}, {0x9c1661a651213e2d, 0x06bea10ca65c084e},
        {0xc31bfa0fe5698db8, 0x486e494fcff30a62}, {0xf3e2f893dec3f126, 0x5a89dba3c3efccfa},
        {0x986ddb5c6b3a76b7, 0xf89629465a75e01c}, {0xbe89523386091465, 0xf6bbb397f1135823},
        {0xee2ba6c0678b597f, 0x746aa07ded582e2c}, {0x94db483840b717ef, 0xa8c2a44eb4571cdc},
        {0xba121a4650e4ddeb, 0x92f34d62616ce413}, {0xe896a0d7e51e1566, 0x77b020baf9c81d17},
        {0x915e2486ef32cd60, 0x0ace1474dc1d122e}, {0xb5b5ada8aaff80b8, 0x0d819992132456ba},
        {0xe3231912d5bf60e6, 0x10e1fff697ed6c69}, {0x8df5efabc5979c8f, 0xca8d3ffa1ef463c1},
        {0xb1736b96b6fd83b3, 0xbd308ff8a6b17cb2}, {0xddd0467c64bce4a0, 0xac7cb3f6d05ddbde},
        {0x8aa22c0dbef60ee4, 0x6bcdf07a423aa96b}, {0xad4ab7112eb3929d, 0x86c16c98d2c953c6},
        {0xd89d64d57a607744, 0xe871c7bf077ba8b7}, {0x87625f056c7c4a8b, 0x11471cd764ad4972},
        {0xa93af6c6c79b5d2d, 0xd598e40d3dd89bcf}, {0xd389b47879823479, 0x4aff1d108d4ec2c3},
        {0x843610cb4bf160cb, 0xcedf722a585139ba}, {0xa54394fe1eedb8fe, 0xc2974eb4ee658828},
        {0xce947a3da6a9273e, 0x733d226229feea32}, {0x811ccc668829b887, 0x0806357d5a3f525f},
        {0xa163ff802a3426a8, 0xca07c2dcb0cf26f7}, {0xc9bcff6034c13052, 0xfc89b393dd02f0b5},
        {0xfc2c3f3841f17c67, 0xbbac2078d443ace2}, {0x9d9ba7832936edc0, 0xd54b944b84aa4c0d},
        {0xc5029163f384a931, 0x0a9e795e65d4df11}, {0xf64335bcf065d37d, 0x4d4617b5ff4a16d5},
        {0x99ea0196163fa42e, 0x504bced1bf8e4e45}, {0xc06481fb9bcf8d39, 0xe45ec2862f71e1d6},
        {0xf07da27a82c37088, 0x5d767327bb4e5a4c}, {0x964e858c91ba2655, 0x3a6a07f8d510f86f},
        {0xbbe226efb628afea, 0x890489f70a55368b}, {0xeadab0aba3b2dbe5, 0x2b45ac74ccea842e},
        {0x92c8ae6b464fc96f, 0x3b0b8bc90012929d}, {0xb77ada0617e3bbcb, 0x09ce6ebb40173744},
        {0xe55990879ddcaabd, 0xcc420a6a101d0515}, {0x8f57fa54c2a9eab6, 0x9fa946824a12232d},
        {0xb32df8e9f3546564, 0x47939822dc96abf9}, {0xdff9772470297ebd, 0x59787e2b93bc56f7},
        {0x8bfbea76c619ef36, 0x57eb4edb3c55b65a}, {0xaefae51477a06b03, 0xede622920b6b23f1},
        {0xdab99e59958885c4, 0xe95fab368e45eced}, {0x88b402f7fd75539b, 0x11dbcb0218ebb414},
        {0xaae103b5fcd2a881, 0xd652bdc29f26a119}, {0xd59944a37c0752a2, 0x4be76d3346f0495f},
        {0x857fcae62d8493a5, 0x6f70a4400c562ddb}, {0xa6dfbd9fb8e5b88e, 0xcb4ccd500f6bb952},
        {0xd097ad07a71f26b2, 0x7e2000a41346a7a7}, {0x825ecc24c873782f, 0x8ed400668c0c28c8},
        {0xa2f67f2dfa90563b, 0x728900802f0f32fa}, {0xcbb41ef979346bca, 0x4f2b40a03ad2ffb9},
        {0xfea126b7d78186bc, 0xe2f610c84987bfa8}, {0x9f24b832e6b0f436, 0x0dd9ca7d2df4d7c9},
        {0xc6ede63fa05d3143, 0x91503d1c79720dbb}, {0xf8a95fcf88747d94, 0x75a44c6397ce912a},
        {0x9b69dbe1b548ce7c, 0xc986afbe3ee11aba}, {0xc24452da229b021b, 0xfbe85badce996168},
        {0xf2d56790ab41c2a2, 0xfae27299423fb9c3}, {0x97c560ba6b0919a5, 0xdccd879fc967d41a},
        {0xbdb6b8e905cb600f, 0x5400e987bbc1c920}, {0xed246723473e3813, 0x290123e9aab23b68},
        {0x9436c0760c86e30b, 0xf9a0b6720aaf6521}, {0xb94470938fa89bce, 0xf808e40e8d5b3e69},
        {0xe7958cb87392c2c2, 0xb60b1d1230b20e04}, {0x90bd77f3483bb9b9, 0xb1c6f22b5e6f48c2},
        {0xb4ecd5f01a4aa828, 0x1e38aeb6360b1af3}, {0xe2280b6c20dd5232, 0x25c6da63c38de1b0},
        {0x8d590723948a535f, 0x579c487e5a38ad0e}, {0xb0af48ec79ace837, 0x2d835a9df0c6d851},
        {0xdcdb1b2798182244, 0xf8e431456cf88e65}, {0x8a08f0f8bf0f156b, 0x1b8e9ecb641b58ff},
        {0xac8b2d36eed2dac5, 0xe272467e3d222f3f}, {0xd7adf884aa879177, 0x5b0ed81dcc6abb0f},
        {0x86ccbb52ea94baea, 0x98e947129fc2b4e9}, {0xa87fea27a539e9a5, 0x3f2398d747b36224},
        {0xd29fe4b18e88640e, 0x8eec7f0d19a03aad}, {0x83a3eeeef9153e89, 0x1953cf68300424ac},
        {0xa48ceaaab75a8e2b, 0x5fa8c3423c052dd7}, {0xcdb02555653131b6, 0x3792f412cb06794d},
        {0x808e17555f3ebf11, 0xe2bbd88bbee40bd0}, {0xa0b19d2ab70e6ed6, 0x5b6aceaeae9d0ec4},
        {0xc8de047564d20a8b, 0xf245825a5a445275}, {0xfb158592be068d2e, 0xeed6e2f0f0d56712},
        {0x9ced737bb6c4183d, 0x55464dd69685606b}, {0xc428d05aa4751e4c, 0xaa97e14c3c26b886},
        {0xf53304714d9265df, 0xd53dd99f4b3066a8}, {0x993fe2c6d07b7fab, 0xe546a8038efe4029},
        {0xbf8fdb78849a5f96, 0xde98520472bdd033}, {0xef73d256a5c0f77c, 0x963e66858f6d4440},
        {0x95a8637627989aad, 0xdde7001379a44aa8}, {0xbb127c53b17ec159, 0x5560c018580d5d52},
        {0xe9d71b689dde71af, 0xaab8f01e6e10b4a6}, {0x9226712162ab070d, 0xcab3961304ca70e8},
        {0xb6b00d69bb55c8d1, 0x3d607b97c5fd0d22}, {0xe45c10c42a2b3b05, 0x8cb89a7db77c506a},
        {0x8eb98a7a9a5b04e3, 0x77f3608e92adb242}, {0xb267ed1940f1c61c, 0x55f038b237591ed3},
        {0xdf01e85f912e37a3, 0x6b6c46dec52f6688}, {0x8b61313bbabce2c6, 0x2323ac4b3b3da015},
        {0xae397d8aa96c1b77, 0xabec975e0a0d081a}, {0xd9c7dced53c72255, 0x96e7bd358c904a21},
        {0x881cea14545c7575, 0x7e50d64177da2e54}, {0xaa242499697392d2, 0xdde50bd1d5d0b9e9},
        {0xd4ad2dbfc3d07787, 0x955e4ec64b44e864}, {0x84ec3c97da624ab4, 0xbd5af13bef0b113e},
        {0xa6274bbdd0fadd61, 0xecb1ad8aeacdd58e}, {0xcfb11ead453994ba, 0x67de18eda5814af2},
        {0x81ceb32c4b43fcf4, 0x80eacf948770ced7}, {0xa2425ff75e14fc31, 0xa1258379a94d028d},
        {0xcad2f7f5359a3b3e, 0x096ee45813a04330}, {0xfd87b5f28300ca0d, 0x8bca9d6e188853fc},
        {0x9e74d1b791e07e48, 0x775ea264cf55347d}, {0xc612062576589dda, 0x95364afe032a819d},
        {0xf79687aed3eec551, 0x3a83ddbd83f52204}, {0x9abe14cd44753b52, 0xc4926a9672793542},
        {0xc16d9a0095928a27, 0x75b7053c0f178293}, {0xf1c90080baf72cb1, 0x5324c68b12dd6338},
        {0x971da05074da7bee, 0xd3f6fc16ebca5e03}, {0xbce5086492111aea, 0x88f4bb1ca6bcf584},
        {0xec1e4a7db69561a5, 0x2b31e9e3d06c32e5}, {0x9392ee8e921d5d07, 0x3aff322e62439fcf},
        {0xb877aa3236a4b449, 0x09befeb9fad487c2}, {0xe69594bec44de15b, 0x4c2ebe687989a9b3},
        {0x901d7cf73ab0acd9, 0x0f9d37014bf60a10}, {0xb424dc35095cd80f, 0x538484c19ef38c94},
        {0xe12e13424bb40e13, 0x2865a5f206b06fb9}, {0x8cbccc096f5088cb, 0xf93f87b7442e45d3},
        {0xafebff0bcb24aafe, 0xf78f69a51539d748}, {0xdbe6fecebdedd5be, 0xb573440e5a884d1b},
        {0x89705f4136b4a597, 0x31680a88f8953030}, {0xabcc77118461cefc, 0xfdc20d2b36ba7c3d},
        {0xd6bf94d5e57a42bc, 0x3d32907604691b4c}, {0x8637bd05af6c69b5, 0xa63f9a49c2c1b10f},
        {0xa7c5ac471b478423, 0x0fcf80dc33721d53}, {0xd1b71758e219652b, 0xd3c36113404ea4a8},
        {0x83126e978d4fdf3b, 0x645a1cac083126e9}, {0xa3d70a3d70a3d70a, 0x3d70a3d70a3d70a3},
        {0xcccccccccccccccc, 0xcccccccccccccccc}, {0x8000000000000000, 0x0000000000000000},
        {0xa000000000000000, 0x0000000000000000}, {0xc800000000000000, 0x0000000000000000},
        {0xfa00000000000000, 0x0000000000000000}, {0x9c40000000000000, 0x0000000000000000},
        {0xc350000000000000, 0x0000000000000000}, {0xf424000000000000, 0x0000000000000000},
        {0x9896800000000000, 0x0000000000000000}, {0xbebc200000000000, 0x0000000000000000},
        {0xee6b280000000000, 0x0000000000000000}, {0x9502f90000000000, 0x0000000000000000},
        {0xba43b74000000000, 0x0000000000000000}, {0xe8d4a51000000000, 0x0000000000000000},
        {0x9184e72a00000000, 0x0000000000000000}, {0xb5e620f480000000, 0x0000000000000000},
        {0xe35fa931a0000000, 0x0000000000000000}, {0x8e1bc9bf04000000, 0x0000000000000000},
        {0xb1a2bc2ec5000000, 0x0000000000000000}, {0xde0b6b3a76400000, 0x0000000000000000},
        {0x8ac7230489e80000, 0x0000000000000000}, {0xad78ebc5ac620000, 0x0000000000000000},
        {0xd8d726b7177a8000, 0x0000000000000000}, {0x878678326eac9000, 0x0000000000000000},
        {0xa968163f0a57b400, 0x0000000000000000}, {0xd3c21bcecceda100, 0x0000000000000000},
        {0x84595161401484a0, 0x0000000000000000}, {0xa56fa5b99019a5c8, 0x0000000000000000},
        {0xcecb8f27f4200f3a, 0x0000000000000000}, {0x813f3978f8940984, 0x4000000000000000},
        {0xa18f07d736b90be5, 0x5000000000000000}, {0xc9f2c9cd04674ede, 0xa400000000000000},
        {0xfc6f7c4045812296, 0x4d00000000000000}, {0x9dc5ada82b70b59d, 0xf020000000000000},
        {0xc5371912364ce305, 0x6c28000000000000}, {0xf684df56c3e01bc6, 0xc732000000000000},
        {0x9a130b963a6c115c, 0x3c7f400000000000}, {0xc097ce7bc90715b3, 0x4b9f100000000000},
        {0xf0bdc21abb48db20, 0x1e86d40000000000}, {0x96769950b50d88f4, 0x1314448000000000},
        {0xbc143fa4e250eb31, 0x17d955a000000000}, {0xeb194f8e1ae525fd, 0x5dcfab0800000000},
        {0x92efd1b8d0cf37be, 0x5aa1cae500000000}, {0xb7abc627050305ad, 0xf14a3d9e40000000},
        {0xe596b7b0c643c719, 0x6d9ccd05d0000000}, {0x8f7e32ce7bea5c6f, 0xe4820023a2000000},
        {0xb35dbf821ae4f38b, 0xdda2802c8a800000}, {0xe0352f62a19e306e, 0xd50b2037ad200000},
        {0x8c213d9da502de45, 0x4526f422cc340000}, {0xaf298d050e4395d6, 0x9670b12b7f410000},
        {0xdaf3f04651d47b4c, 0x3c0cdd765f114000}, {0x88d8762bf324cd0f, 0xa5880a69fb6ac800},
        {0xab0e93b6efee0053, 0x8eea0d047a457a00}, {0xd5d238a4abe98068, 0x72a4904598d6d880},
        {0x85a36366eb71f041, 0x47a6da2b7f864750}, {0xa70c3c40a64e6c51, 0x999090b65f67d924},
        {0xd0cf4b50cfe20765, 0xfff4b4e3f741cf6d}, {0x82818f1281ed449f, 0xbff8f10e7a8921a4},
        {0xa321f2d7226895c7, 0xaff72d52192b6a0d}, {0xcbea6f8ceb02bb39, 0x9bf4f8a69f764490},
        {0xfee50b7025c36a08, 0x02f236d04753d5b4}, {0x9f4f2726179a2245, 0x01d762422c946590},
        {0xc722f0ef9d80aad6, 0x424d3ad2b7b97ef5}, {0xf8ebad2b84e0d58b, 0xd2e0898765a7deb2},
        {0x9b934c3b330c8577, 0x63cc55f49f88eb2f}, {0xc2781f49ffcfa6d5, 0x3cbf6b71c76b25fb},
        {0xf316271c7fc3908a, 0x8bef464e3945ef7a}, {0x97edd871cfda3a56, 0x97758bf0e3cbb5ac},
        {0xbde94e8e43d0c8ec, 0x3d52eeed1cbea317}, {0xed63a231d4c4fb27, 0x4ca7aaa863ee4bdd},
        {0x945e455f24fb1cf8, 0x8fe8caa93e74ef6a}, {0xb975d6b6ee39e436, 0xb3e2fd538e122b44},
        {0xe7d34c64a9c85d44, 0x60dbbca87196b616}, {0x90e40fbeea1d3a4a, 0xbc8955e946fe31cd},
        {0xb51d13aea4a488dd, 0x6babab6398bdbe41}, {0xe264589a4dcdab14, 0xc696963c7eed2dd1},
        {0x8d7eb76070a08aec, 0xfc1e1de5cf543ca2}, {0xb0de65388cc8ada8, 0x3b25a55f43294bcb},
        {0xdd15fe86affad912, 0x49ef0eb713f39ebe}, {0x8a2dbf142dfcc7ab, 0x6e3569326c784337},
        {0xacb92ed9397bf996, 0x49c2c37f07965404}, {0xd7e77a8f87daf7fb, 0xdc33745ec97be906},
        {0x86f0ac99b4e8dafd, 0x69a028bb3ded71a3}, {0xa8acd7c0222311bc, 0xc40832ea0d68ce0c},
        {0xd2d80db02aabd62b, 0xf50a3fa490c30190}, {0x83c7088e1aab65db, 0x792667c6da79e0fa},
        {0xa4b8cab1a1563f52, 0x577001b891185938}, {0xcde6fd5e09abcf26, 0xed4c0226b55e6f86},
        {0x80b05e5ac60b6178, 0x544f8158315b05b4}, {0xa0dc75f1778e39d6, 0x696361ae3db1c721},
        {0xc913936dd571c84c, 0x03bc3a19cd1e38e9}, {0xfb5878494ace3a5f, 0x04ab48a04065c723},
        {0x9d174b2dcec0e47b, 0x62eb0d64283f9c76}, {0xc45d1df942711d9a, 0x3ba5d0bd324f8394},
        {0xf5746577930d6500, 0xca8f44ec7ee36479}, {0x9968bf6abbe85f20, 0x7e998b13cf4e1ecb},
        {0xbfc2ef456ae276e8, 0x9e3fedd8c321a67e}, {0xefb3ab16c59b14a2, 0xc5cfe94ef3ea101e},
        {0x95d04aee3b80ece5, 0xbba1f1d158724a12}, {0xbb445da9ca61281f, 0x2a8a6e45ae8edc97},
        {0xea1575143cf97226, 0xf52d09d71a3293bd}, {0x924d692ca61be758, 0x593c2626705f9c56},
        {0xb6e0c377cfa2e12e, 0x6f8b2fb00c77836c}, {0xe498f455c38b997a, 0x0b6dfb9c0f956447},
        {0x8edf98b59a373fec, 0x4724bd4189bd5eac}, {0xb2977ee300c50fe7, 0x58edec91ec2cb657},
        {0xdf3d5e9bc0f653e1, 0x2f2967b66737e3ed}, {0x8b865b215899f46c, 0xbd79e0d20082ee74},
        {0xae67f1e9aec07187, 0xecd8590680a3aa11}, {0xda01ee641a708de9, 0xe80e6f4820cc9495},
        {0x884134fe908658b2, 0x3109058d147fdcdd}, {0xaa51823e34a7eede, 0xbd4b46f0599fd415},
        {0xd4e5e2cdc1d1ea96, 0x6c9e18ac7007c91a}, {0x850fadc09923329e, 0x03e2cf6bc604ddb0},
        {0xa6539930bf6bff45, 0x84db8346b786151c}, {0xcfe87f7cef46ff16, 0xe612641865679a63},
        {0x81f14fae158c5f6e, 0x4fcb7e8f3f60c07e}, {0xa26da3999aef7749, 0xe3be5e330f38f09d},
        {0xcb090c8001ab551c, 0x5cadf5bfd3072cc5}, {0xfdcb4fa002162a63, 0x73d9732fc7c8f7f6},
        {0x9e9f11c4014dda7e, 0x2867e7fddcdd9afa}, {0xc646d63501a1511d, 0xb281e1fd541501b8},
        {0xf7d88bc24209a565, 0x1f225a7ca91a4226}, {0x9ae757596946075f, 0x3375788de9b06958},
        {0xc1a12d2fc3978937, 0x0052d6b1641c83ae}, {0xf209787bb47d6b84, 0xc0678c5dbd23a49a},
        {0x9745eb4d50ce6332, 0xf840b7ba963646e0}, {0xbd176620a501fbff, 0xb650e5a93bc3d898},
        {0xec5d3fa8ce427aff, 0xa3e51f138ab4cebe}, {0x93ba47c980e98cdf, 0xc66f336c36b10137},
        {0xb8a8d9bbe123f017, 0xb80b0047445d4184}, {0xe6d3102ad96cec1d, 0xa60dc059157491e5},
        {0x9043ea1ac7e41392, 0x87c89837ad68db2f}, {0xb454e4a179dd1877, 0x29babe4598c311fb},
        {0xe16a1dc9d8545e94, 0xf4296dd6fef3d67a}, {0x8ce2529e2734bb1d, 0x1899e4a65f58660c},
        {0xb01ae745b101e9e4, 0x5ec05dcff72e7f8f}, {0xdc21a1171d42645d, 0x76707543f4fa1f73},
        {0x899504ae72497eba, 0x6a06494a791c53a8}, {0xabfa45da0edbde69, 0x0487db9d17636892},
        {0xd6f8d7509292d603, 0x45a9d2845d3c42b6}, {0x865b86925b9bc5c2, 0x0b8a2392ba45a9b2},
        {0xa7f26836f282b732, 0x8e6cac7768d7141e}, {0xd1ef0244af2364ff, 0x3207d795430cd926},
        {0x8335616aed761f1f, 0x7f44e6bd49e807b8}, {0xa402b9c5a8d3a6e7, 0x5f16206c9c6209a6},
        {0xcd036837130890a1, 0x36dba887c37a8c0f}, {0x802221226be55a64, 0xc2494954da2c9789},
        {0xa02aa96b06deb0fd, 0xf2db9baa10b7bd6c}, {0xc83553c5c8965d3d, 0x6f92829494e5acc7},
        {0xfa42a8b73abbf48c, 0xcb772339ba1f17f9}, {0x9c69a97284b578d7, 0xff2a760414536efb},
        {0xc38413cf25e2d70d, 0xfef5138519684aba}, {0xf46518c2ef5b8cd1, 0x7eb258665fc25d69},
        {0x98bf2f79d5993802, 0xef2f773ffbd97a61}, {0xbeeefb584aff8603, 0xaafb550ffacfd8fa},
        {0xeeaaba2e5dbf6784, 0x95ba2a53f983cf38}, {0x952ab45cfa97a0b2, 0xdd945a747bf26183},
        {0xba756174393d88df, 0x94f971119aeef9e4}, {0xe912b9d1478ceb17, 0x7a37cd5601aab85d},
        {0x91abb422ccb812ee, 0xac62e055c10ab33a}, {0xb616a12b7fe617aa, 0x577b986b314d6009},
        {0xe39c49765fdf9d94, 0xed5a7e85fda0b80b}, {0x8e41ade9fbebc27d, 0x14588f13be847307},
        {0xb1d219647ae6b31c, 0x596eb2d8ae258fc8}, {0xde469fbd99a05fe3, 0x6fca5f8ed9aef3bb},
        {0x8aec23d680043bee, 0x25de7bb9480d5854}, {0xada72ccc20054ae9, 0xaf561aa79a10ae6a},
        {0xd910f7ff28069da4, 0x1b2ba1518094da04}, {0x87aa9aff79042286, 0x90fb44d2f05d0842},
        {0xa99541bf57452b28, 0x353a1607ac744a53}, {0xd3fa922f2d1675f2, 0x42889b8997915ce8},
        {0x847c9b5d7c2e09b7, 0x69956135febada11}, {0xa59bc234db398c25, 0x43fab9837e699095},
        {0xcf02b2c21207ef2e, 0x94f967e45e03f4bb}, {0x8161afb94b44f57d, 0x1d1be0eebac278f5},
        {0xa1ba1ba79e1632dc, 0x6462d92a69731732}, {0xca28a291859bbf93, 0x7d7b8f7503cfdcfe},
        {0xfcb2cb35e702af78, 0x5cda735244c3d43e}, {0x9defbf01b061adab, 0x3a0888136afa64a7},
        {0xc56baec21c7a1916, 0x088aaa1845b8fdd0}, {0xf6c69a72a3989f5b, 0x8aad549e57273d45},
        {0x9a3c2087a63f6399, 0x36ac54e2f678864b}, {0xc0cb28a98fcf3c7f, 0x84576a1bb416a7dd},
        {0xf0fdf2d3f3c30b9f, 0x656d44a2a11c51d5}, {0x969eb7c47859e743, 0x9f644ae5a4b1b325},
        {0xbc4665b596706114, 0x873d5d9f0dde1fee}, {0xeb57ff22fc0c7959, 0xa90cb506d155a7ea},
        {0x9316ff75dd87cbd8, 0x09a7f12442d588f2}, {0xb7dcbf5354e9bece, 0x0c11ed6d538aeb2f},
        {0xe5d3ef282a242e81, 0x8f1668c8a86da5fa}, {0x8fa475791a569d10, 0xf96e017d694487bc},
        {0xb38d92d760ec4455, 0x37c981dcc395a9ac}, {0xe070f78d3927556a, 0x85bbe253f47b1417},
        {0x8c469ab843b89562, 0x93956d7478ccec8e}, {0xaf58416654a6babb, 0x387ac8d1970027b2},
        {0xdb2e51bfe9d0696a, 0x06997b05fcc0319e}, {0x88fcf317f22241e2, 0x441fece3bdf81f03},
        {0xab3c2fddeeaad25a, 0xd527e81cad7626c3}, {0xd60b3bd56a5586f1, 0x8a71e223d8d3b074},
        {0x85c7056562757456, 0xf6872d5667844e49}, {0xa738c6bebb12d16c, 0xb428f8ac016561db},
        {0xd106f86e69d785c7, 0xe13336d701beba52}, {0x82a45b450226b39c, 0xecc0024661173473},
        {0xa34d721642b06084, 0x27f002d7f95d0190}, {0xcc20ce9bd35c78a5, 0x31ec038df7b441f4},
        {0xff290242c83396ce, 0x7e67047175a15271}, {0x9f79a169bd203e41, 0x0f0062c6e984d386},
        {0xc75809c42c684dd1, 0x52c07b78a3e60868}, {0xf92e0c3537826145, 0xa7709a56ccdf8a82},
        {0x9bbcc7a142b17ccb, 0x88a66076400bb691}, {0xc2abf989935ddbfe, 0x6acff893d00ea435},
        {0xf356f7ebf83552fe, 0x0583f6b8c4124d43}, {0x98165af37b2153de, 0xc3727a337a8b704a},
        {0xbe1bf1b059e9a8d6, 0x744f18c0592e4c5c}, {0xeda2ee1c7064130c, 0x1162def06f79df73},
        {0x9485d4d1c63e8be7, 0x8addcb5645ac2ba8}, {0xb9a74a0637ce2ee1, 0x6d953e2bd7173692},
        {0xe8111c87c5c1ba99, 0xc8fa8db6ccdd0437}, {0x910ab1d4db9914a0, 0x1d9c9892400a22a2},
        {0xb54d5e4a127f59c8, 0x2503beb6d00cab4b}, {0xe2a0b5dc971f303a, 0x2e44ae64840fd61d},
        {0x8da471a9de737e24, 0x5ceaecfed289e5d2}, {0xb10d8e1456105dad, 0x7425a83e872c5f47},
        {0xdd50f1996b947518, 0xd12f124e28f77719}, {0x8a5296ffe33cc92f, 0x82bd6b70d99aaa6f},
        {0xace73cbfdc0bfb7b, 0x636cc64d1001550b}, {0xd8210befd30efa5a, 0x3c47f7e05401aa4e},
        {0x8714a775e3e95c78, 0x65acfaec34810a71}, {0xa8d9d1535ce3b396, 0x7f1839a741a14d0d},
        {0xd31045a8341ca07c, 0x1ede48111209a050}, {0x83ea2b892091e44d, 0x934aed0aab460432},
        {0xa4e4b66b68b65d60, 0xf81da84d5617853f}, {0xce1de40642e3f4b9, 0x36251260ab9d668e},
        {0x80d2ae83e9ce78f3, 0xc1d72b7c6b426019}, {0xa1075a24e4421730, 0xb24cf65b8612f81f},
        {0xc94930ae1d529cfc, 0xdee033f26797b627}, {0xfb9b7cd9a4a7443c, 0x169840ef017da3b1},
        {0x9d412e0806e88aa5, 0x8e1f289560ee864e}, {0xc491798a08a2ad4e, 0xf1a6f2bab92a27e2},
        {0xf5b5d7ec8acb58a2, 0xae10af696774b1db}, {0x9991a6f3d6bf1765, 0xacca6da1e0a8ef29},
        {0xbff610b0cc6edd3f, 0x17fd090a58d32af3}, {0xeff394dcff8a948e, 0xddfc4b4cef07f5b0},
        {0x95f83d0a1fb69cd9, 0x4abdaf101564f98e}, {0xbb764c4ca7a4440f, 0x9d6d1ad41abe37f1},
        {0xea53df5fd18d5513, 0x84c86189216dc5ed}, {0x92746b9be2f8552c, 0x32fd3cf5b4e49bb4},
        {0xb7118682dbb66a77, 0x3fbc8c33221dc2a1}, {0xe4d5e82392a40515, 0x0fabaf3feaa5334a},
        {0x8f05b1163ba6832d, 0x29cb4d87f2a7400e}, {0xb2c71d5bca9023f8, 0x743e20e9ef511012},
        {0xdf78e4b2bd342cf6, 0x914da9246b255416}, {0x8bab8eefb6409c1a, 0x1ad089b6c2f7548e},
        {0xae9672aba3d0c320, 0xa184ac2473b529b1}, {0xda3c0f568cc4f3e8, 0xc9e5d72d90a2741e},
        {0x8865899617fb1871, 0x7e2fa67c7a658892}, {0xaa7eebfb9df9de8d, 0xddbb901b98feeab7},
        {0xd51ea6fa85785631, 0x552a74227f3ea565}, {0x8533285c936b35de, 0xd53a88958f87275f},
        {0xa67ff273b8460356, 0x8a892abaf368f137}, {0xd01fef10a657842c, 0x2d2b7569b0432d85},
        {0x8213f56a67f6b29b, 0x9c3b29620e29fc73}, {0xa298f2c501f45f42, 0x8349f3ba91b47b8f},
        {0xcb3f2f7642717713, 0x241c70a936219a73}, {0xfe0efb53d30dd4d7, 0xed238cd383aa0110},
        {0x9ec95d1463e8a506, 0xf4363804324a40aa}, {0xc67bb4597ce2ce48, 0xb143c6053edcd0d5},
        {0xf81aa16fdc1b81da, 0xdd94b7868e94050a}, {0x9b10a4e5e9913128, 0xca7cf2b4191c8326},
        {0xc1d4ce1f63f57d72, 0xfd1c2f611f63a3f0}, {0xf24a01a73cf2dccf, 0xbc633b39673c8cec},
        {0x976e41088617ca01, 0xd5be0503e085d813}, {0xbd49d14aa79dbc82, 0x4b2d8644d8a74e18},
        {0xec9c459d51852ba2, 0xddf8e7d60ed1219e}, {0x93e1ab8252f33b45, 0xcabb90e5c942b503},
        {0xb8da1662e7b00a17, 0x3d6a751f3b936243}, {0xe7109bfba19c0c9d, 0x0cc512670a783ad4},
        {0x906a617d450187e2, 0x27fb2b80668b24c5}, {0xb484f9dc9641e9da, 0xb1f9f660802dedf6},
        {0xe1a63853bbd26451, 0x5e7873f8a0396973}, {0x8d07e33455637eb2, 0xdb0b487b6423e1e8},
        {0xb049dc016abc5e5f, 0x91ce1a9a3d2cda62}, {0xdc5c5301c56b75f7, 0x7641a140cc7810fb},
        {0x89b9b3e11b6329ba, 0xa9e904c87fcb0a9d}, {0xac2820d9623bf429, 0x546345fa9fbdcd44},
        {0xd732290fbacaf133, 0xa97c177947ad4095}, {0x867f59a9d4bed6c0, 0x49ed8eabcccc485d},
        {0xa81f301449ee8c70, 0x5c68f256bfff5a74}, {0xd226fc195c6a2f8c, 0x73832eec6fff3111},
        {0x83585d8fd9c25db7, 0xc831fd53c5ff7eab}, {0xa42e74f3d032f525, 0xba3e7ca8b77f5e55},
        {0xcd3a1230c43fb26f, 0x28ce1bd2e55f35eb}, {0x80444b5e7aa7cf85, 0x7980d163cf5b81b3},
        {0xa0555e361951c366, 0xd7e105bcc332621f}, {0xc86ab5c39fa63440, 0x8dd9472bf3fefaa7},
        {0xfa856334878fc150, 0xb14f98f6f0feb951}, {0x9c935e00d4b9d8d2, 0x6ed1bf9a569f33d3},
        {0xc3b8358109e84f07, 0x0a862f80ec4700c8}, {0xf4a642e14c6262c8, 0xcd27bb612758c0fa},
        {0x98e7e9cccfbd7dbd, 0x8038d51cb897789c}, {0xbf21e44003acdd2c, 0xe0470a63e6bd56c3},
        {0xeeea5d5004981478, 0x1858ccfce06cac74}, {0x95527a5202df0ccb, 0x0f37801e0c43ebc8},
        {0xbaa718e68396cffd, 0xd30560258f54e6ba}, {0xe950df20247c83fd, 0x47c6b82ef32a2069},
        {0x91d28b7416cdd27e, 0x4cdc331d57fa5441}, {0xb6472e511c81471d, 0xe0133fe4adf8e952},
        {0xe3d8f9e563a198e5, 0x58180fddd97723a6}, {0x8e679c2f5e44ff8f, 0x570f09eaa7ea7648},
        {0xb201833b35d63f73, 0x2cd2cc6551e513da}, {0xde81e40a034bcf4f, 0xf8077f7ea65e58d1},
        {0x8b112e86420f6191, 0xfb04afaf27faf782}, {0xadd57a27d29339f6, 0x79c5db9af1f9b563},
        {0xd94ad8b1c7380874, 0x18375281ae7822bc}, {0x87cec76f1c830548, 0x8f2293910d0b15b5},
        {0xa9c2794ae3a3c69a, 0xb2eb3875504ddb22}, {0xd433179d9c8cb841, 0x5fa60692a46151eb},
        {0x849feec281d7f328, 0xdbc7c41ba6bcd333}, {0xa5c7ea73224deff3, 0x12b9b522906c0800},
        {0xcf39e50feae16bef, 0xd768226b34870a00}, {0x81842f29f2cce375, 0xe6a1158300d46640},
        {0xa1e53af46f801c53, 0x60495ae3c1097fd0}, {0xca5e89b18b602368, 0x385bb19cb14bdfc4},
        {0xfcf62c1dee382c42, 0x46729e03dd9ed7b5}, {0x9e19db92b4e31ba9, 0x6c07a2c26a8346d1}
    }};

    static constexpr int pow10_exp_shift = 16;
    static constexpr std::int32_t pow10_exp_log2_10 = 217706;

    static constexpr std::int32_t pow10_table_get_exp(std::int32_t const exp10)
    {
        return (exp10 * pow10_exp_log2_10 - ((f64_bits - 1) << pow10_exp_shift)) >> pow10_exp_shift;
    }

    static constexpr uint128 pow10_table_get_sig(std::int32_t const exp10)
    {
        return pow10_sig_table[static_cast<std::size_t>(exp10 - pow10_sig_table_min_exp)];
    }

#if defined(__SIZEOF_INT128__)
    __extension__ using u128 = unsigned __int128;

    static constexpr uint128 u128_mul(std::uint64_t const a, std::uint64_t const b)
    {
        u128 const m = static_cast<u128>(a) * b;
        return {static_cast<std::uint64_t>(m >> std::numeric_limits<std::uint64_t>::digits), static_cast<std::uint64_t>(m)};
    }

    static constexpr uint128 u128_mul_add(std::uint64_t const a, std::uint64_t const b, std::uint64_t const c)
    {
        u128 const m = static_cast<u128>(a) * b + c;
        return {static_cast<std::uint64_t>(m >> std::numeric_limits<std::uint64_t>::digits), static_cast<std::uint64_t>(m)};
    }
#else
    static constexpr uint128 u128_mul(std::uint64_t const a, std::uint64_t const b)
    {
        constexpr int half = std::numeric_limits<std::uint32_t>::digits;
        std::uint64_t const a0 = static_cast<std::uint32_t>(a);
        std::uint64_t const a1 = a >> half;
        std::uint64_t const b0 = static_cast<std::uint32_t>(b);
        std::uint64_t const b1 = b >> half;
        std::uint64_t const p00 = a0 * b0;
        std::uint64_t const p01 = a0 * b1;
        std::uint64_t const p10 = a1 * b0;
        std::uint64_t const p11 = a1 * b1;
        std::uint64_t const m0 = p01 + (p00 >> half);
        std::uint64_t const m1 = p10 + static_cast<std::uint32_t>(m0);
        return {p11 + (m0 >> half) + (m1 >> half), (m1 << half) | static_cast<std::uint32_t>(p00)};
    }

    static constexpr uint128 u128_mul_add(std::uint64_t const a, std::uint64_t const b, std::uint64_t const c)
    {
        uint128 const m = u128_mul(a, b);
        std::uint64_t const t = m.lo + c;
        return {m.hi + (t < m.lo || t < c ? 1 : 0), t};
    }
#endif

    static constexpr std::size_t bigint_chunks = 64;

    struct bigint {
        std::uint32_t used;
        std::array<std::uint64_t, bigint_chunks> bits;

        constexpr void add_u64(std::uint64_t const val)
        {
            std::uint64_t const num = bits[0];
            std::uint64_t const add = num + val;
            bits[0] = add;
            if (add >= num || add >= val) [[likely]]
                return;
            for (std::uint32_t idx = 1; idx < used; ++idx) {
                if (bits[idx] != std::numeric_limits<std::uint64_t>::max()) [[likely]] {
                    bits[idx] += 1;
                    return;
                }
                bits[idx] = 0;
            }
            bits[used++] = 1;
        }

        constexpr void mul_u64(std::uint64_t const val)
        {
            std::uint32_t idx = 0;
            while (idx < used && bits[idx] == 0)
                ++idx;
            std::uint64_t carry = 0;
            for (; idx < used; ++idx) {
                uint128 const m = u128_mul_add(bits[idx], val, carry);
                bits[idx] = m.lo;
                carry = m.hi;
            }
            if (carry != 0)
                bits[used++] = carry;
        }

        constexpr void mul_pow2(std::uint32_t const exp)
        {
            constexpr std::uint32_t chunk_bits = std::numeric_limits<std::uint64_t>::digits;
            std::uint32_t const shft = exp % chunk_bits;
            std::uint32_t move = exp / chunk_bits;
            std::uint32_t idx = used;
            if (shft == 0) [[unlikely]] {
                for (; idx > 0; --idx)
                    bits[idx + move - 1] = bits[idx - 1];
                used += move;
            } else {
                bits[idx] = 0;
                for (; idx > 0; --idx)
                    bits[idx + move] = bits[idx] << shft | bits[idx - 1] >> (chunk_bits - shft);
                bits[move] = bits[0] << shft;
                used += move + (bits[used + move] > 0 ? 1 : 0);
            }
            while (move != 0)
                bits[--move] = 0;
        }

        constexpr void mul_pow10(std::int32_t const exp)
        {
            std::int32_t e = exp;
            for (; e >= u64_pow10_max_exact_exp; e -= u64_pow10_max_exact_exp)
                mul_u64(u64_pow10_table[u64_pow10_max_exact_exp]);
            if (e != 0)
                mul_u64(u64_pow10_table[static_cast<std::size_t>(e)]);
        }

        constexpr void set_u64(std::uint64_t const val)
        {
            used = 1;
            bits[0] = val;
        }

        constexpr std::int32_t set_buf(std::string_view const text, std::uint64_t const sig, std::int32_t const exp,
                                       std::size_t const sig_cut, std::size_t const sig_end, std::size_t const dot_pos)
        {
            if (sig_cut == std::string_view::npos) [[unlikely]] {
                set_u64(sig);
                return exp;
            }
            bool const has_dot = sig_cut < dot_pos && dot_pos < sig_end;
            std::size_t dig_len_total = u64_safe_dig + (sig_end - sig_cut) - (has_dot ? 1 : 0);
            std::size_t end = sig_end;
            bool dig_big_cut = false;
            if (dig_len_total > f64_max_dec_dig) {
                dig_big_cut = true;
                end -= dig_len_total - (f64_max_dec_dig + 1);
                end -= dot_pos + 1 == end ? 1 : 0;
                dig_len_total = f64_max_dec_dig + 1;
            }
            set_u64(sig - (padded_char(text, sig_cut) >= '5' ? 1 : 0));
            std::int32_t len = 0;
            std::uint64_t val = 0;
            for (std::size_t cur = sig_cut; cur < end; ++cur) {
                if (cur == dot_pos)
                    continue;
                val = val * decimal + static_cast<std::uint64_t>(padded_char(text, cur) - '0');
                ++len;
                if (cur + 1 == end && dig_big_cut)
                    val = val - val % decimal + 1;
                if (len == u64_safe_dig || cur + 1 == end) {
                    mul_pow10(len);
                    add_u64(val);
                    val = 0;
                    len = 0;
                }
            }
            return exp - (static_cast<std::int32_t>(dig_len_total) - u64_safe_dig);
        }
    };

    static constexpr std::strong_ordering bigint_cmp(bigint const &a, bigint const &b)
    {
        if (a.used != b.used)
            return a.used <=> b.used;
        return std::lexicographical_compare_three_way(
            std::ranges::begin(std::span(a.bits).first(a.used) | std::views::reverse),
            std::ranges::end(std::span(a.bits).first(a.used) | std::views::reverse),
            std::ranges::begin(std::span(b.bits).first(b.used) | std::views::reverse),
            std::ranges::end(std::span(b.bits).first(b.used) | std::views::reverse));
    }

    static constexpr diy_fp diy_fp_get_cached_pow10(std::int32_t const exp10)
    {
        uint128 const sig = pow10_table_get_sig(exp10);
        return {sig.hi + (sig.lo >> (f64_bits - 1)), pow10_table_get_exp(exp10)};
    }

    static constexpr diy_fp diy_fp_mul(diy_fp const fp, diy_fp const fp2)
    {
        uint128 const m = u128_mul(fp.sig, fp2.sig);
        return {m.hi + (m.lo >> (f64_bits - 1)), fp.exp + fp2.exp + f64_bits};
    }

    static constexpr std::uint64_t diy_fp_to_ieee_raw(diy_fp const fp)
    {
        if (fp.sig == 0) [[unlikely]]
            return 0;
        int const lz_bits = std::countl_zero(fp.sig);
        std::uint64_t const sig = fp.sig << lz_bits >> (f64_bits - f64_sig_full_bits);
        std::int32_t exp = fp.exp - lz_bits + f64_bits - f64_sig_full_bits + f64_sig_bits;
        if (exp >= f64_max_bin_exp) [[unlikely]]
            return f64_bits_inf;
        if (exp >= f64_min_bin_exp - 1) [[likely]] {
            exp += f64_exp_bias;
            return static_cast<std::uint64_t>(exp) << f64_sig_bits | (sig & f64_sig_mask);
        }
        if (exp >= f64_min_bin_exp - f64_sig_full_bits) [[likely]]
            return sig >> (f64_min_bin_exp - exp - 1);
        return 0;
    }

    CBOR_NEVER_INLINE static constexpr std::strong_ordering bigcomp(std::string_view const text, std::uint64_t const sig,
                                                                  std::int32_t const exp, std::size_t const sig_cut,
                                                                  std::size_t const sig_end, std::size_t const dot_pos,
                                                                  diy_fp const fp_upper)
    {
        bigint big_full{};
        bigint big_comp{};
        std::int32_t const exp10 = big_full.set_buf(text, sig, exp, sig_cut, sig_end, dot_pos);
        big_comp.set_u64(fp_upper.sig);
        if (exp10 >= 0)
            big_full.mul_pow10(exp10);
        else
            big_comp.mul_pow10(-exp10);
        if (fp_upper.exp > 0)
            big_comp.mul_pow2(static_cast<std::uint32_t>(fp_upper.exp));
        else
            big_full.mul_pow2(static_cast<std::uint32_t>(-fp_upper.exp));
        return bigint_cmp(big_full, big_comp);
    }

    template <class Handler>
    static constexpr std::expected<std::size_t, error> read_num(std::string_view const text, std::size_t const hdr,
                                                                Handler &handler)
    {
        enum class digi { intg_more, frac, frac_more, frac_end, exp_more, exp_finish, finish };
        auto const ch = [text](std::size_t const at) { return padded_char(text, at); };
        bool const sign = ch(hdr) == '-';
        std::size_t cur = hdr + (sign ? 1 : 0);
        auto const at_cur = [&cur] { return cur; };
        auto const return_i64 = [&handler, &at_cur, sign](std::uint64_t const v) {
            return (sign && v != 0 ? handler.number_integer(v - 1) : handler.number_unsigned(v)).transform(at_cur);
        };
        auto const return_f64 = [&handler, &at_cur, sign](double const v) {
            return handler.number_float(sign ? -v : v).transform(at_cur);
        };
        auto const return_f64_bin = [&return_f64](std::uint64_t const raw) { return return_f64(std::bit_cast<double>(raw)); };
        auto const digit_value = [&ch](std::size_t const at) { return static_cast<std::uint64_t>(ch(at) - '0'); };

        std::size_t sig_cut = std::string_view::npos;
        std::size_t sig_end = std::string_view::npos;
        std::size_t dot_pos = std::string_view::npos;
        std::uint64_t sig = 0;
        std::int32_t exp = 0;
        std::int64_t exp_sig = 0;
        std::int64_t exp_lit = 0;
        std::size_t i = 1;
        digi next = digi::finish;

        if (!char_is_nonzero(ch(cur))) {
            if (ch(cur) != '0') [[unlikely]]
                return std::unexpected(error::syntax_error);
            ++cur;
            if (!char_is_digit_or_fp(ch(cur))) [[likely]] {
                return return_i64(0);
            }
            if (ch(cur) == '.') {
                dot_pos = cur++;
                if (!char_is_digit(ch(cur))) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                while (ch(cur) == '0')
                    ++cur;
                if (char_is_digit(ch(cur))) {
                    sig = digit_value(cur);
                    --cur;
                    next = digi::frac;
                }
            }
            if (next != digi::frac) {
                if (char_is_digit(ch(cur))) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                if (char_is_exp(ch(cur))) {
                    cur += 1 + (char_is_sign(ch(cur + 1)) ? std::size_t{1} : 0);
                    if (!char_is_digit(ch(cur))) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    for (; char_is_digit(ch(cur)); ++cur) {
                        if (exp_lit >= exponent_beyond_double) [[unlikely]]
                            return std::unexpected(error::syntax_error);
                        exp_lit = static_cast<std::int64_t>(digit_value(cur)) + exp_lit * decimal;
                    }
                }
                return return_f64_bin(0);
            }
        } else {
            sig = digit_value(cur);
            for (; i < u64_safe_dig; ++i) {
                std::uint64_t const num = digit_value(cur + i);
                if (num >= decimal)
                    break;
                sig = num + sig * decimal;
            }
            if (i == u64_safe_dig) {
                cur += u64_safe_dig;
                if (!char_is_digit_or_fp(ch(cur))) {
                    return return_i64(sig);
                }
                next = digi::intg_more;
            } else {
                if (!char_is_fp(ch(cur + i))) [[likely]] {
                    cur += i;
                    return return_i64(sig);
                }
                dot_pos = cur + i;
                if (ch(cur + i) == '.') {
                    next = digi::frac;
                } else {
                    cur += i;
                    sig_end = cur;
                    next = digi::exp_more;
                }
            }
        }

        while (next != digi::finish) {
            switch (next) {
            case digi::frac:
                for (; i < u64_safe_dig; ++i) {
                    std::uint64_t const num = digit_value(cur + i + 1);
                    if (num >= decimal)
                        break;
                    sig = num + sig * decimal;
                }
                cur += i + 1;
                next = char_is_digit(ch(cur)) ? digi::frac_more : digi::frac_end;
                break;
            case digi::intg_more: {
                unsigned char const c = ch(cur);
                if (char_is_digit(c) && !char_is_digit_or_fp(ch(cur + 1))) {
                    std::uint64_t const num = digit_value(cur);
                    constexpr std::uint64_t u64_max = std::numeric_limits<std::uint64_t>::max();
                    if (sig < u64_max / decimal || (sig == u64_max / decimal && num <= u64_max % decimal)) {
                        sig = num + sig * decimal;
                        ++cur;
                        return return_i64(sig);
                    }
                }
                if (char_is_exp(c)) {
                    dot_pos = cur;
                    next = digi::exp_more;
                    break;
                }
                if (c == '.') {
                    dot_pos = cur++;
                    if (!char_is_digit(ch(cur))) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                }
                next = digi::frac_more;
                break;
            }
            case digi::frac_more: {
                sig_cut = cur;
                sig += ch(cur) >= '5' ? std::uint64_t{1} : 0;
                do
                    ++cur;
                while (char_is_digit(ch(cur)));
                if (dot_pos == std::string_view::npos) {
                    if (!char_is_fp(ch(cur))) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    dot_pos = cur;
                    if (ch(cur) == '.') {
                        ++cur;
                        if (!char_is_digit(ch(cur))) [[unlikely]]
                            return std::unexpected(error::syntax_error);
                        while (char_is_digit(ch(cur)))
                            ++cur;
                    }
                }
                exp_sig = static_cast<std::int64_t>(dot_pos) - static_cast<std::int64_t>(sig_cut) +
                          (dot_pos < sig_cut ? 1 : 0);
                std::size_t tmp = cur - 1;
                while ((ch(tmp) == '0' || ch(tmp) == '.') && tmp > hdr)
                    --tmp;
                if (tmp < sig_cut)
                    sig_cut = std::string_view::npos;
                else
                    sig_end = cur;
                next = char_is_exp(ch(cur)) ? digi::exp_more : digi::exp_finish;
                break;
            }
            case digi::frac_end:
                if (dot_pos + 1 == cur) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                sig_end = cur;
                exp_sig = -static_cast<std::int64_t>(cur - dot_pos - 1);
                if (char_is_exp(ch(cur))) {
                    next = digi::exp_more;
                    break;
                }
                if (exp_sig < f64_min_dec_exp - u64_safe_dig) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                exp = static_cast<std::int32_t>(exp_sig);
                next = digi::finish;
                break;
            case digi::exp_more: {
                ++cur;
                bool const exp_sign = ch(cur) == '-';
                cur += char_is_sign(ch(cur)) ? std::size_t{1} : 0;
                if (!char_is_digit(ch(cur))) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                while (ch(cur) == '0')
                    ++cur;
                for (; char_is_digit(ch(cur)); ++cur) {
                    if (exp_lit >= exponent_beyond_double) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    exp_lit = static_cast<std::int64_t>(digit_value(cur)) + exp_lit * decimal;
                }
                exp_sig += exp_sign ? -exp_lit : exp_lit;
                next = digi::exp_finish;
                break;
            }
            case digi::exp_finish:
                if (exp_sig < f64_min_dec_exp - u64_safe_dig || exp_sig > f64_max_dec_exp) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                exp = static_cast<std::int32_t>(exp_sig);
                next = digi::finish;
                break;
            case digi::finish:
                break;
            }
        }

        if constexpr (double_math_correct) {
            if (sig < (std::uint64_t{1} << f64_sig_full_bits) && exp >= -f64_pow10_max_exact_exp &&
                exp <= f64_pow10_max_exact_exp) {
                double dbl = static_cast<double>(sig);
                if (exp < 0)
                    dbl /= f64_pow10_table[static_cast<std::size_t>(-exp)];
                else
                    dbl *= f64_pow10_table[static_cast<std::size_t>(exp)];
                return return_f64(dbl);
            }
        }
        if (sig == 0) [[unlikely]] {
            return return_f64_bin(0);
        }

        if (sig_cut == std::string_view::npos && exp > -f64_max_dec_exp + 1 && exp < f64_max_dec_exp - (u64_safe_dig + 1))
            [[likely]] {
            constexpr int low_bits = f64_bits - (f64_sig_full_bits + 1) - 1;
            constexpr std::uint64_t low_mask = (std::uint64_t{1} << low_bits) - 1;
            constexpr std::uint64_t round_bit = std::uint64_t{1} << (f64_bits - (f64_sig_full_bits + 1));
            constexpr std::uint64_t top_bit = std::uint64_t{1} << (f64_bits - 1);
            uint128 const sig2 = pow10_table_get_sig(exp);
            int const lz = std::countl_zero(sig);
            std::uint64_t const sig1 = sig << lz;
            std::int32_t exp2 = pow10_table_get_exp(exp) - lz;
            uint128 const m = u128_mul(sig1, sig2.hi);
            std::uint64_t hi = m.hi;
            bool exact = false;
            std::uint64_t const bits = hi & low_mask;
            if (bits - 1 < low_mask - 1) {
                exact = true;
            } else {
                uint128 const m2 = u128_mul(sig1, sig2.lo);
                std::uint64_t const add = m.lo + m2.hi;
                if (add + 1 > 1) {
                    hi += add < m.lo || add < m2.hi ? 1 : 0;
                    exact = true;
                }
            }
            if (exact) {
                int const shift = hi < top_bit ? 1 : 0;
                hi <<= shift;
                exp2 += f64_bits - shift;
                hi += (hi & round_bit) != 0 ? round_bit : 0;
                if (hi < round_bit) {
                    hi = top_bit;
                    exp2 += 1;
                }
                hi >>= f64_bits - f64_sig_full_bits;
                exp2 += f64_bits - f64_sig_full_bits + f64_sig_bits + f64_exp_bias;
                return return_f64_bin(static_cast<std::uint64_t>(exp2) << f64_sig_bits | (hi & f64_sig_mask));
            }
        }

        constexpr std::int32_t err_ulp_log = 3;
        constexpr std::int32_t err_ulp = 1 << err_ulp_log;
        constexpr std::int32_t err_cached_pow = err_ulp / 2;
        constexpr std::int32_t err_mul_fixed = err_ulp / 2;
        constexpr std::int32_t diy_sig_bits = std::numeric_limits<std::uint64_t>::digits;
        constexpr std::int32_t exp_bias = f64_exp_bias + f64_sig_bits;
        constexpr std::int32_t exp_subnormal = -exp_bias + 1;

        diy_fp fp{sig, 0};
        std::uint64_t fp_err = sig_cut != std::string_view::npos ? std::uint64_t{err_ulp / 2} : 0;
        int bits = std::countl_zero(fp.sig);
        fp.sig <<= bits;
        fp.exp -= bits;
        fp_err <<= bits;
        fp = diy_fp_mul(fp, diy_fp_get_cached_pow10(exp));
        fp_err += std::uint64_t{err_cached_pow} + (fp_err != 0 ? 1 : 0) + std::uint64_t{err_mul_fixed};
        bits = std::countl_zero(fp.sig);
        fp.sig <<= bits;
        fp.exp -= bits;
        fp_err <<= bits;

        std::int32_t const order_of_magnitude = diy_sig_bits + fp.exp;
        std::int32_t effective_significand_size = order_of_magnitude - exp_subnormal;
        if (order_of_magnitude >= exp_subnormal + f64_sig_full_bits) [[likely]]
            effective_significand_size = f64_sig_full_bits;
        else if (order_of_magnitude <= exp_subnormal)
            effective_significand_size = 0;
        std::int32_t precision_digits_count = diy_sig_bits - effective_significand_size;
        if (precision_digits_count + err_ulp_log >= diy_sig_bits) [[unlikely]] {
            std::int32_t const shr = precision_digits_count + err_ulp_log - diy_sig_bits + 1;
            fp.sig >>= shr;
            fp.exp += shr;
            fp_err = (fp_err >> shr) + 1 + std::uint64_t{err_ulp};
            precision_digits_count -= shr;
        }
        std::uint64_t const precision_bits =
            (fp.sig & ((std::uint64_t{1} << precision_digits_count) - 1)) * std::uint64_t{err_ulp};
        std::uint64_t const half_way = (std::uint64_t{1} << (precision_digits_count - 1)) * std::uint64_t{err_ulp};
        fp.sig >>= precision_digits_count;
        fp.sig += precision_bits >= half_way + fp_err ? 1 : 0;
        fp.exp += precision_digits_count;

        std::uint64_t raw = diy_fp_to_ieee_raw(fp);
        if (raw == f64_bits_inf) [[unlikely]]
            return std::unexpected(error::syntax_error);
        if (precision_bits > half_way - fp_err && precision_bits < half_way + fp_err) [[unlikely]] {
            diy_fp fp_upper{(raw & f64_sig_mask) + ((raw & f64_exp_mask) != 0 ? std::uint64_t{1} << f64_sig_bits : 0),
                            (raw & f64_exp_mask) != 0 ? static_cast<std::int32_t>((raw & f64_exp_mask) >> f64_sig_bits) : 1};
            fp_upper.exp -= f64_exp_bias + f64_sig_bits;
            fp_upper.sig <<= 1;
            fp_upper.exp -= 1;
            fp_upper.sig += 1;
            std::strong_ordering const cmp = bigcomp(text, sig, exp, sig_cut, sig_end, dot_pos, fp_upper);
            if (cmp != 0) [[likely]]
                raw += cmp > 0 ? std::uint64_t{1} : 0;
            else
                raw += raw & 1;
            if (raw == f64_bits_inf) [[unlikely]]
                return std::unexpected(error::syntax_error);
        }
        if ((raw & f64_exp_mask) == 0) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return return_f64_bin(raw);
    }

    static constexpr std::expected<std::size_t, error> read_uni_esc(std::string_view const text, std::size_t const at,
                                                          std::string &out)
    {
        auto const hex_load_4 = [text](std::size_t const from) -> std::expected<std::uint32_t, error> {
            if (auto const left = detail::validity::check_text_left(text, from, hex4_digits); !left) [[unlikely]]
                return std::unexpected(left.error());
            std::uint32_t value = 0;
            std::uint8_t invalid = 0;
            for (char const c : std::span(text).subspan(from, hex4_digits)) {
                std::uint8_t const v = hex_conv_table[static_cast<unsigned char>(c)];
                invalid |= v;
                value = value << rfc4648::base16_group_bits | v;
            }
            if ((invalid & hex_conv_invalid) != 0) [[unlikely]]
                return std::unexpected(error::syntax_error);
            return value;
        };
        auto const hi = hex_load_4(at);
        if (!hi) [[unlikely]]
            return std::unexpected(hi.error());
        std::size_t const after = at + hex4_digits;
        if (*hi < rfc2781::high_surrogate_first || *hi > rfc2781::low_surrogate_last) [[likely]] {
            append_utf8(out, *hi);
            return after;
        }
        if (*hi >= rfc2781::low_surrogate_first) [[unlikely]]
            return std::unexpected(error::syntax_error);
        if (!detail::validity::check_text_left(text, after, escape_prefix.size()) ||
            !std::string_view(std::span(text).subspan(after)).starts_with(escape_prefix)) [[unlikely]]
            return std::unexpected(error::syntax_error);
        auto const lo = hex_load_4(after + escape_prefix.size());
        if (!lo || *lo < rfc2781::low_surrogate_first || *lo > rfc2781::low_surrogate_last) [[unlikely]]
            return std::unexpected(error::syntax_error);
        append_utf8(out, rfc2781::surrogate_pair_first + ((*hi - rfc2781::high_surrogate_first) << rfc2781::surrogate_bits) +
                             (*lo - rfc2781::low_surrogate_first));
        return after + escape_prefix.size() + hex4_digits;
    }

    template <class Handler>
    static constexpr std::expected<std::size_t, error> read_str(std::string_view const text, std::size_t const at,
                                                                Handler &handler, std::string &unescaped)
    {
        std::size_t const hdr = at + 1;
        std::size_t src = skip_ascii(text, hdr);
        while (padded_char(text, src) > rfc3629::one_octet_last)
            src = skip_ascii(text, skip_utf8(text, src));
        std::string_view content = detail::validity::checked_substr(text, hdr, src - hdr);
        if (padded_char(text, src) != '"') {
            unescaped.assign(content);
            for (;;) {
                unsigned char const c = padded_char(text, src);
                if (c == '"')
                    break;
                if (c != '\\') [[unlikely]]
                    return std::unexpected(error::syntax_error);
                unsigned char const e = padded_char(text, ++src);
                ++src;
                switch (e) {
                case '"':
                case '\\':
                case '/':
                    unescaped.push_back(static_cast<char>(e));
                    break;
                case 'b':
                    unescaped.push_back('\b');
                    break;
                case 'f':
                    unescaped.push_back('\f');
                    break;
                case 'n':
                    unescaped.push_back('\n');
                    break;
                case 'r':
                    unescaped.push_back('\r');
                    break;
                case 't':
                    unescaped.push_back('\t');
                    break;
                case 'u': {
                    auto const next = read_uni_esc(text, src, unescaped);
                    if (!next) [[unlikely]]
                        return next;
                    src = *next;
                    break;
                }
                [[unlikely]] default:
                    return std::unexpected(error::syntax_error);
                }
                std::size_t run = skip_ascii(text, src);
                while (padded_char(text, run) > rfc3629::one_octet_last)
                    run = skip_ascii(text, skip_utf8(text, run));
                unescaped.append(detail::validity::checked_substr(text, src, run - src));
                src = run;
            }
            content = unescaped;
        }
        std::size_t const end = src + 1;
        return handler.string(content, text.size() - end).transform([end] { return end; });
    }

    template <class Handler>
    static constexpr std::expected<std::size_t, error> read_true(std::string_view const text, std::size_t const at,
                                                                Handler &handler)
    {
        constexpr std::string_view literal = "true";
        if (!std::string_view{std::span(text).subspan(at)}.starts_with(literal)) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return handler.boolean(true).transform([at, literal] { return at + literal.size(); });
    }

    template <class Handler>
    static constexpr std::expected<std::size_t, error> read_false(std::string_view const text, std::size_t const at,
                                                                Handler &handler)
    {
        constexpr std::string_view literal = "false";
        if (!std::string_view{std::span(text).subspan(at)}.starts_with(literal)) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return handler.boolean(false).transform([at, literal] { return at + literal.size(); });
    }

    template <class Handler>
    static constexpr std::expected<std::size_t, error> read_null(std::string_view const text, std::size_t const at,
                                                                Handler &handler)
    {
        constexpr std::string_view literal = "null";
        if (!std::string_view{std::span(text).subspan(at)}.starts_with(literal)) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return handler.null().transform([at, literal] { return at + literal.size(); });
    }

    template <class Handler>
    static constexpr std::expected<std::size_t, error> read_root_minify(std::string_view const text, std::size_t cur,
                                                                        Handler &handler, std::string &unescaped)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        enum class label { arr_val_begin, arr_val_end, obj_key_begin, obj_key_end, obj_val_begin, obj_val_end, arr_end, obj_end };
        bool const obj = text[cur++] == '{';
        std::size_t const ctn = obj ? handler.start_object() : handler.start_array();
        std::size_t ctn_len = 0;
        label next = obj ? label::obj_key_begin : label::arr_val_begin;
        while (next != label::arr_end && next != label::obj_end) {
            unsigned char const c = padded_char(text, cur);
            std::expected<std::size_t, error> r = std::unexpected(error::syntax_error);
            switch (next) {
            case label::arr_val_begin:
            case label::obj_val_begin:
                if (char_is_ctn(c))
                    r = read_root_minify(text, cur, handler, unescaped);
                else if (char_is_num(c))
                    r = read_num(text, cur, handler);
                else if (c == '"')
                    r = read_str(text, cur, handler, unescaped);
                else if (c == 't')
                    r = read_true(text, cur, handler);
                else if (c == 'f')
                    r = read_false(text, cur, handler);
                else if (c == 'n')
                    r = read_null(text, cur, handler);
                else if (c == ']' && next == label::arr_val_begin && ctn_len == 0) {
                    ++cur;
                    next = label::arr_end;
                    continue;
                } else
                    break;
                if (!r) [[unlikely]]
                    return r;
                cur = *r;
                ++ctn_len;
                next = next == label::arr_val_begin ? label::arr_val_end : label::obj_val_end;
                continue;
            case label::arr_val_end:
            case label::obj_val_end:
                if (c == ',') {
                    ++cur;
                    next = obj ? label::obj_key_begin : label::arr_val_begin;
                    continue;
                }
                if (c == (obj ? '}' : ']')) {
                    ++cur;
                    next = obj ? label::obj_end : label::arr_end;
                    continue;
                }
                break;
            case label::obj_key_begin:
                if (c == '"') [[likely]] {
                    r = read_str(text, cur, handler, unescaped);
                    if (!r) [[unlikely]]
                        return r;
                    cur = *r;
                    ++ctn_len;
                    next = label::obj_key_end;
                    continue;
                }
                if (c == '}' && ctn_len == 0) {
                    ++cur;
                    next = label::obj_end;
                    continue;
                }
                break;
            case label::obj_key_end:
                if (c == ':') {
                    ++cur;
                    next = label::obj_val_begin;
                    continue;
                }
                break;
            case label::arr_end:
            case label::obj_end:
                break;
            }
            if (!char_is_trivia(c)) [[unlikely]]
                return std::unexpected(error::syntax_error);
            if (char_is_space(c)) {
                do
                    ++cur;
                while (char_is_space(padded_char(text, cur)));
                continue;
            }
            auto const end = optional_space_end(text, cur);
            if (!end) [[unlikely]]
                return end;
            cur = *end;
        }
        return (obj ? handler.end_object(ctn, ctn_len / rfc8949::data_items_per_pair) : handler.end_array(ctn, ctn_len))
            .transform([cur] { return cur; });
    }

    template <class Handler>
    static constexpr std::expected<std::size_t, error> read_root_single(std::string_view const text, std::size_t const cur,
                                                                        Handler &handler, std::string &unescaped)
    {
        unsigned char const c = padded_char(text, cur);
        if (char_is_num(c))
            return read_num(text, cur, handler);
        if (c == '"')
            return read_str(text, cur, handler, unescaped);
        if (c == 't')
            return read_true(text, cur, handler);
        if (c == 'f')
            return read_false(text, cur, handler);
        if (c == 'n')
            return read_null(text, cur, handler);
        return std::unexpected(error::syntax_error);
    }

    static std::expected<owning<item>, error> read_root_minify(std::string_view const text)
    {
        auto const start = optional_space_end(text, 0);
        if (!start) [[unlikely]]
            return std::unexpected(start.error());
        auto decoded = std::make_unique<zone>(detail::validity::checked_mul(text.size(), sizeof(item) + 1)
                                                  .transform([](std::size_t const size) {
                                                      return std::clamp(size, std::size_t{1},
                                                                        detail::validity::initial_size_limit);
                                                  })
                                                  .value_or(detail::validity::initial_size_limit));
        std::vector<item> values;
        std::string unescaped;
        sax_dom_parser handler{*decoded, values};
        auto const next = char_is_ctn(padded_char(text, *start))
                              ? read_root_minify(text, *start, handler, unescaped)
                              : read_root_single(text, *start, handler, unescaped);
        if (!next) [[unlikely]]
            return std::unexpected(next.error());
        auto const end = optional_space_end(text, *next);
        if (!end || *end != text.size()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        std::pmr::vector<item> &root = decoded->items_reserved(1);
        root.push_back(values.front());
        return zone_owner::tree_owned(&root.front(), std::move(decoded));
    }

    static constexpr std::expected<std::size_t, error> parse_literal(literal_cursor const cursor, std::string &out)
    {
        std::size_t const size_before = out.size();
        {
            backpatches patches;
            writer handler{out, patches};
            std::string unescaped;
            auto const next = char_is_ctn(padded_char(cursor.text, cursor.at))
                                  ? read_root_minify(cursor.text, cursor.at, handler, unescaped)
                                  : read_root_single(cursor.text, cursor.at, handler, unescaped);
            if (next && value_ends(cursor.text, *next)) [[likely]] {
                patches.apply(out);
                return next;
            }
        }
        out.resize(size_before);
        backpatches patches;
        auto const next = parse_literal(cursor, out, patches);
        if (!next) [[unlikely]]
            return next;
        patches.apply(out);
        return next;
    }

    static constexpr std::expected<detail::validity::offset, error>
    append_deterministically_encoded(std::string &out, std::string_view const encoded,
                                     detail::validity::offset const at)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto h = detail::heads::read_raw_head<false>(encoded, at);
        for (;;) {
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                break;
            if (auto const r = append_head(out, major_type::tag, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            h = detail::heads::read_raw_head<false>(encoded, h->at);
        }
        if (h->info == std::to_underlying(rfc8949::additional_information::indefinite_length)) [[unlikely]]
            detail::validity::throw_logic_error("cbor::path: a literal holds an indefinite length");
        detail::validity::offset next = h->at;
        switch (h->major) {
        case major_type::unsigned_integer:
        case major_type::negative_integer:
            if (auto const r = append_head(out, h->major, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        case major_type::byte_string:
        case major_type::text_string: {
            detail::heads::decoder<false> d{detail::validity::subspan(encoded, next)};
            auto const content = d.decode_string(h->argument);
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            next = detail::validity::next(next, content->size());
            if (auto const r = append_head(out, h->major, content->size(), no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            out += *content;
            return next;
        }
        case major_type::array:
        case major_type::map: {
            bool const map = h->major == major_type::map;
            if (auto const r = detail::validity::check_pending_items(
                    h->major, h->info, h->argument, detail::validity::subspan(encoded, next).size());
                !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = append_head(out, h->major, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            std::size_t const start = out.size();
            std::vector<encoded_pair> key_value_pairs;
            std::uint64_t count = 0;
            for (; count < h->argument; ++count) {
                std::size_t const key_at = out.size();
                auto const first = append_deterministically_encoded(out, encoded, next);
                if (!first) [[unlikely]]
                    return first;
                next = *first;
                if (map) {
                    std::size_t const value_at = out.size();
                    auto const second = append_deterministically_encoded(out, encoded, next);
                    if (!second) [[unlikely]]
                        return second;
                    next = *second;
                    key_value_pairs.push_back({key_at - start, value_at - start, out.size() - start});
                }
            }
            if (map) {
                std::string const region{std::string_view{std::span(out).subspan(start)}};
                auto const key = [&region](encoded_pair const &p) {
                    return std::string_view(std::span(region).subspan(p.key, p.value - p.key));
                };
                std::ranges::sort(key_value_pairs, {}, key);
                if (error const c =
                        detail::validity::check_key_unique(std::ranges::adjacent_find(key_value_pairs, {}, key) != key_value_pairs.end())
                            .error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                out.resize(start);
                for (encoded_pair const &p : key_value_pairs)
                    out += std::string_view(std::span(region).subspan(p.key, p.end - p.key));
            }
            return next;
        }
        default:
            break;
        }
        constexpr std::uint8_t half_precision_float =
            std::to_underlying(rfc8949::simple_information::half_precision_float);
        constexpr std::uint8_t double_precision_float =
            std::to_underlying(rfc8949::simple_information::double_precision_float);
        if (h->info < half_precision_float) {
            if (error const r = detail::validity::check_simple_value(h->info, h->argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            if (auto const r = append_head(out, major_type::simple, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }
        if (h->info > double_precision_float) [[unlikely]]
            return std::unexpected(error::syntax_error);
        detail::ieee754::float_key const key = detail::ieee754::float_key_of(h->info, h->argument);
        if (key.nan) {
            if (auto const r = append_head(out, major_type::simple,
                                           key.widened | std::uint64_t{detail::ieee754::double_precision.exponent_max}
                                                             << detail::ieee754::double_precision.significand_bits,
                                           double_precision_float);
                !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }
        if (auto const r = append_float(out, key.value == 0 ? 0.0 : key.value, no_indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        return next;
    }

    static constexpr std::string encoding_indicator(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info < std::to_underlying(rfc8949::additional_information::one_byte_argument))
            return {};
        if (info == detail::heads::preferred_argument_info(argument))
            return {};
        return {'_',
                static_cast<char>('0' + info -
                                  std::to_underlying(rfc8949::additional_information::one_byte_argument))};
    }

    static constexpr void encode_base16(std::string &out, std::uint8_t const byte)
    {
        out.push_back(rfc4648::base16_lowercase_alphabet[byte >> rfc4648::base16_group_bits]);
        out.push_back(rfc4648::base16_lowercase_alphabet[byte & ((1 << rfc4648::base16_group_bits) - 1)]);
    }

    static constexpr std::string hex_of(std::string_view const bytes)
    {
        std::string out;
        out.reserve(std::numeric_limits<std::uint8_t>::digits / rfc4648::base16_group_bits * bytes.size());
        for (char const c : bytes)
            encode_base16(out, static_cast<std::uint8_t>(c));
        return out;
    }

    static constexpr std::string quoted_of(std::string_view const text)
    {
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
                    encode_base16(out, static_cast<std::uint8_t>(c));
                } else {
                    out.push_back(c);
                }
                break;
            }
        }
        out.push_back('"');
        return out;
    }

    static constexpr std::string number_of(double const value)
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
        auto const e = std::ranges::find(out, 'e');
        std::string mantissa(out.begin(), e);
        if (mantissa.find('.') == std::string::npos)
            mantissa += ".0";
        if (e == out.end())
            return mantissa;
        std::string_view exponent(std::next(e), out.end());
        char const sign = exponent.front();
        exponent.remove_prefix(1);
        exponent.remove_prefix(std::min(exponent.find_first_not_of('0'), exponent.size() - 1));
        return mantissa + "e" + sign + std::string(exponent);
    }

    static constexpr std::string float_of(std::uint8_t const info, std::uint64_t const argument)
    {
        std::size_t const width =
            info - std::to_underlying(rfc8949::simple_information::half_precision_float);
        double const value = detail::ieee754::decode_float(info, argument);
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
            std::to_underlying(detail::ieee754::preferred_float_info(value)) -
            std::to_underlying(rfc8949::simple_information::half_precision_float);
        return number_of(value) + (width == preferred ? std::string{} : indicator);
    }

    static std::expected<void, error> write_item(std::string &out, item const &node)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return r;
        std::uint8_t const info = node.additional_information;
        switch (node.m_type) {
        case cbor::type::unsigned_integer:
            out += std::to_string(node.via.u64) + encoding_indicator(info, node.via.u64);
            break;
        case cbor::type::negative_integer:
            out += node.via.u64 == std::numeric_limits<std::uint64_t>::max() ? std::string("-18446744073709551616")
                                                                              : "-" + std::to_string(node.via.u64 + 1);
            out += encoding_indicator(info, node.via.u64);
            break;
        case cbor::type::byte_string:
            out += "h'";
            for (std::byte const b : node.via.bin)
                encode_base16(out, std::to_integer<std::uint8_t>(b));
            out += "'" + encoding_indicator(info, node.via.bin.size());
            break;
        case cbor::type::text_string:
            out += quoted_of(node.via.str) + encoding_indicator(info, node.via.str.size());
            break;
        case cbor::type::array:
        case cbor::type::map: {
            bool const map = node.m_type == cbor::type::map;
            std::span<item const> const children = map ? node.via.map : node.via.array;
            std::size_t const count = map ? children.size() / rfc8949::data_items_per_pair : children.size();
            out += map ? "{" : "[";
            if (std::string const indicator = encoding_indicator(info, count); !indicator.empty())
                out += indicator + " ";
            for (std::size_t i = 0; i != children.size(); ++i) {
                if (i != 0)
                    out += map && i % rfc8949::data_items_per_pair != 0 ? ": " : ", ";
                if (auto const r = write_item(out, children[i]); !r) [[unlikely]]
                    return r;
            }
            out += map ? "}" : "]";
            break;
        }
        case cbor::type::tag:
        case cbor::type::typed_array:
        case cbor::type::unsigned_bignum:
        case cbor::type::negative_bignum:
            out += std::to_string(node.via.tag.number) + encoding_indicator(info, node.via.tag.number) + "(";
            if (auto const r = write_item(out, *node.via.tag.content); !r) [[unlikely]]
                return r;
            out += ")";
            break;
        case cbor::type::shared_reference:
            detail::validity::throw_logic_error_if(true, "cbor::to_edn: the walk met a shared_reference");
            break;
        case cbor::type::false_value:
            out += "false";
            break;
        case cbor::type::true_value:
            out += "true";
            break;
        case cbor::type::null:
            out += "null";
            break;
        case cbor::type::undefined:
            out += "undefined";
            break;
        case cbor::type::simple_value:
            out += "simple(" + std::to_string(std::to_underlying(node.via.simple_value)) + ")";
            break;
        case cbor::type::half_float:
        case cbor::type::single_float:
        case cbor::type::double_float: {
            auto const width = static_cast<rfc8949::simple_information>(
                std::to_underlying(rfc8949::simple_information::half_precision_float) +
                (std::to_underlying(node.m_type) - std::to_underlying(cbor::type::half_float)));
            out += float_of(std::to_underlying(width), detail::ieee754::encode_float(width, node.via.f64));
            break;
        }
        }
        return {};
    }

    static constexpr bool break_consumed(detail::heads::decoder<true> &d)
    {
        if (!detail::validity::bytes_fit(detail::heads::initial_byte_size, d.encoded.size()) ||
            d.encoded.front() !=
                detail::heads::initial_byte(major_type::simple,
                                    std::to_underlying(rfc8949::simple_information::break_stop_code)))
            return false;
        d.encoded.remove_prefix(1);
        return true;
    }

    static constexpr std::expected<void, error>
    write_diagnostic(std::string &out, detail::heads::decoder<true> &d)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const h = detail::heads::read_raw_head<true>(d.encoded, detail::validity::offset{});
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        d.encoded = detail::validity::subspan(d.encoded, h->at);
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
                if (auto const r = detail::validity::check_bytes_left(detail::heads::initial_byte_size, d.encoded.size());
                    !r) [[unlikely]]
                    return std::unexpected(r.error());
                auto const initial = static_cast<std::uint8_t>(d.encoded.front());
                if (error const c =
                        detail::validity::check_chunk(
                            h->major,
                            static_cast<major_type>(initial >> rfc8949::additional_information_bits),
                            detail::heads::additional_information_of(initial))
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
        case std::to_underlying(rfc8949::simple_information::simple_value_follows):
            if (error const r = detail::validity::check_simple_value(h->info, h->argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            out += "simple(" + std::to_string(h->argument) + ")";
            return {};
        case std::to_underlying(rfc8949::simple_information::half_precision_float):
        case std::to_underlying(rfc8949::simple_information::single_precision_float):
        case std::to_underlying(rfc8949::simple_information::double_precision_float):
            out += float_of(h->info, h->argument);
            return {};
        [[unlikely]] case std::to_underlying(rfc8949::additional_information::indefinite_length):
            return std::unexpected(error::syntax_error);
        default:
            out += "simple(" + std::to_string(h->argument) + ")";
            return {};
        }
    }

    friend class cbor::path;

    friend std::expected<std::string, error> cbor::diagnostic_notation(std::string_view encoded);

    friend constexpr std::expected<std::string, error> cbor::diag2cbor(std::string_view text);

    friend std::expected<std::string, error> item::to_edn() const;

    friend std::expected<owning<item>, error> cbor::parse_edn(std::string_view text);

    friend std::expected<std::string, error> cbor::to_edn(item const &root);

    friend std::expected<std::string, error> cbor::to_edn(owning<item> const &tree);

    friend class detail::edn_text;
};

class edn_text
{
    struct open {
        std::uint64_t items;
        std::uint64_t done;
        major_type major;
    };

    using notation = extended_diagnostic_notation;

    std::string text;
    std::vector<open> opened;
    major_type string_major = major_type::text_string;
    std::uint64_t string_left = 0;
    std::uint8_t string_info = 0;

    void begin_item()
    {
        if (opened.empty() || opened.back().major == major_type::tag || opened.back().done == 0)
            return;
        bool const value = opened.back().major == major_type::map && opened.back().done % rfc8949::data_items_per_pair != 0;
        text += value ? ": " : ", ";
    }

    void end_item()
    {
        while (!opened.empty()) {
            open &o = opened.back();
            if (++o.done != o.items)
                return;
            text += o.major == major_type::array ? "]" : o.major == major_type::map ? "}" : ")";
            opened.pop_back();
        }
    }

    void end_string()
    {
        text += string_major == major_type::text_string ? "\"" : "'";
        text += notation::encoding_indicator(string_info, 0);
        end_item();
    }

public:
    void write_head(major_type const major, std::uint8_t const info, std::uint64_t const argument)
    {
        begin_item();
        std::string const indicator = notation::encoding_indicator(info, argument);
        switch (major) {
        case major_type::unsigned_integer:
            text += std::to_string(argument) + indicator;
            end_item();
            return;
        case major_type::negative_integer:
            text += argument == std::numeric_limits<std::uint64_t>::max() ? std::string("-18446744073709551616")
                                                                          : "-" + std::to_string(argument + 1);
            text += indicator;
            end_item();
            return;
        case major_type::byte_string:
        case major_type::text_string:
            text += major == major_type::text_string ? "\"" : "h'";
            string_major = major;
            string_left = argument;
            string_info = indicator.empty() ? std::uint8_t{0} : info;
            if (argument == 0)
                end_string();
            return;
        case major_type::array:
        case major_type::map:
            text += major == major_type::map ? "{" : "[";
            if (!indicator.empty())
                text += indicator + " ";
            if (argument == 0) {
                text += major == major_type::map ? "}" : "]";
                end_item();
                return;
            }
            opened.push_back({major == major_type::map ? argument * rfc8949::data_items_per_pair : argument, 0, major});
            return;
        case major_type::tag:
            text += std::to_string(argument) + indicator + "(";
            opened.push_back({1, 0, major});
            return;
        case major_type::simple:
            break;
        }
        switch (info) {
        case std::to_underlying(simple_value::false_value):
            text += "false";
            break;
        case std::to_underlying(simple_value::true_value):
            text += "true";
            break;
        case std::to_underlying(simple_value::null):
            text += "null";
            break;
        case std::to_underlying(simple_value::undefined):
            text += "undefined";
            break;
        case std::to_underlying(rfc8949::simple_information::half_precision_float):
        case std::to_underlying(rfc8949::simple_information::single_precision_float):
        case std::to_underlying(rfc8949::simple_information::double_precision_float):
            text += notation::float_of(info, argument);
            break;
        default:
            text += "simple(" + std::to_string(argument) + ")";
            break;
        }
        end_item();
    }

    void write_head(major_type const major, std::uint64_t const argument)
    {
        write_head(major, detail::heads::preferred_argument_info(argument), argument);
    }

    void write_payload(std::string_view const part)
    {
        if (part.empty())
            return;
        if (string_major == major_type::text_string) {
            std::string const quoted = notation::quoted_of(part);
            text.append(quoted, 1, quoted.size() - std::string_view("\"\"").size());
        } else {
            text += notation::hex_of(part);
        }
        string_left -= part.size();
        if (string_left == 0)
            end_string();
    }

    void write_payload(std::span<std::byte const> const part)
    {
        if (part.empty())
            return;
        for (std::byte const b : part)
            notation::encode_base16(text, std::to_integer<std::uint8_t>(b));
        string_left -= part.size();
        if (string_left == 0)
            end_string();
    }

    std::string release()
    {
        return std::move(text);
    }
};

}

/**
 * @brief Gives the text of the item in @p encoded in the diagnostic notation of RFC 8949 section 8.
 *
 * A head that is not in preferred form carries its encoding indicator, for example `1_0`.
 * Indefinite length is printed, for example `[_ 1]`.
 * A tag is printed as its number and its content; tag 29 is not resolved.
 * @p encoded holds exactly one item.
 * The call does not check tag content, UTF-8 or duplicate keys.
 *
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
    detail::heads::decoder<true> d{encoded};
    std::string out;
    if (auto const r =
            detail::extended_diagnostic_notation::write_diagnostic(out, d);
        !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const one = detail::validity::check_one_data_item(d.encoded); !one) [[unlikely]]
        return std::unexpected(one.error());
    return out;
}

/**
 * @brief Writes the first data item in a range of bytes in diagnostic notation.
 * @ingroup tools
 */
template <borrowed_range_of_bytes Encoded>
    requires(!std::is_array_v<std::remove_cvref_t<Encoded>>)
std::expected<std::string, error> diagnostic_notation(Encoded &&encoded)
{
    return diagnostic_notation(detail::chars_of(encoded));
}

/**
 * @brief Gives the CBOR bytes that the extended diagnostic notation (EDN) in @p text writes.
 *
 * The text holds one item, with white space and comments around it, in the notation of RFC 8949 section 8 and of
 * draft-ietf-cbor-edn-literals. The bytes are one well-formed item. Each head has the width that its encoding
 * indicator gives, for example `1_1`, and else the preferred width. Embedded CBOR `<<...>>` holds its bytes as they
 * are written, also a CBOR sequence or no item.
 * The call does not check validity: a repeated key, a tag content as written, such as `64(1)` or `29(0)`, and a text
 * string that is not UTF-8 stay in the bytes. cbor::parse_edn() gives the tree and checks it.
 *
 * @code{.example}
 * call: cbor::diag2cbor("[1, 1_1]")
 * result: 5 bytes: 82 01 19 00 01
 * @endcode
 *
 * @retval syntax_error the text is not one item of the notation, text follows the item, or the text uses
 * indefinite length
 * @retval nesting_depth_exceeded the item is deeper than the stack of the thread holds
 * @see cbor::parse_edn()
 * @see cbor::diagnostic_notation()
 *
 * @ingroup tools
 */
constexpr std::expected<std::string, error> diag2cbor(std::string_view const text)
{
    using edn = detail::extended_diagnostic_notation;
    auto const start = edn::optional_space_end(text, 0);
    if (!start) [[unlikely]]
        return std::unexpected(error::syntax_error);
    std::string bytes;
    auto const next = edn::parse_literal({text, *start}, bytes);
    if (!next) [[unlikely]]
        return std::unexpected(next.error() == error::invalid_path ? error::syntax_error : next.error());
    auto const end = edn::optional_space_end(text, *next);
    if (!end || *end != text.size()) [[unlikely]]
        return std::unexpected(error::syntax_error);
    return bytes;
}

template <std::size_t N>
class edn_literal;

inline namespace literals
{

template <fixed_string Text>
consteval auto operator""_edn();

}

/**
 * @brief Holds the CBOR bytes of one item that the literal `"..."_edn` read from its EDN text at compile time.
 *
 * Only the literal makes one, so the bytes are always one well-formed item, as cbor::diag2cbor() writes it.
 * cbor::lazy::at() takes it as a key or an index.
 *
 * @see cbor::lazy::at()
 * @see cbor::diag2cbor()
 *
 * @ingroup tools
 */
template <std::size_t N>
class edn_literal
{
    std::array<char, N> encoded;

    consteval explicit edn_literal(std::array<char, N> const &bytes) : encoded(bytes)
    {
    }

    template <fixed_string Text>
    friend consteval auto literals::operator""_edn();

    friend struct lazy;
};

inline namespace literals
{

/**
 * @brief Reads the EDN text of one item at compile time and gives its CBOR bytes as a cbor::edn_literal.
 *
 * The reader is the one of cbor::diag2cbor(). A text that is not one item of EDN does not compile.
 * `using namespace cbor::literals;` or `using namespace cbor;` makes the literal visible.
 *
 * @code{.example}
 * message: {h'01': "bytes", [1, 2]: "pair"}
 * call: doc->at("h'01'"_edn)->get<std::string_view>()
 * result: ref to "bytes"
 * call: doc->at("[1, 2]"_edn)->get<std::string_view>()
 * result: ref to "pair"
 * @endcode
 *
 * @see cbor::lazy::at()
 * @see cbor::diag2cbor()
 *
 * @ingroup tools
 */
template <fixed_string Text>
consteval auto operator""_edn()
{
    constexpr std::size_t size =
        diag2cbor(Text.view()).transform([](std::string const &bytes) { return bytes.size(); }).value_or(0);
    static_assert(size != 0, "cbor::literals::operator\"\"_edn: the text is not one item of EDN");
    std::array<char, size> bytes{};
    if (auto const written = diag2cbor(Text.view()))
        std::ranges::copy(*written, bytes.begin());
    return edn_literal<size>(bytes);
}

}

/**
 * @brief Reads the extended diagnostic notation (EDN) in @p text into a tree.
 *
 * The text holds one item, with white space and comments around it, in the notation of RFC 8949 section 8 and of
 * draft-ietf-cbor-edn-literals. JSON text is EDN text. The tree is the one that cbor::decode() gives for the bytes
 * of cbor::diag2cbor(); cbor::encode() writes it, and item::to_edn() gives the text back.
 *
 * @retval syntax_error the text is not one item of the notation, text follows the item, or the content of a tag 24
 * is not one item
 * @retval nesting_depth_exceeded the item is deeper than the stack of the thread holds
 * @retval string_length_exceeded a string is longer than `limits::string_length`, where a build sets that limit
 * @retval container_elements_exceeded an array or a map holds more than `limits::container_elements` items, where a
 * build sets that limit
 * @retval inadmissible_type_for_tag_content a typed array (RFC 8746) whose content is not a byte string of whole
 * elements
 * @see cbor::diag2cbor()
 * @see item::to_edn()
 *
 * @ingroup tools
 */
inline std::expected<owning<item>, error> parse_edn(std::string_view const text)
{
    if (auto tree = detail::extended_diagnostic_notation::read_root_minify(text)) [[likely]]
        return tree;
    return diag2cbor(text).and_then([](std::string const &bytes) { return decode(std::string_view(bytes)); });
}

inline std::expected<std::string, error> item::to_edn() const
{
    std::string bytes;
    if (auto const r = encode(bytes, *this); !r) [[unlikely]]
        return std::unexpected(r.error());
    return diagnostic_notation(bytes);
}

/**
 * @brief Gives the text of the tree at `root` in the extended diagnostic notation (EDN).
 *
 * A tree with no `shared_reference` is walked; the call reads no bytes. Each integer, length and tag number carries
 * the encoding indicator of the width that its item keeps, as cbor::diagnostic_notation() writes it, and a float
 * keeps its width. A tag 24 gives its byte string. cbor::parse_edn() reads the text back into the same tree.
 * A `root` that holds a `shared_reference` (a tree of cbor::decode_cycles()) gives the text of the bytes of
 * cbor::encode(): a tag 28 stands only on a value that a tag 29 names, and each tag 29 has the index of its mark in
 * the text, in its shortest head. A `shared_reference` stands for its target, and an inner `root` also writes the
 * values above it that its references name. cbor::decode_cycles() of the bytes of cbor::diag2cbor() of the text of
 * the root of the tree gives a tree with the same targets.
 *
 * @code{.example}
 * bytes: 82 01 19 00 02
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of [1, 2_1]
 * call: cbor::to_edn(**tree)
 * result: "[1, 2_1]"
 * @endcode
 *
 * @retval nesting_depth_exceeded the tree, or the path through its references, is deeper than the stack of the
 * thread holds
 * @see cbor::to_edn(owning<item> const &)
 * @see cbor::parse_edn()
 *
 * @ingroup tools
 */
inline std::expected<std::string, error> to_edn(item const &root)
{
    if (root.m_holds_shared_reference) [[unlikely]]
        return root.to_edn();
    std::string out;
    if (auto const r = detail::extended_diagnostic_notation::write_item(out, root); !r) [[unlikely]]
        return std::unexpected(r.error());
    return out;
}

/**
 * @brief Gives the text of the whole tree that `tree` owns in the extended diagnostic notation (EDN).
 *
 * The text is that of cbor::to_edn(item const &) of the root.
 *
 * @code{.example}
 * bytes: d8 1c 81 d8 1d 00
 * call: auto const tree = cbor::decode_cycles(bytes)
 * result: owning item of 28([29(0)])
 * call: cbor::to_edn(*tree)
 * result: "28([29(0)])"
 * @endcode
 *
 * @retval nesting_depth_exceeded the tree is deeper than the stack of the thread holds
 * @throws std::logic_error `tree` owns no tree
 * @see cbor::to_edn(item const &)
 * @see cbor::decode_cycles()
 *
 * @ingroup tools
 */
inline std::expected<std::string, error> to_edn(owning<item> const &tree)
{
    std::ignore = detail::zone_owner::zone_found(tree, "cbor::to_edn: the owning item holds no decoded tree");
    return to_edn(*tree);
}

}
