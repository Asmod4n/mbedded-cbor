// Port of the encode tests of QCBOR (https://github.com/laurencelundblade/QCBOR), file test/qcbor_encode_tests.c,
// and of the example program example.c, at commit 1d060574db3d58aac439bc3b8a60167951c7e959. The tests exist to show
// that mbedded-cbor writes the same bytes as QCBOR for the same values. Each expected value comes from the upstream
// test and is checked against RFC 8949 in refs/rfc8949.txt.
//
// QCBOR writes integers in preferred serialization (RFC 8949 4.1). cbor::encoder::unsigned_integer and
// cbor::encoder::negative_integer write the width of their argument type (encode.hpp: "The head takes the width of
// `T` ... also when a shorter head holds the value"), and cbor::structs<T>::encode writes the preferred
// serialization (structs.hpp: "Writes @p value as one CBOR item in preferred serialization"). So a test writes the
// heads, the strings and the keys with cbor::encoder, the integers of upstream with cbor::structs, and joins the
// pieces in the order of upstream.
//
// The license of test/qcbor_encode_tests.c follows, verbatim.
//
// Copyright (c) 2016-2018, The Linux Foundation.
// Copyright (c) 2018-2025, Laurence Lundblade.
// Copyright (c) 2022, Arm Limited. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
//      copyright notice, this list of conditions and the following
//      disclaimer in the documentation and/or other materials provided
//      with the distribution.
//    * Neither the name of The Linux Foundation nor the names of its
//      contributors, nor the name "Laurence Lundblade" may be used to
//      endorse or promote products derived from this software without
//      specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
// BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// example.c carries "Copyright (c) 2020-2021, Laurence Lundblade. All rights reserved.", "Copyright (c) 2021, Arm
// Limited. All rights reserved." and "SPDX-License-Identifier: BSD-3-Clause", with the BSD 3-Clause License of the
// file LICENSE of QCBOR, verbatim:
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

#include <bit>
#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "../reference/value.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <variant>
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

std::string hex_of(std::string_view const bytes)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (char const c : bytes) {
        auto const b = static_cast<unsigned char>(c);
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xf]);
    }
    return out;
}

template <class Write>
std::string written(Write const write)
{
    std::string out;
    cbor::encoder e(out);
    write(e);
    REQUIRE(e.finish().has_value());
    return out;
}

std::string array_head(std::uint64_t const count)
{
    return written([count](cbor::encoder<std::string> &e) { REQUIRE(e.array(count).has_value()); });
}

std::string map_head(std::uint64_t const count)
{
    return written([count](cbor::encoder<std::string> &e) { REQUIRE(e.map(count).has_value()); });
}

std::string tag_head(std::uint64_t const number)
{
    return written([number](cbor::encoder<std::string> &e) { REQUIRE(e.tag(number).has_value()); });
}

std::string text(std::string_view const t)
{
    return written([t](cbor::encoder<std::string> &e) { REQUIRE(e.text_string(t).has_value()); });
}

template <class T>
std::string preferred(T const &value)
{
    auto const out = cbor::structs<T>::encode(value);
    REQUIRE(out.has_value());
    return *out;
}

