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

template <std::size_t DepthMax>
struct lazy_elements;

template <std::size_t DepthMax>
struct lazy_entries;

class well_formedness
{
    struct no_marks {
        void mark(heads::decoder const &)
        {
        }
    };

    template <std::size_t DepthMax, class Marks>
    static std::expected<void, error> item_skip(heads::decoder &d, Marks &marks, std::size_t const depth)
    {
        std::array<std::uint64_t, DepthMax + 2> left;
        std::size_t level = 0;
        left.at(0) = 1;
        for (;;) {
            while (left.at(level) == 0) {
                if (level == 0)
                    return {};
                --level;
            }
            --left.at(level);
            if (auto const r = validity::check_nesting_depth(depth + level, DepthMax); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (d.encoded.size() >= 9) {
                auto const initial = static_cast<std::uint8_t>(d.encoded.front());
                auto const major = static_cast<major_type>(initial >> 5);
                std::uint8_t const info = initial & 0x1f;
                if (info <= std::to_underlying(heads::additional_information::eight_byte_argument) &&
                    major != major_type::tag &&
                    (major != major_type::simple_float || info != std::to_underlying(heads::additional_information::one_byte_argument))) {
                    bool const immediate = info < std::to_underlying(heads::additional_information::one_byte_argument);
                    std::size_t const size = heads::argument_size(info);
                    std::uint64_t argument = info;
                    if (info == std::to_underlying(heads::additional_information::one_byte_argument)) {
                        argument = static_cast<std::uint8_t>(d.encoded.at(1));
                    } else if (!immediate) {
                        argument = heads::unsigned_read<std::uint64_t>(std::span<char const>(d.encoded.substr(1, 8)).first<8>()) >>
                                   ((64 - 8 * size) & 63);
                    }
                    d.encoded.remove_prefix(1 + size);
                    if (major == major_type::byte_string || major == major_type::text_string) {
                        if (auto const s = d.byte_string_decode(argument); !s) [[unlikely]]
                            return std::unexpected(s.error());
                    } else if (major == major_type::array) {
                        left.at(++level) = argument;
                    } else if (major == major_type::map) {
                        left.at(++level) = argument > std::numeric_limits<std::uint64_t>::max() / 2
                                               ? std::numeric_limits<std::uint64_t>::max()
                                               : argument * 2;
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
                left.at(++level) = h->argument;
                break;
            case major_type::map:
                left.at(++level) = h->argument > std::numeric_limits<std::uint64_t>::max() / 2
                                       ? std::numeric_limits<std::uint64_t>::max()
                                       : h->argument * 2;
                break;
            case major_type::tag:
                if (h->argument == std::to_underlying(heads::tag_number::shareable))
                    marks.mark(d);
                left.at(++level) = 1;
                break;
            case major_type::simple_float:
                if (auto const r = validity::check_simple_value(h->info, h->argument); !r) [[unlikely]]
                    return std::unexpected(r.error());
                break;
            default:
                break;
            }
        }
    }

    template <std::size_t DepthMax>
    friend std::expected<std::size_t, error> doc_end(std::string_view encoded);

    friend class decoding;

    friend struct lazy;

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

    friend class jsonpath;

    friend class value_sharing;

#ifdef __cpp_impl_reflection
    friend class generic;
#endif
};

template <std::size_t DepthMax>
std::expected<std::size_t, error> doc_end(std::string_view const encoded)
{
    heads::decoder d{encoded};
    well_formedness::no_marks none;
    if (auto const r = well_formedness::item_skip<DepthMax>(d, none, 0); !r) [[unlikely]]
        return std::unexpected(r.error());
    return encoded.size() - d.encoded.size();
}

}
