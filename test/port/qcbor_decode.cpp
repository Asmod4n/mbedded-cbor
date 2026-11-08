// Port of the decode tests of QCBOR (https://github.com/laurencelundblade/QCBOR), file
// test/qcbor_decode_tests.c and test/not_well_formed_cbor.h, at commit 1d060574db3d58aac439bc3b8a60167951c7e959.
// The tests exist to show that mbedded-cbor gives the same results as QCBOR on the same inputs. Each expected
// value comes from the upstream test and is checked against RFC 8949 in refs/rfc8949.txt. Where mbedded-cbor
// documents another behaviour, the test checks the documented behaviour, and its comment names the upstream case,
// the documentation line and the RFC section.
//
// The license of test/qcbor_decode_tests.c follows, verbatim.
//
// Copyright (c) 2016-2018, The Linux Foundation.
// Copyright (c) 2018-2025, Laurence Lundblade.
// Copyright (c) 2021, Arm Limited.
// All rights reserved.
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
// test/not_well_formed_cbor.h carries "Copyright (c) 2019, Laurence Lundblade. All rights reserved." and
// "SPDX-License-Identifier: BSD-3-Clause", with the BSD 3-Clause License of the file LICENSE of QCBOR:
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

#include "../small_stack.hpp"
#include "../reference/value.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
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

std::string as_string(std::vector<std::uint8_t> const &bytes)
{
    std::string out;
    for (std::uint8_t const b : bytes)
        out.push_back(static_cast<char>(b));
    return out;
}

std::string as_string(std::span<std::byte const> const bytes)
{
    std::string out;
    for (std::byte const b : bytes)
        out.push_back(static_cast<char>(b));
    return out;
}

std::expected<cbor::lazy, error> lazy_of(std::string encoded)
{
    return cbor::lazy::adopt(std::move(encoded));
}

template <class T>
std::expected<T, error> read_as(std::expected<cbor::lazy, error> const &at)
{
    return at.and_then([](cbor::lazy const &l) { return l.get<T>(); });
}

std::expected<std::string, error> text_of(std::expected<cbor::lazy, error> const &at)
{
    return at.and_then([](cbor::lazy const &l) { return l.get<std::string_view>(); })
        .transform([](cbor::ref<std::string_view> const &r) { return std::string(*r); });
}

std::expected<std::string, error> bytes_of(std::expected<cbor::lazy, error> const &at)
{
    return at.and_then([](cbor::lazy const &l) { return l.get<std::span<std::byte const>>(); })
        .transform([](cbor::ref<std::span<std::byte const>> const &r) { return as_string(*r); });
}

std::expected<std::uint64_t, error> count_of(std::expected<cbor::lazy, error> const &at)
{
    return at.and_then([](cbor::lazy const &l) { return l.size(); });
}

std::span<cbor::item const> elements_of(cbor::item const &node)
{
    auto const a = node.get<cbor::type::array>();
    REQUIRE(a.has_value());
    return *a;
}

std::span<cbor::item const> entries_of(cbor::item const &node)
{
    auto const m = node.get<cbor::type::map>();
    REQUIRE(m.has_value());
    return *m;
}

std::string text_in(cbor::item const &node)
{
    auto const t = node.get<cbor::type::text_string>();
    REQUIRE(t.has_value());
    return std::string(*t);
}

// [-9223372036854775808, -4294967297, ..., 9223372036854775807, 18446744073709551615]: spExpectedEncodedInts.
std::string const expected_encoded_ints = as_string({
    0x98, 0x2f, 0x3b, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x3a, 0xff, 0xff, 0xff, 0xff, 0x3a, 0xff, 0xff, 0xff, 0xfe, 0x3a, 0xff, 0xff, 0xff, 0xfd, 0x3a, 0x7f, 0xff,
    0xff, 0xff, 0x3a, 0x7f, 0xff, 0xff, 0xfe, 0x3a, 0x00, 0x01, 0x00, 0x01, 0x3a, 0x00, 0x01, 0x00, 0x00, 0x39, 0xff,
    0xff, 0x39, 0xff, 0xfe, 0x39, 0xff, 0xfd, 0x39, 0x01, 0x00, 0x38, 0xff, 0x38, 0xfe, 0x38, 0xfd, 0x38, 0x18, 0x37,
    0x36, 0x20, 0x00, 0x00, 0x01, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19, 0x18, 0x1a, 0x18, 0xfe, 0x18, 0xff, 0x19, 0x01,
    0x00, 0x19, 0x01, 0x01, 0x19, 0xff, 0xfe, 0x19, 0xff, 0xff, 0x1a, 0x00, 0x01, 0x00, 0x00, 0x1a, 0x00, 0x01, 0x00,
    0x01, 0x1a, 0x00, 0x01, 0x00, 0x02, 0x1a, 0x7f, 0xff, 0xff, 0xff, 0x1a, 0x7f, 0xff, 0xff, 0xff, 0x1a, 0x80, 0x00,
    0x00, 0x00, 0x1a, 0x80, 0x00, 0x00, 0x01, 0x1a, 0xff, 0xff, 0xff, 0xfe, 0x1a, 0xff, 0xff, 0xff, 0xff, 0x1b, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x1b, 0x7f, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});

// {"first integer": 42, "an array of two strings": ["string1", "string2"], "map in a map": {"bytes 1": h'78787878',
// "bytes 2": h'79797979', "another int": 98, "text 2": "lies, damn lies and statistics"}}: pValidMapEncoded.
std::string const valid_map_encoded = as_string({
    0xa3, 0x6d, 0x66, 0x69, 0x72, 0x73, 0x74, 0x20, 0x69, 0x6e, 0x74, 0x65, 0x67, 0x65, 0x72, 0x18, 0x2a, 0x77, 0x61,
    0x6e, 0x20, 0x61, 0x72, 0x72, 0x61, 0x79, 0x20, 0x6f, 0x66, 0x20, 0x74, 0x77, 0x6f, 0x20, 0x73, 0x74, 0x72, 0x69,
    0x6e, 0x67, 0x73, 0x82, 0x67, 0x73, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x31, 0x67, 0x73, 0x74, 0x72, 0x69, 0x6e, 0x67,
    0x32, 0x6c, 0x6d, 0x61, 0x70, 0x20, 0x69, 0x6e, 0x20, 0x61, 0x20, 0x6d, 0x61, 0x70, 0xa4, 0x67, 0x62, 0x79, 0x74,
    0x65, 0x73, 0x20, 0x31, 0x44, 0x78, 0x78, 0x78, 0x78, 0x67, 0x62, 0x79, 0x74, 0x65, 0x73, 0x20, 0x32, 0x44, 0x79,
    0x79, 0x79, 0x79, 0x6b, 0x61, 0x6e, 0x6f, 0x74, 0x68, 0x65, 0x72, 0x20, 0x69, 0x6e, 0x74, 0x18, 0x62, 0x66, 0x74,
    0x65, 0x78, 0x74, 0x20, 0x32, 0x78, 0x1e, 0x6c, 0x69, 0x65, 0x73, 0x2c, 0x20, 0x64, 0x61, 0x6d, 0x6e, 0x20, 0x6c,
    0x69, 0x65, 0x73, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x73, 0x74, 0x61, 0x74, 0x69, 0x73, 0x74, 0x69, 0x63, 0x73});

// The same map with indefinite lengths: pValidMapIndefEncoded.
std::string const valid_map_indefinite_encoded = as_string({
    0xbf, 0x6d, 0x66, 0x69, 0x72, 0x73, 0x74, 0x20, 0x69, 0x6e, 0x74, 0x65, 0x67, 0x65, 0x72, 0x18, 0x2a, 0x77, 0x61,
    0x6e, 0x20, 0x61, 0x72, 0x72, 0x61, 0x79, 0x20, 0x6f, 0x66, 0x20, 0x74, 0x77, 0x6f, 0x20, 0x73, 0x74, 0x72, 0x69,
    0x6e, 0x67, 0x73, 0x9f, 0x67, 0x73, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x31, 0x67, 0x73, 0x74, 0x72, 0x69, 0x6e, 0x67,
    0x32, 0xff, 0x6c, 0x6d, 0x61, 0x70, 0x20, 0x69, 0x6e, 0x20, 0x61, 0x20, 0x6d, 0x61, 0x70, 0xbf, 0x67, 0x62, 0x79,
    0x74, 0x65, 0x73, 0x20, 0x31, 0x44, 0x78, 0x78, 0x78, 0x78, 0x67, 0x62, 0x79, 0x74, 0x65, 0x73, 0x20, 0x32, 0x44,
    0x79, 0x79, 0x79, 0x79, 0x6b, 0x61, 0x6e, 0x6f, 0x74, 0x68, 0x65, 0x72, 0x20, 0x69, 0x6e, 0x74, 0x18, 0x62, 0x66,
    0x74, 0x65, 0x78, 0x74, 0x20, 0x32, 0x78, 0x1e, 0x6c, 0x69, 0x65, 0x73, 0x2c, 0x20, 0x64, 0x61, 0x6d, 0x6e, 0x20,
    0x6c, 0x69, 0x65, 0x73, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x73, 0x74, 0x61, 0x74, 0x69, 0x73, 0x74, 0x69, 0x63, 0x73,
    0xff, 0xff});

} // namespace

