// Gives the fuzz target of cbor::sequence and cbor::sequence_reader over the pieces of the input.
#pragma once

#include "../test/sequence_reader.hpp"
#include "common.hpp"

#include <cbor/error.hpp>
#include <cbor/sequence.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <random>
#include <string_view>
#include <vector>

namespace fuzz
{

// cbor::sequence over an owner and a view into it gives the items of the form over a std::shared_ptr, and the
// post-increment steps as the pre-increment does; empty() says whether there is an item.
inline void sequence_forms(std::string_view const bytes)
{
    auto const shared = std::make_shared<std::string const>(bytes);
    auto const held = std::make_shared<std::vector<char> const>(bytes.begin(), bytes.end());
    cbor::sequence const a(shared);
    cbor::sequence const b(held, std::span<char const>(*held));
    require(a.empty() == bytes.empty() && b.empty() == bytes.empty());
    auto i = a.begin();
    auto j = b.begin();
    for (std::size_t n = 0; n <= bytes.size(); ++n) {
        require((i == std::default_sentinel) == (j == std::default_sentinel));
        if (i == std::default_sentinel)
            return;
        auto const x = *i;
        auto const y = *j;
        require(x.has_value() == y.has_value());
        if (x)
            require(*x == *y);
        else
            require(x.error() == y.error());
        ++i;
        j++;
    }
    require(false);
}

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
    require(read == test::reader_outcome_of(bytes, cuts, form, true));
    sequence_forms(bytes);

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
