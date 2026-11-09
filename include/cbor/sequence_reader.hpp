/**
 * @file
 * Reads a CBOR Sequence that arrives in pieces. next() gives each
 * complete data item as a cbor::lazy, or too_little_data while the
 * item is not complete. Keeps a piece that it takes by move or in a
 * std::shared_ptr without a copy. Copies an item that spans two pieces.
 * https://www.rfc-editor.org/rfc/rfc8742#section-2
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "error.hpp"
#include "head.hpp"
#include "lazy.hpp"
#include "ref.hpp"
#include "validity.hpp"

namespace cbor
{

/**
 * @brief Reads a CBOR sequence (RFC 8742) whose bytes arrive in pieces, for example from a socket.
 *
 * `feed` gives the reader the next piece.
 * `next` gives the next item as a cbor::lazy when all of its bytes are there, and `too_little_data` while it waits.
 * `empty` is true when the reader holds no byte that no item took.
 *
 * `feed(std::move(s))` takes a `std::string` by move.
 * `feed(shared_ptr)` holds the piece without a copy.
 * `feed(view)` and `feed(s)` copy the bytes.
 * An item that is whole in a piece that the reader holds is a view into that piece.
 * The reader copies an item that the end of a piece cuts, and only the bytes of that item.
 * Each item keeps its bytes alive after later feeds and after the reader is gone.
 *
 * A char array, such as a literal, does not compile, because it holds a NUL after its last byte.
 * A `std::shared_ptr` to bytes that are not const does not compile.
 * The reader cannot be copied.
 *
 * @code{.callseq}
 * cbor::sequence_reader r  → sequence_reader
 * r.feed(std::move(piece)) → void
 * r.next()                 → std::expected<lazy, error>
 * r.empty()                → bool
 * @endcode
 * @code{.example}
 * bytes: 82 01 02 03
 * call: cbor::sequence_reader r
 * result: sequence_reader that holds no unread byte
 * call: auto first = bytes.substr(0, 2)
 * result: 2 bytes: 82 01
 * call: r.feed(std::move(first))
 * result: void
 * call: r.empty()
 * result: false
 * call: auto second = bytes.substr(2)
 * result: 2 bytes: 02 03
 * call: r.feed(std::move(second))
 * result: void
 * call: r.next()->size()
 * result: 2
 * call: r.next()->get<std::int64_t>()
 * result: 3
 * call: r.empty()
 * result: true
 * @endcode
 *
 * @retval too_little_data the item is not complete yet; feed more bytes
 * @retval syntax_error the bytes are not a well-formed item
 * @retval indefinite_length a head uses indefinite length
 * @throws std::logic_error a null `std::shared_ptr`, or a call on a reader that was moved from
 *
 * After an error other than `too_little_data`, every call of `next` gives that error, and `feed` keeps no byte.
 *
 * @snippet doc_reference_tools.cpp sequence_reader
 * @see cbor::sequence
 * @see cbor::lazy
 *
 * @ingroup tools
 */
class sequence_reader
{
    enum class phase { head, argument, string_content, elements };

    struct held_piece {
        std::shared_ptr<void const> owner;
        std::string_view bytes;
    };

    std::shared_ptr<std::string> buffer = std::make_shared<std::string>();
    std::size_t item_begin = 0;
    std::deque<held_piece> pieces;
    std::size_t piece_offset = 0;
    std::size_t scanned = 0;
    std::uint64_t phase_end = 0;
    phase at = phase::head;
    std::uint64_t pending = 1;
    error failure{};

    static constexpr std::size_t growth_factor = 2;

    void throw_logic_error_if_moved_from() const
    {
        detail::validity::throw_logic_error_if_null(buffer,
                                                    "cbor::sequence_reader: the reader was moved from");
    }

