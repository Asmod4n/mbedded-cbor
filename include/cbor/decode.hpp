#ifndef CBOR_DECODE_HPP
#define CBOR_DECODE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <memory_resource>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "error.hpp"
#include "head.hpp"
#include "item.hpp"
#include "ref.hpp"
#include "rfc8949.hpp"
#include "validity.hpp"

namespace cbor
{

template <bool Checked>
struct tree_decoder;

struct zone {
    using link = std::pair<item const *, item const *>;

    std::pmr::monotonic_buffer_resource memory;
    std::vector<link> encoded_cbor_data_items;
    std::span<char> free{};

    explicit zone(std::size_t const initial_size) : memory(initial_size)
    {
    }

    std::span<char> bump_allocate(std::size_t const size, std::size_t const bytes_left)
    {
        if (size > free.size()) [[unlikely]] {
            std::size_t const chunk = size + std::min(validity::bump_chunk_size, bytes_left);
            free = std::span<char>(std::pmr::polymorphic_allocator<char>(&memory).allocate(chunk), chunk);
        }
        std::span<char> const taken = free.first(size);
        free = free.subspan(size);
        return taken;
    }
};

struct zone_owner {
    std::unique_ptr<zone> owned;

    void operator()(item const *) const noexcept
    {
    }

    static owning<item> tree_owned(item const *const root, std::unique_ptr<zone> decoded)
    {
        return owning<item>(std::shared_ptr<item const>(root, zone_owner{std::move(decoded)}));
    }

