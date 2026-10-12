#pragma once

#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace test
{

// What a reader of a CBOR Sequence hands out: the bytes of each item it gives, and the error that ends the
// run. too_little_data ends a run whose bytes are all read; any other error ends a run at the item that has
// it.
struct sequence_outcome {
    std::vector<std::string> items;
    cbor::error last;

    bool operator==(sequence_outcome const &) const = default;
};

// The same items from cbor::sequence over the whole buffer. An item goes through lazy::from, as the items of
// a sequence_reader do, so tag 55799 is stripped on both sides.
inline sequence_outcome sequence_outcome_of(std::string_view const bytes)
{
    sequence_outcome out{{}, cbor::error::too_little_data};
    for (auto const e : cbor::sequence{bytes}) {
        if (!e) {
            out.last = e.error();
            return out;
        }
        auto const l = cbor::lazy::from(*e);
        if (!l) {
            out.last = l.error();
            return out;
        }
        out.items.emplace_back(l->top_level->encoded);
    }
    return out;
}

// Feeds the bytes to one reader in the pieces that the cuts give, and calls next() after each piece until it
// gives an error.
inline sequence_outcome reader_outcome_of(std::string_view const bytes,
                                          std::span<std::size_t const> const cuts)
{
    cbor::sequence_reader r;
    sequence_outcome out{{}, cbor::error::too_little_data};
    std::size_t from = 0;
    for (std::size_t i = 0; i <= cuts.size(); ++i) {
        std::size_t const to = i < cuts.size() ? cuts[i] : bytes.size();
        r.feed(bytes.substr(from, to - from));
        from = to;
        for (;;) {
            auto const item = r.next();
            if (!item) {
                out.last = item.error();
                break;
            }
            out.items.emplace_back(item->top_level->encoded);
        }
        if (out.last != cbor::error::too_little_data)
            return out;
    }
    return out;
}

inline sequence_outcome reader_outcome_byte_by_byte(std::string_view const bytes)
{
    std::vector<std::size_t> cuts;
    for (std::size_t i = 1; i < bytes.size(); ++i)
        cuts.push_back(i);
    return reader_outcome_of(bytes, cuts);
}

inline sequence_outcome reader_outcome_split_at(std::string_view const bytes, std::size_t const at)
{
    std::size_t const cut[] = {at};
    return reader_outcome_of(bytes, cut);
}

} // namespace test

// A failed check prints the count of items, each item in hexadecimal, and the error that ended the run.
template <>
struct doctest::StringMaker<test::sequence_outcome> {
    static doctest::String convert(test::sequence_outcome const &o)
    {
        std::string text = std::to_string(o.items.size()) + " items:";
        for (std::string const &item : o.items) {
            text += ' ';
            for (char const c : item) {
                constexpr char digits[] = "0123456789abcdef";
                auto const byte = static_cast<unsigned char>(c);
                text += digits[byte >> 4];
                text += digits[byte & 0xf];
            }
        }
        text += " last " + std::to_string(static_cast<int>(o.last));
        return text.c_str();
    }
};
