#include <cbor/cbor.hpp>
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
    cbor::item const minus_500{cbor::type::negative_integer, 25, {.u64 = 499}};
    CHECK(minus_500.type == cbor::type::negative_integer);
    CHECK(minus_500.additional_information == 25);
    CHECK(-1 - static_cast<std::int64_t>(minus_500.via.u64) == -500);
}

// RFC 8949 3.4: a tag is major type 6 with the tag number as argument.
// The content points to the item of the tag content, so a reader goes
// from the tag to its content by the address.
TEST_CASE("a tag holds its number and points to its content")
{
    cbor::item const epoch{cbor::type::unsigned_integer, 26, {.u64 = 1363896240}};
    cbor::item const time{cbor::type::tag, 1, {.tag = {1, &epoch}}};
    CHECK(time.via.tag.number == 1);
    CHECK(time.via.tag.content == &epoch);
}

// RFC 8949 3.3 Table 3: major type 7 says by its additional information
// what the item is. A float keeps the width it had on the wire in the
// additional information and in its type, and its value widened to a double,
// which holds every value of the narrower widths.
TEST_CASE("a float keeps its width in the additional information and its value as a double")
{
    cbor::item const f16{cbor::type::half_float, 25, {.f64 = 1.5}};
    cbor::item const f32{cbor::type::single_float, 26, {.f64 = 100000.0}};
    cbor::item const f64{cbor::type::double_float, 27, {.f64 = 1.1}};
    CHECK(f16.additional_information == 25);
    CHECK(f16.via.f64 == 1.5);
    CHECK(f32.additional_information == 26);
    CHECK(f32.via.f64 == 100000.0);
    CHECK(f64.additional_information == 27);
    CHECK(f64.via.f64 == 1.1);
}

// undefined is a value of its own, apart from null, so that a message
// from JavaScript passes through unchanged.
TEST_CASE("null and undefined are two items")
{
    cbor::item const null{cbor::type::null, 22, {.simple_value = cbor::simple_value::null}};
    cbor::item const undefined{cbor::type::undefined, 23, {.simple_value = cbor::simple_value::undefined}};
    CHECK(null.type != undefined.type);
    CHECK(null.additional_information != undefined.additional_information);
}

// An item owns no memory, so a cache of many items costs only their own
// size. Each member of the union is an immediate value or a view, and the
// item takes 24 bytes: one byte of type, one of additional information,
// and a union of 16 bytes.
TEST_CASE("an item owns no memory")
{
    CHECK(std::is_trivially_destructible_v<cbor::item>);
    CHECK(std::is_trivially_copyable_v<cbor::item>);
    CHECK(std::is_aggregate_v<cbor::item>);
    CHECK_EQ(sizeof(cbor::item), 24u);
}

std::shared_ptr<cbor::item const> item_of(std::expected<std::shared_ptr<cbor::item const>, cbor::error> const &r)
{
    REQUIRE(r.has_value());
    return *r;
}

// Each decode returns items that the caller owns, and no lazy keeps a
// cache that a second call or a second thread would write. Two decodes
// of one node give two items with the same content.
TEST_CASE("decode: a node decoded twice gives two items that the caller owns")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\x01\x62hi"sv));
    REQUIRE(top_level.has_value());
    auto const first = top_level->decode();
    auto const second = top_level->decode();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK_NE(first->get(), second->get());
    CHECK_EQ(argument_of(*(*first)), argument_of(*(*second)));
    auto const a = children_of(**first);
    auto const b = children_of(**second);
    REQUIRE_EQ(a.size(), 2u);
    REQUIRE_EQ(b.size(), 2u);
    CHECK_NE(a[1], b[1]);
    CHECK_EQ(a[1]->via.str.data(), b[1]->via.str.data());
}

