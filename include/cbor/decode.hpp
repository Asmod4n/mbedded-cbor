/**
 * @file
 * Decodes the first CBOR data item in a buffer into a tree of
 * cbor::item in the form of the yyjson DOM: an item of 16 bytes, all
 * items of a tree in one array, each container followed by its
 * descendants and holding the distance to its next sibling, and one
 * copy of the input that the strings point into. Decodes a bignum that
 * fits in 64 bits as an integer. Decodes the content of a tag 24 as one
 * more data item. decode_cycles also resolves tags 28 and 29 of the
 * value-sharing registration.
 * https://www.rfc-editor.org/rfc/rfc8949
 * http://cbor.schmorp.de/value-sharing
 * https://github.com/ibireme/yyjson
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "error.hpp"
#include "head.hpp"
#include "ieee754.hpp"
#include "item.hpp"
#include "padded_string.hpp"
#include "ref.hpp"
#include "shared.hpp"
#include "rfc8949.hpp"
#include "validity.hpp"

namespace cbor
{

namespace detail
{

namespace dom
{

inline constexpr std::size_t growth_divisor = 2;

struct value_block {
    static constexpr std::size_t pair_values = 2;

    std::unique_ptr<item[]> values;
    std::span<item> items;
    std::size_t val_end;

    static value_block allocated(std::size_t size);

    [[gnu::noinline]] void grow(std::size_t const used, std::size_t const needed)
    {
        value_block grown = allocated(std::max(items.size() + items.size() / growth_divisor, needed));
        for (auto &&[to, from] : std::views::zip(grown.items, items.first(used)))
            to = from;
        *this = std::move(grown);
    }

    [[gnu::noinline]] void grow(std::size_t const used)
    {
        grow(used, 0);
    }
};

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif

inline value_block value_block::allocated(std::size_t const size)
{
    std::unique_ptr<item[]> values(new item[size]);
    std::span<item> const items(values.get(), size);
    return {std::move(values), items, size - std::min(size, pair_values)};
}

#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif

}

struct tree {
    using link = std::pair<item const *, item const *>;

    dom::value_block block;
    padded_string text;
    std::vector<link> encoded_cbor_data_items;
};

struct tree_owner {
    std::unique_ptr<tree> owned;

    void operator()(item const *) const noexcept
    {
    }

    static owning<item> tree_owned(std::unique_ptr<tree> decoded)
    {
        item const *const root = &decoded->block.items.front();
        return owning<item>(std::shared_ptr<item const>(root, tree_owner{std::move(decoded)}));
    }

    static tree const &tree_found(owning<item> const &held, char const *const what)
    {
        tree_owner const *const owner = std::get_deleter<tree_owner>(held.held);
        detail::validity::throw_logic_error_if(owner == nullptr || owner->owned == nullptr, what);
        return *owner->owned;
    }
};

namespace dom
{

class decoder
{
    using heads = detail::heads;
    using validity = detail::validity;

public:
    static constexpr std::size_t max_nesting_depth = 1024;

    static std::expected<void, error> check_nesting_depth(std::size_t const open_containers)
    {
        if (open_containers >= max_nesting_depth) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        return {};
    }

    static std::expected<void, error> check_input_size(std::size_t const size)
    {
        if (std::cmp_greater_equal(size, kind::high_bound)) [[unlikely]]
            return std::unexpected(error::value_too_large);
        return {};
    }

    static void set_integer(item &x, major_type const major, std::uint8_t const info, std::uint64_t const argument)
    {
        if (major == major_type::unsigned_integer) {
            x.m_tag = kind::word(0, info, kind::type_number | kind::subtype_uint);
            x.m_uni.u64 = argument;
        } else if (argument <= std::uint64_t{std::numeric_limits<std::int64_t>::max()}) {
            x.m_tag = kind::word(0, info, kind::type_number | kind::subtype_sint);
            x.m_uni.i64 = static_cast<std::int64_t>(~argument);
        } else {
            x.m_tag = kind::word(0, info, kind::type_number | kind::subtype_negative);
            x.m_uni.u64 = argument;
        }
    }

    static void set_float(item &x, std::uint8_t const info, double const value)
    {
        x.m_tag = kind::word(0, info,
                             static_cast<std::uint8_t>(
                                 kind::type_number | kind::subtype_real |
                                 (info - std::to_underlying(rfc8949::simple_information::half_precision_float))
                                     << kind::width_bit));
        x.m_uni.f64 = value;
    }

    static void set_text(item &x, char const *const first, std::size_t const size)
    {
        x.m_tag = kind::word(size, heads::preferred_argument_info(size), kind::type_text);
        x.m_uni.str = first;
    }

    static void set_literal(item &x, simple_value const value)
    {
        switch (value) {
        case simple_value::false_value:
            x.m_tag = kind::word(0, std::to_underlying(value), kind::type_bool | kind::subtype_false);
            return;
        case simple_value::true_value:
            x.m_tag = kind::word(0, std::to_underlying(value), kind::type_bool | kind::subtype_true);
            return;
        case simple_value::null:
            x.m_tag = kind::word(0, std::to_underlying(value), kind::type_null);
            return;
        case simple_value::undefined:
            x.m_tag = kind::word(0, std::to_underlying(value), kind::type_null | kind::subtype_undefined);
            return;
        }
    }

    static std::uint64_t high_of(item const &x)
    {
        return kind::high_of(x.m_tag);
    }

    static void set_container(item &x, std::uint64_t const count, std::uint8_t const info, std::uint8_t const type,
                              std::size_t const ofs)
    {
        x.m_tag = kind::word(count, info, type);
        x.m_uni.ofs = ofs;
    }

    static void set_type(item &x, std::uint8_t const type)
    {
        x.m_tag = kind::word(0, 0, type);
    }

    static std::uint8_t type_of(item const &x)
    {
        return static_cast<std::uint8_t>(x.m_tag & kind::type_mask);
    }

    static void set_open_count(item &x, std::size_t const count)
    {
        x.m_tag = kind::word(count, 0, type_of(x));
    }

    static std::size_t ofs_of(item const &x)
    {
        return x.m_uni.ofs;
    }

    static void set_ofs(item &x, std::size_t const ofs)
    {
        x.m_uni.ofs = ofs;
    }

private:
    struct frame {
        std::size_t at;
        std::uint64_t left;
    };

    struct embedded_item {
        std::string_view encoded;
        std::size_t tagged;
        std::size_t depth;
        error failure;
    };

    struct index_link {
        std::size_t tagged;
        std::size_t embedded;
    };

    static constexpr std::size_t no_item = std::numeric_limits<std::size_t>::max();

    struct open_mark {
        std::uint64_t index;
        std::size_t target;
    };

    static bool is_tag_number(heads::head const h, rfc8949::tag_number const number)
    {
        return h.major == major_type::tag && h.argument == std::to_underlying(number);
    }

    static major_type major_type_of(item const &x)
    {
        return heads::major_type_of(x.type());
    }

    [[gnu::noinline]] static std::expected<void, error> check_typed_array_content(std::uint64_t const tag,
                                                                                  item const &content)
    {
        major_type const m = major_type_of(content);
        return validity::check_typed_array_content(
            tag, m, m == major_type::byte_string ? kind::high_of(content.m_tag) : std::uint64_t{});
    }

    [[gnu::noinline]] static std::expected<bool, error> decode_bignum(heads::decoder<true> &d, item &x,
                                                                      std::uint64_t const tag)
    {
        if (!heads::starts_with_definite_byte_string(d.encoded).value_or(false))
            return false;
        heads::decoder<true> const before = d;
        auto const read = d.decode_bignum(tag);
        if (!read) [[unlikely]]
            return std::unexpected(read.error());
        if (!*read) {
            d = before;
            return false;
        }
        set_integer(x, (*read)->major, (*read)->info, (*read)->argument);
        return true;
    }

    struct marks_table {
        std::uint64_t marks = 0;
        std::size_t marked = 0;
        std::vector<open_mark> open;

        void open_marks_of(std::size_t const target)
        {
            for (std::size_t i = 0; i < marked; ++i)
                open.push_back({marks + i, target});
            marks += marked;
            marked = 0;
        }

        void close_marks_of(std::size_t const target)
        {
            if (!open.empty()) [[unlikely]]
                std::erase_if(open, [target](open_mark const &m) { return m.target == target; });
        }

        void close_marks_of_scalar()
        {
            marks += marked;
            marked = 0;
        }
    };

    struct sharedref {
        heads::head index;
        std::size_t target;
    };

    [[gnu::noinline]] static std::expected<sharedref, error> decode_sharedref(heads::decoder<true> &d,
                                                                              marks_table &table)
    {
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (auto const c =
                validity::check_tag_content(std::to_underlying(rfc8949::tag_number::sharedref), h->major);
            !c) [[unlikely]]
            return std::unexpected(c.error());
        if (auto const index = value_sharing::check_sharedref_index(h->argument, table.marks); !index) [[unlikely]]
            return std::unexpected(h->argument - table.marks < table.marked ? error::sharedref_not_defined
                                                                            : index.error());
        auto const named = std::ranges::lower_bound(table.open, h->argument, {}, &open_mark::index);
        if (named == table.open.end() || named->index != h->argument) [[unlikely]]
            return std::unexpected(error::sharedref_not_cyclic);
        std::size_t const target = named->target;
        table.open_marks_of(target);
        return sharedref{*h, target};
    }

    struct items_decoder {
        value_block &block;
        std::vector<embedded_item> embedded;
        std::vector<index_link> links;

        [[gnu::noinline]] void add_embedded(item const &content, std::size_t const tagged, std::size_t const depth)
        {
            if ((content.m_tag & kind::type_mask) != kind::type_bytes) {
                links.push_back({tagged, no_item});
                return;
            }
            embedded.push_back(
                {std::string_view(content.m_uni.str, static_cast<std::size_t>(kind::high_of(content.m_tag))),
                 tagged, depth, error{}});
        }

        [[gnu::noinline]] static void mark_path(std::span<item> const v, std::span<frame const> const path)
        {
            for (frame const &f : path)
                v[f.at].m_tag |= kind::shared_flag;
        }

        template <bool Cycles>
        std::expected<std::size_t, error> decode_item(std::size_t const first, std::string_view const encoded,
                                                      std::size_t const depth, bool const one_item)
        {
            std::span<item> const v = block.items;
            heads::decoder<true> d{encoded};
            std::vector<frame> open_containers;
            marks_table table;
            std::uint64_t left = 1;
            std::uint64_t open = 1;
            std::size_t val = first;
            for (;;) {
                auto const h = d.decode_head();
                if (!h) [[unlikely]]
                    return std::unexpected(h.error());
                --open;
                item &x = v[val];
                switch (h->major) {
                case major_type::unsigned_integer:
                case major_type::negative_integer:
                    set_integer(x, h->major, h->info, h->argument);
                    break;
                case major_type::byte_string:
                case major_type::text_string: {
                    auto const s = d.decode_string(h->argument);
                    if (!s) [[unlikely]]
                        return std::unexpected(s.error());
                    x.m_tag = kind::word(h->argument, h->info,
                                         h->major == major_type::text_string ? kind::type_text : kind::type_bytes);
                    x.m_uni.str = s->data();
                    break;
                }
                case major_type::array:
                case major_type::map: {
                    std::uint64_t const count = validity::count_enclosed_data_items(h->major, h->argument);
                    auto const reserved = validity::checked_add(open, count);
                    if (!reserved) [[unlikely]]
                        return std::unexpected(error::too_little_data);
                    if (auto const r = validity::check_bytes_left(*reserved, d.encoded.size()); !r) [[unlikely]]
                        return std::unexpected(r.error());
                    x.m_tag = kind::word(h->argument, h->info,
                                         h->major == major_type::map ? kind::type_map : kind::type_array);
                    if (count != 0) {
                        if (auto const r = check_nesting_depth(depth + open_containers.size()); !r) [[unlikely]]
                            return std::unexpected(r.error());
                        if constexpr (Cycles)
                            if (table.marked != 0) [[unlikely]]
                                table.open_marks_of(val);
                        open = *reserved;
                        open_containers.push_back({val, left});
                        left = count;
                        ++val;
                        continue;
                    }
                    x.m_uni.ofs = 1;
                    break;
                }
                case major_type::tag: {
                    if constexpr (Cycles) {
                        if (h->argument == std::to_underlying(rfc8949::tag_number::shareable)) [[unlikely]] {
                            ++table.marked;
                            ++open;
                            continue;
                        }
                        if (h->argument == std::to_underlying(rfc8949::tag_number::sharedref)) [[unlikely]] {
                            auto const r = decode_sharedref(d, table);
                            if (!r) [[unlikely]]
                                return std::unexpected(r.error());
                            x.m_tag = kind::word(r->index.argument, r->index.info,
                                                 kind::type_extended | kind::extended_shared_reference |
                                                     kind::shared_flag);
                            x.m_uni.i64 = static_cast<std::int64_t>(r->target) - static_cast<std::int64_t>(val);
                            mark_path(v, open_containers);
                            break;
                        }
                    }
                    if (heads::is_bignum(h->major, h->argument)) [[unlikely]] {
                        auto const r = decode_bignum(d, x, h->argument);
                        if (!r) [[unlikely]]
                            return std::unexpected(r.error());
                        if (*r)
                            break;
                    }
                    if (auto const r = check_nesting_depth(depth + open_containers.size()); !r) [[unlikely]]
                        return std::unexpected(r.error());
                    if constexpr (Cycles)
                        if (table.marked != 0) [[unlikely]]
                            table.open_marks_of(val);
                    x.m_tag = kind::word(0, h->info,
                                         kind::type_extended | (validity::is_typed_array_tag(h->argument)
                                                                    ? kind::extended_typed_array
                                                                    : kind::extended_tag));
                    x.m_uni.u64 = h->argument;
                    ++open;
                    open_containers.push_back({val, left});
                    left = 1;
                    ++val;
                    continue;
                }
                case major_type::simple:
                    if (heads::is_floating_point(h->major, h->info)) {
                        set_float(x, h->info, ieee754::decode_float(h->info, h->argument));
                        break;
                    }
                    switch (h->info) {
                    case std::to_underlying(simple_value::false_value):
                    case std::to_underlying(simple_value::true_value):
                    case std::to_underlying(simple_value::null):
                    case std::to_underlying(simple_value::undefined):
                        set_literal(x, static_cast<simple_value>(h->info));
                        break;
                    default:
                        x.m_tag = kind::word(0, h->info, kind::type_extended | kind::extended_simple);
                        x.m_uni.u64 = h->argument;
                        break;
                    }
                    break;
                }
                if constexpr (Cycles)
                    if (table.marked != 0) [[unlikely]]
                        table.close_marks_of_scalar();
                ++val;
                while (--left == 0) {
                    if (open_containers.empty()) {
                        if (one_item)
                            if (auto const r = validity::check_one_data_item(d.encoded); !r) [[unlikely]]
                                return std::unexpected(r.error());
                        return val;
                    }
                    frame const f = open_containers.back();
                    open_containers.pop_back();
                    left = f.left;
                    if constexpr (Cycles)
                        table.close_marks_of(f.at);
                    item &c = v[f.at];
                    if ((c.m_tag & kind::type_mask) != kind::type_extended) {
                        c.m_uni.ofs = val - f.at;
                        continue;
                    }
                    if (kind::is_extended(c, kind::extended_typed_array)) [[unlikely]] {
                        if (auto const r = check_typed_array_content(c.m_uni.u64, v[f.at + 1]); !r) [[unlikely]]
                            return std::unexpected(r.error());
                    } else if (c.m_uni.u64 == std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
                        [[unlikely]] {
                        add_embedded(v[f.at + 1], f.at, depth + open_containers.size() + 1);
                    }
                    c.m_tag |= std::uint64_t{val - f.at} << kind::tag_bit;
                }
            }
        }
    };

    static void schedule(std::vector<embedded_item> &pending, std::vector<embedded_item> &found,
                         std::expected<std::size_t, error> const &decoded)
    {
        if (!decoded) [[unlikely]]
            pending.push_back({{}, 0, 0, decoded.error()});
        pending.insert(pending.end(), found.rbegin(), found.rend());
        found.clear();
    }

    [[gnu::noinline]] static std::vector<tree::link> links_of(std::span<item const> const items,
                                                              std::vector<index_link> &links)
    {
        std::ranges::sort(links, {}, &index_link::tagged);
        std::vector<tree::link> found;
        found.reserve(links.size());
        for (index_link const &l : links)
            found.emplace_back(&items[l.tagged], l.embedded == no_item ? nullptr : &items[l.embedded]);
        return found;
    }

public:
    template <bool Cycles>
    static std::expected<std::unique_ptr<tree>, error> decode(std::string_view const bytes)
    {
        std::size_t const size = bytes.size();
        if (size == 0) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (auto const r = check_input_size(size); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto decoded = std::make_unique<tree>(value_block::allocated(size), padded_string(bytes),
                                              std::vector<tree::link>{});
        items_decoder out{decoded->block, {}, {}};
        auto const outer = out.decode_item<Cycles>(0, std::string_view(decoded->text), 0, false);
        if (out.embedded.empty() && out.links.empty() && outer) [[likely]]
            return decoded;
        bool failed = !outer;
        std::size_t written = outer.value_or(0);
        std::vector<embedded_item> pending;
        schedule(pending, out.embedded, outer);
        while (!pending.empty()) {
            embedded_item const e = pending.back();
            pending.pop_back();
            if (e.failure != error{}) [[unlikely]]
                return std::unexpected(e.failure);
            std::size_t const first = failed ? 0 : written;
            if (decoded->block.items.size() - first < e.encoded.size()) [[unlikely]]
                decoded->block.grow(written, first + e.encoded.size());
            auto const inner = out.decode_item<Cycles>(first, e.encoded, e.depth, true);
            failed = failed || !inner;
            if (!failed) {
                out.links.push_back({e.tagged, first});
                written = *inner;
            }
            schedule(pending, out.embedded, inner);
        }
        decoded->encoded_cbor_data_items = links_of(decoded->block.items, out.links);
        return decoded;
    }
};

}

template <bool Cycles>
std::expected<owning<item>, error> decode_tree(std::string_view const encoded)
{
    return dom::decoder::decode<Cycles>(encoded).transform(
        [](std::unique_ptr<tree> &&decoded) { return tree_owner::tree_owned(std::move(decoded)); });
}

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
 * call: inner->get().get<std::string_view>()
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
    detail::tree const &decoded = detail::tree_owner::tree_found(tree, what);
    auto const found = std::ranges::lower_bound(decoded.encoded_cbor_data_items, &tagged, std::less<>{},
                                                &detail::tree::link::first);
    detail::validity::throw_logic_error_if(found == decoded.encoded_cbor_data_items.end() || found->first != &tagged,
                                           what);
    if (found->second == nullptr) [[unlikely]]
        return std::unexpected(error::incorrect_type);
    return std::cref(*found->second);
}

/**
 * @brief Decodes the one top-level item in `encoded` into a tree of cbor::item.
 *
 * It checks every head.
 * It does not check that a text string is UTF-8.
 * The tree holds one copy of `encoded`, and each text and byte string points into it, so the tree stays valid after
 * `encoded` ends.
 * A bignum whose magnitude fits 64 bits becomes an integer node.
 * A tag 24 is a tag node, and cbor::encoded_cbor_data_item() gives its decoded content.
 * A tag 28 and a tag 29 of the value-sharing registration are tag nodes, as every tag that decode does not interpret;
 * cbor::decode_cycles() resolves them.
 * One leading tag 55799 is read as its content, so the root is that content.
 * A second tag 55799 and a tag 55799 inside an item stay tag nodes.
 *
 * @code{.example}
 * message: ["hi", 1]
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of ["hi", 1]
 * call: (*tree)->get<cbor::array_range>()->size()
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
 * @retval nesting_depth_exceeded the item holds more than 1024 open arrays, maps and tags at one point
 * @retval inadmissible_type_for_tag_content a typed array (RFC 8746) whose content is not a byte string of whole
 * elements. A tag 2 or 3 over another content is an `unsigned_bignum` or `negative_bignum` item over that content,
 * a tag 24 over another content is a `tag` item over that content, and decode checks the content of no other tag.
 * @snippet doc_reference_item.cpp decode
 * @see cbor::item
 * @see cbor::decode_cycles()
 * @see cbor::encode()
 *
 * @ingroup decode
 */
