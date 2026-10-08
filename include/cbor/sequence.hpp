#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

#include "error.hpp"
#include "item_end.hpp"
#include "validity.hpp"

namespace cbor
{

template <std::size_t DepthMax = validity::nesting_depth_default>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
class sequence
{
    std::string_view encoded;

public:
    explicit sequence(std::string_view const e) : encoded(e)
    {
    }

    template <std::same_as<std::string> Encoded>
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
            size = item_end<DepthMax>(encoded);
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
        return iterator{encoded, item_end<DepthMax>(encoded)};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }
};

}
