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
 * @brief A range over the items in a CBOR Sequence.
 * @headerfile sequence.hpp cbor/sequence.hpp
 *
 * Each item is a view into the encoded bytes. The sequence retains its
 * shared owner. Iterators and item views do not retain that owner; keep the
 * sequence or another owner alive. Keep the storage address and size stable, and its bytes
 * unchanged while using iterators or item views.
 * If an item's encoded size cannot be determined, iteration yields an error
 * for that item and then ends. An empty sequence's begin() equals end().
 *
 * @note A null pointer in the first constructor, or an ownership-empty
 * pointer in the second, throws std::logic_error when exceptions are
 * enabled and terminates the process otherwise. The second constructor
 * accepts a null stored pointer if it still shares ownership of the bytes.
 *
 * @par Complexity
 * Auxiliary storage is O(1). Construction and scanning allocate nothing
 * on the normal path. Releasing the last shared owner can run its deleter.
 *
 * @code
 * #include <cbor/sequence.hpp>
 *
 * int main()
 * {
 *     auto const owner = std::make_shared<std::string const>("\x01\x02\x03", 3);
 *     std::size_t count = 0;
 *     for (auto const item : cbor::sequence(owner)) {
 *         if (!item)
 *             return 1;
 *         count += item->size();
 *     }
 *     return count == 3 ? 0 : 1;
 * }
 * @endcode
 */
class sequence
{
    std::shared_ptr<void const> owner;
    std::string_view encoded;

public:
    /**
     * @brief Make a sequence that shares ownership of its encoded bytes.
     * @param e A non-null shared pointer to a character range.
     * @throws std::logic_error if @p e is null and exceptions are enabled.
     * @par Complexity
     * O(1); reads no encoded bytes. The normal path allocates nothing.
     */
    template <owning_range_of_chars Encoded>
    explicit sequence(std::shared_ptr<Encoded const> e)
    {
        validity::throw_logic_error_if_null(e, "cbor::sequence: the encoded data items are null");
        encoded = std::string_view(std::ranges::data(*e), std::ranges::size(*e));
        owner = std::move(e);
    }

    /**
     * @brief Make a sequence over a borrowed character range.
     * @param o A non-empty shared owner that keeps the bytes of @p e alive.
     * @param e A contiguous borrowed character range of encoded bytes.
     * @pre @p o keeps the bytes of @p e alive and unchanged for their use.
     * @throws std::logic_error if @p o is empty and exceptions are enabled.
     * @par Complexity
     * O(1); reads no encoded bytes. The normal path allocates nothing.
     */
    template <borrowed_range_of_chars Encoded>
    sequence(std::shared_ptr<void const> o, Encoded const &e)
        : owner(std::move(o)), encoded(std::ranges::data(e), std::ranges::size(e))
    {
        validity::throw_logic_error_if_empty(owner, "cbor::sequence: the owner of the encoded data items is empty");
    }


    /**
     * @brief An input iterator that borrows the encoded bytes.
     * @headerfile sequence.hpp cbor/sequence.hpp
     */
    struct iterator {
        /// Encoded item bytes or an error from item_size().
        using value_type = std::expected<std::string_view, error>;
        /// A signed distance type for the iterator interface.
        using difference_type = std::ptrdiff_t;

        std::string_view encoded;
        std::expected<std::size_t, error> size;

        /**
         * @brief Return the current item as a view, or its size error.
         *
         * This iterator retains no owner of the bytes.
         * @return The encoded item bytes, or the error found while locating it.
         * @pre This iterator is valid and is not the end iterator.
         * @pre The bytes referenced by this iterator are alive.
         * @par Complexity
         * O(1); allocates nothing.
         */
        value_type operator*() const
        {
            if (!size) [[unlikely]]
                return std::unexpected(size.error());
            return std::string_view(std::span(encoded).first(*size));
        }

        /**
         * @brief Advance to the next item.
         *
         * After an item size error, this moves to the end of the range.
         * This iterator retains no owner of the bytes.
         * @return This iterator after the advance.
         * @pre This iterator is valid and is not the end iterator.
         * @pre The bytes referenced by this iterator are alive.
         * @par Complexity
         * O(1 + n), where n is the number of CBOR heads examined to size the
         * next item. After an error, O(1). Allocates nothing.
         */
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

        /**
         * @brief Advance to the next item.
         *
         * This iterator retains no owner of the bytes.
         * @pre This iterator is valid and is not the end iterator.
         * @pre The bytes referenced by this iterator are alive.
         * @par Complexity
         * O(1 + n), where n is the number of CBOR heads examined to size the
         * next item. After an error, O(1). Allocates nothing.
         */
        void operator++(int)
        {
            ++*this;
        }

        /**
         * @brief Test whether this iterator reached the end.
         * @return Whether no encoded bytes remain.
         * @par Complexity
         * O(1); allocates nothing.
         */
        bool operator==(std::default_sentinel_t) const
        {
            return encoded.empty();
        }
    };

    /**
     * @brief Return an iterator to the first item.
     * @return An iterator with the first item's size, or its error. For an
     * empty sequence, this iterator equals end().
     * @pre The bytes referenced by this sequence are alive.
     * @par Complexity
     * O(1 + n), where n is the number of CBOR heads examined to size the
     * first item. An empty input takes O(1). Allocates nothing.
     */
    iterator begin() const
    {
        return iterator{encoded, item_size(encoded)};
    }

    /**
     * @brief Return the range sentinel.
     * @return The end sentinel.
     * @par Complexity
     * O(1); allocates nothing.
     */
    std::default_sentinel_t end() const
    {
        return {};
    }

    /**
     * @brief Test whether the sequence has no encoded bytes.
     * @return Whether the encoded range is empty.
     * @par Complexity
     * O(1); allocates nothing.
     */
    bool empty() const
    {
        return encoded.empty();
    }
};

}
