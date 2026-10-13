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
#include <thread>
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

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_sharedref_chain_ends(std::size_t const references_followed, std::size_t const marked)
    {
        if (references_followed > marked) [[unlikely]]
            return std::unexpected(error::sharedref_not_complete);
        return {};
    }

    template <std::invocable MarksRead, class Projection, bool Checked>
    static std::expected<void, error> check_tag_content(std::uint64_t tag, std::string_view encoded,
                                                        validity::offset content_at, MarksRead marks_read,
                                                        Projection offset_of, validity::limit_checks<Checked> checks);

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
    static std::expected<validity::offset, error> shared_resolve(top_level_item &top_level, validity::offset at,
                                                            validity::limit_checks<Checked> checks);

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

    friend item const &shared_value(std::shared_ptr<item const> const &tree, item const &reference);

    friend std::expected<std::variant<std::reference_wrapper<item const>, lazy>, error>
    shared_value_resolve(std::shared_ptr<item const> const &tree, item const &reference);

    friend item const &encoded_cbor_data_item(std::shared_ptr<item const> const &tree, item const &tagged);

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

template <std::invocable MarksRead, class Projection, bool Checked>
std::expected<void, error>
value_sharing::check_tag_content(std::uint64_t const tag, std::string_view const encoded,
                                 validity::offset const content_at, MarksRead const marks_read,
                                 Projection const offset_of, validity::limit_checks<Checked> const checks)
{
    validity::offset at = content_at;
    std::size_t references_followed = 0;
    for (;;) {
        auto const c = heads::raw_head_read(encoded, at, checks);
        if (!c) [[unlikely]]
            return std::unexpected(c.error());
        if (c->major == major_type::tag && c->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
            at = c->at;
            continue;
        }
        if (c->major != major_type::tag || c->argument != std::to_underlying(rfc8949::tag_number::sharedref))
            return validity::check_tag_content(tag, c->major, c->info);
        auto const n = heads::raw_head_read(encoded, c->at, checks);
        if (!n) [[unlikely]]
            return std::unexpected(n.error());
        if (error const e = validity::check_tag_content(c->argument, n->major, n->info).error_or(error{}); e != error{})
            [[unlikely]]
            return std::unexpected(e);
        auto const &marks = std::invoke(marks_read);
        auto const marked = check_sharedref_index(
            n->argument, static_cast<std::size_t>(std::ranges::distance(
                             std::ranges::begin(marks), std::ranges::upper_bound(marks, at, {}, offset_of))));
        if (!marked) [[unlikely]]
            return std::unexpected(marked.error());
        if (auto const ends = check_sharedref_chain_ends(++references_followed, std::ranges::size(marks)); !ends)
            [[unlikely]]
            return std::unexpected(ends.error());
        validity::offset const shared = std::invoke(offset_of, marks[std::to_underlying(*marked)]);
        if (shared >= at) [[unlikely]]
            return std::unexpected(error::sharedref_not_complete);
        at = shared;
    }
}

struct value_sharing::top_level_item {
    std::shared_ptr<void const> owner;
    std::string_view encoded;
    std::vector<lazy> sharedrefs;
    validity::offset high_water_mark;
#ifndef NDEBUG
    std::thread::id owner_thread = std::this_thread::get_id();
#endif
    std::vector<validity::offset> sharedrefs_resolved;
    std::string_view sharedrefs_unread;
    std::uint64_t sharedrefs_pending = 1;
    bool sharedrefs_started = false;

