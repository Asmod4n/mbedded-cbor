#pragma once

#include <cbor/encode.hpp>
#include <cbor/item.hpp>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace test
{

// Compares two trees as theories/Writer.v unfold does. A shared_reference stands for the item it names. encode and then
// decode give other indices. They write a value without a cycle twice. So a comparison of the nodes does not compare
// such trees. A pair that is already under comparison counts as equal, so a cycle ends (theories/Keys.v compares two
// graphs by the same bisimilarity). An item with no children compares by the bytes that encode writes for it. The fuzz
// targets use it too, so it needs no test framework.
class bisimilarity
{
    std::vector<std::pair<cbor::item, cbor::item>> assumed;

    static cbor::item resolved(cbor::item const i)
    {
        if (i.type() == cbor::type::shared_reference)
            return i.get<std::pair<std::uint64_t, cbor::item>>()->second;
        return i;
    }

    static std::string bytes_of(cbor::item const i)
    {
        std::string out;
        if (!cbor::encode(out, i))
            return {};
        return out;
    }

    bool all_hold(std::ranges::forward_range auto const l, std::ranges::forward_range auto const r)
    {
        if (l.size() != r.size())
            return false;
        for (auto x = l.begin(), y = r.begin(); x != l.end(); ++x, ++y)
            if (!holds(*x, *y))
                return false;
        return true;
    }

    bool tags_hold(cbor::item const a, cbor::item const b)
    {
        auto const l = a.get<std::pair<std::uint64_t, cbor::item>>().value();
        auto const r = b.get<std::pair<std::uint64_t, cbor::item>>().value();
        return l.first == r.first && holds(l.second, r.second);
    }

    bool all_pairs_hold(std::ranges::forward_range auto const l, std::ranges::forward_range auto const r)
    {
        if (l.size() != r.size())
            return false;
        for (auto x = l.begin(), y = r.begin(); x != l.end(); ++x, ++y)
            if (!holds(std::get<0>(*x), std::get<0>(*y)) || !holds(std::get<1>(*x), std::get<1>(*y)))
                return false;
        return true;
    }

public:
    bool holds(cbor::item const x, cbor::item const y)
    {
        cbor::item const a = resolved(x);
        cbor::item const b = resolved(y);
        if (std::ranges::contains(assumed, std::pair(a, b)))
            return true;
        assumed.emplace_back(a, b);
        if (a.type() != b.type() || a.additional_information() != b.additional_information())
            return false;
        switch (a.type()) {
        case cbor::type::array:
            return all_hold(a.elements().value(), b.elements().value());
        case cbor::type::map:
            return all_pairs_hold(a.pairs().value(), b.pairs().value());
        case cbor::type::tag:
            return tags_hold(a, b);
        default:
            return bytes_of(a) == bytes_of(b);
        }
    }
};

inline bool unfoldings_equal(cbor::item const a, cbor::item const b)
{
    return bisimilarity{}.holds(a, b);
}

} // namespace test
