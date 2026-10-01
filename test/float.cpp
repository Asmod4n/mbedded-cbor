#include <cbor/internal/float.hpp>
#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>

using cbor::internal::float_decode_binary16;
using cbor::internal::float_encode_binary16;
using cbor::internal::preferred_float_info;

namespace
{

std::uint32_t bits_of(float f)
{
	return std::bit_cast<std::uint32_t>(f);
}

} // namespace

// Ported from test.rb: 'major 7: float widths decode (f16 / f32 / f64)'.
// f32 and f64 are a bit_cast and wait for the decoder.
TEST_CASE("major 7: f16 decodes")
{
	CHECK_EQ(bits_of(float_decode_binary16(0x3c00)), bits_of(1.0f));
	CHECK_EQ(bits_of(float_decode_binary16(0x0000)), bits_of(0.0f));
}

// Ported from test.rb: 'major 7: ±Inf and NaN roundtrip at all widths'.
TEST_CASE("major 7: ±Inf and NaN")
{
	CHECK_EQ(
		bits_of(float_decode_binary16(float_encode_binary16(INFINITY))),
		bits_of(INFINITY));
	CHECK_EQ(bits_of(float_decode_binary16(
			 float_encode_binary16(-INFINITY))),
		 bits_of(-INFINITY));
	CHECK(std::isnan(float_decode_binary16(float_encode_binary16(NAN))));
	CHECK_EQ(bits_of(float_decode_binary16(0x7c00)), bits_of(INFINITY));
	CHECK(std::isnan(float_decode_binary16(0x7e00)));
}

// Ported from test.rb: 'major 7: ±zero and sample floats roundtrip'.
TEST_CASE("major 7: ±zero and sample floats")
{
	for (float const f : {0.0f, -0.0f, 1.5f}) {
		CHECK_EQ(preferred_float_info(f), 25);
		CHECK_EQ(bits_of(float_decode_binary16(
				 float_encode_binary16(f))),
			 bits_of(f));
	}
	CHECK_EQ(preferred_float_info(1.0e300), 27);
}

// Ported from test.rb: '§4.1 float wire: exact bytes for well-known f16
// values'.
TEST_CASE("§4.1 float wire: exact bytes for well-known f16 values")
{
	struct {
		float value;
		std::uint16_t wire;
	} const cases[] = {
		{0.0f, 0x0000},	    {-0.0f, 0x8000},	 {1.0f, 0x3c00},
		{1.5f, 0x3e00},	    {-1.5f, 0xbe00},	 {0.5f, 0x3800},
		{0.25f, 0x3400},    {100.0f, 0x5640},	 {65504.0f, 0x7bff},
		{INFINITY, 0x7c00}, {-INFINITY, 0xfc00},
	};
	for (auto const &c : cases) {
		CHECK_EQ(preferred_float_info(c.value), 25);
		CHECK_EQ(float_encode_binary16(c.value), c.wire);
	}
}

// Ported from test.rb: '§4.1 float wire: NaN canonicalizes to f16 0xF97E00'.
TEST_CASE("§4.1 float wire: NaN canonicalizes to f16 0xF97E00")
{
	CHECK_EQ(preferred_float_info(NAN), 25);
	CHECK_EQ(float_encode_binary16(NAN), 0x7e00);
}

// Ported from test.rb: '§4.1 float width: normals and subnormals select f16 (3
// bytes)'.
TEST_CASE("§4.1 float width: normals and subnormals select f16")
{
	for (double const v :
	     {0.0, -0.0, 1.0, 1.5, -1.5, 0.5, 0.25, 100.0, 65504.0,
	      double(INFINITY), double(-INFINITY), double(NAN),
	      1.0 / 16777216.0, 1.0 / 8388608.0, 1.0 / 32768.0})
		CHECK_EQ(preferred_float_info(v), 25);
}

// Ported from test.rb: '§4.1 float width: subnormal 2^-24 encodes as f16
// 0xF90001'.
TEST_CASE("§4.1 float width: subnormal 2^-24 encodes as f16 0x0001")
{
	CHECK_EQ(float_encode_binary16(1.0f / 16777216.0f), 0x0001);
}

// Ported from test.rb: '§4.1 float width: subnormal powers of 2 round-trip
// correctly'.
TEST_CASE("§4.1 float width: subnormal powers of 2 round-trip correctly")
{
	float v = 1.0f / 16777216.0f;
	for (int i = 0; i < 10; ++i) {
		CHECK_EQ(preferred_float_info(v), 25);
		CHECK_EQ(bits_of(float_decode_binary16(
				 float_encode_binary16(v))),
			 bits_of(v));
		v *= 2.0f;
	}
}

// Ported from test.rb: '§4.1 float width: values above f16 range use f32 (5
// bytes)'.
TEST_CASE("§4.1 float width: values above f16 range use f32")
{
	CHECK_EQ(preferred_float_info(65505.0), 26);
	CHECK_EQ(preferred_float_info(1.0e10), 26);
	CHECK_EQ(preferred_float_info(std::ldexp(1.0, -126)), 26);
}

// Ported from test.rb: '§4.1 float width: f64-only values use f64 (9 bytes)'.
TEST_CASE("§4.1 float width: f64-only values use f64")
{
	for (double const v : {3.14, 1.0 / 3.0, 1.0e300})
		CHECK_EQ(preferred_float_info(v), 27);
}

// Ported from test.rb: '§4.1 float width: re-encoding wider-wire decoded floats
// narrows'.
TEST_CASE("§4.1 float width: re-encoding wider-wire decoded floats narrows")
{
	CHECK_EQ(preferred_float_info(
			 std::bit_cast<float>(std::uint32_t{0x3f800000})),
		 25);
	CHECK_EQ(preferred_float_info(std::bit_cast<double>(
			 std::uint64_t{0x3ff0000000000000})),
		 25);
}
