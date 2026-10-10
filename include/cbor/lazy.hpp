#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "binding.hpp"
#include "decode.hpp"
#include "item_size.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "owning_ref.hpp"
#include "shared.hpp"

namespace cbor
{

inline lazy_elements::iterator::value_type lazy_elements::iterator::operator*() const
{
    if (failure != error{}) [[unlikely]]
        return std::unexpected(failure);
    return lazy{top_level, offset};
}

inline bool lazy_elements::iterator::operator==(iterator const &other) const
{
    return offset == other.offset && left == other.left && failure == other.failure &&
           (top_level == other.top_level ||
            (top_level && other.top_level && top_level->encoded.data() == other.top_level->encoded.data()));
}

inline lazy_elements::iterator &lazy_elements::iterator::operator++()
{
    if (failure != error{}) [[unlikely]] {
        --left;
        return *this;
    }
    heads::decoder d{std::string_view(std::span(top_level->encoded).subspan(offset))};
    well_formedness::no_marks none;
    if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]] {
        failure = r.error();
        --left;
        return *this;
    }
    offset = top_level->encoded.size() - d.encoded.size();
    --left;
    return *this;
}

inline void lazy_entries::iterator::value_find()
{
    if (left == 0)
        return;
    heads::decoder d{std::string_view(std::span(top_level->encoded).subspan(key))};
    well_formedness::no_marks none;
    if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]] {
        failure = r.error();
        return;
    }
    value = top_level->encoded.size() - d.encoded.size();
}

inline lazy_entries::iterator::value_type lazy_entries::iterator::operator*() const
{
    if (failure != error{}) [[unlikely]]
        return std::unexpected(failure);
    return std::pair{lazy{top_level, key}, lazy{top_level, value}};
}

inline bool lazy_entries::iterator::operator==(iterator const &other) const
{
    return key == other.key && value == other.value && left == other.left && failure == other.failure &&
           (top_level == other.top_level ||
            (top_level && other.top_level && top_level->encoded.data() == other.top_level->encoded.data()));
}

inline lazy_entries::iterator &lazy_entries::iterator::operator++()
{
    if (failure != error{}) [[unlikely]] {
        --left;
        return *this;
    }
    heads::decoder d{std::string_view(std::span(top_level->encoded).subspan(value))};
    well_formedness::no_marks none;
    if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]] {
        failure = r.error();
        --left;
        return *this;
    }
    key = top_level->encoded.size() - d.encoded.size();
    --left;
    value_find();
    return *this;
}

inline std::expected<lazy, error> lazy::from(std::shared_ptr<void const> owner, std::string_view const encoded)
{
    validity::throw_logic_error_if_empty(owner,
                                         "cbor::lazy::from: the owner of the encoded data item is empty");
    if (auto const r = validity::check_input_bytes(encoded.size()); !r) [[unlikely]]
        return std::unexpected(r.error());
    std::string_view const content = heads::self_described_cbor_content(encoded);
    return lazy{
        std::make_shared<value_sharing::top_level_item>(std::move(owner), content, std::vector<lazy>{}, 0), 0};
}

inline std::expected<lazy, error> lazy::from(std::shared_ptr<std::string const> encoded)
{
    validity::throw_logic_error_if_null(encoded, "cbor::lazy::from: the encoded data item is null");
    std::string_view const view = *encoded;
    return from(std::move(encoded), view);
}

template <std::same_as<std::string> Encoded>
std::expected<lazy, error> lazy::from(Encoded &&encoded)
{
    return from(std::make_shared<std::string const>(std::move(encoded)));
}

inline std::expected<lazy, error> lazy::from(std::string_view const encoded)
{
    return from(std::make_shared<std::string const>(encoded));
}

