#pragma once

#include "common.hpp"
#include "../test/owned_copy.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fuzz
{

// Every text key of the top-level item, so a generated path names keys that exist; a key made of random bytes
// would almost never match.
inline void text_keys(test::value const &v, std::vector<std::string> &keys, int const depth)
{
    if (depth > 16)
        return;
    if (auto const *m = test::get_if<test::map>(v)) {
        for (auto const &[k, x] : *m) {
            if ((std::holds_alternative<test::short_text>(k.kind) || std::holds_alternative<std::string *>(k.kind)))
                keys.emplace_back(k.text());
            text_keys(x, keys, depth + 1);
        }
    } else if (auto const *a = test::get_if<test::array>(v)) {
        for (auto const &e : *a)
            text_keys(e, keys, depth + 1);
    } else if (auto const *t = test::get_if<test::tagged>(v)) {
        text_keys(t->content, keys, depth + 1);
    }
}

inline bool identifier_p(std::string_view const key)
{
    auto const letter = [](char const c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (key.empty() || !letter(key.front()))
        return false;
    for (char const c : key)
        if (!letter(c) && !(c >= '0' && c <= '9'))
            return false;
    return true;
}

// One segment of a generated query, kept beside its text so the reference walks the same query.
struct segment {
    enum class kind { key, index, wildcard } kind;
    std::string key;
    std::int64_t index;
};

// A name selector in single quotes, with the escapes of RFC 9535 2.3.1.1 where a character needs one.
inline double binary16_decode(std::uint16_t const half)
{
    constexpr unsigned exponent_bits = 0x1f;
    constexpr unsigned mantissa_bits = 0x3ff;
    constexpr unsigned sign_bit = 0x8000;
    constexpr int subnormal_scale = -24;
    constexpr int normal_bias = 25;
    constexpr unsigned implicit_one = 1024;
    constexpr unsigned exponent_shift = 10;
    unsigned const exponent = (half >> exponent_shift) & exponent_bits;
    unsigned const mantissa = half & mantissa_bits;
    double const magnitude = exponent == 0 ? std::ldexp(mantissa, subnormal_scale)
                             : exponent != exponent_bits
                                 ? std::ldexp(mantissa + implicit_one, static_cast<int>(exponent) - normal_bias)
                             : mantissa == 0 ? std::numeric_limits<double>::infinity()
                                             : std::numeric_limits<double>::quiet_NaN();
    return half & sign_bit ? -magnitude : magnitude;
}

inline std::string quoted(std::string_view const key)
{
    std::string text = "'";
    for (char const c : key) {
        if (c == '\'' || c == '\\') {
            text += '\\';
            text += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            constexpr std::string_view digits = "0123456789abcdef";
            text += "\\u00";
            text += digits.at(static_cast<unsigned char>(c) >> 4);
            text += digits.at(static_cast<unsigned char>(c) & 0xf);
        } else {
            text += c;
        }
    }
    return text + "'";
}

struct query {
    std::string text;
    std::vector<segment> segments;
    bool valid;
};

// A query of RFC 9535 built from the bytes of the input: name selectors in both forms, index selectors of every
// sign and size, wildcards, blanks, and now and then a byte that breaks the grammar.
inline query query_from(source &in, std::vector<std::string> const &keys)
{
    query q{"$", {}, true};
    for (std::size_t n = in.byte() % 7; n > 0; --n) {
        switch (in.byte() % 7) {
        case 0:
        case 1: {
            std::string key = keys.empty() || in.byte() % 4 == 0 ? in.text() : keys.at(in.byte() % keys.size());
            q.text += identifier_p(key) && in.byte() & 1 ? "." + key : "[" + fuzz::quoted(key) + "]";
            q.segments.push_back({segment::kind::key, key, 0});
            break;
        }
        case 2: {
            std::uint8_t const form = in.byte();
            std::int64_t index = 0;
            if (form % 3 == 0)
                index = static_cast<std::int64_t>(in.number() % (std::uint64_t{1} << 53));
            else
                index = static_cast<std::int8_t>(in.byte()) % 8;
            q.text += "[" + std::to_string(index) + "]";
            q.segments.push_back({segment::kind::index, {}, index});
            break;
        }
        case 3:
            q.text += in.byte() & 1 ? "[*]" : ".*";
            q.segments.push_back({segment::kind::wildcard, {}, 0});
            break;
        case 4:
            q.text += " ";
            break;
        case 5:
            q.text += "[ 'a' ]";
            q.segments.push_back({segment::kind::key, "a", 0});
            break;
        default:
            q.text += static_cast<char>(in.byte());
            q.valid = false;
            break;
        }
    }
    if (q.text.ends_with(' '))
        q.valid = false;
    return q;
}

// The reference decodes a tag 24 embedded data item whole, and lazy reads it only up to the item a step needs. Where the
// whole embedded data item does not decode, the reference cannot say what lazy finds.
struct undecided {};

// The numbers of an RFC 8746 typed array (2.1) as values of the model: the tag names the class, the length and the byte
// order of each element. A byte string whose length is no multiple of the element length has no elements. The model
// holds no binary128, so a query that reaches one is undecided.
inline std::optional<std::vector<test::value>> typed_array_elements(std::uint64_t const tag, std::string_view const bytes)
{
    constexpr std::uint64_t f = 0x10;
    constexpr std::uint64_t s = 0x08;
    constexpr std::uint64_t e = 0x04;
    constexpr std::uint64_t ll = 0x03;
    constexpr std::size_t bits_per_byte = 8;
    bool const floating = (tag & f) != 0;
    bool const signed_integer = (tag & s) != 0;
    bool const little_endian = (tag & e) != 0;
    std::size_t const length = std::size_t{1} << ((tag & ll) + (floating ? 1 : 0));
    if (bytes.size() % length != 0)
        return std::nullopt;
    if (length > sizeof(std::uint64_t))
        throw undecided{};
    std::vector<test::value> elements;
    for (std::size_t at = 0; at < bytes.size(); at += length) {
        std::uint64_t bits = 0;
        for (std::size_t i = 0; i < length; ++i)
            bits = bits << bits_per_byte | static_cast<unsigned char>(bytes[at + (little_endian ? length - 1 - i : i)]);
        if (floating && length == 2)
            elements.push_back(test::value{binary16_decode(static_cast<std::uint16_t>(bits))});
        else if (floating && length == 4)
            elements.push_back(test::value{static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)))});
        else if (floating)
            elements.push_back(test::value{std::bit_cast<double>(bits)});
        else if (signed_integer && bits >> (length * bits_per_byte - 1) != 0)
            elements.push_back(test::value{test::negative{(~bits) & (~std::uint64_t{0} >> (sizeof(std::uint64_t) * bits_per_byte - length * bits_per_byte))}});
        else
            elements.push_back(test::value{bits});
    }
    return elements;
}

