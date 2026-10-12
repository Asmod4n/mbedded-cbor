#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "error.hpp"
#include "head.hpp"
#include "lazy.hpp"
#include "shared.hpp"
#include "validity.hpp"

namespace cbor
{

class sequence_reader
{
    enum class phase { head, argument, string_content, elements };

    std::shared_ptr<std::string> buffer = std::make_shared<std::string>();
    std::size_t item_begin = 0;
    std::size_t scanned = 0;
    std::uint64_t phase_end = 0;
    phase at = phase::head;
    std::size_t level = 0;
    std::uint64_t pending = 1;
    std::array<std::uint64_t, validity::nesting_depth_limit + 1> left{1};
    error failure{};
    limit_values item_limits{};

    static constexpr std::size_t growth_factor = 2;

    bool item_completed()
    {
        while (left[level] == 0) {
            if (level == 0)
                return true;
            --level;
        }
        return false;
    }

    template <bool Checked>
    std::expected<void, error> item_scan(std::size_t const depth_max,
                                         validity::limit_checks<Checked> const checks)
    {
        if (auto const r = validity::check_nesting_depth(depth_max, validity::nesting_depth_limit); !r)
            [[unlikely]]
            return r;
        std::string_view const item{std::span(std::as_const(*buffer)).subspan(item_begin)};
        for (;;) {
            if (at == phase::string_content) {
                if (item.size() < phase_end) [[unlikely]]
                    return std::unexpected(error::too_little_data);
                scanned = static_cast<std::size_t>(phase_end);
                at = phase::head;
                if (item_completed())
                    return {};
            }
            if (at == phase::elements) {
                if (auto const r = validity::check_pending_items(pending, item.size() - scanned); !r)
                    [[unlikely]]
                    return r;
                at = phase::head;
            }
            if (at == phase::argument && item.size() < phase_end) [[unlikely]]
                return std::unexpected(error::too_little_data);
            heads::decoder<Checked> d{std::string_view{std::span(item).subspan(scanned)}, checks};
            auto const h = d.head_decode();
            if (!h) [[unlikely]] {
                if (h.error() == error::too_little_data && !d.encoded.empty()) {
                    auto const initial = static_cast<std::uint8_t>(d.encoded.front());
                    std::uint8_t const info = initial & ((1 << rfc8949::additional_information_bits) - 1);
                    at = phase::argument;
                    phase_end = scanned + heads::initial_byte_size + heads::argument_size(info);
                }
                return std::unexpected(h.error());
            }
            at = phase::head;
            scanned = item.size() - d.encoded.size();
            if (auto const r = validity::check_input_bytes(scanned, checks); !r) [[unlikely]]
                return r;
            --left[level];
            --pending;
            std::uint64_t children = 0;
            switch (h->major) {
            case major_type::byte_string:
            case major_type::text_string:
                phase_end = validity::checked_add(std::uint64_t{scanned}, h->argument)
                                .value_or(std::numeric_limits<std::uint64_t>::max());
                if (auto const r = validity::check_input_bytes(phase_end, checks); !r) [[unlikely]]
                    return r;
                at = phase::string_content;
                continue;
            case major_type::array:
                children = h->argument;
                break;
            case major_type::map:
                children = validity::checked_mul(h->argument, rfc8949::data_items_per_pair)
                               .value_or(std::numeric_limits<std::uint64_t>::max());
                break;
            case major_type::tag:
                children = 1;
                break;
            case major_type::simple_float:
                if (!validity::check_simple_value(h->info, h->argument)) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                break;
            default:
                break;
            }
            if (children != 0) {
                if (auto const r = validity::check_nesting_depth(level + 1, depth_max); !r) [[unlikely]]
                    return r;
                left[++level] = children;
                pending = validity::checked_add(pending, children)
                              .value_or(std::numeric_limits<std::uint64_t>::max());
                at = phase::elements;
                continue;
            }
            if (item_completed())
                return {};
        }
    }

public:
    sequence_reader() = default;

    sequence_reader(sequence_reader const &) = delete;

    sequence_reader &operator=(sequence_reader const &) = delete;

    sequence_reader(sequence_reader &&) = default;

    sequence_reader &operator=(sequence_reader &&) = default;

    void feed(std::string_view const bytes)
    {
        validity::throw_logic_error_if_null(buffer, "cbor::sequence_reader: the reader was moved from");
        if (buffer.use_count() == 1) {
            if (item_begin >= buffer->size() - item_begin) {
                buffer->erase(0, item_begin);
                item_begin = 0;
            }
        } else if (buffer->capacity() - buffer->size() < bytes.size()) {
            std::string_view const rest{std::span(std::as_const(*buffer)).subspan(item_begin)};
            auto block = std::make_shared<std::string>();
            block->reserve(
                std::max(buffer->capacity(), validity::checked_mul(rest.size() + bytes.size(), growth_factor)
                                                 .value_or(std::numeric_limits<std::size_t>::max())));
            block->append(rest);
            buffer = std::move(block);
            item_begin = 0;
        }
        buffer->append(bytes);
    }

    bool empty() const
    {
        validity::throw_logic_error_if_null(buffer, "cbor::sequence_reader: the reader was moved from");
        return buffer->size() == item_begin;
    }

    std::expected<lazy, error> next()
    {
        validity::throw_logic_error_if_null(buffer, "cbor::sequence_reader: the reader was moved from");
        if (failure != error{}) [[unlikely]]
            return std::unexpected(failure);
        if (scanned == 0 && level == 0 && at == phase::head)
            item_limits = limits.load();
        auto const r = validity::limits_apply(
            item_limits,
            [this]<bool Checked>(std::size_t const depth_max, validity::limit_checks<Checked> const checks) {
                return item_scan(depth_max, checks);
            });
        if (!r) [[unlikely]] {
            if (r.error() != error::too_little_data)
                failure = r.error();
            return std::unexpected(r.error());
        }
        std::string_view const item{std::span(std::as_const(*buffer)).subspan(item_begin, scanned)};
        item_begin += scanned;
        scanned = 0;
        level = 0;
        left[0] = 1;
        pending = 1;
        return lazy::from(buffer, item, item_limits);
    }
};

} // namespace cbor
