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

struct lazy;

struct lazy_elements;

struct lazy_entries;

class well_formedness
{
    struct no_marks {
        void mark(heads::decoder const &)
        {
        }
    };

    template <class Marks>
    static std::expected<void, error> item_skip(heads::decoder &d, Marks &marks)
    {
        std::uint64_t pending = 1;
        while (pending != 0) {
            --pending;
            std::uint64_t added = 0;
            std::uint8_t const initial = d.encoded.empty() ? 0xff : static_cast<std::uint8_t>(d.encoded.front());
            auto const major = static_cast<major_type>(initial >> 5);
            std::uint8_t const info = initial & 0x1f;
            if (d.encoded.size() >= heads::initial_byte_size + sizeof(std::uint64_t) &&
                info <= std::to_underlying(rfc8949::additional_information::eight_byte_argument) &&
                major != major_type::tag &&
                (major != major_type::simple_float ||
                 info != std::to_underlying(rfc8949::additional_information::one_byte_argument))) [[likely]] {
                bool const immediate = info < std::to_underlying(rfc8949::additional_information::one_byte_argument);
                std::size_t const size = heads::argument_size(info);
                std::uint64_t argument = info;
                if (info == std::to_underlying(rfc8949::additional_information::one_byte_argument)) {
                    argument = static_cast<std::uint8_t>(d.encoded[1]);
                } else if (!immediate) {
                    argument = heads::unsigned_read<std::uint64_t>(std::span<char const>(d.encoded).subspan<1, 8>()) >>
                               ((64 - 8 * size) & 63);
                }
                d.encoded.remove_prefix(1 + size);
                if (major == major_type::byte_string || major == major_type::text_string) {
                    if (auto const s = d.byte_string_decode(argument); !s) [[unlikely]]
                        return std::unexpected(s.error());
                    continue;
                }
                if (major == major_type::array)
                    added = argument;
                else if (major == major_type::map)
                    added = validity::checked_mul(argument, 2).value_or(std::numeric_limits<std::uint64_t>::max());
                else
                    continue;
            } else {
                auto const h = d.head_decode();
                if (!h) [[unlikely]]
                    return std::unexpected(h.error());
                switch (h->major) {
                case major_type::byte_string:
                case major_type::text_string:
                    if (auto const s = d.byte_string_decode(h->argument); !s) [[unlikely]]
                        return std::unexpected(s.error());
                    continue;
                case major_type::array:
                    added = h->argument;
                    break;
                case major_type::map:
                    added = validity::checked_mul(h->argument, 2).value_or(std::numeric_limits<std::uint64_t>::max());
                    break;
                case major_type::tag:
                    if (h->argument == std::to_underlying(rfc8949::tag_number::shareable))
                        marks.mark(d);
                    added = 1;
                    break;
                case major_type::simple_float:
                    if (!validity::check_simple_value(h->info, h->argument).has_value()) [[unlikely]]
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

    template <class Marks>
        requires requires(Marks &m, heads::decoder const &at, std::size_t depth) { m.mark(at, depth); }
    static std::expected<void, error> item_skip(heads::decoder &d, Marks &marks, std::size_t const depth,
                                                std::size_t const depth_max)
    {
        std::array<std::uint64_t, validity::nesting_depth_limit + 2> left;
        std::size_t level = 0;
        left[0] = 1;
        for (;;) {
            while (left[level] == 0) {
                if (level == 0)
                    return {};
                --level;
            }
            --left[level];
            if (!validity::check_nesting_depth(depth + level, depth_max).has_value()) [[unlikely]]
                return std::unexpected(error::nesting_depth_exceeded);
            if (d.encoded.size() >= heads::initial_byte_size + sizeof(std::uint64_t)) [[likely]] {
                auto const initial = static_cast<std::uint8_t>(d.encoded.front());
                auto const major = static_cast<major_type>(initial >> 5);
                std::uint8_t const info = initial & 0x1f;
                if (info <= std::to_underlying(rfc8949::additional_information::eight_byte_argument) &&
                    major != major_type::tag &&
                    (major != major_type::simple_float ||
                     info != std::to_underlying(rfc8949::additional_information::one_byte_argument)))
                    [[likely]] {
                    bool const immediate =
                        info < std::to_underlying(rfc8949::additional_information::one_byte_argument);
                    std::size_t const size = heads::argument_size(info);
                    std::uint64_t argument = info;
                    if (info == std::to_underlying(rfc8949::additional_information::one_byte_argument)) {
                        argument = static_cast<std::uint8_t>(d.encoded[1]);
                    } else if (!immediate) {
                        argument = heads::unsigned_read<std::uint64_t>(std::span<char const>(d.encoded).subspan<1, 8>()) >>
                                   ((64 - 8 * size) & 63);
                    }
                    d.encoded.remove_prefix(1 + size);
                    if (major == major_type::byte_string || major == major_type::text_string) {
                        if (auto const s = d.byte_string_decode(argument); !s) [[unlikely]]
                            return std::unexpected(s.error());
                    } else if (major == major_type::array) {
                        left[++level] = argument;
                    } else if (major == major_type::map) {
                        left[++level] = validity::checked_mul(argument, 2)
                                            .value_or(std::numeric_limits<std::uint64_t>::max());
                    }
                    continue;
                }
            }
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            switch (h->major) {
            case major_type::byte_string:
            case major_type::text_string:
                if (auto const s = d.byte_string_decode(h->argument); !s) [[unlikely]]
                    return std::unexpected(s.error());
                break;
            case major_type::array:
                left[++level] = h->argument;
                break;
            case major_type::map:
                left[++level] =
                    validity::checked_mul(h->argument, 2).value_or(std::numeric_limits<std::uint64_t>::max());
                break;
            case major_type::tag:
                if (h->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
                    marks.mark(d, depth + level);
                }
                left[++level] = 1;
                break;
            case major_type::simple_float:
                if (!validity::check_simple_value(h->info, h->argument).has_value()) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                break;
            default:
                break;
            }
        }
    }

    friend std::expected<std::size_t, error> item_end(std::string_view encoded);

    friend class validity;

    friend class decoding;

    friend struct lazy;

    friend struct lazy_elements;

    friend struct lazy_entries;

    friend class jsonpath;

    friend class value_sharing;

#ifdef __cpp_impl_reflection
    friend class generic;
#endif
};

inline std::expected<std::size_t, error> item_end(std::string_view const encoded)
{
    heads::decoder d{encoded};
    well_formedness::no_marks none;
    if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
        return std::unexpected(r.error());
    return encoded.size() - d.encoded.size();
}

}
