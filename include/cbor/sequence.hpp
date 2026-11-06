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

class sequence
{
    std::shared_ptr<void const> owner;
    std::string_view encoded;

public:
    template <owning_range_of_chars Encoded>
    explicit sequence(std::shared_ptr<Encoded const> e)
    {
        validity::throw_logic_error_if_null(e, "cbor::sequence: the encoded data items are null");
        encoded = std::string_view(std::span<char const>(*e));
        owner = std::move(e);
    }

    template <borrowed_range_of_chars Encoded>
    sequence(std::shared_ptr<void const> o, Encoded const &e)
        : owner(std::move(o)), encoded(std::span<char const>(e))
    {
        validity::throw_logic_error_if_empty(owner, "cbor::sequence: the owner of the encoded data items is empty");
    }


    class iterator
    {
        friend class sequence;

        std::string_view bytes;
        std::expected<std::size_t, error> size;

        iterator(std::string_view const b, std::expected<std::size_t, error> const s) : bytes(b), size(s) {}

    public:
        using value_type = std::expected<std::string_view, error>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        std::string_view encoded() const
        {
            return bytes;
        }

        value_type operator*() const
        {
            if (!size) [[unlikely]]
                return std::unexpected(size.error());
            return validity::substr(bytes, validity::offset{0}, *size);
        }

        iterator &operator++()
        {
            if (!size) [[unlikely]] {
                bytes = std::string_view(std::span(bytes).last(0));
                return *this;
            }
            bytes = validity::subspan(bytes, validity::offset{*size});
            size = item_size(bytes);
            return *this;
        }

        void operator++(int)
        {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const
        {
            return bytes.empty();
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
