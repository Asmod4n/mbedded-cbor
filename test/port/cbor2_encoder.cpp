// The cases of tests/test_encoder.py of the Python library cbor2, ported to the public interface of this
// library. These tests exist to show that this library writes the same bytes as cbor2 for the same values. A Python
// value maps to the C++ type that cbor::structs writes as the same CBOR item (structs.hpp). The expected bytes come
// from the upstream tests and are checked against RFC 8949 in refs/. A case where this library documents another
// behaviour tests that behaviour, and its comment names the upstream case, the documentation line and the section of
// the RFC that allows it.
//
// Upstream: https://github.com/agronholm/cbor2, commit 5fa04929b44c82b8416d6afc064afb0da198f278.
//
// The license of the upstream tests:
//
// The MIT License (MIT)
//
// Copyright (c) 2016 Alex Grönholm
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "cbor2.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using cbor::error;
using cbor2::dumps;
using cbor2::hexlify;
using cbor2::unhexlify;

namespace
{

// The bytes of one item that the calls of `write` give to a cbor::encoder, as hex.
template <class Write>
std::string encoded_by(Write const write)
{
    std::string out;
    cbor::encoder e(out);
    write(e);
    auto const size = e.finish();
    REQUIRE(size.has_value());
    return hexlify(out);
}

struct text_in_order {
    std::string a;
    std::string b;
};

struct text_out_of_order {
    std::string b;
    std::string a;
};

struct integer_keys {
    std::uint8_t first;
    std::uint8_t second;
};

} // namespace

template <>
struct cbor::describe_members<text_in_order> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{"a", &text_in_order::a},
        cbor::member{"b", &text_in_order::b},
    };
};

template <>
struct cbor::describe_members<text_out_of_order> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{"b", &text_out_of_order::b},
        cbor::member{"a", &text_out_of_order::a},
    };
};

template <>
struct cbor::describe_members<integer_keys> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{255, &integer_keys::first},
        cbor::member{2, &integer_keys::second},
    };
};

// Upstream: test_encode_length. cbor::encoder writes the head of an array and of a map with the shortest head for
// the count (encode.hpp). The rows with an indefinite length are not ported: encode.hpp gives no call that writes
// one.
TEST_CASE("cbor2 encoder: test_encode_length")
{
    CHECK_EQ(dumps(std::uint64_t{1}), "01");
    CHECK_EQ(encoded_by([](auto &e) { REQUIRE(e.array(2).has_value()); }), "82");
    CHECK_EQ(encoded_by([](auto &e) { REQUIRE(e.map(0).has_value()); }), "a0");
}

#ifdef CBOR_HAS_INT128
// Upstream: test_integer. RFC 8949 3.1 and 3.4.3, and the preferred serialization of 4.2.2: an integer that fits
// 64 bits takes major type 0 or 1, and a larger one a bignum. structs.hpp: "an integer of 8 to 64 bits; __int128 |
// an integer; a bignum (tag 2, 3)".
TEST_CASE("cbor2 encoder: test_integer")
{
    cbor::int128 const two_64 = static_cast<cbor::int128>(1) << 64;
    struct row {
        cbor::int128 value;
        std::string_view expected;
    };
    std::vector<row> const rows{
        {0, "00"},
        {1, "01"},
        {10, "0a"},
        {23, "17"},
        {24, "1818"},
        {100, "1864"},
        {1000, "1903e8"},
        {1000000, "1a000f4240"},
        {1000000000000, "1b000000e8d4a51000"},
        {two_64 - 1, "1bffffffffffffffff"},
        {two_64, "c249010000000000000000"},
        {-two_64, "3bffffffffffffffff"},
        {-two_64 - 1, "c349010000000000000000"},
        {-1, "20"},
        {-10, "29"},
        {-100, "3863"},
        {-1000, "3903e7"},
    };
    for (row const &r : rows) {
        INFO("expected: ", r.expected);
        CHECK_EQ(dumps(r.value), r.expected);
        if (r.value >= std::numeric_limits<std::int64_t>::min() && r.value <= std::numeric_limits<std::int64_t>::max())
            CHECK_EQ(dumps(static_cast<std::int64_t>(r.value)), r.expected);
        if (r.value >= 0 && r.value <= std::numeric_limits<std::uint64_t>::max())
            CHECK_EQ(dumps(static_cast<std::uint64_t>(r.value)), r.expected);
    }
}
#endif