// [-9223372036854775808, ..., 31, ..., 18446744073709551615]: spExpectedEncodedInts of the encode tests; it has 31
// more than the list of the decode tests.
std::string const expected_encoded_ints = as_string({
    0x98, 0x30, 0x3b, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x3a, 0xff, 0xff, 0xff, 0xff, 0x3a, 0xff, 0xff, 0xff, 0xfe, 0x3a, 0xff, 0xff, 0xff, 0xfd, 0x3a, 0x7f, 0xff,
    0xff, 0xff, 0x3a, 0x7f, 0xff, 0xff, 0xfe, 0x3a, 0x00, 0x01, 0x00, 0x01, 0x3a, 0x00, 0x01, 0x00, 0x00, 0x39, 0xff,
    0xff, 0x39, 0xff, 0xfe, 0x39, 0xff, 0xfd, 0x39, 0x01, 0x00, 0x38, 0xff, 0x38, 0xfe, 0x38, 0xfd, 0x38, 0x18, 0x37,
    0x36, 0x20, 0x00, 0x00, 0x01, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19, 0x18, 0x1a, 0x18, 0x1f, 0x18, 0xfe, 0x18, 0xff,
    0x19, 0x01, 0x00, 0x19, 0x01, 0x01, 0x19, 0xff, 0xfe, 0x19, 0xff, 0xff, 0x1a, 0x00, 0x01, 0x00, 0x00, 0x1a, 0x00,
    0x01, 0x00, 0x01, 0x1a, 0x00, 0x01, 0x00, 0x02, 0x1a, 0x7f, 0xff, 0xff, 0xff, 0x1a, 0x7f, 0xff, 0xff, 0xff, 0x1a,
    0x80, 0x00, 0x00, 0x00, 0x1a, 0x80, 0x00, 0x00, 0x01, 0x1a, 0xff, 0xff, 0xff, 0xfe, 0x1a, 0xff, 0xff, 0xff, 0xff,
    0x1b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x1b,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});

using integer = std::variant<std::int64_t, std::uint64_t>;

std::vector<integer> const encoded_int_values = {
    std::int64_t{-9223372036854775807LL - 1},
    std::int64_t{-4294967297LL},
    std::int64_t{-4294967296LL},
    std::int64_t{-4294967295LL},
    std::int64_t{-4294967294LL},
    std::int64_t{-2147483648LL},
    std::int64_t{-2147483647LL},
    std::int64_t{-65538},
    std::int64_t{-65537},
    std::int64_t{-65536},
    std::int64_t{-65535},
    std::int64_t{-65534},
    std::int64_t{-257},
    std::int64_t{-256},
    std::int64_t{-255},
    std::int64_t{-254},
    std::int64_t{-25},
    std::int64_t{-24},
    std::int64_t{-23},
    std::int64_t{-1},
    std::int64_t{0},
    std::uint64_t{0},
    std::int64_t{1},
    std::int64_t{22},
    std::int64_t{23},
    std::int64_t{24},
    std::int64_t{25},
    std::int64_t{26},
    std::int64_t{31},
    std::int64_t{254},
    std::int64_t{255},
    std::int64_t{256},
    std::int64_t{257},
    std::int64_t{65534},
    std::int64_t{65535},
    std::int64_t{65536},
    std::int64_t{65537},
    std::int64_t{65538},
    std::int64_t{2147483647LL},
    std::int64_t{2147483647LL},
    std::int64_t{2147483648LL},
    std::int64_t{2147483649LL},
    std::int64_t{4294967294LL},
    std::int64_t{4294967295LL},
    std::int64_t{4294967296LL},
    std::int64_t{4294967297LL},
    std::int64_t{9223372036854775807LL},
    std::uint64_t{18446744073709551615ULL},
};

} // namespace

// IntegerValuesTest1: each integer takes the shortest head that holds it (RFC 8949 4.1, 4.2.1). A std::vector of a
// std::variant is an array of the items of the alternatives (structs.hpp: "another container | an array",
// "std::variant<E...> | the item of the alternative").
TEST_CASE("qcbor IntegerValuesTest1: integers encode with the shortest head")
{
    CHECK_EQ(hex_of(preferred(encoded_int_values)), hex_of(expected_encoded_ints));
    std::string pieces = array_head(encoded_int_values.size());
    for (integer const &v : encoded_int_values)
        pieces += std::visit([](auto const n) { return preferred(n); }, v);
    CHECK_EQ(hex_of(pieces), hex_of(expected_encoded_ints));
}