inline std::expected<owning<item>, error> decode(std::string_view const encoded)
{
    return detail::decode_tree<false>(detail::heads::self_described_cbor_content(encoded));
}

/**
 * @brief Reads one data item from a range of bytes.
 * @ingroup decode
 */
template <borrowed_range_of_bytes Encoded>
    requires(!std::is_array_v<std::remove_cvref_t<Encoded>>)
std::expected<owning<item>, error> decode(Encoded &&encoded)
{
    return decode(detail::chars_of(encoded));
}

/**
 * @brief Decodes the one top-level item in `encoded` as cbor::decode() does, and resolves its tags 28 and 29.
 *
 * Tags 28 and 29 of the value-sharing registration are resolved, with a table for this call: a tag 28 makes no
 * node and marks the node of its content, and a tag 29 with the index n is a `shared_reference` node whose target is
 * the node that the n-th tag 28 marks. A tag 29 closes a cycle: its target is a node whose decode has not ended,
 * the node above the tag 29 or that node itself. A tag 29 that names a node whose decode has ended is an error. The
 * content of a tag 24 has its own table. cbor::encode() and cbor::to_edn() write such a tree, with a tag 28 and a
 * tag 29 only for a cycle.
 *
 * @code{.example}
 * bytes: d8 1c 81 d8 1d 00
 * call: auto const tree = cbor::decode_cycles(bytes)
 * result: owning item of 28([29(0)])
 * call: (*tree)->get<cbor::array_range>()->front().type()
 * result: cbor::type::shared_reference
 * @endcode
 *
 * @retval too_little_data, syntax_error, indefinite_length, nesting_depth_exceeded as cbor::decode()
 * @retval sharedref_index_not_marked a tag 29 whose index names no tag 28 before it
 * @retval sharedref_not_defined a tag 29 that names a tag 28 whose content is only tags 28 and this tag 29, as in
 * `28(29(0))`
 * @retval sharedref_not_cyclic a tag 29 that names a tag 28 whose content is decoded to its end, as in
 * `[28(1), 29(0)]`
 * @retval inadmissible_type_for_tag_content a tag 29 over an item that is not an unsigned integer, or a typed array
 * (RFC 8746) whose content is not a byte string of whole elements
 * @see cbor::decode()
 * @see cbor::to_edn(item const &)
 * @see cbor::encode()
 *
 * @ingroup decode
 */
inline std::expected<owning<item>, error> decode_cycles(std::string_view const encoded)
{
    return detail::decode_tree<true>(detail::heads::self_described_cbor_content(encoded));
}

/**
 * @brief Reads one data item from a range of bytes, and resolves its tags 28 and 29.
 * @ingroup decode
 */
template <borrowed_range_of_bytes Encoded>
    requires(!std::is_array_v<std::remove_cvref_t<Encoded>>)
std::expected<owning<item>, error> decode_cycles(Encoded &&encoded)
{
    return decode_cycles(detail::chars_of(encoded));
}

}
