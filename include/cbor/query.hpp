#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "binding.hpp"
#include "doc_end.hpp"
#include "error.hpp"
#include "head.hpp"
#include "inspect.hpp"
#include "lazy.hpp"
#include "shared.hpp"

namespace cbor
{

struct lazy;

template <std::size_t DepthMax = 64, class Binding>
result<typename Binding::value, error> at_path(Binding &binding, std::string_view path, lazy const &l);

template <fixed_string Path, std::size_t DepthMax>
class verify_path;

template <fixed_string Path, std::size_t DepthMax = 64, class Binding>
    requires(verify_path<Path, DepthMax>::value)
result<typename Binding::value, error> at_path(Binding &binding, lazy const &l);

class jsonpath
{
    struct selector {
        enum class kind { key, index, wildcard } kind;
        std::size_t key_at;
        std::size_t key_size;
        std::int64_t index;
    };

    struct query {
        std::vector<selector> selectors;
        std::string keys;
    };

    static constexpr std::expected<query, error> query_parse(std::string_view const text, bool const literals,
                                                             std::size_t const depth_max)
    {
        query q;
        if (text.empty() || text.front() != '$') [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t at = 1;
        auto const name_first = [](char const c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
        };
        for (;;) {
            std::size_t const segment = diagnostic_notation::blank_end(text, at);
            if (segment == text.size()) {
                if (segment != at) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                break;
            }
            at = segment;
            std::size_t const key_at = q.keys.size();
            if (text.at(at) == '.') {
                ++at;
                if (at < text.size() && text.at(at) == '*') {
                    q.selectors.push_back({selector::kind::wildcard, 0, 0, 0});
                    ++at;
                    continue;
                }
                std::size_t end = at;
                while (end < text.size() && (name_first(text.at(end)) || (end != at && diagnostic_notation::digit(text.at(end)))))
                    ++end;
                if (end == at) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (auto const r = diagnostic_notation::head_append(q.keys, major_type::text_string, end - at, diagnostic_notation::no_indicator); !r) [[unlikely]]
                    return std::unexpected(r.error());
                q.keys += text.substr(at, end - at);
                q.selectors.push_back({selector::kind::key, key_at, q.keys.size() - key_at, 0});
                at = end;
                continue;
            }
            if (text.at(at) != '[') [[unlikely]]
                return std::unexpected(error::invalid_path);
            at = diagnostic_notation::blank_end(text, at + 1);
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text.at(at);
            std::size_t end;
            if (c == '*') {
                q.selectors.push_back({selector::kind::wildcard, 0, 0, 0});
                end = at + 1;
            } else if (c == '\'' || c == '"') {
                std::string name;
                auto const next = diagnostic_notation::quoted_parse(text, at, name);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                if (auto const r = diagnostic_notation::head_append(q.keys, major_type::text_string, name.size(), diagnostic_notation::no_indicator); !r) [[unlikely]]
                    return std::unexpected(r.error());
                q.keys += name;
                q.selectors.push_back({selector::kind::key, key_at, q.keys.size() - key_at, 0});
                end = *next;
            } else {
                std::size_t digits_at = at + (c == '-' ? 1 : 0);
                std::size_t digits_end = digits_at;
                while (digits_end < text.size() && diagnostic_notation::digit(text.at(digits_end)))
                    ++digits_end;
                std::size_t const close = diagnostic_notation::blank_end(text, digits_end);
                bool const integer = digits_end != digits_at && digits_end - digits_at <= 16 && close < text.size() &&
                                     text.at(close) == ']' &&
                                     !(text.at(digits_at) == '0' && (digits_end > digits_at + 1 || c == '-'));
                if (integer) {
                    std::int64_t value = 0;
                    for (char const d : text.substr(digits_at, digits_end - digits_at))
                        value = value * 10 + (d - '0');
                    constexpr std::int64_t exact_max = (std::int64_t{1} << 53) - 1;
                    if (value > exact_max) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    q.selectors.push_back({selector::kind::index, 0, 0, c == '-' ? -value : value});
                    end = digits_end;
                } else if (literals) {
                    std::string literal;
                    auto const next = diagnostic_notation::literal_parse(text, at, literal, 0, depth_max);
                    if (!next) [[unlikely]]
                        return std::unexpected(next.error());
                    if (auto const r = diagnostic_notation::canonical_append(q.keys, literal, 0, 0, depth_max); !r) [[unlikely]]
                        return std::unexpected(r.error());
                    q.selectors.push_back({selector::kind::key, key_at, q.keys.size() - key_at, 0});
                    end = *next;
                } else [[unlikely]] {
                    return std::unexpected(error::invalid_path);
                }
            }
            at = diagnostic_notation::blank_end(text, end);
            if (at >= text.size() || text.at(at) != ']') [[unlikely]]
                return std::unexpected(error::invalid_path);
            ++at;
        }
        if (q.selectors.size() > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        return q;
    }

    template <std::size_t DepthMax>
    static result<lazy, error> key_find(lazy const &node, std::string_view key);

    static constexpr std::expected<std::size_t, error> literal_end(std::string_view const literal, std::size_t const at,
                                                                   std::size_t const depth, std::size_t const depth_max)
    {
        if (depth > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        auto const h = heads::raw_head_read(literal, at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        std::size_t next = h->at;
        switch (h->major) {
        case major_type::byte_string:
        case major_type::text_string:
            if (literal.size() - next < h->argument) [[unlikely]]
                return std::unexpected(error::too_little_data);
            return next + static_cast<std::size_t>(h->argument);
        case major_type::array:
        case major_type::map:
            for (std::uint64_t i = 0; i < (h->major == major_type::map ? 2 : 1) * h->argument; ++i) {
                auto const end = literal_end(literal, next, depth + 1, depth_max);
                if (!end) [[unlikely]]
                    return end;
                next = *end;
            }
            return next;
        case major_type::tag:
            return literal_end(literal, next, depth + 1, depth_max);
        default:
            return next;
        }
    }

    struct float_key {
        bool nan;
        std::uint64_t widened;
        double value;
    };

    static float_key float_key_of(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info == std::to_underlying(heads::simple_float_information::half_precision_float))
            return {(argument >> 10 & 0x1f) == 0x1f && (argument & 0x3ff) != 0,
                    (argument >> 15) << 63 | (argument & 0x3ff) << 42,
                    static_cast<double>(heads::float_decode_binary16(static_cast<std::uint16_t>(argument)))};
        if (info == std::to_underlying(heads::simple_float_information::single_precision_float))
            return {(argument >> 23 & 0xff) == 0xff && (argument & 0x7fffff) != 0,
                    (argument >> 31) << 63 | (argument & 0x7fffff) << 29,
                    static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(argument)))};
        return {(argument >> 52 & 0x7ff) == 0x7ff && (argument & 0xfffffffffffff) != 0,
                (argument >> 63) << 63 | (argument & 0xfffffffffffff), std::bit_cast<double>(argument)};
    }

    template <std::size_t DepthMax>
    static std::expected<std::size_t, error> document_item_end(value_sharing::document &doc, std::size_t at, std::size_t depth);

    template <std::size_t DepthMax>
    static std::expected<bool, error> key_equal(value_sharing::document &doc, std::size_t at, std::string_view literal, std::size_t literal_at,
                                                std::size_t depth);

    template <std::size_t DepthMax, class Binding>
    static result<typename Binding::value, error> query_walk(Binding &binding, std::span<selector const> selectors,
                                                             std::string_view keys, lazy const &root);

    template <fixed_string, std::size_t>
    friend class verify_path;

    template <std::size_t DepthMax, class Binding>
    friend result<typename Binding::value, error> at_path(Binding &binding, std::string_view path, lazy const &l);

    template <fixed_string Path, std::size_t DepthMax, class Binding>
        requires(verify_path<Path, DepthMax>::value)
    friend result<typename Binding::value, error> at_path(Binding &binding, lazy const &l);
};

template <fixed_string Path, std::size_t DepthMax>
class verify_path : public std::bool_constant<jsonpath::query_parse(Path.view(), true, DepthMax).has_value()>
{
};

template <std::size_t DepthMax>
std::expected<std::size_t, error> jsonpath::document_item_end(value_sharing::document &doc, std::size_t const at, std::size_t const depth)
{
    heads::decoder d{doc.encoded.substr(at)};
    if (auto const r = well_formedness::item_skip<DepthMax>(d, doc, depth); !r) [[unlikely]]
        return std::unexpected(r.error());
    return doc.encoded.size() - d.encoded.size();
}

template <std::size_t DepthMax>
std::expected<bool, error> jsonpath::key_equal(value_sharing::document &doc, std::size_t const start, std::string_view const literal,
                                               std::size_t const literal_at, std::size_t const depth)
{
    if (depth > DepthMax) [[unlikely]]
        return std::unexpected(error::nesting_depth_exceeded);
    auto const at = value_sharing::shared_resolve<DepthMax>(doc, start);
    if (!at) [[unlikely]]
        return std::unexpected(at.error());
    auto const h = heads::raw_head_read(doc.encoded, *at);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    auto const l = heads::raw_head_read(literal, literal_at);
    if (!l) [[unlikely]]
        return std::unexpected(l.error());
    if (h->info == std::to_underlying(heads::additional_information::indefinite_length)) [[unlikely]]
        return std::unexpected(error::indefinite_length);
    if (h->major != l->major)
        return false;
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return h->argument == l->argument;
    case major_type::byte_string:
    case major_type::text_string:
        if (doc.encoded.size() - h->at < h->argument) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return h->argument == l->argument &&
               doc.encoded.substr(h->at, static_cast<std::size_t>(h->argument)) ==
                   literal.substr(l->at, static_cast<std::size_t>(l->argument));
    case major_type::array: {
        if (h->argument != l->argument)
            return false;
        std::size_t d = h->at;
        std::size_t k = l->at;
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const equal = key_equal<DepthMax>(doc, d, literal, k, depth + 1);
            if (!equal || !*equal)
                return equal;
            auto const d_end = document_item_end<DepthMax>(doc, d, depth + 1);
            if (!d_end) [[unlikely]]
                return std::unexpected(d_end.error());
            auto const k_end = literal_end(literal, k, depth + 1, DepthMax);
            if (!k_end) [[unlikely]]
                return std::unexpected(k_end.error());
            d = *d_end;
            k = *k_end;
        }
        return true;
    }
    case major_type::map: {
        if (h->argument != l->argument)
            return false;
        std::size_t d = h->at;
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const value_at = document_item_end<DepthMax>(doc, d, depth + 1);
            if (!value_at) [[unlikely]]
                return std::unexpected(value_at.error());
            auto const pair_end = document_item_end<DepthMax>(doc, *value_at, depth + 1);
            if (!pair_end) [[unlikely]]
                return std::unexpected(pair_end.error());
            bool paired = false;
            std::size_t k = l->at;
            for (std::uint64_t j = 0; j < l->argument && !paired; ++j) {
                auto const k_value = literal_end(literal, k, depth + 1, DepthMax);
                if (!k_value) [[unlikely]]
                    return std::unexpected(k_value.error());
                auto const k_end = literal_end(literal, *k_value, depth + 1, DepthMax);
                if (!k_end) [[unlikely]]
                    return std::unexpected(k_end.error());
                auto const key_same = key_equal<DepthMax>(doc, d, literal, k, depth + 1);
                if (!key_same) [[unlikely]]
                    return key_same;
                if (*key_same) {
                    for (std::size_t earlier = h->at; earlier < d;) {
                        auto const twin = key_equal<DepthMax>(doc, earlier, literal, k, depth + 1);
                        if (!twin) [[unlikely]]
                            return twin;
                        if (*twin) [[unlikely]]
                            return std::unexpected(error::duplicate_key);
                        auto const earlier_value = document_item_end<DepthMax>(doc, earlier, depth + 1);
                        if (!earlier_value) [[unlikely]]
                            return std::unexpected(earlier_value.error());
                        auto const earlier_end = document_item_end<DepthMax>(doc, *earlier_value, depth + 1);
                        if (!earlier_end) [[unlikely]]
                            return std::unexpected(earlier_end.error());
                        earlier = *earlier_end;
                    }
                    auto const value_same = key_equal<DepthMax>(doc, *value_at, literal, *k_value, depth + 1);
                    if (!value_same) [[unlikely]]
                        return value_same;
                    paired = *value_same;
                }
                k = *k_end;
            }
            if (!paired)
                return false;
            d = *pair_end;
        }
        return true;
    }
    case major_type::tag:
        if (h->argument != l->argument)
            return false;
        return key_equal<DepthMax>(doc, h->at, literal, l->at, depth + 1);
    default:
        break;
    }
    constexpr std::uint8_t half = std::to_underlying(heads::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice = std::to_underlying(heads::simple_float_information::double_precision_float);
    bool const h_float = h->info >= half && h->info <= twice;
    bool const l_float = l->info >= half && l->info <= twice;
    if (h_float != l_float)
        return false;
    if (!h_float)
        return h->argument == l->argument;
    float_key const a = float_key_of(h->info, h->argument);
    float_key const b = float_key_of(l->info, l->argument);
    if (a.nan || b.nan)
        return a.nan && b.nan && a.widened == b.widened;
    return a.value == b.value;
}