// SimpleValuesTest1: [true, false, null, undefined, {"UNDef": undefined}] (RFC 8949 3.3).
TEST_CASE("qcbor SimpleValuesTest1: true, false, null and undefined encode in one byte")
{
    std::string const out = written([](cbor::encoder<std::string> &e) {
        REQUIRE(e.array(5).has_value());
        REQUIRE(e.simple_value(cbor::simple_value::true_value).has_value());
        REQUIRE(e.simple_value(cbor::simple_value::false_value).has_value());
        REQUIRE(e.simple_value(cbor::simple_value::null).has_value());
        REQUIRE(e.simple_value(cbor::simple_value::undefined).has_value());
        REQUIRE(e.map(1).has_value());
        REQUIRE(e.text_string("UNDef").has_value());
        REQUIRE(e.simple_value(cbor::simple_value::undefined).has_value());
    });
    CHECK_EQ(hex_of(out),
             hex_of(as_string({0x85, 0xf5, 0xf4, 0xf6, 0xf7, 0xa1, 0x65, 0x55, 0x4e, 0x44, 0x65, 0x66, 0xf7})));
}

// EncodeLengthThirtyoneTest: an array of 31 elements, a map of 31 pairs and a text of 31 bytes take a one-byte
// argument (0x98 0x1f, 0xb8 0x1f, 0x78 0x1f), because the additional information 31 means indefinite length (RFC
// 8949 3, 3.2).
TEST_CASE("qcbor EncodeLengthThirtyoneTest: a count of 31 takes a one-byte argument")
{
    std::string out = map_head(5) + text("arr") + array_head(31);
    for (std::int64_t ix = 0; ix < 31; ++ix)
        out += preferred(ix);
    out += text("map") + map_head(31);
    for (std::int64_t ix = 0; ix < 31; ++ix) {
        char const c = ix < 26 ? static_cast<char>('a' + ix) : static_cast<char>('A' + (ix - 26));
        out += text(std::string(1, c)) + preferred(ix);
    }
    out += text("min31") + preferred(std::int64_t{-31});
    out += text("plus31") + preferred(std::int64_t{31});
    out += text("str") + text("testtesttesttesttesttestqcbor11");
    std::string const expected = as_string({
        0xa5, 0x63, 0x61, 0x72, 0x72, 0x98, 0x1f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
        0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19, 0x18,
        0x1a, 0x18, 0x1b, 0x18, 0x1c, 0x18, 0x1d, 0x18, 0x1e, 0x63, 0x6d, 0x61, 0x70, 0xb8, 0x1f, 0x61, 0x61, 0x00,
        0x61, 0x62, 0x01, 0x61, 0x63, 0x02, 0x61, 0x64, 0x03, 0x61, 0x65, 0x04, 0x61, 0x66, 0x05, 0x61, 0x67, 0x06,
        0x61, 0x68, 0x07, 0x61, 0x69, 0x08, 0x61, 0x6a, 0x09, 0x61, 0x6b, 0x0a, 0x61, 0x6c, 0x0b, 0x61, 0x6d, 0x0c,
        0x61, 0x6e, 0x0d, 0x61, 0x6f, 0x0e, 0x61, 0x70, 0x0f, 0x61, 0x71, 0x10, 0x61, 0x72, 0x11, 0x61, 0x73, 0x12,
        0x61, 0x74, 0x13, 0x61, 0x75, 0x14, 0x61, 0x76, 0x15, 0x61, 0x77, 0x16, 0x61, 0x78, 0x17, 0x61, 0x79, 0x18,
        0x18, 0x61, 0x7a, 0x18, 0x19, 0x61, 0x41, 0x18, 0x1a, 0x61, 0x42, 0x18, 0x1b, 0x61, 0x43, 0x18, 0x1c, 0x61,
        0x44, 0x18, 0x1d, 0x61, 0x45, 0x18, 0x1e, 0x65, 0x6d, 0x69, 0x6e, 0x33, 0x31, 0x38, 0x1e, 0x66, 0x70, 0x6c,
        0x75, 0x73, 0x33, 0x31, 0x18, 0x1f, 0x63, 0x73, 0x74, 0x72, 0x78, 0x1f, 0x74, 0x65, 0x73, 0x74, 0x74, 0x65,
        0x73, 0x74, 0x74, 0x65, 0x73, 0x74, 0x74, 0x65, 0x73, 0x74, 0x74, 0x65, 0x73, 0x74, 0x74, 0x65, 0x73, 0x74,
        0x71, 0x63, 0x62, 0x6f, 0x72, 0x31, 0x31,
    });
    CHECK_EQ(hex_of(out), hex_of(expected));
}

