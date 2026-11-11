/**
 * @file
 * Decodes the first CBOR data item in a buffer into a tree of
 * cbor::item. The tree holds a copy of each string in its own memory.
 * Decodes a bignum that fits in 64 bits as an integer. Decodes the
 * content of a tag 24 as one more data item.
 * https://www.rfc-editor.org/rfc/rfc8949
 */
#pragma once

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
#include <utility>
#include <vector>

#include "string_saver.hpp"
#include "error.hpp"
#include "head.hpp"
#include "item.hpp"
#include "ref.hpp"
#include "shared.hpp"
#include "rfc8949.hpp"
#include "validity.hpp"

namespace cbor
{

namespace detail
{

template <bool Resolve>
struct tree_decoder;

struct zone {
    using link = std::pair<item const *, item const *>;

    std::pmr::monotonic_buffer_resource memory;
    string_saver strings{memory};
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
        detail::validity::throw_logic_error_if(owner == nullptr || owner->owned == nullptr, what);
        return *owner->owned;
    }

    template <class Link, class Projection>
    static Link const &link_found(std::vector<Link> const &links, item const &from, Projection const key,
                                  char const *const what)
    {
        auto const found = std::ranges::lower_bound(links, &from, std::less<>{}, key);
        detail::validity::throw_logic_error_if(found == links.end() || std::invoke(key, *found) != &from,
                                               what);
        return *found;
    }

};

}

/**
 * @brief Gives the root of the item that a tag 24 (RFC 8949 3.4.5.1) holds in its byte string.
 *
 * `tagged` is the tag node in `tree`.
 * cbor::decode() decodes that content when it decodes the tree, so the call only looks it up.
 *
 * @code{.example}
 * bytes: d8 18 42 61 61
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of 24(h'6161')
 * call: auto const inner = cbor::encoded_cbor_data_item(*tree, **tree)
 * result: reference to item of "a"
 * call: inner->get().get<cbor::type::text_string>()
 * result: "a"
 * @endcode
 *
 * @retval incorrect_type the content of the tag 24 is not a byte string
 * @snippet doc_reference_item.cpp encoded_cbor_data_item
 * @see cbor::decode()
 * @see cbor::item
 *
 * @ingroup decode
 */
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

template <bool Resolve>
struct tree_decoder {
    zone &decoded;
    heads::decoder<true> d;
    std::size_t open = 0;
    std::vector<item const *> marks;

    std::expected<void, error> decode_item(std::pmr::vector<item> &into);

    std::expected<void, error> decode_content(std::pmr::vector<item> &into, heads::head h, std::size_t marked = 0);

    std::expected<void, error> decode_shareable(std::pmr::vector<item> &into, std::size_t marked);

    std::expected<void, error> decode_sharedref(std::pmr::vector<item> &into, std::size_t marked);

    CBOR_ALWAYS_INLINE inline std::expected<void, error> decode_scalar(item &node, heads::head h);

    CBOR_NEVER_INLINE std::expected<bool, error> decode_bignum(std::pmr::vector<item> &into, std::uint64_t tag);
};


template <bool Resolve>
std::expected<void, error> tree_decoder<Resolve>::decode_scalar(item &node, heads::head const h)
{
    switch (h.major) {
    case major_type::byte_string:
    case major_type::text_string: {
        auto const string = d.decode_string(h.argument);
        if (!string) [[unlikely]]
            return std::unexpected(string.error());
        std::string_view const own = decoded.strings.save(
            *string, string->size() + std::min(detail::validity::string_saver_chunk_size, d.encoded.size()));
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
            node.via.simple_value = static_cast<simple_value>(h.argument);
            return {};
        }
    default:
        return {};
    }
}