// The fault: decode on the lazy inside a temporary expected returned a
// reference into the top-level item that the temporary freed at the end
// of the statement. The result now owns its items and the top-level
// item, so it stays valid after the lazy is gone.
TEST_CASE("decode: the item outlives the lazy that decoded it")
{
    std::string const s("\x83\x01\x02\x03"sv);
    auto const r = cbor::lazy::from(s)->decode();
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
    auto const top_level = cbor::lazy::from(std::string("\xa1\x61k\x82\x01\x02"sv));
    REQUIRE(top_level.has_value());
    auto const map = item_of(top_level->decode());
    CHECK_EQ(argument_of(*map), 1u);
    auto const entries = top_level->entries();
    REQUIRE(entries.has_value());
    auto const entry = *entries->begin();
    REQUIRE(entry.has_value());
    auto const value = top_level->at("k");
    REQUIRE(value.has_value());
    auto const by_key = item_of(value->decode());
    auto const by_entry = item_of(entry->second.decode());
    CHECK_EQ(argument_of(*by_key), argument_of(*by_entry));
    REQUIRE_EQ(children_of(*by_key).size(), 2u);
    REQUIRE_EQ(children_of(*by_entry).size(), 2u);
    CHECK_EQ(argument_of(*children_of(*by_key)[1]), argument_of(*children_of(*by_entry)[1]));
}

// RFC 8949 3.1: an array is its count and its data items. A map holds each pair as key and value, one after
// the other, so a map of n pairs shows 2n items in the order of the wire.
TEST_CASE("decode: an array and a map hold their data items in the order of the wire")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\x01\xa2\x61k\x62hi\x02\x80"sv));
    REQUIRE(top_level.has_value());
    auto const array = item_of(top_level->decode());
    auto const elements = children_of(*array);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(argument_of(*elements[0]), 1u);
    CHECK_EQ(elements[1]->type, cbor::type::map);
    auto const pairs = children_of(*elements[1]);
    REQUIRE_EQ(pairs.size(), 4u);
    CHECK_EQ(pairs[0]->via.str, "k");
    CHECK_EQ(pairs[1]->via.str, "hi");
    CHECK_EQ(argument_of(*pairs[2]), 2u);
    CHECK_EQ(pairs[3]->type, cbor::type::array);
    CHECK(children_of(*pairs[3]).empty());
}