// EncodeDateTest: dates of RFC 8949 3.4.1 (tag 0), 3.4.2 (tag 1) and RFC 8943 (tag 100 and tag 1004). The epoch
// date comes from cbor::structs<std::chrono::sys_seconds> ("tag 1 on an integer"), and the days from
// cbor::structs<std::chrono::year_month_day> ("tag 100 on an integer of days"): -10676 days after 1970-01-01 is
// 1940-10-09. The tags 0 and 1004 come from cbor::encoder::tag. Upstream writes 1004("1985-04-12T23:20:50.52Z");
// RFC 8943 2 admits only a full-date in tag 1004, so that item is not valid. The encoder does not check tag content
// (encode.hpp: "The call does not check that the content fits the tag"), and RFC 8949 5.2 lets a generic encoder
// write it, so the bytes are those of upstream.
TEST_CASE("qcbor EncodeDateTest: date strings, epoch dates and days encode under their tags")
{
    using namespace std::chrono;
    std::string const epoch = preferred(sys_seconds{seconds{1363896240}});
    CHECK_EQ(hex_of(epoch), hex_of(as_string({0xc1, 0x1a, 0x51, 0x4b, 0x67, 0xb0})));
    std::string const days = preferred(year_month_day{year{1940}, month{10}, day{9}});
    CHECK_EQ(hex_of(days), hex_of(as_string({0xd8, 0x64, 0x39, 0x29, 0xb3})));

    std::string out = array_head(9);
    out += text("2013-03-21T20:04:00Z");
    out += tag_head(0) + text("2013-03-21T20:04:00Z");
    out += preferred(std::int64_t{1363896240});
    out += epoch;
    out += days;
    out += preferred(std::int64_t{3994});
    out += tag_head(1004) + text("1940-10-09");
    out += text("1980-12-08");
    out += map_head(6);
    out += text("Sample Date from RFC 3339") + tag_head(0) + text("1985-04-12T23:20:50.52Z");
    out += text("SD") + preferred(sys_seconds{seconds{999}});
    out += text("Sample Date from RFC 8943") + text("1985-04-12");
    out += preferred(std::int64_t{42}) + tag_head(1004) + text("1985-04-12T23:20:50.52Z");
    out += text("SY") + days;
    out += preferred(std::int64_t{45}) + preferred(std::int64_t{3994});
    std::string const expected = as_string({
        0x89, 0x74, 0x32, 0x30, 0x31, 0x33, 0x2d, 0x30, 0x33, 0x2d, 0x32, 0x31, 0x54, 0x32, 0x30, 0x3a, 0x30, 0x34,
        0x3a, 0x30, 0x30, 0x5a, 0xc0, 0x74, 0x32, 0x30, 0x31, 0x33, 0x2d, 0x30, 0x33, 0x2d, 0x32, 0x31, 0x54, 0x32,
        0x30, 0x3a, 0x30, 0x34, 0x3a, 0x30, 0x30, 0x5a, 0x1a, 0x51, 0x4b, 0x67, 0xb0, 0xc1, 0x1a, 0x51, 0x4b, 0x67,
        0xb0, 0xd8, 0x64, 0x39, 0x29, 0xb3, 0x19, 0x0f, 0x9a, 0xd9, 0x03, 0xec, 0x6a, 0x31, 0x39, 0x34, 0x30, 0x2d,
        0x31, 0x30, 0x2d, 0x30, 0x39, 0x6a, 0x31, 0x39, 0x38, 0x30, 0x2d, 0x31, 0x32, 0x2d, 0x30, 0x38, 0xa6, 0x78,
        0x19, 0x53, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x20, 0x44, 0x61, 0x74, 0x65, 0x20, 0x66, 0x72, 0x6f, 0x6d, 0x20,
        0x52, 0x46, 0x43, 0x20, 0x33, 0x33, 0x33, 0x39, 0xc0, 0x77, 0x31, 0x39, 0x38, 0x35, 0x2d, 0x30, 0x34, 0x2d,
        0x31, 0x32, 0x54, 0x32, 0x33, 0x3a, 0x32, 0x30, 0x3a, 0x35, 0x30, 0x2e, 0x35, 0x32, 0x5a, 0x62, 0x53, 0x44,
        0xc1, 0x19, 0x03, 0xe7, 0x78, 0x19, 0x53, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x20, 0x44, 0x61, 0x74, 0x65, 0x20,
        0x66, 0x72, 0x6f, 0x6d, 0x20, 0x52, 0x46, 0x43, 0x20, 0x38, 0x39, 0x34, 0x33, 0x6a, 0x31, 0x39, 0x38, 0x35,
        0x2d, 0x30, 0x34, 0x2d, 0x31, 0x32, 0x18, 0x2a, 0xd9, 0x03, 0xec, 0x77, 0x31, 0x39, 0x38, 0x35, 0x2d, 0x30,
        0x34, 0x2d, 0x31, 0x32, 0x54, 0x32, 0x33, 0x3a, 0x32, 0x30, 0x3a, 0x35, 0x30, 0x2e, 0x35, 0x32, 0x5a, 0x62,
        0x53, 0x59, 0xd8, 0x64, 0x39, 0x29, 0xb3, 0x18, 0x2d, 0x19, 0x0f, 0x9a,
    });
    CHECK_EQ(hex_of(out), hex_of(expected));

    // The epoch date and the days decode back to the same values.
    auto const t = cbor::structs<sys_seconds>::decode(std::string(epoch));
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->time_since_epoch(), seconds{1363896240});
    auto const d = cbor::structs<year_month_day>::decode(std::string(days));
    REQUIRE(d.has_value());
    CHECK(**d == year_month_day{year{1940}, month{10}, day{9}});
}

