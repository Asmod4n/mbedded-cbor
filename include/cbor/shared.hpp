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
#include <forward_list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#ifdef __STDCPP_FLOAT128_T__
#include <stdfloat>
#endif
#include <string>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "rfc8746.hpp"
#include "rfc9535.hpp"
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
public:
    enum class index : std::size_t {};

    CBOR_ALWAYS_INLINE static constexpr std::expected<index, error>
    check_sharedref_index(std::uint64_t const argument, std::size_t const marked)
    {
        if (!std::in_range<std::size_t>(argument)) [[unlikely]]
            return std::unexpected(error::sharedref_index_out_of_range);
        if (argument >= marked) [[unlikely]]
            return std::unexpected(error::sharedref_index_not_marked);
        return index{static_cast<std::size_t>(argument)};
    }

private:
    struct top_level_item;

    struct zone;

    struct zone_owner;

    template <bool Checked>
    struct sharing_decoder;

    template <bool Checked>
    struct tree_decoder;

    template <bool Checked>
    struct resolved;

    template <bool Checked>
    static std::expected<resolved<Checked>, error> container_resolve(std::shared_ptr<top_level_item> source,
                                                                     validity::offset offset,
                                                                     validity::limit_checks<Checked> checks);

    template <bool Checked>
    static std::expected<resolved<Checked>, error> tag_content_resolve(std::shared_ptr<top_level_item> source,
                                                                       validity::offset offset,
                                                                       validity::limit_checks<Checked> checks);

    struct tag_content {
        validity::offset offset;
        std::string_view bytes;
    };

    template <bool Checked>
    static std::expected<tag_content, error> typed_array_bytes_read(resolved<Checked> const &found);

    static std::expected<std::string_view, error> typed_array_element_read(lazy const &node);

    template <bool Checked>
    static std::expected<resolved<Checked>, error> lazy_resolve(lazy const &node, validity::limit_checks<Checked> checks);

    template <bool Checked, std::integral Index>
    static std::expected<lazy, error> index_select(resolved<Checked> const &found, Index i);

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

    friend std::expected<std::reference_wrapper<item const>, error>
    encoded_cbor_data_item(std::shared_ptr<item const> const &tree, item const &tagged);

    friend struct lazy;

    friend struct lazy_elements;

    friend struct lazy_entries;


    friend class jsonpath;

#ifdef __cpp_impl_reflection
    friend class generic;

    template <class>
    friend class structs;
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
        iterator(std::shared_ptr<value_sharing::top_level_item> t, validity::offset const o, std::uint64_t const l,
                 limit_values const v, std::uint64_t const tag)
            : top_level(std::move(t)), offset(o), left(l), loaded(v), typed_array_tag(tag)
        {
        }

        std::shared_ptr<value_sharing::top_level_item> top_level;
        validity::offset offset{};
        std::uint64_t left{};
        error failure{};
        limit_values loaded{};
        std::uint64_t typed_array_tag{};

        friend struct lazy_elements;
    };

    iterator begin() const
    {
        return iterator{top_level, offset, count, loaded, typed_array_tag};
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
    lazy_elements(std::shared_ptr<value_sharing::top_level_item> t, validity::offset const o,
                  std::uint64_t const c, limit_values const v, std::uint64_t const tag)
        : top_level(std::move(t)), offset(o), count(c), loaded(v), typed_array_tag(tag)
    {
    }

    std::shared_ptr<value_sharing::top_level_item> top_level;
    validity::offset offset;
    std::uint64_t count;
    limit_values loaded;
    std::uint64_t typed_array_tag;

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
        iterator(std::shared_ptr<value_sharing::top_level_item> t, validity::offset const k, std::uint64_t const l,
                 limit_values const v)
            : top_level(std::move(t)), key(k), value(k), left(l), loaded(v)
        {
            value_find();
        }

        void value_find();

        std::shared_ptr<value_sharing::top_level_item> top_level;
        validity::offset key{};
        validity::offset value{};
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
    lazy_entries(std::shared_ptr<value_sharing::top_level_item> t, validity::offset const o, std::uint64_t const c,
                 limit_values const v)
        : top_level(std::move(t)), offset(o), count(c), loaded(v)
    {
    }

    std::shared_ptr<value_sharing::top_level_item> top_level;
    validity::offset offset;
    std::uint64_t count;
    limit_values loaded;

    friend struct lazy;
};

