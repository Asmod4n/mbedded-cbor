#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
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

}
