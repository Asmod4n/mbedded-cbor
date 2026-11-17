#pragma once

#include <cbor/encode.hpp>
#include <cbor/item.hpp>
#include <cbor/rfc8949.hpp>
#include "owned_copy.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

using item_pair = std::pair<std::uint64_t, cbor::item const *>;
using item_pairs = std::ranges::stride_view<std::ranges::adjacent_view<std::span<cbor::item const>, 2>>;

// The argument of the head that cbor::encode writes for the item. A float has no value as an argument, and the argument
// of a negative integer can be larger than an int64_t holds, so both are read back from the encoding.
inline std::uint64_t read_argument_from_encoding(cbor::item const &node)
{
    std::string head;
    REQUIRE(cbor::encode(head, node).has_value());
    std::uint8_t const info = static_cast<std::uint8_t>(head.front()) & 0x1f;
    if (info < 24)
        return info;
    std::uint64_t bits = 0;
    for (char const c : std::string_view(head).substr(1))
        bits = bits << 8 | static_cast<std::uint8_t>(c);
    return bits;
}

// The argument of the head of an item, as cbor::encode writes it. So that a test compares an item with the bytes of its
// head without a case for each type.
inline std::uint64_t read_argument_of_item(cbor::item const &node)
{
    switch (node.type()) {
    case cbor::type::byte_string:
        return node.get<std::span<std::byte const>>().value().size();
    case cbor::type::text_string:
        return node.get<std::string_view>().value().size();
    case cbor::type::array:
        return node.get<std::span<cbor::item const>>().value().size();
    case cbor::type::map:
        return node.get<item_pairs>().value().size();
    case cbor::type::tag:
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
    case cbor::type::shared_reference:
        return node.get<item_pair>().value().first;
    case cbor::type::typed_array:
        return node.get<cbor::typed_array>().value().tag;
    case cbor::type::false_value:
    case cbor::type::true_value:
    case cbor::type::null:
    case cbor::type::undefined:
    case cbor::type::simple_value:
        return std::to_underlying(node.get<cbor::simple_value>().value());
    case cbor::type::half_float:
    case cbor::type::single_float:
    case cbor::type::double_float:
    case cbor::type::negative_integer:
        return read_argument_from_encoding(node);
    case cbor::type::unsigned_integer:
        return node.get<std::uint64_t>().value();
    }
    std::unreachable();
}

// The tag content of an item of a tag type whose content is an item. So that a test reads the content of a tag whose
// type comes from the wire. A typed array gives its bytes instead, through get<cbor::typed_array>.
inline cbor::item const *read_tag_content_of_item(cbor::item const &node)
{
    auto const tagged = node.get<item_pair>();
    if (!tagged) {
        FAIL("the item is no tag");
        return nullptr;
    }
    return tagged->second;
}

// The children of an array or a map as pointers, a key and its value one after the other, so that a test reads each
// child as it read it before.
inline std::vector<cbor::item const *> list_children_of_item(cbor::item const &container)
{
    auto const array = container.get<std::span<cbor::item const>>();
    auto const map = container.get<item_pairs>();
    REQUIRE((array || map));
    std::vector<cbor::item const *> children;
    if (array)
        for (cbor::item const &child : *array)
            children.push_back(&child);
    else
        for (auto const pair : *map) {
            children.push_back(&std::get<0>(pair));
            children.push_back(&std::get<1>(pair));
        }
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

inline std::string_view read_digits_of_bignum(cbor::item const &n)
{
    std::span<std::byte const> const magnitude = n.get<item_pair>().value().second->get<std::span<std::byte const>>().value();
    std::string_view const s(reinterpret_cast<char const *>(magnitude.data()), magnitude.size());
    return s.substr(std::min(s.find_first_not_of('\0'), s.size()));
}

inline bool tag_values_are_equal(cbor::item const &a, cbor::item const &b)
{
    item_pair const x = a.get<item_pair>().value();
    item_pair const y = b.get<item_pair>().value();
    return x.first == y.first && item_values_are_equal(*x.second, *y.second);
}

inline bool item_values_are_equal(cbor::item const &a, cbor::item const &b)
{
    if (a.type() != b.type())
        return false;
    switch (a.type()) {
    case cbor::type::byte_string:
        return std::ranges::equal(a.get<std::span<std::byte const>>().value(), b.get<std::span<std::byte const>>().value());
    case cbor::type::text_string:
        return a.get<std::string_view>().value() == b.get<std::string_view>().value();
    case cbor::type::array:
    case cbor::type::map: {
        auto const x = list_children_of_item(a);
        auto const y = list_children_of_item(b);
        return std::ranges::equal(x, y, [](cbor::item const *l, cbor::item const *r) { return item_values_are_equal(*l, *r); });
    }
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
        return read_digits_of_bignum(a) == read_digits_of_bignum(b);
    case cbor::type::tag:
        return tag_values_are_equal(a, b);
    case cbor::type::typed_array: {
        cbor::typed_array const x = a.get<cbor::typed_array>().value();
        cbor::typed_array const y = b.get<cbor::typed_array>().value();
        return x.tag == y.tag && std::ranges::equal(x.bytes, y.bytes);
    }
    case cbor::type::shared_reference:
        return a.get<item_pair>().value().first == b.get<item_pair>().value().first;
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
