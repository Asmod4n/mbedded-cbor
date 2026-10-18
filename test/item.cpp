#include <cbor/cbor.hpp>
#include "owned_copy.hpp"
#include <doctest/doctest.h>

#include "item_head.hpp"
#include "limits_guard.hpp"

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

namespace item_test
{

using namespace std::string_literals;
using namespace std::string_view_literals;

// RFC 8949 section 3: every data item starts with a head of major type,
// additional information and argument. An integer is its type, its
// additional information and its argument, so an item of major type 1
// gives -1 - argument from the fields.
TEST_CASE("an integer is the head fields and nothing else")
{
    auto const tree = cbor::decode("\x39\x01\xf3"sv);
    REQUIRE(tree.has_value());
    cbor::item const &minus_500 = **tree;
    CHECK(minus_500.type == cbor::type::negative_integer);
    CHECK(minus_500.additional_information == 25);
    CHECK(-1 - static_cast<std::int64_t>(minus_500.get<cbor::type::negative_integer>().value()) == -500);
}

// RFC 8949 3.4: a tag is major type 6 with the tag number as argument.
// The content points to the item of the tag content, so a reader goes
// from the tag to its content by the address.
TEST_CASE("a tag holds its number and points to its content")
{
    auto const tree = cbor::decode("\xc1\x1a\x51\x4b\x67\xb0"sv);
    REQUIRE(tree.has_value());
    cbor::item const &time = **tree;
    CHECK(time.get<cbor::type::tag>().value().first == 1);
    cbor::item const &epoch = *time.get<cbor::type::tag>().value().second;
    CHECK(epoch.additional_information == 26);
    CHECK(epoch.get<cbor::type::unsigned_integer>().value() == 1363896240u);
}

// RFC 8949 3.3 Table 3: major type 7 says by its additional information
// what the item is. A float keeps the width it had on the wire in the
// additional information and in its type, and its value widened to a double,
// which holds every value of the narrower widths.
TEST_CASE("a float keeps its width in the additional information and its value as a double")
{
    auto const tree = cbor::decode("\x83\xf9\x3e\x00\xfa\x47\xc3\x50\x00\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"sv);
    REQUIRE(tree.has_value());
    auto const floats = (*tree)->get<cbor::type::array>().value();
    cbor::item const &f16 = floats[0];
    cbor::item const &f32 = floats[1];
    cbor::item const &f64 = floats[2];
    CHECK(f16.additional_information == 25);
    CHECK(f16.get<cbor::type::half_float>().value() == 1.5);
    CHECK(f32.additional_information == 26);
    CHECK(f32.get<cbor::type::single_float>().value() == 100000.0);
    CHECK(f64.additional_information == 27);
    CHECK(f64.get<cbor::type::double_float>().value() == 1.1);
}

// undefined is a value of its own, apart from null, so that a message
// from JavaScript passes through unchanged.
TEST_CASE("null and undefined are two items")
{
    auto const tree = cbor::decode("\x82\xf6\xf7"sv);
    REQUIRE(tree.has_value());
    auto const items = (*tree)->get<cbor::type::array>().value();
    cbor::item const &null = items[0];
    cbor::item const &undefined = items[1];
    CHECK(null.type != undefined.type);
    CHECK(null.additional_information != undefined.additional_information);
}

// An item owns no memory, so a cache of many items costs only their own
// size. Each member of the union is an immediate value or a view, and the
// item takes 24 bytes: one byte of type, one of additional information,
// and a union of 16 bytes. It is no aggregate, because only a decode
// builds one.
TEST_CASE("an item owns no memory")
{
    CHECK(std::is_trivially_destructible_v<cbor::item>);
    CHECK(std::is_trivially_copyable_v<cbor::item>);
    CHECK_FALSE(std::is_aggregate_v<cbor::item>);
    CHECK_EQ(sizeof(cbor::item), 24u);
}

cbor::owning<cbor::item> item_of(std::expected<cbor::owning<cbor::item>, cbor::error> const &r)
{
    REQUIRE(r.has_value());
    return *r;
}

// Each decode returns items that the caller owns, and no lazy keeps a
// cache that a second call or a second thread would write. Two decodes
// of one node give two items with the same content. A decode copies
// each text string into its own tree (owner decision: eager copies), so
// the two texts lie in two places.
TEST_CASE("decode: a node decoded twice gives two items that the caller owns")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\x01\x62hi"sv));
    REQUIRE(top_level.has_value());
    auto const first = top_level->clone();
    auto const second = top_level->clone();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK_NE(&**first, &**second);
    CHECK_EQ(argument_of(*(*first)), argument_of(*(*second)));
    auto const a = children_of(**first);
    auto const b = children_of(**second);
    REQUIRE_EQ(a.size(), 2u);
    REQUIRE_EQ(b.size(), 2u);
    CHECK_NE(a[1], b[1]);
    CHECK_EQ(a[1]->get<cbor::type::text_string>().value(), b[1]->get<cbor::type::text_string>().value());
    CHECK_NE(a[1]->get<cbor::type::text_string>().value().data(), b[1]->get<cbor::type::text_string>().value().data());
}

// Owner decision: cbor::decode builds a tree that points into nothing
// outside itself. The input is freed before the tree is read; the
// address sanitizer build finds any read of the freed input.
TEST_CASE("decode: the tree stays valid after the input is gone")
{
    auto input = std::make_unique<std::string>("\x83\x62hi\x42\x01\x02\xd8\x18\x42\x61\x62"s);
    auto const tree = cbor::decode(*input);
    input.reset();
    REQUIRE(tree.has_value());
    auto const children = children_of(**tree);
    REQUIRE_EQ(children.size(), 3u);
    CHECK_EQ(children[0]->get<cbor::type::text_string>().value(), "hi"sv);
    CHECK_EQ(children[1]->get<cbor::type::byte_string>().value().size(), 2u);
    CHECK_EQ(children[1]->get<cbor::type::byte_string>().value()[1], std::byte{2});
    auto const embedded = cbor::encoded_cbor_data_item(*tree, *children[2]);
    REQUIRE(embedded.has_value());
    CHECK_EQ(embedded->get().get<cbor::type::text_string>().value(), "b"sv);
}

// RFC 8949 3.1: the argument of a string head is its length in bytes. A
// length that the bytes left cannot hold is too_little_data before any
// memory is taken for the copy (rule: a count in the input reserves
// nothing).
TEST_CASE("decode: a string length larger than the input is too_little_data")
{
    for (std::string const &wire : {"\x7b\x80\x00\x00\x00\x00\x00\x00\x00"s,
                                   "\x5b\x7f\xff\xff\xff\xff\xff\xff\xff\x00"s, "\x65"s + "abc"}) {
        CAPTURE(wire);
        CHECK_EQ(cbor::decode(wire).error(), cbor::error::too_little_data);
    }
}

// cbor::decode takes exactly one data item (RFC 8949 5.3.1). Bytes after
// it are a syntax error.
TEST_CASE("decode: bytes after the data item are a syntax error")
{
    CHECK_EQ(cbor::decode("\x01\x02"s).error(), cbor::error::syntax_error);
}

