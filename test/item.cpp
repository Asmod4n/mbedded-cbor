#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

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
// additional information and argument. An integer is its head alone, so
// an item of major type 1 gives -1 - argument from the fields.
TEST_CASE("an integer is the head fields and nothing else")
{
    cbor::item const minus_500{cbor::major_type::negative_integer, 25, 499, {}};
    CHECK(minus_500.major_type == cbor::major_type::negative_integer);
    CHECK(minus_500.additional_information == 25);
    CHECK(-1 - static_cast<std::int64_t>(minus_500.argument) == -500);
    CHECK(std::holds_alternative<std::monostate>(minus_500.content));
}

// RFC 8949 3.4: a tag is major type 6 with the tag number as argument.
// The content points to the item of the tag content, so a reader goes
// from the tag to its content by the address.
TEST_CASE("a tag holds its number as argument and points to its content")
{
    cbor::item const epoch{cbor::major_type::unsigned_integer, 26, 1363896240, {}};
    cbor::item const time{cbor::major_type::tag, 1, 1, &epoch};
    CHECK(time.argument == 1);
    CHECK(std::get<cbor::item const *>(time.content) == &epoch);
}

// RFC 8949 3.3 Table 3: major type 7 says by its additional information
// what the item is. A float keeps the width it had on the wire in the
// additional information, and its value in the type of that width.
TEST_CASE("a float keeps its width in the additional information and its value in that width")
{
#if defined(__STDCPP_FLOAT16_T__)
    using half = std::float16_t;
#else
    using half = float;
#endif
    cbor::item const f16{cbor::major_type::simple_float, 25, 0x3e00, half(1.5)};
    cbor::item const f32{cbor::major_type::simple_float, 26, 0x47c35000, 100000.0f};
    cbor::item const f64{cbor::major_type::simple_float, 27, 0x3ff199999999999a, 1.1};
    CHECK(f16.additional_information == 25);
    CHECK(std::get<half>(f16.content) == half(1.5));
    CHECK(f32.additional_information == 26);
    CHECK(std::get<float>(f32.content) == 100000.0f);
    CHECK(f64.additional_information == 27);
    CHECK(std::get<double>(f64.content) == 1.1);
}

// undefined is a value of its own, apart from null, so that a message
// from JavaScript passes through unchanged. The two differ only in the
// additional information.
TEST_CASE("null and undefined are two items")
{
    cbor::item const null{cbor::major_type::simple_float, 22, 22, {}};
    cbor::item const undefined{cbor::major_type::simple_float, 23, 23, {}};
    CHECK(null.additional_information != undefined.additional_information);
}

// A tag whose number is registered decodes to a record. The record is
// still major type 6 on the wire, so the head fields say tag. The names
// come from the registry and the fields are a view on the encoded bytes,
// so the record owns no memory.
TEST_CASE("a record is a tag in its head fields")
{
    static constexpr std::string_view names[] = {"x"};
    static constexpr std::byte fields[] = {std::byte{0x07}};
    cbor::item const r{cbor::major_type::tag, 25, 40000, cbor::record{40000, names, fields}};
    CHECK(r.major_type == cbor::major_type::tag);
    CHECK_EQ(std::get<cbor::record>(r.content).names[0], "x"sv);
    CHECK_EQ(std::get<cbor::record>(r.content).fields.data(), fields);
}

// An item owns no memory, so a cache of many items costs only their own
// size. Each alternative of the content is an immediate value or a view.
TEST_CASE("an item owns no memory")
{
    CHECK(std::is_trivially_destructible_v<cbor::record>);
    CHECK(std::is_trivially_copyable_v<std::span<std::byte const>>);
}

std::shared_ptr<cbor::item const> item_of(std::expected<std::shared_ptr<cbor::item const>, cbor::error> const &r)
{
    REQUIRE(r.has_value());
    return *r;
}

// The children of an array or a map, read through the public accessor.
std::span<cbor::item const *const> children_of(cbor::item const &container)
{
    auto const *const d = std::get_if<cbor::data_items>(&container.content);
    REQUIRE(d != nullptr);
    return d->span();
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
    CHECK_EQ((*first)->argument, (*second)->argument);
    auto const a = children_of(**first);
    auto const b = children_of(**second);
    REQUIRE_EQ(a.size(), 2u);
    REQUIRE_EQ(b.size(), 2u);
    CHECK_NE(a[1], b[1]);
    CHECK_EQ(std::get<std::string_view>(a[1]->content).data(), std::get<std::string_view>(b[1]->content).data());
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
    CHECK_EQ((*r)->major_type, cbor::major_type::array);
    CHECK_EQ((*r)->argument, 3u);
    auto const elements = children_of(**r);
    REQUIRE_EQ(elements.size(), 3u);
    CHECK_EQ(elements[2]->argument, 3u);
}