// The children of every container stand in one vector, and each container shows its own part. Nested
// containers and siblings each see only their own children, in the order of the wire.
TEST_CASE("decode: nested arrays and maps each show their own children")
{
    // [[1, [2, 3]], {4: [5]}, [], 6]
    auto const top_level = cbor::lazy::from(std::string("\x84\x82\x01\x82\x02\x03\xa1\x04\x81\x05\x80\x06"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
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
    auto const top_level = cbor::lazy::from(std::string("\x9b\x80\x00\x00\x00\x00\x00\x00\x00\x01"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::too_little_data);
    auto const map = cbor::lazy::from(std::string("\xbb\x40\x00\x00\x00\x00\x00\x00\x00\x01"sv));
    REQUIRE(map.has_value());
    auto const m = map->decode();
    REQUIRE_FALSE(m.has_value());
    CHECK_EQ(m.error(), cbor::error::too_little_data);
}

// The check of the count admits a count equal to the bytes left: five empty arrays in five bytes are five
// children, each with no children of its own.
TEST_CASE("decode: a count equal to the bytes left of empty children is read")
{
    auto const top_level = cbor::lazy::from(std::string("\x85\x80\x80\x80\x80\x80"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
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
    auto const top_level = cbor::lazy::from(std::move(wire));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
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
    auto const top_level = cbor::lazy::from(std::string("\x82\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00\xd8\x40\x42\x01\x02"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    auto const elements = children_of(*root);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(elements[0]->type, cbor::type::unsigned_bignum);
    CHECK_EQ(argument_of(*elements[0]), 2u);
    auto const *const magnitude = elements[0]->via.tag.content;
    CHECK_EQ(magnitude->via.bin.size(), 9u);
    CHECK_EQ(argument_of(*elements[1]), 64u);
    auto const *const typed = elements[1]->via.tag.content;
    CHECK_EQ(typed->via.bin.size(), 2u);
}

// RFC 8949 5.6 lets a decoder that is not in a deterministic profile keep one entry of a repeated key. decode
// builds the item of such a map with no error, at the top and inside an array.
TEST_CASE("decode: a map with a repeated key is read with no error")
{
    auto const twice = cbor::lazy::from(std::string("\xa2\x61k\x01\x61k\x02"sv));
    REQUIRE(twice.has_value());
    CHECK(twice->decode().has_value());
    auto const nested = cbor::lazy::from(std::string("\x81\xa2\x01\x00\x18\x01\x00"sv));
    REQUIRE(nested.has_value());
    REQUIRE(nested->decode().has_value());
    auto const inner = nested->at(0);
    REQUIRE(inner.has_value());
    CHECK(inner->decode().has_value());
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
    auto const unique = cbor::lazy::from(std::move(distinct));
    REQUIRE(unique.has_value());
    CHECK(unique->decode().has_value());
    auto const twice = cbor::lazy::from(std::move(map));
    REQUIRE(twice.has_value());
    CHECK(twice->decode().has_value());
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
    auto const unique = cbor::lazy::from(std::move(negative));
    REQUIRE(unique.has_value());
    CHECK(unique->decode().has_value());
    std::string longer = map;
    longer.replace(map.size() - 4, 4, "\x1a\x00\x00\x00\x00\x00"sv);
    auto const twice = cbor::lazy::from(std::move(longer));
    REQUIRE(twice.has_value());
    CHECK(twice->decode().has_value());
}

// The result owns what it shows, so decode on a temporary lazy is safe
// and compiles.
TEST_CASE("decode: a temporary lazy gives an item")
{
    auto const r = std::move(*cbor::lazy::from(std::string("\x01"sv))).decode();
    REQUIRE(r.has_value());
    CHECK_EQ((*r)->type, cbor::type::unsigned_integer);
    CHECK_EQ(argument_of(*(*r)), 1u);
}

// The order does not matter: a child decoded first and decoded again
// after its parent has one content.
TEST_CASE("decode: a child decoded before its parent has the same content after it")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\x01\x82\x02\x03"sv));
    REQUIRE(top_level.has_value());
    auto const child = top_level->at(1);
    REQUIRE(child.has_value());
    auto const inner = item_of(child->decode());
    REQUIRE(top_level->decode().has_value());
    auto const again = top_level->at(1);
    REQUIRE(again.has_value());
    auto const later = item_of(again->decode());
    CHECK_EQ(argument_of(*later), argument_of(*inner));
    CHECK_EQ(argument_of(*children_of(*later)[1]), argument_of(*children_of(*inner)[1]));
}

// RFC 8949 3.4 and the value sharing tags 28 and 29: tag 28 stays a tag item over the shared value, and the
// item of 29(0) points to that value in the same tree. Nothing is copied and nothing is followed.
TEST_CASE("decode: a shared reference points to the item of the shared value")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1c\x62hi\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    auto const elements = children_of(*root);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(elements[0]->type, cbor::type::tag);
    CHECK_EQ(argument_of(*elements[0]), 28u);
    auto const *const value = elements[0]->via.tag.content;
    CHECK_EQ(value->via.str, "hi");
    CHECK_EQ(argument_of(*elements[1]), 29u);
    CHECK_EQ(&cbor::shared_value(root, *elements[1]), value);
    CHECK_EQ(elements[1]->via.tag.content->type, cbor::type::unsigned_integer);
    CHECK_EQ(elements[1]->via.tag.content->via.u64, 0u);
}

// A shared reference from inside a lazy to a mark before it: the decode starts at the element and reads the
// mark before the element, so the reference points to an item with the shared value.
TEST_CASE("decode: a shared reference to a mark before the decoded node is read")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1c\x62hi\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const element = top_level->at(1);
    REQUIRE(element.has_value());
    auto const inner = item_of(element->decode());
    CHECK_EQ(cbor::shared_value(inner, *children_of(*inner)[0]).via.str, "hi");
}

// A cycle comes from the wire: 28([29(0)]) is an array that holds
// itself. The element points to that array, and the decode ends.
TEST_CASE("decode: a cyclic array is its own element")
{
    auto const top_level = cbor::lazy::from(std::string("\xd8\x1c\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    auto const *const array = root->via.tag.content;
    CHECK_EQ(array->type, cbor::type::array);
    auto const elements = children_of(*array);
    REQUIRE_EQ(elements.size(), 1u);
    CHECK_EQ(&cbor::shared_value(root, *elements[0]), array);
}

// A reference to a value that comes later on the wire is refused, as in
// every other reader of the shared values. A reference counts only the
// marks that come before it on the wire, so the mark after it is not
// marked, as in at_path. The decode enters the array, so the whole
// item is refused too.
TEST_CASE("decode: a forward shared reference is refused")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1d\x00\xd8\x1c\x00"sv));
    REQUIRE(top_level.has_value());
    auto const whole = top_level->decode();
    REQUIRE_FALSE(whole.has_value());
    CHECK_EQ(whole.error(), cbor::error::sharedref_index_not_marked);
    auto const element = top_level->at(0);
    REQUIRE(element.has_value());
    auto const r = element->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::sharedref_index_not_marked);
}

// Fault 21: in [28(0), 28(29(0)), 28(29(1)), ...] each element refers to the
// one before it. The decode of the last element walked the chain once for
// each step and recursed once for each step at the same depth, so a long chain
// took time in the square of its length and overflowed the stack. The decode
// reads the marks before the element in one loop, and the chain of pointers
// ends at the integer at its start.
TEST_CASE("decode: a long chain of shared references resolves without recursion")
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
    auto const top_level = cbor::lazy::from(std::move(document));
    REQUIRE(top_level.has_value());
    auto const last = top_level->at(chain_length - 1);
    REQUIRE(last.has_value());
    auto const decoded = item_of(last->decode());
    cbor::item const *at = decoded->via.tag.content;
    std::size_t steps = 0;
    while (at->type == cbor::type::tag && at->via.tag.number == 29) {
        at = &cbor::shared_value(decoded, *at);
        if (++steps > chain_length)
            break;
    }
    CHECK_EQ(steps, chain_length - 1u);
    CHECK_EQ(at->type, cbor::type::unsigned_integer);
}

// The nesting depth is checked where the decode recurses: 100(100(100(0))) holds an
// integer at depth 3, and so does [[[0]]].
TEST_CASE("decode: the nesting depth is limited by the nesting depth in force")
{
    auto const top_level = cbor::lazy::from(std::string("\xd8\x64\xd8\x64\xd8\x64\x00"sv));
    REQUIRE(top_level.has_value());
    {
        test::limits_guard const depth{{.nesting_depth = 2}};
        auto const refused = top_level->decode();
        REQUIRE_FALSE(refused.has_value());
        CHECK_EQ(refused.error(), cbor::error::nesting_depth_exceeded);
    }
    auto const arrays = cbor::lazy::from(std::string("\x81\x81\x81\x00"sv));
    REQUIRE(arrays.has_value());
    {
        test::limits_guard const depth{{.nesting_depth = 2}};
        auto const refused = arrays->decode();
        REQUIRE_FALSE(refused.has_value());
        CHECK_EQ(refused.error(), cbor::error::nesting_depth_exceeded);
    }
    test::limits_guard const depth{{.nesting_depth = 3}};
    CHECK(top_level->decode().has_value());
    CHECK(arrays->decode().has_value());
}

// The decode recurses once for each level of nesting. At the hard bound of the nesting depth, an array in an
// array 512 times and a tag on a tag 512 times decode with no overflow of the stack, in a debug build with
// AddressSanitizer too.
TEST_CASE("decode: the hard bound of the nesting depth fits the stack")
{
    constexpr std::size_t bound = 512;
    test::limits_guard const depth{{.nesting_depth = bound}};
    auto const arrays = cbor::lazy::from(std::string(bound, '\x81') + '\x00');
    REQUIRE(arrays.has_value());
    CHECK(arrays->decode().has_value());
    std::string tags;
    for (std::size_t i = 0; i < bound; ++i)
        tags += "\xd8\x64"sv;
    tags += '\x00';
    auto const tagged = cbor::lazy::from(std::move(tags));
    REQUIRE(tagged.has_value());
    CHECK(tagged->decode().has_value());
    // [28([[...[0]...]]), [[...[29(0)]...]]]: a lazy at the second element meets the reference near the bound
    // and reads the mark at the depth of its own position, so the stack holds the frames of both nests.
    std::string const shared = "\x82\xd8\x1c"s + std::string(bound - 2, '\x81') + '\x00' +
                               std::string(bound - 3, '\x81') + "\xd8\x1d\x00"s;
    auto const whole = cbor::lazy::from(shared);
    REQUIRE(whole.has_value());
    CHECK(whole->decode().has_value());
    auto const second = whole->at(1);
    REQUIRE(second.has_value());
    CHECK(second->decode().has_value());
}

// Indefinite length is never read; a streaming reader is the place for it.
TEST_CASE("decode: indefinite length is refused")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\x01\x9f\xff"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::indefinite_length);
}

// RFC 8949 3.3: a float16 keeps its width in the additional information,
// so it is written back in the width it came in.
TEST_CASE("decode: a float16 keeps additional information 25")
{
    auto const top_level = cbor::lazy::from(std::string("\xf9\x3e\x00"sv));
    REQUIRE(top_level.has_value());
    auto const f = item_of(top_level->decode());
    CHECK_EQ(f->type, cbor::type::half_float);
    CHECK_EQ(f->additional_information, 25);
    CHECK_EQ(f->via.f64, 1.5);
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
        row{"\xc2\x40"sv, cbor::type::unsigned_bignum},
        row{"\xc3\x40"sv, cbor::type::negative_bignum},
        row{"\xd8\x40\x40"sv, cbor::type::typed_array},
        row{"\xd8\x57\x40"sv, cbor::type::typed_array},
    };
    for (row const &r : rows) {
        auto const l = cbor::lazy::from(r.wire);
        REQUIRE(l.has_value());
        auto const by_lazy = l->type();
        REQUIRE(by_lazy.has_value());
        CHECK_EQ(*by_lazy, r.expected);
        CHECK_EQ(item_of(l->decode())->type, r.expected);
    }
}

// lazy reads through the value sharing tags 28 and 29, as every other read of lazy does, and gives the type of
// the shared value. The decoded item keeps tag 28 and tag 29 as tags.
TEST_CASE("type: lazy reads through tags 28 and 29, the item keeps them")
{
    auto const root = cbor::lazy::from(std::string("\x82\xd8\x1c\x01\xd8\x1d\x00"sv));
    REQUIRE(root.has_value());
    CHECK_EQ(*root->at(0)->type(), cbor::type::unsigned_integer);
    CHECK_EQ(*root->at(1)->type(), cbor::type::unsigned_integer);
    auto const tree = item_of(root->decode());
    auto const elements = children_of(*tree);
    CHECK_EQ(elements[0]->type, cbor::type::tag);
    CHECK_EQ(elements[1]->type, cbor::type::tag);
}

// A lazy that holds no top-level item is a wrong use, as for every other read of lazy.
TEST_CASE("type: an empty lazy throws")
{
    CHECK_THROWS_AS((void)cbor::lazy{}.type(), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.decode(), std::logic_error);
}

}

