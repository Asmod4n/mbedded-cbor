// Decodes the first CBOR data item in a buffer into a tree of
// cbor::item. The tree holds a copy of each string in its own memory.
// Decodes a bignum that fits in 64 bits as an integer. Decodes the
// content of a tag 24 as one more data item.
// https://www.rfc-editor.org/rfc/rfc8949
#ifndef CBOR_DECODE_HPP
#define CBOR_DECODE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <forward_list>
#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "bump.hpp"
#include "error.hpp"
#include "head.hpp"
#include "item.hpp"
#include "ref.hpp"
#include "rfc8949.hpp"
#include "validity.hpp"

namespace cbor
{

namespace detail
{

template <bool Checked>
struct tree_decoder;

struct zone {
    using link = std::pair<item const *, item const *>;

    std::pmr::monotonic_buffer_resource memory;
    bump strings{memory};
    std::pmr::forward_list<std::pmr::vector<item>> items{&memory};
    std::vector<link> encoded_cbor_data_items;

    std::pmr::vector<item> &items_reserved(std::size_t const count)
    {
        std::pmr::vector<item> &reserved = items.emplace_front();
        reserved.reserve(count);
        return reserved;
    }

    explicit zone(std::size_t const initial_size) : memory(initial_size)
    {
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

}

inline std::expected<std::reference_wrapper<item const>, error>
encoded_cbor_data_item(owning<item> const &tree, item const &tagged)
{
    constexpr char const *what = "cbor::encoded_cbor_data_item: the item is not a tag 24 of this decoded tree";
    detail::zone const &z = detail::zone_owner::zone_found(tree, what);
    item const *const embedded = detail::zone_owner::link_found(z.encoded_cbor_data_items, tagged,
                                                                &detail::zone::link::first, what)
                                     .second;
    if (embedded == nullptr) [[unlikely]]
        return std::unexpected(error::incorrect_type);
    return std::cref(*embedded);
}

namespace detail
{

template <bool Checked>
struct tree_decoder {
    zone &decoded;
    heads::decoder<Checked> d;
    std::size_t depth_max;
    std::size_t open = 0;

    std::expected<void, error> decode_item(std::pmr::vector<item> &into, std::size_t depth);

    std::expected<void, error> decode_content(std::pmr::vector<item> &into, heads::head h, std::size_t depth);

    CBOR_ALWAYS_INLINE inline std::expected<void, error> decode_scalar(item &node, heads::head h);

