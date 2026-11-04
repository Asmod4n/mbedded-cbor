// Writes the calls that the console offers on a value: the indexes and
// keys of the value, the types that get<> reads, and the next segments of
// a JSONPath. Path segments follow the normalized paths of RFC 9535.
// https://www.rfc-editor.org/rfc/rfc9535#section-2.7
#pragma once

#include "kinds/kinds.hpp"

#include <cctype>
#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace site
{

constexpr std::string_view lazy_read_types[] = {
    "int",           "long",          "unsigned",      "std::int8_t",   "std::int16_t",
    "std::int32_t",  "std::int64_t",  "std::uint8_t",  "std::uint16_t", "std::uint32_t",
    "std::uint64_t", "std::size_t",   "double",        "bool",          "std::nullptr_t",
    "std::string_view", "std::span<std::byte const>",
};

constexpr unsigned char first_non_ascii = 0x80;

inline std::optional<std::string> console_string(std::string_view const text)
{
    std::string out = "\"";
    for (char const c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (std::iscntrl(static_cast<unsigned char>(c))) {
            return std::nullopt;
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

inline bool name_first(unsigned char const c)
{
    return std::isalpha(c) || c == '_' || c >= first_non_ascii;
}

inline bool shorthand_of(std::string_view const key)
{
    return !key.empty() && name_first(static_cast<unsigned char>(key.front())) &&
           std::ranges::all_of(key.substr(1), [](char const c) {
               return name_first(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c));
           });
}

inline std::string normalized_name(std::string_view const key)
{
    std::string out = "['";
    for (char const c : key) {
        switch (c) {
        case '\'': out += "\\'"; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (std::iscntrl(static_cast<unsigned char>(c)))
                out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
            else
                out += c;
        }
    }
    out += "']";
    return out;
}

inline std::optional<std::size_t> index_typed(std::string_view const partial, std::string_view const open)
{
    if (!partial.starts_with(open))
        return std::nullopt;
    std::string_view const digits = partial.substr(open.size());
    std::size_t n = 0;
    auto const r = std::from_chars(digits.data(), std::to_address(digits.end()), n);
    if (r.ec != std::errc{} || r.ptr == digits.data())
        return std::nullopt;
    return n;
}

template <bool Bracket>
void lazy_index_candidates(value const &self, std::string_view const partial, candidate_taken const &taken)
{
    auto const *const l = held_if<cbor::lazy>(self);
    if (l == nullptr)
        return;
    std::string_view const open = Bracket ? "[" : "at(";
    std::string_view const close = Bracket ? "]" : ")";
    auto const called_with = [&](std::string const &argument) { return std::string(open) + argument + std::string(close); };
    if (auto const elements = l->elements()) {
        std::uint64_t const count = elements->size();
        if (auto const typed = index_typed(partial, open); typed && *typed < count)
            if (!taken(called_with(std::to_string(*typed))))
                return;
        for (std::uint64_t i = 0; i < count; ++i)
            if (!taken(called_with(std::to_string(i))))
                return;
        return;
    }
    auto const entries = l->entries();
    if (!entries)
        return;
    for (auto const e : *entries) {
        if (!e)
            return;
        if (auto const text = e->first.get<std::string_view>()) {
            if (auto const quoted = console_string(**text); quoted && !taken(called_with(*quoted)))
                return;
        } else if (auto const number = e->first.get<std::int64_t>()) {
            if (!taken(called_with("cbor::key{" + std::to_string(*number) + "}")))
                return;
        }
    }
}

inline void lazy_get_candidates(value const &, std::string_view, candidate_taken const &taken)
{
    for (std::string_view const t : lazy_read_types)
        if (!taken("get<" + std::string(t) + ">()"))
            return;
}

inline bool path_children_taken(cbor::lazy const &node, std::string const &path, candidate_taken const &taken)
{
    auto const offered = [&](std::string const &p, bool const many) {
        auto const quoted = console_string(p);
        if (!quoted)
            return true;
        return taken("at_path<" + *quoted + (many ? ", cbor::nodelist>()" : ">()"));
    };
    if (auto const elements = node.elements()) {
        for (std::uint64_t i = 0; i < elements->size(); ++i)
            if (!offered(path + "[" + std::to_string(i) + "]", false))
                return false;
        return offered(path + "[*]", true);
    }
    auto const entries = node.entries();
    if (!entries)
        return true;
    for (auto const e : *entries) {
        if (!e)
            return true;
        auto const text = e->first.get<std::string_view>();
        if (!text)
            continue;
        std::string_view const key = **text;
        if (!offered(path + (shorthand_of(key) ? "." + std::string(key) : normalized_name(key)), false))
            return false;
    }
    return offered(path + "[*]", true);
}

inline std::string console_unescaped(std::string_view const text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] != '\\') {
            out += text[i];
            continue;
        }
        if (++i == text.size())
            break;
        out += text[i] == 'n' ? '\n' : text[i] == 't' ? '\t' : text[i];
    }
    return out;
}

inline void lazy_at_path_candidates(value const &self, std::string_view const partial, candidate_taken const &taken)
{
    auto const *const l = held_if<cbor::lazy>(self);
    if (l == nullptr)
        return;
    constexpr std::string_view open = "at_path<\"";
    std::string const typed = partial.starts_with(open) ? console_unescaped(partial.substr(open.size())) : "$";
    std::vector<std::pair<std::string, cbor::lazy>> nodes;
    for (std::size_t n = typed.size(); n > 0 && nodes.size() < 2; --n) {
        std::string const prefix = typed.substr(0, n);
        auto const query = cbor::jsonpath::compile<cbor::lazy>(prefix);
        if (!query)
            continue;
        auto const found = l->at_path(*query);
        if (!found)
            continue;
        nodes.emplace_back(prefix, *found);
    }
    if (nodes.empty())
        return;
    if (auto const quoted = console_string(nodes.front().first); quoted && !taken("at_path<" + *quoted + ">()"))
        return;
    for (auto const &[path, node] : nodes)
        if (!path_children_taken(node, path, taken))
            return;
}

template <class T>
void indexes_candidates(value const &self, std::string_view const partial, candidate_taken const &taken)
{
    auto const *const s = held_if<T>(self);
    if (s == nullptr)
        return;
    if (auto const typed = index_typed(partial, "["); typed && *typed < s->size())
        if (!taken("[" + std::to_string(*typed) + "]"))
            return;
    for (std::size_t i = 0; i < s->size(); ++i)
        if (!taken("[" + std::to_string(i) + "]"))
            return;
}

inline void item_get_candidates(value const &, std::string_view, candidate_taken const &taken)
{
    for (std::string_view const t : type_names)
        if (!taken("get<cbor::type::" + std::string(t) + ">()"))
            return;
}

}
