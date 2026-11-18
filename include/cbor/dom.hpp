// Gives a tree of CBOR data items in the form of the yyjson DOM: an item of 16 bytes, all items of a tree
// in one array, each container followed by its descendants and holding the distance to its next sibling,
// and one copy of the input that the strings point into. Decodes CBOR into the tree, with tags 28 and 29
// kept as tags or resolved, and encodes the tree in the form of the yyjson writer, with tags 28 and 29
// for its cycles only.
// https://www.rfc-editor.org/rfc/rfc8949
// https://www.rfc-editor.org/rfc/rfc8746
// http://cbor.schmorp.de/value-sharing
// https://github.com/ibireme/yyjson
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hpp"
#include "head.hpp"
#include "ieee754.hpp"
#include "rfc8949.hpp"
#include "shared.hpp"
#include "validity.hpp"

namespace cbor
{

namespace detail
{

namespace dom
{

struct item {
    std::uint64_t tag;
    union {
        std::uint64_t u64;
        std::int64_t i64;
        double f64;
        char const *str;
        std::size_t ofs;
    } uni;
};
static_assert(sizeof(item) == sizeof(std::uint64_t) + sizeof(std::uint64_t));

namespace kind
{
inline constexpr std::uint8_t type_extended = 0;
inline constexpr std::uint8_t type_bytes = 1;
inline constexpr std::uint8_t type_null = 2;
inline constexpr std::uint8_t type_bool = 3;
inline constexpr std::uint8_t type_number = 4;
inline constexpr std::uint8_t type_text = 5;
inline constexpr std::uint8_t type_array = 6;
inline constexpr std::uint8_t type_map = 7;
inline constexpr int subtype_bit = 3;
inline constexpr std::uint8_t type_mask = (1 << subtype_bit) - 1;
inline constexpr int width_bit = 5;
inline constexpr int width_bits = 2;
inline constexpr std::uint8_t subtype_mask = ((1 << width_bit) - 1) & ~type_mask;
inline constexpr int info_bit = 8;
inline constexpr std::uint8_t extended_mask = std::numeric_limits<std::uint8_t>::max() & ~type_mask;
inline constexpr std::uint8_t width_mask = ((1 << width_bits) - 1) << width_bit;
inline constexpr int tag_bit = 13;
inline constexpr std::uint64_t info_mask = (std::uint64_t{1} << (tag_bit - info_bit)) - 1;

inline constexpr std::uint8_t subtype_false = 0 << subtype_bit;
inline constexpr std::uint8_t subtype_true = 1 << subtype_bit;
inline constexpr std::uint8_t subtype_undefined = 1 << subtype_bit;
inline constexpr std::uint8_t subtype_uint = 0 << subtype_bit;
inline constexpr std::uint8_t subtype_sint = 1 << subtype_bit;
inline constexpr std::uint8_t subtype_real = 2 << subtype_bit;
inline constexpr std::uint8_t subtype_negative = 3 << subtype_bit;

inline constexpr std::uint8_t extended_tag = 1 << subtype_bit;
inline constexpr std::uint8_t extended_simple = 2 << subtype_bit;
inline constexpr std::uint8_t extended_shared_reference = 3 << subtype_bit;
inline constexpr std::uint8_t extended_typed_array = 4 << subtype_bit;

constexpr std::uint8_t info_of(std::uint64_t const tag)
{
    return static_cast<std::uint8_t>(tag >> info_bit & info_mask);
}

constexpr std::uint64_t high_of(std::uint64_t const tag)
{
    return tag >> tag_bit;
}

constexpr std::uint64_t word(std::uint64_t const high, std::uint8_t const info, std::uint8_t const low)
{
    return high << tag_bit | std::uint64_t{info} << info_bit | low;
}

constexpr bool is_extended(item const &x, std::uint8_t const extended)
{
    return (x.tag & type_mask) == type_extended && (x.tag & extended_mask) == extended;
}

constexpr bool is_tag(item const &x)
{
    return is_extended(x, extended_tag) || is_extended(x, extended_typed_array);
}

constexpr std::size_t width_of(item const &x)
{
    switch (x.tag & type_mask) {
    case type_array:
    case type_map:
        return x.uni.ofs;
    case type_extended:
        if (is_tag(x))
            return static_cast<std::size_t>(high_of(x.tag));
        return 1;
    default:
        return 1;
    }
}
}

struct link {
    std::size_t tagged;
    std::size_t embedded;
};

struct tree {
    std::unique_ptr<item[]> values;
    std::size_t val_read;
    std::unique_ptr<char[]> text;
    std::size_t dat_read;
    std::vector<link> encoded_cbor_data_items;
    bool has_shared_reference;

