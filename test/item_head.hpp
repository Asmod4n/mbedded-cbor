#pragma once

#include <cbor/encode.hpp>
#include <cbor/item.hpp>
#include <cbor/rfc8949.hpp>
#include "owned_copy.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

// The argument of the head of an item, as cbor::encode writes it; a float is read back from its encoding. So that a test
// compares an item with the bytes of its head without a case for each type.
inline std::uint64_t read_argument_of_item(cbor::item const &node)
{
    switch (node.type) {
    case cbor::type::byte_string:
        return node.get<cbor::type::byte_string>().value().size();
    case cbor::type::text_string:
        return node.get<cbor::type::text_string>().value().size();
    case cbor::type::array:
        return node.get<cbor::type::array>().value().size();
    case cbor::type::map:
        return node.get<cbor::type::map>().value().size() / 2;
    case cbor::type::tag:
        return node.get<cbor::type::tag>().value().first;
    case cbor::type::unsigned_bignum:
        return node.get<cbor::type::unsigned_bignum>().value().first;
    case cbor::type::negative_bignum:
        return node.get<cbor::type::negative_bignum>().value().first;
    case cbor::type::typed_array:
        return node.get<cbor::type::typed_array>().value().first;
    case cbor::type::shared_reference:
        return node.get<cbor::type::shared_reference>().value().first;
    case cbor::type::false_value:
        return std::to_underlying(node.get<cbor::type::false_value>().value());
    case cbor::type::true_value:
        return std::to_underlying(node.get<cbor::type::true_value>().value());
    case cbor::type::null:
        return std::to_underlying(node.get<cbor::type::null>().value());
    case cbor::type::undefined:
        return std::to_underlying(node.get<cbor::type::undefined>().value());
    case cbor::type::simple_value:
        return std::to_underlying(node.get<cbor::type::simple_value>().value());
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
    case cbor::type::unsigned_integer:
        return node.get<cbor::type::unsigned_integer>().value();
    case cbor::type::negative_integer:
        return node.get<cbor::type::negative_integer>().value();
    }
    std::unreachable();
}

// The tag content of an item of any of the four tag types. So that a test reads the content of a tag whose type comes
// from the wire.
inline cbor::item const *read_tag_content_of_item(cbor::item const &node)
{
    switch (node.type) {
    case cbor::type::tag:
        return node.get<cbor::type::tag>().value().second;
    case cbor::type::unsigned_bignum:
        return node.get<cbor::type::unsigned_bignum>().value().second;
    case cbor::type::negative_bignum:
        return node.get<cbor::type::negative_bignum>().value().second;
    case cbor::type::typed_array:
        return node.get<cbor::type::typed_array>().value().second;
    case cbor::type::shared_reference:
        return node.get<cbor::type::shared_reference>().value().second;
    default:
        FAIL("the item is no tag");
        return nullptr;
    }
}

// The children of an array or a map as pointers, so that a test reads each child as it read it before.
inline std::vector<cbor::item const *> list_children_of_item(cbor::item const &container)
{
    auto const array = container.get<cbor::type::array>();
    auto const map = container.get<cbor::type::map>();
    REQUIRE((array || map));
    std::vector<cbor::item const *> children;
    for (cbor::item const &child : array ? *array : *map)
        children.push_back(&child);
    return children;
}


// RFC 8949 3.1: the major type of each member of cbor::type. Tags 2, 3 and the typed arrays of RFC 8746 are major type
// 6. False, true, null, undefined, the other simple values and the floats are major type 7.
inline cbor::major_type find_major_type_of(cbor::type const t)
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
    case cbor::type::shared_reference:
        return cbor::major_type::tag;
    default:
        return cbor::major_type::simple;
    }
}

// RFC 8949 2 defines the data model. Two items are the same value when their types and values agree; a float is compared
// by its width and its bits. So a NaN keeps its payload; a bignum is compared by its magnitude without leading zeros
// (RFC 8949 3.4.3). The tests compare encode(item) with its input by this, and not by the bytes, because the encoder
// writes its own form.
inline bool item_values_are_equal(cbor::item const &a, cbor::item const &b);

template <cbor::type K>
std::string_view read_digits_of_bignum(cbor::item const &n)
{
    std::span<std::byte const> const magnitude = n.get<K>().value().second->template get<cbor::type::byte_string>().value();
    std::string_view const s(reinterpret_cast<char const *>(magnitude.data()), magnitude.size());
    return s.substr(std::min(s.find_first_not_of('\0'), s.size()));
}

template <cbor::type K>
bool tag_values_are_equal(cbor::item const &a, cbor::item const &b)
{
    std::pair<std::uint64_t, cbor::item const *> const x = a.get<K>().value();
    std::pair<std::uint64_t, cbor::item const *> const y = b.get<K>().value();
    return x.first == y.first && item_values_are_equal(*x.second, *y.second);
}

inline bool item_values_are_equal(cbor::item const &a, cbor::item const &b)
{
    if (a.type != b.type)
        return false;
    switch (a.type) {
    case cbor::type::byte_string:
        return std::ranges::equal(a.get<cbor::type::byte_string>().value(), b.get<cbor::type::byte_string>().value());
    case cbor::type::text_string:
        return a.get<cbor::type::text_string>().value() == b.get<cbor::type::text_string>().value();
    case cbor::type::array:
    case cbor::type::map: {
        auto const x = list_children_of_item(a);
        auto const y = list_children_of_item(b);
        return std::ranges::equal(x, y, [](cbor::item const *l, cbor::item const *r) { return item_values_are_equal(*l, *r); });
    }
    case cbor::type::unsigned_bignum:
        return read_digits_of_bignum<cbor::type::unsigned_bignum>(a) == read_digits_of_bignum<cbor::type::unsigned_bignum>(b);
    case cbor::type::negative_bignum:
        return read_digits_of_bignum<cbor::type::negative_bignum>(a) == read_digits_of_bignum<cbor::type::negative_bignum>(b);
    case cbor::type::tag:
        return tag_values_are_equal<cbor::type::tag>(a, b);
    case cbor::type::typed_array:
        return tag_values_are_equal<cbor::type::typed_array>(a, b);
    case cbor::type::shared_reference:
        return a.get<cbor::type::shared_reference>().value().first == b.get<cbor::type::shared_reference>().value().first;
    default:
        return read_argument_of_item(a) == read_argument_of_item(b);
    }
}

// Decodes both byte strings into trees and compares their values.
inline bool same_value(std::string_view const a, std::string_view const b)
{
    auto const x = test::make_lazy_from_copy(std::string(a));
    auto const y = test::make_lazy_from_copy(std::string(b));
    REQUIRE(x.has_value());
    REQUIRE(y.has_value());
    auto const p = x->to_item();
    auto const q = y->to_item();
    REQUIRE(p.has_value());
    REQUIRE(q.has_value());
    return item_values_are_equal(**p, **q);
}
