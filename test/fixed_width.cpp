#include "host.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace
{

template <class F>
std::string encoded(F const &f)
{
    string_writer w;
    cbor::encoder<string_writer> e{w};
    REQUIRE(f(e).has_value());
    REQUIRE(e.flush().has_value());
    return w.bytes;
}

} // namespace

// A schema fixes the width of a field, so a reader finds every field at an offset that the compiler knows.
// The argument therefore has the width of the type, also where RFC 8949 3 permits a shorter one.
TEST_CASE("fixed_width_unsigned_encode: the width of the type, also for a small value")
{
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_unsigned_encode(std::uint8_t{0}); }), "\x18\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_unsigned_encode(std::uint16_t{5}); }), "\x19\x00\x05"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_unsigned_encode(std::uint32_t{7}); }),
             "\x1a\x00\x00\x00\x07"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_unsigned_encode(std::uint64_t{1}); }),
             "\x1b\x00\x00\x00\x00\x00\x00\x00\x01"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_unsigned_encode(std::numeric_limits<std::uint64_t>::max()); }),
             "\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// RFC 8949 3.1 puts the sign into the major type and encodes a negative n as -1 - n. The width stays that of
// the type. The smallest value of each type is the edge where -1 - n needs every bit.
TEST_CASE("fixed_width_signed_encode: major type 0 or 1, the width of the type")
{
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_signed_encode(std::int8_t{0}); }), "\x18\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_signed_encode(std::int8_t{-1}); }), "\x38\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_signed_encode(std::numeric_limits<std::int8_t>::min()); }),
             "\x38\x7f"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_signed_encode(std::int16_t{5}); }), "\x19\x00\x05"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_signed_encode(std::int16_t{-5}); }), "\x39\x00\x04"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_signed_encode(std::int32_t{-500}); }),
             "\x3a\x00\x00\x01\xf3"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_signed_encode(std::numeric_limits<std::int64_t>::max()); }),
             "\x1b\x7f\xff\xff\xff\xff\xff\xff\xff"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_signed_encode(std::numeric_limits<std::int64_t>::min()); }),
             "\x3b\x7f\xff\xff\xff\xff\xff\xff\xff"sv);
}

// RFC 8949 3.3: additional information 26 is a binary32 and 27 a binary64. A float stays a binary32 and a
// double a binary64, also where a shorter form would hold the value exactly.
TEST_CASE("fixed_width_float_encode: binary32 for float, binary64 for double")
{
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_float_encode(1.5f); }), "\xfa\x3f\xc0\x00\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_float_encode(0.0f); }), "\xfa\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_float_encode(1.5); }),
             "\xfb\x3f\xf8\x00\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded([](auto &e) { return e.fixed_width_float_encode(-0.0); }),
             "\xfb\x80\x00\x00\x00\x00\x00\x00\x00"sv);
}

// RFC 8949 3.3: false, true, null and undefined are the simple values 20 to 23, each one byte, 0xf4 to 0xf7.
// The test takes every member of cbor::simple_value, because the input is that finite set.
TEST_CASE("simple_value_encode: one byte for each simple value")
{
    using cbor::simple_value;
    CHECK_EQ(encoded([](auto &e) { return e.simple_value_encode(simple_value::false_value); }), "\xf4"sv);
    CHECK_EQ(encoded([](auto &e) { return e.simple_value_encode(simple_value::true_value); }), "\xf5"sv);
    CHECK_EQ(encoded([](auto &e) { return e.simple_value_encode(simple_value::null); }), "\xf6"sv);
    CHECK_EQ(encoded([](auto &e) { return e.simple_value_encode(simple_value::undefined); }), "\xf7"sv);
}

// RFC 8949 3.3: simple values 24 to 31 are not written in one byte; 24 to 31 after 0xf8 are reserved as well.
// A value outside the enumerators is refused instead of written.
TEST_CASE("simple_value_encode: a value from 24 up is refused")
{
    string_writer w;
    cbor::encoder<string_writer> e{w};
    CHECK_EQ(e.simple_value_encode(static_cast<cbor::simple_value>(24)).error(), std::errc::invalid_argument);
    CHECK_EQ(e.simple_value_encode(static_cast<cbor::simple_value>(31)).error(), std::errc::invalid_argument);
    CHECK(e.simple_value_encode(cbor::simple_value::undefined).has_value());
    REQUIRE(e.flush().has_value());
    CHECK_EQ(w.bytes, "\xf7"sv);
}
