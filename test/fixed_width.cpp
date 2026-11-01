#include "reference.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

using namespace std::string_view_literals;

namespace
{

template <class F>
std::string encoded(F const &f)
{
    std::string out;
    cbor::encoder e{out};
    REQUIRE(f(e).has_value());
    REQUIRE(e.finish().has_value());
    return out;
}

} // namespace

// A schema fixes the width of a field, so a reader finds every field at an offset that the compiler knows.
// The argument therefore has the width of the type, also where RFC 8949 3 permits a shorter one.
TEST_CASE("unsigned_integer: the width of the type, also for a small value")
{
    CHECK_EQ(encoded([](auto &e) { return e.unsigned_integer(std::uint8_t{0}); }), "\x18\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.unsigned_integer(std::uint16_t{5}); }), "\x19\x00\x05"sv);
    CHECK_EQ(encoded([](auto &e) { return e.unsigned_integer(std::uint32_t{7}); }), "\x1a\x00\x00\x00\x07"sv);
    CHECK_EQ(encoded([](auto &e) { return e.unsigned_integer(std::uint64_t{1}); }),
             "\x1b\x00\x00\x00\x00\x00\x00\x00\x01"sv);
    CHECK_EQ(encoded([](auto &e) { return e.unsigned_integer(std::numeric_limits<std::uint64_t>::max()); }),
             "\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// RFC 8949 3.3: additional information 26 is a binary32 and 27 a binary64. A float stays a binary32 and a
// double a binary64, also where a shorter form would hold the value exactly.
TEST_CASE("floating_point: binary32 for float, binary64 for double")
{
    CHECK_EQ(encoded([](auto &e) { return e.floating_point(1.5f); }), "\xfa\x3f\xc0\x00\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.floating_point(0.0f); }), "\xfa\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.floating_point(1.5); }), "\xfb\x3f\xf8\x00\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.floating_point(-0.0); }), "\xfb\x80\x00\x00\x00\x00\x00\x00\x00"sv);
}

// RFC 8949 3.3: false, true, null and undefined are the simple values 20 to 23, each one byte, 0xf4 to 0xf7.
// The test takes every member of cbor::simple_value, because the input is that finite set.
TEST_CASE("simple_value: one byte for each simple value")
{
    using cbor::simple_value;
    CHECK_EQ(encoded([](auto &e) { return e.simple_value(simple_value::false_value); }), "\xf4"sv);
    CHECK_EQ(encoded([](auto &e) { return e.simple_value(simple_value::true_value); }), "\xf5"sv);
    CHECK_EQ(encoded([](auto &e) { return e.simple_value(simple_value::null); }), "\xf6"sv);
    CHECK_EQ(encoded([](auto &e) { return e.simple_value(simple_value::undefined); }), "\xf7"sv);
}

// RFC 8949 3.3: simple values 24 to 31 have no well-formed encoding. A value from 32 up follows 0xf8.
// A refusal ends the encoder, so each value takes an encoder of its own, and a call after the refusal is a
// wrong use.
TEST_CASE("simple_value: a value from 24 to 31 is refused")
{
    for (unsigned const v : {24u, 31u}) {
        CAPTURE(v);
        std::string out;
        cbor::encoder e{out};
        CHECK_EQ(e.simple_value(static_cast<cbor::simple_value>(v)).error(), cbor::error::reserved_simple_value);
        CHECK_THROWS_AS(std::ignore = e.simple_value(cbor::simple_value::null), std::logic_error);
    }
    CHECK_EQ(encoded([](auto &e) { return e.simple_value(static_cast<cbor::simple_value>(32)); }), "\xf8\x20"sv);
}
