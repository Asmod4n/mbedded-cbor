#pragma once

#include "common.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fuzz::structs
{

struct inner {
    std::int32_t id;
    std::string name;
};

struct record {
    std::int64_t number;
    std::uint8_t small;
    bool flag;
    double wide;
    float narrow;
    std::string text;
    std::vector<std::int32_t> list;
    std::optional<std::string> maybe;
    std::map<std::string, std::int16_t> table;
    std::array<std::uint16_t, 2> pair;
    inner nested;
    std::vector<inner> people;
};

// cbor::structs reads any CBOR that has the shape of the struct. What it reads, it writes back, and the written bytes
// read to the same value: the bytes of the second write equal those of the first.
template <class T>
void round_trip(std::string_view const input)
{
    auto const value = cbor::structs<T>::decode(std::string(input));
    if (!value)
        return;
    auto const first = cbor::structs<T>::encode(**value);
    require(first.has_value());
    auto const again = cbor::structs<T>::decode(std::string(*first));
    require(again.has_value());
    auto const second = cbor::structs<T>::encode(**again);
    require(second.has_value() && *second == *first);
    auto const end = cbor::item_size(*first);
    require(end.has_value() && *end == first->size());
}

inline void decode_target(std::string_view const input)
{
    round_trip<record>(input);
    round_trip<double>(input);
    round_trip<float>(input);
    round_trip<std::vector<std::string>>(input);
}

} // namespace fuzz::structs
