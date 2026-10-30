#pragma once

#include "lazy.hpp"

namespace site
{

inline std::string sequence_item_text(std::expected<std::string_view, cbor::error> const &item)
{
    return item ? notation_of(*item) : kind<cbor::error>::text(item.error());
}

template <>
struct kind<cbor::sequence> {
    static constexpr std::string_view name = "cbor::sequence";
    static std::string text(cbor::sequence const &s)
    {
        std::string items;
        std::size_t count = 0;
        for (auto const item : s) {
            items += (count == 0 ? ": " : ", ") + sequence_item_text(item);
            ++count;
        }
        return "sequence of " + number_text(count) + (count == 1 ? " item" : " items") + items;
    }
};

template <>
struct kind<cbor::sequence::iterator> {
    static constexpr std::string_view name = "cbor::sequence::iterator";
    static std::string text(cbor::sequence::iterator const &i)
    {
        if (i == std::default_sentinel)
            return "iterator at the end";
        return "iterator to " + sequence_item_text(*i);
    }
};

template <>
struct kind<cbor::sequence_reader> {
    static constexpr std::string_view name = "cbor::sequence_reader";
    static std::string text(cbor::sequence_reader const &r)
    {
        return r.empty() ? "sequence_reader that holds no unread byte" : "sequence_reader that holds unread bytes";
    }
};

}