    std::vector<lazy> const &sharedrefs_read(validity::offset const up_to = validity::npos)
    {
#ifndef NDEBUG
        if (owner_thread == std::thread::id{})
            owner_thread = std::this_thread::get_id();
        if (owner_thread != std::this_thread::get_id()) [[unlikely]]
            throw std::logic_error("a lazy is used in a thread that does not own it; move it with cbor::transfer");
#endif
        if (!sharedrefs_started) {
            sharedrefs_unread = encoded;
            sharedrefs_started = true;
        }
        heads::decoder<false> all{sharedrefs_unread, validity::limit_checks<false>{}};
        while (sharedrefs_pending != 0 && validity::distance(encoded, all.encoded) <= up_to) {
            --sharedrefs_pending;
            auto const h = all.head_decode();
            if (!h || !validity::check_definite_length(h->major, h->info)) [[unlikely]] {
                sharedrefs_pending = 0;
                break;
            }
            std::uint64_t added = 0;
            switch (h->major) {
            case major_type::byte_string:
            case major_type::text_string:
                if (!all.byte_string_decode(h->argument)) [[unlikely]]
                    sharedrefs_pending = 0;
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
            if (sharedrefs_pending != 0 || added != 0)
                sharedrefs_pending =
                    validity::checked_add(sharedrefs_pending, added).value_or(std::numeric_limits<std::uint64_t>::max());
        }
        sharedrefs_unread = all.encoded;
        return sharedrefs;
    }

    template <bool Checked>
    std::size_t mark(heads::decoder<Checked> const &at)
    {
        validity::offset const offset = validity::distance(encoded, at.encoded);
        if (offset > high_water_mark) {
            sharedrefs.push_back(lazy{{}, offset, {}});
            high_water_mark = offset;
            return sharedrefs.size() - 1;
        }
        auto const known = std::ranges::lower_bound(sharedrefs, offset, {}, &lazy::offset);
        return static_cast<std::size_t>(std::ranges::distance(sharedrefs.begin(), known));
    }

    template <bool Checked>
    static std::expected<lazy, error>
    sharedref_decode(heads::decoder<Checked> &d, validity::offset const reference_at, validity::offset const item_at,
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
        auto const marked = check_sharedref_index(
            n->argument, static_cast<std::size_t>(std::ranges::distance(marks.begin(), before)));
        if (!marked) [[unlikely]]
            return std::unexpected(marked.error());
        lazy const &found = marks[std::to_underlying(*marked)];
        if (found.offset >= item_at) [[unlikely]]
            return std::unexpected(error::sharedref_not_complete);
        return found;
    }
};

struct value_sharing::zone {
    using link = std::pair<item const *, item const *>;

    struct enclosing_tag {
        std::uint64_t number;
        validity::offset content_at;
    };

    struct sharedref {
        item const *reference;
        item const *shareable;
        top_level_item *source;
        validity::offset shared_at;
        std::optional<enclosing_tag> enclosing;
    };

    std::shared_ptr<top_level_item> top_level;
    std::pmr::monotonic_buffer_resource memory;
    std::pmr::forward_list<top_level_item> embedded{&memory};
    std::vector<sharedref> sharedrefs;
    std::vector<link> encoded_cbor_data_items;
    std::size_t marked = 0;

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

    static std::expected<std::variant<std::reference_wrapper<item const>, lazy>, error>
    chain_followed(std::shared_ptr<item const> const &tree, item const &reference, char const *const what)
    {
        zone const &z = zone_found(tree, what);
        auto const key = &zone::sharedref::reference;
        zone::sharedref const &first = link_found(z.sharedrefs, reference, key, what);
        if (first.enclosing) {
            auto const content = validity::limits_apply(
                limits.load(), [&first]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
                    return check_tag_content(
                        first.enclosing->number, first.source->encoded, first.enclosing->content_at,
                        [&first]() -> auto const & { return first.source->sharedrefs_read(); }, &lazy::offset, checks);
                });
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
        }
        zone::sharedref const *at = &first;
        std::size_t references_followed = 0;
        for (;;) {
            if (at->shareable == nullptr)
                return lazy{z.top_level, at->shared_at};
            item const *passed = at->shareable;
            while (passed->type == cbor::type::tag &&
                   (passed->via.tag.number == std::to_underlying(rfc8949::tag_number::shareable) ||
                    passed->via.tag.number == std::to_underlying(rfc8949::tag_number::self_described_cbor)))
                passed = passed->via.tag.content;
            item const &shared = *passed;
            if (shared.type != cbor::type::tag ||
                shared.via.tag.number != std::to_underlying(rfc8949::tag_number::sharedref))
                return std::cref(shared);
            if (auto const ends = check_sharedref_chain_ends(++references_followed, z.marked); !ends) [[unlikely]]
                return std::unexpected(ends.error());
            at = &link_found(z.sharedrefs, shared, key, what);
        }
    }
};