    std::span<item const> items() const
    {
        return {values.get(), val_read};
    }

    std::span<item const> root() const
    {
        return items().first(kind::width_of(items().front()));
    }
};

class reader
{
    using heads = detail::heads;
    using validity = detail::validity;

    static constexpr std::size_t max_nesting_depth = 1024;

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

    struct items_decoder;

    static std::expected<void, error> check_nesting_depth(std::size_t const open_containers)
    {
        if (open_containers >= max_nesting_depth) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        return {};
    }

    static bool is_value_sharing(heads::head const h)
    {
        return h.major == major_type::tag &&
               (h.argument == std::to_underlying(rfc8949::tag_number::shareable) ||
                h.argument == std::to_underlying(rfc8949::tag_number::sharedref));
    }

    static major_type major_type_of(item const &x)
    {
        switch (x.tag & kind::type_mask) {
        case kind::type_bytes:
            return major_type::byte_string;
        case kind::type_text:
            return major_type::text_string;
        case kind::type_array:
            return major_type::array;
        case kind::type_map:
            return major_type::map;
        case kind::type_number:
            switch (x.tag & kind::subtype_mask) {
            case kind::subtype_uint:
                return major_type::unsigned_integer;
            case kind::subtype_real:
                return major_type::simple;
            default:
                return major_type::negative_integer;
            }
        case kind::type_extended:
            if (kind::is_extended(x, kind::extended_simple))
                return major_type::simple;
            return major_type::tag;
        default:
            return major_type::simple;
        }
    }

    static void set_integer(item &x, major_type const major, std::uint8_t const info,
                            std::uint64_t const argument)
    {
        if (major == major_type::unsigned_integer) {
            x.tag = kind::word(0, info, kind::type_number | kind::subtype_uint);
            x.uni.u64 = argument;
        } else if (argument <= std::uint64_t{std::numeric_limits<std::int64_t>::max()}) {
            x.tag = kind::word(0, info, kind::type_number | kind::subtype_sint);
            x.uni.i64 = static_cast<std::int64_t>(~argument);
        } else {
            x.tag = kind::word(0, info, kind::type_number | kind::subtype_negative);
            x.uni.u64 = argument;
        }
    }

    [[gnu::noinline]] static std::expected<void, error> check_typed_array_content(std::uint64_t const tag,
                                                                                  item const &content)
    {
        major_type const m = major_type_of(content);
        return validity::check_typed_array_content(
            tag, m, m == major_type::byte_string ? kind::high_of(content.tag) : std::uint64_t{});
    }

