#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "nesting_depth_max_guard.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
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

cbor::item const *address_of(std::expected<std::reference_wrapper<cbor::item const>, cbor::error> const &r)
{
    REQUIRE(r.has_value());
    return &r->get();
}

// The top-level item is the cache: each node becomes an item once, so a
// second decode gives the same address and builds nothing new.
TEST_CASE("decode: a node decoded twice is the same item")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\x01\x62hi"sv));
    REQUIRE(top_level.has_value());
    cbor::item const *const first = address_of(top_level->decode());
    CHECK_EQ(address_of(top_level->decode()), first);
}

// A child reached through its own lazy is the node that its parent
// already holds, so both paths give one address.
TEST_CASE("decode: a child decoded through its lazy is the item its parent holds")
{
    auto const top_level = cbor::lazy::from(std::string("\xa1\x61k\x82\x01\x02"sv));
    REQUIRE(top_level.has_value());
    cbor::item const *const map = address_of(top_level->decode());
    CHECK_EQ(map->argument, 1u);
    auto const entries = top_level->entries();
    REQUIRE(entries.has_value());
    auto const entry = *entries->begin();
    REQUIRE(entry.has_value());
    auto const value = top_level->at("k");
    REQUIRE(value.has_value());
    CHECK_EQ(address_of(value->decode()), address_of(entry->second.decode()));
}

// RFC 8949 3.1: an array is its count and its encoded elements. The item
// holds the count in its argument and a view on the bytes of the
// elements, so it allocates nothing for them.
TEST_CASE("decode: an array is its count and a view on the bytes of its elements")
{
    std::string const bytes = "\x82\x01\x62hi"s;
    auto const top_level = cbor::lazy::from(std::make_shared<std::string const>(bytes));
    REQUIRE(top_level.has_value());
    cbor::item const *const array = address_of(top_level->decode());
    CHECK_EQ(array->argument, 2u);
    auto const elements = std::get<std::span<std::byte const>>(array->content);
    REQUIRE_EQ(elements.size(), 4u);
    CHECK_EQ(elements[0], std::byte{0x01});
    CHECK_EQ(elements[1], std::byte{0x62});
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

template <class Lazy>
concept decodable = requires(Lazy &&l) { std::forward<Lazy>(l).decode(); };

// A reference to an item lives only as long as the top-level item that
// holds it, so decode on a temporary lazy does not compile.
TEST_CASE("decode: a temporary lazy gives no item")
{
    CHECK_FALSE(decodable<cbor::lazy>);
    CHECK_FALSE(decodable<cbor::lazy const>);
    CHECK(decodable<cbor::lazy const &>);
}

// The order does not matter: a child decoded first is taken by its
// parent later, not built a second time.
TEST_CASE("decode: a child decoded before its parent is the item its parent takes")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\x01\x82\x02\x03"sv));
    REQUIRE(top_level.has_value());
    auto const child = top_level->at(1);
    REQUIRE(child.has_value());
    cbor::item const *const inner = address_of(child->decode());
    REQUIRE(top_level->decode().has_value());
    auto const again = top_level->at(1);
    REQUIRE(again.has_value());
    CHECK_EQ(address_of(again->decode()), inner);
}

// RFC 8949 3.4 and the value sharing tags 28 and 29: a reference is the
// value it names. The item of 29(0) is the item of the shared value, so a
// reader sees one object and never a copy.
TEST_CASE("decode: a shared reference is the item of the shared value")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1c\x62hi\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    REQUIRE(top_level->decode().has_value());
    auto const shared = top_level->at(0);
    REQUIRE(shared.has_value());
    cbor::item const *const value = address_of(shared->decode());
    CHECK_EQ(std::get<std::string_view>(value->content), "hi");
    auto const reference = top_level->at(1);
    REQUIRE(reference.has_value());
    CHECK_EQ(address_of(reference->decode()), value);
}

// A cycle comes from the wire: 28([29(0)]) is an array that holds
// itself. The array is filled in place, so its element is its own
// address and the decode ends.
TEST_CASE("decode: a cyclic array holds its own address")
{
    auto const top_level = cbor::lazy::from(std::string("\xd8\x1c\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    cbor::item const *const array = address_of(top_level->decode());
    auto const elements = top_level->elements();
    REQUIRE(elements.has_value());
    auto const element = *elements->begin();
    REQUIRE(element.has_value());
    CHECK_EQ(address_of(element->decode()), array);
}

// A reference to a value that comes later on the wire is refused, as in
// every other reader of the shared values. The elements of an array are
// read on access. The decode of the array has recorded the mark that
// lies after the reference, so the reference is not complete, as in lazy.
TEST_CASE("decode: a forward shared reference is refused")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1d\x00\xd8\x1c\x00"sv));
    REQUIRE(top_level.has_value());
    REQUIRE(top_level->decode().has_value());
    auto const element = top_level->at(0);
    REQUIRE(element.has_value());
    auto const r = element->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::sharedref_not_complete);
}

// The nesting depth is checked where the decode recurses: 100(100(100(0))) holds an
// integer at depth 3. An array or a map is not entered by the decode, so it adds no depth there.
TEST_CASE("decode: the nesting depth is limited by the nesting depth in force")
{
    auto const top_level = cbor::lazy::from(std::string("\xd8\x64\xd8\x64\xd8\x64\x00"sv));
    REQUIRE(top_level.has_value());
    {
        test::nesting_depth_max_guard const depth{2};
        auto const refused = top_level->decode();
        REQUIRE_FALSE(refused.has_value());
        CHECK_EQ(refused.error(), cbor::error::nesting_depth_exceeded);
    }
    test::nesting_depth_max_guard const depth{3};
    CHECK(top_level->decode().has_value());
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
    cbor::item const *const f = address_of(top_level->decode());
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