// IntegerValuesParseTest: each head width of major types 0 and 1 decodes to its value (RFC 8949 3.1). The last
// element is larger than INT64_MAX, so it is read as an unsigned 64-bit integer, as upstream does.
TEST_CASE("qcbor IntegerValuesParseTest: integers of every head width decode to their values")
{
    std::vector<std::int64_t> const expected = {
        std::numeric_limits<std::int64_t>::min(),
        -4294967297LL,
        -4294967296LL,
        -4294967295LL,
        -4294967294LL,
        -2147483648LL,
        -2147483647LL,
        -65538,
        -65537,
        -65536,
        -65535,
        -65534,
        -257,
        -256,
        -255,
        -254,
        -25,
        -24,
        -23,
        -1,
        0,
        0,
        1,
        22,
        23,
        24,
        25,
        26,
        254,
        255,
        256,
        257,
        65534,
        65535,
        65536,
        65537,
        65538,
        2147483647LL,
        2147483647LL,
        2147483648LL,
        2147483649LL,
        4294967294LL,
        4294967295LL,
        4294967296LL,
        4294967297LL,
        9223372036854775807LL,
    };
    auto const doc = lazy_of(expected_encoded_ints);
    REQUIRE(doc.has_value());
    CHECK(doc->size() == expected.size() + 1);
    std::size_t index = 0;
    for (std::int64_t const v : expected) {
        CAPTURE(index);
        CHECK(read_as<std::int64_t>(doc->at(index)) == v);
        ++index;
    }
    CHECK(read_as<std::uint64_t>(doc->at(index)) == std::numeric_limits<std::uint64_t>::max());
    CHECK(read_as<std::int64_t>(doc->at(index)) == std::unexpected(error::number_out_of_range));

    auto const tree = cbor::decode(expected_encoded_ints);
    REQUIRE(tree.has_value());
    CHECK_EQ(elements_of(**tree).size(), expected.size() + 1);
}

