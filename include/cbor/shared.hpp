#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "binding.hpp"
#include "error.hpp"
#include "head.hpp"
#include "owning_ref.hpp"

namespace cbor
{

struct lazy;

struct item;

template <std::size_t DepthMax>
struct lazy_elements;

template <std::size_t DepthMax>
struct lazy_entries;

class value_sharing
{
    struct top_level_item;

    struct sharing_decoder;

    struct resolved;

    static std::expected<std::size_t, error> shared_resolve(top_level_item &top_level, std::size_t at);

    static std::expected<item *, error> item_resolve(top_level_item &top_level, std::size_t at);

    static std::expected<resolved, error> container_resolve(std::shared_ptr<top_level_item> source, std::size_t offset);

    template <std::size_t DepthMax, class Match>
    static result<lazy> key_find(resolved const &found, Match const &match);

    friend struct lazy;

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

    template <std::size_t DepthMax>
    friend std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &encoded);

    friend class decoding;

    friend class jsonpath;

#ifdef __cpp_impl_reflection
    friend class generic;

    template <class>
    friend class databind;
#endif
};

struct lazy {
    std::shared_ptr<value_sharing::top_level_item> top_level;
    std::size_t offset;

    template <std::same_as<std::string> Encoded>
    static result<lazy> from(Encoded &&encoded);
    static result<lazy> from(std::string_view encoded);
    static result<lazy> from(std::shared_ptr<std::string const> encoded);
    static result<lazy> from(std::shared_ptr<void const> owner, std::string_view encoded);

    template <std::size_t DepthMax = 128>
    result<lazy> at(std::string_view key) const;

    template <std::size_t DepthMax = 128>
    result<lazy> at(std::int64_t index) const;

    template <class T>
        requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                 std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
    result<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                                   std::is_same_v<T, typed_array>,
                               owning_ref<T>, T>> get() const;

    template <std::size_t DepthMax = 128>
    result<lazy_elements<DepthMax>> elements() const;

    template <std::size_t DepthMax = 128>
    result<lazy_entries<DepthMax>> entries() const;
};

}

#include "item.hpp"

namespace cbor
{

struct value_sharing::top_level_item {
    std::shared_ptr<void const> owner;
    std::string_view encoded;
    std::vector<lazy> sharedrefs;
    std::size_t high_water_mark;
    std::deque<item> items{};
    std::vector<std::pair<std::size_t, item *>> item_offsets{};

    std::expected<item *, error> entry(std::size_t const offset)
    {
        auto const known = std::ranges::lower_bound(item_offsets, offset, {}, &std::pair<std::size_t, item *>::first);
        if (known != item_offsets.end() && known->first == offset)
            return known->second;
        auto const h = heads::raw_head_read(encoded, offset);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        item &placeholder = items.emplace_back(item{h->major, h->info, h->argument, lazy{{}, offset}});
        item_offsets.insert(known, {offset, &placeholder});
        return &placeholder;
    }

    std::size_t mark(heads::decoder const &at)
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

    std::expected<lazy, error> sharedref_decode(heads::decoder &d, std::size_t const item_at) const
    {
        auto const n = d.head_decode();
        if (!n) [[unlikely]]
            return std::unexpected(n.error());
        if (n->major != major_type::unsigned_integer) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        if (n->argument >= sharedrefs.size()) [[unlikely]]
            return std::unexpected(error::sharedref_index_not_marked);
        lazy const &found = sharedrefs.at(static_cast<std::size_t>(n->argument));
        if (found.offset >= item_at) [[unlikely]]
            return std::unexpected(error::sharedref_not_complete);
        return found;
    }
};

struct value_sharing::sharing_decoder : heads::decoder {
    top_level_item message;
};

struct value_sharing::resolved {
    std::shared_ptr<top_level_item> source;
    heads::head h;
    heads::decoder d;
};

inline std::expected<std::size_t, error> value_sharing::shared_resolve(top_level_item &top_level, std::size_t at)
{
    std::size_t item_at = at;
    for (;;) {
        auto const h = heads::raw_head_read(top_level.encoded, at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag || h->info == std::to_underlying(heads::additional_information::indefinite_length))
            return at;
        if (h->argument == std::to_underlying(heads::tag_number::shareable)) {
            top_level.mark(heads::decoder{top_level.encoded.substr(h->at)});
            at = h->at;
            continue;
        }
        if (h->argument != std::to_underlying(heads::tag_number::sharedref))
            return at;
        heads::decoder d{top_level.encoded.substr(h->at)};
        auto const found = top_level.sharedref_decode(d, item_at);
        if (!found) [[unlikely]]
            return std::unexpected(found.error());
        at = found->offset;
        item_at = at;
    }
}

inline std::expected<item *, error> value_sharing::item_resolve(top_level_item &top_level, std::size_t const at)
{
    auto const node = shared_resolve(top_level, at);
    if (!node) [[unlikely]]
        return std::unexpected(node.error());
    return top_level.entry(*node);
}

inline std::expected<value_sharing::resolved, error> value_sharing::container_resolve(std::shared_ptr<top_level_item> source,
                                                                                      std::size_t offset)
{
    for (;;) {
        auto const at = shared_resolve(*source, offset);
        if (!at) [[unlikely]]
            return std::unexpected(at.error());
        heads::decoder d{source->encoded.substr(*at)};
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag || h->argument != std::to_underlying(heads::tag_number::encoded_cbor_data_item))
            return resolved{source, *h, d};
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (r->major != major_type::byte_string) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        auto const embedded = d.byte_string_decode(r->argument);
        if (!embedded) [[unlikely]]
            return std::unexpected(embedded.error());
        source = std::make_shared<top_level_item>(source->owner, *embedded, std::vector<lazy>{}, 0);
        offset = 0;
    }
}

}