    static zone &zone_found(owning<item> const &tree, char const *const what)
    {
        zone_owner const *const owner = std::get_deleter<zone_owner>(tree.held);
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
encoded_cbor_data_item(owning<item> const &tree, item const &tagged)
{
    constexpr char const *what = "cbor::encoded_cbor_data_item: the item is not a tag 24 of this decoded tree";
    zone const &z = zone_owner::zone_found(tree, what);
    item const *const embedded = zone_owner::link_found(z.encoded_cbor_data_items, tagged,
                                                              &zone::link::first, what)
                                     .second;
    if (embedded == nullptr) [[unlikely]]
        return std::unexpected(error::incorrect_type);
    return std::cref(*embedded);
}

static_assert(std::is_trivially_destructible_v<item>,
              "The zone runs no destructor of an item, so an item must have no destructor to run.");

template <bool Checked>
struct tree_decoder {
    zone &decoded;
    heads::decoder<Checked> d;
    std::size_t depth_max;
    std::size_t open = 0;

    std::expected<item *, error> item_decode(item *storage, std::size_t depth);

    std::expected<item *, error> content_decode(item *storage, heads::head h, std::size_t depth);

    CBOR_ALWAYS_INLINE inline std::expected<void, error> scalar_decode(item &node, heads::head h);

    CBOR_NEVER_INLINE std::expected<item *, error> bignum_decode(item &node, std::uint64_t tag, std::size_t depth);
};


template <bool Checked>
std::expected<void, error> tree_decoder<Checked>::scalar_decode(item &node, heads::head const h)
{
    switch (h.major) {
    case major_type::byte_string:
    case major_type::text_string: {
        auto const string = d.byte_string_decode(h.argument);
        if (!string) [[unlikely]]
            return std::unexpected(string.error());
        std::span<char> const own = decoded.bump_allocate(string->size(), d.encoded.size());
        std::ranges::copy(*string, own.begin());
        if (h.major == major_type::text_string)
            node.via.str = std::string_view(own.data(), own.size());
        else
            node.via.bin = std::as_bytes(own);
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
std::expected<item *, error> tree_decoder<Checked>::bignum_decode(item &node, std::uint64_t const tag,
                                                                               std::size_t const depth)
{
    auto const next = heads::raw_head_read<Checked>(d.encoded, validity::offset{});
    if (!next || next->major != major_type::byte_string ||
        !validity::check_definite_length(next->major, next->info).has_value())
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
    return ::new (&node) item{tag == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                  ? cbor::type::negative_integer
                                  : cbor::type::unsigned_integer,
                              heads::preferred_argument_info(**read), {.u64 = **read}};
}

template <bool Checked>
std::expected<item *, error> tree_decoder<Checked>::item_decode(item *const storage,
                                                                               std::size_t const depth)
{
    if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const h = d.head_decode();
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    return content_decode(storage, *h, depth);
}

template <bool Checked>
std::expected<item *, error> tree_decoder<Checked>::content_decode(item *const storage,
                                                                                  heads::head const head,
                                                                                  std::size_t const depth)
{
    item &node = *::new (storage) item{heads::type_of(head.major, head.info, head.argument), head.info, {.u64 = head.argument}};
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
        auto const reserved = validity::checked_add(std::uint64_t{open}, *count);
        if (!reserved) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (auto const r = validity::check_pending_items(*reserved, d.encoded.size()); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::size_t const n = static_cast<std::size_t>(*count);
        std::span<item> const nodes(std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(n), n);
        open = static_cast<std::size_t>(*reserved);
        if (n != 0)
            if (auto const r = validity::check_nesting_depth(depth + 1, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
        for (item &child : nodes) {
            --open;
            auto const c = d.head_decode();
            if (!c) [[unlikely]]
                return std::unexpected(c.error());
            if (c->major == major_type::array || c->major == major_type::map || c->major == major_type::tag) {
                if (auto const r = content_decode(&child, *c, depth + 1); !r) [[unlikely]]
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
        auto const content =
            item_decode(std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(1), depth + 1);
        if (!content) [[unlikely]]
            return content;
        node.via.tag = {head.argument, *content};
        if (node.type == cbor::type::typed_array)
            if (auto const r = validity::check_typed_array_content(
                    head.argument, heads::major_type_of((*content)->type),
                    (*content)->type == cbor::type::byte_string ? (*content)->via.bin.size() : std::size_t{});
                !r) [[unlikely]]
                return std::unexpected(r.error());
        if (head.argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return &node;
        item const *const bytes_item = *content;
        if (bytes_item->type != cbor::type::byte_string) {
            decoded.encoded_cbor_data_items.emplace_back(&node, nullptr);
            return &node;
        }
        std::span<std::byte const> const encoded = bytes_item->via.bin;
        tree_decoder inner{decoded,
                           {std::string_view(reinterpret_cast<char const *>(encoded.data()), encoded.size())},
                           depth_max};
        auto const data_item =
            inner.item_decode(std::pmr::polymorphic_allocator<item>(&decoded.memory).allocate(1), depth + 2);
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


template <bool Checked>
struct decoded_tree {
    owning<item> tree;
    std::string_view rest;
};

template <bool Checked>
std::expected<decoded_tree<Checked>, error> decode_tree(std::string_view const encoded,
                                                        std::size_t const depth_max)
{
    auto decoded = std::make_unique<zone>(
        validity::checked_mul(encoded.size(), sizeof(item) + 1)
            .transform([](std::size_t const size) { return std::min(size, validity::initial_size_limit); })
            .value_or(validity::initial_size_limit));
    tree_decoder<Checked> tree{*decoded, {encoded}, depth_max};
    auto const built = tree.item_decode(std::pmr::polymorphic_allocator<item>(&decoded->memory).allocate(1), 0);
    if (!built) [[unlikely]]
        return std::unexpected(built.error());
    std::ranges::sort(decoded->encoded_cbor_data_items, std::less<>{}, &zone::link::first);
    return decoded_tree<Checked>{zone_owner::tree_owned(*built, std::move(decoded)), tree.d.encoded};
}

inline std::expected<owning<item>, error> decode(std::string_view const encoded)
{
    auto const decoded = decode_tree<true>(encoded, limits::nesting_depth);
    if (!decoded) [[unlikely]]
        return std::unexpected(decoded.error());
    if (auto const one = validity::check_one_data_item(decoded->rest); !one) [[unlikely]]
        return std::unexpected(one.error());
    return decoded->tree;
}

}

#endif
