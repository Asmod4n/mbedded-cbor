#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
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
// still major type 6 on the wire, so the head fields say tag.
TEST_CASE("a record is a tag in its head fields")
{
    cbor::item const x{cbor::major_type::unsigned_integer, 7, 7, {}};
    cbor::item const r{cbor::major_type::tag, 25, 40000, cbor::record{40000, {{"x", &x}}}};
    CHECK(r.major_type == cbor::major_type::tag);
    CHECK(std::get<cbor::record>(r.content).fields.at(0).second == &x);
}

using namespace std::string_view_literals;

cbor::item const *address_of(cbor::result<std::reference_wrapper<cbor::item const>> const &r)
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
    auto const &entries = std::get<std::vector<std::pair<cbor::item const *, cbor::item const *>>>(map->content);
    REQUIRE_EQ(entries.size(), 1);
    auto const value = top_level->at("k");
    REQUIRE(value.has_value());
    CHECK_EQ(address_of(value->decode()), entries[0].second);
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
    cbor::item const *const outer = address_of(top_level->decode());
    CHECK_EQ(std::get<std::vector<cbor::item const *>>(outer->content)[1], inner);
}

// RFC 8949 3.4 and the value sharing tags 28 and 29: a reference is the
// value it names. The item of 29(0) is the item of the shared value, so a
// reader sees one object and never a copy.
TEST_CASE("decode: a shared reference is the item of the shared value")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1c\x62hi\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    cbor::item const *const array = address_of(top_level->decode());
    auto const &elements = std::get<std::vector<cbor::item const *>>(array->content);
    REQUIRE_EQ(elements.size(), 2);
    CHECK_EQ(elements[0], elements[1]);
    CHECK_EQ(std::get<std::string_view>(elements[0]->content), "hi");
    auto const reference = top_level->at(1);
    REQUIRE(reference.has_value());
    CHECK_EQ(address_of(reference->decode()), elements[0]);
}

// A cycle comes from the wire: 28([29(0)]) is an array that holds
// itself. The array is filled in place, so its element is its own
// address and the decode ends.
TEST_CASE("decode: a cyclic array holds its own address")
{
    auto const top_level = cbor::lazy::from(std::string("\xd8\x1c\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    cbor::item const *const array = address_of(top_level->decode());
    auto const &elements = std::get<std::vector<cbor::item const *>>(array->content);
    REQUIRE_EQ(elements.size(), 1);
    CHECK_EQ(elements[0], array);
}

// A reference to a value that comes later on the wire is refused, as in
// every other reader of the shared values.
TEST_CASE("decode: a forward shared reference is refused")
{
    auto const top_level = cbor::lazy::from(std::string("\x82\xd8\x1d\x00\xd8\x1c\x00"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->decode();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::sharedref_index_not_marked);
}

// The nesting depth is checked as in every decoder: [[[0]]] holds an
// integer at depth 3.
TEST_CASE("decode: the nesting depth is limited by DepthMax")
{
    auto const top_level = cbor::lazy::from(std::string("\x81\x81\x81\x00"sv));
    REQUIRE(top_level.has_value());
    auto const refused = top_level->decode<2>();
    REQUIRE_FALSE(refused.has_value());
    CHECK_EQ(refused.error(), cbor::error::nesting_depth_exceeded);
    CHECK(top_level->decode<3>().has_value());
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
