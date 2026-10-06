#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace item_test
{

// The type number of an item is the initial byte of RFC 8949 section 3
// with additional information 0. The expected numbers are written from
// the major type times 32, and not from the code.
TEST_CASE("initial_byte gives major type times 32 for major types 0 to 6")
{
    cbor::item const element{std::uint64_t{1}};
    cbor::item const key{std::string_view{"a"}};
    std::byte const bytes[1]{};

    CHECK(cbor::item{std::uint64_t{0}}.initial_byte() == 0);
    CHECK(cbor::item{std::numeric_limits<std::uint64_t>::max()}.initial_byte() == 0);
    CHECK(cbor::item{cbor::negative_integer{0}}.initial_byte() == 32);
    CHECK(cbor::item{std::span<std::byte const>{bytes}}.initial_byte() == 64);
    CHECK(cbor::item{std::string_view{"abc"}}.initial_byte() == 96);
    CHECK(cbor::item{std::vector<cbor::item const *>{&element}}.initial_byte() == 128);
    CHECK(cbor::item{std::vector<std::pair<cbor::item const *, cbor::item const *>>{{&key, &element}}}
              .initial_byte() == 160);
    CHECK(cbor::item{cbor::tag{1, &element}}.initial_byte() == 192);
}

// A record is a tag whose number is registered. On the wire it is still
// major type 6, so a reader of the type number sees a tag.
TEST_CASE("initial_byte of a record is the initial byte of a tag")
{
    cbor::item const field{std::uint64_t{7}};
    cbor::item const r{cbor::record{40000, {{"x", &field}}}};
    CHECK(r.initial_byte() == 192);
}

// RFC 8949 3.3 Table 3: major type 7 carries the simple value 0 to 23 in
// the additional information itself, and false, true, null and undefined
// are the values 20 to 23. Every member of that set is checked.
TEST_CASE("initial_byte of bool, null and a simple value below 24 is 224 plus the value")
{
    CHECK(cbor::item{false}.initial_byte() == 244);
    CHECK(cbor::item{true}.initial_byte() == 245);
    CHECK(cbor::item{nullptr}.initial_byte() == 246);
    CHECK(cbor::item{cbor::simple_value::undefined}.initial_byte() == 247);
    for (std::uint8_t v = 0; v < 24; ++v)
        CHECK(cbor::item{cbor::simple_value{v}}.initial_byte() == 224 + v);
}

// RFC 8949 3.3 Table 3: additional information 24 says that the simple
// value 32 to 255 follows in one byte. Every member of that set is
// checked.
TEST_CASE("initial_byte of a simple value from 32 to 255 is 248")
{
    for (unsigned v = 32; v < 256; ++v)
        CHECK(cbor::item{cbor::simple_value{static_cast<std::uint8_t>(v)}}.initial_byte() == 248);
}

// A double does not keep the width it had on the wire. The width given
// is the shortest that keeps the value, as RFC 8949 4.2.2 prefers.
TEST_CASE("initial_byte of a float is the shortest width that keeps the value")
{
    CHECK(cbor::item{0.0}.initial_byte() == 249);
    CHECK(cbor::item{1.5}.initial_byte() == 249);
    CHECK(cbor::item{std::numeric_limits<double>::infinity()}.initial_byte() == 249);
    CHECK(cbor::item{std::numeric_limits<double>::quiet_NaN()}.initial_byte() == 249);
    CHECK(cbor::item{100000.0}.initial_byte() == 250);
    CHECK(cbor::item{1.1}.initial_byte() == 251);
}

// A node that is not built yet has no major type. 252 is additional
// information 28 of major type 7, which RFC 8949 3.3 reserves and which
// is never well-formed, so no built item gives it.
TEST_CASE("initial_byte of an item that is not built yet is 252")
{
    CHECK(cbor::item{cbor::lazy{{}, 0}}.initial_byte() == 252);
}

}