// Upstream: test_float and test_minimal_floats. structs.hpp: "float, double | the shortest float that holds the
// value". RFC 8949 4.2.2: the preferred serialization of a float is the shortest that keeps the value, and 4.1
// writes a NaN with zeros on the right of its significand as f97e00. cbor2 writes these values in the same form.
TEST_CASE("cbor2 encoder: test_float, test_minimal_floats")
{
    double const inf = std::numeric_limits<double>::infinity();
    struct row {
        double value;
        std::string_view expected;
    };
    std::vector<row> const rows{
        {1.1, "fb3ff199999999999a"},
        {1.0e300, "fb7e37e43c8800759c"},
        {-4.1, "fbc010666666666666"},
        {inf, "f97c00"},
        {std::numeric_limits<double>::quiet_NaN(), "f97e00"},
        {-inf, "f9fc00"},
        {3.5, "f94300"},
        {100000.0, "fa47c35000"},
        {3.8, "fb400e666666666666"},
        {0x1.0p-24, "f90001"},
        {0x1.4p-24, "fa33a00000"},
        {0x1.ff8p-63, "fa207fc000"},
    };
    for (row const &r : rows) {
        INFO("expected: ", r.expected);
        CHECK_EQ(dumps(r.value), r.expected);
    }
}

// Upstream: test_bytestring. structs.hpp: "std::span<std::byte const> | a byte string".
TEST_CASE("cbor2 encoder: test_bytestring")
{
    std::string const four = unhexlify("01020304");
    CHECK_EQ(dumps(std::span<std::byte const>()), "40");
    CHECK_EQ(dumps(std::as_bytes(std::span(four))), "4401020304");
    CHECK_EQ(encoded_by([&](auto &e) { REQUIRE(e.byte_string(std::as_bytes(std::span(four))).has_value()); }),
             "4401020304");
}