struct lazy {
    std::shared_ptr<value_sharing::top_level_item> top_level;
    validity::offset offset;
    std::uint64_t typed_array_tag{};

    static std::expected<lazy, error> copy(std::string_view encoded);
    template <std::same_as<std::string> Encoded>
    static std::expected<lazy, error> copy(Encoded &&encoded);
    static std::expected<lazy, error> view(std::shared_ptr<std::string const> encoded);

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

    std::expected<cbor::type, error> type() const;

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
#ifdef __STDCPP_FLOAT128_T__
                 || std::is_same_v<T, std::float128_t>
#endif
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
};

struct value_sharing::zone {
    using link = std::pair<item const *, item const *>;

    std::shared_ptr<top_level_item> top_level;
    std::pmr::monotonic_buffer_resource memory;
    std::pmr::forward_list<top_level_item> embedded{&memory};
    std::vector<link> encoded_cbor_data_items;

    zone(std::shared_ptr<top_level_item> top, std::size_t const initial_size)
        : top_level(std::move(top)), memory(initial_size)
    {
    }
};

struct value_sharing::zone_owner {
    std::unique_ptr<zone> owned;

    void operator()(item const *) const noexcept
    {
    }

    static zone &zone_found(std::shared_ptr<item const> const &tree, char const *const what)
    {
        zone_owner const *const owner = std::get_deleter<zone_owner>(tree);
        validity::throw_logic_error_if(owner == nullptr || owner->owned == nullptr, what);
        return *owner->owned;
    }

    template <class Link, class Projection>
    static Link const &link_found(std::vector<Link> const &links, item const &from, Projection const key,
                                  char const *const what)
    {
        auto const found = std::ranges::lower_bound(links, &from, std::less<>{}, key);
        validity::throw_logic_error_if(found == links.end() || std::invoke(key, *found) != &from, what);
        return *found;
    }

};

inline std::expected<std::reference_wrapper<item const>, error>
encoded_cbor_data_item(std::shared_ptr<item const> const &tree, item const &tagged)
{
    constexpr char const *what = "cbor::encoded_cbor_data_item: the item is not a tag 24 of this decoded tree";
    value_sharing::zone const &z = value_sharing::zone_owner::zone_found(tree, what);
    item const *const embedded = value_sharing::zone_owner::link_found(z.encoded_cbor_data_items, tagged,
                                                                       &value_sharing::zone::link::first, what)
                                     .second;
    if (embedded == nullptr) [[unlikely]]
        return std::unexpected(error::incorrect_type);
    return std::cref(*embedded);
}

static_assert(std::is_trivially_destructible_v<item>,
              "The zone runs no destructor of an item, so an item must have no destructor to run.");

template <bool Checked>
struct value_sharing::tree_decoder {
    zone &decoded;
    top_level_item &top_level;
    heads::decoder<Checked> d;
    validity::offset start;
    std::size_t depth_max;
    std::size_t open = 0;

    std::expected<item *, error> item_decode(item *storage, std::size_t depth);

    std::expected<item *, error> content_decode(item *storage, heads::head h, std::size_t depth);

    CBOR_ALWAYS_INLINE inline std::expected<void, error> scalar_decode(item &node, heads::head h);

    CBOR_NEVER_INLINE std::expected<item *, error> bignum_decode(item &node, std::uint64_t tag, std::size_t depth);
};

