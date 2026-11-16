// Checks the index of a sharedref (tag 29) against the count of values
// marked as shareable (tag 28).
// http://cbor.schmorp.de/value-sharing
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>

#include "error.hpp"
#include "validity.hpp"

namespace cbor
{

namespace detail
{

class value_sharing
{
public:
    enum class index : std::size_t {};

    CBOR_ALWAYS_INLINE static constexpr std::expected<index, error>
    check_sharedref_index(std::uint64_t const argument, std::size_t const marked)
    {
        if (argument >= marked) [[unlikely]]
            return std::unexpected(error::sharedref_index_not_marked);
        return index{static_cast<std::size_t>(argument)};
    }
};

}

}
