// Gives a range over the data items of a CBOR Sequence in a shared
// buffer. Each element is the bytes of one item or an error. The range
// ends after the first error.
// https://www.rfc-editor.org/rfc/rfc8742#section-2
#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

#include "error.hpp"
#include "item_size.hpp"
#include "ref.hpp"
#include "validity.hpp"

namespace cbor
{

/**
 * @brief A non-copying input range over a CBOR Sequence (RFC 8742).
 * @headerfile sequence.hpp cbor/sequence.hpp
 *
 * The sequence retains its owner; iterators and item views do not. Keep the
 * storage alive, at the same address and size, and its bytes unchanged.
 * A size error is yielded once, then iteration ends. Construction and
 * iteration allocate nothing on the normal path; storage is O(1).
 * Invalid ownership throws std::logic_error, or terminates without exceptions.
 * Releasing the last owner can run its deleter.
 *
 * @include sequence-example.cpp
 */
class sequence
{
    std::shared_ptr<void const> owner;
    std::string_view encoded;

public:
    /// Shares ownership of the encoded bytes.
    /// @param e Non-null owner of the encoded bytes.
    /// @par Complexity
    /// O(1); reads no bytes.
    template <owning_range_of_chars Encoded>
    explicit sequence(std::shared_ptr<Encoded const> e)
    {
        validity::throw_logic_error_if_null(e, "cbor::sequence: the encoded data items are null");
        encoded = std::string_view(std::ranges::data(*e), std::ranges::size(*e));
        owner = std::move(e);
    }

    /// Views borrowed bytes.
    /// @param o Owner with a control block that keeps the bytes of @p e alive.
    /// @param e Borrowed encoded bytes.
    /// @par Complexity
    /// O(1); reads no bytes.
    template <borrowed_range_of_chars Encoded>
    sequence(std::shared_ptr<void const> o, Encoded const &e)
        : owner(std::move(o)), encoded(std::ranges::data(e), std::ranges::size(e))
    {
        validity::throw_logic_error_if_empty(owner, "cbor::sequence: the owner of the encoded data items is empty");
    }


    /// @brief An input iterator that borrows the encoded bytes.
    /// @headerfile sequence.hpp cbor/sequence.hpp
    /// Dereferencing or incrementing requires a valid, non-end iterator.
    struct iterator {
        /// Encoded item bytes or an error from item_size().
        using value_type = std::expected<std::string_view, error>;
        /// A signed distance type for the iterator interface.
        using difference_type = std::ptrdiff_t;

        std::string_view encoded;
        std::expected<std::size_t, error> size;

        /// @return The current item view or its size error.
        /// @par Complexity
        /// O(1).
        value_type operator*() const
        {
            if (!size) [[unlikely]]
                return std::unexpected(size.error());
            return std::string_view(std::span(encoded).first(*size));
        }

        /// Advances to the next item; after an error, advances to the end.
        /// @return This iterator after the advance.
        /// @par Complexity
        /// O(1 + n), where n is the number of heads examined in the next item.
        iterator &operator++()
        {
            if (!size) [[unlikely]] {
                encoded = std::string_view(std::span(encoded).last(0));
                return *this;
            }
            encoded = std::string_view(std::span(encoded).subspan(*size));
            size = item_size(encoded);
            return *this;
        }

        /// Advances to the next item; after an error, advances to the end.
        /// @par Complexity
        /// O(1 + n), where n is the number of heads examined in the next item.
        void operator++(int)
        {
            ++*this;
        }

        /// @return Whether no encoded bytes remain.
        /// @par Complexity
        /// O(1).
        bool operator==(std::default_sentinel_t) const
        {
            return encoded.empty();
        }
    };

    /// @return An iterator to the first item, or the end for empty input.
    /// @par Complexity
    /// O(1 + n), where n is the number of heads examined in the first item.
    iterator begin() const
    {
        return iterator{encoded, item_size(encoded)};
    }

    /// @return The end sentinel.
    /// @par Complexity
    /// O(1).
    std::default_sentinel_t end() const
    {
        return {};
    }

    /// @return Whether the sequence contains no bytes.
    /// @par Complexity
    /// O(1).
    bool empty() const
    {
        return encoded.empty();
    }
};

}
