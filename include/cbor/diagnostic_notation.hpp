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
#include <utility>
#include <vector>

#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"

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

    struct nesting {
        std::size_t depth;
        std::size_t depth_max;
    };

    static constexpr int no_indicator = -1;
    static constexpr int immediate_indicator = -2;

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

    static constexpr bool digit(char const c)
    {
        return c >= '0' && c <= '9';
    }

    static constexpr int hex_digit_value(char const c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    static constexpr std::expected<void, error> head_append(std::string &out, major_type const major,
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
            heads::head_append(out, major, static_cast<std::uint8_t>(argument), argument);
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
        heads::head_append(out, major, static_cast<std::uint8_t>(info), argument);
        return {};
    }

    struct indicated {
        std::size_t at;
        int indicator;
    };

    static constexpr std::expected<indicated, error> indicator_parse(std::string_view const text, std::size_t const at)
    {
        if (at >= text.size() || text[at] != '_')
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

    static constexpr void utf8_append(std::string &out, std::uint32_t const c)
    {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xc0 | c >> 6));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | c >> 12));
            out.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | c >> 18));
            out.push_back(static_cast<char>(0x80 | (c >> 12 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }

    struct code_unit {
        std::size_t at;
        std::uint32_t value;
    };

    static constexpr std::expected<code_unit, error> hex4_parse(std::string_view const text, std::size_t const at)
    {
        if (at + 4 > text.size()) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::uint32_t value = 0;
        for (char const c : std::span(text).subspan(at, 4)) {
            int const v = hex_digit_value(c);
            if (v < 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            value = value << 4 | static_cast<std::uint32_t>(v);
        }
        return code_unit{at + 4, value};
    }

    static constexpr std::expected<std::size_t, error> quoted_parse(std::string_view const text, std::size_t at,
                                                                    std::string &out)
    {
        char const quote = text[at++];
        for (;;) {
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text[at++];
            if (c == quote)
                return at;
            if (static_cast<unsigned char>(c) < 0x20) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
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
                    std::size_t const close = text.find('}', at);
                    if (close == std::string_view::npos || close == at + 1 || close > at + 9) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    for (char const h : std::span(text).subspan(at + 1, close - at - 1)) {
                        int const v = hex_digit_value(h);
                        if (v < 0) [[unlikely]]
                            return std::unexpected(error::invalid_path);
                        c1 = c1 << 4 | static_cast<std::uint32_t>(v);
                    }
                    at = close + 1;
                    if (c1 > 0x10ffff || (c1 >= 0xd800 && c1 < 0xe000)) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    utf8_append(out, c1);
                    break;
                }
                auto const next = hex4_parse(text, at);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                at = next->at;
                c1 = next->value;
                if (c1 >= 0xdc00 && c1 < 0xe000) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (c1 >= 0xd800 && c1 < 0xdc00) {
                    if (at + 2 > text.size() || text[at] != '\\' || text[at + 1] != 'u') [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    auto const low = hex4_parse(text, at + 2);
                    if (!low || low->value < 0xdc00 || low->value >= 0xe000) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    std::uint32_t const c2 = low->value;
                    at = low->at;
                    c1 = 0x10000 + ((c1 - 0xd800) << 10) + (c2 - 0xdc00);
                }
                utf8_append(out, c1);
                break;
            }
            [[unlikely]] default:
                return std::unexpected(error::invalid_path);
            }
        }
    }

    static constexpr std::expected<std::size_t, error> hex_string_parse(std::string_view const text, std::size_t at,
                                                                        std::string &out)
    {
        int high = -1;
        for (;; ++at) {
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text[at];
            if (c == '\'')
                break;
            if (blank(c))
                continue;
            int const v = hex_digit_value(c);
            if (v < 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (high < 0) {
                high = v;
            } else {
                out.push_back(static_cast<char>(high << 4 | v));
                high = -1;
            }
        }
        if (high >= 0) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return at + 1;
    }

    static constexpr std::expected<std::size_t, error> base64_string_parse(std::string_view const text, std::size_t at,
                                                                           std::string &out)
    {
        std::uint32_t bits = 0;
        int count = 0;
        int padding = 0;
        for (;; ++at) {
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text[at];
            if (c == '\'')
                break;
            if (blank(c))
                continue;
            int v;
            if (c >= 'A' && c <= 'Z')
                v = c - 'A';
            else if (c >= 'a' && c <= 'z')
                v = c - 'a' + 26;
            else if (digit(c))
                v = c - '0' + 52;
            else if (c == '+' || c == '-')
                v = 62;
            else if (c == '/' || c == '_')
                v = 63;
            else if (c == '=') {
                ++padding;
                continue;
            } else [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (padding != 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            bits = bits << 6 | static_cast<std::uint32_t>(v);
            if (++count == 4) {
                out.push_back(static_cast<char>(bits >> 16));
                out.push_back(static_cast<char>(bits >> 8));
                out.push_back(static_cast<char>(bits));
                bits = 0;
                count = 0;
            }
        }
        if (count == 1 || (padding != 0 && count + padding != 4)) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (count == 2)
            out.push_back(static_cast<char>(bits >> 4));
        if (count == 3) {
            out.push_back(static_cast<char>(bits >> 10));
            out.push_back(static_cast<char>(bits >> 2));
        }
        return at + 1;
    }

    static constexpr std::expected<void, error> float_append(std::string &out, double const value, int const indicator)
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
        constexpr std::array<std::uint64_t, 3> quiet_nan{0x7e00, 0x7fc00000, 0x7ff8000000000000};
        std::uint64_t const argument =
            nan ? quiet_nan[static_cast<std::size_t>(
                      info - std::to_underlying(rfc8949::simple_float_information::half_precision_float))]
                : heads::float_encode(static_cast<rfc8949::simple_float_information>(info), value);
        return head_append(out, major_type::simple_float, argument, info);
    }

    static constexpr std::expected<std::size_t, error> number_parse(std::string_view const text, std::size_t at,
                                                                    std::string &out)
    {
        bool negative = false;
        char const sign = text[at];
        if (sign == '+' || sign == '-')
            negative = text[at++] == '-';
        auto const word = [&](std::string_view const w) { return std::ranges::starts_with(std::span(text).subspan(at), w); };
        if (word("Infinity") || word("NaN")) {
            bool const nan = word("NaN");
            if (sign == '+' || (nan && sign == '-')) [[unlikely]]
                return std::unexpected(error::invalid_path);
            at += nan ? std::string_view("NaN").size() : std::string_view("Infinity").size();
            auto const next = indicator_parse(text, at);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            int const indicator = next->indicator;
            double const infinity = std::numeric_limits<double>::infinity();
            auto const r = float_append(out, nan ? std::numeric_limits<double>::quiet_NaN() : negative ? -infinity : infinity,
                                        indicator);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            return next->at;
        }
        int base = 10;
        if (word("0x") || word("0X")) {
            base = 16;
            at += std::string_view("0x").size();
        } else if (word("0o")) {
            base = 8;
            at += std::string_view("0o").size();
        } else if (word("0b")) {
            base = 2;
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
            if (c == '.' && !fraction && (base == 10 || base == 16)) {
                fraction = true;
                real = true;
                continue;
            }
            int const v = hex_digit_value(c);
            if (v < 0 || v >= base)
                break;
            ++digits;
            auto const next = validity::checked_mul(mantissa, static_cast<std::uint64_t>(base))
                                  .and_then([v](std::uint64_t const m) {
                                      return validity::checked_add(m, static_cast<std::uint64_t>(v));
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
        if (base == 10 && text[first] == '0' && first + 1 < at && digit(text[first + 1])) [[unlikely]]
            return std::unexpected(error::invalid_path);
        bool const exponent_part = at < text.size() && ((base == 10 && (text[at] == 'e' || text[at] == 'E')) ||
                                                        (base == 16 && (text[at] == 'p' || text[at] == 'P')));
        if (base == 16 && real && !exponent_part) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (exponent_part) {
            real = true;
            ++at;
            bool exponent_negative = false;
            if (at < text.size() && (text[at] == '+' || text[at] == '-'))
                exponent_negative = text[at++] == '-';
            int e = 0;
            std::size_t const e_first = at;
            while (at < text.size() && digit(text[at]) && e < 100000)
                e = e * 10 + (text[at++] - '0');
            if (at == e_first || (at < text.size() && digit(text[at]))) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (base == 16)
                exponent = 4 * exponent + (exponent_negative ? -e : e);
            else
                exponent += exponent_negative ? -e : e;
        } else if (base == 16) {
            exponent *= 4;
        }
        auto const next = indicator_parse(text, at);
        if (!next) [[unlikely]]
            return std::unexpected(next.error());
        int const indicator = next->indicator;
        if (overflow) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (!real) {
            if (negative && mantissa != 0) {
                if (auto const r = head_append(out, major_type::negative_integer, mantissa - 1, indicator); !r) [[unlikely]]
                    return std::unexpected(r.error());
            } else if (auto const r = head_append(out, major_type::unsigned_integer, mantissa, indicator); !r) [[unlikely]] {
                return std::unexpected(r.error());
            }
            return next->at;
        }
        constexpr std::uint64_t exact_max = std::uint64_t{1} << std::numeric_limits<double>::digits;
        double value;
        if (mantissa == 0) {
            value = 0.0;
        } else if (base == 16) {
            if (mantissa > exact_max) [[unlikely]]
                return std::unexpected(error::invalid_path);
            value = static_cast<double>(mantissa);
            for (int i = 0; i < exponent; ++i)
                value *= 2.0;
            for (int i = 0; i > exponent; --i)
                value *= 0.5;
        } else {
            constexpr std::array<double, 23> powers{1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
            while (exponent > 22 && mantissa <= exact_max / 10) {
                mantissa *= 10;
                --exponent;
            }
            while (exponent < 0 && mantissa % 10 == 0) {
                mantissa /= 10;
                ++exponent;
            }
            if (mantissa > exact_max || exponent > 22 || exponent < -22) [[unlikely]]
                return std::unexpected(error::invalid_path);
            value = exponent >= 0 ? static_cast<double>(mantissa) * powers[static_cast<std::size_t>(exponent)]
                                  : static_cast<double>(mantissa) / powers[static_cast<std::size_t>(-exponent)];
        }
        if (auto const r = float_append(out, negative ? -value : value, indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        return next->at;
    }

    static constexpr std::expected<parsed, error> separator_parse(std::string_view const text, std::size_t const at)
    {
        std::size_t next = blank_end(text, at);
        bool separated = next != at;
        if (next < text.size() && text[next] == ',') {
            next = blank_end(text, next + 1);
            separated = true;
        }
        return parsed{next, separated};
    }

    static constexpr std::expected<std::size_t, error> container_parse(literal_cursor const cursor, std::string &out,
                                                                       nesting const n)
    {
        std::string_view const text = cursor.text;
        std::size_t at = cursor.at;
        bool const map = text[at++] == '{';
        char const close = map ? '}' : ']';
        auto const after = indicator_parse(text, at);
        if (!after) [[unlikely]]
            return std::unexpected(after.error());
        int const indicator = after->indicator;
        at = blank_end(text, after->at);
        std::string items;
        std::uint64_t count = 0;
        bool separated = true;
        while (at < text.size() && text[at] != close) {
            if (!separated) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto next = literal_parse({text, at}, items, {n.depth + 1, n.depth_max});
            if (!next) [[unlikely]]
                return next;
            if (map) {
                std::size_t const colon = blank_end(text, *next);
                if (colon >= text.size() || text[colon] != ':') [[unlikely]]
                    return std::unexpected(error::invalid_path);
                next = literal_parse({text, blank_end(text, colon + 1)}, items, {n.depth + 1, n.depth_max});
                if (!next) [[unlikely]]
                    return next;
            }
            ++count;
            auto const s = separator_parse(text, *next);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            at = s->at;
            separated = s->separated;
        }
        if (at >= text.size()) [[unlikely]]
            return std::unexpected(error::invalid_path);
        major_type const major = map ? major_type::map : major_type::array;
        if (indicator == std::to_underlying(rfc8949::additional_information::indefinite_length)) {
            out.push_back(heads::initial_byte(major, static_cast<std::uint64_t>(indicator)));
            out += items;
            out.push_back(
                heads::initial_byte(major_type::simple_float,
                                    std::to_underlying(rfc8949::simple_float_information::break_stop_code)));
        } else {
            if (auto const r = head_append(out, major, count, indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            out += items;
        }
        return at + 1;
    }

    static constexpr std::expected<std::size_t, error> string_finish(std::string_view const text, std::size_t const at,
                                                                     std::string &out, major_type const major,
                                                                     std::string_view const content)
    {
        auto const next = indicator_parse(text, at);
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
        if (auto const r = head_append(out, major, content.size(), indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        out += content;
        return next->at;
    }

    static constexpr std::expected<std::size_t, error> literal_parse(literal_cursor const cursor, std::string &out, nesting const n)
    {
        if (auto const r = validity::check_nesting_depth(n.depth, n.depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::string_view const text = cursor.text;
        std::size_t const at = cursor.at;
        if (at >= text.size()) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::string_view const rest{std::span(text).subspan(at)};
        char const c = rest.front();
        if (c == '[' || c == '{')
            return container_parse(cursor, out, n);
        if (c == '"' || c == '\'') {
            std::string content;
            auto const next = quoted_parse(text, at, content);
            if (!next) [[unlikely]]
                return next;
            return string_finish(text, *next, out, c == '"' ? major_type::text_string : major_type::byte_string, content);
        }
        if (rest.starts_with("h'") || rest.starts_with("b64'")) {
            std::string content;
            bool const hex = rest.front() == 'h';
            auto const next = hex ? hex_string_parse(text, at + 2, content) : base64_string_parse(text, at + 4, content);
            if (!next) [[unlikely]]
                return next;
            return string_finish(text, *next, out, major_type::byte_string, content);
        }
        if (rest.starts_with("<<")) {
            std::string content;
            std::size_t next = blank_end(text, at + std::string_view("<<").size());
            bool separated = true;
            while (!std::ranges::starts_with(std::span(text).subspan(next), std::string_view(">>"))) {
                if (!separated || next >= text.size()) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                auto const item = literal_parse({text, next}, content, {n.depth + 1, n.depth_max});
                if (!item) [[unlikely]]
                    return item;
                auto const s = separator_parse(text, *item);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                next = s->at;
                separated = s->separated;
            }
            return string_finish(text, next + std::string_view(">>").size(), out, major_type::byte_string,
                                 content);
        }
        constexpr auto names = std::to_array<std::string_view>({"false", "true", "null", "undefined"});
        for (std::size_t i = 0; i < names.size(); ++i)
            if (rest.starts_with(names[i])) {
                out.push_back(heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::false_value) + i));
                return at + names[i].size();
            }
        if (rest.starts_with("simple(")) {
            std::size_t next = blank_end(text, at + std::string_view("simple(").size());
            std::size_t const first = next;
            unsigned value = 0;
            while (next < text.size() && digit(text[next]) && value < 1000)
                value = value * 10 + static_cast<unsigned>(text[next++] - '0');
            if (next == first || (text[first] == '0' && next > first + 1)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            next = blank_end(text, next);
            if (next >= text.size() || text[next] != ')' ||
                value > std::numeric_limits<std::uint8_t>::max() ||
                !validity::check_simple_value(heads::preferred_argument_info(value), value)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (auto const r = head_append(out, major_type::simple_float, value, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next + 1;
        }
        if (!(digit(c) || c == '-' || c == '+' || c == '.' || rest.starts_with("Infinity") || rest.starts_with("NaN")))
            [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t digits_end = at;
        while (digits_end < text.size() && digit(text[digits_end]))
            ++digits_end;
        auto const tag_open = indicator_parse(text, digits_end);
        int const indicator = tag_open ? tag_open->indicator : no_indicator;
        if (digits_end != at && tag_open && tag_open->at < text.size() && text[tag_open->at] == '(' &&
            indicator != std::to_underlying(rfc8949::additional_information::indefinite_length)) {
            if (text[at] == '0' && digits_end > at + 1) [[unlikely]]
                return std::unexpected(error::invalid_path);
            std::uint64_t number = 0;
            for (char const d : std::span(text).subspan(at, digits_end - at)) {
                auto const next = validity::checked_mul(number, 10).and_then([d](std::uint64_t const m) {
                    return validity::checked_add(m, static_cast<std::uint64_t>(d - '0'));
                });
                if (!next) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                number = *next;
            }
            if (auto const r = head_append(out, major_type::tag, number, indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const content = literal_parse({text, blank_end(text, tag_open->at + 1)}, out, {n.depth + 1, n.depth_max});
            if (!content) [[unlikely]]
                return content;
            std::size_t const close = blank_end(text, *content);
            if (close >= text.size() || text[close] != ')') [[unlikely]]
                return std::unexpected(error::invalid_path);
            return close + 1;
        }
        return number_parse(text, at, out);
    }

    static constexpr std::expected<std::size_t, error> canonical_append(std::string &out, literal_cursor const cursor, nesting const n)
    {
        if (auto const r = validity::check_nesting_depth(n.depth, n.depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::string_view const encoded = cursor.text;
        std::size_t const at = cursor.at;
        auto const h = heads::raw_head_read(encoded, at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        bool const indefinite =
            h->info == std::to_underlying(rfc8949::additional_information::indefinite_length);
        std::size_t next = h->at;
        switch (h->major) {
        case major_type::unsigned_integer:
        case major_type::negative_integer:
            if (auto const r = head_append(out, h->major, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        case major_type::byte_string:
        case major_type::text_string: {
            std::string content;
            if (!indefinite) {
                heads::decoder d{std::string_view(std::span(encoded).subspan(next))};
                auto const s = d.byte_string_decode(h->argument);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                content = *s;
                next += s->size();
            } else {
                while (!heads::break_at(encoded, next)) {
                    auto const chunk = heads::raw_head_read(encoded, next);
                    if (!chunk) [[unlikely]]
                        return std::unexpected(chunk.error());
                    if (error const c =
                            validity::check_chunk(h->major, chunk->major, chunk->info).error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::unexpected(c);
                    if (encoded.size() - chunk->at < chunk->argument) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    content += std::string_view(std::span(encoded).subspan(chunk->at, static_cast<std::size_t>(chunk->argument)));
                    next = chunk->at + static_cast<std::size_t>(chunk->argument);
                }
                ++next;
            }
            if (auto const r = head_append(out, h->major, content.size(), no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            out += content;
            return next;
        }
        case major_type::array:
        case major_type::map: {
            bool const map = h->major == major_type::map;
            std::vector<std::pair<std::string, std::string>> items;
            for (std::uint64_t i = 0; indefinite ? !heads::break_at(encoded, next) : i < h->argument; ++i) {
                std::pair<std::string, std::string> item;
                auto const first = canonical_append(item.first, {encoded, next}, {n.depth + 1, n.depth_max});
                if (!first) [[unlikely]]
                    return first;
                next = *first;
                if (map) {
                    auto const second = canonical_append(item.second, {encoded, next}, {n.depth + 1, n.depth_max});
                    if (!second) [[unlikely]]
                        return second;
                    next = *second;
                }
                items.push_back(std::move(item));
            }
            if (indefinite)
                ++next;
            if (map) {
                std::ranges::sort(items);
                if (error const c =
                        validity::check_key_unique(
                            !validity::keys_unique(items, &std::pair<std::string, std::string>::first))
                            .error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
            }
            if (auto const r = head_append(out, h->major, items.size(), no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            for (auto const &[first, second] : items)
                out += first + second;
            return next;
        }
        case major_type::tag:
            if (auto const r = head_append(out, major_type::tag, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return canonical_append(out, {encoded, next}, {n.depth + 1, n.depth_max});
        default:
            break;
        }
        constexpr std::uint8_t half =
            std::to_underlying(rfc8949::simple_float_information::half_precision_float);
        constexpr std::uint8_t twice =
            std::to_underlying(rfc8949::simple_float_information::double_precision_float);
        if (h->info < half) {
            if (error const r = validity::check_simple_value(h->info, h->argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            if (auto const r = head_append(out, major_type::simple_float, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }
        if (h->info > twice) [[unlikely]]
            return std::unexpected(error::syntax_error);
        heads::float_key const key = heads::float_key_of(h->info, h->argument);
        if (key.nan) {
            if (auto const r = head_append(out, major_type::simple_float,
                                           key.widened | std::uint64_t{heads::double_precision.exponent_max}
                                                             << heads::double_precision.significand_bits,
                                           twice);
                !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }
        if (auto const r = float_append(out, key.value == 0 ? 0.0 : key.value, no_indicator); !r) [[unlikely]]
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

    static std::string decimal_of(std::uint64_t const n)
    {
        std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 1> text;
        auto const end = std::to_chars(text.data(), std::to_address(text.end()), n).ptr;
        return std::string(text.data(), end);
    }

    static std::string hex_of(std::string_view const bytes)
    {
        constexpr std::string_view digits = "0123456789abcdef";
        std::string out;
        out.reserve(2 * bytes.size());
        for (char const c : bytes) {
            out.push_back(digits[static_cast<std::uint8_t>(c) >> 4]);
            out.push_back(digits[static_cast<std::uint8_t>(c) & 0xf]);
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
                if (static_cast<std::uint8_t>(c) < 0x20) {
                    out += "\\u00";
                    out.push_back(digits[static_cast<std::uint8_t>(c) >> 4]);
                    out.push_back(digits[static_cast<std::uint8_t>(c) & 0xf]);
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
        std::array<char, 400> text;
        double const magnitude = std::fabs(value);
        auto const end = std::to_chars(text.data(), std::to_address(text.end()), value,
                                       magnitude == 0 || (magnitude >= 1e-7 && magnitude < 1e21) ? std::chars_format::fixed
                                                                                               : std::chars_format::scientific)
                             .ptr;
        std::string out(text.data(), end);
        std::size_t const e = out.find('e');
        auto const before_exponent = out | std::views::take(e);
        std::string mantissa(before_exponent.begin(), before_exponent.end());
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
        constexpr std::array<std::uint64_t, 3> quiet_nan{0x7e00, 0x7fc00000, 0x7ff8000000000000};
        std::size_t const width =
            info - std::to_underlying(rfc8949::simple_float_information::half_precision_float);
        double const value = heads::float_decode(info, argument);
        std::string const indicator =
            width == 0 ? std::string{} : std::string{'_', static_cast<char>('1' + width)};
        if (std::isnan(value)) {
            if (argument == quiet_nan[width])
                return "NaN" + indicator;
            std::string bytes(std::size_t{2} << width, '\0');
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

    struct diagnostic_head {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
    };

    static std::expected<diagnostic_head, error> diagnostic_head_decode(heads::decoder &d)
    {
        if (d.encoded.empty()) [[unlikely]]
            return std::unexpected(error::too_little_data);
        auto const initial = static_cast<std::uint8_t>(d.encoded.front());
        auto const major = static_cast<major_type>(initial >> 5);
        std::uint8_t const info = initial & 0x1f;
        if (error const r = validity::check_additional_information(major, info).error_or(error{});
            r != error{}) [[unlikely]]
            return std::unexpected(r);
        if (info != std::to_underlying(rfc8949::additional_information::indefinite_length))
            return d.head_decode().transform([](heads::head const h) { return diagnostic_head{h.major, h.info, h.argument}; });
        d.encoded.remove_prefix(1);
        return diagnostic_head{major, info, 0};
    }

    static bool break_found(heads::decoder &d)
    {
        if (d.encoded.empty() ||
            d.encoded.front() !=
                heads::initial_byte(major_type::simple_float,
                                    std::to_underlying(rfc8949::simple_float_information::break_stop_code)))
            return false;
        d.encoded.remove_prefix(1);
        return true;
    }

    static std::expected<void, error> diagnostic_write(std::string &out, heads::decoder &d, std::string_view const encoded,
                                                       std::vector<std::size_t> &marks, std::size_t const depth,
                                                       std::size_t const depth_max)
    {
        if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const h = diagnostic_head_decode(d);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        bool const indefinite =
            h->info == std::to_underlying(rfc8949::additional_information::indefinite_length);
        switch (h->major) {
        case major_type::unsigned_integer:
            out += decimal_of(h->argument) + encoding_indicator(h->info, h->argument);
            return {};
        case major_type::negative_integer:
            out += h->argument == std::numeric_limits<std::uint64_t>::max() ? std::string("-18446744073709551616")
                                                                            : "-" + decimal_of(h->argument + 1);
            out += encoding_indicator(h->info, h->argument);
            return {};
        case major_type::byte_string:
        case major_type::text_string: {
            bool const text = h->major == major_type::text_string;
            if (!indefinite) {
                auto const s = d.byte_string_decode(h->argument);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                out += text ? quoted_of(*s) : "h'" + hex_of(*s) + "'";
                out += encoding_indicator(h->info, h->argument);
                return {};
            }
            if (break_found(d)) {
                out += text ? "\"\"_" : "''_";
                return {};
            }
            out += "(_ ";
            for (bool first = true; !break_found(d); first = false) {
                if (!first)
                    out += ", ";
                if (d.encoded.empty()) [[unlikely]]
                    return std::unexpected(error::too_little_data);
                auto const initial = static_cast<std::uint8_t>(d.encoded.front());
                if (error const c =
                        validity::check_chunk(h->major, static_cast<major_type>(initial >> 5), initial & 0x1f)
                            .error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                if (auto const r = diagnostic_write(out, d, encoded, marks, depth + 1, depth_max); !r) [[unlikely]]
                    return r;
            }
            out += ")";
            return {};
        }
        case major_type::array:
        case major_type::map: {
            bool const map = h->major == major_type::map;
            out += map ? "{" : "[";
            std::string const indicator = indefinite ? std::string("_") : encoding_indicator(h->info, h->argument);
            if (!indicator.empty())
                out += indicator + " ";
            for (std::uint64_t i = 0; indefinite ? !break_found(d) : i < h->argument; ++i) {
                if (i != 0)
                    out += ", ";
                if (auto const r = diagnostic_write(out, d, encoded, marks, depth + 1, depth_max); !r) [[unlikely]]
                    return r;
                if (map) {
                    out += ": ";
                    if (auto const r = diagnostic_write(out, d, encoded, marks, depth + 1, depth_max); !r) [[unlikely]]
                        return r;
                }
            }
            out += map ? "}" : "]";
            return {};
        }
        case major_type::tag: {
            std::size_t const content_at = encoded.size() - d.encoded.size();
            if (error const e =
                    validity::check_tag_content(h->argument, encoded, content_at, marks, std::identity{}).error_or(error{});
                e != error{}) [[unlikely]]
                return std::unexpected(e);
            if (h->argument == std::to_underlying(rfc8949::tag_number::shareable))
                marks.push_back(content_at);
            out += decimal_of(h->argument) + encoding_indicator(h->info, h->argument) + "(";
            if (auto const r = diagnostic_write(out, d, encoded, marks, depth + 1, depth_max); !r) [[unlikely]]
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
            if (error const r = validity::check_simple_value(h->info, h->argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            out += "simple(" + decimal_of(h->argument) + ")";
            return {};
        case std::to_underlying(rfc8949::simple_float_information::half_precision_float):
        case std::to_underlying(rfc8949::simple_float_information::single_precision_float):
        case std::to_underlying(rfc8949::simple_float_information::double_precision_float):
            out += float_of(h->info, h->argument);
            return {};
        [[unlikely]] case std::to_underlying(rfc8949::additional_information::indefinite_length):
            return std::unexpected(error::syntax_error);
        default:
            out += "simple(" + decimal_of(h->argument) + ")";
            return {};
        }
    }

    friend class jsonpath;

    friend std::expected<std::string, error> diagnostic_notation(std::string_view encoded);
};

inline std::expected<std::string, error> diagnostic_notation(std::string_view const encoded)
{
    heads::decoder d{encoded};
    std::string out;
    std::vector<std::size_t> marks;
    if (auto const r = extended_diagnostic_notation::diagnostic_write(out, d, encoded, marks, 0, validity::nesting_depth_max_read()); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (!d.encoded.empty()) [[unlikely]]
        return std::unexpected(error::syntax_error);
    return out;
}

}
