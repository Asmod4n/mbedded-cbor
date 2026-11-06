#pragma once

#include "common.hpp"

#include <cbor/item_size.hpp>
#include <cbor/structs.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
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

// Every form of decode reads the input to the value that the std::string form reads: a std::shared_ptr to the
// bytes, an owner with a span into them, and both with a default target. The value is compared by its encoding.
template <class T>
void decode_forms(std::string_view const input, std::string const &expected)
{
    auto const bytes = std::make_shared<std::vector<char> const>(input.begin(), input.end());
    auto const encoded_as = [&](auto const &decoded) {
        require(decoded.has_value());
        auto const again = cbor::structs<T>::encode(**decoded);
        require(again.has_value() && *again == expected);
    };
    encoded_as(cbor::structs<T>::decode(bytes));
    encoded_as(cbor::structs<T>::decode(bytes, T{}));
    encoded_as(cbor::structs<T>::decode(bytes, std::span<char const>(*bytes)));
    encoded_as(cbor::structs<T>::decode(bytes, std::span<char const>(*bytes), T{}));
}

// Every target of encode writes the bytes of the std::string form: a container, a span of the exact size, a span
// one byte longer and a writer with allocate, append and done. A span one byte short gives no_buffer_space.
template <class T>
void encode_targets(T const &value, std::string const &expected)
{
    std::vector<char> chars;
    require(cbor::structs<T>::encode(value, chars) == expected.size() && std::ranges::equal(chars, expected));
    std::vector<char> exact(expected.size());
    require(cbor::structs<T>::encode(value, std::span<char>(exact)) == expected.size() &&
            std::ranges::equal(exact, expected));
    std::vector<char> longer(expected.size() + 1);
    require(cbor::structs<T>::encode(value, std::span<char>(longer)) == expected.size() &&
            std::ranges::equal(std::span(longer).first(expected.size()), expected));
    if (!expected.empty()) {
        std::vector<char> short_by_one(expected.size() - 1);
        auto const refused = cbor::structs<T>::encode(value, std::span<char>(short_by_one));
        require(!refused && refused.error() == cbor::error::no_buffer_space);
    }
    string_writer w;
    require(cbor::structs<T>::encode(value, w) == expected.size() && w.encoded == expected);
}

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
    decode_forms<T>(input, *first);
    encode_targets<T>(**value, *first);
}

inline void decode_target(std::string_view const input)
{
    round_trip<record>(input);
    round_trip<double>(input);
    round_trip<float>(input);
    round_trip<std::vector<std::string>>(input);
}

} // namespace fuzz::structs
