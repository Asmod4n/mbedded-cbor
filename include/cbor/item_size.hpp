// Gives the length in bytes of the first CBOR data item in a buffer.
// Goes from head to head and copies nothing. Gives an error for an
// item of indefinite length.
// https://www.rfc-editor.org/rfc/rfc8949#section-3
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"

namespace cbor
{

class reflection;

struct lazy;

struct lazy_elements;

struct lazy_entries;

class well_formedness
{
    struct no_marks {
        template <bool Checked>
        void mark(heads::decoder<Checked> const &)
        {
        }
    };

    template <bool Checked, class Marks>
    static std::expected<void, error> item_skip(heads::decoder<Checked> &d, Marks &marks)
    {
        std::uint64_t pending = 1;
        while (pending != 0) {
            --pending;
            std::uint64_t added = 0;
            if (auto const h = d.head_decode_where_no_check_is_needed()) [[likely]] {
                if (h->major == major_type::byte_string || h->major == major_type::text_string) [[likely]] {
                    if (auto const s = d.byte_string_decode(h->argument); !s) [[unlikely]]
                        return std::unexpected(s.error());
                    continue;
                }
                if (h->major == major_type::array)
                    added = h->argument;
                else if (h->major == major_type::map)
                    added = validity::checked_mul(h->argument, rfc8949::data_items_per_pair)
                                .value_or(std::numeric_limits<std::uint64_t>::max());
                else
                    continue;
            } else {
                auto const slow = d.head_decode();
                if (!slow) [[unlikely]]
                    return std::unexpected(slow.error());
                switch (slow->major) {
                case major_type::byte_string:
                case major_type::text_string:
                    if (auto const s = d.byte_string_decode(slow->argument); !s) [[unlikely]]
                        return std::unexpected(s.error());
                    continue;
                case major_type::array:
                    added = slow->argument;
                    break;
                case major_type::map:
                    added = validity::checked_mul(slow->argument, rfc8949::data_items_per_pair)
                                .value_or(std::numeric_limits<std::uint64_t>::max());
                    break;
                case major_type::tag:
                    if (slow->argument == std::to_underlying(rfc8949::tag_number::shareable))
                        marks.mark(d);
                    added = 1;
                    break;
                case major_type::simple_float:
                    if (!validity::check_simple_value(slow->info, slow->argument).has_value()) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    continue;
                default:
                    continue;
                }
            }
            auto const sum = validity::checked_add(pending, added);
            if (!sum) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (auto const r = validity::check_pending_items(*sum, d.encoded.size()); !r) [[unlikely]]
                return std::unexpected(r.error());
            pending = *sum;
        }
        return {};
    }

    template <bool Checked>
    CBOR_ALWAYS_INLINE static std::expected<void, error> items_skip(heads::decoder<Checked> &d,
                                                                    std::uint64_t const count)
    {
        no_marks none;
        if constexpr (!Checked) {
            for (std::uint64_t i = 0; i < count; ++i)
                if (auto const r = item_skip(d, none); !r) [[unlikely]]
                    return r;
            return {};
        }
        heads::decoder<false> unlimited{d.encoded};
        for (std::uint64_t i = 0; i < count; ++i)
            if (auto const r = item_skip(unlimited, none); !r) [[unlikely]]
                return r;
        d.encoded = unlimited.encoded;
        return {};
    }

    template <bool Checked, class Marks>
        requires requires(Marks &m, heads::decoder<Checked> const &at, std::size_t depth) {
            m.mark(at, depth);
        }
    static std::expected<void, error> item_skip(heads::decoder<Checked> &d, Marks &marks,
                                                std::size_t const depth, std::size_t const depth_max)
    {
        std::array<std::uint64_t, validity::nesting_depth_limit + 1> left;
        if (auto const r = validity::check_nesting_depth(depth_max, validity::nesting_depth_limit); !r)
            [[unlikely]]
            return r;
        if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
            return r;
        std::size_t level = 0;
        left[0] = 1;
        for (;;) {
            while (left[level] == 0) {
                if (level == 0)
                    return {};
                --level;
            }
            --left[level];
            std::uint64_t children = 0;
            if (auto const h = d.head_decode_where_no_check_is_needed()) [[likely]] {
                if (h->major == major_type::byte_string || h->major == major_type::text_string) {
                    if (auto const s = d.byte_string_decode(h->argument); !s) [[unlikely]]
                        return std::unexpected(s.error());
                } else if (h->major == major_type::array) {
                    children = h->argument;
                } else if (h->major == major_type::map) {
                    children = validity::checked_mul(h->argument, rfc8949::data_items_per_pair)
                                   .value_or(std::numeric_limits<std::uint64_t>::max());
                }
            } else {
                auto const slow = d.head_decode();
                if (!slow) [[unlikely]]
                    return std::unexpected(slow.error());
                switch (slow->major) {
                case major_type::byte_string:
                case major_type::text_string:
                    if (auto const s = d.byte_string_decode(slow->argument); !s) [[unlikely]]
                        return std::unexpected(s.error());
                    break;
                case major_type::array:
                    children = slow->argument;
                    break;
                case major_type::map:
                    children = validity::checked_mul(slow->argument, rfc8949::data_items_per_pair)
                                   .value_or(std::numeric_limits<std::uint64_t>::max());
                    break;
                case major_type::tag:
                    if (slow->argument == std::to_underlying(rfc8949::tag_number::shareable))
                        marks.mark(d, depth + level);
                    children = 1;
                    break;
                case major_type::simple_float:
                    if (!validity::check_simple_value(slow->info, slow->argument).has_value()) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    break;
                default:
                    break;
                }
            }
            if (children == 0)
                continue;
            if (auto const r = validity::check_pending_items(children, d.encoded.size()); !r) [[unlikely]]
                return r;
            if (auto const r = validity::check_nesting_depth(depth + level + 1, depth_max); !r) [[unlikely]]
                return r;
            left[++level] = children;
        }
    }

    friend std::expected<std::size_t, error> item_size(std::string_view encoded);

    friend class validity;


    friend struct lazy;

    friend struct lazy_elements;

    friend struct lazy_entries;

    friend class jsonpath;

    friend class generic;

    friend class reflection;
};

inline std::expected<std::size_t, error> item_size(std::string_view const encoded)
{
    heads::decoder<true> d{encoded};
    well_formedness::no_marks none;
    if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
        return std::unexpected(r.error());
    return encoded.size() - d.encoded.size();
}

} // namespace cbor