template <bool Resolve>
std::expected<bool, error> tree_decoder<Resolve>::decode_bignum(std::pmr::vector<item> &into, std::uint64_t const tag)
{
    auto const next = heads::read_raw_head<true>(d.encoded, detail::validity::offset{});
    if (!next || next->major != major_type::byte_string ||
        !detail::validity::check_definite_length(next->major, next->info).has_value())
        return false;
    heads::decoder<true> const before = d;
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

template <bool Resolve>
std::expected<void, error> tree_decoder<Resolve>::decode_item(std::pmr::vector<item> &into)
{
    if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const h = d.decode_head();
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    return decode_content(into, *h);
}

template <bool Resolve>
std::expected<void, error> tree_decoder<Resolve>::decode_content(std::pmr::vector<item> &into,
                                                                 heads::head const head,
                                                                 std::size_t const marked)
{
    if constexpr (Resolve) {
        if (head.major == major_type::tag && head.argument == std::to_underlying(rfc8949::tag_number::shareable))
            [[unlikely]]
            return decode_shareable(into, marked + 1);
        if (head.major == major_type::tag && head.argument == std::to_underlying(rfc8949::tag_number::sharedref))
            [[unlikely]]
            return decode_sharedref(into, marked);
    }
    if (heads::is_bignum(head.major, head.argument)) {
        auto const r = decode_bignum(into, head.argument);
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (*r) {
            if constexpr (Resolve)
                if (marked != 0) [[unlikely]]
                    marks.insert(marks.end(), marked, &into.back());
            return {};
        }
    }
    item &node = into.emplace_back(
        item{heads::type_of(head.major, head.info, head.argument), head.info, {.u64 = head.argument}});
    if constexpr (Resolve)
        if (marked != 0) [[unlikely]]
            marks.insert(marks.end(), marked, &node);
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
        auto const count = detail::validity::checked_mul(
            head.argument, head.major == major_type::map ? rfc8949::data_items_per_pair : std::size_t{1});
        if (!count) [[unlikely]]
            return std::unexpected(error::too_little_data);
        auto const reserved = detail::validity::checked_add(std::uint64_t{open}, *count);
        if (!reserved) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (auto const r = detail::validity::check_pending_items(*reserved, d.encoded.size()); !r)
            [[unlikely]]
            return std::unexpected(r.error());
        std::size_t const n = static_cast<std::size_t>(*count);
        std::pmr::vector<item> &nodes = decoded.items_reserved(n);
        open = static_cast<std::size_t>(*reserved);
        if (n != 0)
            if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
                return std::unexpected(r.error());
        for (std::size_t left = n; left != 0; --left) {
            --open;
            auto const c = d.decode_head();
            if (!c) [[unlikely]]
                return std::unexpected(c.error());
            if (c->major == major_type::array || c->major == major_type::map || c->major == major_type::tag) {
                if (auto const r = decode_content(nodes, *c); !r) [[unlikely]]
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
        if (auto const r = decode_item(held); !r) [[unlikely]]
            return r;
        item const &content = held.front();
        node.via.tag = {head.argument, &content};
        if (node.type == cbor::type::typed_array)
            if (auto const r = detail::validity::check_typed_array_content(
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
        std::string_view const content_encoded =
            std::string_view(std::span(tagged).first(std::to_underlying(detail::validity::distance(tagged, d.encoded))));
        tree_decoder inner{decoded, {std::string_view(std::span(content_encoded).last(content.via.bin.size()))}, 0, {}};
        std::pmr::vector<item> &embedded = decoded.items_reserved(1);
        if (auto const r = inner.decode_item(embedded); !r) [[unlikely]]
            return r;
        if (auto const one = detail::validity::check_one_data_item(inner.d.encoded); !one) [[unlikely]]
            return std::unexpected(one.error());
        decoded.encoded_cbor_data_items.emplace_back(&node, &embedded.front());
        return {};
    }
    }
}

template <bool Resolve>
std::expected<void, error> tree_decoder<Resolve>::decode_shareable(std::pmr::vector<item> &into,
                                                                   std::size_t const marked)
{
    std::size_t chain = marked;
    for (;;) {
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag || h->argument != std::to_underlying(rfc8949::tag_number::shareable))
            return decode_content(into, *h, chain);
        ++chain;
    }
}

template <bool Resolve>
std::expected<void, error> tree_decoder<Resolve>::decode_sharedref(std::pmr::vector<item> &into,
                                                                   std::size_t const marked)
{
    auto const h = d.decode_head();
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    if (error const c = detail::validity::check_tag_content(std::to_underlying(rfc8949::tag_number::sharedref), h->major)
                            .error_or(error{});
        c != error{}) [[unlikely]]
        return std::unexpected(c);
    auto const index = value_sharing::check_sharedref_index(h->argument, marks.size());
    if (!index) [[unlikely]]
        return std::unexpected(h->argument - marks.size() < marked ? error::sharedref_not_defined : index.error());
    item const *const target = marks[std::to_underlying(*index)];
    marks.insert(marks.end(), marked, target);
    into.emplace_back(item{cbor::type::shared_reference, h->info, {.tag = {h->argument, target}}});
    return {};
}

struct decoded_tree {
    owning<item> tree;
};

template <bool Resolve>
std::expected<decoded_tree, error> decode_tree(std::string_view const encoded)
{
    auto decoded = std::make_unique<zone>(detail::validity::checked_mul(encoded.size(), sizeof(item) + 1)
                                              .transform([](std::size_t const size) {
                                                  return std::clamp(size, std::size_t{1},
                                                                    detail::validity::initial_size_limit);
                                              })
                                              .value_or(detail::validity::initial_size_limit));
    tree_decoder<Resolve> tree{*decoded, {encoded}, 0, {}};
    std::pmr::vector<item> &root = decoded->items_reserved(1);
    if (auto const built = tree.decode_item(root); !built) [[unlikely]]
        return std::unexpected(built.error());
    std::ranges::sort(decoded->encoded_cbor_data_items, std::less<>{}, &zone::link::first);
    item const &top = root.front();
    return decoded_tree{zone_owner::tree_owned(&top, std::move(decoded))};
}

}

/**
 * @brief Decodes the one top-level item in `encoded` into a tree of cbor::item.
 *
 * It checks every head.
 * It does not check that a text string is UTF-8.
 * The tree holds a copy of each text and byte string, so it stays valid after `encoded` ends.
 * A bignum whose magnitude fits 64 bits becomes an integer node.
 * A tag 24 is a tag node, and cbor::encoded_cbor_data_item() gives its decoded content.
 * Tags 28 and 29 of the value-sharing registration are resolved, with a table for this call: a tag 28 makes no
 * node and marks the node of its content, and a tag 29 with the index n is a `shared_reference` node whose target is
 * the node that the n-th tag 28 marks. A target can be the node itself or a node above it, so the tree can hold a
 * cycle. The content of a tag 24 has its own table.
 * One leading tag 55799 is read as its content, so the root is that content.
 * A second tag 55799 and a tag 55799 inside an item stay tag nodes.
 *
 * @code{.example}
 * message: ["hi", 1]
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of ["hi", 1]
 * call: (*tree)->get<cbor::type::array>()->size()
 * result: 2
 * @endcode
 * @code{.example}
 * bytes: 01 02
 * call: cbor::decode(bytes)
 * result: owning item of 1
 * @endcode
 * @code{.example}
 * bytes: c1 00
 * call: cbor::decode(bytes)
 * result: owning item of 1(0)
 * @endcode
 *
 * @retval too_little_data the item is not complete, or a count is larger than the bytes that are left
 * @retval syntax_error a head is not well-formed, or a simple value below 32 stands in the two-byte form. A byte after
 * the item is not read, so the input can be a CBOR sequence (RFC 8742)
 * @retval indefinite_length a string, an array or a map has an indefinite length
 * @retval nesting_depth_exceeded the item is deeper than the limit
 * @retval sharedref_index_not_marked a tag 29 whose index names no tag 28 before it
 * @retval sharedref_not_defined a tag 29 that names a tag 28 whose content is only tags 28 and this tag 29, as in
 * `28(29(0))`
 * @retval inadmissible_type_for_tag_content a tag 29 over an item that is not an unsigned integer, or a typed array (RFC 8746) whose content is not a byte string of whole
 * elements. A tag 2 or 3 over another content is an `unsigned_bignum` or `negative_bignum` item over that content,
 * a tag 24 over another content is a `tag` item over that content, and decode checks the content of no other tag.
 * @snippet doc_reference_item.cpp decode
 * @see cbor::item
 * @see cbor::encode()
 *
 * @ingroup decode
 */
inline std::expected<owning<item>, error> decode(std::string_view const encoded)
{
    auto const decoded = detail::decode_tree<true>(heads::self_described_cbor_content(encoded));
    if (!decoded) [[unlikely]]
        return std::unexpected(decoded.error());
    return decoded->tree;
}

}
