#pragma once

#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

// The nesting depth of a well-formed item, counted as RFC 8949 nests it: an array or a map with an element
// and a tag each add one level. The reader refuses an item deeper than cbor::limits::nesting_depth, and
// cbor::sequence does not, so the reference computes the depth on its own.
inline std::size_t nesting_depth_read(std::string_view &bytes)
{
    constexpr unsigned additional_information_bits = 5;
    constexpr unsigned additional_information_mask = (1u << additional_information_bits) - 1;
    constexpr unsigned one_byte_argument = 24;
    constexpr unsigned eight_byte_argument = 27;
    constexpr unsigned bits_per_byte = 8;
    constexpr unsigned major_byte_string = 2;
    constexpr unsigned major_text_string = 3;
    constexpr unsigned major_array = 4;
    constexpr unsigned major_map = 5;
    constexpr unsigned major_tag = 6;
    constexpr std::uint64_t data_items_per_pair = 2;
    auto const initial = static_cast<std::uint8_t>(bytes.front());
    unsigned const major = initial >> additional_information_bits;
    unsigned const info = initial & additional_information_mask;
    bytes.remove_prefix(1);
    std::uint64_t argument = info;
    if (info >= one_byte_argument && info <= eight_byte_argument) {
        std::size_t const width = std::size_t{1} << (info - one_byte_argument);
        argument = 0;
        for (std::size_t i = 0; i < width; ++i)
            argument = argument << bits_per_byte | static_cast<std::uint8_t>(bytes[i]);
        bytes.remove_prefix(width);
    }
    if (major == major_byte_string || major == major_text_string) {
        bytes.remove_prefix(static_cast<std::size_t>(argument));
        return 0;
    }
    std::uint64_t const children = major == major_array ? argument
                                   : major == major_map ? data_items_per_pair * argument
                                   : major == major_tag ? 1
                                                  : 0;
    std::size_t deepest = 0;
    for (std::uint64_t i = 0; i < children; ++i)
        deepest = std::max(deepest, nesting_depth_read(bytes));
    return children == 0 ? 0 : deepest + 1;
}

inline std::size_t nesting_depth_of(std::string_view const item)
{
    std::string_view rest = item;
    return nesting_depth_read(rest);
}

// The same items from cbor::sequence over the whole buffer. An item goes through lazy::view, as the items of
// a sequence_reader do, so tag 55799 is stripped on both sides.
inline sequence_outcome sequence_outcome_of(std::string_view const bytes)
{
    sequence_outcome out{{}, cbor::error::too_little_data};
    auto const owner = std::make_shared<std::string const>(bytes);
    for (auto const e : cbor::sequence(owner)) {
        if (!e) {
            out.last = e.error();
            return out;
        }
        if (nesting_depth_of(*e) > cbor::limits::nesting_depth) {
            out.last = cbor::error::nesting_depth_exceeded;
            return out;
        }
        auto const l = cbor::lazy::view(owner, *e);
        if (!l) {
            out.last = l.error();
            return out;
        }
        out.items.emplace_back(l->top_level.encoded);
    }
    return out;
}

// The forms in which a piece reaches the reader: a view that the reader copies, a std::string that the reader
// takes without a copy, a std::shared_ptr that the reader holds, and the three in turn, one per piece.
enum class feed_form { view, rvalue, shared, each_in_turn };

inline constexpr std::array<feed_form, 4> feed_forms{feed_form::view, feed_form::rvalue, feed_form::shared,
                                                     feed_form::each_in_turn};

inline void piece_feed(cbor::sequence_reader &r, std::string_view const piece, feed_form const form,
                       std::size_t const index)
{
    feed_form const used =
        form == feed_form::each_in_turn ? feed_forms[index % (feed_forms.size() - 1)] : form;
    switch (used) {
    case feed_form::view:
        r.feed(piece);
        break;
    case feed_form::rvalue:
        r.feed(std::string(piece));
        break;
    default:
        r.feed(std::make_shared<std::string const>(piece));
        break;
    }
}

// Feeds the bytes to one reader in the pieces that the cuts give, and calls next() after each piece until it
// gives an error.
inline sequence_outcome reader_outcome_of(std::string_view const bytes, std::span<std::size_t const> const cuts,
                                          feed_form const form = feed_form::view)
{
    cbor::sequence_reader r;
    sequence_outcome out{{}, cbor::error::too_little_data};
    std::size_t from = 0;
    for (std::size_t i = 0; i <= cuts.size(); ++i) {
        std::size_t const to = i < cuts.size() ? cuts[i] : bytes.size();
        piece_feed(r, bytes.substr(from, to - from), form, i);
        from = to;
        for (;;) {
            auto const item = r.next();
            if (!item) {
                out.last = item.error();
                break;
            }
            out.items.emplace_back(item->top_level.encoded);
        }
        if (out.last != cbor::error::too_little_data)
            return out;
    }
    return out;
}

inline sequence_outcome reader_outcome_byte_by_byte(std::string_view const bytes,
                                                    feed_form const form = feed_form::view)
{
    std::vector<std::size_t> cuts;
    for (std::size_t i = 1; i < bytes.size(); ++i)
        cuts.push_back(i);
    return reader_outcome_of(bytes, cuts, form);
}

inline sequence_outcome reader_outcome_split_at(std::string_view const bytes, std::size_t const at,
                                                feed_form const form = feed_form::view)
{
    std::size_t const cut[] = {at};
    return reader_outcome_of(bytes, cut, form);
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
