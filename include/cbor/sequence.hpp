#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <iterator>
#include <span>
#include <string>
#include <ranges>
#include <string_view>
#include <type_traits>

#include "error.hpp"
#include "item_size.hpp"
#include "validity.hpp"

namespace cbor
{

class sequence
{
    std::string_view encoded;

public:
    explicit sequence(std::string_view const e) : encoded(e)
    {
    }

    template <class Encoded>
        requires(!std::is_lvalue_reference_v<Encoded> && !std::ranges::borrowed_range<Encoded>)
    explicit sequence(Encoded &&) = delete;

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
