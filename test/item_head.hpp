#pragma once

#include <cbor/cbor.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
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

// RFC 8949 2 defines the data model. Two items are the same value when their types and values agree; a float
// is compared by its width and its bits, so a NaN keeps its payload; a bignum is compared by its magnitude
// without leading zeros (RFC 8949 3.4.3). The tests compare encode(item) with its input by this, and not by the
// bytes, because the encoder writes its own form.
inline bool item_values_equal(cbor::item const &a, cbor::item const &b)
{
    if (a.type != b.type)
        return false;
    switch (a.type) {
    case cbor::type::byte_string:
        return std::ranges::equal(a.via.bin, b.via.bin);
    case cbor::type::text_string:
        return a.via.str == b.via.str;
    case cbor::type::array:
    case cbor::type::map: {
        auto const x = children_of(a);
        auto const y = children_of(b);
        return std::ranges::equal(x, y, [](cbor::item const *l, cbor::item const *r) { return item_values_equal(*l, *r); });
    }
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum: {
        auto const digits = [](cbor::item const &n) {
            std::string_view const s(reinterpret_cast<char const *>(n.via.tag.content->via.bin.data()),
                                     n.via.tag.content->via.bin.size());
            return s.substr(std::min(s.find_first_not_of('\0'), s.size()));
        };
        return digits(a) == digits(b);
    }
    case cbor::type::tag:
    case cbor::type::typed_array:
        return a.via.tag.number == b.via.tag.number && item_values_equal(*a.via.tag.content, *b.via.tag.content);
    default:
        return argument_of(a) == argument_of(b);
    }
}

// Decodes both byte strings into trees and compares their values.
inline bool same_value(std::string_view const a, std::string_view const b)
{
    auto const x = cbor::lazy::copy(std::string(a));
    auto const y = cbor::lazy::copy(std::string(b));
    REQUIRE(x.has_value());
    REQUIRE(y.has_value());
    auto const p = x->decode();
    auto const q = y->decode();
    REQUIRE(p.has_value());
    REQUIRE(q.has_value());
    return item_values_equal(**p, **q);
}