namespace item_test
{

// encode writes the tree back with the additional information of each head, so an item that is not in preferred
// form and a tag 29 that points back into its own container come back byte for byte. 28([29(0)]) is an array
// that holds itself; encode writes the head of tag 29 and its index and does not follow the pointer.
TEST_CASE("encode: a tree with a cycle through tag 29 comes back byte for byte")
{
    for (std::string_view const wire : {"\xd8\x1c\x81\xd8\x1d\x00"sv, "\x82\xd8\x1c\x19\x00\x01\xd8\x1d\x18\x00"sv,
                                        "\x9a\x00\x00\x00\x01\x7a\x00\x00\x00\x02hi"sv}) {
        CAPTURE(wire);
        auto const top_level = cbor::lazy::from(std::string(wire));
        REQUIRE(top_level.has_value());
        auto const root = item_of(top_level->decode());
        std::string out;
        auto const size = cbor::encode(*root, out);
        REQUIRE(size.has_value());
        CHECK_EQ(*size, wire.size());
        CHECK_EQ(out, wire);
        auto const pieces = cbor::encode_iov(*root);
        REQUIRE(pieces.has_value());
        std::string joined;
        for (std::string_view const piece : **pieces)
            joined.append(piece);
        CHECK_EQ(joined, wire);
    }
}

// encode reads the nesting depth once and refuses a tree deeper than it, as decode does: [[[0]]] decodes at the
// default and fails to encode at depth 2.
TEST_CASE("encode: the nesting depth bounds the tree")
{
    auto const top_level = cbor::lazy::from(std::string("\x81\x81\x81\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    test::limits_guard const depth{{.nesting_depth = 2}};
    std::string out;
    auto const r = cbor::encode(*root, out);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::nesting_depth_exceeded);
}

// encode into a span that is too short gives no_buffer_space.
TEST_CASE("encode: a span that is too short")
{
    auto const top_level = cbor::lazy::from(std::string("\x63" "abc"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    std::array<char, 3> buffer{};
    auto const r = cbor::encode(*root, std::span<char>(buffer));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::no_buffer_space);
}

// A tag 29 that names a mark which comes later on the wire is refused: [29(0), 28(1)] names mark 0 before tag 28
// stands, so the decode of the whole item fails and builds no pointer to an item that does not exist yet.
TEST_CASE("decode: a reference before its mark is refused")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1d\x00\xd8\x1c\x01"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::sharedref_index_not_marked);
}

// RFC 8949 3.4.5.1: tag 24 holds a byte string with one encoded CBOR data item. The decode gives both the byte
// string as written and the embedded item as a tree, so a reader takes the item without a second decode, and
// encode writes the byte string again bit for bit.
TEST_CASE("decode: tag 24 gives the byte string and the embedded item")
{
    std::string const wire = "\xd8\x18\x45\x64IETF"s;
    auto const top_level = cbor::lazy::from(wire);
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    REQUIRE_EQ(root->type, cbor::type::tag);
    CHECK_EQ(root->via.tag.number, 24u);
    CHECK_EQ(root->via.tag.content->type, cbor::type::byte_string);
    CHECK_EQ(root->via.tag.content->via.bin.size(), 5u);
    CHECK_EQ(cbor::encoded_cbor_data_item(root, *root).via.str, "IETF");
    std::string again;
    REQUIRE(cbor::encode(*root, again).has_value());
    CHECK_EQ(again, wire);
}

// RFC 8949 3.4.5.1: the embedded item is a data item like any other, so it can itself be a tag 24. The decode
// enters each level, and encode writes the outer bytes again unchanged.
TEST_CASE("decode: a tag 24 inside a tag 24 is decoded at each level")
{
    std::string const wire = "\xd8\x18\x44\xd8\x18\x41\x01"s;
    auto const top_level = cbor::lazy::from(wire);
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    cbor::item const &outer = cbor::encoded_cbor_data_item(root, *root);
    REQUIRE_EQ(outer.type, cbor::type::tag);
    cbor::item const &inner = cbor::encoded_cbor_data_item(root, outer);
    CHECK_EQ(inner.type, cbor::type::unsigned_integer);
    CHECK_EQ(inner.via.u64, 1u);
    std::string again;
    REQUIRE(cbor::encode(*root, again).has_value());
    CHECK_EQ(again, wire);
}

// The tree of a tag 24 lives in the memory of the decode result. The result must keep it, its bytes and their
// owner alive after the lazy and the caller's string are gone, so the address sanitizer sees every read here.
TEST_CASE("decode: the embedded item of a tag 24 lives as long as the result")
{
    std::shared_ptr<cbor::item const> root;
    {
        std::string const wire = "\xd8\x18\x47\xd8\x18\x44\x82\x01\x61x"s;
        root = item_of(cbor::lazy::from(std::string(wire))->decode());
    }
    cbor::item const &outer = cbor::encoded_cbor_data_item(root, *root);
    cbor::item const &inner = cbor::encoded_cbor_data_item(root, outer);
    auto const children = children_of(inner);
    REQUIRE_EQ(children.size(), 2u);
    CHECK_EQ(children[0]->via.u64, 1u);
    CHECK_EQ(children[1]->via.str, "x");
}

// RFC 8949 and the value-sharing tags: a tag 29 names the item of its tag 28, so both reach the same node of the
// tree and not two copies.
TEST_CASE("decode: a tag 29 reaches the same node as its tag 28")
{
    std::string const wire = "\x82\xd8\x1c\x82\x01\x02\xd8\x1d\x00"s;
    auto const root = item_of(cbor::lazy::from(std::string(wire))->decode());
    auto const children = children_of(*root);
    cbor::item const *const marked = children[0]->via.tag.content;
    cbor::item const *const referred = &cbor::shared_value(root, *children[1]);
    CHECK_EQ(marked, referred);
    CHECK_EQ(children_of(*referred)[1]->via.u64, 2u);
}

// Owner decision: the embedded item is a top-level item of its own. A tag 29 inside it refers to a tag 28 inside
// the same embedded item, and the marks outside do not count there.
TEST_CASE("decode: a shared reference inside tag 24 refers to a mark inside the same embedded item")
{
    auto const top_level = cbor::lazy::from(std::string("\xd8\x18\x48\x82\xd8\x1c\x61\x61\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    auto const elements = children_of(cbor::encoded_cbor_data_item(root, *root));
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(&cbor::shared_value(root, *elements[1]), elements[0]->via.tag.content);
}

// The other direction of the same decision: a mark outside is not visible inside, so 29(0) inside the embedded
// item has no mark to refer to.
TEST_CASE("decode: a shared reference inside tag 24 does not see a mark outside")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1c\x61\x61\xd8\x18\x43\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::sharedref_index_not_marked);
}

// And a mark inside the embedded item does not count outside: the 29(0) after the tag 24 has no mark.
TEST_CASE("decode: a mark inside tag 24 does not count outside")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x18\x43\xd8\x1c\x01\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::sharedref_index_not_marked);
}