// The reference: RFC 9535 2.1.2 over the eager value. Each segment maps the nodelist to the children it selects.
// A name selects a value under an equal text key; an index counts from the end when negative and selects a
// value under an equal integer key of a map; a wildcard selects every element or every value. Without a
// wildcard a missing child is an error; with one it adds no node. A segment passes every tag that the path does
// not interpret, tags 28 and 29 included (RFC 8949 6.1). Tag 24 is a top-level item of its own when a
// segment goes into it. A typed array of RFC 8746 is the array of its numbers when a segment goes into it, and its
// content must be a byte string (RFC 8746 2, RFC 8949 5.3.2). A nodelist longer than the message is an error.
inline std::optional<test::value> walked(test::value const &start, std::vector<segment> const &segments,
                                         std::size_t const limit)
{
    bool nodelist = false;
    for (auto const &s : segments)
        nodelist = nodelist || s.kind == segment::kind::wildcard;
    std::vector<test::value> nodes{start};
    for (auto const &s : segments) {
        std::vector<test::value> next;
        for (test::value v : nodes) {
            while (auto const *t = test::get_if<test::tagged>(v)) {
                if (t->tag == 2 || t->tag == 3 || t->tag == 24 || (t->tag >= 64 && t->tag <= 87))
                    break;
                test::value const content = t->content;
                v = content;
            }
            if (auto const *t = test::get_if<test::tagged>(v); t && t->tag == 24) {
                auto const *b = test::get_if<test::bytes>(t->content);
                if (!b)
                    return std::nullopt;
                auto inner = test::decoded(b->b);
                if (!inner)
                    throw undecided{};
                v = *inner;
            }
            if (auto const *t = test::get_if<test::tagged>(v); t && t->tag >= 64 && t->tag <= 87 && t->tag != 76) {
                auto const *b = test::get_if<test::bytes>(t->content);
                if (!b)
                    return std::nullopt;
                auto const elements = typed_array_elements(t->tag, b->b);
                if (!elements)
                    return std::nullopt;
                v = test::value{test::array(elements->begin(), elements->end())};
            }
            std::size_t const before = next.size();
            if (s.kind == segment::kind::wildcard) {
                if (auto const *a = test::get_if<test::array>(v))
                    next.insert(next.end(), a->begin(), a->end());
                else if (auto const *m = test::get_if<test::map>(v))
                    for (auto const &[k, x] : *m)
                        next.push_back(x);
                else if (!nodelist)
                    return std::nullopt;
                if (next.size() > limit)
                    return std::nullopt;
                continue;
            }
            if (auto const *m = test::get_if<test::map>(v)) {
                for (auto const &[k, x] : *m) {
                    // An eager tree keeps a tag 2 or 3 over content that is not a byte string as a tag; a lookup by
                    // number reads such a key as a bignum and gives inadmissible_type_for_tag_content.
                    if (auto const *g = std::get_if<test::tagged *>(&k.kind);
                        g && s.kind == segment::kind::index && ((*g)->tag == 2 || (*g)->tag == 3) &&
                        !test::get_if<test::bytes>((*g)->content))
                        return std::nullopt;
                    bool const t = (std::holds_alternative<test::short_text>(k.kind) || std::holds_alternative<std::string *>(k.kind));
                    auto const *u = std::get_if<std::uint64_t>(&k.kind);
                    auto const *n = std::get_if<test::negative>(&k.kind);
                    bool const match =
                        s.kind == segment::kind::key
                            ? t && k.text() == s.key
                            : (u && s.index >= 0 && *u == static_cast<std::uint64_t>(s.index)) ||
                                  (n && s.index < 0 && n->argument == static_cast<std::uint64_t>(-1 - s.index));
                    if (match) {
                        next.push_back(x);
                        break;
                    }
                }
            } else if (auto const *a = test::get_if<test::array>(v); a && s.kind == segment::kind::index) {
                auto const size = static_cast<std::int64_t>(a->size());
                std::int64_t const position = s.index < 0 ? s.index + size : s.index;
                if (position >= 0 && position < size)
                    next.push_back(a->at(static_cast<std::size_t>(position)));
            }
            if (next.size() == before && !nodelist)
                return std::nullopt;
            if (next.size() > limit)
                return std::nullopt;
        }
        nodes = std::move(next);
    }
    if (!nodelist)
        return nodes.front();
    return test::value{test::array(nodes.begin(), nodes.end())};
}