// The fault: decode on the lazy inside a temporary expected returned a
// reference into the top-level item that the temporary freed at the end
// of the statement. The result now owns its items and the top-level
// item, so it stays valid after the lazy is gone.
TEST_CASE("decode: the item outlives the lazy that decoded it")
{
    std::string const s("\x83\x01\x02\x03"sv);
    auto const r = test::lazy_of_copy(s)->clone();
    REQUIRE(r.has_value());
    CHECK_EQ((*r)->type, cbor::type::array);
    CHECK_EQ(argument_of(*(*r)), 3u);
    auto const elements = children_of(**r);
    REQUIRE_EQ(elements.size(), 3u);
    CHECK_EQ(argument_of(*elements[2]), 3u);
}

// A child reached through two paths decodes to the same content.
TEST_CASE("decode: a child decoded through two paths has one content")
{
    auto const top_level = test::lazy_of_copy(std::string("\xa1\x61k\x82\x01\x02"sv));
    REQUIRE(top_level.has_value());
    auto const map = item_of(top_level->clone());
    CHECK_EQ(argument_of(*map), 1u);
    auto const entries = top_level->entries();
    REQUIRE(entries.has_value());
    auto const entry = *entries->begin();
    REQUIRE(entry.has_value());
    auto const value = top_level->at("k");
    REQUIRE(value.has_value());
    auto const by_key = item_of(value->clone());
    auto const by_entry = item_of(entry->second.clone());
    CHECK_EQ(argument_of(*by_key), argument_of(*by_entry));
    REQUIRE_EQ(children_of(*by_key).size(), 2u);
    REQUIRE_EQ(children_of(*by_entry).size(), 2u);
    CHECK_EQ(argument_of(*children_of(*by_key)[1]), argument_of(*children_of(*by_entry)[1]));
}

// RFC 8949 3.1: an array is its count and its data items. A map holds each pair as key and value, one after
// the other, so a map of n pairs shows 2n items in the order of the wire.
TEST_CASE("decode: an array and a map hold their data items in the order of the wire")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\x01\xa2\x61k\x62hi\x02\x80"sv));
    REQUIRE(top_level.has_value());
    auto const array = item_of(top_level->clone());
    auto const elements = children_of(*array);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(argument_of(*elements[0]), 1u);
    CHECK_EQ(elements[1]->type, cbor::type::map);
    auto const pairs = children_of(*elements[1]);
    REQUIRE_EQ(pairs.size(), 4u);
    CHECK_EQ(pairs[0]->get<cbor::type::text_string>().value(), "k");
    CHECK_EQ(pairs[1]->get<cbor::type::text_string>().value(), "hi");
    CHECK_EQ(argument_of(*pairs[2]), 2u);
    CHECK_EQ(pairs[3]->type, cbor::type::array);
    CHECK(children_of(*pairs[3]).empty());
}