    [[gnu::noinline]] static std::expected<void, error> decode_sharedref(heads::decoder<true> &d, item &x,
                                                                         std::size_t const at,
                                                                         std::vector<std::size_t> &marks,
                                                                         std::size_t const marked)
    {
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (auto const c =
                validity::check_tag_content(std::to_underlying(rfc8949::tag_number::sharedref), h->major);
            !c) [[unlikely]]
            return std::unexpected(c.error());
        auto const index = value_sharing::check_sharedref_index(h->argument, marks.size());
        if (!index) [[unlikely]]
            return std::unexpected(h->argument - marks.size() < marked ? error::sharedref_not_defined
                                                                       : index.error());
        std::size_t const target = marks[std::to_underlying(*index)];
        marks.insert(marks.end(), marked, target);
        x.tag = kind::word(h->argument, h->info, kind::type_extended | kind::extended_shared_reference);
        x.uni.i64 = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(at);
        return {};
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

    struct items_decoder {
        std::span<item> values;
        std::vector<embedded_item> embedded;
        bool has_shared_reference;

        [[gnu::noinline]] void add_embedded(item const &content, std::size_t const tagged,
                                            std::size_t const depth)
        {
            embedded.push_back(
                {std::string_view(content.uni.str, static_cast<std::size_t>(kind::high_of(content.tag))),
                 tagged, depth, error{}});
        }

        template <bool SharedRefs>
        std::expected<std::size_t, error> decode_item(std::size_t const first, std::string_view const encoded,
                                                      std::size_t const depth, bool const one_item)
        {
            std::span<item> const v = values;
            heads::decoder<true> d{encoded};
            std::vector<frame> open_containers;
            std::vector<std::size_t> marks;
            std::size_t marked = 0;
            std::uint64_t left = 1;
            std::uint64_t open = 1;
            std::size_t val = first;
            for (;;) {
                auto const h = d.decode_head();
                if (!h) [[unlikely]]
                    return std::unexpected(h.error());
                --open;
                item &x = v[val];
                if constexpr (SharedRefs)
                    if (marked != 0 && !is_value_sharing(*h)) [[unlikely]] {
                        marks.insert(marks.end(), marked, val);
                        marked = 0;
                    }
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
                    x.tag =
                        kind::word(h->argument, h->info,
                                   h->major == major_type::text_string ? kind::type_text : kind::type_bytes);
                    x.uni.str = s->data();
                    break;
                }
                case major_type::array:
                case major_type::map: {
                    std::uint64_t const count = validity::count_enclosed_data_items(h->major, h->argument);
                    auto const reserved = validity::checked_add(open, count);
                    if (!reserved) [[unlikely]]
                        return std::unexpected(error::too_little_data);
                    if (auto const r = validity::check_bytes_left(*reserved, d.encoded.size()); !r)
                        [[unlikely]]
                        return std::unexpected(r.error());
                    x.tag = kind::word(h->argument, h->info,
                                       h->major == major_type::map ? kind::type_map : kind::type_array);
                    if (count != 0) {
                        if (auto const r = check_nesting_depth(depth + open_containers.size()); !r)
                            [[unlikely]]
                            return std::unexpected(r.error());
                        open = *reserved;
                        open_containers.push_back({val, left});
                        left = count;
                        ++val;
                        continue;
                    }
                    x.uni.ofs = 1;
                    break;
                }
                case major_type::tag: {
                    if constexpr (SharedRefs) {
                        if (h->argument == std::to_underlying(rfc8949::tag_number::shareable)) [[unlikely]] {
                            ++marked;
                            ++open;
                            continue;
                        }
                        if (h->argument == std::to_underlying(rfc8949::tag_number::sharedref)) [[unlikely]] {
                            if (auto const r = decode_sharedref(d, x, val, marks, marked); !r) [[unlikely]]
                                return std::unexpected(r.error());
                            marked = 0;
                            has_shared_reference = true;
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
                    x.tag = kind::word(0, h->info,
                                       kind::type_extended | (validity::is_typed_array_tag(h->argument)
                                                                  ? kind::extended_typed_array
                                                                  : kind::extended_tag));
                    x.uni.u64 = h->argument;
                    ++open;
                    open_containers.push_back({val, left});
                    left = 1;
                    ++val;
                    continue;
                }
                case major_type::simple:
                    if (heads::is_floating_point(h->major, h->info)) {
                        x.tag =
                            kind::word(0, h->info,
                                       static_cast<std::uint8_t>(
                                           kind::type_number | kind::subtype_real |
                                           (h->info - std::to_underlying(
                                                          rfc8949::simple_information::half_precision_float))
                                               << kind::width_bit));
                        x.uni.f64 = ieee754::decode_float(h->info, h->argument);
                        break;
                    }
                    switch (h->info) {
                    case std::to_underlying(simple_value::false_value):
                        x.tag = kind::word(0, h->info, kind::type_bool | kind::subtype_false);
                        break;
                    case std::to_underlying(simple_value::true_value):
                        x.tag = kind::word(0, h->info, kind::type_bool | kind::subtype_true);
                        break;
                    case std::to_underlying(simple_value::null):
                        x.tag = kind::word(0, h->info, kind::type_null);
                        break;
                    case std::to_underlying(simple_value::undefined):
                        x.tag = kind::word(0, h->info, kind::type_null | kind::subtype_undefined);
                        break;
                    default:
                        x.tag = kind::word(0, h->info, kind::type_extended | kind::extended_simple);
                        x.uni.u64 = h->argument;
                        break;
                    }
                    break;
                }
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
                    item &c = v[f.at];
                    if ((c.tag & kind::type_mask) != kind::type_extended) {
                        c.uni.ofs = val - f.at;
                        continue;
                    }
                    if (kind::is_extended(c, kind::extended_typed_array)) [[unlikely]] {
                        if (auto const r = check_typed_array_content(c.uni.u64, v[f.at + 1]); !r) [[unlikely]]
                            return std::unexpected(r.error());
                    } else if (c.uni.u64 == std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item) &&
                               (v[f.at + 1].tag & kind::type_mask) == kind::type_bytes) [[unlikely]] {
                        add_embedded(v[f.at + 1], f.at, depth + open_containers.size() + 1);
                    }
                    c.tag |= std::uint64_t{val - f.at} << kind::tag_bit;
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

public:
    template <bool SharedRefs>
    static std::expected<tree, error> decode(std::string_view const bytes)
    {
        std::string_view const encoded = heads::self_described_cbor_content(bytes);
        std::size_t const size = encoded.size();
        if (size == 0) [[unlikely]]
            return std::unexpected(error::too_little_data);
        auto text = std::make_unique_for_overwrite<char[]>(size);
        std::ranges::copy(encoded, text.get());
        auto values = std::make_unique_for_overwrite<item[]>(size);
        items_decoder out{std::span<item>(values.get(), size), {}, false};
        auto const outer = out.decode_item<SharedRefs>(0, std::string_view(text.get(), size), 0, false);
        if (out.embedded.empty() && outer) [[likely]]
            return tree{std::move(values), *outer, std::move(text), size, {}, out.has_shared_reference};
        bool failed = !outer;
        std::size_t written = outer.value_or(0);
        std::vector<embedded_item> pending;
        std::vector<link> links;
        schedule(pending, out.embedded, outer);
        while (!pending.empty()) {
            embedded_item const e = pending.back();
            pending.pop_back();
            if (e.failure != error{}) [[unlikely]]
                return std::unexpected(e.failure);
            std::size_t const first = failed ? 0 : written;
            auto const inner = out.decode_item<SharedRefs>(first, e.encoded, e.depth, true);
            failed = failed || !inner;
            if (!failed) {
                links.push_back({e.tagged, first});
                written = *inner;
            }
            schedule(pending, out.embedded, inner);
        }
        std::ranges::sort(links, {}, &link::tagged);
        return tree{std::move(values),       written, std::move(text), size, std::move(links),
                    out.has_shared_reference};
    }
};

class writer
{
    using heads = detail::heads;
    using validity = detail::validity;

    static constexpr std::size_t growth_divisor = 2;

    static constexpr std::size_t not_open = std::numeric_limits<std::size_t>::max();

    struct output {
        std::string &out;
        std::size_t start;
        std::span<char> buffer;
        std::size_t used;

        [[gnu::noinline]] void grow(std::size_t const needed)
        {
            std::size_t const size = std::max(buffer.size() + buffer.size() / growth_divisor, used + needed);
            out.resize(start + size);
            buffer = std::span<char>(out).subspan(start);
        }

        void reserve(std::size_t const needed)
        {
            if (needed > buffer.size() - used) [[unlikely]]
                grow(needed);
        }

        void write_head(major_type const major, std::uint8_t const info, std::uint64_t const argument)
        {
            reserve(heads::head_size_max);
            used += heads::write_head(buffer, used, major, info, argument);
        }

        std::size_t finish()
        {
            out.resize(start + used);
            return used;
        }

        void discard()
        {
            out.resize(start);
        }

        void write_string(major_type const major, std::uint8_t const info, std::string_view const content)
        {
            reserve(heads::head_size_max + content.size());
            used += heads::write_head(buffer, used, major, info, content.size());
            std::ranges::copy(content, buffer.subspan(used).begin());
            used += content.size();
        }
    };

    static std::expected<void, error> write_item(output &o, item const &x)
    {
        std::uint8_t const info = kind::info_of(x.tag);
        switch (x.tag & kind::type_mask) {
        case kind::type_null:
        case kind::type_bool:
            o.write_head(major_type::simple, info, info);
            return {};
        case kind::type_number:
            switch (x.tag & kind::subtype_mask) {
            case kind::subtype_uint:
                o.write_head(major_type::unsigned_integer, info, x.uni.u64);
                return {};
            case kind::subtype_sint:
                o.write_head(major_type::negative_integer, info, static_cast<std::uint64_t>(~x.uni.i64));
                return {};
            case kind::subtype_negative:
                o.write_head(major_type::negative_integer, info, x.uni.u64);
                return {};
            default:
                o.write_head(
                    major_type::simple, info,
                    ieee754::encode_float(static_cast<rfc8949::simple_information>(info), x.uni.f64));
                return {};
            }
        case kind::type_bytes:
            o.write_string(major_type::byte_string, info,
                           std::string_view(x.uni.str, static_cast<std::size_t>(kind::high_of(x.tag))));
            return {};
        case kind::type_text:
            o.write_string(major_type::text_string, info,
                           std::string_view(x.uni.str, static_cast<std::size_t>(kind::high_of(x.tag))));
            return {};
        case kind::type_array:
            o.write_head(major_type::array, info, kind::high_of(x.tag));
            return {};
        case kind::type_map:
            o.write_head(major_type::map, info, kind::high_of(x.tag));
            return {};
        default:
            switch (x.tag & kind::extended_mask) {
            case kind::extended_tag:
            case kind::extended_typed_array:
                o.write_head(major_type::tag, info, x.uni.u64);
                return {};
            case kind::extended_simple:
                o.write_head(major_type::simple, info, x.uni.u64);
                return {};
            default:
                return std::unexpected(error::cyclic_data_structure);
            }
        }
    }

    static void write_preferred_head(output &o, major_type const major, std::uint64_t const argument)
    {
        o.write_head(major, heads::preferred_argument_info(argument), argument);
    }

    static std::uint64_t children_of(item const &x)
    {
        switch (x.tag & kind::type_mask) {
        case kind::type_array:
            return kind::high_of(x.tag);
        case kind::type_map:
            return rfc8949::data_items_per_pair * kind::high_of(x.tag);
        case kind::type_extended:
            return kind::is_tag(x) ? 1 : 0;
        default:
            return 0;
        }
    }

    static std::uint64_t size_of(item const &x)
    {
        std::uint64_t const head = heads::initial_byte_size + heads::argument_size(kind::info_of(x.tag));
        auto const type = x.tag & kind::type_mask;
        if (type == kind::type_bytes || type == kind::type_text)
            return head + kind::high_of(x.tag);
        return head;
    }

    class depth_first_search
    {
        struct frame {
            std::size_t node;
            std::size_t child;
            std::uint64_t left;
            bool below;
        };

        static constexpr std::uint64_t back_edge_size =
            heads::head_size(std::to_underlying(rfc8949::tag_number::sharedref)) + heads::initial_byte_size;

        std::span<item const> items;
        std::vector<std::uint8_t> on_path;
        std::vector<std::uint8_t> target;
        std::vector<std::size_t> open;
        std::uint64_t direct = 0;
        std::uint64_t in_tree = 0;

        std::size_t followed(std::size_t const at) const
        {
            if (kind::is_extended(items[at], kind::extended_shared_reference)) [[unlikely]]
                return static_cast<std::size_t>(static_cast<std::int64_t>(at) + items[at].uni.i64);
            return at;
        }

        std::expected<void, error> count(std::uint64_t const bytes, bool const below)
        {
            direct += bytes;
            if (!below)
                in_tree += bytes;
            return validity::check_amplification(direct, in_tree);
        }

    public:
        explicit depth_first_search(std::span<item const> const tree_items)
            : items(tree_items), on_path(tree_items.size()), target(tree_items.size()),
              open(tree_items.size(), not_open)
        {
        }

        std::expected<void, error> find_back_edges(std::size_t const root)
        {
            std::vector<frame> path;
            if (auto const r = count(size_of(items[root]), false); !r) [[unlikely]]
                return r;
            on_path[root] = 1;
            path.push_back({root, root + 1, children_of(items[root]), false});
            while (!path.empty()) {
                frame &f = path.back();
                if (f.left == 0) {
                    on_path[f.node] = 0;
                    path.pop_back();
                    continue;
                }
                std::size_t const child = f.child;
                bool const below = f.below;
                f.child += kind::width_of(items[child]);
                --f.left;
                bool const reference = kind::is_extended(items[child], kind::extended_shared_reference);
                std::size_t const next = followed(child);
                if (reference && !below)
                    in_tree += heads::head_size(std::to_underlying(rfc8949::tag_number::sharedref)) +
                               heads::initial_byte_size +
                               heads::argument_size(kind::info_of(items[child].tag));
                if (on_path[next] != 0) {
                    target[next] = 1;
                    if (auto const r = count(back_edge_size, true); !r) [[unlikely]]
                        return r;
                    continue;
                }
                if (auto const r = count(size_of(items[next]), below || reference); !r) [[unlikely]]
                    return r;
                on_path[next] = 1;
                path.push_back({next, next + 1, children_of(items[next]), below || reference});
            }
            return {};
        }

        std::expected<void, error> write(output &o, std::size_t const root)
        {
            std::vector<frame> path;
            std::size_t marks = 0;
            std::size_t next = root;
            for (;;) {
                std::size_t const index = marks;
                if (target[next] != 0) {
                    write_preferred_head(o, major_type::tag,
                                         std::to_underlying(rfc8949::tag_number::shareable));
                    ++marks;
                }
                if (auto const r = write_item(o, items[next]); !r) [[unlikely]]
                    return r;
                open[next] = index;
                path.push_back({next, next + 1, children_of(items[next]), false});
                for (;;) {
                    if (path.empty())
                        return {};
                    frame &f = path.back();
                    if (f.left == 0) {
                        open[f.node] = not_open;
                        path.pop_back();
                        continue;
                    }
                    std::size_t const child = f.child;
                    f.child += kind::width_of(items[child]);
                    --f.left;
                    next = followed(child);
                    if (open[next] == not_open)
                        break;
                    write_preferred_head(o, major_type::tag,
                                         std::to_underlying(rfc8949::tag_number::sharedref));
                    write_preferred_head(o, major_type::unsigned_integer, open[next]);
                }
            }
        }
    };

    static output output_for(std::string &out, std::size_t const estimate)
    {
        output o{out, out.size(), {}, 0};
        o.grow(estimate + heads::head_size_max);
        return o;
    }

public:
    static std::expected<std::size_t, error> encode(std::string &out, tree const &t)
    {
        output o = output_for(out, t.dat_read);
        for (item const &x : t.root())
            if (auto const r = write_item(o, x); !r) [[unlikely]] {
                o.discard();
                return std::unexpected(r.error());
            }
        return o.finish();
    }

    static std::expected<std::size_t, error> encode_cycles(std::string &out, tree const &t)
    {
        if (!t.has_shared_reference) [[likely]]
            return encode(out, t);
        std::span<item const> const items = t.root();
        depth_first_search walk(items);
        if (auto const r = walk.find_back_edges(0); !r) [[unlikely]]
            return std::unexpected(r.error());
        output o = output_for(out, t.dat_read);
        if (auto const r = walk.write(o, 0); !r) [[unlikely]] {
            o.discard();
            return std::unexpected(r.error());
        }
        return o.finish();
    }
};

inline std::expected<tree, error> decode(std::string_view const encoded)
{
    return reader::decode<false>(encoded);
}

inline std::expected<tree, error> decode_shared_refs(std::string_view const encoded)
{
    return reader::decode<true>(encoded);
}

inline std::expected<std::size_t, error> encode(std::string &out, tree const &decoded)
{
    return writer::encode(out, decoded);
}

inline std::expected<std::size_t, error> encode_cycles(std::string &out, tree const &decoded)
{
    return writer::encode_cycles(out, decoded);
}

}

}

}
