#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "binding.hpp"
#include "error.hpp"
#include "head.hpp"
#include "item_size.hpp"
#include "owning_ref.hpp"
#include "validity.hpp"

namespace cbor
{

struct lazy;

struct item;

struct lazy_elements;

struct lazy_entries;

class value_sharing
{
    struct top_level_item;

    struct decoded_items;

    template <bool Checked>
    struct sharing_decoder;

    template <bool Checked>
    struct resolved;

    template <bool Checked>
    static std::expected<std::size_t, error> shared_resolve(top_level_item &top_level, std::size_t at,
                                                            validity::limit_checks<Checked> checks);

    template <bool Checked>
    static std::expected<item *, error> item_resolve(decoded_items &decoded, std::size_t at,
                                                     validity::limit_checks<Checked> checks);

    template <bool Checked>
    static std::expected<std::pair<item *, std::size_t>, error>
    item_decode(decoded_items &decoded, std::size_t at, std::size_t depth, std::size_t depth_max,
                validity::limit_checks<Checked> checks);

    template <bool Checked>
    static std::expected<resolved<Checked>, error> container_resolve(std::shared_ptr<top_level_item> source,
                                                                     std::size_t offset,
                                                                     validity::limit_checks<Checked> checks);

    template <class Key, class Entries = lazy_entries, bool Checked>
        requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
    static std::expected<typename Entries::iterator, error> key_find(resolved<Checked> found, Key key,
                                                                     limit_values loaded);

    template <bool Sorted, class Iterator, class Last, class Key, bool Checked>
        requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
    static std::expected<std::pair<Iterator, bool>, error> key_find(Iterator first, Last last, Key key,
                                                                    validity::limit_checks<Checked> checks);

    template <class Iterator>
    static std::expected<lazy, error> value_of(std::expected<Iterator, error> found);

    template <class Key, class Entries = lazy_entries>
        requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
    static std::expected<std::size_t, error> key_count(std::expected<typename Entries::iterator, error> found, Key key);

    friend class validity;

    friend struct lazy;

    friend struct lazy_elements;

    friend struct lazy_entries;

    friend class decoding;

    friend class jsonpath;

#ifdef __cpp_impl_reflection
    friend class generic;

    template <class>
    friend class databind;
#endif
};

struct lazy_elements : std::ranges::view_interface<lazy_elements> {
    struct iterator {
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::expected<lazy, error>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        value_type operator*() const;

        iterator &operator++();

        iterator operator++(int)
        {
            iterator const before = *this;
            ++*this;
            return before;
        }

        bool operator==(iterator const &) const;

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }

    private:
        iterator(std::shared_ptr<value_sharing::top_level_item> t, std::size_t const o, std::uint64_t const l,
                 limit_values const v)
            : top_level(std::move(t)), offset(o), left(l), loaded(v)
        {
        }

        std::shared_ptr<value_sharing::top_level_item> top_level;
        std::size_t offset{};
        std::uint64_t left{};
        error failure{};
        limit_values loaded{};

        friend struct lazy_elements;
    };

    iterator begin() const
    {
        return iterator{top_level, offset, count, loaded};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }

    std::uint64_t size() const
    {
        return count;
    }

    void front() const = delete;

private:
    lazy_elements(std::shared_ptr<value_sharing::top_level_item> t, std::size_t const o,
                  std::uint64_t const c, limit_values const v)
        : top_level(std::move(t)), offset(o), count(c), loaded(v)
    {
    }

    std::shared_ptr<value_sharing::top_level_item> top_level;
    std::size_t offset;
    std::uint64_t count;
    limit_values loaded;

    friend struct lazy;
};

struct lazy_entries : std::ranges::view_interface<lazy_entries> {
    struct iterator {
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::expected<std::pair<lazy, lazy>, error>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        value_type operator*() const;

        iterator &operator++();

        iterator operator++(int)
        {
            iterator const before = *this;
            ++*this;
            return before;
        }

        bool operator==(iterator const &) const;

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }

    private:
        iterator(std::shared_ptr<value_sharing::top_level_item> t, std::size_t const k, std::uint64_t const l,
                 limit_values const v)
            : top_level(std::move(t)), key(k), value(k), left(l), loaded(v)
        {
            value_find();
        }

        void value_find();

        std::shared_ptr<value_sharing::top_level_item> top_level;
        std::size_t key{};
        std::size_t value{};
        std::uint64_t left{};
        error failure{};
        limit_values loaded{};