// The embedded bytes are read with the same checks as the outer bytes, so malformed embedded bytes give the
// error that the same bytes give at the top level.
TEST_CASE("decode: malformed bytes inside tag 24 give their own error")
{
    auto const short_head = cbor::lazy::from(std::string("\xd8\x18\x41\x18"sv));
    REQUIRE(short_head.has_value());
    auto const r = short_head->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::too_little_data);
    auto const reserved = cbor::lazy::from(std::string("\xd8\x18\x41\x1c"sv));
    REQUIRE(reserved.has_value());
    auto const at_top = cbor::lazy::from(std::string("\x1c"sv));
    REQUIRE(at_top.has_value());
    auto const inside = reserved->decode();
    auto const outside = at_top->decode();
    REQUIRE_FALSE(inside.has_value());
    REQUIRE_FALSE(outside.has_value());
    CHECK_EQ(inside.error(), outside.error());
}

// The limits hold across the boundary: the embedded root of 24(<<0>>) is at depth 2, below the byte string, and
// an embedded array counts against the container limit as an outer array does.
TEST_CASE("decode: the limits and the nesting depth count inside tag 24")
{
    auto const top_level = cbor::lazy::from(std::string("\xd8\x18\x41\x00"sv));
    REQUIRE(top_level.has_value());
    {
        test::limits_guard const depth{{.nesting_depth = 1}};
        auto const refused = top_level->decode();
        REQUIRE_FALSE(refused.has_value());
        CHECK_EQ(refused.error(), cbor::error::nesting_depth_exceeded);
    }
    {
        test::limits_guard const depth{{.nesting_depth = 2}};
        CHECK(top_level->decode().has_value());
    }
    auto const array = cbor::lazy::from(std::string("\xd8\x18\x44\x83\x01\x02\x03"sv));
    REQUIRE(array.has_value());
    test::limits_guard const elements{{.container_elements = 2}};
    auto const refused = array->decode();
    REQUIRE_FALSE(refused.has_value());
    CHECK_EQ(refused.error(), cbor::error::container_elements_exceeded);
}

