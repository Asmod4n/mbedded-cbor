#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

#include "error.hpp"
#include "head.hpp"
#include "validity.hpp"

namespace cbor
{

class value_sharing
{
public:
    enum class index : std::size_t {};

    CBOR_ALWAYS_INLINE static constexpr std::expected<index, error>
    check_sharedref_index(std::uint64_t const argument, std::size_t const marked)
    {
        if (!std::in_range<std::size_t>(argument)) [[unlikely]]
            return std::unexpected(error::sharedref_index_out_of_range);
        if (argument >= marked) [[unlikely]]
            return std::unexpected(error::sharedref_index_not_marked);
        return index{static_cast<std::size_t>(argument)};
    }

private:
    template <bool Checked>
    struct sharing_decoder;

    friend class generic;

    template <class>
    friend class structs;
};

template <bool Checked>
struct value_sharing::sharing_decoder : heads::decoder<Checked> {
    enum class slot : std::uint8_t { in_skipped_member, open, value_member };

    std::vector<slot> slots;

    void mark(heads::decoder<Checked> const &)
    {
        slots.push_back(slot::in_skipped_member);
    }
};

}
