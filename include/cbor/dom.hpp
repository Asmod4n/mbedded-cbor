// Gives a tree of CBOR data items in the form of the yyjson DOM: an item of 16 bytes, all items of a tree
// in one array, each container followed by its descendants and holding the distance to its next sibling,
// and one padded copy of the input that the strings point into. Decodes CBOR into the tree, with tags 28
// and 29 kept as tags or resolved, reads JSON text into the tree in the form of the yyjson reader, and
// encodes the tree in the form of the yyjson writer, with tags 28 and 29 for its cycles only.
// https://www.rfc-editor.org/rfc/rfc8949
// https://www.rfc-editor.org/rfc/rfc8259
// https://www.rfc-editor.org/rfc/rfc8746
// http://cbor.schmorp.de/value-sharing
// https://github.com/ibireme/yyjson
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "diagnostic_notation.hpp"
#include "error.hpp"
#include "head.hpp"
#include "ieee754.hpp"
#include "padded_string.hpp"
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
inline constexpr std::uint64_t high_bound = std::uint64_t{1}
                                            << (std::numeric_limits<std::uint64_t>::digits - tag_bit);

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

inline constexpr std::size_t growth_divisor = 2;

struct link {
    std::size_t tagged;
    std::size_t embedded;
};

struct tree {
    std::unique_ptr<item[]> values;
    std::size_t val_read;
    padded_string text;
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

inline constexpr std::size_t utf8_seq_size = 4;

constexpr std::uint32_t utf8_seq(std::uint8_t const b0, std::uint8_t const b1, std::uint8_t const b2,
                                 std::uint8_t const b3)
{
    return std::bit_cast<std::uint32_t>(std::array<std::uint8_t, utf8_seq_size>{b0, b1, b2, b3});
}

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

