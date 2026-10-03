#pragma once

#include "common.hpp"

#include <charconv>
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

// A path in the grammar of path_compile, built from the bytes of the input: keys in both forms, indexes of
// every sign and size, wildcards and white space.
inline std::string path_from(source &in, std::vector<std::string> const &keys)
{
    std::string path = in.byte() & 1 ? "$" : "";
    for (std::size_t n = in.byte() % 7; n > 0; --n) {
        switch (in.byte() % 6) {
        case 0:
        case 1: {
            std::string key = keys.empty() || in.byte() % 4 == 0 ? in.text() : keys.at(in.byte() % keys.size());
            if (identifier_p(key) && in.byte() & 1)
                path += "." + key;
            else if (key.find('"') == std::string::npos)
                path += "[\"" + key + "\"]";
            break;
        }
        case 2: {
            std::uint8_t const form = in.byte();
            std::int64_t index = 0;
            if (form % 3 == 0)
                index = static_cast<std::int64_t>(in.number());
            else
                index = static_cast<std::int8_t>(in.byte()) % 8;
            path += "[" + std::to_string(index) + "]";
            break;
        }
        case 3:
            path += "[*]";
            break;
        case 4:
            path += " ";
            break;
        default:
            path += static_cast<char>(in.byte());
            break;
        }
    }
    return path;
}

// The steps written back as text in the bracket form, which path_compile reads as the same steps.
inline std::optional<std::string> path_text(std::span<cbor::path_step const> const steps)
{
    std::string text = "$";
    for (auto const &s : steps) {
        if (s.kind == cbor::path_step::kind::key) {
            if (s.key.find('"') != std::string_view::npos)
                return std::nullopt;
            text += "[\"" + std::string(s.key) + "\"]";
        } else if (s.kind == cbor::path_step::kind::index) {
            text += "[" + std::to_string(s.index) + "]";
        } else {
            text += "[*]";
        }
    }
    return text;
}

// The reference: the path walked over the eager value with the rules of lazy::at. A key matches a text key
// only; an index counts from the end when negative and matches an integer key of a map; tag 24 is a document
// of its own when a step goes into it; a wildcard collects the rest of the path for every element.
inline std::optional<test::value> walked(test::value const &start, std::span<cbor::path_step const> const steps)
{
    test::value v = start;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (auto const *t = std::get_if<test::tagged>(&v.kind); t && t->tag == 24) {
            auto const *b = std::get_if<test::bytes>(&t->content.at(0).kind);
            if (!b)
                return std::nullopt;
            test_host host;
            auto inner = cbor::decode<16>(host, b->b);
            if (!inner)
                return std::nullopt;
            v = *inner;
        }
        cbor::path_step const &s = steps.subspan(i).front();
        if (s.kind == cbor::path_step::kind::key) {
            auto const *m = std::get_if<test::map>(&v.kind);
            if (!m)
                return std::nullopt;
            std::optional<test::value> found;
            for (auto const &[k, x] : *m)
                if (auto const *t = std::get_if<std::string>(&k.kind); t && *t == s.key) {
                    found = x;
                    break;
                }
            if (!found)
                return std::nullopt;
            v = *found;
        } else if (s.kind == cbor::path_step::kind::index) {
            if (auto const *a = std::get_if<test::array>(&v.kind)) {
                auto const size = static_cast<std::int64_t>(a->size());
                std::int64_t const position = s.index < 0 ? s.index + size : s.index;
                if (position < 0 || position >= size)
                    return std::nullopt;
                v = a->at(static_cast<std::size_t>(position));
            } else if (auto const *m = std::get_if<test::map>(&v.kind)) {
                std::optional<test::value> found;
                for (auto const &[k, x] : *m) {
                    auto const *u = std::get_if<std::uint64_t>(&k.kind);
                    auto const *n = std::get_if<test::negative>(&k.kind);
                    if ((u && s.index >= 0 && *u == static_cast<std::uint64_t>(s.index)) ||
                        (n && s.index < 0 && n->argument == static_cast<std::uint64_t>(-1 - s.index))) {
                        found = x;
                        break;
                    }
                }
                if (!found)
                    return std::nullopt;
                v = *found;
            } else {
                return std::nullopt;
            }
        } else {
            auto const *a = std::get_if<test::array>(&v.kind);
            if (!a)
                return std::nullopt;
            test::array out;
            for (auto const &e : *a) {
                auto r = walked(e, steps.subspan(i + 1));
                if (!r)
                    return std::nullopt;
                out.push_back(std::move(*r));
            }
            return test::value{std::move(out)};
        }
    }
    return v;
}

inline void path_target(std::string_view const input)
{
    if (auto const steps = cbor::path_compile(input)) {
        if (auto const text = path_text(*steps)) {
            auto const again = cbor::path_compile(*text);
            require(again.has_value() && again->size() == steps->size());
            for (std::size_t i = 0; i < steps->size(); ++i) {
                auto const &a = steps->at(i);
                auto const &b = again->at(i);
                require(a.kind == b.kind && a.key == b.key && a.index == b.index);
            }
        }
    }

    if (input.empty())
        return;
    std::size_t const split = std::min<std::size_t>(static_cast<unsigned char>(input.front()) % 64, input.size() - 1);
    std::string_view const path_bytes = input.substr(1, split);
    std::string_view const document = input.substr(1 + split);
    test_host host;
    auto const eager = cbor::decode<16>(host, document);
    if (!eager)
        return;
    std::vector<std::string> keys;
    text_keys(*eager, keys, 0);
    source pin{path_bytes};
    std::string const path = path_from(pin, keys);
    auto const steps = cbor::path_compile(path);
    if (!steps)
        return;
    auto const root = cbor::lazy::from(std::string{document});
    require(root.has_value());
    auto const found = cbor::path_decode<16>(host, *steps, *root);
    auto const expected = walked(*eager, *steps);
    require(found.has_value() == expected.has_value());
    if (found)
        require(same(*found, *expected));
}

} // namespace fuzz
