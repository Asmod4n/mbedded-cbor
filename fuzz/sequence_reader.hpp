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

// A sequence_reader gives one outcome for the same bytes, whatever the pieces they arrive in and whatever form of
// feed brings them. The first byte of the input chooses the form and seeds the lengths of the pieces; the rest is
// the CBOR Sequence. The outcome is compared with cbor::sequence over the whole buffer, and it is the same, with
// one difference: the reader refuses an item as soon as it is nested deeper than the limit, so it can give
// nesting_depth_exceeded where the rest of the item is cut or not well-formed. The reference never gives such an
// item.
inline void sequence_reader_target(std::string_view const input)
{
    if (input.empty())
        return;
    auto const first = static_cast<std::uint8_t>(input.front());
    test::feed_form const form = test::feed_forms[first % test::feed_forms.size()];
    std::minstd_rand pieces(static_cast<std::uint_fast32_t>(first / test::feed_forms.size() + 1u));
    std::string_view const bytes = input.substr(1);
    constexpr std::size_t piece_size_max = 16;
    std::vector<std::size_t> cuts;
    for (std::size_t at = 0;;) {
        at += 1 + pieces() % piece_size_max;
        if (at >= bytes.size())
            break;
        cuts.push_back(at);
    }
    test::sequence_outcome const read = test::reader_outcome_of(bytes, cuts, form);
    require(read == test::reader_outcome_split_at(bytes, 0));

    test::sequence_outcome const whole = test::sequence_outcome_of(bytes);
    std::size_t const n = read.items.size();
    require(n <= whole.items.size());
    for (std::size_t i = 0; i < n; ++i)
        require(read.items[i] == whole.items[i]);
    if (read.last == cbor::error::nesting_depth_exceeded) {
        require(whole.items.size() == n);
        return;
    }
    require(whole == read);
}

} // namespace fuzz
