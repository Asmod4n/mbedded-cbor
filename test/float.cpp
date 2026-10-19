#include "reference.hpp"
#include "owned_copy.hpp"

#include <algorithm>
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

#ifdef __cpp_impl_reflection
std::string preferred_float(double d)
{
    auto const e = cbor::structs<double>::encode(d);
    REQUIRE(e.has_value());
    return *e;
}
#endif

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

// RFC 8949 4.1: zero-padding the significand of a shorter NaN at the right gives the same NaN in a longer
// form, and 2.2 calls two NaNs equal when their zero-extended significands are equal. A decoded binary16 or
// binary32 NaN keeps its sign and its payload in the double, also a signaling NaN, whose quiet bit a
// conversion of the processor would set. The cbor-wg test vectors decode f97d1f to the NaN of
// fb7ff47c0000000000. decode and lazy give the same bits.
TEST_CASE("major 7: a NaN keeps its sign and payload when it is decoded")
{
    struct {
        std::string_view wire;
        std::uint64_t bits;
    } const cases[] = {
        {"\xf9\x7d\x1f"sv, 0x7ff47c0000000000},
        {"\xf9\x7c\x01"sv, 0x7ff0040000000000},
        {"\xf9\xfd\x01"sv, 0xfff4040000000000},
        {"\xf9\x7e\x00"sv, 0x7ff8000000000000},
        {"\xfa\x7f\x80\x00\x01"sv, 0x7ff0000020000000},
        {"\xfa\x7f\xa3\xf5\x53"sv, 0x7ff47eaa60000000},
        {"\xfa\xff\xc0\x00\x00"sv, 0xfff8000000000000},
        {"\xfb\x7f\xf4\x7c\x00\x00\x00\x00\x00"sv, 0x7ff47c0000000000},
    };
    for (auto const &c : cases) {
        CAPTURE(c.bits);
        CHECK_EQ(bits_of(float_of(c.wire)), c.bits);
        auto const top = test::lazy_of_copy(c.wire);
        REQUIRE(top.has_value());
        auto const d = top->get<double>();
        REQUIRE(d.has_value());
        CHECK_EQ(bits_of(*d), c.bits);
    }
}

// Ported from test.rb: 'major 7: ±zero and sample floats roundtrip'.
TEST_CASE("major 7: ±zero and sample floats")
{
    for (double const f : {0.0, -0.0, 1.5, 1.0e300})
        CHECK_EQ(bits_of(float_of(encoded_float(f))), bits_of(f));
}

// The encoder writes a double at its native width, binary64. structs writes deterministic CBOR,
// so the width tests below go through structs, which needs reflection.
TEST_CASE("major 7: the encoder writes a double as binary64")
{
    for (double const v : {0.0, 1.5, 65504.0, 3.14})
        CHECK_EQ(encoded_float(v).size(), 9);
}

#ifdef __cpp_impl_reflection
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
        CHECK_EQ(preferred_float(c.value), c.wire);
}

// Ported from test.rb: '§4.1 float wire: NaN canonicalizes to f16 0xF97E00'.
TEST_CASE("§4.1 float wire: NaN canonicalizes to f16 0xF97E00")
{
    CHECK_EQ(preferred_float(NAN), "\xf9\x7e\x00"sv);
}

// Ported from test.rb: '§4.1 float width: normals and subnormals select f16 (3 bytes)'.
TEST_CASE("§4.1 float width: normals and subnormals select f16")
{
    for (double const v : {0.0, -0.0, 1.0, 1.5, -1.5, 0.5, 0.25, 100.0, 65504.0, double(INFINITY),
                           double(-INFINITY), double(NAN), 1.0 / 16777216.0, 1.0 / 8388608.0, 1.0 / 32768.0})
        CHECK_EQ(preferred_float(v).size(), 3);
}

// Ported from test.rb: '§4.1 float width: subnormal 2^-24 encodes as f16 0xF90001'.
TEST_CASE("§4.1 float width: subnormal 2^-24 encodes as f16 0xF90001")
{
    CHECK_EQ(preferred_float(1.0 / 16777216.0), "\xf9\x00\x01"sv);
}

