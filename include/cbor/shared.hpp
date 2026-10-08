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
#include "item_end.hpp"
#include "owning_ref.hpp"
#include "validity.hpp"

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

    template <std::size_t DepthMax>
    static std::expected<std::pair<item *, std::size_t>, error> item_decode(top_level_item &top_level, std::size_t at,
                                                                           std::size_t depth);

    static std::expected<resolved, error> container_resolve(std::shared_ptr<top_level_item> source, std::size_t offset);

    template <std::size_t DepthMax, class Match>
    static std::expected<lazy, error> key_find(resolved const &found, Match const &match);

    friend class validity;

    friend struct lazy;

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

    template <std::size_t DepthMax>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
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
    static std::expected<lazy, error> from(Encoded &&encoded);
    static std::expected<lazy, error> from(std::string_view encoded);
    static std::expected<lazy, error> from(std::shared_ptr<std::string const> encoded);
    static std::expected<lazy, error> from(std::shared_ptr<void const> owner, std::string_view encoded);
    template <class Encoded>
        requires std::same_as<std::remove_const_t<Encoded>, std::string>
    static std::expected<lazy, error> from(std::shared_ptr<void const> owner, Encoded &&encoded) = delete;

    template <std::size_t DepthMax = validity::nesting_depth_default>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    std::expected<lazy, error> at(std::string_view key) const;

    template <std::size_t DepthMax = validity::nesting_depth_default>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    std::expected<lazy, error> at(std::int64_t index) const;

    template <class T>
        requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                 std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value> ||
                 std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
    std::expected<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                                   std::is_same_v<T, typed_array>,
                               owning_ref<T>, T>, error> get() const;

    template <std::size_t DepthMax = validity::nesting_depth_default>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    std::expected<lazy_elements<DepthMax>, error> elements() const;

    template <std::size_t DepthMax = validity::nesting_depth_default>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    std::expected<lazy_entries<DepthMax>, error> entries() const;

    template <std::size_t DepthMax = validity::nesting_depth_default, class Self>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value() &&
                 std::is_lvalue_reference_v<Self>)
    std::expected<std::reference_wrapper<item const>, error> decode(this Self &&self);
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
        auto const known = item_offsets.empty() || item_offsets.back().first < offset
                               ? item_offsets.end()
                               : std::ranges::lower_bound(item_offsets, offset, {}, &std::pair<std::size_t, item *>::first);
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
        if (error const c =
                validity::check_tag_content(std::to_underlying(rfc8949::tag_number::sharedref), n->major, n->info)
                    .error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        auto const index = validity::check_sharedref_index(n->argument, sharedrefs.size());
        if (!index) [[unlikely]]
            return std::unexpected(index.error());
        lazy const &found = sharedrefs[*index];
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
        if (h->major != major_type::tag)
            return at;
        if (h->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
            top_level.mark(heads::decoder{std::string_view(std::span(top_level.encoded).subspan(h->at))});
            at = h->at;
            continue;
        }
        if (h->argument != std::to_underlying(rfc8949::tag_number::sharedref))
            return at;
        heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(h->at))};
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
    validity::throw_logic_error_if_null(source, "cbor::lazy: the lazy holds no top-level item");
    for (;;) {
        auto const at = shared_resolve(*source, offset);
        if (!at) [[unlikely]]
            return std::unexpected(at.error());
        heads::decoder d{std::string_view(std::span(source->encoded).subspan(*at))};
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag ||
            h->argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return resolved{source, *h, d};
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

template <std::size_t DepthMax, class First, class Second>
std::expected<bool, error> validity::keys_equivalent(First &first, std::size_t const first_at, Second &second,
                                                     std::size_t const second_at, std::size_t const depth)
{
    if (auto const r = check_nesting_depth(depth, DepthMax); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const encoded_of = []<class Message>(Message &message) -> std::string_view {
        if constexpr (std::same_as<Message, std::string_view const>)
            return message;
        else
            return message.encoded;
    };
    auto const resolve = []<class Message>(Message &message,
                                           std::size_t const at) -> std::expected<std::size_t, error> {
        if constexpr (std::same_as<Message, std::string_view const>)
            return at;
        else
            return value_sharing::shared_resolve(message, at);
    };
    auto const skip = []<class Message>(Message &message, heads::decoder &d, std::size_t const level) {
        if constexpr (std::same_as<Message, std::string_view const>) {
            well_formedness::no_marks none;
            return well_formedness::item_skip<DepthMax>(d, none, level);
        } else {
            return well_formedness::item_skip<DepthMax>(d, message, level);
        }
    };
    std::string_view const a = encoded_of(first);
    std::string_view const b = encoded_of(second);
    auto const x = resolve(first, first_at);
    if (!x) [[unlikely]]
        return std::unexpected(x.error());
    auto const y = resolve(second, second_at);
    if (!y) [[unlikely]]
        return std::unexpected(y.error());
    auto const h = heads::raw_head_read(a, *x);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    auto const k = heads::raw_head_read(b, *y);
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
        heads::decoder d{std::string_view(std::span(a).subspan(h->at))};
        auto const s = d.byte_string_decode(h->argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        heads::decoder e{std::string_view(std::span(b).subspan(k->at))};
        auto const t = e.byte_string_decode(k->argument);
        if (!t) [[unlikely]]
            return std::unexpected(t.error());
        return *s == *t;
    }
    case major_type::array: {
        if (h->argument != k->argument)
            return false;
        heads::decoder d{std::string_view(std::span(a).subspan(h->at))};
        heads::decoder e{std::string_view(std::span(b).subspan(k->at))};
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const equal = keys_equivalent<DepthMax>(first, a.size() - d.encoded.size(), second,
                                                         b.size() - e.encoded.size(), depth + 1);
            if (!equal || !*equal)
                return equal;
            if (auto const r = skip(first, d, depth + 1); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = skip(second, e, depth + 1); !r) [[unlikely]]
                return std::unexpected(r.error());
        }
        return true;
    }
    case major_type::map: {
        if (h->argument != k->argument)
            return false;
        if (auto const r = check_keys_unique<DepthMax>(first, h->at, h->argument, depth + 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (auto const r = check_keys_unique<DepthMax>(second, k->at, k->argument, depth + 1); !r)
            [[unlikely]]
            return std::unexpected(r.error());
        heads::decoder d{std::string_view(std::span(a).subspan(h->at))};
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            std::size_t const key = a.size() - d.encoded.size();
            if (auto const r = skip(first, d, depth + 1); !r) [[unlikely]]
                return std::unexpected(r.error());
            std::size_t const value = a.size() - d.encoded.size();
            if (auto const r = skip(first, d, depth + 1); !r) [[unlikely]]
                return std::unexpected(r.error());
            bool paired = false;
            heads::decoder e{std::string_view(std::span(b).subspan(k->at))};
            for (std::uint64_t j = 0; j < k->argument && !paired; ++j) {
                std::size_t const other = b.size() - e.encoded.size();
                if (auto const r = skip(second, e, depth + 1); !r) [[unlikely]]
                    return std::unexpected(r.error());
                std::size_t const other_value = b.size() - e.encoded.size();
                if (auto const r = skip(second, e, depth + 1); !r) [[unlikely]]
                    return std::unexpected(r.error());
                auto const key_same = keys_equivalent<DepthMax>(first, key, second, other, depth + 1);
                if (!key_same) [[unlikely]]
                    return key_same;
                if (!*key_same)
                    continue;
                auto const value_same =
                    keys_equivalent<DepthMax>(first, value, second, other_value, depth + 1);
                if (!value_same || !*value_same)
                    return value_same;
                paired = true;
            }
            if (!paired)
                return false;
        }
        return true;
    }
    case major_type::tag:
        if (h->argument != k->argument)
            return false;
        return keys_equivalent<DepthMax>(first, h->at, second, k->at, depth + 1);
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

template <std::size_t DepthMax, class Message>
std::expected<void, error> validity::check_keys_unique(Message &message, std::size_t const first_key,
                                                       std::uint64_t const count, std::size_t const depth)
{
    if (count < 2)
        return {};
    std::string_view encoded;
    if constexpr (std::same_as<Message, std::string_view const>)
        encoded = message;
    else
        encoded = message.encoded;
    well_formedness::no_marks none;
    auto &marks = [&]() -> auto & {
        if constexpr (std::same_as<Message, std::string_view const>)
            return none;
        else
            return message;
    }();
    std::array<std::size_t, stack_keys> on_stack;
    std::vector<std::size_t> on_heap;
    std::span<std::size_t> keys;
    if (count <= stack_keys) [[likely]]
        keys = std::span(on_stack).first(static_cast<std::size_t>(count));
    else {
        if (count > encoded.size() - first_key) [[unlikely]]
            return std::unexpected(error::too_little_data);
        on_heap.resize(static_cast<std::size_t>(count));
        keys = on_heap;
    }
    heads::decoder walk{std::string_view(std::span(encoded).subspan(first_key))};
    for (std::size_t &k : keys) {
        k = encoded.size() - walk.encoded.size();
        if (auto const r = well_formedness::item_skip<DepthMax>(walk, marks, depth); !r) [[unlikely]]
            return r;
        if (auto const r = well_formedness::item_skip<DepthMax>(walk, marks, depth); !r) [[unlikely]]
            return r;
    }
    return check_keys_unique<DepthMax>(message, std::span<std::size_t const>(keys), depth);
}

template <std::size_t DepthMax, class Message>
std::expected<void, error> validity::check_keys_unique(Message &message,
                                                       std::span<std::size_t const> const key_offsets,
                                                       std::size_t const depth)
{
    if (key_offsets.size() < 2)
        return {};
    std::string_view encoded;
    if constexpr (std::same_as<Message, std::string_view const>)
        encoded = message;
    else
        encoded = message.encoded;
    struct key {
        std::size_t at;
        major_type major;
        std::uint64_t argument;
        std::string_view payload;
        bool direct;
    };
    auto const key_read = [encoded](std::size_t const at) -> std::expected<key, error> {
        auto const h = heads::raw_head_read(encoded, at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        key k{at, h->major, h->argument, {}, false};
        bool const definite_string =
            (h->major == major_type::text_string || h->major == major_type::byte_string) &&
            check_definite_length(h->major, h->info).has_value();
        k.direct = definite_string || h->major == major_type::unsigned_integer ||
                   h->major == major_type::negative_integer;
        if (definite_string) {
            heads::decoder d{std::string_view(std::span(encoded).subspan(h->at))};
            auto const s = d.byte_string_decode(h->argument);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            k.payload = *s;
        }
        return k;
    };
    if (key_offsets.size() <= stack_keys) [[likely]] {
        std::array<std::uint64_t, stack_keys> prints;
        bool direct = true;
        for (std::size_t i = 0; i < key_offsets.size(); ++i) {
            auto const k = key_read(key_offsets[i]);
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            direct &= k->direct;
            std::span<char const> const bytes(k->payload);
            std::uint64_t first = 0;
            std::uint64_t last = 0;
            if (bytes.size() >= sizeof(std::uint64_t)) {
                first = heads::unsigned_read<std::uint64_t>(bytes.first<sizeof(std::uint64_t)>());
                last = heads::unsigned_read<std::uint64_t>(bytes.last<sizeof(std::uint64_t)>());
            } else {
                for (char const c : bytes)
                    first = first << 8 | static_cast<std::uint8_t>(c);
            }
            prints[i] = first ^ std::rotl(last, 29) ^
                        ((k->argument << 3 | std::to_underlying(k->major)) * 0x9e3779b97f4a7c15u);
        }
        std::span<std::uint64_t> const sorted = std::span(prints).first(key_offsets.size());
        std::ranges::sort(sorted);
        if (direct && std::ranges::adjacent_find(sorted) == sorted.end()) [[likely]]
            return {};
    }
    std::array<key, stack_keys> on_stack;
    std::vector<key> on_heap;
    std::span<key> keys;
    if (key_offsets.size() <= stack_keys) [[likely]]
        keys = std::span(on_stack).first(key_offsets.size());
    else {
        on_heap.resize(key_offsets.size());
        keys = on_heap;
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        auto const k = key_read(key_offsets[i]);
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        keys[i] = *k;
    }
    auto const others = std::ranges::partition(keys, &key::direct);
    std::span<key> const direct = keys.first(keys.size() - others.size());
    auto const before = [](key const &x, key const &y) {
        if (x.major != y.major)
            return x.major < y.major;
        if (x.argument != y.argument)
            return x.argument < y.argument;
        return x.payload < y.payload;
    };
    std::ranges::sort(direct, before);
    auto const same = [](key const &x, key const &y) {
        return x.major == y.major && x.argument == y.argument && x.payload == y.payload;
    };
    if (error const c = check_key_unique(std::ranges::adjacent_find(direct, same) != direct.end())
                            .error_or(error{});
        c != error{}) [[unlikely]]
        return std::unexpected(c);
    for (std::size_t i = direct.size(); i < keys.size(); ++i)
        for (std::size_t j = 0; j < keys.size(); ++j)
            if (j < direct.size() || j > i) {
                auto const equal = keys_equivalent<DepthMax>(message, keys[i].at, message, keys[j].at, depth);
                if (!equal) [[unlikely]]
                    return std::unexpected(equal.error());
                if (error const c = check_key_unique(*equal).error_or(error{}); c != error{}) [[unlikely]]
                    return std::unexpected(c);
            }
    return {};
}

} // namespace cbor
