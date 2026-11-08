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
 * @brief Splits a CBOR sequence (RFC 8742) into its items.
 *
 * Each item is a `std::string_view` into the bytes, with no copy.
 * The sequence holds the owner of the bytes. A view stays valid while the sequence or the owner lives.
 * The sequence is an input range: it has no size, and `begin()` reads the first item again.
 * Each item gives the errors of cbor::item_size(). After an item with an error, the loop ends.
 *
 * @code{.callseq}
 * cbor::sequence(owner) → sequence
 * s.begin()             → sequence::iterator
 * *it                   → std::expected<std::string_view, error>
 * s.empty()             → bool
 * @endcode
 * @code{.example}
 * bytes: 01 02 03
 * call: auto const owner = std::make_shared<std::string const>(bytes)
 * result: shared_ptr to 3 bytes: 01 02 03
 * call: auto const s = cbor::sequence(owner)
 * result: sequence of 3 items: 1, 2, 3
 * call: s.empty()
 * result: false
 * call: cbor::at_path<"$", std::int64_t>(**s.begin())
 * result: 1
 * @endcode
 *
 * @snippet doc_reference_tools.cpp sequence
 * @see cbor::item_size()
 */
class sequence
{
    std::shared_ptr<void const> owner;
    std::string_view encoded;

public:
    /**
     * @brief Makes a sequence that owns the bytes.
     * @param e the pointer to the bytes of the sequence
     * @throws std::logic_error the pointer @p e is null
     */
    template <owning_range_of_chars Encoded>
    explicit sequence(std::shared_ptr<Encoded const> e)
    {
        detail::validity::throw_logic_error_if_null(e, "cbor::sequence: the encoded data items are null");
        encoded = std::string_view(std::span<char const>(*e));
        owner = std::move(e);
    }

    /**
     * @brief Makes a sequence over bytes that @p o owns.
     * @param o the owner of the bytes
     * @param e the bytes of the sequence
     * @throws std::logic_error the owner @p o owns nothing
     */
    template <borrowed_range_of_chars Encoded>
    sequence(std::shared_ptr<void const> o, Encoded const &e)
        : owner(std::move(o)), encoded(std::span<char const>(e))
    {
        detail::validity::throw_logic_error_if_empty(
            owner, "cbor::sequence: the owner of the encoded data items is empty");
    }


    /** @brief Reads the items of a sequence one after the other. */
    class iterator
    {
        friend class sequence;

        std::string_view bytes;
        std::expected<std::size_t, error> size;

        iterator(std::string_view const b, std::expected<std::size_t, error> const s) : bytes(b), size(s) {}

    public:
        /** @brief The bytes of one item, or the error of that item. */
        using value_type = std::expected<std::string_view, error>;
        /** @brief The type of the distance between two iterators. */
        using difference_type = std::ptrdiff_t;

        /** @brief Makes an iterator that is at the end. */
        iterator() = default;

        /**
         * @brief Gives the bytes from the current item to the end of the sequence.
         * @return the current item and the bytes after it
         */
        std::string_view encoded() const
        {
            return bytes;
        }

        /**
         * @brief Gives the current item.
         * @return the bytes of the item, or its error
         */
        value_type operator*() const
        {
            if (!size) [[unlikely]]
                return std::unexpected(size.error());
            return detail::validity::substr(bytes, detail::validity::offset{0}, *size);
        }

        /**
         * @brief Goes to the next item, or to the end after an item with an error.
         * @return this iterator
         */
        iterator &operator++()
        {
            if (!size) [[unlikely]] {
                bytes = std::string_view(std::span(bytes).last(0));
                return *this;
            }
            bytes = detail::validity::subspan(bytes, detail::validity::offset{*size});
            size = item_size(bytes);
            return *this;
        }

        /** @brief Goes to the next item, as the prefix `++` does. */
        void operator++(int)
        {
            ++*this;
        }

        /**
         * @brief Tells if the iterator is at the end.
         * @return `true` when no bytes are left
         */
        bool operator==(std::default_sentinel_t) const
        {
            return bytes.empty();
        }
    };

    /**
     * @brief Reads the first item and gives an iterator to it.
     * @return the iterator at the first item, or at the end when the sequence has no bytes
     */
    iterator begin() const
    {
        return iterator{encoded, item_size(encoded)};
    }

    /**
     * @brief Gives the end of the range.
     * @return the sentinel that ends the loop
     */
    std::default_sentinel_t end() const
    {
        return {};
    }

    /**
     * @brief Tells if the sequence has no bytes.
     * @return `true` when the sequence has no bytes
     */
    bool empty() const
    {
        return encoded.empty();
    }
};

}
