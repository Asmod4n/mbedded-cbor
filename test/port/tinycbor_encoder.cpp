// The tests of the TinyCBOR encoder, ported to the public API of mbedded-cbor. The tests show that mbedded-cbor writes
// the bytes that another library writes for the same values, or the bytes that its documentation states.
//
// Upstream: https://github.com/intel/tinycbor, commit 0cb8ba93637a18e4497e85a3f5c681ee7dba38f4,
// tests/encoder/tst_encoder.cpp and tests/encoder/data.cpp. These files carry this license:
//
// Copyright (C) 2021 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// Each upstream call maps to the call of mbedded-cbor that writes the same thing:
// - cbor_encode_uint(), cbor_encode_int(), cbor_encode_boolean(), cbor_encode_null() write the shortest head. The
//   streaming cbor::encoder writes an integer in the width of its C++ type (encode.hpp, "also when a shorter head
//   holds the value"), so the port writes these values with cbor::structs, which writes preferred serialization;
// - cbor_encode_negative_int(), cbor_encode_simple_value(), cbor_encode_undefined(), cbor_encode_half_float(),
//   cbor_encode_float(), cbor_encode_double(), cbor_encode_byte_string(), cbor_encode_text_string() and
//   cbor_encode_tag() map to the member functions of cbor::encoder with the same names;
// - a container of upstream is a C++ container that cbor::structs writes as an array or a map;
// - the tests that wrap each value in a tag, an array or a map (tags(), arrays(), maps(), writerApi(),
//   shortBuffer()) write the tree that cbor::decode() reads from the expected bytes, with cbor::encode(), which keeps
//   the width of each head (encode.hpp).
// No call of the library writes indefinite length; the rows of upstream with indefinite length are not ported.

#include "tinycbor.hpp"

#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <variant>
#include <vector>
#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;
using tinycbor_port::error_text;
using tinycbor_port::no_error;
using tinycbor_port::outcome;