// EncodeRawTest: [[[[[[]]]]], the 48 integers]: upstream inserts two encoded items as they are. cbor::encode writes
// a decoded tree with the width of each head (encode.hpp: "Each integer, length and tag number keeps the width of
// its head"), so the decoded items give the same bytes back.
TEST_CASE("qcbor EncodeRawTest: two encoded items inserted into an array keep their bytes")
{
    std::string const five_arrays = as_string({0x81, 0x81, 0x81, 0x81, 0x80});
    std::string out = array_head(2);
    for (std::string const &raw : {five_arrays, expected_encoded_ints}) {
        auto const tree = cbor::decode(raw);
        REQUIRE(tree.has_value());
        CHECK(cbor::encode(**tree, out) == raw.size());
    }
    CHECK_EQ(hex_of(out), hex_of(as_string({0x82}) + five_arrays + expected_encoded_ints));
}

// MapEncodeTest: the map of CreateMap, spValidMapEncoded. 42 and 98 take a one-byte argument (RFC 8949 4.1), which
// is the width of std::uint8_t.
TEST_CASE("qcbor MapEncodeTest: a map with nested array and map encodes")
{
    std::string const xxxx = "xxxx";
    std::string const yyyy = "yyyy";
    std::string const out = written([&](cbor::encoder<std::string> &e) {
        REQUIRE(e.map(3).has_value());
        REQUIRE(e.text_string("first integer").has_value());
        REQUIRE(e.unsigned_integer(std::uint8_t{42}).has_value());
        REQUIRE(e.text_string("an array of two strings").has_value());
        REQUIRE(e.array(2).has_value());
        REQUIRE(e.text_string("string1").has_value());
        REQUIRE(e.text_string("string2").has_value());
        REQUIRE(e.text_string("map in a map").has_value());
        REQUIRE(e.map(4).has_value());
        REQUIRE(e.text_string("bytes 1").has_value());
        REQUIRE(e.byte_string(std::as_bytes(std::span(xxxx))).has_value());
        REQUIRE(e.text_string("bytes 2").has_value());
        REQUIRE(e.byte_string(std::as_bytes(std::span(yyyy))).has_value());
        REQUIRE(e.text_string("another int").has_value());
        REQUIRE(e.unsigned_integer(std::uint8_t{98}).has_value());
        REQUIRE(e.text_string("text 2").has_value());
        REQUIRE(e.text_string("lies, damn lies and statistics").has_value());
    });
    std::string const expected = as_string({
        0xa3, 0x6d, 0x66, 0x69, 0x72, 0x73, 0x74, 0x20, 0x69, 0x6e, 0x74, 0x65, 0x67, 0x65, 0x72, 0x18, 0x2a, 0x77,
        0x61, 0x6e, 0x20, 0x61, 0x72, 0x72, 0x61, 0x79, 0x20, 0x6f, 0x66, 0x20, 0x74, 0x77, 0x6f, 0x20, 0x73, 0x74,
        0x72, 0x69, 0x6e, 0x67, 0x73, 0x82, 0x67, 0x73, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x31, 0x67, 0x73, 0x74, 0x72,
        0x69, 0x6e, 0x67, 0x32, 0x6c, 0x6d, 0x61, 0x70, 0x20, 0x69, 0x6e, 0x20, 0x61, 0x20, 0x6d, 0x61, 0x70, 0xa4,
        0x67, 0x62, 0x79, 0x74, 0x65, 0x73, 0x20, 0x31, 0x44, 0x78, 0x78, 0x78, 0x78, 0x67, 0x62, 0x79, 0x74, 0x65,
        0x73, 0x20, 0x32, 0x44, 0x79, 0x79, 0x79, 0x79, 0x6b, 0x61, 0x6e, 0x6f, 0x74, 0x68, 0x65, 0x72, 0x20, 0x69,
        0x6e, 0x74, 0x18, 0x62, 0x66, 0x74, 0x65, 0x78, 0x74, 0x20, 0x32, 0x78, 0x1e, 0x6c, 0x69, 0x65, 0x73, 0x2c,
        0x20, 0x64, 0x61, 0x6d, 0x6e, 0x20, 0x6c, 0x69, 0x65, 0x73, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x73, 0x74, 0x61,
        0x74, 0x69, 0x73, 0x74, 0x69, 0x63, 0x73,
    });
    CHECK_EQ(hex_of(out), hex_of(expected));
}

