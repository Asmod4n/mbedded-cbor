// Port of the float tests of QCBOR (https://github.com/laurencelundblade/QCBOR), file test/float_tests.c, with
// test/half_to_double_from_rfc7049.c, at commit 1d060574db3d58aac439bc3b8a60167951c7e959. The tests exist to show
// that mbedded-cbor encodes and decodes floats as QCBOR does on the same values. Each expected value comes from the
// upstream test and is checked against RFC 8949 in refs/rfc8949.txt: 3.3 for the three widths, 4.1 for the
// preferred serialization, and Appendix D for the decoder of half-precision floats.
//
// The license of test/float_tests.c follows. The file carries "Copyright (c) 2018-2025, Laurence Lundblade. All
// rights reserved.", "Copyright (c) 2021, Arm Limited. All rights reserved." and "SPDX-License-Identifier:
// BSD-3-Clause", with the BSD 3-Clause License of the file LICENSE of QCBOR, verbatim:
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "../hex.hpp"
#include "../reference/value.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using cbor::error;

namespace
{

std::string as_string(std::initializer_list<std::uint8_t> const bytes)
{
    std::string out;
    for (std::uint8_t const b : bytes)
        out.push_back(static_cast<char>(b));
    return out;
}

template <class T>
std::string not_preferred(T const value)
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.floating_point(value).has_value());
    REQUIRE(e.finish().has_value());
    return out;
}

template <class T>
std::string preferred(T const value)
{
    auto const out = cbor::structs<T>::encode(value);
    REQUIRE(out.has_value());
    return *out;
}

std::expected<double, error> double_of(std::string encoded)
{
    return cbor::lazy::adopt(std::move(encoded)).and_then([](cbor::lazy const &l) {
        return l.get<double>();
    });
}

struct double_test_case {
    double number;
    std::string preferred;
    std::string not_preferred;
};