// A child reached through two paths decodes to the same content.
TEST_CASE("decode: a child decoded through two paths has one content")
{
    auto const top_level = cbor::lazy::from(std::string("\xa1\x61k\x82\x01\x02"sv));
    REQUIRE(top_level.has_value());
    auto const map = item_of(top_level->decode());
    CHECK_EQ(map->argument, 1u);
    auto const entries = top_level->entries();
    REQUIRE(entries.has_value());
    auto const entry = *entries->begin();
    REQUIRE(entry.has_value());
    auto const value = top_level->at("k");
    REQUIRE(value.has_value());
    auto const by_key = item_of(value->decode());
    auto const by_entry = item_of(entry->second.decode());
    CHECK_EQ(by_key->argument, by_entry->argument);
    REQUIRE_EQ(children_of(*by_key).size(), 2u);
    REQUIRE_EQ(children_of(*by_entry).size(), 2u);
    CHECK_EQ(children_of(*by_key)[1]->argument, children_of(*by_entry)[1]->argument);
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
    CHECK_EQ(elements[0]->argument, 1u);
    CHECK_EQ(elements[1]->major_type, cbor::major_type::map);
    auto const pairs = children_of(*elements[1]);
    REQUIRE_EQ(pairs.size(), 4u);
    CHECK_EQ(std::get<std::string_view>(pairs[0]->content), "k");
    CHECK_EQ(std::get<std::string_view>(pairs[1]->content), "hi");
    CHECK_EQ(pairs[2]->argument, 2u);
    CHECK_EQ(pairs[3]->major_type, cbor::major_type::array);
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
    CHECK_EQ(first[0]->argument, 1u);
    auto const inner = children_of(*first[1]);
    REQUIRE_EQ(inner.size(), 2u);
    CHECK_EQ(inner[0]->argument, 2u);
    CHECK_EQ(inner[1]->argument, 3u);
    auto const pairs = children_of(*outer[1]);
    REQUIRE_EQ(pairs.size(), 2u);
    CHECK_EQ(pairs[0]->argument, 4u);
    auto const value = children_of(*pairs[1]);
    REQUIRE_EQ(value.size(), 1u);
    CHECK_EQ(value[0]->argument, 5u);
    CHECK(children_of(*outer[2]).empty());
    CHECK_EQ(outer[3]->argument, 6u);
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
            sum += c->argument;
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
    CHECK_EQ(elements[0]->major_type, cbor::major_type::tag);
    CHECK_EQ(elements[0]->argument, 2u);
    auto const *const magnitude = std::get<cbor::item const *>(elements[0]->content);
    CHECK_EQ(std::get<std::span<std::byte const>>(magnitude->content).size(), 9u);
    CHECK_EQ(elements[1]->argument, 64u);
    auto const *const typed = std::get<cbor::item const *>(elements[1]->content);
    CHECK_EQ(std::get<std::span<std::byte const>>(typed->content).size(), 2u);
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
    CHECK_EQ((*r)->major_type, cbor::major_type::unsigned_integer);
    CHECK_EQ((*r)->argument, 1u);
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
    CHECK_EQ(later->argument, inner->argument);
    CHECK_EQ(children_of(*later)[1]->argument, children_of(*inner)[1]->argument);
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
    CHECK_EQ(elements[0]->major_type, cbor::major_type::tag);
    CHECK_EQ(elements[0]->argument, 28u);
    auto const *const value = std::get<cbor::item const *>(elements[0]->content);
    CHECK_EQ(std::get<std::string_view>(value->content), "hi");
    CHECK_EQ(elements[1]->argument, 29u);
    auto const reference = std::get<cbor::sharedref>(elements[1]->content);
    CHECK_EQ(reference.shareable, value);
    CHECK_EQ(reference.argument, 0u);
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
    auto const reference = std::get<cbor::sharedref>(children_of(*inner)[0]->content);
    CHECK_EQ(std::get<std::string_view>(reference.shareable->content), "hi");
}

// A cycle comes from the wire: 28([29(0)]) is an array that holds
// itself. The element points to that array, and the decode ends.
TEST_CASE("decode: a cyclic array is its own element")
{
    auto const top_level = cbor::lazy::from(std::string("\xd8\x1c\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->decode());
    auto const *const array = std::get<cbor::item const *>(root->content);
    CHECK_EQ(array->major_type, cbor::major_type::array);
    auto const elements = children_of(*array);
    REQUIRE_EQ(elements.size(), 1u);
    CHECK_EQ(std::get<cbor::sharedref>(elements[0]->content).shareable, array);
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
    cbor::item const *at = std::get<cbor::item const *>(decoded->content);
    std::size_t steps = 0;
    while (auto const *r = std::get_if<cbor::sharedref>(&at->content)) {
        at = r->shareable;
        if (++steps > chain_length)
            break;
    }
    CHECK_EQ(steps, chain_length - 1u);
    CHECK_EQ(at->major_type, cbor::major_type::unsigned_integer);
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
    CHECK_EQ(f->major_type, cbor::major_type::simple_float);
    CHECK_EQ(f->additional_information, 25);
    CHECK_EQ(f->argument, 0x3e00);
#if defined(__STDCPP_FLOAT16_T__)
    CHECK_EQ(std::bit_cast<std::uint16_t>(std::get<std::float16_t>(f->content)), 0x3e00);
#else
    CHECK_EQ(std::bit_cast<std::uint32_t>(std::get<float>(f->content)), 0x3fc00000);
#endif
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
        auto const by_item = item_of(l->decode())->type();
        REQUIRE(by_item.has_value());
        CHECK_EQ(*by_item, r.expected);
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
    CHECK_EQ(*elements[0]->type(), cbor::type::tag);
    CHECK_EQ(*elements[1]->type(), cbor::type::tag);
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

}