template <bool Checked>
struct value_sharing::sharing_decoder : heads::decoder<Checked> {
    enum class slot : std::uint8_t { in_skipped_member, open, value_member };

    std::vector<slot> slots;

    void mark(heads::decoder<Checked> const &)
    {
        slots.push_back(slot::in_skipped_member);
    }
};

template <bool Checked>
struct value_sharing::resolved {
    std::shared_ptr<top_level_item> source;
    heads::head h;
    heads::decoder<Checked> d;
    validity::offset item_at;
};

template <bool Checked>
std::expected<value_sharing::resolved<Checked>, error>
value_sharing::container_resolve(std::shared_ptr<top_level_item> source, validity::offset offset,
                                 validity::limit_checks<Checked> const checks)
{
    validity::throw_logic_error_if_null(source, "cbor::lazy: the lazy holds no top-level item");
    for (;;) {
        validity::offset const at = heads::self_described_cbor_content_at(source->encoded, offset);
        heads::decoder<Checked> d{validity::subspan(source->encoded, at), checks};
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag ||
            h->argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return resolved<Checked>{source, *h, d, at};
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (error const c = validity::check_tag_content(h->argument, r->major).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        auto const embedded = d.byte_string_decode(r->argument);
        if (!embedded) [[unlikely]]
            return std::unexpected(embedded.error());
        source = std::make_shared<top_level_item>(source->owner, *embedded);
        offset = validity::offset{};
    }
}

template <bool Checked>
std::expected<value_sharing::resolved<Checked>, error>
value_sharing::tag_content_resolve(std::shared_ptr<top_level_item> source, validity::offset offset,
                                   validity::limit_checks<Checked> const checks)
{
    for (;;) {
        auto found = container_resolve(std::move(source), offset, checks);
        if (!found) [[unlikely]]
            return found;
        if (found->h.major != major_type::tag || !heads::is_tag_passed(found->h.argument)) [[likely]] {
            if (auto const r = validity::check_pending_items(found->h.major, found->h.info, found->h.argument,
                                                             found->d.encoded.size());
                !r) [[unlikely]]
                return std::unexpected(r.error());
            return found;
        }
        source = std::move(found->source);
        offset = validity::distance(source->encoded, found->d.encoded);
    }
}

template <bool Checked>
std::expected<value_sharing::tag_content, error> value_sharing::typed_array_bytes_read(resolved<Checked> const &found)
{
    if (found.h.major != major_type::tag) [[unlikely]]
        return std::unexpected(error::incorrect_type);
    if (error const r = validity::typed_array_check(found.h.argument, 0).error_or(error{}); r != error{})
        [[unlikely]]
        return std::unexpected(r);
    validity::offset const content =
        heads::self_described_cbor_content_at(found.source->encoded, validity::distance(found.source->encoded, found.d.encoded));
    heads::decoder<Checked> d{validity::subspan(found.source->encoded, content), found.d.checks};
    auto const r = d.head_decode();
    if (!r) [[unlikely]]
        return std::unexpected(r.error());
    if (error const c = validity::check_tag_content(found.h.argument, r->major).error_or(error{});
        c != error{}) [[unlikely]]
        return std::unexpected(c);
    auto const offset = validity::offset{validity::distance(found.source->encoded, d.encoded)};
    auto const bytes = d.byte_string_decode(r->argument);
    if (!bytes) [[unlikely]]
        return std::unexpected(bytes.error());
    if (error const c = validity::typed_array_check(found.h.argument, bytes->size()).error_or(error{});
        c != error{}) [[unlikely]]
        return std::unexpected(c);
    return tag_content{offset, *bytes};
}

}

