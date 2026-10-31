// Gives a range over the data items of a CBOR Sequence in a shared
// buffer. Each element is the bytes of one item or an error. No
// element follows an error.
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

class sequence
{
    std::shared_ptr<void const> owner;
    std::string_view encoded;

public:
    template <owning_range_of_chars Encoded>
    explicit sequence(std::shared_ptr<Encoded const> e)
    {
        validity::throw_logic_error_if_null(e, "cbor::sequence: the encoded data items are null");
        encoded = std::string_view(std::ranges::data(*e), std::ranges::size(*e));
        owner = std::move(e);
    }

    template <borrowed_range_of_chars Encoded>
    sequence(std::shared_ptr<void const> o, Encoded const &e)
        : owner(std::move(o)), encoded(std::ranges::data(e), std::ranges::size(e))
    {
        validity::throw_logic_error_if_empty(owner, "cbor::sequence: the owner of the encoded data items is empty");
    }


    struct iterator {
        using value_type = std::expected<std::string_view, error>;
        using difference_type = std::ptrdiff_t;

        std::string_view encoded;
        std::expected<std::size_t, error> size;

        value_type operator*() const
        {
            if (!size) [[unlikely]]
                return std::unexpected(size.error());
            return std::string_view(std::span(encoded).first(*size));
        }

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

        void operator++(int)
        {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const
        {
            return encoded.empty();
        }
    };

    iterator begin() const
    {
        return iterator{encoded, item_size(encoded)};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }

    bool empty() const
    {
        return encoded.empty();
    }
};

}