template <std::size_t DepthMax>
result<lazy, error> jsonpath::key_find(lazy const &node, std::string_view const key)
{
    auto const found = value_sharing::container_resolve(node.document, node.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    heads::decoder text_key{key};
    auto const literal = text_key.head_decode();
    bool const text = literal && literal->major == major_type::text_string;
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        std::size_t const start = source->encoded.size() - d.encoded.size();
        heads::decoder probe = d;
        for (;;) {
            heads::decoder look = probe;
            auto const k = look.head_decode();
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            if (k->major == major_type::tag && k->argument == std::to_underlying(heads::tag_number::shareable)) {
                probe = look;
                continue;
            }
            if (k->major == major_type::tag && k->argument == std::to_underlying(heads::tag_number::sharedref)) {
                auto const n = look.head_decode();
                if (!n) [[unlikely]]
                    return std::unexpected(n.error());
                if (n->major != major_type::unsigned_integer) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                if (n->argument >= source->marks.size()) [[unlikely]]
                    return std::unexpected(error::sharedref_index_not_marked);
                std::size_t const marked = source->marks.at(static_cast<std::size_t>(n->argument));
                if (marked >= start) [[unlikely]]
                    return std::unexpected(error::sharedref_not_complete);
                probe = heads::decoder{source->encoded.substr(marked)};
            }
            break;
        }
        bool match = false;
        heads::decoder look = probe;
        auto const k = look.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        if (text && k->major == major_type::text_string) {
            auto const content = look.byte_string_decode(k->argument);
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            match = *content == text_key.encoded;
        }
        if (auto const r = well_formedness::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (!text) {
            auto const equal = key_equal<DepthMax>(*source, start, key, 0, 0);
            if (!equal) [[unlikely]]
                return std::unexpected(equal.error());
            match = *equal;
        }
        if (match)
            return lazy{source, source->encoded.size() - d.encoded.size()};
        if (auto const r = well_formedness::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}

template <std::size_t DepthMax, class Binding>
result<typename Binding::value, error> jsonpath::query_walk(Binding &binding, std::span<selector const> const selectors,
                                                            std::string_view const keys, lazy const &root)
{
    if (!root.document) [[unlikely]]
        throw std::logic_error("cbor::at_path: the lazy holds no document");
    bool const nodelist =
        std::ranges::any_of(selectors, [](selector const &s) { return s.kind == selector::kind::wildcard; });
    std::size_t const limit = root.document->encoded.size();
    if (!nodelist) {
        lazy node = root;
        for (selector const &s : selectors) {
            auto const child = s.kind == selector::kind::index
                                   ? node.at<DepthMax>(s.index)
                                   : key_find<DepthMax>(node, keys.substr(s.key_at, s.key_size));
            if (!child) [[unlikely]]
                return std::unexpected(child.error());
            if (limit == 0) [[unlikely]]
                return std::unexpected(error::nodelist_too_long);
            node = *child;
        }
        auto value = lazy_decode<DepthMax>(binding, node);
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        return std::move(*value);
    }
    std::vector<lazy> nodes{root};
    std::vector<lazy> next;
    for (selector const &s : selectors) {
        next.clear();
        for (lazy const &node : nodes) {
            error failure{};
            if (s.kind == selector::kind::wildcard) {
                auto const found = value_sharing::container_resolve(node.document, node.offset);
                if (!found) [[unlikely]]
                    return std::unexpected(found.error());
                if (found->h.major == major_type::array) {
                    auto const elements = node.elements<DepthMax>();
                    if (!elements) [[unlikely]]
                        return std::unexpected(elements.error());
                    for (auto const element : *elements) {
                        if (!element) [[unlikely]]
                            return std::unexpected(element.error());
                        if (next.size() == limit) [[unlikely]]
                            return std::unexpected(error::nodelist_too_long);
                        next.push_back(*element);
                    }
                } else if (found->h.major == major_type::map) {
                    auto const entries = node.entries<DepthMax>();
                    if (!entries) [[unlikely]]
                        return std::unexpected(entries.error());
                    for (auto const entry : *entries) {
                        if (!entry) [[unlikely]]
                            return std::unexpected(entry.error());
                        if (next.size() == limit) [[unlikely]]
                            return std::unexpected(error::nodelist_too_long);
                        next.push_back(entry->second);
                    }
                } else {
                    failure = error::not_indexable;
                }
            } else {
                auto const child = s.kind == selector::kind::index
                                       ? node.at<DepthMax>(s.index)
                                       : key_find<DepthMax>(node, keys.substr(s.key_at, s.key_size));
                if (child) {
                    if (next.size() == limit) [[unlikely]]
                        return std::unexpected(error::nodelist_too_long);
                    next.push_back(*child);
                } else {
                    failure = child.error();
                }
            }
            if (failure == error{})
                continue;
            if (failure != error::not_indexable && failure != error::index_out_of_bounds &&
                failure != error::key_not_found) [[unlikely]]
                return std::unexpected(failure);
        }
        std::swap(nodes, next);
    }
    auto array = binding.array_decode(nodes.size());
    for (lazy const &node : nodes) {
        auto value = lazy_decode<DepthMax>(binding, node);
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        array = binding.array_append(std::move(array), std::move(*value));
    }
    return array;
}

template <std::size_t DepthMax, class Binding>
result<typename Binding::value, error> at_path(Binding &binding, std::string_view const path, lazy const &l)
{
    auto const q = jsonpath::query_parse(path, false, DepthMax);
    if (!q) [[unlikely]]
        return std::unexpected(q.error());
    return jsonpath::query_walk<DepthMax>(binding, q->selectors, q->keys, l);
}

template <fixed_string Path, std::size_t DepthMax, class Binding>
    requires(verify_path<Path, DepthMax>::value)
result<typename Binding::value, error> at_path(Binding &binding, lazy const &l)
{
    constexpr std::size_t count = jsonpath::query_parse(Path.view(), true, DepthMax)->selectors.size();
    constexpr std::size_t size = jsonpath::query_parse(Path.view(), true, DepthMax)->keys.size();
    constexpr auto compiled = [] {
        auto const q = *jsonpath::query_parse(Path.view(), true, DepthMax);
        std::pair<std::array<jsonpath::selector, count>, std::array<char, size>> c{};
        std::ranges::copy(q.selectors, c.first.begin());
        std::ranges::copy(q.keys, c.second.begin());
        return c;
    }();
    return jsonpath::query_walk<DepthMax>(binding, std::span<jsonpath::selector const>(compiled.first),
                                          std::string_view(compiled.second.data(), size), l);
}

}