inline item const &shared_value(std::shared_ptr<item const> const &tree, item const &reference)
{
    constexpr char const *what = "cbor::shared_value: the item is not a tag 29 of this decoded tree";
    value_sharing::zone const &z = value_sharing::zone_owner::zone_found(tree, what);
    auto const &found =
        value_sharing::zone_owner::link_found(z.sharedrefs, reference, &value_sharing::zone::sharedref::reference, what);
    validity::throw_logic_error_if(found.shareable == nullptr,
                                   "cbor::shared_value: the tag 29 names an item before the decoded tree; use "
                                   "cbor::shared_value_resolve");
    return *found.shareable;
}

inline std::expected<std::variant<std::reference_wrapper<item const>, lazy>, error>
shared_value_resolve(std::shared_ptr<item const> const &tree, item const &reference)
{
    return value_sharing::zone_owner::chain_followed(
        tree, reference, "cbor::shared_value_resolve: the item is not a tag 29 of this decoded tree");
}

inline item const &encoded_cbor_data_item(std::shared_ptr<item const> const &tree, item const &tagged)
{
    constexpr char const *what = "cbor::encoded_cbor_data_item: the item is not a tag 24 of this decoded tree";
    value_sharing::zone const &z = value_sharing::zone_owner::zone_found(tree, what);
    return *value_sharing::zone_owner::link_found(z.encoded_cbor_data_items, tagged,
                                                  &value_sharing::zone::link::first, what)
                .second;
}

static_assert(std::is_trivially_destructible_v<item>,
              "The zone runs no destructor of an item, so an item must have no destructor to run.");

template <bool Checked>
struct value_sharing::tree_decoder {
    zone &decoded;
    top_level_item &top_level;
    std::vector<item const *> &shareables;
    heads::decoder<Checked> d;
    validity::offset start;
    std::size_t depth_max;
    std::optional<std::size_t> mark_next;
    item *root = nullptr;
    std::size_t open = 0;
    std::optional<zone::enclosing_tag> sharedref_enclosing{};

    std::expected<item *, error> item_decode(item *storage, std::size_t depth, std::optional<std::size_t> mark);

    std::expected<item *, error> content_decode(item *storage, heads::head h, std::size_t depth,
                                                std::optional<std::size_t> mark);

    CBOR_ALWAYS_INLINE inline std::expected<void, error> scalar_decode(item &node, heads::head h);

    CBOR_NEVER_INLINE std::expected<item *, error> bignum_decode(item &node, std::uint64_t tag, std::size_t depth);

    CBOR_NEVER_INLINE error bignum_content_check(std::uint64_t tag);

    std::size_t marks_counted();
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
    validity::offset item_at;
};

template <bool Checked>
std::expected<validity::offset, error> value_sharing::shared_resolve(top_level_item &top_level, validity::offset at,
                                                                validity::limit_checks<Checked> const checks)
{
    validity::offset item_at = at;
    std::size_t first = std::numeric_limits<std::size_t>::max();
    std::size_t references_followed = 0;
    for (;;) {
        auto const h = heads::raw_head_read(top_level.encoded, at, checks);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag) {
            if (first < top_level.sharedrefs_resolved.size())
                top_level.sharedrefs_resolved[first] = at;
            return at;
        }
        if (h->argument == std::to_underlying(rfc8949::tag_number::shareable) ||
            h->argument == std::to_underlying(rfc8949::tag_number::self_described_cbor)) {
            at = h->at;
            continue;
        }
        if (h->argument != std::to_underlying(rfc8949::tag_number::sharedref)) {
            if (first < top_level.sharedrefs_resolved.size())
                top_level.sharedrefs_resolved[first] = at;
            return at;
        }
        heads::decoder<Checked> d{validity::subspan(top_level.encoded, h->at), checks};
        std::vector<lazy> const &marks = top_level.sharedrefs_read(at);
        auto const found = top_level_item::sharedref_decode(d, at, item_at, marks);
        if (!found) [[unlikely]]
            return std::unexpected(found.error());
        if (auto const ends = check_sharedref_chain_ends(++references_followed, marks.size()); !ends) [[unlikely]]
            return std::unexpected(ends.error());
        auto const position = static_cast<std::size_t>(std::ranges::distance(
            marks.begin(), std::ranges::lower_bound(marks, found->offset, {}, &lazy::offset)));
        if (top_level.sharedrefs_resolved.size() < marks.size())
            top_level.sharedrefs_resolved.resize(marks.size(), validity::npos);
        if (top_level.sharedrefs_resolved[position] != validity::npos) {
            if (first < top_level.sharedrefs_resolved.size())
                top_level.sharedrefs_resolved[first] = top_level.sharedrefs_resolved[position];
            return top_level.sharedrefs_resolved[position];
        }
        if (first == std::numeric_limits<std::size_t>::max())
            first = position;
        at = found->offset;
        item_at = at;
    }
}