    CBOR_NEVER_INLINE std::expected<bool, error> decode_bignum(std::pmr::vector<item> &into, std::uint64_t tag,
                                                               std::size_t depth);
};


template <bool Checked>
std::expected<void, error> tree_decoder<Checked>::decode_scalar(item &node, heads::head const h)
{
    switch (h.major) {
    case major_type::byte_string:
    case major_type::text_string: {
        auto const string = d.decode_byte_string(h.argument);
        if (!string) [[unlikely]]
            return std::unexpected(string.error());
        std::string_view const own = decoded.strings.alloc_str(
            *string, string->size() + std::min(validity::bump_chunk_size, d.encoded.size()));
        if (h.major == major_type::text_string)
            node.via.str = own;
        else
            node.via.bin = std::as_bytes(std::span(own));
        return {};
    }
    case major_type::simple_float:
        switch (static_cast<rfc8949::simple_float_information>(h.info)) {
        case rfc8949::simple_float_information::half_precision_float:
        case rfc8949::simple_float_information::single_precision_float:
        case rfc8949::simple_float_information::double_precision_float:
            node.via.f64 = heads::decode_float(h.info, h.argument);
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
std::expected<bool, error> tree_decoder<Checked>::decode_bignum(std::pmr::vector<item> &into, std::uint64_t const tag,
                                                                std::size_t const depth)
{
    auto const next = heads::read_raw_head<Checked>(d.encoded, validity::offset{});
    if (!next || next->major != major_type::byte_string ||
        !validity::check_definite_length(next->major, next->info).has_value())
        return false;
    if (auto const r = validity::check_nesting_depth(depth + 1, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    heads::decoder<Checked> const before = d;
    auto const read = d.decode_bignum(tag);
    if (!read) [[unlikely]]
        return std::unexpected(read.error());
    if (!*read) {
        d = before;
        return false;
    }
    into.emplace_back(item{tag == std::to_underlying(rfc8949::tag_number::negative_bignum)
                               ? cbor::type::negative_integer
                               : cbor::type::unsigned_integer,
                           heads::preferred_argument_info(**read), {.u64 = **read}});
    return true;
}

template <bool Checked>
std::expected<void, error> tree_decoder<Checked>::decode_item(std::pmr::vector<item> &into, std::size_t const depth)
{
    if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const h = d.decode_head();
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    return decode_content(into, *h, depth);
}

template <bool Checked>
CBOR_FLATTEN std::expected<void, error> tree_decoder<Checked>::decode_content(std::pmr::vector<item> &into,
                                                                 heads::head const head, std::size_t const depth)
{
    if (heads::is_bignum(head.major, head.argument))
        if (auto const r = decode_bignum(into, head.argument, depth); !r || *r)
            return r.transform([](bool) {});
    item &node = into.emplace_back(
        item{heads::type_of(head.major, head.info, head.argument), head.info, {.u64 = head.argument}});
    switch (head.major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return {};
    case major_type::byte_string:
    case major_type::text_string:
    case major_type::simple_float:
        return decode_scalar(node, head);
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
        std::pmr::vector<item> &nodes = decoded.items_reserved(n);
        open = static_cast<std::size_t>(*reserved);
        if (n != 0)
            if (auto const r = validity::check_nesting_depth(depth + 1, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
        for (std::size_t left = n; left != 0; --left) {
            --open;
            auto const c = d.decode_head();
            if (!c) [[unlikely]]
                return std::unexpected(c.error());
            if (c->major == major_type::array || c->major == major_type::map || c->major == major_type::tag) {
                if (auto const r = decode_content(nodes, *c, depth + 1); !r) [[unlikely]]
                    return r;
                continue;
            }
            item &child = nodes.emplace_back(item{heads::type_of(c->major, c->info, c->argument), c->info, {.u64 = c->argument}});
            if (auto const r = decode_scalar(child, *c); !r) [[unlikely]]
                return std::unexpected(r.error());
        }
        if (head.major == major_type::map)
            node.via.map = std::span<item const>(nodes);
        else
            node.via.array = std::span<item const>(nodes);
        return {};
    }
    default: {
        std::string_view const tagged = d.encoded;
        std::pmr::vector<item> &held = decoded.items_reserved(1);
        if (auto const r = decode_item(held, depth + 1); !r) [[unlikely]]
            return r;
        item const &content = held.front();
        node.via.tag = {head.argument, &content};
        if (node.type == cbor::type::typed_array)
            if (auto const r = validity::check_typed_array_content(
                    head.argument, heads::major_type_of(content.type),
                    content.type == cbor::type::byte_string ? content.via.bin.size() : std::size_t{});
                !r) [[unlikely]]
                return std::unexpected(r.error());
        if (head.argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return {};
        if (content.type != cbor::type::byte_string) {
            decoded.encoded_cbor_data_items.emplace_back(&node, nullptr);
            return {};
        }
        std::string_view const content_encoded = tagged.substr(0, std::to_underlying(validity::distance(tagged, d.encoded)));
        tree_decoder inner{decoded, {content_encoded.substr(content_encoded.size() - content.via.bin.size())}, depth_max};
        std::pmr::vector<item> &embedded = decoded.items_reserved(1);
        if (auto const r = inner.decode_item(embedded, depth + 2); !r) [[unlikely]]
            return r;
        if (auto const one = validity::check_one_data_item(inner.d.encoded); !one) [[unlikely]]
            return std::unexpected(one.error());
        decoded.encoded_cbor_data_items.emplace_back(&node, &embedded.front());
        return {};
    }
    }
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
            .transform([](std::size_t const size) { return std::clamp(size, std::size_t{1}, validity::initial_size_limit); })
            .value_or(validity::initial_size_limit));
    tree_decoder<Checked> tree{*decoded, {encoded}, depth_max};
    std::pmr::vector<item> &root = decoded->items_reserved(1);
    if (auto const built = tree.decode_item(root, 0); !built) [[unlikely]]
        return std::unexpected(built.error());
    std::ranges::sort(decoded->encoded_cbor_data_items, std::less<>{}, &zone::link::first);
    item const &top = root.front();
    return decoded_tree<Checked>{zone_owner::tree_owned(&top, std::move(decoded)), tree.d.encoded};
}

}

inline std::expected<owning<item>, error> decode(std::string_view const encoded)
{
    auto const decoded = detail::decode_tree<true>(heads::self_described_cbor_content(encoded), limits::nesting_depth);
    if (!decoded) [[unlikely]]
        return std::unexpected(decoded.error());
    return decoded->tree;
}

}

#endif
