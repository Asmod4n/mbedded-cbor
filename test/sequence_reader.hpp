#pragma once

#include <cbor/error.hpp>
#include <cbor/lazy.hpp>
#include <cbor/sequence.hpp>
#include <cbor/sequence_reader.hpp>
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
        auto const l = cbor::lazy::view(owner, *e);
        if (!l) {
            out.last = l.error();
            return out;
        }
        out.items.emplace_back(l->top_level.encoded);
    }
    return out;
}

// The forms in which a piece reaches the reader: a view that the reader copies, a std::vector<char> that the
// reader takes without a copy, a std::shared_ptr that the reader holds, an lvalue std::vector<char> that the reader
// copies, and the four in turn, one per piece.
// The owned forms are a std::vector<char> of the exact size of the piece and not a std::string, because the
// inline buffer and the closing zero of a std::string hide a read of up to 16 bytes past the piece from ASan.
enum class feed_form { view, rvalue, shared, lvalue, each_in_turn };

inline constexpr std::array<feed_form, 5> feed_forms{feed_form::view, feed_form::rvalue, feed_form::shared,
                                                     feed_form::lvalue, feed_form::each_in_turn};

inline cbor::sequence_reader piece_fed(cbor::sequence_reader r, std::string_view const piece, feed_form const form,
                                       std::size_t const index)
{
    feed_form const used =
        form == feed_form::each_in_turn ? feed_forms[index % (feed_forms.size() - 1)] : form;
    switch (used) {
    case feed_form::view:
        r.feed(piece);
        break;
    case feed_form::rvalue:
        r.feed(std::vector<char>(piece.begin(), piece.end()));
        break;
    case feed_form::lvalue: {
        std::vector<char> const owned(piece.begin(), piece.end());
        r.feed(owned);
        break;
    }
    default:
        r.feed(std::make_shared<std::vector<char> const>(piece.begin(), piece.end()));
        break;
    }
    return r;
}

// Feeds the bytes to one reader in the pieces that the cuts give, and calls next() after each piece until it
// gives an error. With moved, the reader moves into a new reader by construction and back by assignment after the
// middle piece, so a move in the middle of a stream keeps every item.
inline sequence_outcome reader_outcome_of(std::string_view const bytes, std::span<std::size_t const> const cuts,
                                          feed_form const form = feed_form::view, bool const moved = false)
{
    cbor::sequence_reader r;
    sequence_outcome out{{}, cbor::error::too_little_data};
    std::size_t from = 0;
    for (std::size_t i = 0; i <= cuts.size(); ++i) {
        if (moved && i == cuts.size() / 2) {
            cbor::sequence_reader middle(std::move(r));
            r = std::move(middle);
        }
        std::size_t const to = i < cuts.size() ? cuts[i] : bytes.size();
        r = piece_fed(std::move(r), bytes.substr(from, to - from), form, i);
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
                constexpr std::string_view digits = "0123456789abcdef";
                auto const byte = static_cast<unsigned char>(c);
                text += digits[byte >> 4];
                text += digits[byte & 0xf];
            }
        }
        text += " last " + std::to_string(static_cast<int>(o.last));
        return text.c_str();
    }
};