template <bool Checked>
std::expected<value_sharing::resolved<Checked>, error>
value_sharing::container_resolve(std::shared_ptr<top_level_item> source, validity::offset offset,
                                 validity::limit_checks<Checked> const checks)
{
    validity::throw_logic_error_if_null(source, "cbor::lazy: the lazy holds no top-level item");
    for (;;) {
        auto const at = shared_resolve(*source, offset, checks);
        if (!at) [[unlikely]]
            return std::unexpected(at.error());
        heads::decoder<Checked> d{validity::subspan(source->encoded, *at), checks};
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag ||
            h->argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return resolved<Checked>{source, *h, d, *at};
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (error const c = validity::check_tag_content(h->argument, r->major, r->info).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        auto const embedded = d.byte_string_decode(r->argument);
        if (!embedded) [[unlikely]]
            return std::unexpected(embedded.error());
        source = std::make_shared<top_level_item>(source->owner, *embedded, std::vector<lazy>{}, validity::offset{});
        offset = validity::offset{};
    }
}

template <bool Checked>
std::expected<value_sharing::resolved<Checked>, error>
value_sharing::tag_content_resolve(std::shared_ptr<top_level_item> source, validity::offset offset,
                                   validity::limit_checks<Checked> const checks)
{
    std::size_t references_followed = 0;
    for (;;) {
        top_level_item const *const before = source.get();
        auto found = container_resolve(std::move(source), offset, checks);
        if (!found) [[unlikely]]
            return found;
        if (found->source.get() != before)
            references_followed = 0;
        else if (found->item_at < offset)
            if (auto const ends = check_sharedref_chain_ends(++references_followed, found->source->sharedrefs.size());
                !ends) [[unlikely]]
                return std::unexpected(ends.error());
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
    auto const content =
        shared_resolve(*found.source, validity::distance(found.source->encoded, found.d.encoded), found.d.checks);
    if (!content) [[unlikely]]
        return std::unexpected(content.error());
    heads::decoder<Checked> d{validity::subspan(found.source->encoded, *content), found.d.checks};
    auto const r = d.head_decode();
    if (!r) [[unlikely]]
        return std::unexpected(r.error());
    if (error const c = validity::check_tag_content(found.h.argument, r->major, r->info).error_or(error{});
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
    auto const resolve = [checks]<class Message>(Message &message,
                                                 offset const at) -> std::expected<offset, error> {
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
    if (h->major != k->major || heads::is_bignum(h->major, h->argument)) [[unlikely]] {
        auto const integer_of = [&]<class Message>(Message &message, std::string_view const encoded,
                                                   heads::raw_head const n)
            -> std::expected<std::optional<std::pair<bool, std::uint64_t>>, error> {
            if (n.major == major_type::unsigned_integer || n.major == major_type::negative_integer)
                return std::pair{n.major == major_type::negative_integer, n.argument};
            if (!heads::is_bignum(n.major, n.argument))
                return std::nullopt;
            auto const content = resolve(message, n.at);
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            heads::decoder<Checked> d{subspan(encoded, *content), checks};
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

template <class T>
inline constexpr bool is_thread_bound_v =
    std::same_as<T, lazy> || std::same_as<T, lazy_elements> || std::same_as<T, lazy_entries> ||
    std::same_as<T, lazy_elements::iterator> || std::same_as<T, lazy_entries::iterator>;

template <class T>
inline constexpr bool is_thread_bound_v<owning_ref<T>> = true;

template <class T>
struct sendable {
    T value;
};

template <class T>
inline constexpr bool is_sendable_v = !is_thread_bound_v<T>;

template <class T>
inline constexpr bool is_sendable_v<sendable<T>> = true;

inline sendable<lazy> transfer(lazy &&value)
{
    if (value.top_level.use_count() != 1) [[unlikely]]
        throw std::logic_error("cbor::transfer needs the only handle on the item in this thread");
#ifndef NDEBUG
    value.top_level->owner_thread = std::thread::id{};
#endif
    return {std::move(value)};
}

} // namespace cbor