// The typed read gives views, and lazy gives views that hold an owner. Both are compared by their bytes, and a float by
// its bits.
template <class T>
auto comparable(T const &v)
{
    if constexpr (std::is_same_v<T, std::string_view>)
        return std::string(v);
    else if constexpr (std::is_same_v<T, std::span<std::byte const>>)
        return std::vector<std::byte>(v.begin(), v.end());
    else if constexpr (std::is_same_v<T, cbor::typed_array>)
        return std::pair{v.tag, std::vector<std::byte>(v.bytes.begin(), v.bytes.end())};
    else if constexpr (std::is_same_v<T, double>)
        return std::bit_cast<std::uint64_t>(v);
    else
        return v;
}

using step = std::variant<std::string_view, std::int64_t>;

// The typed read of a path against the lazy chain over the same steps: both give the same value or the same error, on
// every input, also where the input is not well-formed.
template <cbor::fixed_string Path, class T>
void typed_read_check(std::string const &document, std::vector<step> const &steps)
{
    auto const owner = std::make_shared<std::string const>(document);
    auto const typed = [&owner] {
        if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                      std::is_same_v<T, cbor::typed_array>)
            return cbor::at_path<Path, T>(owner);
        else
            return cbor::at_path<Path, T>(std::string_view(*owner));
    }();
    auto const root = test::lazy_of_copy(std::string(document));
    require(root.has_value());
    std::expected<cbor::lazy, cbor::error> node = *root;
    for (step const &s : steps)
        node = node.and_then([&s](cbor::lazy const &l) {
            return std::holds_alternative<std::int64_t>(s) ? test::index_select(l, std::get<std::int64_t>(s))
                                                           : l.at(std::get<std::string_view>(s));
        });
    auto const chained = node.and_then([](cbor::lazy const &l) { return l.get<T>(); });
    require(typed.has_value() == chained.has_value());
    if (!typed) {
        require(typed.error() == chained.error());
        return;
    }
    if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                  std::is_same_v<T, cbor::typed_array>)
        require(comparable(**typed) == comparable(**chained));
    else
        require(comparable(*typed) == comparable(*chained));
}

