#pragma once

#include "common.hpp"

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
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
            if (test::test_binding{}.kind_of(k) == cbor::kind::text_string)
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
            q.text += identifier_p(key) && in.byte() & 1 ? "." + key : "[" + quoted(key) + "]";
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

// The reference: RFC 9535 2.1.2 over the eager value. Each segment maps the nodelist to the children it selects.
// A name selects a value under an equal text key; an index counts from the end when negative and selects a
// value under an equal integer key of a map; a wildcard selects every element or every value. Without a
// wildcard a missing child is an error; with one it adds no node. Tag 24 is a top-level item of its own when a
// segment goes into it. A nodelist longer than the message is an error.
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
            if (auto const *t = test::get_if<test::tagged>(v); t && t->tag == 24) {
                auto const *b = test::get_if<test::bytes>(t->content);
                if (!b)
                    return std::nullopt;
                test_binding binding;
                auto inner = cbor::lazy_decode<16>(binding, *cbor::decode<16>(b->b));
                if (!inner)
                    throw undecided{};
                v = *inner;
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
                    bool const t = test::test_binding{}.kind_of(k) == cbor::kind::text_string;
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
    auto const typed = [&document] {
        if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                      std::is_same_v<T, cbor::typed_array>)
            return cbor::at_path<Path, T, 16>(document);
        else
            return cbor::at_path<Path, T, 16>(std::string_view(document));
    }();
    auto const root = cbor::lazy::from(std::string(document));
    require(root.has_value());
    std::expected<cbor::lazy, cbor::error> node = *root;
    for (step const &s : steps)
        node = node.and_then([&s](cbor::lazy const &l) {
            return std::holds_alternative<std::int64_t>(s) ? l.at<16>(std::get<std::int64_t>(s)) : l.at<16>(std::get<std::string_view>(s));
        });
    auto const chained = node.and_then([](cbor::lazy const &l) { return l.get<T>(); });
    require(typed.has_value() == chained.has_value());
    if (!typed) {
        require(typed.error() == chained.error());
        return;
    }
    if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                  std::is_same_v<T, cbor::typed_array>)
        require(comparable(*typed) == comparable(**chained));
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

    if (auto const any = cbor::lazy::from(std::string(1, '\0'))) {
        test_binding any_binding;
        (void)cbor::at_path<16>(any_binding, input, *any);
    }

    if (input.empty())
        return;
    std::size_t const split = std::min<std::size_t>(static_cast<unsigned char>(input.front()) % 64, input.size() - 1);
    std::string_view const path_bytes = input.substr(1, split);
    std::string_view const document = input.substr(1 + split);
    test_binding binding;
    auto const eager = cbor::lazy_decode<16>(binding, *cbor::decode<16>(document));
    if (!eager)
        return;
    std::vector<std::string> keys;
    text_keys(*eager, keys, 0);
    source pin{path_bytes};
    query const q = query_from(pin, keys);
    auto const root = cbor::lazy::from(std::string{document});
    require(root.has_value());
    auto const found = cbor::at_path<16>(binding, q.text, *root);
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