        friend struct lazy_entries;

        friend struct lazy;

        friend class value_sharing;
    };

    iterator begin() const
    {
        return iterator{top_level, offset, count, loaded};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }

    std::uint64_t size() const
    {
        return count;
    }

    void front() const = delete;

private:
    lazy_entries(std::shared_ptr<value_sharing::top_level_item> t, std::size_t const o, std::uint64_t const c,
                 limit_values const v)
        : top_level(std::move(t)), offset(o), count(c), loaded(v)
    {
    }

    std::shared_ptr<value_sharing::top_level_item> top_level;
    std::size_t offset;
    std::uint64_t count;
    limit_values loaded;

    friend struct lazy;
};

struct lazy {
    std::shared_ptr<value_sharing::top_level_item> top_level;
    std::size_t offset;

    template <std::same_as<std::string> Encoded>
    static std::expected<lazy, error> from(Encoded &&encoded);
    static std::expected<lazy, error> from(std::string_view encoded);
    static std::expected<lazy, error> from(std::shared_ptr<std::string const> encoded);
    static std::expected<lazy, error> from(std::shared_ptr<void const> owner, std::string_view encoded);
    template <class Encoded>
        requires std::same_as<std::remove_const_t<Encoded>, std::string>
    static std::expected<lazy, error> from(std::shared_ptr<void const> owner, Encoded &&encoded) = delete;

    std::expected<lazy, error> at(std::string_view key) const;

    std::expected<lazy, error> at(key k) const;

    std::expected<lazy, error> at(std::size_t index) const;

    std::expected<lazy_entries::iterator, error> find(std::string_view key) const;

    std::expected<lazy_entries::iterator, error> find(std::int64_t key) const;

    std::expected<bool, error> contains(std::string_view key) const;

    std::expected<bool, error> contains(std::int64_t key) const;

    std::expected<std::size_t, error> count(std::string_view key) const;

    std::expected<std::size_t, error> count(std::int64_t key) const;

    std::expected<std::uint64_t, error> size() const;

    std::expected<bool, error> empty() const;

