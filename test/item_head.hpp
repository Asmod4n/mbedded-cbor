#pragma once

#include <cbor/cbor.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

// The argument of the head of an item, as the encoder writes it; a float is read back from its encoding, so that a test compares an item with the bytes of
// its head without a case for each type.
inline std::uint64_t argument_of(cbor::item const &node)
{
    switch (node.type) {
    case cbor::type::byte_string:
        return node.via.bin.size();
    case cbor::type::text_string:
        return node.via.str.size();
    case cbor::type::array:
        return node.via.array.size();
    case cbor::type::map:
        return node.via.map.size() / 2;
    case cbor::type::tag:
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
    case cbor::type::typed_array:
        return node.via.tag.number;
    case cbor::type::false_value:
    case cbor::type::true_value:
    case cbor::type::null:
    case cbor::type::undefined:
    case cbor::type::simple_value:
        return std::to_underlying(node.via.simple_value);
    case cbor::type::half_float:
    case cbor::type::single_float:
    case cbor::type::double_float: {
        std::string head;
        REQUIRE(cbor::encode(node, head).has_value());
        std::uint64_t bits = 0;
        for (char const c : std::string_view(head).substr(1))
            bits = bits << 8 | static_cast<std::uint8_t>(c);
        return bits;
    }
    default:
        return node.via.u64;
    }
}

// The children of an array or a map as pointers, so that a test reads each child as it read it before.
inline std::vector<cbor::item const *> children_of(cbor::item const &container)
{
    REQUIRE((container.type == cbor::type::array || container.type == cbor::type::map));
    std::vector<cbor::item const *> children;
    for (cbor::item const &child : container.type == cbor::type::array ? container.via.array : container.via.map)
        children.push_back(&child);
    return children;
}


// RFC 8949 3.1: the major type of each member of cbor::type. Tags 2, 3 and the typed arrays of RFC 8746 are major
// type 6; false, true, null, undefined, the other simple values and the floats are major type 7.
inline cbor::major_type major_of(cbor::type const t)
{
    switch (t) {
    case cbor::type::unsigned_integer:
        return cbor::major_type::unsigned_integer;
    case cbor::type::negative_integer:
        return cbor::major_type::negative_integer;
    case cbor::type::byte_string:
        return cbor::major_type::byte_string;
    case cbor::type::text_string:
        return cbor::major_type::text_string;
    case cbor::type::array:
        return cbor::major_type::array;
    case cbor::type::map:
        return cbor::major_type::map;
    case cbor::type::tag:
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
    case cbor::type::typed_array:
        return cbor::major_type::tag;
    default:
        return cbor::major_type::simple_float;
    }
}
