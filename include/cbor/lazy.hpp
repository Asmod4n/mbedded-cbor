#pragma once

#include <algorithm>
#include <array>
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

#include "rfc8746.hpp"
#include "rfc9535.hpp"
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
    return lazy{top_level, offset, typed_array_tag};
}

inline bool lazy_elements::iterator::operator==(iterator const &other) const
{
    return offset == other.offset && typed_array_tag == other.typed_array_tag && left == other.left &&
           failure == other.failure &&
           (top_level == other.top_level ||
            (top_level && other.top_level && top_level->encoded.data() == other.top_level->encoded.data()));
}

inline lazy_elements::iterator &lazy_elements::iterator::operator++()
{
    if (failure != error{}) [[unlikely]] {
        --left;
        return *this;
    }
    if (typed_array_tag != 0) {
        offset = validity::next(offset, validity::typed_array_element_size(typed_array_tag));
        --left;
        return *this;
    }
    return validity::limits_apply(
        loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) -> iterator & {
            heads::decoder<Checked> d{validity::subspan(top_level->encoded, offset),
                                      checks};
            if (auto const r = well_formedness::items_skip(d, 1); !r) [[unlikely]] {
                failure = r.error();
                --left;
                return *this;
            }
            offset = validity::distance(top_level->encoded, d.encoded);
            --left;
            if (left != 0 && d.encoded.empty()) [[unlikely]]
                failure = error::too_little_data;
            return *this;
        });
}

inline void lazy_entries::iterator::value_find()
{
    if (left == 0)
        return;
    validity::limits_apply(
        loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
            heads::decoder<Checked> d{validity::subspan(top_level->encoded, key), checks};
            if (auto const r = well_formedness::items_skip(d, 1); !r) [[unlikely]] {
                failure = r.error();
                return;
            }
            if (d.encoded.empty()) [[unlikely]] {
                failure = error::too_little_data;
                return;
            }
            value = validity::distance(top_level->encoded, d.encoded);
        });
}

inline lazy_entries::iterator::value_type lazy_entries::iterator::operator*() const
{
    if (failure != error{}) [[unlikely]]
        return std::unexpected(failure);
    return std::pair{lazy{top_level, key, {}}, lazy{top_level, value, {}}};
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
    validity::limits_apply(
        loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
            heads::decoder<Checked> d{validity::subspan(top_level->encoded, value), checks};
            if (auto const r = well_formedness::items_skip(d, 1); !r) [[unlikely]] {
                failure = r.error();
                --left;
                return;
            }
            key = validity::distance(top_level->encoded, d.encoded);
            --left;
        });
    if (failure == error{})
        value_find();
    return *this;
}

inline std::expected<lazy, error> lazy::from(std::shared_ptr<void const> owner,
                                             std::string_view const encoded, limit_values const loaded)
{
    validity::throw_logic_error_if_empty(owner,
                                         "cbor::lazy::from: the owner of the encoded data item is empty");
    if (auto const r = validity::limits_apply(
            loaded,
            [encoded]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
                return validity::check_input_bytes(encoded.size(), checks);
            });
        !r) [[unlikely]]
        return std::unexpected(r.error());
    std::string_view const content = heads::self_described_cbor_content(encoded);
    return lazy{
        std::make_shared<value_sharing::top_level_item>(std::move(owner), content, std::vector<lazy>{}, validity::offset{}), validity::offset{}, {}};
}

template <class F>
decltype(auto) lazy::limits_apply(limit_values const loaded, F &&f) const
{
    validity::throw_logic_error_if_null(top_level, "cbor::lazy: the lazy holds no top-level item");
    return validity::limits_apply(loaded, std::forward<F>(f));
}

inline std::expected<lazy, error> lazy::view(std::shared_ptr<std::string const> encoded)
{
    validity::throw_logic_error_if_null(encoded, "cbor::lazy::view: the encoded data item is null");
    std::string_view const bytes = *encoded;
    return from(std::move(encoded), bytes, limits.load());
}

template <std::same_as<std::string> Encoded>
std::expected<lazy, error> lazy::copy(Encoded &&encoded)
{
    return view(std::make_shared<std::string const>(std::move(encoded)));
}

inline std::expected<lazy, error> lazy::copy(std::string_view const encoded)
{
    return view(std::make_shared<std::string const>(encoded));
}

