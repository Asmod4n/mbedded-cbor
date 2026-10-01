#include "host.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace
{

std::uint64_t bits_of(double d)
{
    return std::bit_cast<std::uint64_t>(d);
}

double float_of(std::string_view wire)
{
    auto const v = decoded(wire);
    REQUIRE(v.has_value());
    REQUIRE(std::holds_alternative<double>(v->kind));
    return std::get<double>(v->kind);
}

std::string encoded_float(double d)
{
    return encoded(value{d});
}

} // namespace

// Ported from test.rb: 'major 7: float widths decode (f16 / f32 / f64)'.
TEST_CASE("major 7: float widths decode")
{
    CHECK_EQ(bits_of(float_of("\xf9\x3c\x00"sv)), bits_of(1.0));
    CHECK_EQ(bits_of(float_of("\xf9\x00\x00"sv)), bits_of(0.0));
    CHECK_EQ(bits_of(float_of("\xfa\x3f\x80\x00\x00"sv)), bits_of(1.0));
    CHECK_EQ(bits_of(float_of("\xfa\x80\x00\x00\x00"sv)), bits_of(-0.0));
    CHECK_EQ(bits_of(float_of("\xfb\x3f\xf0\x00\x00\x00\x00\x00\x00"sv)), bits_of(1.0));
}

// Ported from test.rb: 'major 7: ±Inf and NaN roundtrip at all widths'.
TEST_CASE("major 7: ±Inf and NaN")
{
    CHECK_EQ(bits_of(float_of(encoded_float(INFINITY))), bits_of(INFINITY));
    CHECK_EQ(bits_of(float_of(encoded_float(-INFINITY))), bits_of(-INFINITY));
    CHECK(std::isnan(float_of(encoded_float(NAN))));
    CHECK_EQ(bits_of(float_of("\xf9\x7c\x00"sv)), bits_of(INFINITY));
    CHECK(std::isnan(float_of("\xf9\x7e\x00"sv)));
}

// Ported from test.rb: 'major 7: ±zero and sample floats roundtrip'.
TEST_CASE("major 7: ±zero and sample floats")
{
    for (double const f : {0.0, -0.0, 1.5, 1.0e300})
        CHECK_EQ(bits_of(float_of(encoded_float(f))), bits_of(f));
}

// Ported from test.rb: '§4.1 float wire: exact bytes for well-known f16 values'.
TEST_CASE("§4.1 float wire: exact bytes for well-known f16 values")
{
    struct {
        double value;
        std::string_view wire;
    } const cases[] = {
        {0.0, "\xf9\x00\x00"sv},      {-0.0, "\xf9\x80\x00"sv},      {1.0, "\xf9\x3c\x00"sv},
        {1.5, "\xf9\x3e\x00"sv},      {-1.5, "\xf9\xbe\x00"sv},      {0.5, "\xf9\x38\x00"sv},
        {0.25, "\xf9\x34\x00"sv},     {100.0, "\xf9\x56\x40"sv},     {65504.0, "\xf9\x7b\xff"sv},
        {INFINITY, "\xf9\x7c\x00"sv}, {-INFINITY, "\xf9\xfc\x00"sv},
    };
    for (auto const &c : cases)
        CHECK_EQ(encoded_float(c.value), c.wire);
}

// Ported from test.rb: '§4.1 float wire: NaN canonicalizes to f16 0xF97E00'.
TEST_CASE("§4.1 float wire: NaN canonicalizes to f16 0xF97E00")
{
    CHECK_EQ(encoded_float(NAN), "\xf9\x7e\x00"sv);
}

// Ported from test.rb: '§4.1 float width: normals and subnormals select f16 (3 bytes)'.
TEST_CASE("§4.1 float width: normals and subnormals select f16")
{
    for (double const v : {0.0, -0.0, 1.0, 1.5, -1.5, 0.5, 0.25, 100.0, 65504.0, double(INFINITY),
                           double(-INFINITY), double(NAN), 1.0 / 16777216.0, 1.0 / 8388608.0, 1.0 / 32768.0})
        CHECK_EQ(encoded_float(v).size(), 3);
}

// Ported from test.rb: '§4.1 float width: subnormal 2^-24 encodes as f16 0xF90001'.
TEST_CASE("§4.1 float width: subnormal 2^-24 encodes as f16 0xF90001")
{
    CHECK_EQ(encoded_float(1.0 / 16777216.0), "\xf9\x00\x01"sv);
}

// Ported from test.rb: '§4.1 float width: subnormal powers of 2 round-trip correctly'.
TEST_CASE("§4.1 float width: subnormal powers of 2 round-trip correctly")
{
    double v = 1.0 / 16777216.0;
    for (int i = 0; i < 10; ++i) {
        std::string const wire = encoded_float(v);
        CHECK_EQ(wire.size(), 3);
        CHECK_EQ(bits_of(float_of(wire)), bits_of(v));
        v *= 2.0;
    }
}

// Ported from test.rb: '§4.1 float width: values above f16 range use f32 (5 bytes)'.
TEST_CASE("§4.1 float width: values above f16 range use f32")
{
    for (double const v : {65505.0, 1.0e10, std::ldexp(1.0, -126)})
        CHECK_EQ(encoded_float(v).size(), 5);
}

// Ported from test.rb: '§4.1 float width: f64-only values use f64 (9 bytes)'.
TEST_CASE("§4.1 float width: f64-only values use f64")
{
    for (double const v : {3.14, 1.0 / 3.0, 1.0e300})
        CHECK_EQ(encoded_float(v).size(), 9);
}

// Ported from test.rb: '§4.1 float width: re-encoding wider-wire decoded floats narrows'.
TEST_CASE("§4.1 float width: re-encoding wider-wire decoded floats narrows")
{
    CHECK_EQ(encoded_float(float_of("\xfa\x3f\x80\x00\x00"sv)).size(), 3);
    CHECK_EQ(encoded_float(float_of("\xfb\x3f\xf0\x00\x00\x00\x00\x00\x00"sv)).size(), 3);
}
