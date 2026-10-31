// Reads a CBOR Sequence that arrives in pieces. next() gives each
// complete data item as a cbor::lazy, or too_little_data while the
// item is not complete. Keeps a piece that it takes by move or in a
// std::shared_ptr without a copy, and copies an item that spans two
// pieces.
// https://www.rfc-editor.org/rfc/rfc8742#section-2
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "error.hpp"
#include "head.hpp"
#include "lazy.hpp"
#include "ref.hpp"
#include "validity.hpp"

namespace cbor
{

class sequence_reader
{
    enum class phase { head, argument, string_content, elements };

    struct held_piece {
        std::shared_ptr<void const> owner;
        std::string_view bytes;
    };

    std::shared_ptr<std::string> buffer = std::make_shared<std::string>();
    std::size_t item_begin = 0;
    std::vector<held_piece> pieces;
    std::size_t piece_first = 0;
    std::size_t piece_offset = 0;
    std::size_t scanned = 0;
    std::uint64_t phase_end = 0;
    phase at = phase::head;
    std::size_t level = 0;
    std::uint64_t pending = 1;
    std::array<std::uint64_t, limits::nesting_depth + 1> left{1};
    error failure{};

    static constexpr std::size_t growth_factor = 2;

    void throw_logic_error_if_moved_from() const
    {
        validity::throw_logic_error_if_null(buffer, "cbor::sequence_reader: the reader was moved from");
    }

    bool item_completed()
    {
        while (left[level] == 0) {
            if (level == 0)
                return true;
            --level;
        }
        return false;
    }

    std::expected<void, error> item_scan(std::string_view const item)
    {
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
            heads::decoder<false> d{std::string_view{std::span(item).subspan(scanned)}};
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
            --left[level];
            --pending;
            std::uint64_t children = 0;
            switch (h->major) {
            case major_type::byte_string:
            case major_type::text_string:
                phase_end = validity::checked_add(std::uint64_t{scanned}, h->argument)
                                .value_or(std::numeric_limits<std::uint64_t>::max());
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
                if (auto const r = validity::check_nesting_depth(level + 1, limits::nesting_depth); !r)
                    [[unlikely]]
                    return r;
                left[++level] = children;
                pending = validity::checked_add(pending, children)
                              .value_or(std::numeric_limits<std::uint64_t>::max());
                at = phase::elements;
                continue;
            }
            if (item_completed())
                return {};
            if (h->major == major_type::array || h->major == major_type::map)
                at = phase::elements;
        }
    }

    std::uint64_t bytes_missing(std::size_t const available) const
    {
        switch (at) {
        case phase::string_content:
        case phase::argument:
            return phase_end - available;
        case phase::elements:
            return pending - (available - scanned);
        default:
            return 1;
        }
    }

    void bytes_append(std::string_view const bytes)
    {
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

    void piece_push(held_piece p)
    {
        if (p.bytes.empty())
            return;
        pieces.erase(pieces.begin(), pieces.begin() + static_cast<std::ptrdiff_t>(piece_first));
        piece_first = 0;
        pieces.push_back(std::move(p));
    }

    void piece_drop()
    {
        pieces[piece_first] = held_piece{};
        ++piece_first;
        piece_offset = 0;
    }

    void bytes_feed(std::string_view const bytes)
    {
        throw_logic_error_if_moved_from();
        if (failure != error{}) [[unlikely]]
            return;
        if (piece_first == pieces.size()) {
            bytes_append(bytes);
            return;
        }
        auto copy = std::make_shared<std::string const>(bytes);
        std::string_view const held(*copy);
        piece_push(held_piece{std::move(copy), held});
    }

    void failure_keep(error const e)
    {
        failure = e;
        pieces.clear();
        piece_first = 0;
        piece_offset = 0;
        buffer->resize(item_begin);
    }

    void item_state_reset()
    {
        scanned = 0;
        level = 0;
        left[0] = 1;
        pending = 1;
        at = phase::head;
    }

public:
    sequence_reader() = default;

    sequence_reader(sequence_reader const &) = delete;

    sequence_reader &operator=(sequence_reader const &) = delete;

    sequence_reader(sequence_reader &&) = default;

    sequence_reader &operator=(sequence_reader &&) = default;

    template <borrowed_range_of_chars Piece>
        requires(!std::is_array_v<Piece>)
    void feed(Piece const piece)
    {
        bytes_feed(std::string_view(std::ranges::data(piece), std::ranges::size(piece)));
    }

    template <owning_range_of_chars Piece>
        requires(!std::is_array_v<Piece>)
    void feed(Piece const &piece)
    {
        bytes_feed(std::string_view(std::ranges::data(piece), std::ranges::size(piece)));
    }

    template <owning_range_of_chars Piece>
    void feed(Piece &&piece)
    {
        feed(std::make_shared<Piece const>(std::move(piece)));
    }

    template <owning_range_of_chars Piece>
    void feed(std::shared_ptr<Piece const> piece)
    {
        throw_logic_error_if_moved_from();
        validity::throw_logic_error_if_null(piece, "cbor::sequence_reader::feed: the piece is null");
        if (failure != error{}) [[unlikely]]
            return;
        std::string_view const bytes(std::ranges::data(*piece), std::ranges::size(*piece));
        piece_push(held_piece{std::move(piece), bytes});
    }

    bool empty() const
    {
        throw_logic_error_if_moved_from();
        return buffer->size() == item_begin && piece_first == pieces.size();
    }

    std::expected<lazy, error> next()
    {
        throw_logic_error_if_moved_from();
        if (failure != error{}) [[unlikely]]
            return std::unexpected(failure);
        for (;;) {
            bool const copied = buffer->size() != item_begin;
            std::string_view window;
            if (copied)
                window = std::string_view{std::span(std::as_const(*buffer)).subspan(item_begin)};
            else if (piece_first != pieces.size())
                window = std::string_view{std::span(pieces[piece_first].bytes).subspan(piece_offset)};
            auto const r = item_scan(window);
            if (r) {
                std::string_view const item{std::span(window).first(scanned)};
                std::shared_ptr<void const> owner = copied ? buffer : pieces[piece_first].owner;
                if (copied) {
                    item_begin += scanned;
                } else {
                    piece_offset += scanned;
                    if (piece_offset == pieces[piece_first].bytes.size())
                        piece_drop();
                }
                item_state_reset();
                return lazy::view(std::move(owner), item);
            }
            if (r.error() != error::too_little_data) [[unlikely]] {
                failure_keep(r.error());
                return std::unexpected(r.error());
            }
            if (!copied) {
                if (pieces.size() - piece_first < 2)
                    return std::unexpected(error::too_little_data);
                bytes_append(window);
                piece_drop();
                continue;
            }
            if (piece_first == pieces.size())
                return std::unexpected(error::too_little_data);
            std::string_view const source{std::span(pieces[piece_first].bytes).subspan(piece_offset)};
            std::size_t const n = static_cast<std::size_t>(
                std::min<std::uint64_t>(bytes_missing(window.size()), source.size()));
            bytes_append(std::string_view{std::span(source).first(n)});
            piece_offset += n;
            if (piece_offset == pieces[piece_first].bytes.size())
                piece_drop();
        }
    }
};

} // namespace cbor