// RFC 8949 3.4.5.1: the byte string of tag 24 holds one encoded data item. decode reads everything and refuses
// bytes after that item with syntax_error; the error is a provisional form that the owner has not decided yet.
// A lazy step and a typed read read only up to their target, by owner decision, so they do not see those bytes.
TEST_CASE("tag 24 with bytes after its data item is refused by decode and not seen by a step")
{
    std::string const wire = "\xd8\x18\x42\x01\x02"s;
    auto const top_level = cbor::lazy::from(wire);
    REQUIRE(top_level.has_value());
    auto const tree = top_level->decode();
    REQUIRE_FALSE(tree.has_value());
    CHECK_EQ(tree.error(), cbor::error::syntax_error);
    CHECK_EQ(*top_level->get<int>(), 1);
    CHECK_EQ(*cbor::at_path<"$", int>(std::string_view(wire)), 1);
    std::string const in_array = "\xd8\x18\x43\x81\x01\x02"s;
    CHECK_EQ(*cbor::lazy::from(in_array)->at(0)->get<int>(), 1);
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
        auto const root = item_of(cbor::lazy::from(std::string(r.wire))->decode());
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
// The expected type is written from the tables of RFC 8949 3.1 and 3.3, and the item must come
// back byte for byte, which also checks the major type that encode takes from the type.
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
                expected = argument == 2   ? cbor::type::unsigned_bignum
                           : argument == 3 ? cbor::type::negative_bignum
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
            auto const top_level = cbor::lazy::from(std::string(wire));
            REQUIRE(top_level.has_value());
            auto const decoded = top_level->decode();
            REQUIRE(decoded.has_value());
            CHECK_EQ((*decoded)->type, expected);
            CHECK_EQ((*decoded)->additional_information, info);
            CHECK_EQ(major_of((*decoded)->type), static_cast<cbor::major_type>(major));
            std::string again;
            REQUIRE(cbor::encode(**decoded, again).has_value());
            CHECK_EQ(again, wire);
        }
    }
}