namespace cbor
{

template <class First, class Second, bool Checked>
std::expected<bool, error> validity::keys_equivalent(First &first, offset const first_at, Second &second,
                                                     offset const second_at, std::size_t const depth,
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
    auto const skip = [](heads::decoder<Checked> &d) {
        well_formedness::no_marks none;
        return well_formedness::item_skip(d, none);
    };
    auto const resolve = []<class Message>(Message &message, offset const at) -> offset {
        if constexpr (std::same_as<Message, std::string_view const>)
            return at;
        else
            return heads::self_described_cbor_content_at(message.encoded, at);
    };
    std::string_view const a = encoded_of(first);
    std::string_view const b = encoded_of(second);
    auto const h = heads::raw_head_read(a, resolve(first, first_at), checks);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    auto const k = heads::raw_head_read(b, resolve(second, second_at), checks);
    if (!k) [[unlikely]]
        return std::unexpected(k.error());
    if (error const c = check_definite_length(h->major, h->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    if (error const c = check_definite_length(k->major, k->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    if (h->major != k->major || heads::is_bignum(h->major, h->argument)) [[unlikely]] {
        auto const integer_of = [&]<class Message>(Message &message, std::string_view const encoded,
                                                   heads::raw_head const n)
            -> std::expected<std::optional<std::pair<bool, std::uint64_t>>, error> {
            if (n.major == major_type::unsigned_integer || n.major == major_type::negative_integer)
                return std::pair{n.major == major_type::negative_integer, n.argument};
            if (!heads::is_bignum(n.major, n.argument))
                return std::nullopt;
            heads::decoder<Checked> d{subspan(encoded, resolve(message, n.at)), checks};
            auto const read = d.bignum_decode(n.argument);
            if (!read) [[unlikely]]
                return std::unexpected(read.error());
            if (!*read)
                return std::nullopt;
            return std::pair{n.argument == std::to_underlying(rfc8949::tag_number::negative_bignum), **read};
        };
        auto const x_value = integer_of(first, a, *h);
        if (!x_value) [[unlikely]]
            return std::unexpected(x_value.error());
        auto const y_value = integer_of(second, b, *k);
        if (!y_value) [[unlikely]]
            return std::unexpected(y_value.error());
        if (*x_value && *y_value)
            return **x_value == **y_value;
        if (h->major != k->major)
            return false;
    }
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return h->argument == k->argument;
    case major_type::byte_string:
    case major_type::text_string: {
        heads::decoder<Checked> d{subspan(a, h->at), checks};
        auto const s = d.byte_string_decode(h->argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        heads::decoder<Checked> e{subspan(b, k->at), checks};
        auto const t = e.byte_string_decode(k->argument);
        if (!t) [[unlikely]]
            return std::unexpected(t.error());
        return *s == *t;
    }
    case major_type::array: {
        if (h->argument != k->argument)
            return false;
        heads::decoder<Checked> d{subspan(a, h->at), checks};
        heads::decoder<Checked> e{subspan(b, k->at), checks};
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const equal = keys_equivalent(first, distance(a, d.encoded), second,
                                               distance(b, e.encoded), depth + 1, depth_max, checks);
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
        auto const pairs_counted = [&]<class From, class In>(From &from, offset const key,
                                                             offset const value, In &in,
                                                             std::string_view const in_encoded,
                                                             offset const in_at)
            -> std::expected<std::uint64_t, error> {
            std::uint64_t count = 0;
            heads::decoder<Checked> e{validity::subspan(in_encoded, in_at), checks};
            for (std::uint64_t j = 0; j < h->argument; ++j) {
                offset const other = distance(in_encoded, e.encoded);
                if (auto const r = skip(e); !r) [[unlikely]]
                    return std::unexpected(r.error());
                offset const other_value = distance(in_encoded, e.encoded);
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
        heads::decoder<Checked> d{validity::subspan(a, h->at), checks};
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            offset const key = distance(a, d.encoded);
            if (auto const r = skip(d); !r) [[unlikely]]
                return std::unexpected(r.error());
            offset const value = distance(a, d.encoded);
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

} // namespace cbor