// BasicEncodeTest: {66: true}, then [451, {66: true}, {-70000: {66: true}}] with the first map inserted as it is.
// The bytes of the second item stand in the comment of upstream: 83 19 01c3 a1 18 42 f5 a1 3a 0001116f a1 18 42 f5.
TEST_CASE("qcbor BasicEncodeTest: a map with one boolean, inserted twice into an array")
{
    std::string const first = written([](cbor::encoder<std::string> &e) {
        REQUIRE(e.map(1).has_value());
        REQUIRE(e.unsigned_integer(std::uint8_t{66}).has_value());
        REQUIRE(e.simple_value(cbor::simple_value::true_value).has_value());
    });
    CHECK_EQ(hex_of(first), hex_of(as_string({0xa1, 0x18, 0x42, 0xf5})));
    auto const one = cbor::lazy::adopt(std::string(first));
    REQUIRE(one.has_value());
    CHECK(one->at(cbor::key{66}).and_then([](cbor::lazy const &l) { return l.get<bool>(); }) == true);

    std::string const second = array_head(3) + preferred(std::uint64_t{451}) + first + map_head(1) +
                               preferred(std::int64_t{-70000}) + first;
    CHECK_EQ(hex_of(second), hex_of(as_string({0x83, 0x19, 0x01, 0xc3, 0xa1, 0x18, 0x42, 0xf5, 0xa1, 0x3a, 0x00,
                                               0x01, 0x11, 0x6f, 0xa1, 0x18, 0x42, 0xf5})));
    auto const two = cbor::lazy::adopt(std::string(second));
    REQUIRE(two.has_value());
    CHECK(two->size() == 3u);
    CHECK(two->at(0).and_then([](cbor::lazy const &l) { return l.get<std::int64_t>(); }) == 451);
    CHECK(two->at(1).and_then([](cbor::lazy const &l) { return l.at(cbor::key{66}); }).and_then([](cbor::lazy const &l) {
        return l.get<bool>();
    }) == true);
    auto const inner = two->at(2).and_then([](cbor::lazy const &l) { return l.at(cbor::key{-70000}); });
    CHECK(inner.and_then([](cbor::lazy const &l) { return l.size(); }) == 1u);
    CHECK(inner.and_then([](cbor::lazy const &l) { return l.at(cbor::key{66}); }).and_then([](cbor::lazy const &l) {
        return l.get<bool>();
    }) == true);
}