// IntegerValuesParseTest, spTooSmallNegative: 3b 8000000000000000 is -9223372036854775809. RFC 8949 3.1 allows it,
// but int64_t cannot hold it. Upstream gives QCBOR_ERR_INT_OVERFLOW from GetNext, because the QCBOR item holds an
// int64_t. The read as int64_t here gives number_out_of_range (lazy.hpp, get: "number_out_of_range the number does
// not fit T"). cbor::decode keeps the argument (item.hpp: "A negative integer gives its argument n, for the value
// -1 - n"), so the decode succeeds where QCBOR stops.
TEST_CASE("qcbor IntegerValuesParseTest: a negative integer below INT64_MIN does not fit int64_t")
{
    std::string const too_small = as_string({0x3b, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    CHECK(read_as<std::int64_t>(lazy_of(too_small)) == std::unexpected(error::number_out_of_range));
    auto const tree = cbor::decode(too_small);
    REQUIRE(tree.has_value());
    CHECK((**tree).type == cbor::type::negative_integer);
    CHECK((**tree).get<cbor::type::negative_integer>() == std::uint64_t{0x8000000000000000});
}

// ShortBufferParseTest: every truncation of the integer array is incomplete. RFC 8949 Appendix F calls this "too
// little data". Upstream accepts QCBOR_ERR_HIT_END or QCBOR_ERR_NO_MORE_ITEMS; both mean the same kind.
TEST_CASE("qcbor ShortBufferParseTest: each truncation of the integer array gives too_little_data")
{
    for (std::size_t n = expected_encoded_ints.size() - 1; n != 0; --n) {
        CAPTURE(n);
        std::string const prefix = expected_encoded_ints.substr(0, n);
        CHECK(cbor::decode(prefix).error_or(error{}) == error::too_little_data);
        CHECK(cbor::item_size(prefix).error_or(error{}) == error::too_little_data);
    }
}

// SimpleArrayTest and ShortBufferParseTest2: [23, 6000, h'galactic', h'haven token'] encodes and decodes back, and
// each truncation of it is incomplete (RFC 8949 Appendix F, too little data).
TEST_CASE("qcbor SimpleArrayTest: a simple array encodes, decodes back, and each truncation fails")
{
    std::string const galactic = "galactic";
    std::string const haven = "haven token";
    using row = std::tuple<std::int64_t, std::int64_t, std::span<std::byte const>, std::span<std::byte const>>;
    auto const encoded =
        cbor::structs<row>::encode(row{23, 6000, std::as_bytes(std::span(galactic)), std::as_bytes(std::span(haven))});
    REQUIRE(encoded.has_value());

    auto const doc = lazy_of(*encoded);
    REQUIRE(doc.has_value());
    CHECK(doc->size() == 4u);
    CHECK(read_as<std::int64_t>(doc->at(0)) == 23);
    CHECK(read_as<std::int64_t>(doc->at(1)) == 6000);
    CHECK(bytes_of(doc->at(2)) == galactic);
    CHECK(bytes_of(doc->at(3)) == haven);

    for (std::size_t n = encoded->size() - 1; n != 0; --n) {
        CAPTURE(n);
        CHECK(cbor::decode(encoded->substr(0, n)).error_or(error{}) == error::too_little_data);
    }
}

// EmptyMapsAndArraysTest: [0, [], [[], [0], {}, {1: {}, 2: {}, 3: []}]] decodes with each empty container.
TEST_CASE("qcbor EmptyMapsAndArraysTest: empty arrays and maps decode at each level")
{
    std::string const empties =
        as_string({0x83, 0x00, 0x80, 0x84, 0x80, 0x81, 0x00, 0xa0, 0xa3, 0x01, 0xa0, 0x02, 0xa0, 0x03, 0x80});
    auto const tree = cbor::decode(empties);
    REQUIRE(tree.has_value());
    auto const top = elements_of(**tree);
    REQUIRE_EQ(top.size(), 3u);
    CHECK(top[0].get<cbor::type::unsigned_integer>() == 0u);
    CHECK(elements_of(top[1]).empty());
    auto const inner = elements_of(top[2]);
    REQUIRE_EQ(inner.size(), 4u);
    CHECK(elements_of(inner[0]).empty());
    REQUIRE_EQ(elements_of(inner[1]).size(), 1u);
    CHECK(elements_of(inner[1])[0].get<cbor::type::unsigned_integer>() == 0u);
    CHECK(entries_of(inner[2]).empty());
    auto const three = entries_of(inner[3]);
    REQUIRE_EQ(three.size(), 6u);
    CHECK(three[0].get<cbor::type::unsigned_integer>() == 1u);
    CHECK(entries_of(three[1]).empty());
    CHECK(three[2].get<cbor::type::unsigned_integer>() == 2u);
    CHECK(entries_of(three[3]).empty());
    CHECK(three[4].get<cbor::type::unsigned_integer>() == 3u);
    CHECK(elements_of(three[5]).empty());
}

// EmptyMapsAndArraysTest with sEmptiesIndef: the same structure with indefinite lengths. Upstream decodes it.
// cbor::decode refuses indefinite lengths (decode.hpp: "indefinite_length a string, an array or a map has an
// indefinite length"); RFC 8949 3.2.2 makes them well-formed, and cbor::diagnostic_notation reads them
// (diagnostic_notation.hpp: "Indefinite length is printed"). The text follows RFC 8949 8.1 and Appendix A
// ("[_ ]" is 0x9fff).
TEST_CASE("qcbor EmptyMapsAndArraysTest: the indefinite-length form is well-formed and decode refuses it")
{
    std::string const empties = as_string({0x9f, 0x00, 0x9f, 0xff, 0x9f, 0x9f, 0xff, 0x9f, 0x00, 0xff, 0xbf, 0xff, 0xbf,
                                           0x01, 0xbf, 0xff, 0x02, 0xbf, 0xff, 0x03, 0x9f, 0xff, 0xff, 0xff, 0xff});
    CHECK(cbor::decode(empties).error_or(error{}) == error::indefinite_length);
    CHECK(cbor::diagnostic_notation(empties) ==
          std::string("[_ 0, [_ ], [_ [_ ], [_ 0], {_ }, {_ 1: {_ }, 2: {_ }, 3: [_ ]}]]"));
}

// ParseEmptyMapInMapTest: {2: {}}: the empty map is the value of the integer key 2.
TEST_CASE("qcbor ParseEmptyMapInMapTest: an empty map is the value of key 2")
{
    auto const doc = lazy_of(as_string({0xa1, 0x02, 0xa0}));
    REQUIRE(doc.has_value());
    auto const inner = doc->at(cbor::key{2});
    REQUIRE(inner.has_value());
    CHECK(inner->type() == cbor::type::map);
    CHECK(inner->size() == 0u);
}

// ParseDeepArrayTest: [[[[[[[]]]]]]] decodes with an array at each of 7 levels.
TEST_CASE("qcbor ParseDeepArrayTest: seven nested arrays decode")
{
    auto const tree = cbor::decode(as_string({0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x80}));
    REQUIRE(tree.has_value());
    cbor::item const *at = &**tree;
    for (int level = 0; level != 6; ++level) {
        auto const e = elements_of(*at);
        REQUIRE_EQ(e.size(), 1u);
        at = e.data();
    }
    CHECK(elements_of(*at).empty());
}

// ParseTooDeepArrayTest: upstream writes nested heads 0x81 and no last element, and expects
// QCBOR_ERR_ARRAY_DECODE_NESTING_TOO_DEEP at its own limit. The depth here is the one of this library,
// test::levels_main_thread_reads (small_stack.hpp), not the limit of upstream. mbedded-cbor has no fixed
// nesting limit: its limit is the stack of the thread (error.hpp: "nesting_depth_exceeded A reader that recurses
// comes to the end of the stack of its thread"). RFC 8949 10 asks a decoder to manage the stack depth, and does not
// fix a number. So the input is read to its end and is incomplete (RFC 8949 Appendix F, too little data). The same
// heads with the last element 0x80 decode.
TEST_CASE("qcbor ParseTooDeepArrayTest: nested arrays are within the limit of mbedded-cbor")
{
    std::string const open(test::levels_main_thread_reads, static_cast<char>(0x81));
    CHECK(cbor::decode(open).error_or(error{}) == error::too_little_data);
    CHECK(cbor::decode(open + static_cast<char>(0x80)).has_value());
}

// ParseMapTest with ParseMapTest1: the map pValidMapEncoded decodes with each key, each value and the order of the
// pairs.
TEST_CASE("qcbor ParseMapTest: a map with nested array and map decodes")
{
    auto const tree = cbor::decode(valid_map_encoded);
    REQUIRE(tree.has_value());
    auto const top = entries_of(**tree);
    REQUIRE_EQ(top.size(), 6u);
    CHECK_EQ(text_in(top[0]), "first integer");
    CHECK(top[1].get<cbor::type::unsigned_integer>() == 42u);
    CHECK_EQ(text_in(top[2]), "an array of two strings");
    auto const strings = elements_of(top[3]);
    REQUIRE_EQ(strings.size(), 2u);
    CHECK_EQ(text_in(strings[0]), "string1");
    CHECK_EQ(text_in(strings[1]), "string2");
    CHECK_EQ(text_in(top[4]), "map in a map");
    auto const inner = entries_of(top[5]);
    REQUIRE_EQ(inner.size(), 8u);
    CHECK_EQ(text_in(inner[0]), "bytes 1");
    auto const b1 = inner[1].get<cbor::type::byte_string>();
    REQUIRE(b1.has_value());
    CHECK_EQ(as_string(*b1), "xxxx");
    CHECK_EQ(text_in(inner[2]), "bytes 2");
    auto const b2 = inner[3].get<cbor::type::byte_string>();
    REQUIRE(b2.has_value());
    CHECK_EQ(as_string(*b2), "yyyy");
    CHECK_EQ(text_in(inner[4]), "another int");
    CHECK(inner[5].get<cbor::type::unsigned_integer>() == 98u);
    CHECK_EQ(text_in(inner[6]), "text 2");
    CHECK_EQ(text_in(inner[7]), "lies, damn lies and statistics");

    auto const doc = lazy_of(valid_map_encoded);
    REQUIRE(doc.has_value());
    CHECK(doc->size() == 3u);
    CHECK(read_as<std::int64_t>(doc->at("first integer")) == 42);
    CHECK(count_of(doc->at("an array of two strings")) == 2u);
    auto const map = doc->at("map in a map");
    CHECK(count_of(map) == 4u);
    REQUIRE(map.has_value());
    CHECK(bytes_of(map->at("bytes 1")) == std::string("xxxx"));
    CHECK(bytes_of(map->at("bytes 2")) == std::string("yyyy"));
    CHECK(read_as<std::int64_t>(map->at("another int")) == 98);
    CHECK(text_of(map->at("text 2")) == std::string("lies, damn lies and statistics"));
}

// ParseMapTest with pValidMapIndefEncoded (NestedMapTestIndefLen uses the same form): upstream decodes it.
// cbor::decode refuses indefinite lengths (decode.hpp: "indefinite_length a string, an array or a map has an
// indefinite length"). RFC 8949 3.2.2 makes the input well-formed, and cbor::diagnostic_notation reads it.
TEST_CASE("qcbor ParseMapTest: the indefinite-length map is well-formed and decode refuses it")
{
    CHECK(cbor::decode(valid_map_indefinite_encoded).error_or(error{}) == error::indefinite_length);
    CHECK(cbor::diagnostic_notation(valid_map_indefinite_encoded) ==
          std::string("{_ \"first integer\": 42, \"an array of two strings\": [_ \"string1\", \"string2\"], "
                      "\"map in a map\": {_ \"bytes 1\": h'78787878', \"bytes 2\": h'79797979', \"another int\": 98, "
                      "\"text 2\": \"lies, damn lies and statistics\"}}"));
}

// ParseMapTest, spLargeArrayFake and the three other heads: an array or map head with a count and no elements.
// Upstream reads the head of 65534 elements, and refuses 65535 elements and 32768 pairs with
// QCBOR_ERR_ARRAY_DECODE_TOO_LONG, its own limit. mbedded-cbor compares the count with the bytes that are left
// (lazy.hpp, size: "too_little_data the count is larger than the bytes that are left"), so each head gives
// too_little_data. RFC 8949 Appendix F calls an array without its elements "too little data".
TEST_CASE("qcbor ParseMapTest: a large count with no elements gives too_little_data")
{
    for (auto const &head : {as_string({0x99, 0xff, 0xfe}), as_string({0x99, 0xff, 0xff}), as_string({0xb9, 0x7f, 0xff}),
                             as_string({0xba, 0x00, 0x00, 0x80, 0x00})}) {
        CAPTURE(head.size());
        CHECK(count_of(lazy_of(head)) == std::unexpected(error::too_little_data));
        CHECK(cbor::decode(head).error_or(error{}) == error::too_little_data);
    }
}

// SimpleValueDecodeTests, spSimpleValues: each element on its own. RFC 8949 3.3: e0 and f3 are the simple values 0
// and 19, f8 20 and f8 ff are 32 and 255, and f8 with a value under 32 is not well-formed (Appendix F, syntax
// error). A break outside an indefinite-length item is a syntax error too. Upstream reads the elements one after the
// other and continues after an error; cbor::decode stops at the first error, so the array as a whole gives
// syntax_error.
TEST_CASE("qcbor SimpleValueDecodeTests: simple values decode, and the reserved two-byte forms fail")
{
    auto simple_of = [](std::string const &in) {
        return cbor::decode(in).and_then(
            [](cbor::owning<cbor::item> const &t) { return t->get<cbor::type::simple_value>(); });
    };
    auto const f = cbor::decode(as_string({0xf4}));
    REQUIRE(f.has_value());
    CHECK((**f).type == cbor::type::false_value);
    auto const t = cbor::decode(as_string({0xf5}));
    REQUIRE(t.has_value());
    CHECK((**t).type == cbor::type::true_value);
    auto const n = cbor::decode(as_string({0xf6}));
    REQUIRE(n.has_value());
    CHECK((**n).type == cbor::type::null);
    auto const u = cbor::decode(as_string({0xf7}));
    REQUIRE(u.has_value());
    CHECK((**u).type == cbor::type::undefined);
    CHECK(cbor::decode(as_string({0xff})).error_or(error{}) == error::syntax_error);
    CHECK(simple_of(as_string({0xe0})) == cbor::simple_value{0});
    CHECK(simple_of(as_string({0xf3})) == cbor::simple_value{19});
    CHECK(cbor::decode(as_string({0xf8, 0x00})).error_or(error{}) == error::syntax_error);
    CHECK(cbor::decode(as_string({0xf8, 0x13})).error_or(error{}) == error::syntax_error);
    CHECK(cbor::decode(as_string({0xf8, 0x1f})).error_or(error{}) == error::syntax_error);
    CHECK(simple_of(as_string({0xf8, 0x20})) == cbor::simple_value{32});
    CHECK(simple_of(as_string({0xf8, 0xff})) == cbor::simple_value{255});

    std::string const all = as_string(
        {0x8a, 0xf4, 0xf5, 0xf6, 0xf7, 0xff, 0xe0, 0xf3, 0xf8, 0x00, 0xf8, 0x13, 0xf8, 0x1f, 0xf8, 0x20, 0xf8, 0xff});
    CHECK(cbor::decode(all).error_or(error{}) == error::syntax_error);
}

// SimpleValueDecodeTests, spGoodSimpleValues: the map {1: false, 2: true, 3: null, 4: undefined, 5: simple(0),
// 6: simple(19), 7: simple(32), "@": simple(255), 15: 15}. RFC 8949 3.3 gives false, true, null and undefined the
// simple values 20 to 23. A key that is not in the map gives key_not_found (upstream QCBOR_ERR_LABEL_NOT_FOUND), and
// an integer read as a simple value gives incorrect_type (upstream QCBOR_ERR_UNEXPECTED_TYPE).
TEST_CASE("qcbor SimpleValueDecodeTests: simple values are found by key")
{
    auto const doc = lazy_of(as_string({0xa9, 0x01, 0xf4, 0x02, 0xf5, 0x03, 0xf6, 0x04, 0xf7, 0x05, 0xe0, 0x06,
                                        0xf3, 0x07, 0xf8, 0x20, 0x61, 0x40, 0xf8, 0xff, 0x0f, 0x0f}));
    REQUIRE(doc.has_value());
    CHECK(read_as<cbor::simple_value>(doc->at(cbor::key{1})) == cbor::simple_value::false_value);
    CHECK(read_as<cbor::simple_value>(doc->at(cbor::key{2})) == cbor::simple_value::true_value);
    CHECK(read_as<cbor::simple_value>(doc->at(cbor::key{3})) == cbor::simple_value::null);
    CHECK(read_as<cbor::simple_value>(doc->at(cbor::key{4})) == cbor::simple_value::undefined);
    CHECK(read_as<cbor::simple_value>(doc->at(cbor::key{5})) == cbor::simple_value{0});
    CHECK(read_as<cbor::simple_value>(doc->at(cbor::key{6})) == cbor::simple_value{19});
    CHECK(read_as<cbor::simple_value>(doc->at(cbor::key{7})) == cbor::simple_value{32});
    CHECK(read_as<cbor::simple_value>(doc->at("@")) == cbor::simple_value{255});
    CHECK(doc->at(cbor::key{99}).error_or(error{}) == error::key_not_found);
    CHECK(doc->at("xx").error_or(error{}) == error::key_not_found);
    CHECK(read_as<cbor::simple_value>(doc->at(cbor::key{15})) == std::unexpected(error::incorrect_type));
}

namespace
{

struct not_well_formed {
    char const *description;
    std::vector<std::uint8_t> input;
    error expected;
};

// The union of paNotWellFormedCBOR (not_well_formed_cbor.h) and the not-well-formed rows of Failures
// (qcbor_decode_tests.c). The error is the kind of RFC 8949 Appendix F: too little data, or a syntax error. Upstream
// gives QCBOR_ERR_HIT_END or QCBOR_ERR_NO_MORE_ITEMS for the first kind, and QCBOR_ERR_BAD_BREAK,
// QCBOR_ERR_INDEFINITE_STRING_CHUNK, QCBOR_ERR_BAD_TYPE_7 or QCBOR_ERR_UNSUPPORTED for the second. Three rows of
// Failures have a fault in the C literal ("0x5f\x41\x00", "\x9\x01\x02", "\0xfa\x00\x00"), and one row has the
// length 2 for 4 bytes ("\xa2\x00\x00\x00"); these rows take the bytes of not_well_formed_cbor.h.
std::vector<not_well_formed> const not_well_formed_inputs = {
    {"An indefinite length byte string not closed off", {0x5f, 0x41, 0x00}, error::too_little_data},
    {"An indefinite length text string not closed off", {0x7f, 0x61, 0x00}, error::too_little_data},
    {"indefinite length byte string with text string chunk", {0x5f, 0x61, 0x00, 0xff}, error::syntax_error},
    {"indefinite length text string with a byte string chunk", {0x7f, 0x41, 0x00, 0xff}, error::syntax_error},
    {"indefinite length byte string with an positive integer chunk", {0x5f, 0x00, 0xff}, error::syntax_error},
    {"indefinite length byte string with an negative integer chunk", {0x5f, 0x21, 0xff}, error::syntax_error},
    {"indefinite length byte string with an array chunk", {0x5f, 0x80, 0xff}, error::syntax_error},
    {"indefinite length byte string with an map chunk", {0x5f, 0xa0, 0xff}, error::syntax_error},
    {"indefinite length byte string with tagged integer chunk", {0x5f, 0xc0, 0x00, 0xff}, error::syntax_error},
    {"indefinite length byte string with an simple type chunk", {0x5f, 0xe0, 0xff}, error::syntax_error},
    {"indefinite length byte string with indefinite string inside", {0x5f, 0x5f, 0x41, 0x00, 0xff, 0xff},
     error::syntax_error},
    {"indefinite length text string with indefinite string inside", {0x7f, 0x7f, 0x61, 0x00, 0xff, 0xff},
     error::syntax_error},
    {"A definte length array that is supposed to have 1 item, but has none", {0x81}, error::too_little_data},
    {"A definte length array that is supposed to have 2 items, but has only 1", {0x82, 0x00},
     error::too_little_data},
    {"A definte length array that is supposed to have 511 items, but has only 1", {0x9a, 0x01, 0xff, 0x00},
     error::too_little_data},
    {"A definte length map that is supposed to have 1 item, but has none", {0xa1}, error::too_little_data},
    {"A definte length map that is supposed to have s item, but has only 1", {0xa2, 0x01, 0x02},
     error::too_little_data},
    {"Indefinite length array with zero items and no break", {0x9f}, error::too_little_data},
    {"Indefinite length array with two items and no break", {0x9f, 0x01, 0x02}, error::too_little_data},
    {"Indefinite length map with zero items and no break", {0xbf}, error::too_little_data},
    {"Indefinite length map with two items and no break", {0xbf, 0x01, 0x02, 0x01, 0x02}, error::too_little_data},
    {"Unclosed indefinite array containing a closed definite length array", {0x9f, 0x80, 0x00},
     error::too_little_data},
    {"Definite length array containing an unclosed indefinite length array", {0x81, 0x9f}, error::too_little_data},
    {"Unclosed indefinite map containing a closed definite length array", {0xbf, 0x01, 0x80, 0x00, 0xa0},
     error::too_little_data},
    {"Definite length map containing an unclosed indefinite length array", {0xa1, 0x02, 0x9f},
     error::too_little_data},
    {"Deeply nested definite length arrays with deepest one unclosed", {0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81},
     error::too_little_data},
    {"Deeply nested indefinite length arrays with deepest one unclosed",
     {0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0xff, 0xff, 0xff, 0xff},
     error::too_little_data},
    {"Mixed nesting with indefinite unclosed", {0x9f, 0x81, 0x9f, 0x81, 0x9f, 0x9f, 0xff, 0xff, 0xff},
     error::too_little_data},
    {"Mixed nesting with definite unclosed", {0x9f, 0x82, 0x9f, 0x81, 0x9f, 0x9f, 0xff, 0xff, 0xff, 0xff},
     error::syntax_error},
    {"Unclosed indefinite length map in definite length maps", {0xa1, 0x01, 0xa2, 0x02, 0xbf, 0xff, 0x02, 0xbf},
     error::too_little_data},
    {"Unclosed definite length map in indefinite length maps", {0xbf, 0x01, 0xbf, 0x02, 0xa1},
     error::too_little_data},
    {"Unclosed indefinite length array in definite length maps", {0xa1, 0x01, 0xa2, 0x02, 0x9f, 0xff, 0x02, 0x9f},
     error::too_little_data},
    {"Unclosed definite length array in indefinite length maps", {0xbf, 0x01, 0xbf, 0x02, 0x81},
     error::too_little_data},
    {"Unclosed indefinite length map in definite length arrays", {0x81, 0x82, 0xbf, 0xff, 0xbf},
     error::too_little_data},
    {"Unclosed definite length map in indefinite length arrays", {0x9f, 0x9f, 0xa1}, error::too_little_data},
    {"Positive integer missing 1 byte argument", {0x18}, error::too_little_data},
    {"Positive integer missing 2 byte argument", {0x19}, error::too_little_data},
    {"Positive integer missing 4 byte argument", {0x1a}, error::too_little_data},
    {"Positive integer missing 8 byte argument", {0x1b}, error::too_little_data},
    {"Positive integer missing 1 byte of 2 byte argument", {0x19, 0x01}, error::too_little_data},
    {"Positive integer missing 2 bytes of 4 byte argument", {0x1a, 0x01, 0x02}, error::too_little_data},
    {"Positive integer missing 1 bytes of 7 byte argument", {0x1b, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
     error::too_little_data},
    {"Negative integer missing 1 byte argument", {0x38}, error::too_little_data},
    {"Binary string missing 1 byte argument", {0x58}, error::too_little_data},
    {"Text string missing 1 byte argument", {0x78}, error::too_little_data},
    {"Array missing 1 byte argument", {0x98}, error::too_little_data},
    {"Map missing 1 byte argument", {0xb8}, error::too_little_data},
    {"Tag missing 1 byte argument", {0xd8}, error::too_little_data},
    {"Simple missing 1 byte argument", {0xf8}, error::too_little_data},
    {"Half-precision missing 1 byte", {0xf9, 0x00}, error::too_little_data},
    {"Float missing 2 bytes", {0xfa, 0x00, 0x00}, error::too_little_data},
    {"Double missing 5 bytes", {0xfb, 0x00, 0x00, 0x00}, error::too_little_data},
    {"Tag with no content", {0xc0}, error::too_little_data},
    {"Array of length 1 with sole member replaced by a break", {0x81, 0xff}, error::syntax_error},
    {"Array of length 2 with 2nd member replaced by a break", {0x82, 0x00, 0xff}, error::syntax_error},
    {"Map of length 1 with sole member label replaced by a break (too_little_data: the map claims 2 items and 1 byte "
     "is left, and the decode doc gives too_little_data for a count larger than the bytes that are left)",
     {0xa1, 0xff},
     error::too_little_data},
    {"Alternate representation that some decoders handle differently", {0xa1, 0xff, 0x00}, error::syntax_error},
    {"Array of length 1 with 2nd member value replaced by a break", {0xa1, 0x00, 0xff}, error::syntax_error},
    {"Map of length 2 with 2nd entry label replaced by a break", {0xa2, 0x00, 0x00, 0xff, 0x00},
     error::syntax_error},
    {"Map of length 2 with 2nd entry value replaced by a break", {0xa2, 0x00, 0x00, 0x00, 0xff},
     error::syntax_error},
    {"Map of length 2 with 2nd entry value replaced by a break (Failures)", {0xa2, 0x00, 0x00, 0x01, 0xff},
     error::syntax_error},
    {"A bare break is not well formed", {0xff}, error::syntax_error},
    {"A bare break after a zero length definite length array", {0x80, 0xff}, error::syntax_error},
    {"A bare break after a zero length indefinite length map", {0x9f, 0xff, 0xff}, error::syntax_error},
    {"A break inside a definite length array inside an indefenite length array", {0x9f, 0x81, 0xff},
     error::syntax_error},
    {"Must use 0xe0 instead", {0xf8, 0x00}, error::syntax_error},
    {"Should use 0xe1 instead", {0xf8, 0x01}, error::syntax_error},
    {"Should use 0xe2 instead", {0xf8, 0x02}, error::syntax_error},
    {"Should use 0xe3 instead", {0xf8, 0x03}, error::syntax_error},
    {"Should use 0xe4 instead", {0xf8, 0x04}, error::syntax_error},
    {"Should use 0xe5 instead", {0xf8, 0x05}, error::syntax_error},
    {"Should use 0xe6 instead", {0xf8, 0x06}, error::syntax_error},
    {"Should use 0xe7 instead", {0xf8, 0x07}, error::syntax_error},
    {"Should use 0xe8 instead", {0xf8, 0x08}, error::syntax_error},
    {"Should use 0xe9 instead", {0xf8, 0x09}, error::syntax_error},
    {"Should use 0xea instead", {0xf8, 0x0a}, error::syntax_error},
    {"Should use 0xeb instead", {0xf8, 0x0b}, error::syntax_error},
    {"Should use 0xec instead", {0xf8, 0x0c}, error::syntax_error},
    {"Should use 0xed instead", {0xf8, 0x0d}, error::syntax_error},
    {"Should use 0xee instead", {0xf8, 0x0e}, error::syntax_error},
    {"Should use 0xef instead", {0xf8, 0x0f}, error::syntax_error},
    {"Should use 0xf0 instead", {0xf8, 0x10}, error::syntax_error},
    {"Should use 0xf1 instead", {0xf8, 0x11}, error::syntax_error},
    {"Should use 0xf2 instead", {0xf8, 0x12}, error::syntax_error},
    {"Must use 0xf3 instead", {0xf8, 0x13}, error::syntax_error},
    {"Must use 0xf4 instead", {0xf8, 0x14}, error::syntax_error},
    {"Must use 0xf5 instead", {0xf8, 0x15}, error::syntax_error},
    {"Must use 0xf6 instead", {0xf8, 0x16}, error::syntax_error},
    {"Must use 0xf7 instead", {0xf8, 0x17}, error::syntax_error},
    {"Reserved 0xf8 0x18", {0xf8, 0x18}, error::syntax_error},
    {"Reserved 0xf8 0x19", {0xf8, 0x19}, error::syntax_error},
    {"Reserved 0xf8 0x1a", {0xf8, 0x1a}, error::syntax_error},
    {"Reserved 0xf8 0x1b", {0xf8, 0x1b}, error::syntax_error},
    {"Reserved 0xf8 0x1c", {0xf8, 0x1c}, error::syntax_error},
    {"Reserved 0xf8 0x1d", {0xf8, 0x1d}, error::syntax_error},
    {"Reserved 0xf8 0x1e", {0xf8, 0x1e}, error::syntax_error},
    {"Reserved 0xf8 0x1f", {0xf8, 0x1f}, error::syntax_error},
    {"Positive integer with \"argument\" an indefinite length", {0x1f}, error::syntax_error},
    {"Negative integer with \"argument\" an indefinite length", {0x3f}, error::syntax_error},
    {"CBOR tag with \"argument\" an indefinite length", {0xdf, 0x00}, error::syntax_error},
    {"CBOR tag with \"argument\" an indefinite length alternate vector", {0xdf}, error::syntax_error},
    {"A byte string is of length 1 without the 1 byte", {0x41}, error::too_little_data},
    {"A text string is of length 1 without the 1 byte", {0x61}, error::too_little_data},
    {"Byte string should have 65520 bytes, but has one", {0x59, 0xff, 0xf0, 0x00}, error::too_little_data},
    {"Text string should have 65520 bytes, but has one", {0x79, 0xff, 0xf0, 0x00}, error::too_little_data},
    {"Major type positive integer with reserved value 28", {0x1c}, error::syntax_error},
    {"Major type positive integer with reserved value 29", {0x1d}, error::syntax_error},
    {"Major type positive integer with reserved value 30", {0x1e}, error::syntax_error},
    {"Major type negative integer with reserved value 28", {0x3c}, error::syntax_error},
    {"Major type negative integer with reserved value 29", {0x3d}, error::syntax_error},
    {"Major type negative integer with reserved value 30", {0x3e}, error::syntax_error},
    {"Major type byte string with reserved value 28 length", {0x5c}, error::syntax_error},
    {"Major type byte string with reserved value 29 length", {0x5d}, error::syntax_error},
    {"Major type byte string with reserved value 30 length", {0x5e}, error::syntax_error},
    {"Major type text string with reserved value 28 length", {0x7c}, error::syntax_error},
    {"Major type text string with reserved value 29 length", {0x7d}, error::syntax_error},
    {"Major type text string with reserved value 30 length", {0x7e}, error::syntax_error},
    {"Major type array with reserved value 28 length", {0x9c}, error::syntax_error},
    {"Major type array with reserved value 29 length", {0x9d}, error::syntax_error},
    {"Major type array with reserved value 30 length", {0x9e}, error::syntax_error},
    {"Major type map with reserved value 28 length", {0xbc}, error::syntax_error},
    {"Major type map with reserved value 29 length", {0xbd}, error::syntax_error},
    {"Major type map with reserved value 30 length", {0xbe}, error::syntax_error},
    {"Major type tag with reserved value 28 length", {0xdc}, error::syntax_error},
    {"Major type tag with reserved value 29 length", {0xdd}, error::syntax_error},
    {"Major type tag with reserved value 30 length", {0xde}, error::syntax_error},
    {"Major type simple with reserved value 28 length", {0xfc}, error::syntax_error},
    {"Major type simple with reserved value 29 length", {0xfd}, error::syntax_error},
    {"Major type simple with reserved value 30 length", {0xfe}, error::syntax_error},
    {"Map with 1 item when it should have 2", {0xa1, 0x00}, error::too_little_data},
    {"Map with 3 item when it should have 4", {0xa2, 0x00, 0x00, 0x00}, error::too_little_data},
    {"Indefinite map with 1 item when it should have 2", {0xbf, 0x00, 0xff}, error::syntax_error},
    {"Indefinite map with 3 item when it should have 4", {0xbf, 0x00, 0x00, 0x00, 0xff}, error::syntax_error},
};

} // namespace

// NotWellFormedTests and DecodeFailureTests: each input is not well-formed. cbor::diagnostic_notation reads
// indefinite lengths and exactly one item (diagnostic_notation.hpp: "syntax_error a head is not well-formed, a chunk
// of an indefinite string has the wrong type, or bytes follow the item"), so it gives the kind of RFC 8949 Appendix
// F for each input.
TEST_CASE("qcbor NotWellFormedTests: each not-well-formed input gives its Appendix F error")
{
    for (auto const &c : not_well_formed_inputs) {
        std::string const description = c.description;
        CAPTURE(description);
        CHECK(cbor::diagnostic_notation(as_string(c.input)).error_or(error{}) == c.expected);
    }
}

// NotWellFormedTests: cbor::decode and cbor::item_size refuse each input too. The input 80 ff ("A bare break after a
// zero length definite length array") is the one difference: decode reads one item and stops (decode.hpp: "A byte
// after the item is not read, so the input can be a CBOR sequence (RFC 8742)"; item_size.hpp: "Bytes after the item
// do not change the result"). RFC 8949 Appendix F says that bytes after the item are an error only where the
// application expects exactly one item. A cbor::sequence over the same bytes gives the break as a syntax error.
TEST_CASE("qcbor NotWellFormedTests: decode and item_size refuse each not-well-formed input")
{
    std::string const bare_break_after_array = as_string({0x80, 0xff});
    for (auto const &c : not_well_formed_inputs) {
        std::string const description = c.description;
        CAPTURE(description);
        std::string const in = as_string(c.input);
        if (in == bare_break_after_array) {
            CHECK(cbor::decode(in).has_value());
            CHECK(cbor::item_size(in) == std::size_t{1});
            continue;
        }
        CHECK_FALSE(cbor::decode(in).has_value());
        CHECK_FALSE(cbor::item_size(in).has_value());
    }
    std::vector<std::expected<std::string_view, error>> items;
    for (auto const e : cbor::sequence{std::make_shared<std::string const>(bare_break_after_array)})
        items.push_back(e);
    REQUIRE_EQ(items.size(), 2u);
    CHECK(items[0].has_value());
    CHECK(items[1] == std::unexpected(error::syntax_error));
}

// DecodeFailureTests: a text string head with the length SIZE_MAX and no bytes. Upstream gives
// QCBOR_ERR_STRING_TOO_LONG, its own limit. cbor::decode compares the length with the bytes that are left
// (decode.hpp: "too_little_data the item is not complete"), and RFC 8949 Appendix F calls this too little data.
TEST_CASE("qcbor DecodeFailureTests: a text string of SIZE_MAX bytes with no bytes is incomplete")
{
    std::string head = as_string({0x7b});
    for (int i = 0; i != 8; ++i)
        head.push_back(static_cast<char>(0xff));
    CHECK(cbor::decode(head).error_or(error{}) == error::too_little_data);
}

// DecodeFailureTests, the invalid rows: tag 0 on an integer, tag 1 on a byte string, tag 1 on tag 0, and tag 2 on an
// integer. Upstream gives QCBOR_ERR_BAD_OPT_TAG. RFC 8949 5.3.2 lets a decoder pass a tag that it does not convert,
// and the owner decided that cbor::decode keeps a tag over such a content as a tag (decode.hpp: "decode checks the
// content of no other tag"; a tag 2 over another content is an unsigned_bignum item over it). A converter checks:
// structs<sys_seconds> names inadmissible_type_for_tag_content for tag 1 over a byte string or a tag (RFC 8949
// 3.4.2 admits an integer or a float). std::uint64_t takes no bignum (structs.hpp: "an integer of 8 to 64 bits |
// an integer"), so tag 2 is another item for it: incorrect_type.
TEST_CASE("qcbor DecodeFailureTests: a tag with an inadmissible content type")
{
    auto const type_of = [](std::string const &in) {
        auto const tree = cbor::decode(in);
        REQUIRE(tree.has_value());
        return (*tree)->type;
    };
    CHECK(type_of(as_string({0xc0, 0x00})) == cbor::type::tag);
    CHECK(type_of(as_string({0xc1, 0x41, 0x33})) == cbor::type::tag);
    CHECK(type_of(as_string({0xc1, 0xc0, 0x00})) == cbor::type::tag);
    CHECK(type_of(as_string({0xc2, 0x00})) == cbor::type::unsigned_bignum);
    CHECK(cbor::structs<std::uint64_t>::decode(as_string({0xc2, 0x00})).error_or(error{}) == error::incorrect_type);
    CHECK(cbor::structs<std::chrono::sys_seconds>::decode(as_string({0xc1, 0x41, 0x33})).error_or(error{}) ==
          error::inadmissible_type_for_tag_content);
    CHECK(cbor::structs<std::chrono::sys_seconds>::decode(as_string({0xc1, 0xc0, 0x00})).error_or(error{}) ==
          error::inadmissible_type_for_tag_content);
}

// ComprehensiveInputTest: every input of one and of two bytes goes through each reader. Upstream checks only that
// nothing crashes; the sanitizers of the build check the same here. A decode that succeeds reads a well-formed item,
// so item_size gives the size of that item within the input.
TEST_CASE("qcbor ComprehensiveInputTest: every input of one or two bytes is read safely")
{
    auto read_all = [](std::string const &in) {
        auto const tree = cbor::decode(in);
        auto const size = cbor::item_size(in);
        (void)cbor::diagnostic_notation(in);
        if (auto const doc = lazy_of(in); doc.has_value()) {
            (void)doc->type();
            (void)doc->size();
            (void)doc->clone();
        }
        for (auto const e : cbor::sequence{std::make_shared<std::string const>(in)})
            (void)e;
        if (tree.has_value()) {
            CHECK(size.has_value());
            CHECK(size.value_or(in.size() + 1) <= in.size());
        }
    };
    for (int first = 0; first != 256; ++first) {
        std::string in(1, static_cast<char>(first));
        read_all(in);
        in.push_back('\0');
        for (int second = 0; second != 256; ++second) {
            in[1] = static_cast<char>(second);
            read_all(in);
        }
    }
}

// DateParseTest, spDateTestInput: tag 0 and tag 1 (RFC 8949 3.4.1, 3.4.2). cbor::structs reads tag 1 into
// std::chrono::sys_seconds (structs.hpp: "std::chrono::sys_seconds | tag 1 on an integer"). Rows 3, 5 and 6.
TEST_CASE("qcbor DateParseTest: an epoch date on an integer reads into sys_seconds")
{
    using namespace std::chrono_literals;
    using cbor::structs;
    using std::chrono::sys_seconds;
    auto const date = structs<sys_seconds>::decode(as_string({0xc1, 0x1a, 0x53, 0x72, 0x4e, 0x00}));
    REQUIRE(date.has_value());
    CHECK_EQ((*date)->time_since_epoch(), 1400000000s);

    // Row 6: 1b f0f0f0f0f0f0f0f0 does not fit int64_t. Upstream gives QCBOR_ERR_DATE_OVERFLOW.
    CHECK(structs<sys_seconds>::decode(as_string({0xc1, 0x1b, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0}))
              .error_or(error{}) == error::number_out_of_range);

    // Row 1: tag 0 on "1985-04-12". Upstream gives QCBOR_TYPE_DATE_STRING. mbedded-cbor has no type for tag 0, so
    // the tree keeps the tag (item.hpp: "`tag` ... the tag number and the content"; RFC 8949 5.2 asks a generic
    // decoder to forward tags).
    auto const text = cbor::decode(as_string({0xc0, 0x6a, '1', '9', '8', '5', '-', '0', '4', '-', '1', '2'}));
    REQUIRE(text.has_value());
    auto const tagged = (**text).get<cbor::type::tag>();
    REQUIRE(tagged.has_value());
    CHECK_EQ(tagged->first, 0u);
    CHECK_EQ(text_in(*tagged->second), "1985-04-12");

    // Row 5: 15(22(1(1400000001))). Upstream reads the date under the two tags. The tree keeps all three tags.
    auto const nested = cbor::decode(as_string({0xcf, 0xd8, 0x16, 0xc1, 0x1a, 0x53, 0x72, 0x4e, 0x01}));
    REQUIRE(nested.has_value());
    auto const t15 = (**nested).get<cbor::type::tag>();
    REQUIRE(t15.has_value());
    CHECK_EQ(t15->first, 15u);
    auto const t22 = t15->second->get<cbor::type::tag>();
    REQUIRE(t22.has_value());
    CHECK_EQ(t22->first, 22u);
    auto const t1 = t22->second->get<cbor::type::tag>();
    REQUIRE(t1.has_value());
    CHECK_EQ(t1->first, 1u);
    CHECK(t1->second->get<cbor::type::unsigned_integer>() == 1400000001u);
}

// DateParseTest, rows 7 to 13: tag 1 on a float. Upstream reads 1.1 as 1 second and a fraction of 0.1, and gives
// QCBOR_ERR_DATE_OVERFLOW for 3.4028234663852886e+38, 9223372036854775808.0, NaN, +Infinity and -Infinity; it
// reads 9223372036854773760.0. RFC 8949 3.4.2 admits a float. std::chrono::sys_seconds counts whole seconds in an
// int64_t, and the structs guide says that decode takes a float under tag 1 when its value is a whole number and
// gives number_out_of_range for any other float. So 1.1, the values outside int64_t, NaN and the infinities give
// number_out_of_range, and 9223372036854773760.0 gives that count of seconds, as upstream.
TEST_CASE("qcbor DateParseTest: an epoch date on a float reads into sys_seconds when it is whole")
{
    using cbor::structs;
    using std::chrono::sys_seconds;
    for (auto const &in : {
             as_string({0xc1, 0xfa, 0x3f, 0x8c, 0xcc, 0xcd}),
             as_string({0xc1, 0xfa, 0x7f, 0x7f, 0xff, 0xff}),
             as_string({0xc1, 0xfb, 0x43, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
             as_string({0xc1, 0xfa, 0x7f, 0xc0, 0x00, 0x00}),
             as_string({0xc1, 0xfb, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
             as_string({0xc1, 0xf9, 0xfc, 0x00}),
         }) {
        CAPTURE(in.size());
        CHECK(structs<sys_seconds>::decode(std::string(in)).error_or(error{}) == error::number_out_of_range);
    }
    auto const largest = structs<sys_seconds>::decode(as_string({0xc1, 0xfb, 0x43, 0xdf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe}));
    REQUIRE(largest.has_value());
    CHECK_EQ((*largest)->time_since_epoch().count(), 9223372036854773760);
}

// BignumParseTest, spBigNumInput: [2(h'010000000000000000'), 3(h'010000000000000000'), {-64: 3(...), 64: 2(...),
// "BN+": 2(...), "BN-": 3(...)}]. The magnitude has 9 bytes, so it does not fit 64 bits, and the tree keeps the
// bignum with its bytes (RFC 8949 3.4.3).
TEST_CASE("qcbor BignumParseTest: bignums larger than 64 bits keep their bytes")
{
    std::string const magnitude = as_string({0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    std::string const input = as_string({
        0x83, 0xc2, 0x49, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x49, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x38, 0x3f, 0xc3, 0x49, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x18, 0x40, 0xc2, 0x49, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x42, 0x4e, 0x2b,
        0xc2, 0x49, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x42, 0x4e, 0x2d, 0xc3, 0x49, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    });
    auto magnitude_of = [](cbor::item const &node, cbor::type const kind) {
        CHECK(node.type == kind);
        auto const b = kind == cbor::type::unsigned_bignum ? node.get<cbor::type::unsigned_bignum>()
                                                            : node.get<cbor::type::negative_bignum>();
        REQUIRE(b.has_value());
        CHECK_EQ(b->first, kind == cbor::type::unsigned_bignum ? 2u : 3u);
        auto const bytes = b->second->get<cbor::type::byte_string>();
        REQUIRE(bytes.has_value());
        return as_string(*bytes);
    };
    auto const tree = cbor::decode(input);
    REQUIRE(tree.has_value());
    auto const top = elements_of(**tree);
    REQUIRE_EQ(top.size(), 3u);
    CHECK_EQ(magnitude_of(top[0], cbor::type::unsigned_bignum), magnitude);
    CHECK_EQ(magnitude_of(top[1], cbor::type::negative_bignum), magnitude);
    auto const map = entries_of(top[2]);
    REQUIRE_EQ(map.size(), 8u);
    CHECK(map[0].get<cbor::type::negative_integer>() == 63u);
    CHECK_EQ(magnitude_of(map[1], cbor::type::negative_bignum), magnitude);
    CHECK(map[2].get<cbor::type::unsigned_integer>() == 64u);
    CHECK_EQ(magnitude_of(map[3], cbor::type::unsigned_bignum), magnitude);
    CHECK_EQ(text_in(map[4]), "BN+");
    CHECK_EQ(magnitude_of(map[5], cbor::type::unsigned_bignum), magnitude);
    CHECK_EQ(text_in(map[6]), "BN-");
    CHECK_EQ(magnitude_of(map[7], cbor::type::negative_bignum), magnitude);

    auto const doc = lazy_of(input);
    REQUIRE(doc.has_value());
    auto const map_at = doc->at(2);
    REQUIRE(map_at.has_value());
    CHECK(map_at->at(cbor::key{-64}).and_then([](cbor::lazy const &l) { return l.type(); }) ==
          cbor::type::negative_bignum);
    CHECK(map_at->at(cbor::key{64}).and_then([](cbor::lazy const &l) { return l.type(); }) ==
          cbor::type::unsigned_bignum);
    CHECK(map_at->at("BN+").and_then([](cbor::lazy const &l) { return l.type(); }) == cbor::type::unsigned_bignum);
    CHECK(map_at->at("BN-").and_then([](cbor::lazy const &l) { return l.type(); }) == cbor::type::negative_bignum);
}

namespace
{

struct number_conversion {
    char const *description;
    std::vector<std::uint8_t> input;
    std::expected<std::int64_t, error> to_int64;
    std::expected<std::uint64_t, error> to_uint64;
    std::expected<double, error> to_double;
};

auto const out_of_range = std::unexpected(error::number_out_of_range);
auto const other_type = std::unexpected(error::incorrect_type);

std::vector<std::uint8_t> extreme_bignum(std::uint8_t const tag)
{
    std::vector<std::uint8_t> in = {tag, 0x59, 0x01, 0x90};
    in.insert(in.end(), 400, 0xf0);
    return in;
}

// NumberConversions of IntegerConvertTest. The columns are the reads as int64_t, uint64_t and double with
// cbor::lazy::get. Upstream gives QCBOR_ERR_CONVERSION_UNDER_OVER_FLOW or QCBOR_ERR_NUMBER_SIGN_CONVERSION where the
// value does not fit; lazy.hpp documents "number_out_of_range the number does not fit T" for both. Rows where the
// documentation of mbedded-cbor gives another result say so in the description, and the cases above the table say
// why.
std::vector<number_conversion> const number_conversions = {
    {"too large to fit into int64_t", {0xc3, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, out_of_range,
     out_of_range, out_of_range},
    {"largest negative int that fits in int64_t", {0xc3, 0x48, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
     std::numeric_limits<std::int64_t>::min(), out_of_range, -9223372036854775808.0},
    {"negative bignum -1", {0xc3, 0x41, 0x00}, -1, out_of_range, -1.0},
    {"negative bignum 0xc349010000000000000000 -18446744073709551617",
     {0xc3, 0x49, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     out_of_range,
     out_of_range,
     out_of_range},
    {"extreme pos bignum", extreme_bignum(0xc2), out_of_range, out_of_range, out_of_range},
    {"extreme neg bignum", extreme_bignum(0xc3), out_of_range, out_of_range, out_of_range},
    {"Positive bignum 0xffff", {0xc2, 0x42, 0xff, 0xff}, 65535, 65535u, 65535.0},
    {"Positive integer 18446744073709551615", {0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, out_of_range,
     std::numeric_limits<std::uint64_t>::max(), out_of_range},
    {"Postive integer 0", {0x00}, 0, 0u, 0.0},
    {"Negative integer -18446744073709551616", {0x3b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, out_of_range,
     out_of_range, -18446744073709551616.0},
    {"Double Floating point value 100.3", {0xfb, 0x40, 0x59, 0x13, 0x33, 0x33, 0x33, 0x33, 0x33}, other_type,
     other_type, 100.3},
    {"half-precision Floating point value -4", {0xf9, 0xc4, 0x00}, other_type, other_type, -4.0},
    {"+inifinity single precision", {0xfa, 0x7f, 0x80, 0x00, 0x00}, other_type, other_type,
     std::numeric_limits<double>::infinity()},
    {"-inifinity single precision", {0xfa, 0xff, 0x80, 0x00, 0x00}, other_type, other_type,
     -std::numeric_limits<double>::infinity()},
    {"Type error conversion for double", {0xfb, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, other_type,
     other_type, 1.0},
    {"Decimal Fraction with positive bignum 257 * 10e3",
     {0xc4, 0x82, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc2, 0x42, 0x01, 0x01},
     other_type,
     other_type,
     other_type},
    {"bigfloat with negative bignum -258 * 2e3",
     {0xc5, 0x82, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc3, 0x42, 0x01, 0x01},
     other_type,
     other_type,
     other_type},
    {"Big float 3 * 2^^2", {0xc5, 0x82, 0x02, 0x03}, other_type, other_type, other_type},
    {"Decimal fraction 3/10", {0xc4, 0x82, 0x20, 0x03}, other_type, other_type, other_type},
    {"Big float: INT64_MIN * 2e-1",
     {0xc5, 0x82, 0x20, 0x3b, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
     other_type,
     other_type,
     other_type},
};

} // namespace

// IntegerConvertTest. Three kinds of rows differ from upstream, each by a documented behaviour of
// cbor::lazy::get (lazy.hpp):
// - A float read as an integer: upstream converts 100.3 to 100 and -4.0 to -4, and gives QCBOR_ERR_FLOAT_EXCEPTION
//   for NaN and infinity. lazy.hpp: "an integer type | an integer or a bignum that fits T", so a float is
//   incorrect_type. RFC 8949 3.3 makes a float a separate kind of number, and 2 gives no conversion between them.
// - An integer read as double that a double does not hold exactly: upstream rounds 18446744073709551615 to
//   18446744073709551616.0 and -18446744073709551617 to -18446744073709551616.0, and gives infinity for 400-byte
//   bignums. lazy.hpp: "double | a float, or an integer or a bignum that a double holds exactly", so these give
//   number_out_of_range. -18446744073709551616 (3b ffffffffffffffff) is -2^64, which a double holds exactly.
// - A decimal fraction or a bigfloat (tags 4 and 5, RFC 8949 3.4.4): upstream computes the value. lazy.hpp: "The
//   call passes a tag that it does not interpret", so the read meets the array of exponent and mantissa, and gives
//   incorrect_type.
// The row "Floating point value NaN 0xfa7fc00000" stands in the float test file, and the row "Type error conversion
// for float" is the -infinity row with a QCBOR type mask; mbedded-cbor has no type mask.
TEST_CASE("qcbor IntegerConvertTest: numbers read as int64_t, uint64_t and double")
{
    for (auto const &c : number_conversions) {
        std::string const description = c.description;
        CAPTURE(description);
        auto const in = as_string(c.input);
        CHECK(read_as<std::int64_t>(lazy_of(in)) == c.to_int64);
        CHECK(read_as<std::uint64_t>(lazy_of(in)) == c.to_uint64);
        auto const d = read_as<double>(lazy_of(in));
        double const read_value = d.value_or(0.0);
        cbor::error const read_error = d.error_or(error{});
        CAPTURE(read_value);
        CAPTURE(read_error);
        REQUIRE_EQ(d.has_value(), c.to_double.has_value());
        if (d.has_value())
            CHECK_EQ(std::bit_cast<std::uint64_t>(*d), std::bit_cast<std::uint64_t>(*c.to_double));
        else
            CHECK_EQ(d.error(), c.to_double.error());
    }
    auto const nan = read_as<double>(lazy_of(as_string({0xfa, 0x7f, 0xc0, 0x00, 0x00})));
    REQUIRE(nan.has_value());
    CHECK(std::isnan(*nan));
    CHECK(read_as<std::int64_t>(lazy_of(as_string({0xfa, 0x7f, 0xc0, 0x00, 0x00}))) == other_type);
}

// IntegerConvertTest, "Positive bignum 0x01020304 indefinite length string": upstream reads the bignum from an
// indefinite-length byte string. cbor::decode refuses indefinite lengths (decode.hpp: "indefinite_length a string,
// an array or a map has an indefinite length"); RFC 8949 3.4.3 and 3.2.3 make the input valid.
TEST_CASE("qcbor IntegerConvertTest: a bignum in an indefinite-length string is refused")
{
    CHECK(cbor::decode(as_string({0xc2, 0x5f, 0x42, 0x01, 0x02, 0x41, 0x03, 0x41, 0x04, 0xff})).error_or(error{}) ==
          error::indefinite_length);
}

// CBORSequenceDecodeTests: "1985-04-12", 0, 1400000000, "hi" is a CBOR sequence (RFC 8742) of four items with the
// sizes 11, 1, 5 and 3. An empty input has no item.
TEST_CASE("qcbor CBORSequenceDecodeTests: a sequence splits into its items")
{
    auto items_of = [](std::string const &in) {
        std::vector<std::expected<std::string_view, error>> out;
        for (auto const e : cbor::sequence{std::make_shared<std::string const>(in)})
            out.push_back(e);
        return out;
    };
    auto const four = items_of(as_string({0x6a, '1', '9', '8', '5', '-', '0', '4', '-', '1', '2', 0x00, 0x1a, 0x53,
                                          0x72, 0x4e, 0x00, 0x62, 'h', 'i'}));
    REQUIRE_EQ(four.size(), 4u);
    CHECK(four[0].transform([](std::string_view const v) { return v.size(); }) == 11u);
    CHECK(four[1].transform([](std::string_view const v) { return v.size(); }) == 1u);
    CHECK(four[2].transform([](std::string_view const v) { return v.size(); }) == 5u);
    CHECK(four[3].transform([](std::string_view const v) { return v.size(); }) == 3u);
    CHECK(cbor::sequence{std::make_shared<std::string const>()}.empty());
    CHECK(items_of(std::string{}).empty());
}

// CBORSequenceDecodeTests, the sequences 01 9f 02 and 01 9f ff: upstream reads the indefinite-length array as the
// second item. cbor::sequence gives the errors of cbor::item_size (sequence.hpp: "Each item gives the errors of
// cbor::item_size()"), and item_size refuses indefinite lengths (item_size.hpp: "indefinite_length a head uses
// indefinite length"). RFC 8949 3.2.2 makes 9f ff well-formed.
TEST_CASE("qcbor CBORSequenceDecodeTests: an indefinite-length item in a sequence is refused")
{
    for (auto const &in : {as_string({0x01, 0x9f, 0x02}), as_string({0x01, 0x9f, 0xff})}) {
        std::vector<std::expected<std::string_view, error>> items;
        for (auto const e : cbor::sequence{std::make_shared<std::string const>(in)})
            items.push_back(e);
        REQUIRE_EQ(items.size(), 2u);
        CHECK(items[0].has_value());
        CHECK(items[1] == std::unexpected(error::indefinite_length));
    }
}

// IndefiniteLengthArrayMapTest: [_ 1, [2, 3]] is well-formed (RFC 8949 Appendix A has the same text for
// 0x9f01820203ff). The five bad inputs are incomplete or have a break where no item may end (RFC 8949 Appendix F).
// cbor::decode refuses indefinite lengths (decode.hpp: "indefinite_length"); upstream decodes the first input.
TEST_CASE("qcbor IndefiniteLengthArrayMapTest: indefinite-length arrays and their faults")
{
    std::string const good = as_string({0x9f, 0x01, 0x82, 0x02, 0x03, 0xff});
    CHECK(cbor::diagnostic_notation(good) == std::string("[_ 1, [2, 3]]"));
    CHECK(cbor::decode(good).error_or(error{}) == error::indefinite_length);
    CHECK(cbor::diagnostic_notation(as_string({0x9f})).error_or(error{}) == error::too_little_data);
    CHECK(cbor::diagnostic_notation(as_string({0x9f, 0x9f, 0x02, 0xff})).error_or(error{}) ==
          error::too_little_data);
    CHECK(cbor::diagnostic_notation(as_string({0x9f, 0x02, 0xff, 0xff})).error_or(error{}) == error::syntax_error);
    CHECK(cbor::diagnostic_notation(as_string({0x81, 0x9f})).error_or(error{}) == error::too_little_data);
    CHECK(cbor::diagnostic_notation(as_string({0x9f, 0xd1, 0xff})).error_or(error{}) == error::syntax_error);
}

// IndefiniteLengthNestTest: arrays of indefinite length nested 1 to test::levels_every_stack_holds deep (the depth
// of this library, not the limit of upstream) are well-formed. cbor::diagnostic_notation reads each; cbor::decode refuses them (decode.hpp: "indefinite_length").
TEST_CASE("qcbor IndefiniteLengthNestTest: nested indefinite-length arrays are well-formed")
{
    for (std::size_t depth = 1; depth <= test::levels_every_stack_holds; ++depth) {
        CAPTURE(depth);
        std::string const nested =
            std::string(depth, static_cast<char>(0x9f)) + std::string(depth, static_cast<char>(0xff));
        CHECK(cbor::diagnostic_notation(nested).has_value());
        CHECK(cbor::decode(nested).error_or(error{}) == error::indefinite_length);
    }
}

// IndefiniteLengthStringTest: [(_ "strea", "ming")] is well-formed (RFC 8949 Appendix A: 0x7f657374726561646d696e67ff
// is (_ "strea", "ming")). A chunk of another major type and an integer chunk are syntax errors, and a string with
// no break is incomplete (RFC 8949 3.2.3, Appendix F). The map {(_ "struu", "ming"): 1} has an indefinite-length key.
// The array of one byte string of 7 chunks of 1 to 128 bytes, each with a one-byte length head, is well-formed.
// cbor::decode refuses indefinite lengths (decode.hpp: "indefinite_length"); upstream decodes the good inputs.
TEST_CASE("qcbor IndefiniteLengthStringTest: indefinite-length strings and their faults")
{
    std::string const good =
        as_string({0x81, 0x7f, 0x65, 0x73, 0x74, 0x72, 0x65, 0x61, 0x64, 0x6d, 0x69, 0x6e, 0x67, 0xff});
    CHECK(cbor::diagnostic_notation(good) == std::string("[(_ \"strea\", \"ming\")]"));
    CHECK(cbor::decode(good).error_or(error{}) == error::indefinite_length);
    CHECK(cbor::diagnostic_notation(
              as_string({0x81, 0x7f, 0x65, 0x73, 0x74, 0x72, 0x65, 0x61, 0x44, 0x6d, 0x69, 0x6e, 0x67, 0xff}))
              .error_or(error{}) == error::syntax_error);
    CHECK(cbor::diagnostic_notation(as_string({0x81, 0x7f, 0x01, 0x02, 0xff})).error_or(error{}) ==
          error::syntax_error);
    CHECK(cbor::diagnostic_notation(
              as_string({0x81, 0x7f, 0x65, 0x73, 0x74, 0x72, 0x65, 0x61, 0x64, 0x6d, 0x69, 0x6e, 0x67}))
              .error_or(error{}) == error::too_little_data);
    CHECK(cbor::diagnostic_notation(
              as_string({0xa1, 0x7f, 0x65, 0x73, 0x74, 0x72, 0x75, 0x75, 0x64, 0x6d, 0x69, 0x6e, 0x67, 0xff, 0x01})) ==
          std::string("{(_ \"struu\", \"ming\"): 1}"));

    std::string big = as_string({0x81, 0x5f});
    std::uint8_t next = 0;
    for (int chunk = 1; chunk <= 128; chunk *= 2) {
        big.push_back(static_cast<char>(0x58));
        big.push_back(static_cast<char>(chunk));
        for (int j = 0; j != chunk; ++j)
            big.push_back(static_cast<char>(next++));
    }
    big.push_back(static_cast<char>(0xff));
    CHECK(cbor::diagnostic_notation(big).has_value());
    CHECK(cbor::decode(big).error_or(error{}) == error::indefinite_length);
}

// ParseMapAsArrayTest, pPerverseLabels: a map of 14 pairs whose keys are true, null, simple(255), floats, a map, an
// array, a tag, indefinite-length containers and strings. RFC 8949 3.1 allows any item as a key. The input has
// indefinite lengths, so cbor::decode refuses it (decode.hpp: "indefinite_length"), and cbor::diagnostic_notation
// reads it. Upstream reads it in its map-as-array mode, which mbedded-cbor does not have.
TEST_CASE("qcbor ParseMapAsArrayTest: a map with keys of every kind is well-formed")
{
    std::string const perverse = as_string({
        0xae, 0xf5, 0x61, 0x61, 0xf6, 0x61, 0x62, 0xf8, 0xff, 0x61, 0x63, 0xf9, 0x7e, 0x00, 0x61, 0x64, 0xfa,
        0x7f, 0x7f, 0xff, 0xff, 0x61, 0x65, 0xfb, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x66,
        0xa1, 0x19, 0x03, 0xe8, 0x10, 0x61, 0x67, 0x81, 0x81, 0x81, 0x80, 0x61, 0x68, 0xc1, 0x09, 0x61, 0x69,
        0x82, 0x05, 0xa2, 0x01, 0x02, 0x03, 0x04, 0x61, 0x6a, 0xbf, 0xff, 0x61, 0x6b, 0x9f, 0x11, 0x12, 0x13,
        0xff, 0x61, 0x6c, 0x7f, 0x62, 0x41, 0x42, 0x62, 0x43, 0x44, 0xff, 0x61, 0x6d, 0xd9, 0x01, 0x02, 0xbf,
        0x7f, 0x61, 0x4a, 0x61, 0x4b, 0xff, 0x00, 0xf4, 0xd7, 0x80, 0xff, 0x61, 0x6e,
    });
    CHECK(cbor::diagnostic_notation(perverse).has_value());
    CHECK(cbor::decode(perverse).error_or(error{}) == error::indefinite_length);
}

// OptTagParseTest, spEncodedLargeTag and spLotsOfTags: the tag number 10489608748473423768 needs all 8 bytes of its
// argument, and 10489608748473423768(136(6(7([])))) nests four tags (RFC 8949 3.4).
TEST_CASE("qcbor OptTagParseTest: a large tag number and four nested tags decode")
{
    std::uint64_t const large = 0x9192939495969798u;
    auto const one = cbor::decode(as_string({0xdb, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x80}));
    REQUIRE(one.has_value());
    auto const t = (**one).get<cbor::type::tag>();
    REQUIRE(t.has_value());
    CHECK_EQ(t->first, large);
    CHECK(elements_of(*t->second).empty());

    auto const four =
        cbor::decode(as_string({0xdb, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0xd8, 0x88, 0xc6, 0xc7, 0x80}));
    REQUIRE(four.has_value());
    cbor::item const *at = &**four;
    for (std::uint64_t const number : {large, std::uint64_t{136}, std::uint64_t{6}, std::uint64_t{7}}) {
        auto const tag = at->get<cbor::type::tag>();
        REQUIRE(tag.has_value());
        CHECK_EQ(tag->first, number);
        at = tag->second;
    }
    CHECK(elements_of(*at).empty());
}
