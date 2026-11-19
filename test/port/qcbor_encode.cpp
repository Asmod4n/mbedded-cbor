// Port of the encode tests of QCBOR (https://github.com/laurencelundblade/QCBOR), file test/qcbor_encode_tests.c,
// and of the example program example.c, at commit 1d060574db3d58aac439bc3b8a60167951c7e959. The tests exist to show
// that mbedded-cbor writes the same bytes as QCBOR for the same values. Each expected value comes from the upstream
// test and is checked against RFC 8949 in refs/rfc8949.txt.
//
// QCBOR writes integers in preferred serialization (RFC 8949 4.1), and cbor::encode writes the preferred
// serialization (structs.hpp: "Writes @p value as one CBOR item in preferred serialization"). So a test writes the
// values of upstream with cbor::structs, and a decoded item with cbor::encode. The library writes no single head, so
// an upstream test that writes heads one call at a time has no port.
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

#include "../hex.hpp"

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

template <class T>
std::string encode_in_preferred_form(T const &value)
{
    auto const out = cbor::encode<T>(value);
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
    CHECK_EQ(test::hexlify(encode_in_preferred_form(encoded_int_values)), test::hexlify(expected_encoded_ints));
}

// EncodeDateTest: dates of RFC 8949 3.4.1 (tag 0), 3.4.2 (tag 1) and RFC 8943 (tag 100 and tag 1004). The epoch
// date comes from cbor::structs<std::chrono::sys_seconds> ("tag 1 on an integer"), and the days from
// cbor::structs<std::chrono::year_month_day> ("tag 100 on an integer of days"): -10676 days after 1970-01-01 is
// 1940-10-09. The array of upstream with the tags 0 and 1004 is written one head at a time, so it has no port.
TEST_CASE("qcbor EncodeDateTest: date strings, epoch dates and days encode under their tags")
{
    using namespace std::chrono;
    std::string const epoch = encode_in_preferred_form(sys_seconds{seconds{1363896240}});
    CHECK_EQ(test::hexlify(epoch), test::hexlify(as_string({0xc1, 0x1a, 0x51, 0x4b, 0x67, 0xb0})));
    std::string const days = encode_in_preferred_form(year_month_day{year{1940}, month{10}, day{9}});
    CHECK_EQ(test::hexlify(days), test::hexlify(as_string({0xd8, 0x64, 0x39, 0x29, 0xb3})));

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
    std::string out = as_string({0x82});
    for (std::string const &raw : {five_arrays, expected_encoded_ints}) {
        auto const tree = cbor::decode(raw);
        REQUIRE(tree.has_value());
        CHECK(cbor::encode(out, **tree) == raw.size());
    }
    CHECK_EQ(test::hexlify(out), test::hexlify(as_string({0x82}) + five_arrays + expected_encoded_ints));
}