// DoubleTestCases: each double with its preferred and its not-preferred (64-bit) encoding. The CDE and dCBOR columns
// of upstream are not ported: upstream calls them a guess, and mbedded-cbor has no encoder for them.
std::vector<double_test_case> const double_test_cases = {
    {0.0, as_string({0xf9, 0x00, 0x00}), as_string({0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {-0.0, as_string({0xf9, 0x80, 0x00}), as_string({0xfb, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {std::numeric_limits<double>::quiet_NaN(), as_string({0xf9, 0x7e, 0x00}),
     as_string({0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {std::numeric_limits<double>::infinity(), as_string({0xf9, 0x7c, 0x00}),
     as_string({0xfb, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {-std::numeric_limits<double>::infinity(), as_string({0xf9, 0xfc, 0x00}),
     as_string({0xfb, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {1.0, as_string({0xf9, 0x3c, 0x00}), as_string({0xfb, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {-2.0, as_string({0xf9, 0xc0, 0x00}), as_string({0xfb, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {0.333251953125, as_string({0xf9, 0x35, 0x55}),
     as_string({0xfb, 0x3f, 0xd5, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {5.9604644775390625E-8, as_string({0xf9, 0x00, 0x01}),
     as_string({0xfb, 0x3e, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {3.0517578125E-5, as_string({0xf9, 0x02, 0x00}), as_string({0xfb, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {6.097555160522461E-5, as_string({0xf9, 0x03, 0xff}),
     as_string({0xfb, 0x3f, 0x0f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {6.103515625E-5, as_string({0xf9, 0x04, 0x00}), as_string({0xfb, 0x3f, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {6.1035156250000014E-5, as_string({0xfb, 0x3f, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
     as_string({0xfb, 0x3f, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01})},
    {6.1035156249999993E-5, as_string({0xfb, 0x3f, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
     as_string({0xfb, 0x3f, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff})},
    {65504.0, as_string({0xf9, 0x7b, 0xff}), as_string({0xfb, 0x40, 0xef, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {65504.1, as_string({0xfb, 0x40, 0xef, 0xfc, 0x03, 0x33, 0x33, 0x33, 0x33}),
     as_string({0xfb, 0x40, 0xef, 0xfc, 0x03, 0x33, 0x33, 0x33, 0x33})},
    {65536.0, as_string({0xfa, 0x47, 0x80, 0x00, 0x00}),
     as_string({0xfb, 0x40, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {1.401298464324817e-45, as_string({0xfa, 0x00, 0x00, 0x00, 0x01}),
     as_string({0xfb, 0x36, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {5.8774717541114375E-39, as_string({0xfa, 0x00, 0x40, 0x00, 0x00}),
     as_string({0xfb, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {1.1754942106924411E-38, as_string({0xfa, 0x00, 0x7f, 0xff, 0xff}),
     as_string({0xfb, 0x38, 0x0f, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x00})},
    {1.1754943508222874E-38, as_string({0xfb, 0x38, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
     as_string({0xfb, 0x38, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff})},
    {1.1754943508222875e-38, as_string({0xfa, 0x00, 0x80, 0x00, 0x00}),
     as_string({0xfb, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {1.1754943508222878e-38, as_string({0xfb, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
     as_string({0xfb, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01})},
    {16777216, as_string({0xfa, 0x4b, 0x80, 0x00, 0x00}),
     as_string({0xfb, 0x41, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {16777217, as_string({0xfb, 0x41, 0x70, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00}),
     as_string({0xfb, 0x41, 0x70, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00})},
    {3.4028234663852886E+38, as_string({0xfa, 0x7f, 0x7f, 0xff, 0xff}),
     as_string({0xfb, 0x47, 0xef, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00})},
    {3.402823466385289E+38, as_string({0xfb, 0x47, 0xef, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x01}),
     as_string({0xfb, 0x47, 0xef, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x01})},
    {3.402823669209385e+38, as_string({0xfb, 0x47, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
     as_string({0xfb, 0x47, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {5.0e-324, as_string({0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
     as_string({0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01})},
    {2.2250738585072009e-308, as_string({0xfb, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
     as_string({0xfb, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff})},
    {2.2250738585072014e-308, as_string({0xfb, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
     as_string({0xfb, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {1.7976931348623157e308, as_string({0xfb, 0x7f, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
     as_string({0xfb, 0x7f, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff})},
};

struct nan_test_case {
    std::uint64_t bits;
    std::string preferred;
    std::string not_preferred;
};

// NaNTestCases: NaNs with a payload. RFC 8949 4.1 makes the preferred serialization the shortest form that keeps
// the value, and 2.2 and 4.1 keep the payload of a NaN: a payload that has no zero bits at the right to drop stays
// in the longer form.
std::vector<nan_test_case> const nan_test_cases = {
    {0x7ff8000000000000ULL, as_string({0xf9, 0x7e, 0x00}),
     as_string({0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {0x7ff8000000000001ULL, as_string({0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}),
     as_string({0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01})},
    {0x7ffffc0000000000ULL, as_string({0xf9, 0x7f, 0xff}),
     as_string({0xfb, 0x7f, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00})},
    {0x7ff80000000003ffULL, as_string({0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff}),
     as_string({0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff})},
    {0x7fffffffe0000000ULL, as_string({0xfa, 0x7f, 0xff, 0xff, 0xff}),
     as_string({0xfb, 0x7f, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x00})},
    {0x7ffffffff0000000ULL, as_string({0xfb, 0x7f, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00}),
     as_string({0xfb, 0x7f, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00})},
    {0x7fffffffffffffffULL, as_string({0xfb, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
     as_string({0xfb, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff})},
};

// The decoder of RFC 8949 Appendix D, Figure 3, as upstream has it in half_to_double_from_rfc7049.c.
double decode_half(std::uint16_t const half)
{
    unsigned const exp = (half >> 10) & 0x1fu;
    unsigned const mant = half & 0x3ffu;
    double val;
    if (exp == 0)
        val = std::ldexp(static_cast<double>(mant), -24);
    else if (exp != 31)
        val = std::ldexp(static_cast<double>(mant + 1024), static_cast<int>(exp) - 25);
    else
        val = mant == 0 ? std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
    return (half & 0x8000u) != 0 ? -val : val;
}

} // namespace

// FloatValuesTests with DoubleTestCases: cbor::structs<double>::encode writes the preferred serialization
// (structs.hpp: "`float`, `double` | the shortest float that holds the value"), and cbor::encoder::floating_point
// writes the width of its type (encode.hpp: "It does not choose a shorter form"). Both forms decode to the same
// double.
TEST_CASE("qcbor FloatValuesTests: doubles encode in preferred and 64-bit form, and decode back")
{
    for (auto const &c : double_test_cases) {
        double const number = c.number;
        CAPTURE(number);
        CHECK_EQ(test::hexlify(preferred(c.number)), test::hexlify(c.preferred));
        CHECK_EQ(test::hexlify(not_preferred(c.number)), test::hexlify(c.not_preferred));
        for (std::string const &in : {c.preferred, c.not_preferred}) {
            auto const d = double_of(in);
            REQUIRE(d.has_value());
            if (std::isnan(c.number))
                CHECK(std::isnan(*d));
            else
                CHECK_EQ(std::bit_cast<std::uint64_t>(*d), std::bit_cast<std::uint64_t>(c.number));
        }
    }
}

// FloatValuesTests with NaNTestCases: the payload of a NaN survives the encode in both forms and the decode of both
// forms.
TEST_CASE("qcbor FloatValuesTests: NaNs keep their payload in preferred and 64-bit form")
{
    for (auto const &c : nan_test_cases) {
        std::uint64_t const bits = c.bits;
        CAPTURE(bits);
        double const nan = std::bit_cast<double>(c.bits);
        CHECK_EQ(test::hexlify(preferred(nan)), test::hexlify(c.preferred));
        CHECK_EQ(test::hexlify(not_preferred(nan)), test::hexlify(c.not_preferred));
        for (std::string const &in : {c.preferred, c.not_preferred}) {
            auto const d = double_of(in);
            REQUIRE(d.has_value());
            CHECK_EQ(std::bit_cast<std::uint64_t>(*d), c.bits);
        }
    }
}

// HalfPrecisionAgainstRFCCodeTest: each half-precision float decodes to the value of the decoder of RFC 8949
// Appendix D. Upstream takes every 60th value; this test takes all 65536. As upstream, a NaN is checked only as a
// NaN, because the RFC decoder gives one NaN for every payload.
TEST_CASE("qcbor HalfPrecisionAgainstRFCCodeTest: every half-precision float decodes as RFC 8949 Appendix D")
{
    for (std::uint32_t h = 0; h <= 0xffff; ++h) {
        auto const half = static_cast<std::uint16_t>(h);
        CAPTURE(half);
        std::string in = as_string({0xf9});
        in.push_back(static_cast<char>(half >> 8));
        in.push_back(static_cast<char>(half & 0xff));
        auto const d = double_of(in);
        REQUIRE(d.has_value());
        double const expected = decode_half(half);
        if (std::isnan(expected))
            CHECK(std::isnan(*d));
        else
            CHECK_EQ(std::bit_cast<std::uint64_t>(*d), std::bit_cast<std::uint64_t>(expected));
    }
}

namespace
{

// [0.0, 3.14, 0.0, NaN, Infinity, 0.0, 3.140000104904175, 0.0, NaN, Infinity, {100: 0.0, 101: 3.1415926,
// "euler": 2.718281828459045, 105: 0.0, 102: 0.0, 103: 3.141592502593994, "euler2": 2.7182817459106445, 106: 0.0}]:
// spExpectedFloats.
std::string const expected_floats = as_string({
    0x8b, 0xf9, 0x00, 0x00, 0xfb, 0x40, 0x09, 0x1e, 0xb8, 0x51, 0xeb, 0x85, 0x1f, 0xfb, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfb, 0x7f, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xf9, 0x00, 0x00, 0xfa, 0x40, 0x48, 0xf5, 0xc3, 0xfa, 0x00, 0x00, 0x00, 0x00, 0xfa,
    0x7f, 0xc0, 0x00, 0x00, 0xfa, 0x7f, 0x80, 0x00, 0x00, 0xa8, 0x18, 0x64, 0xf9, 0x00, 0x00, 0x18, 0x65, 0xfb,
    0x40, 0x09, 0x21, 0xfb, 0x4d, 0x12, 0xd8, 0x4a, 0x65, 0x65, 0x75, 0x6c, 0x65, 0x72, 0xfb, 0x40, 0x05, 0xbf,
    0x0a, 0x8b, 0x14, 0x57, 0x69, 0x18, 0x69, 0xfb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x66,
    0xf9, 0x00, 0x00, 0x18, 0x67, 0xfa, 0x40, 0x49, 0x0f, 0xda, 0x66, 0x65, 0x75, 0x6c, 0x65, 0x72, 0x32, 0xfa,
    0x40, 0x2d, 0xf8, 0x54, 0x18, 0x6a, 0xfa, 0x00, 0x00, 0x00, 0x00,
});

template <class Write>
std::string written(Write const write)
{
    std::string out;
    cbor::encoder e(out);
    write(e);
    REQUIRE(e.finish().has_value());
    return out;
}

} // namespace

// GeneralFloatEncodeTests: the array of spExpectedFloats. QCBOREncode_AddDouble and QCBOREncode_AddFloat write the
// preferred form, which is cbor::structs<double>::encode and cbor::structs<float>::encode here. The forms
// "NoPreferred" write the width of the type, which is cbor::encoder::floating_point here. The heads and keys come
// from cbor::encoder. The pieces are joined in the order of upstream.
TEST_CASE("qcbor GeneralFloatEncodeTests: floats in preferred and full width form one array")
{
    using limits = std::numeric_limits<double>;
    using limits_f = std::numeric_limits<float>;
    auto key = [](std::uint8_t const k) {
        return written([k](cbor::encoder<std::string> &e) { REQUIRE(e.unsigned_integer(k).has_value()); });
    };
    auto text = [](std::string_view const t) {
        return written([t](cbor::encoder<std::string> &e) { REQUIRE(e.text_string(t).has_value()); });
    };
    std::string out = written([](cbor::encoder<std::string> &e) { REQUIRE(e.array(11).has_value()); });
    out += preferred(0.0);
    out += preferred(3.14);
    out += not_preferred(0.0);
    out += not_preferred(std::bit_cast<double>(std::uint64_t{0x7ff8000000000000}));
    out += not_preferred(limits::infinity());
    out += preferred(0.0f);
    out += preferred(3.14f);
    out += not_preferred(0.0f);
    out += not_preferred(std::bit_cast<float>(std::uint32_t{0x7fc00000}));
    out += not_preferred(limits_f::infinity());
    out += written([](cbor::encoder<std::string> &e) { REQUIRE(e.map(8).has_value()); });
    out += key(100) + preferred(0.0);
    out += key(101) + preferred(3.1415926);
    out += text("euler") + preferred(2.71828182845904523536);
    out += key(105) + not_preferred(0.0);
    out += key(102) + preferred(0.0f);
    out += key(103) + preferred(3.1415926f);
    out += text("euler2") + preferred(2.71828182845904523536f);
    out += key(106) + not_preferred(0.0f);
    CHECK_EQ(test::hexlify(out), test::hexlify(expected_floats));
}

// GeneralFloatDecodeTests: the first ten elements of spExpectedFloats decode to double, from each of the three
// widths (RFC 8949 3.3).
TEST_CASE("qcbor GeneralFloatDecodeTests: floats of each width decode to double")
{
    auto const doc = cbor::lazy::adopt(std::string(expected_floats));
    REQUIRE(doc.has_value());
    auto at = [&doc](std::size_t const i) {
        auto const d = doc->at(i).and_then([](cbor::lazy const &l) { return l.get<double>(); });
        REQUIRE(d.has_value());
        return *d;
    };
    CHECK_EQ(std::bit_cast<std::uint64_t>(at(0)), std::bit_cast<std::uint64_t>(0.0));
    CHECK_EQ(std::bit_cast<std::uint64_t>(at(1)), std::bit_cast<std::uint64_t>(3.14));
    CHECK_EQ(std::bit_cast<std::uint64_t>(at(2)), std::bit_cast<std::uint64_t>(0.0));
    CHECK(std::isnan(at(3)));
    CHECK_EQ(std::bit_cast<std::uint64_t>(at(4)), std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity()));
    CHECK_EQ(std::bit_cast<std::uint64_t>(at(5)), std::bit_cast<std::uint64_t>(0.0));
    CHECK_EQ(std::bit_cast<std::uint64_t>(at(6)), std::bit_cast<std::uint64_t>(3.1400001049041748));
    CHECK_EQ(std::bit_cast<std::uint64_t>(at(7)), std::bit_cast<std::uint64_t>(0.0));
    CHECK(std::isnan(at(8)));
    CHECK_EQ(std::bit_cast<std::uint64_t>(at(9)), std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity()));

    auto const tree = cbor::decode(expected_floats);
    REQUIRE(tree.has_value());
    auto const elements = (**tree).get<cbor::type::array>();
    REQUIRE(elements.has_value());
    REQUIRE_EQ(elements->size(), 11u);
    CHECK((*elements)[0].type == cbor::type::half_float);
    CHECK((*elements)[1].type == cbor::type::double_float);
    CHECK((*elements)[6].type == cbor::type::single_float);
    CHECK_EQ(std::bit_cast<std::uint64_t>((*elements)[6].get<cbor::type::single_float>().value_or(0.0)),
             std::bit_cast<std::uint64_t>(3.1400001049041748));
}
