#pragma once

#include <cbor/encode.hpp>
#include <cbor/item.hpp>

#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <utility>

namespace test
{

// Compares two trees as theories/Writer.v unfold does: a shared_reference stands for the item it names. encode and
// then decode give other indices and write a value without a cycle twice, so a comparison of the nodes does not
// compare such trees. A pair that is already under comparison counts as equal, so a cycle ends (theories/Keys.v
// compares two graphs by the same bisimilarity). An item with no children compares by the bytes that encode writes
// for it. The fuzz targets use it too, so it needs no test framework.
class bisimilarity
{
    std::set<std::pair<cbor::item const *, cbor::item const *>> assumed;

    static cbor::item const &resolved(cbor::item const &i)
    {
        if (auto const r = i.get<cbor::type::shared_reference>())
            return *r->second;
        return i;
    }

    static std::string bytes_of(cbor::item const &i)
    {
        std::string out;
        if (!cbor::encode(i, out))
            return {};
        return out;
    }

    bool all_hold(std::span<cbor::item const> const l, std::span<cbor::item const> const r)
    {
        if (l.size() != r.size())
            return false;
        for (std::size_t i = 0; i < l.size(); ++i)
            if (!holds(l[i], r[i]))
                return false;
        return true;
    }

    template <cbor::type K>
    bool tags_hold(cbor::item const &a, cbor::item const &b)
    {
        auto const l = a.get<K>().value();
        auto const r = b.get<K>().value();
        return l.first == r.first && holds(*l.second, *r.second);
    }

public:
    bool holds(cbor::item const &x, cbor::item const &y)
    {
        cbor::item const &a = resolved(x);
        cbor::item const &b = resolved(y);
        if (!assumed.emplace(&a, &b).second)
            return true;
        if (a.type != b.type || a.additional_information != b.additional_information)
            return false;
        switch (a.type) {
        case cbor::type::array:
            return all_hold(a.get<cbor::type::array>().value(), b.get<cbor::type::array>().value());
        case cbor::type::map:
            return all_hold(a.get<cbor::type::map>().value(), b.get<cbor::type::map>().value());
        case cbor::type::tag:
            return tags_hold<cbor::type::tag>(a, b);
        case cbor::type::typed_array:
            return tags_hold<cbor::type::typed_array>(a, b);
        default:
            return bytes_of(a) == bytes_of(b);
        }
    }
};

inline bool unfoldings_equal(cbor::item const &a, cbor::item const &b)
{
    return bisimilarity{}.holds(a, b);
}

} // namespace test