namespace tinycbor_encoder
{

template <class T>
std::string written_by_structs(T const &value)
{
    auto const r = cbor::structs<T>::encode(value);
    if (!r)
        return "error: " + error_text(r.error());
    return *r;
}

// Writes with one call of cbor::encoder into a std::string, as upstream writes with one call into a buffer.
template <class Call>
std::string written_by_encoder(Call const call)
{
    std::string out;
    cbor::encoder e(out);
    if (auto const r = call(e); !r)
        return "error: " + error_text(r.error());
    if (auto const r = e.finish(); !r)
        return "error: " + error_text(r.error());
    return out;
}

std::span<std::byte const> bytes_of(std::string_view const s)
{
    return std::as_bytes(std::span(s));
}

// The tree that cbor::decode() reads from the bytes, written again by cbor::encode().
std::string reencoded(std::string const &bytes)
{
    auto const tree = cbor::decode(bytes);
    if (!tree)
        return "error: decode: " + error_text(tree.error());
    std::string out;
    if (auto const r = cbor::encode(**tree, out); !r)
        return "error: encode: " + error_text(r.error());
    return out;
}

struct row {
    std::string_view name;
    std::string output;
};

// tags_data() of upstream: addFixedData(), addStringsData() and the definite rows of addArraysAndMaps().
std::vector<row> tags_data()
{
    return {
        {"0U", "\x00"s},
        {"1U", "\x01"s},
        {"10U", "\x0a"s},
        {"23U", "\x17"s},
        {"24U", "\x18\x18"s},
        {"255U", "\x18\xff"s},
        {"256U", "\x19\x01\x00"s},
        {"65535U", "\x19\xff\xff"s},
        {"65536U", "\x1a\0\1\x00\x00"s},
        {"4294967295U", "\x1a\xff\xff\xff\xff"s},
        {"4294967296U", "\x1b\0\0\0\1\0\0\0\0"s},
        {"UINT64_MAX", "\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s},
        {"0", "\x00"s},
        {"1", "\x01"s},
        {"10", "\x0a"s},
        {"23", "\x17"s},
        {"24", "\x18\x18"s},
        {"255", "\x18\xff"s},
        {"256", "\x19\x01\x00"s},
        {"65535", "\x19\xff\xff"s},
        {"65536", "\x1a\0\1\x00\x00"s},
        {"4294967295", "\x1a\xff\xff\xff\xff"s},
        {"4294967296", "\x1b\0\0\0\1\0\0\0\0"s},
        {"-1", "\x20"s},
        {"-2", "\x21"s},
        {"-24", "\x37"s},
        {"-25", "\x38\x18"s},
        {"-UINT8_MAX", "\x38\xff"s},
        {"-UINT8_MAX-1", "\x39\x01\x00"s},
        {"-UINT16_MAX", "\x39\xff\xff"s},
        {"-UINT16_MAX-1", "\x3a\0\1\x00\x00"s},
        {"-UINT32_MAX", "\x3a\xff\xff\xff\xff"s},
        {"-UINT32_MAX-1", "\x3b\0\0\0\1\0\0\0\0"s},
        {"negative1", "\x20"s},
        {"negative2", "\x21"s},
        {"negative24", "\x37"s},
        {"negative25", "\x38\x18"s},
        {"negativeUINT8_MAX", "\x38\xff"s},
        {"negativeUINT8_MAX-1", "\x39\x01\x00"s},
        {"negativeUINT16_MAX", "\x39\xff\xff"s},
        {"negativeUINT16_MAX-1", "\x3a\0\1\x00\x00"s},
        {"negativeUINT32_MAX", "\x3a\xff\xff\xff\xff"s},
        {"negativeUINT32_MAX-1", "\x3b\0\0\0\1\0\0\0\0"s},
        {"negativeUINT64_MAX", "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xfe"s},
        {"negativeUINT64_MAX+1", "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s},
        {"simple0", "\xe0"s},
        {"simple19", "\xf3"s},
        {"false", "\xf4"s},
        {"true", "\xf5"s},
        {"null", "\xf6"s},
        {"undefined", "\xf7"s},
        {"simple32", "\xf8\x20"s},
        {"simple255", "\xf8\xff"s},
        {"0.f16", "\xf9\0\0"s},
        {"-1.f16", "\xf9\xbc\0"s},
        {"1.5f16", "\xf9\x3e\0"s},
        {"nan_f16", "\xf9\x7e\0"s},
        {"-inf_f16", "\xf9\xfc\0"s},
        {"+inf_f16", "\xf9\x7c\0"s},
        {"0.f", "\xfa\0\0\0\0"s},
        {"0.", "\xfb\0\0\0\0\0\0\0\0"s},
        {"-1.f", "\xfa\xbf\x80\0\0"s},
        {"-1.", "\xfb\xbf\xf0\0\0\0\0\0\0"s},
        {"16777215.f", "\xfa\x4b\x7f\xff\xff"s},
        {"16777215.", "\xfb\x41\x6f\xff\xff\xe0\0\0\0"s},
        {"-16777215.f", "\xfa\xcb\x7f\xff\xff"s},
        {"-16777215.", "\xfb\xc1\x6f\xff\xff\xe0\0\0\0"s},
        {"nan_f", "\xfa\x7f\xc0\0\0"s},
        {"nan", "\xfb\x7f\xf8\0\0\0\0\0\0"s},
        {"-inf_f", "\xfa\xff\x80\0\0"s},
        {"-inf", "\xfb\xff\xf0\0\0\0\0\0\0"s},
        {"+inf_f", "\xfa\x7f\x80\0\0"s},
        {"+inf", "\xfb\x7f\xf0\0\0\0\0\0\0"s},
        {"emptybytestring", "\x40"s},
        {"bytestring1", "\x41 "s},
        {"bytestring1-nul", "\x41\0"s},
        {"bytestring5", "\x45Hello"s},
        {"bytestring24", "\x58\x18""123456789012345678901234"s},
        {"bytestring256", "\x59\1\0"s + std::string(256, '3')},
        {"emptytextstring", "\x60"s},
        {"textstring1", "\x61 "s},
        {"textstring1-nul", "\x61\0"s},
        {"textstring5", "\x65Hello"s},
        {"textstring24", "\x78\x18""123456789012345678901234"s},
        {"textstring256", "\x79\1\0"s + std::string(256, '3')},
        {"emptyarray", "\x80"s},
        {"emptymap", "\xa0"s},
        {"array-0", "\x81\0"s},
        {"array-{0-0}", "\x82\0\0"s},
        {"array-Hello", "\x81\x65Hello"s},
        {"array-array-0", "\x81\x81\0"s},
        {"array-array-{0-0}", "\x81\x82\0\0"s},
        {"array-array-0-0", "\x82\x81\0\0"s},
        {"array-array-Hello", "\x81\x81\x65Hello"s},
        {"map-0:0", "\xa1\0\0"s},
        {"map-0:0-1:1", "\xa2\0\0\1\1"s},
        {"map-0:{map-0:0-1:1}", "\xa1\0\xa2\0\0\1\1"s},
        {"array-map1", "\x81\xa1\0\0"s},
        {"array-map2", "\x82\xa1\0\0\xa1\1\1"s},
        {"map-array1", "\xa1\x62oc\x81\0"s},
        {"map-array2", "\xa1\x62oc\x84\0\1\2\3"s},
        {"map-array3", "\xa2\x62oc\x82\0\1\2\3"s},
        {"array-1(0)", "\x81\xc1\0"s},
        {"array-1(map)", "\x81\xc1\xa0"s},
        {"map-1(2):3(4)", "\xa1\xc1\2\xc3\4"s},
    };
}

}

using namespace tinycbor_encoder;

// fixed, the integers: cbor_encode_uint() and cbor_encode_int() of upstream write the shortest head, as
// cbor::structs does for the C++ type of the QVariant of upstream.
TEST_CASE("tinycbor encoder fixed: integers")
{
    CHECK_EQ(written_by_structs(std::uint32_t{0}), "\x00"s);
    CHECK_EQ(written_by_structs(std::uint32_t{1}), "\x01"s);
    CHECK_EQ(written_by_structs(std::uint32_t{10}), "\x0a"s);
    CHECK_EQ(written_by_structs(std::uint32_t{23}), "\x17"s);
    CHECK_EQ(written_by_structs(std::uint32_t{24}), "\x18\x18"s);
    CHECK_EQ(written_by_structs(std::uint32_t{255}), "\x18\xff"s);
    CHECK_EQ(written_by_structs(std::uint32_t{256}), "\x19\x01\x00"s);
    CHECK_EQ(written_by_structs(std::uint32_t{65535}), "\x19\xff\xff"s);
    CHECK_EQ(written_by_structs(std::uint32_t{65536}), "\x1a\0\1\x00\x00"s);
    CHECK_EQ(written_by_structs(std::uint32_t{4294967295}), "\x1a\xff\xff\xff\xff"s);
    CHECK_EQ(written_by_structs(std::uint64_t{4294967296}), "\x1b\0\0\0\1\0\0\0\0"s);
    CHECK_EQ(written_by_structs(std::numeric_limits<std::uint64_t>::max()), "\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s);
    CHECK_EQ(written_by_structs(0), "\x00"s);
    CHECK_EQ(written_by_structs(1), "\x01"s);
    CHECK_EQ(written_by_structs(10), "\x0a"s);
    CHECK_EQ(written_by_structs(23), "\x17"s);
    CHECK_EQ(written_by_structs(24), "\x18\x18"s);
    CHECK_EQ(written_by_structs(255), "\x18\xff"s);
    CHECK_EQ(written_by_structs(256), "\x19\x01\x00"s);
    CHECK_EQ(written_by_structs(65535), "\x19\xff\xff"s);
    CHECK_EQ(written_by_structs(65536), "\x1a\0\1\x00\x00"s);
    CHECK_EQ(written_by_structs(std::int64_t{4294967295}), "\x1a\xff\xff\xff\xff"s);
    CHECK_EQ(written_by_structs(std::int64_t{4294967296}), "\x1b\0\0\0\1\0\0\0\0"s);
    CHECK_EQ(written_by_structs(-1), "\x20"s);
    CHECK_EQ(written_by_structs(-2), "\x21"s);
    CHECK_EQ(written_by_structs(-24), "\x37"s);
    CHECK_EQ(written_by_structs(-25), "\x38\x18"s);
    CHECK_EQ(written_by_structs(-256), "\x38\xff"s);
    CHECK_EQ(written_by_structs(-257), "\x39\x01\x00"s);
    CHECK_EQ(written_by_structs(-65536), "\x39\xff\xff"s);
    CHECK_EQ(written_by_structs(-65537), "\x3a\0\1\x00\x00"s);
    CHECK_EQ(written_by_structs(std::int64_t{-4294967296}), "\x3a\xff\xff\xff\xff"s);
    CHECK_EQ(written_by_structs(std::int64_t{-4294967297}), "\x3b\0\0\0\1\0\0\0\0"s);
}

// fixed, the rows of NegativeInteger: cbor_encode_negative_int(abs) writes -abs, and abs 0 stands for -2^64. A value
// that std::int64_t holds is written by cbor::structs, as cbor_encode_int() writes it. The two values below -2^63
// take the eight-byte head, which cbor::encoder::negative_integer() with a std::uint64_t argument writes (the argument
// is abs - 1, RFC 8949 3.1).
TEST_CASE("tinycbor encoder fixed: negative integers")
{
    CHECK_EQ(written_by_structs(std::int64_t{-1}), "\x20"s);
    CHECK_EQ(written_by_structs(std::int64_t{-2}), "\x21"s);
    CHECK_EQ(written_by_structs(std::int64_t{-24}), "\x37"s);
    CHECK_EQ(written_by_structs(std::int64_t{-25}), "\x38\x18"s);
    CHECK_EQ(written_by_structs(std::int64_t{-256}), "\x38\xff"s);
    CHECK_EQ(written_by_structs(std::int64_t{-257}), "\x39\x01\x00"s);
    CHECK_EQ(written_by_structs(std::int64_t{-65536}), "\x39\xff\xff"s);
    CHECK_EQ(written_by_structs(std::int64_t{-65537}), "\x3a\0\1\x00\x00"s);
    CHECK_EQ(written_by_structs(std::int64_t{-4294967296}), "\x3a\xff\xff\xff\xff"s);
    CHECK_EQ(written_by_structs(std::int64_t{-4294967297}), "\x3b\0\0\0\1\0\0\0\0"s);
    std::uint64_t const max = std::numeric_limits<std::uint64_t>::max();
    CHECK_EQ(written_by_encoder([&](auto &e) { return e.negative_integer(max - 1); }),
             "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xfe"s);
    CHECK_EQ(written_by_encoder([&](auto &e) { return e.negative_integer(max); }),
             "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s);
}

// fixed, the simple values: false, true and null are C++ values; undefined and the other simple values are
// cbor::simple_value.
TEST_CASE("tinycbor encoder fixed: simple values")
{
    auto const simple = [](std::uint8_t const n) {
        return written_by_encoder([n](auto &e) { return e.simple_value(cbor::simple_value{n}); });
    };
    CHECK_EQ(simple(0), "\xe0"s);
    CHECK_EQ(simple(19), "\xf3"s);
    CHECK_EQ(written_by_structs(false), "\xf4"s);
    CHECK_EQ(written_by_structs(true), "\xf5"s);
    CHECK_EQ(written_by_structs(nullptr), "\xf6"s);
    CHECK_EQ(written_by_encoder([](auto &e) { return e.simple_value(cbor::simple_value::undefined); }), "\xf7"s);
    CHECK_EQ(simple(32), "\xf8\x20"s);
    CHECK_EQ(simple(255), "\xf8\xff"s);
}

// fixed, the floats: cbor_encode_float() and cbor_encode_double() keep the width of the value, as
// cbor::encoder::floating_point() does ("It does not choose a shorter form").
TEST_CASE("tinycbor encoder fixed: floats")
{
    auto const as_float = [](float const v) {
        return written_by_encoder([v](auto &e) { return e.floating_point(v); });
    };
    auto const as_double = [](double const v) {
        return written_by_encoder([v](auto &e) { return e.floating_point(v); });
    };
    float const nanf = std::bit_cast<float>(std::uint32_t{0x7fc00000});
    float const inff = std::bit_cast<float>(std::uint32_t{0x7f800000});
    float const ninff = std::bit_cast<float>(std::uint32_t{0xff800000});
    double const nan = std::bit_cast<double>(std::uint64_t{0x7ff8000000000000});
    double const inf = std::bit_cast<double>(std::uint64_t{0x7ff0000000000000});
    double const ninf = std::bit_cast<double>(std::uint64_t{0xfff0000000000000});
    CHECK_EQ(as_float(0.f), "\xfa\0\0\0\0"s);
    CHECK_EQ(as_double(0.), "\xfb\0\0\0\0\0\0\0\0"s);
    CHECK_EQ(as_float(-1.f), "\xfa\xbf\x80\0\0"s);
    CHECK_EQ(as_double(-1.), "\xfb\xbf\xf0\0\0\0\0\0\0"s);
    CHECK_EQ(as_float(16777215.f), "\xfa\x4b\x7f\xff\xff"s);
    CHECK_EQ(as_double(16777215.), "\xfb\x41\x6f\xff\xff\xe0\0\0\0"s);
    CHECK_EQ(as_float(-16777215.f), "\xfa\xcb\x7f\xff\xff"s);
    CHECK_EQ(as_double(-16777215.), "\xfb\xc1\x6f\xff\xff\xe0\0\0\0"s);
    CHECK_EQ(as_float(nanf), "\xfa\x7f\xc0\0\0"s);
    CHECK_EQ(as_double(nan), "\xfb\x7f\xf8\0\0\0\0\0\0"s);
    CHECK_EQ(as_float(ninff), "\xfa\xff\x80\0\0"s);
    CHECK_EQ(as_double(ninf), "\xfb\xff\xf0\0\0\0\0\0\0"s);
    CHECK_EQ(as_float(inff), "\xfa\x7f\x80\0\0"s);
    CHECK_EQ(as_double(inf), "\xfb\x7f\xf0\0\0\0\0\0\0"s);
}

#if defined(__STDCPP_FLOAT16_T__)
// fixed, the half floats (qfloat16 of upstream), and halfFloat: cbor_encode_half_float() writes the 16 bits that it
// gets, as cbor::encoder::floating_point() of a std::float16_t does ("A NaN keeps its bits").
TEST_CASE("tinycbor encoder: half floats")
{
    auto const as_half = [](std::uint16_t const bits) {
        return written_by_encoder([bits](auto &e) { return e.floating_point(std::bit_cast<std::float16_t>(bits)); });
    };
    CHECK_EQ(as_half(0x0000), "\xf9\0\0"s);
    CHECK_EQ(as_half(0xbc00), "\xf9\xbc\0"s);
    CHECK_EQ(as_half(0x3e00), "\xf9\x3e\0"s);
    CHECK_EQ(as_half(0x7e00), "\xf9\x7e\0"s);
    CHECK_EQ(as_half(0xfc00), "\xf9\xfc\0"s);
    CHECK_EQ(as_half(0x7c00), "\xf9\x7c\0"s);
    std::uint16_t const raws[] = {0x0000, 0x8000, 0x0001, 0x8001, 0x03ff, 0x83ff, 0x0400, 0x8400, 0x3c00, 0xbc00,
                                  0x3e00, 0xbe00, 0x7bff, 0xfbff, 0x7c00, 0xfc00, 0x7c01, 0xfc01, 0x7e00, 0xfe00};
    for (std::uint16_t const raw : raws) {
        CAPTURE(raw);
        std::string const expected = "\xf9"s + static_cast<char>(raw >> 8) + static_cast<char>(raw & 0xff);
        CHECK_EQ(as_half(raw), expected);
    }
}
#endif

// floatAsHalfFloat: cbor_encode_float_as_half_float() writes a float as a half float. cbor::structs writes a float as
// "the shortest float that holds the value" (structs.hpp), which is the half float for each row. Upstream leaves out
// the zeros and the NaNs.
TEST_CASE("tinycbor encoder: a float as a half float")
{
    struct half_row {
        std::string_view name;
        std::string output;
        double input;
    };
    double const inf = std::numeric_limits<double>::infinity();
    half_row const rows[] = {
        {"min.denorm", "\x00\x01"s, std::ldexp(1.0, -14) * std::ldexp(1.0, -10)},
        {"-min.denorm", "\x80\x01"s, std::ldexp(-1.0, -14) * std::ldexp(1.0, -10)},
        {"max.denorm", "\x03\xff"s, std::ldexp(1.0, -14) * (1.0 - std::ldexp(1.0, -10))},
        {"-max.denorm", "\x83\xff"s, std::ldexp(-1.0, -14) * (1.0 - std::ldexp(1.0, -10))},
        {"min.norm", "\x04\x00"s, std::ldexp(1.0, -14)},
        {"-min.norm", "\x84\x00"s, std::ldexp(-1.0, -14)},
        {"1.0", "\x3c\x00"s, 1.0},
        {"-1.0", "\xbc\x00"s, -1.0},
        {"1.5", "\x3e\x00"s, 1.5},
        {"-1.5", "\xbe\x00"s, -1.5},
        {"max", "\x7b\xff"s, std::ldexp(1.0, 15) * (2.0 - std::ldexp(1.0, -10))},
        {"-max", "\xfb\xff"s, std::ldexp(-1.0, 15) * (2.0 - std::ldexp(1.0, -10))},
        {"inf", "\x7c\x00"s, inf},
        {"-inf", "\xfc\x00"s, -inf},
    };
    for (half_row const &r : rows) {
        CAPTURE(r.name);
        CHECK_EQ(written_by_structs(static_cast<float>(r.input)), "\xf9"s + r.output);
    }
}

// floatAsHalfFloatCloseToZero: upstream rounds a value below the smallest half float to a zero. cbor::structs does
// not round: it writes "the shortest float that holds the value" (structs.hpp), and for 2^-25 that is the single
// float. RFC 8949 4.1 asks preferred serialization to keep the value. The zeros are as upstream.
TEST_CASE("tinycbor encoder: a float close to zero as a half float")
{
    CHECK_EQ(written_by_structs(0.0f), "\xf9\x00\x00"s);
    CHECK_EQ(written_by_structs(-0.0f), "\xf9\x80\x00"s);
    CHECK_EQ(written_by_structs(static_cast<float>(std::ldexp(1.0, -14) * std::ldexp(1.0, -11))),
             "\xfa\x33\x00\x00\x00"s);
    CHECK_EQ(written_by_structs(static_cast<float>(std::ldexp(-1.0, -14) * std::ldexp(1.0, -11))),
             "\xfa\xb3\x00\x00\x00"s);
}

// floatAsHalfFloatNaN: a NaN as a half float has the exponent of all ones and a mantissa that is not zero.
TEST_CASE("tinycbor encoder: a NaN as a half float")
{
    std::string const out = written_by_structs(std::bit_cast<float>(std::uint32_t{0x7fc00000}));
    REQUIRE_EQ(out.size(), 3u);
    CHECK_EQ(static_cast<unsigned char>(out.at(0)), 0xf9u);
    CHECK_EQ(static_cast<unsigned char>(out.at(1)) & 0x7cu, 0x7cu);
    CHECK_NE((static_cast<unsigned char>(out.at(1)) & 0x03u) | static_cast<unsigned char>(out.at(2)), 0u);
}

// strings: cbor_encode_byte_string() and cbor_encode_text_string() write the shortest head for the length, as
// cbor::encoder does.
TEST_CASE("tinycbor encoder: strings")
{
    auto const bytes = [](std::string const &s) {
        return written_by_encoder([&s](auto &e) { return e.byte_string(bytes_of(s)); });
    };
    auto const text = [](std::string const &s) {
        return written_by_encoder([&s](auto &e) { return e.text_string(s); });
    };
    CHECK_EQ(bytes(""s), "\x40"s);
    CHECK_EQ(bytes(" "s), "\x41 "s);
    CHECK_EQ(bytes("\0"s), "\x41\0"s);
    CHECK_EQ(bytes("Hello"s), "\x45Hello"s);
    CHECK_EQ(bytes("123456789012345678901234"s), "\x58\x18""123456789012345678901234"s);
    CHECK_EQ(bytes(std::string(256, '3')), "\x59\1\0"s + std::string(256, '3'));
    CHECK_EQ(text(""s), "\x60"s);
    CHECK_EQ(text(" "s), "\x61 "s);
    CHECK_EQ(text("\0"s), "\x61\0"s);
    CHECK_EQ(text("Hello"s), "\x65Hello"s);
    CHECK_EQ(text("123456789012345678901234"s), "\x78\x18""123456789012345678901234"s);
    CHECK_EQ(text(std::string(256, '3')), "\x79\1\0"s + std::string(256, '3'));
}

// arraysAndMaps, the definite rows: a C++ container that cbor::structs writes as an array or a map. A std::tuple is
// an array, and a std::map is a map (structs.hpp). Two rows are not here: "map-array3" has keys of two types, and
// "map-1(2):3(4)" has a tag as a key, which cbor::structs refuses (structs.hpp, "unsupported_value a map key is an
// array, a map or a tag"); the round trip in "tinycbor encoder: tags, arrays and maps" writes both.
TEST_CASE("tinycbor encoder: arrays and maps")
{
    using pairs = std::map<int, int>;
    CHECK_EQ(written_by_structs(std::tuple<>{}), "\x80"s);
    CHECK_EQ(written_by_structs(pairs{}), "\xa0"s);
    CHECK_EQ(written_by_structs(std::tuple<int>{0}), "\x81\0"s);
    CHECK_EQ(written_by_structs(std::tuple<int, int>{0, 0}), "\x82\0\0"s);
    CHECK_EQ(written_by_structs(std::tuple<std::string>{"Hello"}), "\x81\x65Hello"s);
    CHECK_EQ(written_by_structs(std::tuple<std::tuple<int>>{{0}}), "\x81\x81\0"s);
    CHECK_EQ(written_by_structs(std::tuple<std::tuple<int, int>>{{0, 0}}), "\x81\x82\0\0"s);
    CHECK_EQ(written_by_structs(std::tuple<std::tuple<int>, int>{{0}, 0}), "\x82\x81\0\0"s);
    CHECK_EQ(written_by_structs(std::tuple<std::tuple<std::string>>{{"Hello"}}), "\x81\x81\x65Hello"s);
    CHECK_EQ(written_by_structs(pairs{{0, 0}}), "\xa1\0\0"s);
    CHECK_EQ(written_by_structs(pairs{{0, 0}, {1, 1}}), "\xa2\0\0\1\1"s);
    CHECK_EQ(written_by_structs(std::map<int, pairs>{{0, {{0, 0}, {1, 1}}}}), "\xa1\0\xa2\0\0\1\1"s);
    CHECK_EQ(written_by_structs(std::tuple<pairs>{{{0, 0}}}), "\x81\xa1\0\0"s);
    CHECK_EQ(written_by_structs(std::tuple<pairs, pairs>{{{0, 0}}, {{1, 1}}}), "\x82\xa1\0\0\xa1\1\1"s);
    CHECK_EQ(written_by_structs(std::map<std::string, std::tuple<int>>{{"oc", {0}}}), "\xa1\x62oc\x81\0"s);
    CHECK_EQ(written_by_structs(std::map<std::string, std::tuple<int, int, int, int>>{{"oc", {0, 1, 2, 3}}}),
             "\xa1\x62oc\x84\0\1\2\3"s);
    CHECK_EQ(written_by_structs(std::tuple<cbor::tagged_item<1, int>>{{0}}), "\x81\xc1\0"s);
    CHECK_EQ(written_by_structs(std::tuple<cbor::tagged_item<1, pairs>>{{{}}}), "\x81\xc1\xa0"s);
}

// fixed, strings and arraysAndMaps as a round trip: cbor::encode() writes the tree of the expected bytes, and keeps
// the width of each head, so the bytes come back.
TEST_CASE("tinycbor encoder: the rows written again from their tree")
{
    for (row const &r : tags_data()) {
        CAPTURE(r.name);
        CHECK_EQ(reencoded(r.output), r.output);
    }
}

// tags, arrays and maps: each row of tags_data() in a tag, in an array and in a map, written again from its tree.
// Two tags differ from upstream, because cbor::decode() builds the tree:
// - tag 55799: "One leading tag 55799 is read as its content, so the root is that content" (decode.hpp); the tree
//   has no tag, and the bytes come back without it;
// - tag 24 on a byte string: cbor::decode() decodes the content as one data item (decode.hpp), and RFC 8949 3.4.5.1
//   says that content that does not encode one well-formed item is not valid. The rows whose content is no item give
//   the error of that decode.
TEST_CASE("tinycbor encoder: tags, arrays and maps")
{
    std::map<std::string_view, std::string> const tag24_errors = {
        {"emptybytestring", "error: decode: " + error_text(error::too_little_data)},
        {"bytestring5", "error: decode: " + error_text(error::too_little_data)},
        {"bytestring24", "error: decode: " + error_text(error::syntax_error)},
        {"bytestring256", "error: decode: " + error_text(error::syntax_error)},
    };
    for (row const &r : tags_data()) {
        CAPTURE(r.name);
        std::string const &o = r.output;
        CHECK_EQ(reencoded("\xc1"s + o), "\xc1"s + o);
        auto const tag24 = tag24_errors.find(r.name);
        CHECK_EQ(reencoded("\xd8\x18"s + o), tag24 == tag24_errors.end() ? "\xd8\x18"s + o : tag24->second);
        CHECK_EQ(reencoded("\xd8\xff"s + o), "\xd8\xff"s + o);
        CHECK_EQ(reencoded("\xd9\1\0"s + o), "\xd9\1\0"s + o);
        CHECK_EQ(reencoded("\xd9\xd9\xf7"s + o), o);
        CHECK_EQ(reencoded("\xd9\xff\xff"s + o), "\xd9\xff\xff"s + o);
        CHECK_EQ(reencoded("\xda\0\1\0\0"s + o), "\xda\0\1\0\0"s + o);
        CHECK_EQ(reencoded("\xda\xff\xff\xff\xff"s + o), "\xda\xff\xff\xff\xff"s + o);
        CHECK_EQ(reencoded("\xdb\0\0\0\1\0\0\0\0"s + o), "\xdb\0\0\0\1\0\0\0\0"s + o);
        CHECK_EQ(reencoded("\xdb\xff\xff\xff\xff\xff\xff\xff\xff"s + o), "\xdb\xff\xff\xff\xff\xff\xff\xff\xff"s + o);
        CHECK_EQ(reencoded("\xc1\xc1"s + o), "\xc1\xc1"s + o);

        CHECK_EQ(reencoded("\x81"s + o), "\x81"s + o);
        CHECK_EQ(reencoded("\x82"s + o + o), "\x82"s + o + o);
        std::string longoutput = o;
        for (int i = 0; i < 5; ++i)
            longoutput += longoutput;
        CHECK_EQ(reencoded("\x98\x20"s + longoutput), "\x98\x20"s + longoutput);
        for (int i = 0; i < 3; ++i)
            longoutput += longoutput;
        CHECK_EQ(reencoded("\x99\1\0"s + longoutput), "\x99\1\0"s + longoutput);
        CHECK_EQ(reencoded("\x81\x81"s + o), "\x81\x81"s + o);
        CHECK_EQ(reencoded("\x81\x82"s + o + o), "\x81\x82"s + o + o);
        CHECK_EQ(reencoded("\x82\x81"s + o + o), "\x82\x81"s + o + o);
        CHECK_EQ(reencoded("\x82\x81"s + o + "\x81" + o), "\x82\x81"s + o + "\x81" + o);

        CHECK_EQ(reencoded("\xa1\1"s + o), "\xa1\1"s + o);
        CHECK_EQ(reencoded("\xa2\1"s + o + o + "\x18\x18"), "\xa2\1"s + o + o + "\x18\x18");
        CHECK_EQ(reencoded("\xa1"s + o + o), "\xa1"s + o + o);
        std::string longmap = "\1"s + o;
        for (int i = 0; i < 5; ++i)
            longmap += longmap;
        CHECK_EQ(reencoded("\xb8\x20"s + longmap), "\xb8\x20"s + longmap);
        for (int i = 0; i < 3; ++i)
            longmap += longmap;
        CHECK_EQ(reencoded("\xb9\1\0"s + longmap), "\xb9\1\0"s + longmap);
        CHECK_EQ(reencoded("\xa1\1\xa1\2"s + o), "\xa1\1\xa1\2"s + o);
        CHECK_EQ(reencoded("\xa1\1\xa2\2"s + o + o + "\xf4"), "\xa1\1\xa2\2"s + o + o + "\xf4");
        CHECK_EQ(reencoded("\xa2\1\xa1\2"s + o + o + "\xf4"), "\xa2\1\xa1\2"s + o + o + "\xf4");
    }
}

namespace tinycbor_encoder
{

// The writer of upstream's writerApi(): it appends each piece to a string, or fails each call and counts the calls.
struct counting_writer {
    std::string *out;
    int *calls;
    bool fails;

    std::expected<void, std::errc> append(std::string_view const part)
    {
        ++*calls;
        if (fails)
            return std::unexpected(std::errc::io_error);
        out->append(part);
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

struct counting_target {
    std::string *out;
    int *calls;
    bool fails;

    counting_writer allocate(std::size_t)
    {
        return {out, calls, fails};
    }
};

}

// writerApi and writerApiFail: a writer gets the bytes; a writer that fails gives io_error at its first call, and is
// not called again.
TEST_CASE("tinycbor encoder: a writer")
{
    for (row const &r : tags_data()) {
        CAPTURE(r.name);
        auto const tree = cbor::decode(r.output);
        REQUIRE(tree.has_value());
        std::string out;
        int calls = 0;
        CHECK_EQ(outcome(cbor::encode(**tree, counting_target{&out, &calls, false})), error_text(no_error));
        CHECK_EQ(out, r.output);
        std::string unused;
        int failed_calls = 0;
        CHECK_EQ(outcome(cbor::encode(**tree, counting_target{&unused, &failed_calls, true})),
                 error_text(error::io_error));
        CHECK_EQ(failed_calls, 1);
    }
}

// shortBuffer: a buffer shorter than the bytes gives no_buffer_space (encode.hpp, "the span is too short"). Upstream
// also says how many bytes are missing; the library has no call that says it.
TEST_CASE("tinycbor encoder: a buffer that is too short")
{
    for (row const &r : tags_data()) {
        CAPTURE(r.name);
        auto const tree = cbor::decode(r.output);
        REQUIRE(tree.has_value());
        std::vector<char> buffer(r.output.size());
        for (std::size_t length = 0; length < r.output.size(); ++length) {
            CAPTURE(length);
            CHECK_EQ(outcome(cbor::encode(**tree, std::span<char>(buffer).first(length))),
                     error_text(error::no_buffer_space));
        }
        CHECK_EQ(cbor::encode(**tree, std::span<char>(buffer)).value_or(0), r.output.size());
    }
}

// tooShortArrays, tooShortMaps, tooBigArrays, tooBigMaps: upstream counts the items of a container and gives
// CborErrorTooFewItems or CborErrorTooManyItems. cbor::encoder does not count them: "a wrong count gives bytes that are
// not one item" (encode.hpp). The port writes the wrong count, finishes without error, and checks that the bytes are
// not one item: too few items end too early, and too many items leave bytes after the item. The input is the row
// "textstring5".
TEST_CASE("tinycbor encoder: a wrong count of items")
{
    auto const input = [](auto &e) { return e.text_string("Hello"); };
    struct wrong_count {
        std::string_view name;
        bool map;
        std::uint64_t count;
        int inputs;
        bool too_few;
    };
    wrong_count const rows[] = {
        {"tooShortArrays", false, 2, 1, true},
        {"tooShortMaps", true, 2, 1, true},
        {"tooBigArrays", false, 1, 2, false},
        {"tooBigMaps", true, 1, 3, false},
    };
    for (wrong_count const &r : rows) {
        CAPTURE(r.name);
        std::string out;
        cbor::encoder e(out);
        bool const opened = r.map ? e.map(r.count).has_value() : e.array(r.count).has_value();
        CHECK(opened);
        for (int i = 0; i < r.inputs; ++i)
            CHECK(input(e).has_value());
        CHECK_EQ(outcome(e.finish()), error_text(no_error));
        auto const size = cbor::item_size(out);
        if (r.too_few)
            CHECK_EQ(outcome(size), error_text(error::too_little_data));
        else
            CHECK_LT(size.value_or(out.size()), out.size());
    }
}

// illegalSimpleType: a simple value from 24 to 31 is reserved (RFC 8949 3.3); cbor::encoder::simple_value() gives
// reserved_simple_value. Upstream refuses 25 to 31.
TEST_CASE("tinycbor encoder: reserved simple values")
{
    for (std::uint8_t n = 25; n <= 31; ++n) {
        CAPTURE(int{n});
        std::string out;
        cbor::encoder e(out);
        CHECK_EQ(outcome(e.simple_value(cbor::simple_value{n})), error_text(error::reserved_simple_value));
    }
}
