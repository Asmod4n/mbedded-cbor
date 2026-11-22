#include "reference.hpp"
#include "owned_copy.hpp"
#include "item_head.hpp"
#include "rfc8949.hpp"
#include <cbor/structs.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

// RFC 8949 3.3: a simple value 0 to 23 is the additional information of a one-byte head. A simple value 24 to 255
// follows 0xf8 in one byte.
std::string wire_of(std::uint8_t const v)
{
    if (v < 24)
        return std::string(1, static_cast<char>(0xe0 | v));
    return "\xf8"s + static_cast<char>(v);
}

// RFC 8949 3.3 and Appendix F: 0xf8 with a value below 32 is not well-formed.
bool reserved(std::uint8_t const v)
{
    return v >= 24 && v < 32;
}

std::string edn_of(std::uint8_t const v)
{
    switch (v) {
    case 20:
        return "false";
    case 21:
        return "true";
    case 22:
        return "null";
    case 23:
        return "undefined";
    default:
        return "simple(" + std::to_string(v) + ")";
    }
}

} // namespace

// The input is the finite set 0 to 255. So the test takes every member. lazy::get gives a simple value as
// cbor::simple_value, also one that has no name in Table 4.
TEST_CASE("simple value: lazy::get gives every simple value 0 to 255")
{
    for (unsigned i = 0; i < test::rfc8949::one_byte_values; ++i) {
        auto const v = static_cast<std::uint8_t>(i);
        CAPTURE(i);
        auto const top_level = test::make_lazy_from_copy(wire_of(v));
        if (reserved(v)) {
            if (top_level.has_value()) {
                auto const read = top_level->get<cbor::simple_value>();
                CHECK_EQ(read.error(), error::syntax_error);
            } else {
                CHECK_EQ(top_level.error(), error::syntax_error);
            }
            continue;
        }
        REQUIRE(top_level.has_value());
        auto const r = top_level->get<cbor::simple_value>();
        REQUIRE(r.has_value());
        CHECK_EQ(std::to_underlying(*r), v);
    }
}

// A simple value is major type 7, but a float of major type 7 is no simple value, and an integer is none
// either.
TEST_CASE("simple value: lazy::get refuses a float and an integer")
{
    for (auto const &wire : {"\xf9\x3c\x00"s, "\xfa\x47\xc3\x50\x00"s,
                             "\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"s, "\x01"s, "\x14"s}) {
        auto const top_level = test::make_lazy_from_copy(wire);
        REQUIRE(top_level.has_value());
        auto const read = top_level->get<cbor::simple_value>();
        CHECK_EQ(read.error(), error::incorrect_type);
    }
}

// at_path reads the same set from the bytes with no lazy.
TEST_CASE("simple value: at_path gives every simple value 0 to 255")
{
    for (unsigned i = 0; i < test::rfc8949::one_byte_values; ++i) {
        auto const v = static_cast<std::uint8_t>(i);
        CAPTURE(i);
        auto const r = cbor::at_path<"$[0]", cbor::simple_value>("\x81"s + wire_of(v));
        if (reserved(v)) {
            CHECK_EQ(r.error(), error::syntax_error);
            continue;
        }
        REQUIRE(r.has_value());
        CHECK_EQ(std::to_underlying(*r), v);
    }
    CHECK_EQ((cbor::at_path<"$[0]", cbor::simple_value>("\x81\xf9\x3c\x00"sv)),
             std::unexpected(error::incorrect_type));
    CHECK_EQ((cbor::at_path<"$[0]", cbor::simple_value>("\x81\x01"sv)), std::unexpected(error::incorrect_type));
}

// lazy::to_item gives an item whose content is the simple value, beside the head fields of the wire.
TEST_CASE("simple value: lazy::to_item gives every simple value 0 to 255 as its content")
{
    for (unsigned i = 0; i < test::rfc8949::one_byte_values; ++i) {
        auto const v = static_cast<std::uint8_t>(i);
        CAPTURE(i);
        auto const top_level = test::make_lazy_from_copy(wire_of(v));
        if (reserved(v)) {
            if (top_level.has_value())
                CHECK_EQ(top_level->to_item().error(), error::syntax_error);
            else
                CHECK_EQ(top_level.error(), error::syntax_error);
            continue;
        }
        REQUIRE(top_level.has_value());
        auto const r = top_level->to_item();
        REQUIRE(r.has_value());
        cbor::item const it = **r;
        CHECK_EQ(find_major_type_of(it.type()), cbor::major_type::simple);
        CHECK_EQ(it.additional_information(), v < 24 ? v : 24);
        CHECK_EQ(read_argument_of_item(it), v);
    }
}

// decode gives every simple value to the reference model, and cbor::encode writes the decoded tree back in the same
// bytes.
TEST_CASE("simple value: decode and encode for every simple value 0 to 255")
{
    for (unsigned i = 0; i < test::rfc8949::one_byte_values; ++i) {
        auto const v = static_cast<std::uint8_t>(i);
        CAPTURE(i);
        if (reserved(v)) {
            CHECK_EQ(read_decode_error_of_wire(wire_of(v)), error::syntax_error);
            continue;
        }
        check_both(wire_of(v), V(simple{v}));
    }
}

// diagnostic_notation writes the four names of Table 4 and simple(n) for every other simple value (RFC 8949 8).
TEST_CASE("simple value: diagnostic_notation gives every simple value 0 to 255")
{
    for (unsigned i = 0; i < test::rfc8949::one_byte_values; ++i) {
        auto const v = static_cast<std::uint8_t>(i);
        CAPTURE(i);
        auto const r = cbor::diagnostic_notation(wire_of(v));
        if (reserved(v)) {
            CHECK_EQ(r.error(), error::syntax_error);
            continue;
        }
        REQUIRE(r.has_value());
        CHECK_EQ(*r, edn_of(v));
    }
}

// structs reads and writes cbor::simple_value with the same bytes.
TEST_CASE("simple value: structs reads and writes every simple value 0 to 255")
{
    for (unsigned i = 0; i < test::rfc8949::one_byte_values; ++i) {
        auto const v = static_cast<std::uint8_t>(i);
        CAPTURE(i);
        auto const r = cbor::structs<cbor::simple_value>::decode(wire_of(v));
        if (reserved(v)) {
            CHECK_EQ(r.error(), error::syntax_error);
            continue;
        }
        REQUIRE(r.has_value());
        CHECK_EQ(std::to_underlying(**r), v);
        auto const out = cbor::encode<cbor::simple_value>(static_cast<cbor::simple_value>(v));
        REQUIRE(out.has_value());
        CHECK_EQ(*out, wire_of(v));
    }
}