template <class Key, class Entries, bool Checked>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<typename Entries::iterator, error>
value_sharing::key_find(resolved<Checked> found, Key const key, limit_values const loaded)
{
    if (found.h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    validity::offset const first = validity::distance(found.source->encoded, found.d.encoded);
    std::uint64_t const readable = std::min<std::uint64_t>(found.h.argument, found.d.encoded.size());
    typename Entries::iterator it(std::move(found.source), first, readable, loaded);
    auto found_at = key_find<false>(std::move(it), std::default_sentinel, key, found.d.checks);
    if (!found_at) [[unlikely]]
        return std::unexpected(found_at.error());
    if (found_at->first == std::default_sentinel)
        if (auto const complete = validity::check_entries_all_read(found.h.argument, readable, found_at->first.failure);
            !complete) [[unlikely]]
            return std::unexpected(complete.error());
    return std::move(found_at->first);
}

template <bool Sorted, class Iterator, class Last, class Key, bool Checked>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<std::pair<Iterator, bool>, error>
value_sharing::key_find(Iterator it, Last const last, Key const key,
                        validity::limit_checks<Checked> const checks)
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
        auto const key_at = shared_resolve(*it.top_level, it.key, checks);
        if (!key_at) [[unlikely]]
            return std::unexpected(key_at.error());
        heads::decoder<Checked> probe{validity::subspan(it.top_level->encoded, *key_at),
                                      checks};
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
            if (heads::is_bignum(k->major, k->argument)) [[unlikely]] {
                auto const content = shared_resolve(*it.top_level, validity::distance(it.top_level->encoded, probe.encoded), checks);
                if (!content) [[unlikely]]
                    return std::unexpected(content.error());
                heads::decoder<Checked> d{validity::subspan(it.top_level->encoded, *content), checks};
                auto const read = d.bignum_decode(k->argument);
                if (!read) [[unlikely]]
                    return std::unexpected(read.error());
                bool const negative = k->argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
                equal = *read && negative == (key < 0) &&
                        **read == (key < 0 ? static_cast<std::uint64_t>(-1 - key) : static_cast<std::uint64_t>(key));
            }
        }
        if (equal)
            return std::pair{std::move(it), true};
        if constexpr (Sorted) {
            plain = plain && *key_at == it.key && k->major != major_type::tag;
            if (plain) {
                std::string_view read = validity::subspan(it.top_level->encoded, it.key);
                read.remove_suffix(validity::subspan(it.top_level->encoded, it.value).size());
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
    return find(key, limits.load());
}

inline std::expected<lazy_entries::iterator, error> lazy::find(std::string_view const key,
                                                               limit_values const loaded) const
{
    return limits_apply(loaded,
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<lazy_entries::iterator, error> {
                            auto found = value_sharing::lazy_resolve(*this, checks);
                            if (!found) [[unlikely]]
                                return std::unexpected(found.error());
                            return value_sharing::key_find(std::move(*found), key, loaded);
                        });
}

inline std::expected<lazy_entries::iterator, error> lazy::find(std::int64_t const key) const
{
    return find(key, limits.load());
}

inline std::expected<lazy_entries::iterator, error> lazy::find(std::int64_t const key,
                                                               limit_values const loaded) const
{
    return limits_apply(loaded,
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<lazy_entries::iterator, error> {
                            auto found = value_sharing::lazy_resolve(*this, checks);
                            if (!found) [[unlikely]]
                                return std::unexpected(found.error());
                            return value_sharing::key_find(std::move(*found), key, loaded);
                        });
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
        auto next = validity::limits_apply(
            found->loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
                return key_find<false>(std::ranges::next(std::move(*found)), std::default_sentinel, key,
                                       checks);
            });
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

inline std::expected<std::string_view, error> value_sharing::typed_array_element_read(lazy const &node)
{
    validity::throw_logic_error_if_null(node.top_level, "cbor::lazy: the lazy holds no top-level item");
    return validity::substr(node.top_level->encoded, node.offset,
                            validity::typed_array_element_size(node.typed_array_tag));
}

template <bool Checked>
std::expected<value_sharing::resolved<Checked>, error>
value_sharing::lazy_resolve(lazy const &node, validity::limit_checks<Checked> const checks)
{
    if (node.typed_array_tag == 0) [[likely]]
        return tag_content_resolve(node.top_level, node.offset, checks);
    auto const element = typed_array_element_read(node);
    if (!element) [[unlikely]]
        return std::unexpected(element.error());
    auto const h = heads::typed_array_element_decode(node.typed_array_tag, *element, validity::index{});
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    return resolved<Checked>{node.top_level, *h, heads::decoder<Checked>{std::string_view{}, checks}, node.offset};
}

template <bool Checked, std::integral Index>
std::expected<lazy, error> value_sharing::index_select(resolved<Checked> const &found, Index const index)
{
    if (heads::is_typed_array(found.h)) {
        auto const bytes = typed_array_bytes_read(found);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        std::size_t const size = validity::typed_array_element_size(found.h.argument);
        auto const position = validity::check_index(index, bytes->bytes.size() / size);
        if (!position) [[unlikely]]
            return std::unexpected(position.error());
        return lazy{found.source, validity::element_offset(bytes->offset, size, *position), found.h.argument};
    }
    if (found.h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    auto const position = validity::check_index(index, found.h.argument);
    if (!position) [[unlikely]]
        return std::unexpected(position.error());
    heads::decoder<Checked> d = found.d;
    if (auto const r = well_formedness::items_skip(d, std::to_underlying(*position)); !r) [[unlikely]]
        return std::unexpected(r.error());
    validity::offset const element = validity::distance(found.source->encoded, d.encoded);
    if (auto const target = heads::decoder<Checked>(d).head_decode(); !target) [[unlikely]]
        return std::unexpected(target.error());
    return lazy{found.source, element, {}};
}

inline std::expected<std::uint64_t, error> lazy::size() const
{
    return limits_apply(limits.load(),
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<std::uint64_t, error> {
                            auto const found = value_sharing::lazy_resolve(*this, checks);
                            if (!found) [[unlikely]]
                                return std::unexpected(found.error());
                            if (heads::is_typed_array(found->h))
                                return value_sharing::typed_array_bytes_read(*found).transform(
                                    [&](value_sharing::tag_content const &bytes) -> std::uint64_t {
                                        return bytes.bytes.size() / validity::typed_array_element_size(found->h.argument);
                                    });
                            auto const &[source, h, d, item_at] = *found;
                            if (h.major != major_type::array && h.major != major_type::map) [[unlikely]]
                                return std::unexpected(error::not_indexable);
                            auto const items = validity::checked_mul(
                                h.argument, h.major == major_type::map ? rfc8949::data_items_per_pair : 1);
                            if (!items) [[unlikely]]
                                return std::unexpected(error::too_little_data);
                            if (auto const r = validity::check_pending_items(*items, d.encoded.size()); !r)
                                [[unlikely]]
                                return std::unexpected(r.error());
                            return h.argument;
                        });
}

inline std::expected<bool, error> lazy::empty() const
{
    return size().transform([](std::uint64_t const n) { return n == 0; });
}

inline std::expected<cbor::type, error> lazy::type() const
{
    validity::throw_logic_error_if_null(top_level, "cbor::lazy::type: the lazy holds no top-level item");
    return limits_apply(limits.load(),
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<cbor::type, error> {
                            if (typed_array_tag != 0) [[unlikely]] {
                                auto const found = value_sharing::lazy_resolve(*this, checks);
                                if (!found) [[unlikely]]
                                    return std::unexpected(found.error());
                                return heads::type_of(found->h.major, found->h.info, found->h.argument);
                            }
                            auto const at = value_sharing::shared_resolve(*top_level, offset, checks);
                            if (!at) [[unlikely]]
                                return std::unexpected(at.error());
                            auto const h = heads::raw_head_read(top_level->encoded, *at, checks);
                            if (!h) [[unlikely]]
                                return std::unexpected(h.error());
                            if (!heads::is_bignum(h->major, h->argument)) [[likely]]
                                return heads::type_of(h->major, h->info, h->argument);
                            auto const content = value_sharing::shared_resolve(*top_level, h->at, checks);
                            if (!content) [[unlikely]]
                                return std::unexpected(content.error());
                            heads::decoder<Checked> d{validity::subspan(top_level->encoded, *content),
                                                      checks};
                            auto const read = d.bignum_decode(h->argument);
                            if (!read) [[unlikely]]
                                return std::unexpected(read.error());
                            if (!*read)
                                return heads::type_of(h->major, h->info, h->argument);
                            return h->argument == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                       ? cbor::type::negative_integer
                                       : cbor::type::unsigned_integer;
                        });
}

template <class Last>
    requires(std::same_as<Last, lazy_entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
std::expected<lazy_entries::iterator, error>
lazy::find(lazy_entries::iterator first, Last const last, std::string_view const key)
{
    auto found = validity::limits_apply(
        first.loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
            return value_sharing::key_find<false>(std::move(first), last, key, checks);
        });
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return std::move(found->first);
}

template <class Last>
    requires(std::same_as<Last, lazy_entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
std::expected<lazy_entries::iterator, error>
lazy::find(lazy_entries::iterator first, Last const last, std::int64_t const key)
{
    auto found = validity::limits_apply(
        first.loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
            return value_sharing::key_find<false>(std::move(first), last, key, checks);
        });
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return std::move(found->first);
}

template <class Last, class Entries>
    requires(std::same_as<Last, typename Entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
std::expected<std::ranges::subrange<typename Entries::iterator>, error>
lazy::equal_range(lazy_entries::iterator first, Last const last, std::string_view const key)
{
    auto found = validity::limits_apply(
        first.loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
            return value_sharing::key_find<true>(std::move(first), last, key, checks);
        });
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
    auto found = validity::limits_apply(
        first.loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
            return value_sharing::key_find<true>(std::move(first), last, key, checks);
        });
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
    return lazy{std::move(found->top_level), found->value, {}};
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
    return limits_apply(limits.load(),
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<lazy, error> {
                            auto const found = value_sharing::lazy_resolve(*this, checks);
                            if (!found) [[unlikely]]
                                return std::unexpected(found.error());
                            return value_sharing::index_select(*found, index);
                        });
}

template <class T>
    requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
             std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value> ||
                 std::is_same_v<T, std::string_view> ||
             std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
#ifdef __STDCPP_FLOAT128_T__
             || std::is_same_v<T, std::float128_t>
#endif
std::expected<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                               std::is_same_v<T, typed_array>,
                           owning_ref<T>, T>, error> lazy::get() const
{
    return get<T>(limits.load());
}

template <class T>
auto lazy::get(limit_values const loaded) const
{
    using result = std::expected<std::conditional_t<std::is_same_v<T, std::string_view> ||
                                                        std::is_same_v<T, std::span<std::byte const>> ||
                                                        std::is_same_v<T, typed_array>,
                                                    owning_ref<T>, T>,
                                 error>;
    return limits_apply(
        loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) -> result {
            if constexpr (std::is_same_v<T, std::span<std::byte const>>) {
                if (typed_array_tag != 0) {
                    auto const element = value_sharing::typed_array_element_read(*this);
                    if (!element) [[unlikely]]
                        return std::unexpected(element.error());
                    return owning_ref<T>(top_level->owner, std::as_bytes(std::span(*element)));
                }
            }
#ifdef __STDCPP_FLOAT128_T__
            if constexpr (std::is_same_v<T, std::float128_t>) {
                if (typed_array_tag != std::to_underlying(rfc8746::tag_number::float128_big_endian) &&
                    typed_array_tag != std::to_underlying(rfc8746::tag_number::float128_little_endian)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                auto const element = value_sharing::typed_array_element_read(*this);
                if (!element) [[unlikely]]
                    return std::unexpected(element.error());
                std::array<std::byte, sizeof(std::float128_t)> bits{};
                std::ranges::copy(std::as_bytes(std::span(*element)), bits.begin());
                bool const little_endian =
                    typed_array_tag == std::to_underlying(rfc8746::tag_number::float128_little_endian);
                if (little_endian != (std::endian::native == std::endian::little))
                    std::ranges::reverse(bits);
                return std::bit_cast<std::float128_t>(bits);
            } else {
#endif
            auto const found = value_sharing::lazy_resolve(*this, checks);
            if (!found) [[unlikely]]
                return std::unexpected(found.error());
            auto [source, h, d, item_at] = *found;
            if constexpr (std::integral<T> && !std::is_same_v<T, bool>) {
                bool negative = h.major == major_type::negative_integer;
                std::uint64_t argument = h.argument;
                if (heads::is_bignum(h.major, h.argument)) {
                    negative = h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
                    auto const content = value_sharing::shared_resolve(
                        *source, validity::distance(source->encoded, d.encoded), checks);
                    if (!content) [[unlikely]]
                        return std::unexpected(content.error());
                    d = heads::decoder<Checked>{
                        validity::subspan(source->encoded, *content), checks};
                    auto const read = d.bignum_decode(h.argument);
                    if (!read) [[unlikely]]
                        return std::unexpected(read.error());
                    if (!*read) [[unlikely]]
                        return std::unexpected(error::number_out_of_range);
                    argument = **read;
                } else if (h.major != major_type::unsigned_integer && !negative) [[unlikely]] {
                    return std::unexpected(error::incorrect_type);
                }
                if (error const c = validity::check_number_range<T>(negative, argument).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                T const magnitude = static_cast<T>(argument);
                return static_cast<T>(negative ? ~magnitude : magnitude);
            } else if constexpr (std::is_same_v<T, double>) {
                if (heads::is_bignum(h.major, h.argument)) [[unlikely]] {
                    auto const content = value_sharing::shared_resolve(
                        *source, validity::distance(source->encoded, d.encoded), checks);
                    if (!content) [[unlikely]]
                        return std::unexpected(content.error());
                    d = heads::decoder<Checked>{
                        validity::subspan(source->encoded, *content), checks};
                    auto const read = d.bignum_decode(h.argument);
                    if (!read) [[unlikely]]
                        return std::unexpected(read.error());
                    if (!*read) [[unlikely]]
                        return std::unexpected(error::number_out_of_range);
                    h.major = h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                  ? major_type::negative_integer
                                  : major_type::unsigned_integer;
                    h.argument = **read;
                }
                if (h.major == major_type::unsigned_integer || h.major == major_type::negative_integer) {
                    bool const negative = h.major == major_type::negative_integer;
                    if (error const c = validity::check_number_range<double>(negative, h.argument).error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::unexpected(c);
                    return negative ? -1.0 - static_cast<double>(h.argument) : static_cast<double>(h.argument);
                }
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
                if (error const c = validity::check_simple_value(h.info, h.argument).error_or(error{});
                    c != error{}) [[unlikely]]
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
                auto const bytes = value_sharing::typed_array_bytes_read(*found);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                return owning_ref<T>(source->owner, typed_array{h.argument, std::as_bytes(std::span(bytes->bytes))});
            } else {
                if (h.major != major_type::byte_string) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                auto const bytes = d.byte_string_decode(h.argument);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                return owning_ref<T>(source->owner, std::as_bytes(std::span(*bytes)));
            }
#ifdef __STDCPP_FLOAT128_T__
            }
#endif
        });
}

inline std::expected<lazy_elements, error> lazy::elements() const
{
    return elements(limits.load());
}

inline std::expected<lazy_elements, error> lazy::elements(limit_values const loaded) const
{
    return limits_apply(
        loaded,
        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
            -> std::expected<lazy_elements, error> {
            auto const found = value_sharing::lazy_resolve(*this, checks);
            if (!found) [[unlikely]]
                return std::unexpected(found.error());
            if (heads::is_typed_array(found->h)) {
                auto const bytes = value_sharing::typed_array_bytes_read(*found);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                return lazy_elements(found->source, bytes->offset,
                                     bytes->bytes.size() / validity::typed_array_element_size(found->h.argument), loaded,
                                     found->h.argument);
            }
            auto const &[source, h, d, item_at] = *found;
            if (h.major != major_type::array) [[unlikely]]
                return std::unexpected(error::not_indexable);
            if (auto const r = validity::check_definite_length(h.major, h.info); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = validity::check_pending_items(h.argument, d.encoded.size()); !r) [[unlikely]]
                return std::unexpected(r.error());
            return lazy_elements(source, validity::distance(source->encoded, d.encoded), h.argument, loaded, {});
        });
}

inline std::expected<lazy_entries, error> lazy::entries() const
{
    return entries(limits.load());
}

inline std::expected<lazy_entries, error> lazy::entries(limit_values const loaded) const
{
    return limits_apply(
        loaded,
        [&]<bool Checked>(
            std::size_t, validity::limit_checks<Checked> const checks) -> std::expected<lazy_entries, error> {
            auto const found = value_sharing::lazy_resolve(*this, checks);
            if (!found) [[unlikely]]
                return std::unexpected(found.error());
            auto const &[source, h, d, item_at] = *found;
            if (h.major != major_type::map) [[unlikely]]
                return std::unexpected(error::not_indexable);
            if (auto const r = validity::check_definite_length(h.major, h.info); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const items = validity::checked_mul(h.argument, rfc8949::data_items_per_pair);
            if (!items) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (auto const r = validity::check_pending_items(*items, d.encoded.size()); !r) [[unlikely]]
                return std::unexpected(r.error());
            return lazy_entries(source, validity::distance(source->encoded, d.encoded), h.argument, loaded);
        });
}

template <class Item>
std::expected<void, error> validity::check_tag_content(std::uint64_t const tag, Item const *content,
                                                       std::span<Item const *const> const shareables)
{
    std::size_t references_followed = 0;
    for (;;) {
        if (content->type != cbor::type::tag || (content->via.tag.number != std::to_underlying(rfc8949::tag_number::sharedref) &&
                                                content->via.tag.number != std::to_underlying(rfc8949::tag_number::shareable)))
            return check_tag_content(tag, heads::major_type_of(content->type), content->additional_information);
        if (content->via.tag.number == std::to_underlying(rfc8949::tag_number::shareable)) {
            content = content->via.tag.content;
            continue;
        }
        if (auto const ends = check_sharedref_chain_ends(++references_followed, shareables.size()); !ends) [[unlikely]]
            return ends;
        auto const index = check_sharedref_index(content->via.tag.content->via.u64, shareables.size());
        if (!index) [[unlikely]]
            return std::unexpected(index.error());
        content = shareables[*index];
        if (content == nullptr) [[unlikely]]
            return std::unexpected(error::sharedref_not_complete);
    }
}


template <bool Checked>
std::size_t value_sharing::tree_decoder<Checked>::marks_counted()
{
    if (mark_next)
        return *mark_next;
    std::size_t before = 0;
    if (start != validity::offset{}) {
        std::vector<lazy> const &marks = top_level.sharedrefs_read(start);
        auto const at = std::ranges::lower_bound(marks, start, {}, &lazy::offset);
        before = static_cast<std::size_t>(std::ranges::distance(marks.begin(), at));
        if (at != marks.end() && at->offset == start) {
            if (shareables.size() <= before)
                shareables.resize(before + 1);
            if (shareables[before] == nullptr)
                shareables[before] = root;
            ++before;
        }
    }
    if (shareables.size() < before)
        shareables.resize(before);
    marks_before = before;
    mark_next = before;
    return before;
}

template <bool Checked>
std::expected<void, error> value_sharing::tree_decoder<Checked>::marks_before_decode()
{
    mark_depths found;
    heads::decoder<Checked> all{top_level.encoded, d.checks};
    if (auto const r = well_formedness::item_skip(all, found, 0, depth_max); !r) [[unlikely]]
        return r;
    for (std::size_t j = 0; j < marks_before; ++j) {
        if (shareables[j] != nullptr)
            continue;
        auto const marked = validity::check_sharedref_index(j, found.depths.size());
        if (!marked) [[unlikely]]
            return std::unexpected(marked.error());
        validity::offset const at = top_level.sharedrefs_read(start)[j].offset;
        tree_decoder outside{decoded,
                             top_level,
                             shareables,
                             {validity::subspan(top_level.encoded, at), d.checks},
                             at,
                             depth_max,
                             j + 1,
                             j};
        if (auto const r = outside.item_decode(std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(1), found.depths[*marked] + 1, j); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return {};
}

template <bool Checked>
std::expected<void, error> value_sharing::tree_decoder<Checked>::scalar_decode(item &node, heads::head const h)
{
    switch (h.major) {
    case major_type::byte_string:
    case major_type::text_string: {
        auto const string = d.byte_string_decode(h.argument);
        if (!string) [[unlikely]]
            return std::unexpected(string.error());
        if (h.major == major_type::text_string)
            node.via.str = *string;
        else
            node.via.bin = std::as_bytes(std::span(*string));
        return {};
    }
    case major_type::simple_float:
        switch (static_cast<rfc8949::simple_float_information>(h.info)) {
        case rfc8949::simple_float_information::half_precision_float:
        case rfc8949::simple_float_information::single_precision_float:
        case rfc8949::simple_float_information::double_precision_float:
            node.via.f64 = heads::float_decode(h.info, h.argument);
            return {};
        default:
            if (error const r = validity::check_simple_value(h.info, h.argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            node.via.simple_value = static_cast<simple_value>(h.argument);
            return {};
        }
    default:
        return {};
    }
}

template <bool Checked>
std::expected<item *, error> value_sharing::tree_decoder<Checked>::bignum_decode(item &node, std::uint64_t const tag,
                                                                               std::size_t const depth)
{
    auto const next = heads::raw_head_read(d.encoded, validity::offset{}, d.checks);
    if (!next || next->major != major_type::byte_string)
        return nullptr;
    if (auto const r = validity::check_nesting_depth(depth + 1, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    heads::decoder<Checked> const before = d;
    auto const read = d.bignum_decode(tag);
    if (!read) [[unlikely]]
        return std::unexpected(read.error());
    if (!*read) {
        d = before;
        return nullptr;
    }
    node = item{tag == std::to_underlying(rfc8949::tag_number::negative_bignum) ? cbor::type::negative_integer
                                                                              : cbor::type::unsigned_integer,
                heads::preferred_argument_info(**read),
                {.u64 = **read}};
    return &node;
}

template <bool Checked>
error value_sharing::tree_decoder<Checked>::bignum_content_check(std::uint64_t const tag)
{
    auto const inner = heads::raw_head_read(d.encoded, validity::offset{}, d.checks);
    if (!inner || !heads::is_bignum(inner->major, inner->argument)) [[likely]]
        return error{};
    return validity::check_tag_content(tag, inner->major, inner->info).error_or(error{});
}

template <bool Checked>
std::expected<item *, error> value_sharing::tree_decoder<Checked>::item_decode(item *const storage,
                                                                               std::size_t const depth,
                                                                               std::optional<std::size_t> const mark)
{
    if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const h = d.head_decode();
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    return content_decode(storage, *h, depth, mark);
}

template <bool Checked>
std::expected<item *, error> value_sharing::tree_decoder<Checked>::content_decode(item *const storage,
                                                                                  heads::head const head,
                                                                                  std::size_t const depth,
                                                                                  std::optional<std::size_t> const mark)
{
    item &node = *::new (storage) item{heads::type_of(head.major, head.info, head.argument), head.info, {.u64 = head.argument}};
    if (root == nullptr)
        root = &node;
    if (mark && shareables[*mark] == nullptr)
        shareables[*mark] = &node;
    switch (head.major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return &node;
    case major_type::byte_string:
    case major_type::text_string:
    case major_type::simple_float:
        if (auto const r = scalar_decode(node, head); !r) [[unlikely]]
            return std::unexpected(r.error());
        return &node;
    case major_type::array:
    case major_type::map: {
        auto const count = validity::checked_mul(
            head.argument, head.major == major_type::map ? rfc8949::data_items_per_pair : std::size_t{1});
        if (!count) [[unlikely]]
            return std::unexpected(error::too_little_data);
        auto const reserved = validity::checked_add(open, *count);
        if (!reserved) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (auto const r = validity::check_pending_items(*reserved, d.encoded.size()); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::size_t const n = static_cast<std::size_t>(*count);
        std::span<item> const nodes(std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(n), n);
        open = *reserved;
        if (n != 0)
            if (auto const r = validity::check_nesting_depth(depth + 1, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
        for (item &child : nodes) {
            --open;
            auto const c = d.head_decode();
            if (!c) [[unlikely]]
                return std::unexpected(c.error());
            if (c->major == major_type::array || c->major == major_type::map || c->major == major_type::tag) {
                if (auto const r = content_decode(&child, *c, depth + 1, std::nullopt); !r) [[unlikely]]
                    return r;
                continue;
            }
            ::new (&child) item{heads::type_of(c->major, c->info, c->argument), c->info, {.u64 = c->argument}};
            if (auto const r = scalar_decode(child, *c); !r) [[unlikely]]
                return std::unexpected(r.error());
        }
        if (head.major == major_type::map)
            node.via.map = nodes;
        else
            node.via.array = nodes;
        return &node;
    }
    case major_type::tag: {
        if (heads::is_bignum(head.major, head.argument))
            if (auto const r = bignum_decode(node, head.argument, depth); !r || *r != nullptr)
                return r;
        if (head.argument == std::to_underlying(rfc8949::tag_number::shareable)) {
            std::size_t const number = marks_counted();
            mark_next = number + 1;
            if (shareables.size() <= number)
                shareables.resize(number + 1);
            auto const content =
                item_decode(std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(1), depth + 1, number);
            if (!content) [[unlikely]]
                return content;
            node.via.tag = {head.argument, *content};
            return &node;
        }
        if (head.argument == std::to_underlying(rfc8949::tag_number::sharedref)) {
            if (auto const r = validity::check_nesting_depth(depth + 1, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const n = d.head_decode();
            if (!n) [[unlikely]]
                return std::unexpected(n.error());
            if (error const c = validity::check_tag_content(head.argument, n->major, n->info).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const index = validity::check_sharedref_index(n->argument, marks_counted());
            if (!index) [[unlikely]]
                return std::unexpected(index.error());
            if (shareables[*index] == nullptr)
                if (auto const r = marks_before_decode(); !r) [[unlikely]]
                    return std::unexpected(r.error());
            item const *const shareable = shareables[*index];
            if (shareable == nullptr) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            item const *const number = ::new (std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(1))
                item{heads::type_of(n->major, n->info, n->argument), n->info, {.u64 = n->argument}};
            node.via.tag = {head.argument, number};
            decoded.sharedrefs.emplace_back(&node, shareable);
            return &node;
        }
        if (error const c = bignum_content_check(head.argument); c != error{}) [[unlikely]]
            return std::unexpected(c);
        auto const content =
            item_decode(std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(1), depth + 1, std::nullopt);
        if (!content) [[unlikely]]
            return content;
        if (error const c = validity::check_tag_content(head.argument, static_cast<item const *>(*content),
                                                        std::span<item const *const>(shareables))
                                .error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        node.via.tag = {head.argument, *content};
        if (head.argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return &node;
        item const *bytes = *content;
        while (bytes->type == cbor::type::tag) {
            if (bytes->via.tag.number != std::to_underlying(rfc8949::tag_number::sharedref)) {
                bytes = bytes->via.tag.content;
                continue;
            }
            auto const index = validity::check_sharedref_index(bytes->via.tag.content->via.u64, shareables.size());
            if (!index) [[unlikely]]
                return std::unexpected(index.error());
            bytes = shareables[*index];
        }
        std::span<std::byte const> const encoded = bytes->via.bin;
        top_level_item &nested = decoded.embedded.emplace_front(
            top_level.owner, std::string_view(reinterpret_cast<char const *>(encoded.data()), encoded.size()),
            std::vector<lazy>{}, validity::offset{});
        std::vector<item const *> nested_shareables;
        tree_decoder inner{decoded, nested, nested_shareables, {nested.encoded, d.checks}, validity::offset{}, depth_max, 0, 0};
        auto const data_item = inner.item_decode(std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(1),
                                                 depth + 2, std::nullopt);
        if (!data_item) [[unlikely]]
            return data_item;
        if (auto const one = validity::check_one_data_item(inner.d.encoded); !one) [[unlikely]]
            return std::unexpected(one.error());
        decoded.encoded_cbor_data_items.emplace_back(&node, *data_item);
        return &node;
    }
    }
    std::unreachable();
}

inline std::expected<std::shared_ptr<item const>, error> lazy::decode() const
{
    validity::throw_logic_error_if_null(top_level, "cbor::lazy::decode: the lazy holds no top-level item");
    if (typed_array_tag != 0) [[unlikely]] {
        auto const h = validity::limits_apply(
            limits.load(),
            [this]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                -> std::expected<heads::head, error> {
                return value_sharing::lazy_resolve(*this, checks).transform(
                    [](value_sharing::resolved<Checked> const &found) { return found.h; });
            });
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        std::string encoded;
        heads::head_append(encoded, h->major, h->info, h->argument);
        auto const element = copy(std::move(encoded));
        if (!element) [[unlikely]]
            return std::unexpected(element.error());
        return element->decode();
    }
    return validity::limits_apply(
        limits.load(),
        [this]<bool Checked>(std::size_t const depth_max, validity::limit_checks<Checked> const checks)
            -> std::expected<std::shared_ptr<item const>, error> {
            std::string_view const encoded = validity::subspan(top_level->encoded, offset);
            auto decoded = std::make_unique<value_sharing::zone>(
                top_level, validity::checked_mul(encoded.size(), sizeof(item))
                               .transform([](std::size_t const size) { return std::min(size, validity::initial_size_limit); })
                               .value_or(validity::initial_size_limit));
            std::vector<item const *> shareables;
            value_sharing::tree_decoder<Checked> tree{
                *decoded, *top_level, shareables, {encoded, checks}, offset, depth_max,
                offset == validity::offset{} ? std::optional<std::size_t>{0} : std::nullopt, 0};
            auto const built = tree.item_decode(
                std::pmr::polymorphic_allocator<item>(&decoded->memory).allocate(1), 0, std::nullopt);
            if (!built) [[unlikely]]
                return std::unexpected(built.error());
            std::ranges::sort(decoded->sharedrefs, std::less<>{}, &value_sharing::zone::link::first);
            std::ranges::sort(decoded->encoded_cbor_data_items, std::less<>{}, &value_sharing::zone::link::first);
            return std::shared_ptr<item const>(*built, value_sharing::zone_owner{std::move(decoded)});
        });
}

}