// Upstream: test_bytearray. A bytearray is a mutable byte string; its C++ form is std::vector<std::byte>. structs.hpp:
// "std::vector, std::array of std::byte, an integer, float, double | an RFC 8746 typed array". So the bytes go in
// tag 64, the typed array of unsigned bytes of RFC 8746 2.1, and not in a plain byte string as cbor2 writes them.
TEST_CASE("cbor2 encoder: test_bytearray")
{
    std::vector<std::byte> const bytearray{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    CHECK_EQ(dumps(bytearray), "d8404401020304");
}

// Upstream: test_string. structs.hpp: "std::basic_string<char>, std::string_view | a text string".
TEST_CASE("cbor2 encoder: test_string")
{
    struct row {
        std::string_view value;
        std::string_view expected;
    };
    std::vector<row> const rows{
        {"", "60"},
        {"a", "6161"},
        {"IETF", "6449455446"},
        {"\"\\", "62225c"},
        {"ü", "62c3bc"},
        {"水", "63e6b0b4"},
    };
    for (row const &r : rows) {
        INFO("expected: ", r.expected);
        CHECK_EQ(dumps(std::string(r.value)), r.expected);
        CHECK_EQ(dumps(r.value), r.expected);
    }
}

// Upstream: test_special. RFC 8949 3.3, Table 3. structs.hpp: "bool, std::nullptr_t, cbor::simple_value | true or
// false, null, a simple value".
TEST_CASE("cbor2 encoder: test_special")
{
    CHECK_EQ(dumps(false), "f4");
    CHECK_EQ(dumps(true), "f5");
    CHECK_EQ(dumps(nullptr), "f6");
    CHECK_EQ(dumps(cbor::simple_value::undefined), "f7");
}

// Upstream: test_simple_value and test_simple_val_as_key. RFC 8949 3.3: a simple value below 24 takes one byte, and
// one from 32 takes two.
TEST_CASE("cbor2 encoder: test_simple_value")
{
    struct row {
        std::uint8_t value;
        std::string_view expected;
    };
    for (row const &r : std::vector<row>{{0, "e0"}, {2, "e2"}, {23, "f7"}, {32, "f820"}}) {
        INFO("expected: ", r.expected);
        CHECK_EQ(dumps(cbor::simple_value{r.value}), r.expected);
        CHECK_EQ(encoded_by([&](auto &e) { REQUIRE(e.simple_value(cbor::simple_value{r.value}).has_value()); }),
                 r.expected);
    }
    CHECK_EQ(dumps(std::map<cbor::simple_value, int>{{cbor::simple_value{99}, 1}}), "a1f86301");
}

// Upstream: test_datetime, the rows with as_timestamp. structs.hpp: "std::chrono::sys_seconds | tag 1 on an
// integer". RFC 8949 3.4.2: the seconds from 1970-01-01T00:00Z. A std::chrono::sys_seconds has no time zone, so
// the rows utc and eet are one instant. The row timestamp+micro is not ported: no C++ type of the table writes
// tag 1 on a float.
TEST_CASE("cbor2 encoder: test_datetime as a timestamp")
{
    using namespace std::chrono;
    struct row {
        sys_seconds value;
        std::string_view expected;
    };
    std::vector<row> const rows{
        {sys_days{year{2013} / 3 / 21} + hours{20} + minutes{4}, "c11a514b67b0"},
        {sys_days{year{2013} / 3 / 21} + hours{22} + minutes{4} - hours{2}, "c11a514b67b0"},
        {sys_days{year{1969} / 12 / 31}, "c13a0001517f"},
        {sys_days{year{2200} / 1 / 1}, "c11b00000001b09e1900"},
    };
    for (row const &r : rows) {
        INFO("expected: ", r.expected);
        CHECK_EQ(dumps(r.value), r.expected);
    }
}

// Upstream: test_date[date/timestamp]. structs.hpp: "std::chrono::year_month_day | tag 100 on an integer of
// days". RFC 8943 2: the days from 1970-01-01.
TEST_CASE("cbor2 encoder: test_date")
{
    using namespace std::chrono;
    CHECK_EQ(dumps(year_month_day{year{2018}, month{12}, day{31}}), "d8641945e8");
}

// Upstream: test_custom_tag and test_largest_tag. RFC 8949 3.4: a tag number and its content; the number takes
// the shortest head, up to eight bytes. structs.hpp: "cbor::tagged_item<N, E> | tag N on the item of E".
TEST_CASE("cbor2 encoder: test_custom_tag, test_largest_tag")
{
    CHECK_EQ(dumps(cbor::tagged_item<6000, std::string>{"Hello"}), "d917706548656c6c6f");
    CHECK_EQ(dumps(cbor::tagged_item<18446744073709551615u, std::string>{"v"}), "dbffffffffffffffff6176");
    CHECK_EQ(encoded_by([](auto &e) {
                 REQUIRE(e.tag(6000).has_value());
                 REQUIRE(e.text_string("Hello").has_value());
             }),
             "d917706548656c6c6f");
    CHECK_EQ(encoded_by([](auto &e) {
                 REQUIRE(e.tag(18446744073709551615u).has_value());
                 REQUIRE(e.text_string("v").has_value());
             }),
             "dbffffffffffffffff6176");
}

// Upstream: test_not_cyclic_same_object[nosharing], test_dump_to_file and test_indefinite_containers, the rows
// without indefinite_containers. structs.hpp: "another container | an array", "a map type | a map".
TEST_CASE("cbor2 encoder: arrays and maps")
{
    CHECK_EQ(dumps(std::vector<std::vector<std::string>>{{}, {}}), "828080");
    CHECK_EQ(dumps(std::list<int>{1, 10}), "82010a");
    CHECK_EQ(dumps(std::list<int>{0, 1}), "820001");
    CHECK_EQ(dumps(std::map<std::string, std::string>{}), "a0");
}

// Upstream: test_ordered_map, the rows with text keys and with integer keys, and test_ordered_map[empty]. cbor2
// with canonical=True sorts the keys. structs.hpp: "A struct is a map, and its keys come in the order of RFC 8949
// 4.2.1". So a struct whose members are in another order gives the same bytes.
TEST_CASE("cbor2 encoder: test_ordered_map")
{
    CHECK_EQ(dumps(text_in_order{"", ""}), "a2616160616260");
    CHECK_EQ(dumps(text_out_of_order{"", ""}), "a2616160616260");
    CHECK_EQ(dumps(integer_keys{0, 0}), "a2020018ff00");
}

// Upstream: test_tuple_key and test_dict_key. RFC 8949 5.6: a key can be of any type. structs.hpp:
// "std::tuple<E...> | an array of that count", "a map type | a map".
TEST_CASE("cbor2 encoder: test_tuple_key, test_dict_key")
{
    CHECK_EQ(dumps(std::map<std::tuple<int, int>, std::string>{{{2, 1}, ""}}), "a182020160");
    CHECK_EQ(dumps(std::map<std::map<int, int>, std::string>{{{{2, 1}}, ""}}), "a1a1020160");
}

// Upstream: test_set. The CBOR sets specification (refs/CBOR_SETS.md) writes a set as tag 258 on an array.
// structs.hpp: "std::set, std::flat_set | tag 258 on an array".
TEST_CASE("cbor2 encoder: test_set")
{
    std::string const serialized = dumps(std::set<std::string>{"a", "b", "c"});
    CHECK_EQ(unhexlify(serialized).size(), 10u);
    CHECK(serialized.starts_with("d9010283"));
}
