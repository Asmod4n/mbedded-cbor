/**
 * @file
 * Gives the length in bytes of the first CBOR data item in a buffer.
 * Goes from head to head and copies nothing. Gives an error for an
 * item of indefinite length.
 * https://www.rfc-editor.org/rfc/rfc8949#section-3
 */
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
    template <bool Checked>
    static std::expected<void, error> skip_item(heads::decoder<Checked> &d)
    {
        std::uint64_t pending = 1;
        while (pending != 0) {
            --pending;
            std::uint64_t added = 0;
            if (auto const h = d.try_decode_head()) [[likely]] {
                if (h->major == major_type::byte_string || h->major == major_type::text_string) [[likely]] {
                    if (auto const s = d.decode_byte_string(h->argument); !s) [[unlikely]]
                        return std::unexpected(s.error());
                    continue;
                }
                if (h->major == major_type::array)
                    added = h->argument;
                else if (h->major == major_type::map)
                    added = detail::validity::checked_mul(h->argument, rfc8949::data_items_per_pair)
                                .value_or(std::numeric_limits<std::uint64_t>::max());
                else
                    continue;
            } else {
                auto const slow = d.decode_head();
                if (!slow) [[unlikely]]
                    return std::unexpected(slow.error());
                switch (slow->major) {
                case major_type::byte_string:
                case major_type::text_string:
                    if (auto const s = d.decode_byte_string(slow->argument); !s) [[unlikely]]
                        return std::unexpected(s.error());
                    continue;
                case major_type::array:
                    added = slow->argument;
                    break;
                case major_type::map:
                    added = detail::validity::checked_mul(slow->argument, rfc8949::data_items_per_pair)
                                .value_or(std::numeric_limits<std::uint64_t>::max());
                    break;
                case major_type::tag:
                    added = 1;
                    break;
                case major_type::simple_float:
                    if (!detail::validity::check_simple_value(slow->info, slow->argument).has_value())
                        [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    continue;
                default:
                    continue;
                }
            }
            auto const sum = detail::validity::checked_add(pending, added);
            if (!sum) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (auto const r = detail::validity::check_pending_items(*sum, d.encoded.size()); !r) [[unlikely]]
                return std::unexpected(r.error());
            pending = *sum;
        }
        return {};
    }

    template <bool Checked>
    CBOR_ALWAYS_INLINE static std::expected<void, error> skip_items(heads::decoder<Checked> &d,
                                                                    std::uint64_t const count)
    {
        if constexpr (!Checked) {
            for (std::uint64_t i = 0; i < count; ++i)
                if (auto const r = skip_item(d); !r) [[unlikely]]
                    return r;
            return {};
        }
        heads::decoder<false> unlimited{d.encoded};
        for (std::uint64_t i = 0; i < count; ++i)
            if (auto const r = skip_item(unlimited); !r) [[unlikely]]
                return r;
        d.encoded = unlimited.encoded;
        return {};
    }

    friend std::expected<std::size_t, error> item_size(std::string_view encoded);

    friend class detail::validity;

    friend struct lazy;

    friend struct lazy_elements;

    friend struct lazy_entries;

    friend class jsonpath;

    friend class generic;

    friend class reflection;
};

/**
 * @brief Gives the size in bytes of the first item in @p encoded.
 *
 * Checks that the item is well-formed. Bytes after the item do not change the result.
 * The call does not recurse: an item of any depth gives its size.
 *
 * @code{.callseq}
 * cbor::item_size(bytes) → std::expected<std::size_t, error>
 * @endcode
 * @code{.example}
 * bytes: 82 01 02 07
 * call: cbor::item_size(bytes)
 * result: 3
 * @endcode
 *
 * @retval too_little_data the bytes end before the item ends
 * @retval syntax_error a head is not well-formed
 * @retval indefinite_length a head uses indefinite length
 * @retval string_length_exceeded a string is longer than `limits::string_length`
 * @retval container_elements_exceeded the count of an array or a map is greater than `limits::container_elements`
 * @complexity O(n) time, n the bytes of the item. O(1) memory. No allocation.
 * [The proof](../../guides/complexity.md#item_size).
 * @snippet doc_reference_tools.cpp item_size
 * @see cbor::sequence
 *
 * @ingroup tools
 */
inline std::expected<std::size_t, error> item_size(std::string_view const encoded)
{
    heads::decoder<true> d{encoded};
    if (auto const r = well_formedness::skip_item(d); !r) [[unlikely]]
        return std::unexpected(r.error());
    return std::to_underlying(detail::validity::distance(encoded, d.encoded));
}

} // namespace cbor