    static void set_float(item &x, std::uint8_t const info, double const value)
    {
        x.tag = kind::word(0, info,
                           static_cast<std::uint8_t>(
                               kind::type_number | kind::subtype_real |
                               (info - std::to_underlying(rfc8949::simple_information::half_precision_float))
                                   << kind::width_bit));
        x.uni.f64 = value;
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
                        set_float(x, h->info, ieee754::decode_float(h->info, h->argument));
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

    using edn = extended_diagnostic_notation;

    static constexpr std::size_t estimated_minify_ratio = 6;
    static constexpr std::size_t estimated_pretty_ratio = 16;
    static constexpr std::size_t estimated_extra = 4;
    static constexpr std::size_t pair_values = 2;
    static constexpr std::size_t pretty_probe = 2;
    static constexpr std::string_view pretty_indent = "  ";
    static constexpr std::size_t indent_repeat = 16;
    static constexpr std::string_view pretty_comma = ",\n";
    static constexpr std::string_view pretty_colon = ": ";
    static constexpr std::string_view literal_true = "true";
    static constexpr std::string_view literal_false = "false";
    static constexpr std::string_view literal_null = "null";
    static constexpr std::size_t failed = 0;
    static constexpr std::size_t two_octets = 2;
    static constexpr std::size_t three_octets = 3;
    static constexpr std::size_t four_octets = 4;

    static constexpr std::uint32_t b1_mask = utf8_seq(0x80, 0x00, 0x00, 0x00);
    static constexpr std::uint32_t b1_patt = utf8_seq(0x00, 0x00, 0x00, 0x00);
    static constexpr std::uint32_t b2_mask = utf8_seq(0xE0, 0xC0, 0x00, 0x00);
    static constexpr std::uint32_t b2_patt = utf8_seq(0xC0, 0x80, 0x00, 0x00);
    static constexpr std::uint32_t b2_requ = utf8_seq(0x1E, 0x00, 0x00, 0x00);
    static constexpr std::uint32_t b3_mask = utf8_seq(0xF0, 0xC0, 0xC0, 0x00);
    static constexpr std::uint32_t b3_patt = utf8_seq(0xE0, 0x80, 0x80, 0x00);
    static constexpr std::uint32_t b3_requ = utf8_seq(0x0F, 0x20, 0x00, 0x00);
    static constexpr std::uint32_t b3_erro = utf8_seq(0x0D, 0x20, 0x00, 0x00);
    static constexpr std::uint32_t b4_mask = utf8_seq(0xF8, 0xC0, 0xC0, 0xC0);
    static constexpr std::uint32_t b4_patt = utf8_seq(0xF0, 0x80, 0x80, 0x80);
    static constexpr std::uint32_t b4_requ = utf8_seq(0x07, 0x30, 0x00, 0x00);
    static constexpr std::uint32_t b4_req1 = utf8_seq(0x04, 0x00, 0x00, 0x00);
    static constexpr std::uint32_t b4_req2 = utf8_seq(0x03, 0x30, 0x00, 0x00);

    CBOR_ALWAYS_INLINE static bool is_utf8_seq1(std::uint32_t const uni)
    {
        return (uni & b1_mask) == b1_patt;
    }

    CBOR_ALWAYS_INLINE static bool is_utf8_seq2(std::uint32_t const uni)
    {
        return (uni & b2_mask) == b2_patt && (uni & b2_requ) != 0;
    }

    CBOR_ALWAYS_INLINE static bool is_utf8_seq3(std::uint32_t const uni)
    {
        std::uint32_t const requ = uni & b3_requ;
        return (uni & b3_mask) == b3_patt && requ != 0 && requ != b3_erro;
    }

    CBOR_ALWAYS_INLINE static bool is_utf8_seq4(std::uint32_t const uni)
    {
        std::uint32_t const requ = uni & b4_requ;
        return (uni & b4_mask) == b4_patt && requ != 0 && ((requ & b4_req1) == 0 || (requ & b4_req2) == 0);
    }

    template <std::size_t N>
    [[gnu::always_inline, gnu::flatten]] static std::array<char, N> bytes_at(std::span<char const> const buf,
                                                                             std::size_t const at)
    {
        std::array<char, N> bytes;
        std::ranges::copy(buf.subspan(at).first<N>(), bytes.begin());
        return bytes;
    }

    template <std::size_t N>
    [[gnu::always_inline, gnu::flatten]] static void byte_move(std::span<char> const buf,
                                                               std::size_t const dst, std::size_t const src)
    {
        std::ranges::copy(bytes_at<N>(buf, src), buf.subspan(dst).begin());
    }

    CBOR_ALWAYS_INLINE static void byte_move_forward(std::span<char> const buf, std::size_t dst,
                                                     std::size_t src, std::size_t n)
    {
        constexpr std::size_t eight = 8;
        constexpr std::size_t four = 4;
        constexpr std::size_t two = 2;
        n += n % two;
        if (n == edn::repeat16) {
            byte_move<edn::repeat16>(buf, dst, src);
            return;
        }
        if (n >= eight) {
            byte_move<eight>(buf, dst, src);
            n -= eight;
            dst += eight;
            src += eight;
        }
        if (n >= four) {
            byte_move<four>(buf, dst, src);
            n -= four;
            dst += four;
            src += four;
        }
        if (n >= two)
            byte_move<two>(buf, dst, src);
    }

    [[gnu::always_inline, gnu::flatten]] static std::uint32_t byte_load_4(std::span<char const> const buf,
                                                                          std::size_t const at)
    {
        return std::bit_cast<std::uint32_t>(bytes_at<utf8_seq_size>(buf, at));
    }

    [[gnu::always_inline, gnu::flatten]] static void
    byte_store(std::span<char> const buf, std::size_t const dst, std::uint32_t const uni, std::size_t const n)
    {
        std::ranges::copy_n(std::bit_cast<std::array<char, utf8_seq_size>>(uni).begin(),
                            static_cast<std::ptrdiff_t>(n), buf.subspan(dst).begin());
    }

    template <std::size_t N>
    [[gnu::always_inline, gnu::flatten]] static bool
    byte_match(std::span<char const> const buf, std::size_t const at, std::string_view const pattern)
    {
        return std::ranges::equal(bytes_at<N>(buf, at), pattern.substr(0, N));
    }

    CBOR_ALWAYS_INLINE static unsigned char byte(std::span<char const> const buf, std::size_t const at)
    {
        return static_cast<unsigned char>(buf[at]);
    }

    struct in_place_writer {
        std::span<char> buf;
        std::size_t &dst;

        void push_back(char const c)
        {
            buf[dst] = c;
            ++dst;
        }
    };

    [[gnu::always_inline, gnu::flatten]] static bool hex_load_4(std::span<char const> const buf,
                                                                std::size_t const at, std::uint32_t &value)
    {
        std::uint8_t invalid = 0;
        value = 0;
        for (char const c : buf.subspan(at).first<edn::hex4_digits>()) {
            std::uint8_t const v = edn::hex_conv_table[static_cast<unsigned char>(c)];
            invalid |= v;
            value = value << rfc4648::base16_group_bits | v;
        }
        return (invalid & edn::hex_conv_invalid) == 0;
    }

    [[gnu::always_inline, gnu::flatten]] static bool read_uni_esc(std::span<char> const buf, std::size_t &src,
                                                                  std::size_t &dst)
    {
        std::uint32_t hi;
        std::uint32_t lo;
        src += edn::escape_prefix.size();
        if (!hex_load_4(buf, src, hi)) [[unlikely]]
            return false;
        src += edn::hex4_digits;
        in_place_writer out{buf, dst};
        if (hi < rfc2781::high_surrogate_first || hi > rfc2781::low_surrogate_last) [[likely]] {
            edn::append_utf8(out, hi);
            return true;
        }
        if (hi >= rfc2781::low_surrogate_first) [[unlikely]]
            return false;
        if (!byte_match<edn::escape_prefix.size()>(buf, src, edn::escape_prefix)) [[unlikely]]
            return false;
        if (!hex_load_4(buf, src + edn::escape_prefix.size(), lo)) [[unlikely]]
            return false;
        if (lo < rfc2781::low_surrogate_first || lo > rfc2781::low_surrogate_last) [[unlikely]]
            return false;
        edn::append_utf8(out, rfc2781::surrogate_pair_first +
                                  ((hi - rfc2781::high_surrogate_first) << rfc2781::surrogate_bits) +
                                  (lo - rfc2781::low_surrogate_first));
        src += edn::escape_prefix.size() + edn::hex4_digits;
        return true;
    }

    CBOR_ALWAYS_INLINE static void set_text(item &x, std::span<char const> const buf, std::size_t const first,
                                            std::size_t const size)
    {
        x.tag = kind::word(size, heads::preferred_argument_info(size), kind::type_text);
        x.uni.str = buf.subspan(first).data();
    }

    CBOR_ALWAYS_INLINE static bool is_escape_or_end(unsigned char const c)
    {
        return c <= rfc3629::one_octet_last;
    }

    CBOR_ALWAYS_INLINE static std::size_t read_str(std::span<char> const buf, std::size_t const at, item &x)
    {
        std::size_t const hdr = at + 1;
        std::size_t src = hdr;
        std::size_t dst = 0;
        std::size_t pos = 0;
        std::uint32_t uni = 0;

    skip_ascii:
        for (;;) {
#pragma GCC unroll edn::repeat16
            for (std::size_t i = 0; i < edn::repeat16; ++i) {
                if (!edn::char_is_ascii_skip(byte(buf, src + i))) [[unlikely]] {
                    src += i;
                    goto skip_ascii_end;
                }
            }
            src += edn::repeat16;
        }

    skip_ascii_end:
        if (buf[src] == '"') [[likely]] {
            set_text(x, buf, hdr, src - hdr);
            return src + 1;
        }

        if (!is_escape_or_end(byte(buf, src))) {
            pos = src;
            uni = byte_load_4(buf, src);
            while (is_utf8_seq3(uni)) {
                src += three_octets;
                uni = byte_load_4(buf, src);
            }
            if (is_utf8_seq1(uni))
                goto skip_ascii;
            while (is_utf8_seq2(uni)) {
                src += two_octets;
                uni = byte_load_4(buf, src);
            }
            while (is_utf8_seq4(uni)) {
                src += four_octets;
                uni = byte_load_4(buf, src);
            }
            if (pos == src) [[unlikely]]
                return failed;
            goto skip_ascii;
        }

        dst = src;
    copy_escape:
        if (buf[src] == '\\') [[likely]] {
            switch (buf[++src]) {
            case '"':
                buf[dst++] = '"';
                ++src;
                break;
            case '\\':
                buf[dst++] = '\\';
                ++src;
                break;
            case '/':
                buf[dst++] = '/';
                ++src;
                break;
            case 'b':
                buf[dst++] = '\b';
                ++src;
                break;
            case 'f':
                buf[dst++] = '\f';
                ++src;
                break;
            case 'n':
                buf[dst++] = '\n';
                ++src;
                break;
            case 'r':
                buf[dst++] = '\r';
                ++src;
                break;
            case 't':
                buf[dst++] = '\t';
                ++src;
                break;
            case 'u':
                --src;
                if (!read_uni_esc(buf, src, dst)) [[unlikely]]
                    return failed;
                break;
            [[unlikely]] default:
                return failed;
            }
        } else if (buf[src] == '"') [[likely]] {
            set_text(x, buf, hdr, dst - hdr);
            return src + 1;
        } else [[unlikely]] {
            return failed;
        }

    copy_ascii:
        for (;;) {
#pragma GCC unroll edn::repeat16
            for (std::size_t i = 0; i < edn::repeat16; ++i) {
                if (!edn::char_is_ascii_skip(byte(buf, src + i))) [[unlikely]] {
                    byte_move_forward(buf, dst, src, i);
                    dst += i;
                    src += i;
                    goto copy_utf8;
                }
            }
            byte_move<edn::repeat16>(buf, dst, src);
            dst += edn::repeat16;
            src += edn::repeat16;
        }

    copy_utf8:
        if (!is_escape_or_end(byte(buf, src))) {
            pos = src;
            uni = byte_load_4(buf, src);
            while (is_utf8_seq3(uni)) {
                byte_store(buf, dst, uni, utf8_seq_size);
                dst += three_octets;
                src += three_octets;
                uni = byte_load_4(buf, src);
            }
            if (is_utf8_seq1(uni))
                goto copy_ascii;
            while (is_utf8_seq2(uni)) {
                byte_store(buf, dst, uni, two_octets);
                dst += two_octets;
                src += two_octets;
                uni = byte_load_4(buf, src);
            }
            while (is_utf8_seq4(uni)) {
                byte_store(buf, dst, uni, four_octets);
                dst += four_octets;
                src += four_octets;
                uni = byte_load_4(buf, src);
            }
            if (pos == src) [[unlikely]]
                return failed;
            goto copy_ascii;
        }
        goto copy_escape;
    }

    CBOR_ALWAYS_INLINE static std::size_t
    read_num(std::span<char const> const buf, std::string_view const text, std::size_t const hdr, item &x)
    {
        enum class digi { intg_more, frac, frac_more, frac_end, exp_more, exp_finish, finish };
        auto const ch = [buf](std::size_t const at) { return static_cast<unsigned char>(buf[at]); };
        bool const sign = ch(hdr) == '-';
        std::size_t cur = hdr + (sign ? 1 : 0);
        auto const return_i64 = [&x, &cur, sign](std::uint64_t const v) {
            if (sign && v != 0)
                set_integer(x, major_type::negative_integer, heads::preferred_argument_info(v - 1), v - 1);
            else
                set_integer(x, major_type::unsigned_integer, heads::preferred_argument_info(v), v);
            return cur;
        };
        auto const return_f64 = [&x, &cur, sign](double const v) {
            double const d = sign ? -v : v;
            set_float(x, std::to_underlying(ieee754::preferred_float_info(d)), d);
            return cur;
        };
        auto const return_f64_bin = [&return_f64](std::uint64_t const raw) {
            return return_f64(std::bit_cast<double>(raw));
        };
        auto const digit_value = [&ch](std::size_t const at) {
            return static_cast<std::uint64_t>(ch(at) - '0');
        };

        std::size_t sig_cut = std::string_view::npos;
        std::size_t sig_end = std::string_view::npos;
        std::size_t dot_pos = std::string_view::npos;
        std::uint64_t sig = 0;
        std::int32_t exp = 0;
        std::int64_t exp_sig = 0;
        std::int64_t exp_lit = 0;
        std::size_t i = 1;
        digi next = digi::finish;

        if (!edn::char_is_nonzero(ch(cur))) {
            if (ch(cur) != '0') [[unlikely]]
                return failed;
            ++cur;
            if (!edn::char_is_digit_or_fp(ch(cur))) [[likely]] {
                return return_i64(0);
            }
            if (ch(cur) == '.') {
                dot_pos = cur++;
                if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                    return failed;
                while (ch(cur) == '0')
                    ++cur;
                if (edn::char_is_digit(ch(cur))) {
                    sig = digit_value(cur);
                    --cur;
                    next = digi::frac;
                }
            }
            if (next != digi::frac) {
                if (edn::char_is_digit(ch(cur))) [[unlikely]]
                    return failed;
                if (edn::char_is_exp(ch(cur))) {
                    cur += 1 + (edn::char_is_sign(ch(cur + 1)) ? std::size_t{1} : 0);
                    if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                        return failed;
                    for (; edn::char_is_digit(ch(cur)); ++cur) {
                        if (exp_lit >= edn::exponent_beyond_double) [[unlikely]]
                            return failed;
                        exp_lit = static_cast<std::int64_t>(digit_value(cur)) + exp_lit * edn::decimal;
                    }
                }
                return return_f64_bin(0);
            }
        } else {
            sig = digit_value(cur);
#pragma GCC unroll edn::u64_safe_dig - 1
            for (; i < edn::u64_safe_dig; ++i) {
                std::uint64_t const num = digit_value(cur + i);
                if (num >= edn::decimal)
                    break;
                sig = num + sig * edn::decimal;
            }
            if (i == edn::u64_safe_dig) {
                cur += edn::u64_safe_dig;
                if (!edn::char_is_digit_or_fp(ch(cur))) {
                    return return_i64(sig);
                }
                next = digi::intg_more;
            } else {
                if (!edn::char_is_fp(ch(cur + i))) [[likely]] {
                    cur += i;
                    return return_i64(sig);
                }
                dot_pos = cur + i;
                if (ch(cur + i) == '.') {
                    next = digi::frac;
                } else {
                    cur += i;
                    sig_end = cur;
                    next = digi::exp_more;
                }
            }
        }

        while (next != digi::finish) {
            switch (next) {
            case digi::frac:
#pragma GCC unroll edn::u64_safe_dig - 1
                for (; i < edn::u64_safe_dig; ++i) {
                    std::uint64_t const num = digit_value(cur + i + 1);
                    if (num >= edn::decimal)
                        break;
                    sig = num + sig * edn::decimal;
                }
                cur += i + 1;
                next = edn::char_is_digit(ch(cur)) ? digi::frac_more : digi::frac_end;
                break;
            case digi::intg_more: {
                unsigned char const c = ch(cur);
                if (edn::char_is_digit(c) && !edn::char_is_digit_or_fp(ch(cur + 1))) {
                    std::uint64_t const num = digit_value(cur);
                    constexpr std::uint64_t u64_max = std::numeric_limits<std::uint64_t>::max();
                    if (sig < u64_max / edn::decimal ||
                        (sig == u64_max / edn::decimal && num <= u64_max % edn::decimal)) {
                        sig = num + sig * edn::decimal;
                        ++cur;
                        return return_i64(sig);
                    }
                }
                if (edn::char_is_exp(c)) {
                    dot_pos = cur;
                    next = digi::exp_more;
                    break;
                }
                if (c == '.') {
                    dot_pos = cur++;
                    if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                        return failed;
                }
                next = digi::frac_more;
                break;
            }
            case digi::frac_more: {
                sig_cut = cur;
                sig += ch(cur) >= '5' ? std::uint64_t{1} : 0;
                do
                    ++cur;
                while (edn::char_is_digit(ch(cur)));
                if (dot_pos == std::string_view::npos) {
                    if (!edn::char_is_fp(ch(cur))) [[unlikely]]
                        return failed;
                    dot_pos = cur;
                    if (ch(cur) == '.') {
                        ++cur;
                        if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                            return failed;
                        while (edn::char_is_digit(ch(cur)))
                            ++cur;
                    }
                }
                exp_sig = static_cast<std::int64_t>(dot_pos) - static_cast<std::int64_t>(sig_cut) +
                          (dot_pos < sig_cut ? 1 : 0);
                std::size_t tmp = cur - 1;
                while ((ch(tmp) == '0' || ch(tmp) == '.') && tmp > hdr)
                    --tmp;
                if (tmp < sig_cut)
                    sig_cut = std::string_view::npos;
                else
                    sig_end = cur;
                next = edn::char_is_exp(ch(cur)) ? digi::exp_more : digi::exp_finish;
                break;
            }
            case digi::frac_end:
                if (dot_pos + 1 == cur) [[unlikely]]
                    return failed;
                sig_end = cur;
                exp_sig = -static_cast<std::int64_t>(cur - dot_pos - 1);
                if (edn::char_is_exp(ch(cur))) {
                    next = digi::exp_more;
                    break;
                }
                if (exp_sig < edn::f64_min_dec_exp - edn::u64_safe_dig) [[unlikely]]
                    return failed;
                exp = static_cast<std::int32_t>(exp_sig);
                next = digi::finish;
                break;
            case digi::exp_more: {
                ++cur;
                bool const exp_sign = ch(cur) == '-';
                cur += edn::char_is_sign(ch(cur)) ? std::size_t{1} : 0;
                if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                    return failed;
                while (ch(cur) == '0')
                    ++cur;
                for (; edn::char_is_digit(ch(cur)); ++cur) {
                    if (exp_lit >= edn::exponent_beyond_double) [[unlikely]]
                        return failed;
                    exp_lit = static_cast<std::int64_t>(digit_value(cur)) + exp_lit * edn::decimal;
                }
                exp_sig += exp_sign ? -exp_lit : exp_lit;
                next = digi::exp_finish;
                break;
            }
            case digi::exp_finish:
                if (exp_sig < edn::f64_min_dec_exp - edn::u64_safe_dig || exp_sig > edn::f64_max_dec_exp)
                    [[unlikely]]
                    return failed;
                exp = static_cast<std::int32_t>(exp_sig);
                next = digi::finish;
                break;
            case digi::finish:
                break;
            }
        }

        if constexpr (edn::double_math_correct) {
            if (sig < (std::uint64_t{1} << edn::f64_sig_full_bits) && exp >= -edn::f64_pow10_max_exact_exp &&
                exp <= edn::f64_pow10_max_exact_exp) {
                double dbl = static_cast<double>(sig);
                if (exp < 0)
                    dbl /= edn::f64_pow10_table[static_cast<std::size_t>(-exp)];
                else
                    dbl *= edn::f64_pow10_table[static_cast<std::size_t>(exp)];
                return return_f64(dbl);
            }
        }
        if (sig == 0) [[unlikely]] {
            return return_f64_bin(0);
        }

        if (sig_cut == std::string_view::npos && exp > -edn::f64_max_dec_exp + 1 &&
            exp < edn::f64_max_dec_exp - (edn::u64_safe_dig + 1)) [[likely]] {
            constexpr int low_bits = edn::f64_bits - (edn::f64_sig_full_bits + 1) - 1;
            constexpr std::uint64_t low_mask = (std::uint64_t{1} << low_bits) - 1;
            constexpr std::uint64_t round_bit = std::uint64_t{1}
                                                << (edn::f64_bits - (edn::f64_sig_full_bits + 1));
            constexpr std::uint64_t top_bit = std::uint64_t{1} << (edn::f64_bits - 1);
            edn::uint128 const sig2 = edn::pow10_table_get_sig(exp);
            int const lz = std::countl_zero(sig);
            std::uint64_t const sig1 = sig << lz;
            std::int32_t exp2 = edn::pow10_table_get_exp(exp) - lz;
            edn::uint128 const m = edn::u128_mul(sig1, sig2.hi);
            std::uint64_t hi = m.hi;
            bool exact = false;
            std::uint64_t const bits = hi & low_mask;
            if (bits - 1 < low_mask - 1) {
                exact = true;
            } else {
                edn::uint128 const m2 = edn::u128_mul(sig1, sig2.lo);
                std::uint64_t const add = m.lo + m2.hi;
                if (add + 1 > 1) {
                    hi += add < m.lo || add < m2.hi ? 1 : 0;
                    exact = true;
                }
            }
            if (exact) {
                int const shift = hi < top_bit ? 1 : 0;
                hi <<= shift;
                exp2 += edn::f64_bits - shift;
                hi += (hi & round_bit) != 0 ? round_bit : 0;
                if (hi < round_bit) {
                    hi = top_bit;
                    exp2 += 1;
                }
                hi >>= edn::f64_bits - edn::f64_sig_full_bits;
                exp2 += edn::f64_bits - edn::f64_sig_full_bits + edn::f64_sig_bits + edn::f64_exp_bias;
                return return_f64_bin(static_cast<std::uint64_t>(exp2) << edn::f64_sig_bits |
                                      (hi & edn::f64_sig_mask));
            }
        }

        constexpr std::int32_t err_ulp_log = 3;
        constexpr std::int32_t err_ulp = 1 << err_ulp_log;
        constexpr std::int32_t err_cached_pow = err_ulp / 2;
        constexpr std::int32_t err_mul_fixed = err_ulp / 2;
        constexpr std::int32_t diy_sig_bits = std::numeric_limits<std::uint64_t>::digits;
        constexpr std::int32_t exp_bias = edn::f64_exp_bias + edn::f64_sig_bits;
        constexpr std::int32_t exp_subnormal = -exp_bias + 1;

        edn::diy_fp fp{sig, 0};
        std::uint64_t fp_err = sig_cut != std::string_view::npos ? std::uint64_t{err_ulp / 2} : 0;
        int bits = std::countl_zero(fp.sig);
        fp.sig <<= bits;
        fp.exp -= bits;
        fp_err <<= bits;
        fp = edn::diy_fp_mul(fp, edn::diy_fp_get_cached_pow10(exp));
        fp_err += std::uint64_t{err_cached_pow} + (fp_err != 0 ? 1 : 0) + std::uint64_t{err_mul_fixed};
        bits = std::countl_zero(fp.sig);
        fp.sig <<= bits;
        fp.exp -= bits;
        fp_err <<= bits;

        std::int32_t const order_of_magnitude = diy_sig_bits + fp.exp;
        std::int32_t effective_significand_size = order_of_magnitude - exp_subnormal;
        if (order_of_magnitude >= exp_subnormal + edn::f64_sig_full_bits) [[likely]]
            effective_significand_size = edn::f64_sig_full_bits;
        else if (order_of_magnitude <= exp_subnormal)
            effective_significand_size = 0;
        std::int32_t precision_digits_count = diy_sig_bits - effective_significand_size;
        if (precision_digits_count + err_ulp_log >= diy_sig_bits) [[unlikely]] {
            std::int32_t const shr = precision_digits_count + err_ulp_log - diy_sig_bits + 1;
            fp.sig >>= shr;
            fp.exp += shr;
            fp_err = (fp_err >> shr) + 1 + std::uint64_t{err_ulp};
            precision_digits_count -= shr;
        }
        std::uint64_t const precision_bits =
            (fp.sig & ((std::uint64_t{1} << precision_digits_count) - 1)) * std::uint64_t{err_ulp};
        std::uint64_t const half_way =
            (std::uint64_t{1} << (precision_digits_count - 1)) * std::uint64_t{err_ulp};
        fp.sig >>= precision_digits_count;
        fp.sig += precision_bits >= half_way + fp_err ? 1 : 0;
        fp.exp += precision_digits_count;

        std::uint64_t raw = edn::diy_fp_to_ieee_raw(fp);
        if (raw == edn::f64_bits_inf) [[unlikely]]
            return failed;
        if (precision_bits > half_way - fp_err && precision_bits < half_way + fp_err) [[unlikely]] {
            edn::diy_fp fp_upper{
                (raw & edn::f64_sig_mask) +
                    ((raw & edn::f64_exp_mask) != 0 ? std::uint64_t{1} << edn::f64_sig_bits : 0),
                (raw & edn::f64_exp_mask) != 0
                    ? static_cast<std::int32_t>((raw & edn::f64_exp_mask) >> edn::f64_sig_bits)
                    : 1};
            fp_upper.exp -= edn::f64_exp_bias + edn::f64_sig_bits;
            fp_upper.sig <<= 1;
            fp_upper.exp -= 1;
            fp_upper.sig += 1;
            std::strong_ordering const cmp =
                edn::bigcomp(text, sig, exp, sig_cut, sig_end, dot_pos, fp_upper);
            if (cmp != 0) [[likely]]
                raw += cmp > 0 ? std::uint64_t{1} : 0;
            else
                raw += raw & 1;
            if (raw == edn::f64_bits_inf) [[unlikely]]
                return failed;
        }
        return return_f64_bin(raw);
    }

    struct value_block {
        std::unique_ptr<item[]> values;
        std::span<item> items;
        std::size_t val_end;

        static value_block allocated(std::size_t const size)
        {
            auto values = std::make_unique_for_overwrite<item[]>(size);
            std::span<item> const items(values.get(), size);
            return {std::move(values), items, size - pair_values};
        }

        [[gnu::noinline]] void grow(std::size_t const used)
        {
            value_block grown = allocated(items.size() + items.size() / growth_divisor);
            std::ranges::copy(items.first(used), grown.items.begin());
            *this = std::move(grown);
        }
    };

    CBOR_ALWAYS_INLINE static std::size_t read_true(std::span<char const> const buf, std::size_t const cur,
                                                    item &x)
    {
        if (byte_match<literal_true.size()>(buf, cur, literal_true)) [[likely]] {
            x.tag = kind::word(0, std::to_underlying(simple_value::true_value),
                               kind::type_bool | kind::subtype_true);
            return cur + literal_true.size();
        }
        return failed;
    }

    CBOR_ALWAYS_INLINE static std::size_t read_false(std::span<char const> const buf, std::size_t const cur,
                                                     item &x)
    {
        if (byte_match<literal_false.size() - 1>(buf, cur + 1, literal_false.substr(1))) [[likely]] {
            x.tag = kind::word(0, std::to_underlying(simple_value::false_value),
                               kind::type_bool | kind::subtype_false);
            return cur + literal_false.size();
        }
        return failed;
    }

    CBOR_ALWAYS_INLINE static std::size_t read_null(std::span<char const> const buf, std::size_t const cur,
                                                    item &x)
    {
        if (byte_match<literal_null.size()>(buf, cur, literal_null)) [[likely]] {
            x.tag = kind::word(0, std::to_underlying(simple_value::null), kind::type_null);
            return cur + literal_null.size();
        }
        return failed;
    }

    static std::expected<void, error> check_text_size(std::size_t const size)
    {
        if (std::cmp_greater_equal(size, kind::high_bound)) [[unlikely]]
            return std::unexpected(error::value_too_large);
        return {};
    }

    static std::expected<void, error> check_string_length(item const &x)
    {
        return validity::check_argument<true>(major_type::text_string, kind::high_of(x.tag));
    }

    [[gnu::flatten]] static std::size_t space_end(padded_string const &text, std::size_t cur)
    {
        while (edn::char_is_space(text[cur]))
            ++cur;
        return cur;
    }

    template <bool Pretty>
    [[gnu::noinline]] static std::expected<tree, error> read_root(padded_string text, std::size_t cur)
    {
        std::span<char> const buf = text.padded();
        std::string_view const chars(text);
        std::size_t const len = text.size();
        value_block block = value_block::allocated(
            len / (Pretty ? estimated_pretty_ratio : estimated_minify_ratio) + estimated_extra);
        std::size_t val = 0;
        std::size_t ctn = 0;
        std::size_t ctn_len = 0;
        std::size_t ctn_parent = 0;
        std::size_t depth = 1;
        std::size_t r = failed;

        auto const val_incr = [&block, &val] {
            ++val;
            if (val >= block.val_end) [[unlikely]]
                block.grow(val);
        };
        auto const skip_indent = [&buf, &cur] {
            for (;;) {
#pragma GCC unroll indent_repeat
                for (std::size_t k = 0; k < indent_repeat; ++k) {
                    if (!byte_match<pretty_indent.size()>(buf, cur, pretty_indent))
                        return;
                    cur += pretty_indent.size();
                }
            }
        };
        auto const space = [&buf, &cur] {
            while (edn::char_is_space(byte(buf, ++cur)))
                ;
        };
        auto const newline = [&buf, &cur] {
            if constexpr (Pretty)
                cur += buf[cur] == '\n' ? std::size_t{1} : 0;
        };
        auto const container_begin = [&](std::uint8_t const type) -> std::expected<void, error> {
            if (auto const c = check_nesting_depth(depth); !c) [[unlikely]]
                return c;
            ++depth;
            block.items[ctn].tag =
                kind::word(ctn_len + 1, 0, static_cast<std::uint8_t>(block.items[ctn].tag & kind::type_mask));
            val_incr();
            block.items[val].tag = kind::word(0, 0, type);
            block.items[val].uni.ofs = val - ctn;
            ctn = val;
            ctn_len = 0;
            newline();
            return {};
        };
        auto const container_end = [&](major_type const major) -> std::expected<void, error> {
            item &c = block.items[ctn];
            std::size_t const count =
                major == major_type::map ? ctn_len / rfc8949::data_items_per_pair : ctn_len;
            if (auto const a = validity::check_argument<true>(major, count); !a) [[unlikely]]
                return a;
            ctn_parent = ctn - c.uni.ofs;
            c.tag = kind::word(count, heads::preferred_argument_info(count),
                               major == major_type::map ? kind::type_map : kind::type_array);
            c.uni.ofs = val - ctn + 1;
            return {};
        };

        block.items[ctn].uni.ofs = 0;
        if (buf[cur++] == '{') {
            block.items[ctn].tag = kind::word(0, 0, kind::type_map);
            newline();
            goto obj_key_begin;
        }
        block.items[ctn].tag = kind::word(0, 0, kind::type_array);
        newline();
        goto arr_val_begin;

    arr_begin:
        if (auto const o = container_begin(kind::type_array); !o) [[unlikely]]
            return std::unexpected(o.error());

    arr_val_begin:
        if constexpr (Pretty)
            skip_indent();
        if (buf[cur] == '{') {
            ++cur;
            goto obj_begin;
        }
        if (buf[cur] == '[') {
            ++cur;
            goto arr_begin;
        }
        if (edn::char_is_num(byte(buf, cur))) {
            val_incr();
            ++ctn_len;
            r = read_num(buf, chars, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == '"') {
            val_incr();
            ++ctn_len;
            r = read_str(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                if (auto const a = check_string_length(block.items[val]); !a) [[unlikely]]
                    return std::unexpected(a.error());
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 't') {
            val_incr();
            ++ctn_len;
            r = read_true(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 'f') {
            val_incr();
            ++ctn_len;
            r = read_false(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 'n') {
            val_incr();
            ++ctn_len;
            r = read_null(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == ']') {
            ++cur;
            if (ctn_len == 0) [[likely]]
                goto arr_end;
            goto fail;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto arr_val_begin;
        }
        goto fail;

    arr_val_end:
        if constexpr (Pretty) {
            if (byte_match<pretty_comma.size()>(buf, cur, pretty_comma)) {
                cur += pretty_comma.size();
                goto arr_val_begin;
            }
        }
        if (buf[cur] == ',') {
            ++cur;
            goto arr_val_begin;
        }
        if (buf[cur] == ']') {
            ++cur;
            goto arr_end;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto arr_val_end;
        }
        goto fail;

    arr_end:
        if (auto const e = container_end(major_type::array); !e) [[unlikely]]
            return std::unexpected(e.error());
        goto ctn_end;

    obj_begin:
        if (auto const o = container_begin(kind::type_map); !o) [[unlikely]]
            return std::unexpected(o.error());

    obj_key_begin:
        if constexpr (Pretty)
            skip_indent();
        if (buf[cur] == '"') [[likely]] {
            val_incr();
            ++ctn_len;
            r = read_str(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                if (auto const a = check_string_length(block.items[val]); !a) [[unlikely]]
                    return std::unexpected(a.error());
                cur = r;
                goto obj_key_end;
            }
            goto fail;
        }
        if (buf[cur] == '}') [[likely]] {
            ++cur;
            if (ctn_len == 0) [[likely]]
                goto obj_end;
            goto fail;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto obj_key_begin;
        }
        goto fail;

    obj_key_end:
        if constexpr (Pretty) {
            if (byte_match<pretty_colon.size()>(buf, cur, pretty_colon)) {
                cur += pretty_colon.size();
                goto obj_val_begin;
            }
        }
        if (buf[cur] == ':') {
            ++cur;
            goto obj_val_begin;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto obj_key_end;
        }
        goto fail;

    obj_val_begin:
        if (buf[cur] == '"') {
            ++val;
            ++ctn_len;
            r = read_str(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                if (auto const a = check_string_length(block.items[val]); !a) [[unlikely]]
                    return std::unexpected(a.error());
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (edn::char_is_num(byte(buf, cur))) {
            ++val;
            ++ctn_len;
            r = read_num(buf, chars, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (buf[cur] == '{') {
            ++cur;
            goto obj_begin;
        }
        if (buf[cur] == '[') {
            ++cur;
            goto arr_begin;
        }
        if (buf[cur] == 't') {
            ++val;
            ++ctn_len;
            r = read_true(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 'f') {
            ++val;
            ++ctn_len;
            r = read_false(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 'n') {
            ++val;
            ++ctn_len;
            r = read_null(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto obj_val_begin;
        }
        goto fail;

    obj_val_end:
        if constexpr (Pretty) {
            if (byte_match<pretty_comma.size()>(buf, cur, pretty_comma)) {
                cur += pretty_comma.size();
                goto obj_key_begin;
            }
        }
        if (buf[cur] == ',') [[likely]] {
            ++cur;
            goto obj_key_begin;
        }
        if (buf[cur] == '}') [[likely]] {
            ++cur;
            goto obj_end;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto obj_val_end;
        }
        goto fail;

    obj_end:
        if (auto const e = container_end(major_type::map); !e) [[unlikely]]
            return std::unexpected(e.error());

    ctn_end:
        if (ctn == ctn_parent) [[unlikely]]
            goto doc_end;
        --depth;
        ctn = ctn_parent;
        ctn_len = static_cast<std::size_t>(kind::high_of(block.items[ctn].tag));
        newline();
        if ((block.items[ctn].tag & kind::type_mask) == kind::type_map)
            goto obj_val_end;
        goto arr_val_end;

    doc_end:
        if (space_end(text, cur) != len) [[unlikely]]
            goto fail;
        return tree{std::move(block.values), val + 1, std::move(text), {}, false};

    fail:
        return std::unexpected(error::syntax_error);
    }

    static std::expected<tree, error> read_root_single(padded_string text, std::size_t const cur)
    {
        auto values = std::make_unique_for_overwrite<item[]>(1);
        std::span<char> const buf = text.padded();
        item &x = values[0];
        std::size_t r = failed;
        unsigned char const c = byte(buf, cur);
        if (edn::char_is_num(c))
            r = read_num(buf, std::string_view(text), cur, x);
        else if (c == '"')
            r = read_str(buf, cur, x);
        else if (c == 't')
            r = read_true(buf, cur, x);
        else if (c == 'f')
            r = read_false(buf, cur, x);
        else if (c == 'n')
            r = read_null(buf, cur, x);
        if (r == failed) [[unlikely]]
            return std::unexpected(error::syntax_error);
        if (c == '"')
            if (auto const a = check_string_length(x); !a) [[unlikely]]
                return std::unexpected(a.error());
        if (space_end(text, r) != text.size()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return tree{std::move(values), 1, std::move(text), {}, false};
    }

public:
    template <bool SharedRefs>
    static std::expected<tree, error> decode(std::string_view const bytes)
    {
        std::string_view const encoded = heads::self_described_cbor_content(bytes);
        std::size_t const size = encoded.size();
        if (size == 0) [[unlikely]]
            return std::unexpected(error::too_little_data);
        padded_string text(encoded);
        auto values = std::make_unique_for_overwrite<item[]>(size);
        items_decoder out{std::span<item>(values.get(), size), {}, false};
        auto const outer = out.decode_item<SharedRefs>(0, std::string_view(text), 0, false);
        if (out.embedded.empty() && outer) [[likely]]
            return tree{std::move(values), *outer, std::move(text), {}, out.has_shared_reference};
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
        return tree{std::move(values), written, std::move(text), std::move(links), out.has_shared_reference};
    }

    static std::expected<tree, error> read(std::string_view const json)
    {
        if (auto const r = check_text_size(json.size()); !r) [[unlikely]]
            return std::unexpected(r.error());
        padded_string text(json);
        std::size_t const cur = space_end(text, 0);
        if (cur >= text.size()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        if (text[cur] == '[' || text[cur] == '{') [[likely]] {
            if (std::ranges::all_of(
                    std::views::iota(std::size_t{1}, pretty_probe + 1),
                    [&text, cur](std::size_t const i) { return edn::char_is_space(text[cur + i]); }))
                return read_root<true>(std::move(text), cur);
            return read_root<false>(std::move(text), cur);
        }
        return read_root_single(std::move(text), cur);
    }
};

class writer
{
    using heads = detail::heads;
    using validity = detail::validity;

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
        output o = output_for(out, t.text.size());
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
        output o = output_for(out, t.text.size());
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

inline std::expected<tree, error> read(std::string_view const json)
{
    return reader::read(json);
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