// Ported from test.rb: '§4.1 float width: subnormal powers of 2 round-trip correctly'.
TEST_CASE("§4.1 float width: subnormal powers of 2 round-trip correctly")
{
    double v = 1.0 / 16777216.0;
    for (int i = 0; i < 10; ++i) {
        std::string const wire = preferred_float(v);
        CHECK_EQ(wire.size(), 3);
        CHECK_EQ(bits_of(float_of(wire)), bits_of(v));
        v *= 2.0;
    }
}

// Ported from test.rb: '§4.1 float width: values above f16 range use f32 (5 bytes)'.
TEST_CASE("§4.1 float width: values above f16 range use f32")
{
    for (double const v : {65505.0, 1.0e10, std::ldexp(1.0, -126)})
        CHECK_EQ(preferred_float(v).size(), 5);
}

// Ported from test.rb: '§4.1 float width: f64-only values use f64 (9 bytes)'.
TEST_CASE("§4.1 float width: f64-only values use f64")
{
    for (double const v : {3.14, 1.0 / 3.0, 1.0e300})
        CHECK_EQ(preferred_float(v).size(), 9);
}

// Ported from test.rb: '§4.1 float width: re-encoding wider-wire decoded floats narrows'.
TEST_CASE("§4.1 float width: re-encoding wider-wire decoded floats narrows")
{
    CHECK_EQ(preferred_float(float_of("\xfa\x3f\x80\x00\x00"sv)).size(), 3);
    CHECK_EQ(preferred_float(float_of("\xfb\x3f\xf0\x00\x00\x00\x00\x00\x00"sv)).size(), 3);
}

// RFC 8949 4.1: the shortest float that holds the value. A binary32 subnormal, down to 2^-149, is five bytes.
TEST_CASE("§4.1 float width: binary32 subnormals use f32")
{
    CHECK_EQ(preferred_float(std::ldexp(1.0, -149)), "\xfa\x00\x00\x00\x01"sv);
    CHECK_EQ(preferred_float(std::ldexp(1.0, -127)), "\xfa\x00\x40\x00\x00"sv);
    CHECK_EQ(preferred_float(std::ldexp(1.0, -150)).size(), 9);
    CHECK_EQ(preferred_float(std::ldexp(1.0, -149) * 1.5).size(), 9);
}

namespace
{

// RFC 8949 section 4.2.2: structs writes deterministic CBOR, so a float takes the shortest form that
// keeps its value.
// IEEE 754: a finite value fits a binary format when it equals n * 2^q with
// |n| below 2^precision, q at or above the exponent of the least subnormal,
// and the exponent of the value at most the greatest exponent of the format.
bool fits_binary(double v, int precision, int least_exponent, int max_exponent)
{
    if (v == 0)
        return true;
    int e = 0;
    std::frexp(v, &e);
    if (e > max_exponent)
        return false;
    int const q = std::max(e - precision, least_exponent);
    double const n = std::ldexp(v, -q);
    return n == std::trunc(n) && std::fabs(n) < std::ldexp(1.0, precision);
}

} // namespace

// The encoder chooses the width from a table per exponent. This test walks every
// exponent of a double, so that no row of that table goes unchecked.
TEST_CASE("major 7: every finite exponent takes the shortest width that keeps the value")
{
    for (std::uint64_t exponent = 0; exponent < 0x7ff; ++exponent)
        for (std::uint64_t low = 0; low <= 52; ++low)
            for (std::uint64_t sign : {0u, 1u}) {
                std::uint64_t const significand =
                    low == 52 ? 0
                              : (std::uint64_t{1} << low) |
                                    (std::uint64_t{0xa5} << 44 & ((std::uint64_t{1} << 52) - 1) &
                                     ~((std::uint64_t{1} << low) - 1));
                double const v = std::bit_cast<double>(sign << 63 | exponent << 52 | significand);
                std::string const wire = preferred_float(v);
                CHECK_EQ(bits_of(float_of(wire)), bits_of(v));
                std::size_t const expected = fits_binary(v, 11, -24, 16)     ? 3
                                             : fits_binary(v, 24, -149, 128) ? 5
                                                                             : 9;
                CHECK_EQ(wire.size(), expected);
            }
}
#endif