    template <class Last>
        requires(std::same_as<Last, lazy_entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
    static std::expected<lazy_entries::iterator, error>
    find(lazy_entries::iterator first, Last last, std::string_view key);

    template <class Last>
        requires(std::same_as<Last, lazy_entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
    static std::expected<lazy_entries::iterator, error>
    find(lazy_entries::iterator first, Last last, std::int64_t key);

    template <class Last, class Entries = lazy_entries>
        requires(std::same_as<Last, typename Entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
    static std::expected<std::ranges::subrange<typename Entries::iterator>, error>
    equal_range(lazy_entries::iterator first, Last last, std::string_view key);

    template <class Last, class Entries = lazy_entries>
        requires(std::same_as<Last, typename Entries::iterator> || std::same_as<Last, std::default_sentinel_t>)
    static std::expected<std::ranges::subrange<typename Entries::iterator>, error>
    equal_range(lazy_entries::iterator first, Last last, std::int64_t key);

    template <class T>
        requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                 std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value> ||
                 std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
    std::expected<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                                   std::is_same_v<T, typed_array>,
                               owning_ref<T>, T>, error> get() const;

    std::expected<lazy_elements, error> elements() const;

    std::expected<lazy_entries, error> entries() const;

    std::expected<std::shared_ptr<item const>, error> decode() const;

private:
    static std::expected<lazy, error> from(std::shared_ptr<void const> owner, std::string_view encoded,
                                           limit_values loaded);

    std::expected<lazy_entries::iterator, error> find(std::string_view key, limit_values loaded) const;

    std::expected<lazy_entries::iterator, error> find(std::int64_t key, limit_values loaded) const;

    template <class T>
    auto get(limit_values loaded) const;

    std::expected<lazy_elements, error> elements(limit_values loaded) const;

    std::expected<lazy_entries, error> entries(limit_values loaded) const;

    template <class F>
    decltype(auto) limits_apply(limit_values loaded, F &&f) const;

    friend class jsonpath;
};

}

template <>
inline constexpr bool std::ranges::enable_borrowed_range<cbor::lazy_elements> = true;

template <>
inline constexpr bool std::ranges::enable_borrowed_range<cbor::lazy_entries> = true;

#include "item.hpp"

namespace cbor
{

struct value_sharing::top_level_item {
    std::shared_ptr<void const> owner;
    std::string_view encoded;
    std::vector<lazy> sharedrefs;
    std::size_t high_water_mark;
    std::once_flag sharedrefs_built{};

    std::vector<lazy> const &sharedrefs_read()
    {
        std::call_once(sharedrefs_built, [this] {
            heads::decoder<false> all{encoded, validity::limit_checks<false>{}};
            std::uint64_t pending = 1;
            while (pending != 0) {
                --pending;
                auto const h = all.head_decode();
                if (!h || !validity::check_definite_length(h->major, h->info)) [[unlikely]]
                    return;
                std::uint64_t added = 0;
                switch (h->major) {
                case major_type::byte_string:
                case major_type::text_string:
                    if (!all.byte_string_decode(h->argument)) [[unlikely]]
                        return;
                    break;
                case major_type::array:
                    added = h->argument;
                    break;
                case major_type::map:
                    added = validity::checked_mul(h->argument, rfc8949::data_items_per_pair)
                                .value_or(std::numeric_limits<std::uint64_t>::max());
                    break;
                case major_type::tag:
                    if (h->argument == std::to_underlying(rfc8949::tag_number::shareable))
                        mark(all);
                    added = 1;
                    break;
                default:
                    break;
                }
                pending = validity::checked_add(pending, added).value_or(std::numeric_limits<std::uint64_t>::max());
            }
        });
        return sharedrefs;
    }

    template <bool Checked>
    std::size_t mark(heads::decoder<Checked> const &at)
    {
        std::size_t const offset = encoded.size() - at.encoded.size();
        if (offset > high_water_mark) {
            sharedrefs.push_back(lazy{{}, offset});
            high_water_mark = offset;
            return sharedrefs.size() - 1;
        }
        auto const known = std::ranges::lower_bound(sharedrefs, offset, {}, &lazy::offset);
        return static_cast<std::size_t>(std::ranges::distance(sharedrefs.begin(), known));
    }

    template <bool Checked>
    static std::expected<lazy, error>
    sharedref_decode(heads::decoder<Checked> &d, std::size_t const reference_at, std::size_t const item_at,
                     std::vector<lazy> const &marks)
    {
        auto const n = d.head_decode();
        if (!n) [[unlikely]]
            return std::unexpected(n.error());
        if (error const c =
                validity::check_tag_content(std::to_underlying(rfc8949::tag_number::sharedref), n->major, n->info)
                    .error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        auto const before = std::ranges::upper_bound(marks, reference_at, {}, &lazy::offset);
        auto const index = validity::check_sharedref_index(
            n->argument, static_cast<std::size_t>(std::ranges::distance(marks.begin(), before)));
        if (!index) [[unlikely]]
            return std::unexpected(index.error());
        lazy const &found = marks[*index];
        if (found.offset >= item_at) [[unlikely]]
            return std::unexpected(error::sharedref_not_complete);
        return found;
    }
};

struct value_sharing::decoded_items {
    std::shared_ptr<top_level_item> top_level;
    std::deque<item> items{};
    std::vector<std::pair<std::size_t, item *>> item_offsets{};

    template <bool Checked>
    std::expected<item *, error> entry(std::size_t const offset, validity::limit_checks<Checked> const checks)
    {
        auto const known = item_offsets.empty() || item_offsets.back().first < offset
                               ? item_offsets.end()
                               : std::ranges::lower_bound(item_offsets, offset, {}, &std::pair<std::size_t, item *>::first);
        if (known != item_offsets.end() && known->first == offset)
            return known->second;
        auto const h = heads::raw_head_read(top_level->encoded, offset, checks);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        item &placeholder = items.emplace_back(item{h->major, h->info, h->argument, lazy{{}, offset}});
        item_offsets.insert(known, {offset, &placeholder});
        return &placeholder;
    }
};

template <bool Checked>
struct value_sharing::sharing_decoder : heads::decoder<Checked> {
    top_level_item message;
};

template <bool Checked>
struct value_sharing::resolved {
    std::shared_ptr<top_level_item> source;
    heads::head h;
    heads::decoder<Checked> d;
};

template <bool Checked>
std::expected<std::size_t, error> value_sharing::shared_resolve(top_level_item &top_level, std::size_t at,
                                                                validity::limit_checks<Checked> const checks)
{
    std::size_t item_at = at;
    for (;;) {
        auto const h = heads::raw_head_read(top_level.encoded, at, checks);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag)
            return at;
        if (h->argument == std::to_underlying(rfc8949::tag_number::shareable) ||
            h->argument == std::to_underlying(rfc8949::tag_number::self_described_cbor)) {
            at = h->at;
            continue;
        }
        if (h->argument != std::to_underlying(rfc8949::tag_number::sharedref))
            return at;
        heads::decoder<Checked> d{std::string_view(std::span(top_level.encoded).subspan(h->at)), checks};
        auto const found = top_level_item::sharedref_decode(d, at, item_at, top_level.sharedrefs_read());
        if (!found) [[unlikely]]
            return std::unexpected(found.error());
        at = found->offset;
        item_at = at;
    }
}

template <bool Checked>
std::expected<item *, error> value_sharing::item_resolve(decoded_items &decoded, std::size_t const at,
                                                         validity::limit_checks<Checked> const checks)
{
    auto const node = shared_resolve(*decoded.top_level, at, checks);
    if (!node) [[unlikely]]
        return std::unexpected(node.error());
    return decoded.entry(*node, checks);
}

template <bool Checked>
std::expected<value_sharing::resolved<Checked>, error>
value_sharing::container_resolve(std::shared_ptr<top_level_item> source, std::size_t offset,
                                 validity::limit_checks<Checked> const checks)
{
    validity::throw_logic_error_if_null(source, "cbor::lazy: the lazy holds no top-level item");
    for (;;) {
        auto const at = shared_resolve(*source, offset, checks);
        if (!at) [[unlikely]]
            return std::unexpected(at.error());
        heads::decoder<Checked> d{std::string_view(std::span(source->encoded).subspan(*at)), checks};
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag ||
            h->argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return resolved<Checked>{source, *h, d};
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (error const c = validity::check_tag_content(h->argument, r->major, r->info).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        auto const embedded = d.byte_string_decode(r->argument);
        if (!embedded) [[unlikely]]
            return std::unexpected(embedded.error());
        source = std::make_shared<top_level_item>(source->owner, *embedded, std::vector<lazy>{}, 0);
        offset = 0;
    }
}

}

namespace cbor
{

template <class First, class Second, bool Checked>
std::expected<bool, error> validity::keys_equivalent(First &first, std::size_t const first_at, Second &second,
                                                     std::size_t const second_at, std::size_t const depth,
                                                     std::size_t const depth_max,
                                                     limit_checks<Checked> const checks)
{
    if (auto const r = check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const encoded_of = []<class Message>(Message &message) -> std::string_view {
        if constexpr (std::same_as<Message, std::string_view const>)
            return message;
        else
            return message.encoded;
    };
    auto const resolve = [checks]<class Message>(Message &message,
                                                 std::size_t const at) -> std::expected<std::size_t, error> {
        if constexpr (std::same_as<Message, std::string_view const>)
            return at;
        else
            return value_sharing::shared_resolve(message, at, checks);
    };
    auto const skip = [](heads::decoder<Checked> &d) {
        well_formedness::no_marks none;
        return well_formedness::item_skip(d, none);
    };
    std::string_view const a = encoded_of(first);
    std::string_view const b = encoded_of(second);
    auto const x = resolve(first, first_at);
    if (!x) [[unlikely]]
        return std::unexpected(x.error());
    auto const y = resolve(second, second_at);
    if (!y) [[unlikely]]
        return std::unexpected(y.error());
    auto const h = heads::raw_head_read(a, *x, checks);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    auto const k = heads::raw_head_read(b, *y, checks);
    if (!k) [[unlikely]]
        return std::unexpected(k.error());
    if (error const c = check_definite_length(h->major, h->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    if (error const c = check_definite_length(k->major, k->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    if (h->major != k->major)
        return false;
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return h->argument == k->argument;
    case major_type::byte_string:
    case major_type::text_string: {
        heads::decoder<Checked> d{std::string_view(std::span(a).subspan(h->at)), checks};
        auto const s = d.byte_string_decode(h->argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        heads::decoder<Checked> e{std::string_view(std::span(b).subspan(k->at)), checks};
        auto const t = e.byte_string_decode(k->argument);
        if (!t) [[unlikely]]
            return std::unexpected(t.error());
        return *s == *t;
    }
    case major_type::array: {
        if (h->argument != k->argument)
            return false;
        heads::decoder<Checked> d{std::string_view(std::span(a).subspan(h->at)), checks};
        heads::decoder<Checked> e{std::string_view(std::span(b).subspan(k->at)), checks};
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const equal = keys_equivalent(first, a.size() - d.encoded.size(), second,
                                               b.size() - e.encoded.size(), depth + 1, depth_max, checks);
            if (!equal || !*equal)
                return equal;
            if (auto const r = skip(d); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = skip(e); !r) [[unlikely]]
                return std::unexpected(r.error());
        }
        return true;
    }
    case major_type::map: {
        if (h->argument != k->argument)
            return false;
        auto const pairs_counted = [&]<class From, class In>(From &from, std::size_t const key,
                                                             std::size_t const value, In &in,
                                                             std::string_view const in_encoded,
                                                             std::size_t const in_at)
            -> std::expected<std::uint64_t, error> {
            std::uint64_t count = 0;
            heads::decoder<Checked> e{std::string_view(std::span(in_encoded).subspan(in_at)), checks};
            for (std::uint64_t j = 0; j < h->argument; ++j) {
                std::size_t const other = in_encoded.size() - e.encoded.size();
                if (auto const r = skip(e); !r) [[unlikely]]
                    return std::unexpected(r.error());
                std::size_t const other_value = in_encoded.size() - e.encoded.size();
                if (auto const r = skip(e); !r) [[unlikely]]
                    return std::unexpected(r.error());
                auto const key_same = keys_equivalent(from, key, in, other, depth + 1, depth_max, checks);
                if (!key_same) [[unlikely]]
                    return std::unexpected(key_same.error());
                if (!*key_same)
                    continue;
                auto const value_same =
                    keys_equivalent(from, value, in, other_value, depth + 1, depth_max, checks);
                if (!value_same) [[unlikely]]
                    return std::unexpected(value_same.error());
                count += *value_same;
            }
            return count;
        };
        heads::decoder<Checked> d{std::string_view(std::span(a).subspan(h->at)), checks};
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            std::size_t const key = a.size() - d.encoded.size();
            if (auto const r = skip(d); !r) [[unlikely]]
                return std::unexpected(r.error());
            std::size_t const value = a.size() - d.encoded.size();
            if (auto const r = skip(d); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const own = pairs_counted(first, key, value, first, a, h->at);
            if (!own) [[unlikely]]
                return std::unexpected(own.error());
            auto const other = pairs_counted(first, key, value, second, b, k->at);
            if (!other) [[unlikely]]
                return std::unexpected(other.error());
            if (*own != *other)
                return false;
        }
        return true;
    }
    case major_type::tag:
        if (h->argument != k->argument)
            return false;
        return keys_equivalent(first, h->at, second, k->at, depth + 1, depth_max, checks);
    case major_type::simple_float:
        break;
    }
    constexpr std::uint8_t half = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice =
        std::to_underlying(rfc8949::simple_float_information::double_precision_float);
    bool const h_float = h->info >= half && h->info <= twice;
    bool const k_float = k->info >= half && k->info <= twice;
    if (h_float != k_float)
        return false;
    if (!h_float)
        return h->argument == k->argument;
    heads::float_key const p = heads::float_key_of(h->info, h->argument);
    heads::float_key const q = heads::float_key_of(k->info, k->argument);
    if (p.nan || q.nan) {
        constexpr std::uint64_t significand =
            (std::uint64_t{1} << heads::double_precision.significand_bits) - 1u;
        return p.nan && q.nan && (p.widened & significand) == (q.widened & significand);
    }
    return p.value == q.value;
}

template <class Message, bool Checked>
std::expected<void, error>
validity::check_sorted_keys_unique(Message &message, std::size_t const first_key, std::uint64_t const count,
                                   std::size_t const depth, std::size_t const depth_max,
                                   limit_checks<Checked> const checks)
{
    std::string_view encoded;
    if constexpr (std::same_as<Message, std::string_view const>)
        encoded = message;
    else
        encoded = message.encoded;
    well_formedness::no_marks marks;
    heads::decoder<Checked> walk{std::string_view(std::span(encoded).subspan(first_key)), checks};
    std::size_t previous = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::size_t const key = encoded.size() - walk.encoded.size();
        if (i != 0) {
            auto const equal = keys_equivalent(message, previous, message, key, depth, depth_max, checks);
            if (!equal) [[unlikely]]
                return std::unexpected(equal.error());
            if (error const c = check_key_unique(*equal).error_or(error{}); c != error{}) [[unlikely]]
                return std::unexpected(c);
        }
        previous = key;
        if (auto const r = well_formedness::item_skip(walk, marks); !r) [[unlikely]]
            return r;
        if (auto const r = well_formedness::item_skip(walk, marks); !r) [[unlikely]]
            return r;
    }
    return {};
}

} // namespace cbor