template <class Key, class Entries>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<typename Entries::iterator, error> value_sharing::key_find(resolved found, Key const key)
{
    if (found.h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    std::size_t const first = found.source->encoded.size() - found.d.encoded.size();
    typename Entries::iterator it(std::move(found.source), first,
                                  std::min<std::uint64_t>(found.h.argument, found.d.encoded.size()));
    auto found_at = key_find<false>(std::move(it), std::default_sentinel, key);
    if (!found_at) [[unlikely]]
        return std::unexpected(found_at.error());
    return std::move(found_at->first);
}

template <bool Sorted, class Iterator, class Last, class Key>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<std::pair<Iterator, bool>, error> value_sharing::key_find(Iterator it, Last const last, Key const key)
{
    std::array<char, heads::initial_byte_size + sizeof(std::uint64_t)> head{};
    std::string_view wanted_head;
    std::string_view wanted_content;
    bool plain = Sorted;
    if constexpr (Sorted) {
        std::size_t size = 0;
        if constexpr (std::same_as<Key, std::string_view>) {
            size = heads::head_write(head, 0, major_type::text_string, key.size());
            wanted_content = key;
        } else {
            size = heads::head_write(head, 0, key < 0 ? major_type::negative_integer : major_type::unsigned_integer,
                                     key < 0 ? static_cast<std::uint64_t>(-1 - key) : static_cast<std::uint64_t>(key));
        }
        wanted_head = std::string_view(head.data(), size);
    }
    for (; it != last; ++it) {
        if (it == std::default_sentinel) [[unlikely]] {
            if constexpr (!std::same_as<Last, std::default_sentinel_t>)
                validity::throw_logic_error("cbor::lazy: the last iterator is not a later pair of the same map");
            break;
        }
        if (it.failure != error{}) [[unlikely]]
            return std::unexpected(it.failure);
        auto const key_at = shared_resolve(*it.top_level, it.key);
        if (!key_at) [[unlikely]]
            return std::unexpected(key_at.error());
        heads::decoder probe{std::string_view(std::span(it.top_level->encoded).subspan(*key_at))};
        auto const k = probe.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        bool equal = false;
        if constexpr (std::same_as<Key, std::string_view>) {
            if (k->major == major_type::text_string) {
                auto const content = probe.byte_string_decode(k->argument);
                if (!content) [[unlikely]]
                    return std::unexpected(content.error());
                equal = *content == key;
            }
        } else {
            equal = (k->major == major_type::unsigned_integer && key >= 0 && k->argument == static_cast<std::uint64_t>(key)) ||
                    (k->major == major_type::negative_integer && key < 0 && k->argument == static_cast<std::uint64_t>(-1 - key));
        }
        if (equal)
            return std::pair{std::move(it), true};
        if constexpr (Sorted) {
            plain = plain && *key_at == it.key && k->major != major_type::tag;
            if (plain) {
                std::string_view const read = std::string_view(std::span(it.top_level->encoded).subspan(it.key, it.value - it.key));
                auto order = read.substr(0, wanted_head.size()) <=> wanted_head;
                if (order == 0)
                    order = read.substr(wanted_head.size()) <=> wanted_content;
                if (order > 0)
                    return std::pair{std::move(it), false};
            }
        }
    }
    return std::pair{std::move(it), false};
}

inline std::expected<lazy_entries::iterator, error> lazy::find(std::string_view const key) const
{
    auto found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return value_sharing::key_find(std::move(*found), key);
}

inline std::expected<lazy_entries::iterator, error> lazy::find(std::int64_t const key) const
{
    auto found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return value_sharing::key_find(std::move(*found), key);
}

inline std::expected<bool, error> lazy::contains(std::string_view const key) const
{
    return find(key).transform([](lazy_entries::iterator const &it) { return it != std::default_sentinel; });
}

inline std::expected<bool, error> lazy::contains(std::int64_t const key) const
{
    return find(key).transform([](lazy_entries::iterator const &it) { return it != std::default_sentinel; });
}

template <class Key, class Entries>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<std::size_t, error> value_sharing::key_count(std::expected<typename Entries::iterator, error> found, Key const key)
{
    std::size_t n = 0;
    while (found && *found != std::default_sentinel) {
        ++n;
        auto next = key_find<false>(std::ranges::next(std::move(*found)), std::default_sentinel, key);
        if (!next) [[unlikely]]
            return std::unexpected(next.error());
        found = std::move(next->first);
    }
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return n;
}

inline std::expected<std::size_t, error> lazy::count(std::string_view const key) const
{
    return value_sharing::key_count(find(key), key);
}

inline std::expected<std::size_t, error> lazy::count(std::int64_t const key) const
{
    return value_sharing::key_count(find(key), key);
}

inline std::expected<std::uint64_t, error> lazy::size() const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (found->h.major != major_type::array && found->h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return found->h.argument;
}

inline std::expected<bool, error> lazy::empty() const
{
    return size().transform([](std::uint64_t const n) { return n == 0; });
}

template <class Last>
    requires(std::same_as<Last, lazy_entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
std::expected<lazy_entries::iterator, error>
lazy::find(lazy_entries::iterator first, Last const last, std::string_view const key)
{
    auto found = value_sharing::key_find<false>(std::move(first), last, key);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return std::move(found->first);
}

template <class Last>
    requires(std::same_as<Last, lazy_entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
std::expected<lazy_entries::iterator, error>
lazy::find(lazy_entries::iterator first, Last const last, std::int64_t const key)
{
    auto found = value_sharing::key_find<false>(std::move(first), last, key);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return std::move(found->first);
}

template <class Last, class Entries>
    requires(std::same_as<Last, typename Entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
std::expected<std::ranges::subrange<typename Entries::iterator>, error>
lazy::equal_range(lazy_entries::iterator first, Last const last, std::string_view const key)
{
    auto found = value_sharing::key_find<true>(std::move(first), last, key);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto &[at, equal] = *found;
    if (!equal)
        return std::ranges::subrange{at, at};
    auto after = std::ranges::next(at);
    if (after.failure != error{}) [[unlikely]]
        return std::unexpected(after.failure);
    return std::ranges::subrange{std::move(at), std::move(after)};
}

template <class Last, class Entries>
    requires(std::same_as<Last, typename Entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
std::expected<std::ranges::subrange<typename Entries::iterator>, error>
lazy::equal_range(lazy_entries::iterator first, Last const last, std::int64_t const key)
{
    auto found = value_sharing::key_find<true>(std::move(first), last, key);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto &[at, equal] = *found;
    if (!equal)
        return std::ranges::subrange{at, at};
    auto after = std::ranges::next(at);
    if (after.failure != error{}) [[unlikely]]
        return std::unexpected(after.failure);
    return std::ranges::subrange{std::move(at), std::move(after)};
}

template <class Iterator>
std::expected<lazy, error> value_sharing::value_of(std::expected<Iterator, error> found)
{
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (*found == std::default_sentinel) [[unlikely]]
        return std::unexpected(error::key_not_found);
    return lazy{std::move(found->top_level), found->value};
}

inline std::expected<lazy, error> lazy::at(std::string_view const key) const
{
    return value_sharing::value_of(find(key));
}

inline std::expected<lazy, error> lazy::at(key const k) const
{
    if (k.text != nullptr)
        return at(std::string_view(k.text));
    return value_sharing::value_of(find(k.number));
}

inline std::expected<lazy, error> lazy::at(std::size_t const index) const
{
    auto found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto &[source, h, d] = *found;
    if (h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    auto const position = validity::check_index(index, h.argument);
    if (!position) [[unlikely]]
        return std::unexpected(position.error());
    well_formedness::no_marks none;
    for (std::uint64_t i = 0; i < *position; ++i)
        if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
            return std::unexpected(r.error());
    std::size_t const element = source->encoded.size() - d.encoded.size();
    return lazy{source, element};
}

template <class T>
    requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
             std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value> ||
                 std::is_same_v<T, std::string_view> ||
             std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
std::expected<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                               std::is_same_v<T, typed_array>,
                           owning_ref<T>, T>, error> lazy::get() const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if constexpr (std::integral<T> && !std::is_same_v<T, bool>) {
        bool negative = h.major == major_type::negative_integer;
        std::uint64_t argument = h.argument;
        if (h.major == major_type::tag &&
            (h.argument == std::to_underlying(rfc8949::tag_number::unsigned_bignum) ||
             h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum))) {
            negative = h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
            auto const content = value_sharing::shared_resolve(*source, source->encoded.size() - d.encoded.size());
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            d = heads::decoder{std::string_view(std::span(source->encoded).subspan(*content))};
            auto const r = d.head_decode();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (error const c = validity::check_tag_content(h.argument, r->major, r->info).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = d.byte_string_decode(r->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            std::string_view const magnitude = heads::magnitude_without_leading_zeros(*bytes);
            if (error const c =
                    validity::check_magnitude_size(magnitude.size(), sizeof(std::uint64_t)).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            argument = heads::magnitude_value(magnitude);
        } else if (h.major != major_type::unsigned_integer && !negative) [[unlikely]] {
            return std::unexpected(error::incorrect_type);
        }
        if (error const c = validity::check_number_range<T>(negative, argument).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        T const magnitude = static_cast<T>(argument);
        return static_cast<T>(negative ? ~magnitude : magnitude);
    } else if constexpr (std::is_same_v<T, double>) {
        if (h.major != major_type::simple_float) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        switch (static_cast<rfc8949::simple_float_information>(h.info)) {
        case rfc8949::simple_float_information::half_precision_float:
        case rfc8949::simple_float_information::single_precision_float:
        case rfc8949::simple_float_information::double_precision_float:
            return heads::float_decode(h.info, h.argument);
        [[unlikely]] default:
            return std::unexpected(error::incorrect_type);
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (!heads::is_boolean(h)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return h.info == std::to_underlying(simple_value::true_value);
    } else if constexpr (std::is_same_v<T, simple_value>) {
        if (!heads::is_simple_value(h)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (error const c = validity::check_simple_value(h.info, h.argument).error_or(error{}); c != error{})
            [[unlikely]]
            return std::unexpected(c);
        return static_cast<simple_value>(h.argument);
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
        if (!heads::is_null(h)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return nullptr;
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        if (h.major != major_type::text_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const text = d.byte_string_decode(h.argument);
        if (!text) [[unlikely]]
            return std::unexpected(text.error());
        return owning_ref<T>(source->owner, *text);
    } else if constexpr (std::is_same_v<T, typed_array>) {
        if (h.major != major_type::tag) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (error const r = validity::typed_array_check(h.argument, 0).error_or(error{}); r != error{})
            [[unlikely]]
            return std::unexpected(r);
        auto const content = value_sharing::shared_resolve(*source, source->encoded.size() - d.encoded.size());
        if (!content) [[unlikely]]
            return std::unexpected(content.error());
        d = heads::decoder{std::string_view(std::span(source->encoded).subspan(*content))};
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (error const c = validity::check_tag_content(h.argument, r->major, r->info).error_or(error{}); c != error{})
            [[unlikely]]
            return std::unexpected(c);
        auto const bytes = d.byte_string_decode(r->argument);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        if (error const c = validity::typed_array_check(h.argument, bytes->size()).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        return owning_ref<T>(source->owner, typed_array{h.argument, std::as_bytes(std::span(*bytes))});
    } else {
        if (h.major != major_type::byte_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const bytes = d.byte_string_decode(h.argument);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        return owning_ref<T>(source->owner, std::as_bytes(std::span(*bytes)));
    }
}

inline std::expected<lazy_elements, error> lazy::elements() const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    if (auto const r = validity::check_definite_length(h.major, h.info); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const r = validity::check_pending_items(h.argument, d.encoded.size()); !r) [[unlikely]]
        return std::unexpected(r.error());
    return lazy_elements(source, source->encoded.size() - d.encoded.size(), h.argument);
}

inline std::expected<lazy_entries, error> lazy::entries() const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    if (auto const r = validity::check_definite_length(h.major, h.info); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const r = validity::check_pending_items(h.argument, d.encoded.size()); !r) [[unlikely]]
        return std::unexpected(r.error());
    return lazy_entries(source, source->encoded.size() - d.encoded.size(), h.argument);
}

inline std::expected<std::pair<item *, std::size_t>, error>
value_sharing::item_decode(decoded_items &decoded, std::size_t const at, std::size_t const depth,
                           std::size_t const depth_max)
{
    if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    top_level_item &top_level = *decoded.top_level;
    auto const h = heads::raw_head_read(top_level.encoded, at);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    if (error const r = validity::check_definite_length(h->major, h->info).error_or(error{}); r != error{})
        [[unlikely]]
        return std::unexpected(r);
    if (h->major == major_type::tag && h->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
        return item_decode(decoded, h->at, depth + 1, depth_max);
    }
    if (h->major == major_type::tag && h->argument == std::to_underlying(rfc8949::tag_number::sharedref)) {
        heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(h->at))};
        auto const found = top_level_item::sharedref_decode(d, at, at, top_level.sharedrefs_read());
        if (!found) [[unlikely]]
            return std::unexpected(found.error());
        auto const target = item_resolve(decoded, found->offset);
        if (!target) [[unlikely]]
            return std::unexpected(target.error());
        if (std::holds_alternative<lazy>((*target)->content)) {
            auto const built = item_decode(decoded, found->offset, depth, depth_max);
            if (!built) [[unlikely]]
                return std::unexpected(built.error());
        }
        return std::pair{*target, top_level.encoded.size() - d.encoded.size()};
    }
    auto const e = decoded.entry(at);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    item *const node = *e;
    if (!std::holds_alternative<lazy>(node->content)) {
        if (h->major == major_type::array || h->major == major_type::map)
            return std::pair{node, h->at + std::get<std::span<std::byte const>>(node->content).size()};
        heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(at))};
        well_formedness::no_marks none;
        if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
            return std::unexpected(r.error());
        return std::pair{node, top_level.encoded.size() - d.encoded.size()};
    }
    std::size_t const left = top_level.encoded.size() - h->at;
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        node->content = std::monostate{};
        return std::pair{node, h->at};
    case major_type::byte_string:
    case major_type::text_string: {
        if (h->argument > left) [[unlikely]]
            return std::unexpected(error::too_little_data);
        std::string_view const string{std::span(top_level.encoded).subspan(h->at, h->argument)};
        if (h->major == major_type::text_string)
            node->content = string;
        else
            node->content = std::as_bytes(std::span(string));
        return std::pair{node, h->at + string.size()};
    }
    case major_type::array:
    case major_type::map: {
        heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(at))};
        well_formedness::no_marks none;
        if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::size_t const end = top_level.encoded.size() - d.encoded.size();
        node->content = std::as_bytes(std::span(top_level.encoded).subspan(h->at, end - h->at));
        return std::pair{node, end};
    }
    case major_type::tag: {
        node->content = static_cast<item const *>(nullptr);
        auto const content = item_decode(decoded, h->at, depth + 1, depth_max);
        if (!content) [[unlikely]] {
            node->content = lazy{{}, at};
            return std::unexpected(content.error());
        }
        if (error const c = validity::check_tag_content(
                                h->argument, top_level.encoded, h->at,
                                [&top_level]() -> auto const & { return top_level.sharedrefs_read(); }, &lazy::offset)
                                .error_or(error{});
            c != error{}) [[unlikely]] {
            node->content = lazy{{}, at};
            return std::unexpected(c);
        }
        node->content = static_cast<item const *>(content->first);
        return std::pair{node, content->second};
    }
    case major_type::simple_float:
        switch (static_cast<rfc8949::simple_float_information>(h->info)) {
        case rfc8949::simple_float_information::half_precision_float:
#if defined(__STDCPP_FLOAT16_T__)
            node->content = std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(h->argument));
#else
            node->content = heads::float_decode_binary16(static_cast<std::uint16_t>(h->argument));
#endif
            break;
        case rfc8949::simple_float_information::single_precision_float:
            node->content = std::bit_cast<float>(static_cast<std::uint32_t>(h->argument));
            break;
        case rfc8949::simple_float_information::double_precision_float:
            node->content = std::bit_cast<double>(h->argument);
            break;
        default:
            if (error const r = validity::check_simple_value(h->info, h->argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            node->content = static_cast<simple_value>(h->argument);
            break;
        }
        return std::pair{node, h->at};
    }
    std::unreachable();
}

inline std::expected<std::shared_ptr<item const>, error> lazy::decode() const
{
    validity::throw_logic_error_if_null(top_level, "cbor::lazy::decode: the lazy holds no top-level item");
    auto decoded = std::make_shared<value_sharing::decoded_items>(top_level);
    auto const built = value_sharing::item_decode(*decoded, offset, 0, limits.nesting_depth);
    if (!built) [[unlikely]]
        return std::unexpected(built.error());
    return std::shared_ptr<item const>(std::move(decoded), built->first);
}

template <class Binding>
std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l)
{
    validity::throw_logic_error_if_null(l.top_level, "cbor::lazy_decode: the lazy holds no top-level item");
    decoding::prefix before{*l.top_level, {}, {}};
    decoding::value_decoder<Binding> v{
        {std::string_view(std::span(l.top_level->encoded).subspan(l.offset))}, binding, {}, &before};
    return v.value_decode(0, std::nullopt, limits.nesting_depth);
}

}