// The types of RFC 8949 3.4.3 and RFC 8746 are tags: a bignum and a typed array keep major type 6 when they are
// written back.
TEST_CASE("encode: a bignum and a typed array are written back as tags")
{
    for (std::string_view const wire : {"\xc2\x41\x05"sv, "\xc3\x41\x05"sv, "\xd8\x40\x40"sv}) {
        CAPTURE(wire);
        auto const root = item_of(cbor::lazy::from(std::string(wire))->decode());
        CHECK_EQ(major_of(root->type), cbor::major_type::tag);
        std::string again;
        REQUIRE(cbor::encode(*root, again).has_value());
        CHECK_EQ(again, wire);
    }
}

// shared_value and encoded_cbor_data_item take a tag 29 or a tag 24 of the decoded tree that is passed. Any other
// item, or a tree that no decode made, is a wrong use and throws, so a caller never reads an item that does not
// exist.
TEST_CASE("shared_value: an item that is not a tag 29 of the tree throws")
{
    auto const root = item_of(cbor::lazy::from(std::string("\x82\xd8\x1c\x01\xd8\x1d\x00"sv))->decode());
    CHECK_THROWS_AS((void)cbor::shared_value(root, *root), std::logic_error);
    CHECK_THROWS_AS((void)cbor::shared_value(root, root->via.array[0]), std::logic_error);
    CHECK_THROWS_AS((void)cbor::encoded_cbor_data_item(root, root->via.array[1]), std::logic_error);
    auto const other = item_of(cbor::lazy::from(std::string("\x82\xd8\x1c\x01\xd8\x1d\x00"sv))->decode());
    CHECK_THROWS_AS((void)cbor::shared_value(other, root->via.array[1]), std::logic_error);
    auto const made = std::make_shared<cbor::item const>(cbor::item{cbor::type::unsigned_integer, 0, {.u64 = 0}});
    CHECK_THROWS_AS((void)cbor::shared_value(made, *made), std::logic_error);
    CHECK_THROWS_AS((void)cbor::shared_value(nullptr, *made), std::logic_error);
    CHECK_EQ(&cbor::shared_value(root, root->via.array[1]), root->via.array[0].via.tag.content);
}

// A map holds a key and a value for each entry. A map item built by hand with an odd count of items is a wrong
// use, and encode throws before it writes a head with the wrong count.
TEST_CASE("encode: a map with an odd count of items throws")
{
    cbor::item const key{cbor::type::unsigned_integer, 1, {.u64 = 1}};
    cbor::item const odd{cbor::type::map, 0, {.map = std::span(&key, 1)}};
    std::string out;
    CHECK_THROWS_AS((void)cbor::encode(odd, out), std::logic_error);
}

}