// The children of every container stand in one vector, and each container shows its own part. Nested
// containers and siblings each see only their own children, in the order of the wire.
TEST_CASE("decode: nested arrays and maps each show their own children")
{
    // [[1, [2, 3]], {4: [5]}, [], 6]
    auto const top_level = test::lazy_of_copy(std::string("\x84\x82\x01\x82\x02\x03\xa1\x04\x81\x05\x80\x06"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const outer = children_of(*root);
    REQUIRE_EQ(outer.size(), 4u);
    auto const first = children_of(*outer[0]);
    REQUIRE_EQ(first.size(), 2u);
    CHECK_EQ(argument_of(*first[0]), 1u);
    auto const inner = children_of(*first[1]);
    REQUIRE_EQ(inner.size(), 2u);
    CHECK_EQ(argument_of(*inner[0]), 2u);
    CHECK_EQ(argument_of(*inner[1]), 3u);
    auto const pairs = children_of(*outer[1]);
    REQUIRE_EQ(pairs.size(), 2u);
    CHECK_EQ(argument_of(*pairs[0]), 4u);
    auto const value = children_of(*pairs[1]);
    REQUIRE_EQ(value.size(), 1u);
    CHECK_EQ(argument_of(*value[0]), 5u);
    CHECK(children_of(*outer[2]).empty());
    CHECK_EQ(argument_of(*outer[3]), 6u);
}

// A count in the input reserves nothing: an array that claims 2^63 elements with one byte after its head is
// refused before any element is read, and the decode allocates in proportion to the input only.
TEST_CASE("decode: an array that claims more elements than bytes is refused")
{
    auto const top_level = test::lazy_of_copy(std::string("\x9b\x80\x00\x00\x00\x00\x00\x00\x00\x01"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->clone();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::too_little_data);
    auto const map = test::lazy_of_copy(std::string("\xbb\x40\x00\x00\x00\x00\x00\x00\x00\x01"sv));
    REQUIRE(map.has_value());
    auto const m = map->clone();
    REQUIRE_FALSE(m.has_value());
    CHECK_EQ(m.error(), cbor::error::too_little_data);
}

// The check of the count admits a count equal to the bytes left: five empty arrays in five bytes are five
// children, each with no children of its own.
TEST_CASE("decode: a count equal to the bytes left of empty children is read")
{
    auto const top_level = test::lazy_of_copy(std::string("\x85\x80\x80\x80\x80\x80"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const elements = children_of(*root);
    REQUIRE_EQ(elements.size(), 5u);
    for (cbor::item const *const e : elements)
        CHECK(children_of(*e).empty());
}

// Many sibling containers make the one vector of children grow many times while earlier containers already
// show their part. A container shows its part through an index into the vector, so the growth leaves every
// earlier view valid; AddressSanitizer reports a read of freed memory otherwise.
TEST_CASE("decode: many sibling containers keep their children through the growth of the vector")
{
    constexpr std::size_t siblings = 4096;
    std::string wire = "\x99\x10\x00"s;
    for (std::size_t i = 0; i < siblings; ++i)
        wire += "\x82\x01\x02"sv;
    auto const top_level = cbor::lazy::take_ownership(std::move(wire));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const elements = children_of(*root);
    REQUIRE_EQ(elements.size(), siblings);
    std::size_t sum = 0;
    for (cbor::item const *const e : elements)
        for (cbor::item const *const c : children_of(*e))
            sum += argument_of(*c);
    CHECK_EQ(sum, siblings * 3);
}

// RFC 8949 3.4.3 and 3.4.4: a bignum and a typed array are tags over a byte string. The tree keeps each as a
// tag item whose content is the byte string item.
TEST_CASE("decode: a bignum and a typed array are tags over a byte string")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00\xd8\x40\x42\x01\x02"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const elements = children_of(*root);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(elements[0]->type, cbor::type::unsigned_bignum);
    CHECK_EQ(argument_of(*elements[0]), 2u);
    auto const *const magnitude = elements[0]->get<cbor::type::unsigned_bignum>().value().second;
    CHECK_EQ(magnitude->get<cbor::type::byte_string>().value().size(), 9u);
    CHECK_EQ(argument_of(*elements[1]), 64u);
    auto const *const typed = elements[1]->get<cbor::type::typed_array>().value().second;
    CHECK_EQ(typed->get<cbor::type::byte_string>().value().size(), 2u);
}

// RFC 8949 5.6 lets a decoder that is not in a deterministic profile keep one entry of a repeated key. decode
// builds the item of such a map with no error, at the top and inside an array.
TEST_CASE("decode: a map with a repeated key is read with no error")
{
    auto const twice = test::lazy_of_copy(std::string("\xa2\x61k\x01\x61k\x02"sv));
    REQUIRE(twice.has_value());
    CHECK(twice->clone().has_value());
    auto const nested = test::lazy_of_copy(std::string("\x81\xa2\x01\x00\x18\x01\x00"sv));
    REQUIRE(nested.has_value());
    REQUIRE(nested->clone().has_value());
    auto const inner = nested->at(0);
    REQUIRE(inner.has_value());
    CHECK(inner->clone().has_value());
}

// decode compares no keys, so a map of 65536 text keys reads in one walk, with its last key equal to its first
// or not.
TEST_CASE("decode: a map of 65536 keys is read with a repeated key")
{
    constexpr std::size_t count = 65536;
    std::string map = "\xba\x00\x01\x00\x00"s;
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t const n = i + 1 == count ? 0 : i;
        map += '\x64';
        for (int shift = 24; shift >= 0; shift -= 8)
            map += static_cast<char>(n >> shift & 0xff);
        map += '\x00';
    }
    std::string distinct = map;
    distinct[distinct.size() - 2] = '\x01';
    distinct[distinct.size() - 3] = '\xff';
    distinct[distinct.size() - 4] = '\xff';
    auto const unique = cbor::lazy::take_ownership(std::move(distinct));
    REQUIRE(unique.has_value());
    CHECK(unique->clone().has_value());
    auto const twice = cbor::lazy::take_ownership(std::move(map));
    REQUIRE(twice.has_value());
    CHECK(twice->clone().has_value());
}

// A fuzzer held one core for more than 10 s with one map of 65535 integer keys, because integer keys were
// compared pair by pair. decode compares no keys, so the map reads in one walk with a repeated key or not.
TEST_CASE("decode: a map of 65535 integer keys is read with a repeated key")
{
    constexpr std::size_t count = 65535;
    std::string map = "\xb9\xff\xff"s;
    for (std::size_t i = 0; i < count; ++i) {
        map += '\x19';
        map += static_cast<char>(i >> 8 & 0xff);
        map += static_cast<char>(i & 0xff);
        map += '\x00';
    }
    std::string negative = map;
    negative[3] = '\x39';
    auto const unique = cbor::lazy::take_ownership(std::move(negative));
    REQUIRE(unique.has_value());
    CHECK(unique->clone().has_value());
    std::string longer = map;
    longer.replace(map.size() - 4, 4, "\x1a\x00\x00\x00\x00\x00"sv);
    auto const twice = cbor::lazy::take_ownership(std::move(longer));
    REQUIRE(twice.has_value());
    CHECK(twice->clone().has_value());
}

// The result owns what it shows, so decode on a temporary lazy is safe
// and compiles.
TEST_CASE("decode: a temporary lazy gives an item")
{
    auto const r = std::move(*test::lazy_of_copy(std::string("\x01"sv))).clone();
    REQUIRE(r.has_value());
    CHECK_EQ((*r)->type, cbor::type::unsigned_integer);
    CHECK_EQ(argument_of(*(*r)), 1u);
}

// The order does not matter: a child decoded first and decoded again
// after its parent has one content.
TEST_CASE("decode: a child decoded before its parent has the same content after it")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\x01\x82\x02\x03"sv));
    REQUIRE(top_level.has_value());
    auto const child = top_level->at(1);
    REQUIRE(child.has_value());
    auto const inner = item_of(child->clone());
    REQUIRE(top_level->clone().has_value());
    auto const again = top_level->at(1);
    REQUIRE(again.has_value());
    auto const later = item_of(again->clone());
    CHECK_EQ(argument_of(*later), argument_of(*inner));
    CHECK_EQ(argument_of(*children_of(*later)[1]), argument_of(*children_of(*inner)[1]));
}

// The owner decided that a tree decode reads tags 28 and 29 as plain tags: 28 over the shared value and 29 over
// its index. Nothing is copied and nothing is followed.
TEST_CASE("decode: a shared reference points to the item of the shared value")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\xd8\x1c\x62hi\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const elements = children_of(*root);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(elements[0]->type, cbor::type::tag);
    CHECK_EQ(argument_of(*elements[0]), 28u);
    auto const *const value = elements[0]->get<cbor::type::tag>().value().second;
    CHECK_EQ(value->get<cbor::type::text_string>().value(), "hi");
    CHECK_EQ(argument_of(*elements[1]), 29u);
    CHECK_EQ(elements[1]->get<cbor::type::tag>().value().first, 29u);
    CHECK_EQ(elements[1]->get<cbor::type::tag>().value().second->get<cbor::type::unsigned_integer>(), 0u);
}

// A tag 29 inside a lazy is a tag over its index, also when its mark lies before the decoded node.
TEST_CASE("decode: a tag 29 after its mark is a tag over its index")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\xd8\x1c\x62hi\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const element = top_level->at(1);
    REQUIRE(element.has_value());
    auto const inner = item_of(element->clone());
    cbor::item const &reference = *children_of(*inner)[0];
    CHECK_EQ(reference.get<cbor::type::tag>().value().first, 29u);
    CHECK_EQ(reference.get<cbor::type::tag>().value().second->get<cbor::type::unsigned_integer>().value(), 0u);
}
// A cycle comes from the wire: 28([29(0)]) is an array that holds
// itself. The element points to that array, and the decode ends.
TEST_CASE("decode: a cyclic array is its own element")
{
    auto const top_level = test::lazy_of_copy(std::string("\xd8\x1c\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const *const array = root->get<cbor::type::tag>().value().second;
    CHECK_EQ(array->type, cbor::type::array);
    auto const elements = children_of(*array);
    REQUIRE_EQ(elements.size(), 1u);
    CHECK_EQ(elements[0]->get<cbor::type::tag>().value().first, 29u);
}

// A tag 29 before its mark is a plain tag: the tree decode checks no index.
TEST_CASE("decode: a forward tag 29 is a plain tag")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\xd8\x1d\x00\xd8\x1c\x00"sv));
    REQUIRE(top_level.has_value());
    auto const whole = item_of(top_level->clone());
    CHECK_EQ(whole->get<cbor::type::array>().value()[0].get<cbor::type::tag>().value().first, 29u);
    auto const element = top_level->at(0);
    REQUIRE(element.has_value());
    auto const inner = item_of(element->clone());
    CHECK_EQ(inner->get<cbor::type::tag>().value().first, 29u);
}
// Fault 21: in [28(0), 28(29(0)), 28(29(1)), ...] each element refers to the
// one before it. The decode of the last element walked the chain once for
// each step and recursed once for each step at the same depth, so a long chain
// took time in the square of its length and overflowed the stack. The decode
// now only stores the tag 29. The mark lies before the decoded element, so
// shared_value_resolve gives it as a lazy, and the lazy follows the chain in
// one loop to the integer at its start.
TEST_CASE("decode: a long chain of shared references is read without recursion")
{
    constexpr std::uint16_t chain_length = 50000;
    constexpr char array_head_uint16 = '\x99';
    constexpr char uint16_head = '\x19';
    constexpr std::string_view shareable = "\xd8\x1c";
    constexpr std::string_view sharedref = "\xd8\x1d";
    auto const uint16_of = [](std::uint16_t const n) {
        return std::string{static_cast<char>(n >> CHAR_BIT),
                           static_cast<char>(n & std::numeric_limits<unsigned char>::max())};
    };
    std::string document = array_head_uint16 + uint16_of(chain_length);
    document += shareable;
    document += '\x00';
    for (std::uint16_t i = 1; i < chain_length; ++i) {
        document += shareable;
        document += sharedref;
        document += uint16_head + uint16_of(static_cast<std::uint16_t>(i - 1));
    }
    auto const top_level = cbor::lazy::take_ownership(std::move(document));
    REQUIRE(top_level.has_value());
    auto const last = top_level->at(chain_length - 1);
    REQUIRE(last.has_value());
    auto const decoded = item_of(last->clone());
    auto const reference = decoded->get<cbor::type::tag>().value().second->get<cbor::type::tag>();
    REQUIRE(reference.has_value());
    CHECK_EQ(reference->first, 29u);
    CHECK_EQ(reference->second->get<cbor::type::unsigned_integer>().value(), chain_length - 2u);
}

// The nesting depth is checked where the decode recurses: 100(100(100(0))) holds an
// integer at depth 3, and so does [[[0]]].
TEST_CASE("decode: the nesting depth is limited by the nesting depth in force")
{
    auto const top_level = test::lazy_of_copy(std::string("\xd8\x64\xd8\x64\xd8\x64\x00"sv));
    REQUIRE(top_level.has_value());
    {
        test::limits_guard const depth{{.nesting_depth = 2}};
        auto const refused = top_level->clone();
        REQUIRE_FALSE(refused.has_value());
        CHECK_EQ(refused.error(), cbor::error::nesting_depth_exceeded);
    }
    auto const arrays = test::lazy_of_copy(std::string("\x81\x81\x81\x00"sv));
    REQUIRE(arrays.has_value());
    {
        test::limits_guard const depth{{.nesting_depth = 2}};
        auto const refused = arrays->clone();
        REQUIRE_FALSE(refused.has_value());
        CHECK_EQ(refused.error(), cbor::error::nesting_depth_exceeded);
    }
    test::limits_guard const depth{{.nesting_depth = 3}};
    CHECK(top_level->clone().has_value());
    CHECK(arrays->clone().has_value());
}

// The decode recurses once for each level of nesting. At the hard bound of the nesting depth, an array in an
// array 512 times and a tag on a tag 512 times decode with no overflow of the stack, in a debug build with
// AddressSanitizer too.
TEST_CASE("decode: the hard bound of the nesting depth fits the stack")
{
    constexpr std::size_t bound = 512;
    test::limits_guard const depth{{.nesting_depth = bound}};
    auto const arrays = test::lazy_of_copy(std::string(bound, '\x81') + '\x00');
    REQUIRE(arrays.has_value());
    CHECK(arrays->clone().has_value());
    std::string tags;
    for (std::size_t i = 0; i < bound; ++i)
        tags += "\xd8\x64"sv;
    tags += '\x00';
    auto const tagged = cbor::lazy::take_ownership(std::move(tags));
    REQUIRE(tagged.has_value());
    CHECK(tagged->clone().has_value());
    // [28([[...[0]...]]), [[...[29(0)]...]]]: a lazy at the second element meets the reference near the bound
    // and reads the mark at the depth of its own position, so the stack holds the frames of both nests.
    std::string const shared = "\x82\xd8\x1c"s + std::string(bound - 2, '\x81') + '\x00' +
                               std::string(bound - 3, '\x81') + "\xd8\x1d\x00"s;
    auto const whole = test::lazy_of_copy(shared);
    REQUIRE(whole.has_value());
    CHECK(whole->clone().has_value());
    auto const second = whole->at(1);
    REQUIRE(second.has_value());
    CHECK(second->clone().has_value());
}

// Indefinite length is never read; a streaming reader is the place for it.
TEST_CASE("decode: indefinite length is refused")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\x01\x9f\xff"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->clone();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::indefinite_length);
}