    std::expected<void, error> scan_item(std::string_view const item)
    {
        for (;;) {
            if (at == phase::string_content) {
                if (auto const fits = detail::validity::check_bytes_left(phase_end, item.size()); !fits)
                    [[unlikely]]
                    return std::unexpected(fits.error());
                scanned = static_cast<std::size_t>(phase_end);
                at = phase::head;
                if (pending == 0)
                    return {};
            }
            if (at == phase::elements) {
                if (auto const r = detail::validity::check_pending_items(pending, item.size() - scanned); !r)
                    [[unlikely]]
                    return r;
                at = phase::head;
            }
            if (at == phase::argument)
                if (auto const fits = detail::validity::check_bytes_left(phase_end, item.size()); !fits)
                    [[unlikely]]
                    return std::unexpected(fits.error());
            heads::decoder<false> d{std::string_view{std::span(item).subspan(scanned)}};
            auto const h = d.decode_head();
            if (!h) [[unlikely]] {
                if (h.error() == error::too_little_data &&
                    detail::validity::bytes_fit(heads::initial_byte_size, d.encoded.size())) {
                    auto const initial = static_cast<std::uint8_t>(d.encoded.front());
                    std::uint8_t const info = heads::additional_information_of(initial);
                    at = phase::argument;
                    phase_end = scanned + heads::initial_byte_size + heads::argument_size(info);
                }
                return std::unexpected(h.error());
            }
            at = phase::head;
            scanned =
                std::to_underlying(detail::validity::distance(std::string_view(std::span(item)), d.encoded));
            --pending;
            std::uint64_t children = 0;
            switch (h->major) {
            case major_type::byte_string:
            case major_type::text_string:
                phase_end = detail::validity::checked_add(std::uint64_t{scanned}, h->argument)
                                .value_or(std::numeric_limits<std::uint64_t>::max());
                at = phase::string_content;
                continue;
            case major_type::array:
                children = h->argument;
                break;
            case major_type::map:
                children = detail::validity::checked_mul(h->argument, rfc8949::data_items_per_pair)
                               .value_or(std::numeric_limits<std::uint64_t>::max());
                break;
            case major_type::tag:
                children = 1;
                break;
            default:
                break;
            }
            if (children != 0) {
                pending = detail::validity::checked_add(pending, children)
                              .value_or(std::numeric_limits<std::uint64_t>::max());
                at = phase::elements;
                continue;
            }
            if (pending == 0)
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

    void append_bytes(std::string_view const bytes)
    {
        if (buffer.use_count() == 1) {
            if (item_begin >= buffer->size() - item_begin) {
                buffer->erase(0, item_begin);
                item_begin = 0;
            }
        } else if (!detail::validity::bytes_fit(bytes.size(), buffer->capacity() - buffer->size())) {
            std::string_view const rest{std::span(std::as_const(*buffer)).subspan(item_begin)};
            auto block = std::make_shared<std::string>();
            block->reserve(std::max(buffer->capacity(),
                                    detail::validity::checked_mul(rest.size() + bytes.size(), growth_factor)
                                        .value_or(std::numeric_limits<std::size_t>::max())));
            block->append(rest);
            buffer = std::move(block);
            item_begin = 0;
        }
        buffer->append(bytes);
    }

    void push_piece(held_piece p)
    {
        if (p.bytes.empty())
            return;
        pieces.push_back(std::move(p));
    }

    void piece_drop()
    {
        pieces.pop_front();
        piece_offset = 0;
    }

    void bytes_feed(std::string_view const bytes)
    {
        throw_logic_error_if_moved_from();
        if (failure != error{}) [[unlikely]]
            return;
        if (pieces.empty()) {
            append_bytes(bytes);
            return;
        }
        auto copy = std::make_shared<std::string const>(bytes);
        std::string_view const held(*copy);
        push_piece(held_piece{std::move(copy), held});
    }

    void failure_keep(error const e)
    {
        failure = e;
        pieces.clear();
        piece_offset = 0;
        buffer->resize(item_begin);
    }

    void reset_item_state()
    {
        scanned = 0;
        pending = 1;
        at = phase::head;
    }

public:
    /** @brief Makes a reader that holds no byte. */
    sequence_reader() = default;

    sequence_reader(sequence_reader const &) = delete;

    sequence_reader &operator=(sequence_reader const &) = delete;

    /**
     * @brief Takes the bytes and the state of @p other.
     * @param other the reader that throws on its next call
     */
    sequence_reader(sequence_reader &&other) = default;

    /**
     * @brief Takes the bytes and the state of @p other.
     * @param other the reader that throws on its next call
     */
    sequence_reader &operator=(sequence_reader &&other) = default;

    /** @brief Copies the bytes of a view to the end of the sequence. */
    template <borrowed_range_of_chars Piece>
        requires(!std::is_array_v<Piece>)
    void feed(Piece const piece)
    {
        bytes_feed(std::string_view(std::span<char const>(piece)));
    }

    /** @brief Copies the bytes of a container to the end of the sequence. */
    template <owning_range_of_chars Piece>
        requires(!std::is_array_v<Piece>)
    void feed(Piece const &piece)
    {
        bytes_feed(std::string_view(std::span<char const>(piece)));
    }

    /** @brief Takes a container by move and holds it as the next bytes of the sequence. */
    template <owning_range_of_chars Piece>
    void feed(Piece &&piece)
    {
        feed(std::make_shared<Piece const>(std::move(piece)));
    }

    /** @brief Holds the bytes of @p piece as the next bytes of the sequence, without a copy. */
    template <owning_range_of_chars Piece>
    void feed(std::shared_ptr<Piece const> piece)
    {
        throw_logic_error_if_moved_from();
        detail::validity::throw_logic_error_if_null(piece, "cbor::sequence_reader::feed: the piece is null");
        if (failure != error{}) [[unlikely]]
            return;
        std::string_view const bytes{std::span<char const>(*piece)};
        push_piece(held_piece{std::move(piece), bytes});
    }

    /**
     * @brief Tells if the reader holds no byte that no item took.
     * @return `true` when every byte that was fed is in an item that `next` gave
     */
    bool empty() const
    {
        throw_logic_error_if_moved_from();
        return buffer->size() == item_begin && pieces.empty();
    }

    /** @brief Gives the next item when all of its bytes are there. */
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
            else if (!pieces.empty())
                window = std::string_view{std::span(pieces.front().bytes).subspan(piece_offset)};
            auto const r = scan_item(window);
            if (r) {
                std::string_view const item{std::span(window).first(scanned)};
                std::shared_ptr<void const> owner = copied ? buffer : pieces.front().owner;
                if (copied) {
                    item_begin += scanned;
                } else {
                    piece_offset += scanned;
                    if (piece_offset == pieces.front().bytes.size())
                        piece_drop();
                }
                reset_item_state();
                return lazy::view(std::move(owner), item);
            }
            if (r.error() != error::too_little_data) [[unlikely]] {
                failure_keep(r.error());
                return std::unexpected(r.error());
            }
            if (!copied) {
                if (pieces.size() < 2)
                    return std::unexpected(error::too_little_data);
                append_bytes(window);
                piece_drop();
                continue;
            }
            if (pieces.empty())
                return std::unexpected(error::too_little_data);
            std::string_view const source{std::span(pieces.front().bytes).subspan(piece_offset)};
            std::size_t const n = static_cast<std::size_t>(
                std::min<std::uint64_t>(bytes_missing(window.size()), source.size()));
            append_bytes(std::string_view{std::span(source).first(n)});
            piece_offset += n;
            if (piece_offset == pieces.front().bytes.size())
                piece_drop();
        }
    }
};

}