// example.c: the engine of RunQCborExample encodes as a map with a text, three integers, a double, an array of six
// doubles and a boolean, and decodes back by key to the same values. Upstream compares the decoded engine with the
// first; it checks no bytes.
TEST_CASE("qcbor example.c: the car engine encodes and decodes back")
{
    std::vector<double> const cylinders = {9.0, 9.2, 8.9, 8.9, 9.1, 9.0};
    std::string out = map_head(7);
    out += text("Manufacturer") + text("Porsche");
    out += text("NumCylinders") + preferred(std::int64_t{6});
    out += text("Displacement") + preferred(std::int64_t{3296});
    out += text("Horsepower") + preferred(std::int64_t{210});
    out += text("DesignedCompression") + preferred(9.1);
    out += text("Cylinders") + array_head(cylinders.size());
    for (double const c : cylinders)
        out += preferred(c);
    out += text("Turbo") + preferred(false);

    auto const engine = cbor::lazy::adopt(std::move(out));
    REQUIRE(engine.has_value());
    auto field = [&engine](std::string_view const key) {
        auto const f = engine->at(key);
        REQUIRE(f.has_value());
        return *f;
    };
    auto const manufacturer = field("Manufacturer").get<std::string_view>();
    REQUIRE(manufacturer.has_value());
    CHECK_EQ(**manufacturer, "Porsche");
    CHECK(field("Displacement").get<std::int64_t>() == 3296);
    CHECK(field("Horsepower").get<std::int64_t>() == 210);
    CHECK_EQ(std::bit_cast<std::uint64_t>(field("DesignedCompression").get<double>().value_or(0.0)),
             std::bit_cast<std::uint64_t>(9.1));
    CHECK(field("Turbo").get<bool>() == false);
    CHECK(field("NumCylinders").get<std::int64_t>() == 6);
    auto const array = field("Cylinders");
    REQUIRE(array.size() == cylinders.size());
    std::size_t i = 0;
    for (double const c : cylinders) {
        auto const read = array.at(i).and_then([](cbor::lazy const &l) { return l.get<double>(); });
        REQUIRE(read.has_value());
        CHECK_EQ(std::bit_cast<std::uint64_t>(*read), std::bit_cast<std::uint64_t>(c));
        ++i;
    }
}