// RFC 8949 3.3: a float16 keeps its width in the additional information,
// so it is written back in the width it came in.
TEST_CASE("decode: a float16 keeps additional information 25")
{
    auto const top_level = test::lazy_of_copy(std::string("\xf9\x3e\x00"sv));
    REQUIRE(top_level.has_value());
    auto const f = item_of(top_level->clone());
    CHECK_EQ(f->type, cbor::type::half_float);
    CHECK_EQ(f->additional_information, 25);
    CHECK_EQ(f->get<cbor::type::half_float>().value(), 1.5);
    std::string again;
    REQUIRE(cbor::encode(*f, again).has_value());
    CHECK_EQ(again, "\xf9\x3e\x00"sv);
}

}

namespace item_test
{

// Every value of cbor::type, each from the shortest item that has it. The expectation is written from RFC 8949
// 3.1 and 3.3 (major types, simple values 20 to 23, floats of additional information 25 to 27), 3.4.3 (tags 2
// and 3) and RFC 8746 2 (tags 64 to 87 without the reserved tag 76). lazy and item give the same answer.
TEST_CASE("type: every type of an item, through lazy and through the decoded item")
{
    struct row {
        std::string_view wire;
        cbor::type expected;
    };
    std::array const rows{
        row{"\x00"sv, cbor::type::unsigned_integer},
        row{"\x20"sv, cbor::type::negative_integer},
        row{"\x40"sv, cbor::type::byte_string},
        row{"\x60"sv, cbor::type::text_string},
        row{"\x80"sv, cbor::type::array},
        row{"\xa0"sv, cbor::type::map},
        row{"\xc1\x00"sv, cbor::type::tag},
        row{"\xd8\x4c\x40"sv, cbor::type::tag},
        row{"\xf4"sv, cbor::type::false_value},
        row{"\xf5"sv, cbor::type::true_value},
        row{"\xf6"sv, cbor::type::null},
        row{"\xf7"sv, cbor::type::undefined},
        row{"\xf0"sv, cbor::type::simple_value},
        row{"\xf8\x20"sv, cbor::type::simple_value},
        row{"\xf9\x00\x00"sv, cbor::type::half_float},
        row{"\xfa\x00\x00\x00\x00"sv, cbor::type::single_float},
        row{"\xfb\x00\x00\x00\x00\x00\x00\x00\x00"sv, cbor::type::double_float},
        row{"\xc2\x40"sv, cbor::type::unsigned_integer},
        row{"\xc3\x40"sv, cbor::type::negative_integer},
        row{"\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv, cbor::type::unsigned_bignum},
        row{"\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv, cbor::type::negative_bignum},
        row{"\xd8\x40\x40"sv, cbor::type::typed_array},
        row{"\xd8\x57\x40"sv, cbor::type::typed_array},
    };
    for (row const &r : rows) {
        auto const l = test::lazy_of_copy(r.wire);
        REQUIRE(l.has_value());
        auto const by_lazy = l->type();
        REQUIRE(by_lazy.has_value());
        CHECK_EQ(*by_lazy, r.expected);
        auto const tree = item_of(l->clone());
        CHECK_EQ(tree->type, r.expected);
    }
}

// A lazy and the decoded item both read tags 28 and 29 as tags.
TEST_CASE("type: lazy and the item keep tags 28 and 29")
{
    auto const root = test::lazy_of_copy(std::string("\x82\xd8\x1c\x01\xd8\x1d\x00"sv));
    REQUIRE(root.has_value());
    CHECK_EQ(*root->at(0)->type(), cbor::type::tag);
    CHECK_EQ(*root->at(1)->type(), cbor::type::tag);
    auto const tree = item_of(root->clone());
    auto const elements = children_of(*tree);
    CHECK_EQ(elements[0]->type, cbor::type::tag);
    CHECK_EQ(elements[1]->type, cbor::type::tag);
}

// A lazy that holds no top-level item is a wrong use, as for every other read of lazy.
TEST_CASE("type: an empty lazy throws")
{
    CHECK_THROWS_AS((void)cbor::lazy{}.type(), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.clone(), std::logic_error);
}

}

namespace item_test
{

// The tree keeps the additional information of each head, so encode writes each length, tag number and integer
// at the width it had on the wire. A head that is not in preferred form comes back in the same form. 28([29(0)])
// is an array that holds itself; encode writes the head of tag 29 and its index and does not follow the pointer.
// The index of tag 29 is an integer like any other and keeps its width.
TEST_CASE("encode: a tree is written with the head widths of its input, also with a cycle through tag 29")
{
    struct row {
        std::string_view wire;
        std::string_view own;
    };
    for (row const r : {row{"\xd8\x1c\x81\xd8\x1d\x00"sv, "\xd8\x1c\x81\xd8\x1d\x00"sv},
                        row{"\x82\xd8\x1c\x19\x00\x01\xd8\x1d\x18\x00"sv, "\x82\xd8\x1c\x19\x00\x01\xd8\x1d\x18\x00"sv},
                        row{"\x9a\x00\x00\x00\x01\x7a\x00\x00\x00\x02hi"sv, "\x9a\x00\x00\x00\x01\x7a\x00\x00\x00\x02hi"sv}}) {
        CAPTURE(r.wire);
        auto const top_level = test::lazy_of_copy(std::string(r.wire));
        REQUIRE(top_level.has_value());
        auto const root = item_of(top_level->clone());
        std::string out;
        auto const size = cbor::encode(*root, out);
        REQUIRE(size.has_value());
        CHECK_EQ(*size, r.own.size());
        CHECK_EQ(out, r.own);
        CHECK(same_value(out, r.wire));
        auto const pieces = cbor::encode_iov(*root);
        REQUIRE(pieces.has_value());
        std::string joined;
        for (std::string_view const piece : **pieces)
            joined.append(piece);
        CHECK_EQ(joined, r.own);
    }
}

// encode reads the nesting depth once and refuses a tree deeper than it, as decode does: [[[0]]] decodes at the
// default and fails to encode at depth 2.
TEST_CASE("encode: the nesting depth bounds the tree")
{
    auto const top_level = test::lazy_of_copy(std::string("\x81\x81\x81\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    test::limits_guard const depth{{.nesting_depth = 2}};
    std::string out;
    auto const r = cbor::encode(*root, out);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::nesting_depth_exceeded);
}

// encode into a span that is too short gives no_buffer_space.
TEST_CASE("encode: a span that is too short")
{
    auto const top_level = test::lazy_of_copy(std::string("\x63" "abc"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    std::array<char, 3> buffer{};
    auto const r = cbor::encode(*root, std::span<char>(buffer));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::no_buffer_space);
}

// A tag 29 that names a mark which comes later on the wire is a plain tag in a tree decode.
TEST_CASE("decode: a reference before its mark is a plain tag")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\xd8\x1d\x00\xd8\x1c\x01"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    CHECK_EQ(root->get<cbor::type::array>().value()[0].get<cbor::type::tag>().value().first, 29u);
}
// RFC 8949 3.4.5.1: tag 24 holds a byte string with one encoded CBOR data item. The decode gives both the byte
// string as written and the embedded item as a tree, so a reader takes the item without a second decode, and
// encode writes the byte string again bit for bit.
TEST_CASE("decode: tag 24 gives the byte string and the embedded item")
{
    std::string const wire = "\xd8\x18\x45\x64IETF"s;
    auto const top_level = test::lazy_of_copy(wire);
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    REQUIRE_EQ(root->type, cbor::type::tag);
    CHECK_EQ(root->get<cbor::type::tag>().value().first, 24u);
    CHECK_EQ(root->get<cbor::type::tag>().value().second->get<cbor::type::byte_string>().value().size(), 5u);
    CHECK_EQ(cbor::encoded_cbor_data_item(root, *root)->get().get<cbor::type::text_string>().value(), "IETF");
    std::string again;
    REQUIRE(cbor::encode(*root, again).has_value());
    CHECK_EQ(again, wire);
}

// RFC 8949 3.4.5.1: the embedded item is a data item like any other, so it can itself be a tag 24. The decode
// enters each level, and encode writes the outer bytes again unchanged.
TEST_CASE("decode: a tag 24 inside a tag 24 is decoded at each level")
{
    std::string const wire = "\xd8\x18\x44\xd8\x18\x41\x01"s;
    auto const top_level = test::lazy_of_copy(wire);
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const outer_found = cbor::encoded_cbor_data_item(root, *root);
    REQUIRE(outer_found.has_value());
    cbor::item const &outer = outer_found->get();
    REQUIRE_EQ(outer.type, cbor::type::tag);
    auto const inner_found = cbor::encoded_cbor_data_item(root, outer);
    REQUIRE(inner_found.has_value());
    cbor::item const &inner = inner_found->get();
    CHECK_EQ(inner.type, cbor::type::unsigned_integer);
    CHECK_EQ(inner.get<cbor::type::unsigned_integer>().value(), 1u);
    std::string again;
    REQUIRE(cbor::encode(*root, again).has_value());
    CHECK_EQ(again, wire);
}

// The tree of a tag 24 lives in the memory of the decode result. The result must keep it, its bytes and their
// owner alive after the lazy and the caller's string are gone, so the address sanitizer sees every read here.
TEST_CASE("decode: the embedded item of a tag 24 lives as long as the result")
{
    std::optional<cbor::owning<cbor::item>> root;
    {
        std::string const wire = "\xd8\x18\x47\xd8\x18\x44\x82\x01\x61x"s;
        root.emplace(item_of(test::lazy_of_copy(std::string(wire))->clone()));
    }
    auto const outer_found = cbor::encoded_cbor_data_item(*root, **root);
    REQUIRE(outer_found.has_value());
    cbor::item const &outer = outer_found->get();
    auto const inner_found = cbor::encoded_cbor_data_item(*root, outer);
    REQUIRE(inner_found.has_value());
    cbor::item const &inner = inner_found->get();
    auto const children = children_of(inner);
    REQUIRE_EQ(children.size(), 2u);
    CHECK_EQ(children[0]->get<cbor::type::unsigned_integer>().value(), 1u);
    CHECK_EQ(children[1]->get<cbor::type::text_string>().value(), "x");
}

// A tree decode keeps a tag 28 over its value and a tag 29 over its index.
TEST_CASE("decode: a tag 28 and a tag 29 are tag nodes")
{
    std::string const wire = "\x82\xd8\x1c\x82\x01\x02\xd8\x1d\x00"s;
    auto const root = item_of(test::lazy_of_copy(std::string(wire))->clone());
    auto const children = children_of(*root);
    CHECK(children[0]->get<cbor::type::tag>().value().second->get<cbor::type::array>().has_value());
    CHECK_EQ(children[1]->get<cbor::type::tag>().value().first, 29u);
    CHECK_EQ(children[1]->get<cbor::type::tag>().value().second->get<cbor::type::unsigned_integer>().value(), 0u);
}

// Owner decision: the embedded item is a top-level item of its own. A tag 29 inside it refers to a tag 28 inside
// the same embedded item, and the marks outside do not count there.
TEST_CASE("decode: a shared reference inside tag 24 refers to a mark inside the same embedded item")
{
    auto const top_level = test::lazy_of_copy(std::string("\xd8\x18\x48\x82\xd8\x1c\x61\x61\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const elements = children_of(cbor::encoded_cbor_data_item(root, *root).value().get());
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(elements[1]->get<cbor::type::tag>().value().first, 29u);
}

// The other direction of the same decision: a mark outside is not visible inside, so 29(0) inside the embedded
// item has no mark to refer to.
TEST_CASE("decode: a shared reference inside tag 24 does not see a mark outside")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\xd8\x1c\x61\x61\xd8\x18\x43\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    auto const embedded = cbor::encoded_cbor_data_item(root, root->get<cbor::type::array>().value()[1]);
    REQUIRE(embedded.has_value());
    CHECK_EQ(embedded->get().get<cbor::type::tag>().value().first, 29u);
}

// And a mark inside the embedded item does not count outside: the 29(0) after the tag 24 has no mark.
TEST_CASE("decode: a mark inside tag 24 does not count outside")
{
    auto const top_level = test::lazy_of_copy(std::string("\x82\xd8\x18\x43\xd8\x1c\x01\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->clone());
    CHECK_EQ(root->get<cbor::type::array>().value()[1].get<cbor::type::tag>().value().first, 29u);
}

// The embedded bytes are read with the same checks as the outer bytes, so malformed embedded bytes give the
// error that the same bytes give at the top level.
TEST_CASE("decode: malformed bytes inside tag 24 give their own error")
{
    auto const short_head = test::lazy_of_copy(std::string("\xd8\x18\x41\x18"sv));
    REQUIRE(short_head.has_value());
    auto const r = short_head->clone();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::too_little_data);
    auto const reserved = test::lazy_of_copy(std::string("\xd8\x18\x41\x1c"sv));
    REQUIRE(reserved.has_value());
    auto const at_top = test::lazy_of_copy(std::string("\x1c"sv));
    REQUIRE(at_top.has_value());
    auto const inside = reserved->clone();
    auto const outside = at_top->clone();
    REQUIRE_FALSE(inside.has_value());
    REQUIRE_FALSE(outside.has_value());
    CHECK_EQ(inside.error(), outside.error());
}

// The limits hold across the boundary: the embedded root of 24(<<0>>) is at depth 2, below the byte string, and
// an embedded array counts against the container limit as an outer array does.
TEST_CASE("decode: the limits and the nesting depth count inside tag 24")
{
    auto const top_level = test::lazy_of_copy(std::string("\xd8\x18\x41\x00"sv));
    REQUIRE(top_level.has_value());
    {
        test::limits_guard const depth{{.nesting_depth = 1}};
        auto const refused = top_level->clone();
        REQUIRE_FALSE(refused.has_value());
        CHECK_EQ(refused.error(), cbor::error::nesting_depth_exceeded);
    }
    {
        test::limits_guard const depth{{.nesting_depth = 2}};
        CHECK(top_level->clone().has_value());
    }
    auto const array = test::lazy_of_copy(std::string("\xd8\x18\x44\x83\x01\x02\x03"sv));
    REQUIRE(array.has_value());
    test::limits_guard const elements{{.container_elements = 2}};
    auto const refused = array->clone();
    REQUIRE_FALSE(refused.has_value());
    CHECK_EQ(refused.error(), cbor::error::container_elements_exceeded);
}

// RFC 8949 3.4.5.1: the byte string of tag 24 holds one encoded data item. decode reads everything and refuses
// bytes after that item with syntax_error; the owner decided on this error.
// A lazy step and a typed read read only up to their target, by owner decision, so they do not see those bytes.
TEST_CASE("tag 24 with bytes after its data item is refused by decode and not seen by a step")
{
    std::string const wire = "\xd8\x18\x42\x01\x02"s;
    auto const top_level = test::lazy_of_copy(wire);
    REQUIRE(top_level.has_value());
    auto const tree = top_level->clone();
    REQUIRE_FALSE(tree.has_value());
    CHECK_EQ(tree.error(), cbor::error::syntax_error);
    CHECK_EQ(*top_level->get<int>(), 1);
    CHECK_EQ(*cbor::at_path<"$", int>(std::string_view(wire)), 1);
    std::string const in_array = "\xd8\x18\x43\x81\x01\x02"s;
    CHECK_EQ(*test::lazy_of_copy(in_array)->at(0)->get<int>(), 1);
    CHECK_EQ(*cbor::at_path<"$[0]", int>(std::string_view(in_array)), 1);
}

}

namespace item_test
{

// RFC 8949 3.3: a float of 16 or 32 bits is held as a double, and encode narrows it back to the width of its
// additional information. A conversion of the processor sets the quiet bit of a signalling NaN, so a NaN is
// widened and narrowed by its bits. Each width is checked with a signalling NaN, a quiet NaN with a payload,
// negative zero and the smallest subnormal, which must all come back byte for byte.
TEST_CASE("encode: a float of each width comes back byte for byte")
{
    struct row {
        std::string_view wire;
        cbor::type type;
    };
    row const rows[] = {
        {"\xf9\x7c\x01"sv, cbor::type::half_float},
        {"\xf9\x7e\x01"sv, cbor::type::half_float},
        {"\xf9\xfd\x55"sv, cbor::type::half_float},
        {"\xf9\x80\x00"sv, cbor::type::half_float},
        {"\xf9\x00\x01"sv, cbor::type::half_float},
        {"\xf9\x83\xff"sv, cbor::type::half_float},
        {"\xfa\x7f\x80\x00\x01"sv, cbor::type::single_float},
        {"\xfa\x7f\xc0\x00\x01"sv, cbor::type::single_float},
        {"\xfa\xff\xa0\x00\x00"sv, cbor::type::single_float},
        {"\xfa\x80\x00\x00\x00"sv, cbor::type::single_float},
        {"\xfa\x00\x00\x00\x01"sv, cbor::type::single_float},
        {"\xfb\x7f\xf0\x00\x00\x00\x00\x00\x01"sv, cbor::type::double_float},
        {"\xfb\x7f\xf8\x00\x00\x00\x00\x00\x01"sv, cbor::type::double_float},
        {"\xfb\x80\x00\x00\x00\x00\x00\x00\x00"sv, cbor::type::double_float},
        {"\xfb\x00\x00\x00\x00\x00\x00\x00\x01"sv, cbor::type::double_float},
    };
    for (row const &r : rows) {
        CAPTURE(r.wire);
        auto const root = item_of(test::lazy_of_copy(std::string(r.wire))->clone());
        CHECK_EQ(root->type, r.type);
        CHECK_EQ(root->additional_information, static_cast<std::uint8_t>(r.wire.front()) & 0x1f);
        std::string again;
        REQUIRE(cbor::encode(*root, again).has_value());
        CHECK_EQ(again, r.wire);
    }
}

// RFC 8949 3.1 and 3.3 give the type of an item by its major type and its additional information. The input is a
// finite set, so every pair of major type 0 to 7 and additional information 0 to 27 is checked. A head that is not
// in preferred form is well-formed, so an argument of 1 stands in every width; a simple value in one extra byte
// needs 32 or more. Each tag takes a content that RFC 8949 3.4 admits; tag 29 needs a mark and has its own tests.
// The expected type is written from the tables of RFC 8949 3.1 and 3.3. A tag 2 or 3 over h'05' fits a basic
// integer and is that integer (RFC 8949 3.4.3). encode writes each head at its input width, a folded bignum in the shortest head, and a float
// at its width, which also checks the major type that encode takes from the type.
TEST_CASE("decode: every major type and additional information gives the type of RFC 8949")
{
    for (unsigned major = 0; major < 8; ++major) {
        for (unsigned info = 0; info < 28; ++info) {
            CAPTURE(major);
            CAPTURE(info);
            std::string wire(1, static_cast<char>(major << 5 | info));
            std::size_t const width = info < 24 ? 0 : std::size_t{1} << (info - 24);
            std::uint64_t const argument = info < 24 ? info : major == 7 && info == 24 ? 32 : 1;
            for (std::size_t i = width; i > 0; --i)
                wire += static_cast<char>(argument >> (8 * (i - 1)) & 0xff);
            if (major == 2 || major == 3)
                wire.append(argument, 'a');
            else if (major == 4)
                wire.append(argument, '\x00');
            else if (major == 6)
                wire += argument == 0                     ? "\x60"s
                        : argument == 2 || argument == 3  ? "\x41\x05"s
                        : argument == 24                  ? "\x41\x00"s
                        : argument == 29                  ? ""s
                                                          : "\x00"s;
            if (major == 6 && argument == 29)
                continue;
            else if (major == 5)
                wire.append(2 * argument, '\x00');
            cbor::type expected = cbor::type::simple_value;
            switch (major) {
            case 0:
                expected = cbor::type::unsigned_integer;
                break;
            case 1:
                expected = cbor::type::negative_integer;
                break;
            case 2:
                expected = cbor::type::byte_string;
                break;
            case 3:
                expected = cbor::type::text_string;
                break;
            case 4:
                expected = cbor::type::array;
                break;
            case 5:
                expected = cbor::type::map;
                break;
            case 6:
                expected = argument == 2   ? cbor::type::unsigned_integer
                           : argument == 3 ? cbor::type::negative_integer
                                           : cbor::type::tag;
                break;
            default:
                expected = info == 20   ? cbor::type::false_value
                           : info == 21 ? cbor::type::true_value
                           : info == 22 ? cbor::type::null
                           : info == 23 ? cbor::type::undefined
                           : info == 25 ? cbor::type::half_float
                           : info == 26 ? cbor::type::single_float
                           : info == 27 ? cbor::type::double_float
                                        : cbor::type::simple_value;
                break;
            }
            auto const top_level = test::lazy_of_copy(std::string(wire));
            REQUIRE(top_level.has_value());
            auto const decoded = top_level->clone();
            REQUIRE(decoded.has_value());
            CHECK_EQ((*decoded)->type, expected);
            bool const bignum = major == 6 && (argument == 2 || argument == 3);
            CHECK_EQ((*decoded)->additional_information, bignum ? 5 : info);
            CHECK_EQ(major_of((*decoded)->type), bignum ? static_cast<cbor::major_type>(argument - 2)
                                                        : static_cast<cbor::major_type>(major));
            std::string const own = bignum ? (argument == 2 ? "\x05"s : "\x25"s) : wire;
            std::string again;
            REQUIRE(cbor::encode(**decoded, again).has_value());
            CHECK_EQ(again, own);
            CHECK(same_value(again, wire));
        }
    }
}

// RFC 8949 3.4.3: a bignum that fits a basic integer is that integer, and preferred serialization never writes
// it as a bignum. A larger bignum and a typed array of RFC 8746 keep major type 6 when they are written back.
TEST_CASE("encode: a bignum that fits is an integer, a larger bignum and a typed array stay tags")
{
    struct row {
        std::string_view wire;
        std::string_view own;
        cbor::major_type major;
    };
    std::string const two_64 = "\xc2\x49\x01"s + std::string(8, '\0');
    std::string const minus_two_64_minus_1 = "\xc3\x49\x01"s + std::string(8, '\0');
    for (row const r : {row{"\xc2\x41\x05"sv, "\x05"sv, cbor::major_type::unsigned_integer},
                        row{"\xc3\x41\x05"sv, "\x25"sv, cbor::major_type::negative_integer},
                        row{two_64, two_64, cbor::major_type::tag},
                        row{minus_two_64_minus_1, minus_two_64_minus_1, cbor::major_type::tag},
                        row{"\xd8\x40\x40"sv, "\xd8\x40\x40"sv, cbor::major_type::tag}}) {
        CAPTURE(r.wire);
        auto const root = item_of(test::lazy_of_copy(std::string(r.wire))->clone());
        CHECK_EQ(major_of(root->type), r.major);
        std::string again;
        REQUIRE(cbor::encode(*root, again).has_value());
        CHECK_EQ(again, r.own);
        CHECK(same_value(again, r.wire));
    }
}

// encoded_cbor_data_item takes a tag 24 of the decoded tree that is passed. Any other item is a wrong use and
// throws, so a caller never reads an item that does not exist.
TEST_CASE("encoded_cbor_data_item: an item that is not a tag 24 of the tree throws")
{
    auto const root = item_of(test::lazy_of_copy(std::string("\x82\xd8\x1c\x01\xd8\x1d\x00"sv))->clone());
    CHECK_THROWS_AS((void)cbor::encoded_cbor_data_item(root, root->get<cbor::type::array>().value()[1]), std::logic_error);
    // A tree that no decode made cannot be built: owning<item> has no public constructor.
    CHECK_FALSE((std::is_constructible_v<cbor::owning<cbor::item>, std::shared_ptr<cbor::item const>>));
}
// Owner decision 2026-10-17: an item is a result of a decode only. No caller builds one, so no item holds a wrong
// count or a type that its content does not match, and every read of the content checks the type.
TEST_CASE("item: no caller builds an item")
{
    CHECK_FALSE((std::is_constructible_v<cbor::item, cbor::type, std::uint8_t>));
    CHECK_FALSE(std::is_default_constructible_v<cbor::item>);
    CHECK_FALSE(std::is_copy_assignable_v<cbor::item>);
}

}

// RFC 8949 3: each data item has at least its initial byte. decode refuses a count larger than the bytes left with
// too_little_data before it reads an element, by owner decision; the malformed element after the head is not reached.
TEST_CASE("decode: a count larger than the bytes left is too_little_data before the elements")
{
    for (std::string const &wire : {"\x8c\xff"s, "\x95\xf8\x04"s, "\x95\xbf"s, "\x88\x00\x7f"s, "\x87\xd8\x1c\x1f"s,
                                   "\x85\xd8\x1c\xd8\x1c"s, "\xa2\xff\x00\x00"s}) {
        CAPTURE(wire);
        auto const top_level = test::lazy_of_copy(wire);
        REQUIRE(top_level.has_value());
        auto const r = top_level->clone();
        REQUIRE_FALSE(r.has_value());
        CHECK_EQ(r.error(), cbor::error::too_little_data);
    }
}

// A string of length 0 takes no bytes from the block, and its view must still read as an empty string.
TEST_CASE("decode: an empty text string and an empty byte string read as empty")
{
    auto const tree = cbor::decode("\x82\x60\x40"sv);
    REQUIRE(tree.has_value());
    auto const children = (*tree)->get<cbor::type::array>().value();
    CHECK_EQ(children[0].get<cbor::type::text_string>().value(), ""sv);
    CHECK(children[1].get<cbor::type::byte_string>().value().empty());
}

// The strings of a decode are copied into blocks of validity::bump_chunk_size. A string of exactly that size fills a
// block, and a string one byte longer needs a block of its own; both must keep every byte, and the tree must outlive
// the input.
TEST_CASE("decode: strings at the size of a block and one byte over keep every byte after the input is freed")
{
    constexpr std::size_t block = cbor::validity::bump_chunk_size;
    std::string const a(block, 'a');
    std::string const b(block + 1, 'b');
    auto input = std::make_unique<std::string>("\x83\x61x\x79"s);
    input->push_back(static_cast<char>(block >> 8));
    input->push_back(static_cast<char>(block & 0xff));
    *input += a;
    *input += "\x79"s;
    input->push_back(static_cast<char>((block + 1) >> 8));
    input->push_back(static_cast<char>((block + 1) & 0xff));
    *input += b;
    auto const tree = cbor::decode(*input);
    input.reset();
    REQUIRE(tree.has_value());
    auto const children = (*tree)->get<cbor::type::array>().value();
    CHECK_EQ(children[0].get<cbor::type::text_string>().value(), "x"sv);
    CHECK_EQ(children[1].get<cbor::type::text_string>().value(), a);
    CHECK_EQ(children[2].get<cbor::type::text_string>().value(), b);
}

// An error part way through gives the error and no tree; the strings copied before the error go with the zone.
TEST_CASE("decode: an error after some strings gives the error and no tree")
{
    auto const tree = cbor::decode("\x83\x61x\x61y\x7a\x00\x00\x10\x00"sv);
    REQUIRE_FALSE(tree.has_value());
    CHECK_EQ(tree.error(), error::too_little_data);
}

// The strings inside the tree of a tag 24 are copies in the zone too, so they stay valid after the input is freed.
TEST_CASE("decode: a string inside a tag 24 tree outlives the input")
{
    auto input = std::make_unique<std::string>("\xd8\x18\x45\x64IETF"s);
    auto const tree = cbor::decode(*input);
    input.reset();
    REQUIRE(tree.has_value());
    CHECK_EQ(cbor::encoded_cbor_data_item(*tree, **tree)->get().get<cbor::type::text_string>().value(), "IETF");
}

namespace
{

struct sample {
    cbor::type type;
    std::string_view encoded;
};

// One encoded item for each member of cbor::type, written from RFC 8949 Appendix A and RFC 8746.
constexpr std::array<sample, 18> samples{{
    {cbor::type::unsigned_integer, "\x18\x64"sv},
    {cbor::type::negative_integer, "\x38\x63"sv},
    {cbor::type::byte_string, "\x44\x01\x02\x03\x04"sv},
    {cbor::type::text_string, "\x64IETF"sv},
    {cbor::type::array, "\x83\x01\x02\x03"sv},
    {cbor::type::map, "\xa1\x01\x02"sv},
    {cbor::type::tag, "\xc1\x1a\x51\x4b\x67\xb0"sv},
    {cbor::type::false_value, "\xf4"sv},
    {cbor::type::true_value, "\xf5"sv},
    {cbor::type::null, "\xf6"sv},
    {cbor::type::undefined, "\xf7"sv},
    {cbor::type::simple_value, "\xf0"sv},
    {cbor::type::half_float, "\xf9\x3c\x00"sv},
    {cbor::type::single_float, "\xfa\x47\xc3\x50\x00"sv},
    {cbor::type::double_float, "\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"sv},
    {cbor::type::unsigned_bignum, "\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv},
    {cbor::type::negative_bignum, "\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv},
    {cbor::type::typed_array, "\xd8\x40\x42\x01\x02"sv},
}};

template <cbor::type K>
void check_get(cbor::item const &it)
{
    auto const r = it.get<K>();
    if (K == it.type) {
        CHECK(r.has_value());
        return;
    }
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::incorrect_type);
}

template <std::size_t... I>
void check_every_kind(cbor::item const &it, std::index_sequence<I...>)
{
    (check_get<samples[I].type>(it), ...);
}

} // namespace

// get<K> must give its value for the type of the item and incorrect_type for each of the other 17 types: the set of
// types is finite, so every pair is checked.
TEST_CASE("item get<K>: the value for its own type and incorrect_type for every other type")
{
    for (sample const &s : samples) {
        CAPTURE(std::to_underlying(s.type));
        auto const tree = cbor::decode(s.encoded);
        REQUIRE(tree.has_value());
        REQUIRE_EQ((*tree)->type, s.type);
        check_every_kind(**tree, std::make_index_sequence<samples.size()>{});
    }
}

// The values that get<K> gives are the head fields of RFC 8949 3.1: an integer gives its argument, a negative
// integer too (its value is -1 minus the argument).
TEST_CASE("item get<K>: the values of the head")
{
    auto const u = cbor::decode("\x18\x64"sv);
    CHECK_EQ(*(*u)->get<cbor::type::unsigned_integer>(), 100u);
    auto const n = cbor::decode("\x38\x63"sv);
    CHECK_EQ(*(*n)->get<cbor::type::negative_integer>(), 99u);
    auto const t = cbor::decode("\x64IETF"sv);
    CHECK_EQ(*(*t)->get<cbor::type::text_string>(), "IETF"sv);
    auto const b = cbor::decode("\x44\x01\x02\x03\x04"sv);
    CHECK_EQ((*b)->get<cbor::type::byte_string>()->size(), 4u);
    auto const a = cbor::decode("\x83\x01\x02\x03"sv);
    CHECK_EQ((*a)->get<cbor::type::array>()->size(), 3u);
    auto const m = cbor::decode("\xa1\x01\x02"sv);
    CHECK_EQ((*m)->get<cbor::type::map>()->size(), 2u);
    auto const tag = cbor::decode("\xc1\x1a\x51\x4b\x67\xb0"sv);
    CHECK_EQ((*tag)->get<cbor::type::tag>()->first, 1u);
    CHECK_EQ(*(*tag)->get<cbor::type::tag>()->second->get<cbor::type::unsigned_integer>(), 1363896240u);
    auto const h = cbor::decode("\xf9\x3c\x00"sv);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*(*h)->get<cbor::type::half_float>()), std::bit_cast<std::uint64_t>(1.0));
    auto const yes = cbor::decode("\xf5"sv);
    CHECK_EQ(*(*yes)->get<cbor::type::true_value>(), cbor::simple_value{21});
}