template <cbor::fixed_string Path>
void typed_reads_check(std::string const &document, std::vector<step> const &steps)
{
    typed_read_check<Path, std::int64_t>(document, steps);
    typed_read_check<Path, std::uint64_t>(document, steps);
    typed_read_check<Path, std::int8_t>(document, steps);
    typed_read_check<Path, double>(document, steps);
    typed_read_check<Path, bool>(document, steps);
    typed_read_check<Path, std::nullptr_t>(document, steps);
    typed_read_check<Path, std::string_view>(document, steps);
    typed_read_check<Path, std::span<std::byte const>>(document, steps);
    typed_read_check<Path, cbor::typed_array>(document, steps);
}

inline void path_target(std::string_view const input)
{
    std::string const whole(input);
    typed_reads_check<"$">(whole, {});
    typed_reads_check<"$[0]">(whole, {std::int64_t{0}});
    typed_reads_check<"$[1]">(whole, {std::int64_t{1}});
    typed_reads_check<"$[-1]">(whole, {std::int64_t{-1}});
    typed_reads_check<"$.a">(whole, {"a"});
    typed_reads_check<"$.a[0]">(whole, {"a", std::int64_t{0}});
    typed_reads_check<"$[0].a">(whole, {std::int64_t{0}, "a"});
    typed_reads_check<"$.a.b">(whole, {"a", "b"});
    typed_reads_check<"$[1][0]">(whole, {std::int64_t{1}, std::int64_t{0}});
    typed_reads_check<"$['a'][-1]['b']">(whole, {"a", std::int64_t{-1}, "b"});

    if (auto const any = test::lazy_of_copy(std::string(1, '\0'))) {
        (void)test::at_path(input, *any);
        (void)test::query(input, *any);
    }

    if (input.empty())
        return;
    std::size_t const split = std::min<std::size_t>(static_cast<unsigned char>(input.front()) % 64, input.size() - 1);
    std::string_view const path_bytes = input.substr(1, split);
    std::string_view const document = input.substr(1 + split);
    auto const eager = test::decoded(document);
    if (!eager)
        return;
    std::vector<std::string> keys;
    text_keys(*eager, keys, 0);
    source pin{path_bytes};
    query const q = query_from(pin, keys);
    auto const root = test::lazy_of_copy(std::string{document});
    require(root.has_value());
    auto found = test::at_path(q.text, *root);
    if (!found && found.error() == cbor::error::not_singular_query)
        found = test::query(q.text, *root);
    if (!q.valid)
        return;
    std::optional<test::value> expected;
    try {
        expected = walked(*eager, q.segments, document.size());
    } catch (undecided const &) {
        return;
    }
    require(found.has_value() == expected.has_value());
    if (found)
        require(same(*found, *expected));
}

} // namespace fuzz
