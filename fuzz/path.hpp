#pragma once

#include "common.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fuzz
{

// Every text key of the document, so a generated path names keys that exist; a key made of random bytes
// would almost never match.
inline void text_keys(test::value const &v, std::vector<std::string> &keys, int const depth)
{
    if (depth > 16)
        return;
    if (auto const *m = std::get_if<test::map>(&v.kind)) {
        for (auto const &[k, x] : *m) {
            if (auto const *t = std::get_if<std::string>(&k.kind))
                keys.push_back(*t);
            text_keys(x, keys, depth + 1);
        }
    } else if (auto const *a = std::get_if<test::array>(&v.kind)) {
        for (auto const &e : *a)
            text_keys(e, keys, depth + 1);
    } else if (auto const *t = std::get_if<test::tagged>(&v.kind)) {
        for (auto const &e : t->content)
            text_keys(e, keys, depth + 1);
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

// The reference decodes a tag 24 document whole, and lazy reads it only up to the item a step needs. Where the
// whole document does not decode, the reference cannot say what lazy finds.
struct undecided {};

// The reference: RFC 9535 2.1.2 over the eager value. Each segment maps the nodelist to the children it selects.
// A name selects a value under an equal text key; an index counts from the end when negative and selects a
// value under an equal integer key of a map; a wildcard selects every element or every value. Without a
// wildcard a missing child is an error; with one it adds no node. Tag 24 is a document of its own when a
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
            if (auto const *t = std::get_if<test::tagged>(&v.kind); t && t->tag == 24) {
                auto const *b = std::get_if<test::bytes>(&t->content.at(0).kind);
                if (!b)
                    return std::nullopt;
                test_binding binding;
                auto inner = cbor::decode<16>(binding, b->b);
                if (!inner)
                    throw undecided{};
                v = *inner;
            }
            std::size_t const before = next.size();
            if (s.kind == segment::kind::wildcard) {
                if (auto const *a = std::get_if<test::array>(&v.kind))
                    next.insert(next.end(), a->begin(), a->end());
                else if (auto const *m = std::get_if<test::map>(&v.kind))
                    for (auto const &[k, x] : *m)
                        next.push_back(x);
                else if (!nodelist)
                    return std::nullopt;
                if (next.size() > limit)
                    return std::nullopt;
                continue;
            }
            if (auto const *m = std::get_if<test::map>(&v.kind)) {
                for (auto const &[k, x] : *m) {
                    auto const *t = std::get_if<std::string>(&k.kind);
                    auto const *u = std::get_if<std::uint64_t>(&k.kind);
                    auto const *n = std::get_if<test::negative>(&k.kind);
                    bool const match =
                        s.kind == segment::kind::key
                            ? t && *t == s.key
                            : (u && s.index >= 0 && *u == static_cast<std::uint64_t>(s.index)) ||
                                  (n && s.index < 0 && n->argument == static_cast<std::uint64_t>(-1 - s.index));
                    if (match) {
                        next.push_back(x);
                        break;
                    }
                }
            } else if (auto const *a = std::get_if<test::array>(&v.kind); a && s.kind == segment::kind::index) {
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

inline void path_target(std::string_view const input)
{
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
    auto const eager = cbor::decode<16>(binding, document);
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
