#pragma once

#include "../test/sequence_reader.hpp"
#include "common.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string_view>
#include <vector>

namespace fuzz
{

// A sequence_reader gives one outcome for the same bytes, whatever the pieces they arrive in. The first byte
// of the input seeds the lengths of the pieces; the rest is the CBOR Sequence. The outcome is compared with
// cbor::sequence over the whole buffer. Two differences are allowed, and each is checked for what it is:
// cbor::sequence gives too_little_data when an array or a map announces more items than bytes remain, where
// the reader reads the malformed byte after the head; and the reader refuses an item nested deeper than the
// limit, which cbor::sequence does not check. A decode of that item also fails, with nesting_depth_exceeded or
// with an error that it finds first on its way down.
inline void sequence_reader_target(std::string_view const input)
{
    if (input.empty())
        return;
    std::minstd_rand pieces(static_cast<std::uint8_t>(input.front()) + 1u);
    std::string_view const bytes = input.substr(1);
    std::vector<std::size_t> cuts;
    for (std::size_t at = 0;;) {
        at += 1 + pieces() % 16;
        if (at >= bytes.size())
            break;
        cuts.push_back(at);
    }
    test::sequence_outcome const read = test::reader_outcome_of(bytes, cuts);
    require(read == test::reader_outcome_split_at(bytes, 0));

    test::sequence_outcome const whole = test::sequence_outcome_of(bytes);
    std::size_t const n = read.items.size();
    require(n <= whole.items.size());
    for (std::size_t i = 0; i < n; ++i)
        require(read.items[i] == whole.items[i]);
    if (read.last == cbor::error::nesting_depth_exceeded) {
        if (whole.items.size() > n) {
            auto const deep = test::decoded(whole.items[n]);
            require(!deep.has_value());
        }
        return;
    }
    require(whole.items.size() == n);
    if (read.last == cbor::error::too_little_data)
        require(whole.last == cbor::error::too_little_data);
    else if (whole.last != cbor::error::too_little_data)
        require(whole.last == read.last);
}

} // namespace fuzz
