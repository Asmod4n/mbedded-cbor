// The cases of tests/test_decoder.py of the Python library cbor2, ported to the public interface of this
// library. These tests exist to show that this library reads the same bytes as cbor2 to the same values. The
// expected values come from the upstream tests and are checked against RFC 8949 in refs/. A case where this
// library documents another behaviour tests that behaviour, and its comment names the upstream case, the
// documentation line and the section of the RFC that allows it.
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

#include "../small_stack.hpp"
#include "cbor2.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using cbor::error;
using cbor2::hexlify;
using cbor2::loads;
using cbor2::unhexlify;
using test::A;
using test::M;
using test::V;

namespace
{

test::value tag_of(std::uint64_t const number, test::value content)
{
    return test::value{test::tagged{number, std::move(content)}};
}

test::value bytes_of(std::string_view const hex)
{
    return test::value{test::bytes{unhexlify(hex)}};
}

test::value negative_of(std::uint64_t const argument)
{
    return test::value{test::negative{argument}};
}

// The error of cbor::decode, or error{} where cbor::decode gives a tree.
error decode_error_of(std::string_view const payload)
{
    auto const tree = cbor::decode(payload);
    return tree.has_value() ? error{} : tree.error();
}

struct decoded_row {
    std::string_view payload;
    test::value expected;
};

void check_decoded(std::vector<decoded_row> const &rows)
{
    for (decoded_row const &row : rows) {
        INFO("payload: ", row.payload);
        auto const decoded = loads(unhexlify(row.payload));
        REQUIRE(decoded.has_value());
        CHECK(*decoded == row.expected);
    }
}

// A payload with an indefinite length: cbor::decode refuses it, and cbor::diagnostic_notation reads it.
struct streaming_row {
    std::string_view payload;
    std::string_view diagnostic;
};

void check_streaming(std::vector<streaming_row> const &rows)
{
    for (streaming_row const &row : rows) {
        INFO("payload: ", row.payload);
        std::string const bytes = unhexlify(row.payload);
        CHECK_EQ(decode_error_of(bytes), error::indefinite_length);
        auto const text = cbor::diagnostic_notation(bytes);
        REQUIRE(text.has_value());
        CHECK_EQ(*text, row.diagnostic);
    }
}

} // namespace

// Upstream: test_integer. RFC 8949 3.1 gives major types 0 and 1, and 3.4.3 the bignums of tags 2 and 3.
// cbor::decode makes an integer node of a value that fits 64 bits, and keeps a larger bignum as a tag node over
// its byte string (decode.hpp: "A bignum whose magnitude fits 64 bits becomes an integer node").
TEST_CASE("cbor2 decoder: test_integer")
{
    struct row {
        std::string_view payload;
        cbor::type type;
        std::uint64_t argument;
    };
    std::vector<row> const rows{
        {"00", cbor::type::unsigned_integer, 0},
        {"01", cbor::type::unsigned_integer, 1},
        {"0a", cbor::type::unsigned_integer, 10},
        {"17", cbor::type::unsigned_integer, 23},
        {"1818", cbor::type::unsigned_integer, 24},
        {"1819", cbor::type::unsigned_integer, 25},
        {"1864", cbor::type::unsigned_integer, 100},
        {"1903e8", cbor::type::unsigned_integer, 1000},
        {"1a000f4240", cbor::type::unsigned_integer, 1000000},
        {"1b000000e8d4a51000", cbor::type::unsigned_integer, 1000000000000},
        {"1bffffffffffffffff", cbor::type::unsigned_integer, 18446744073709551615u},
        // -18446744073709551616 is -1 - n with n = 2^64 - 1.
        {"3bffffffffffffffff", cbor::type::negative_integer, 18446744073709551615u},
        {"20", cbor::type::negative_integer, 0},
        {"29", cbor::type::negative_integer, 9},
        {"3863", cbor::type::negative_integer, 99},
        {"3903e7", cbor::type::negative_integer, 999},
    };
    for (row const &r : rows) {
        INFO("payload: ", r.payload);
        auto const tree = cbor::decode(unhexlify(r.payload));
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type, r.type);
        if (r.type == cbor::type::unsigned_integer)
            CHECK_EQ((*tree)->get<cbor::type::unsigned_integer>().value(), r.argument);
        else
            CHECK_EQ((*tree)->get<cbor::type::negative_integer>().value(), r.argument);
    }

    // 18446744073709551616 and -18446744073709551617: tag 2 and tag 3 over the byte string 01 00 00 00 00 00 00 00 00
    // (RFC 8949 3.4.3).
    struct bignum_row {
        std::string_view payload;
        cbor::type type;
        std::uint64_t tag;
    };
    std::vector<bignum_row> const bignums{
        {"c249010000000000000000", cbor::type::unsigned_bignum, 2},
        {"c349010000000000000000", cbor::type::negative_bignum, 3},
    };
    for (bignum_row const &r : bignums) {
        INFO("payload: ", r.payload);
        auto const tree = cbor::decode(unhexlify(r.payload));
        REQUIRE(tree.has_value());
        REQUIRE_EQ((*tree)->type, r.type);
        auto const tag = r.type == cbor::type::unsigned_bignum ? (*tree)->get<cbor::type::unsigned_bignum>()
                                                                : (*tree)->get<cbor::type::negative_bignum>();
        REQUIRE(tag.has_value());
        CHECK_EQ(tag->first, r.tag);
        auto const magnitude = tag->second->get<cbor::type::byte_string>();
        REQUIRE(magnitude.has_value());
        std::string const m(reinterpret_cast<char const *>(magnitude->data()), magnitude->size());
        CHECK_EQ(hexlify(m), "010000000000000000");
    }
}

#ifdef CBOR_HAS_INT128
// Upstream: test_integer, test_positive_bignum and test_negative_bignum, read as a C++ integer. structs.hpp: "an
// integer of 8 to 64 bits; __int128 | an integer; a bignum (tag 2, 3)".
TEST_CASE("cbor2 decoder: test_integer as cbor::int128")
{
    cbor::int128 const two_64 = static_cast<cbor::int128>(1) << 64;
    struct row {
        std::string_view payload;
        cbor::int128 expected;
    };
    std::vector<row> const rows{
        {"00", 0},
        {"01", 1},
        {"0a", 10},
        {"17", 23},
        {"1818", 24},
        {"1819", 25},
        {"1864", 100},
        {"1903e8", 1000},
        {"1a000f4240", 1000000},
        {"1b000000e8d4a51000", 1000000000000},
        {"1bffffffffffffffff", two_64 - 1},
        {"c249010000000000000000", two_64},
        {"3bffffffffffffffff", -two_64},
        {"c349010000000000000000", -two_64 - 1},
        {"20", -1},
        {"29", -10},
        {"3863", -100},
        {"3903e7", -1000},
    };
    for (row const &r : rows) {
        INFO("payload: ", r.payload);
        auto const decoded = cbor::structs<cbor::int128>::decode(unhexlify(r.payload));
        REQUIRE(decoded.has_value());
        CHECK((**decoded == r.expected));
    }
}
#endif

// Upstream: test_invalid_integer_subtype. RFC 8949 3: the additional information 28 to 30 is reserved, so the
// head is not well-formed.
TEST_CASE("cbor2 decoder: test_invalid_integer_subtype")
{
    CHECK_EQ(decode_error_of(unhexlify("1c")), error::syntax_error);
}

// Upstream: test_float. RFC 8949 3.3 and Appendix A give each value. The comparison is of the bits, so the sign
// of -0.0 counts, as Appendix A gives it.
TEST_CASE("cbor2 decoder: test_float")
{
    struct row {
        std::string_view payload;
        cbor::type type;
        double expected;
    };
    double const inf = std::numeric_limits<double>::infinity();
    std::vector<row> const rows{
        {"f90000", cbor::type::half_float, 0.0},
        {"f98000", cbor::type::half_float, -0.0},
        {"f93c00", cbor::type::half_float, 1.0},
        {"fb3ff199999999999a", cbor::type::double_float, 1.1},
        {"f93e00", cbor::type::half_float, 1.5},
        {"f97bff", cbor::type::half_float, 65504.0},
        {"fa47c35000", cbor::type::single_float, 100000.0},
        {"fa7f7fffff", cbor::type::single_float, 3.4028234663852886e38},
        {"fb7e37e43c8800759c", cbor::type::double_float, 1.0e300},
        {"f90001", cbor::type::half_float, 5.960464477539063e-8},
        {"f90400", cbor::type::half_float, 0.00006103515625},
        {"f9c400", cbor::type::half_float, -4.0},
        {"fbc010666666666666", cbor::type::double_float, -4.1},
        {"f97c00", cbor::type::half_float, inf},
        {"f9fc00", cbor::type::half_float, -inf},
        {"fa7f800000", cbor::type::single_float, inf},
        {"faff800000", cbor::type::single_float, -inf},
        {"fb7ff0000000000000", cbor::type::double_float, inf},
        {"fbfff0000000000000", cbor::type::double_float, -inf},
    };
    for (row const &r : rows) {
        INFO("payload: ", r.payload);
        auto const tree = cbor::decode(unhexlify(r.payload));
        REQUIRE(tree.has_value());
        REQUIRE_EQ((*tree)->type, r.type);
        double const got = r.type == cbor::type::half_float     ? (*tree)->get<cbor::type::half_float>().value()
                           : r.type == cbor::type::single_float ? (*tree)->get<cbor::type::single_float>().value()
                                                                : (*tree)->get<cbor::type::double_float>().value();
        CHECK_EQ(std::bit_cast<std::uint64_t>(got), std::bit_cast<std::uint64_t>(r.expected));
    }
}

// Upstream: test_float_nan.
TEST_CASE("cbor2 decoder: test_float_nan")
{
    for (std::string_view const payload : {"f97e00", "fa7fc00000", "fb7ff8000000000000"}) {
        INFO("payload: ", payload);
        auto const decoded = loads(unhexlify(payload));
        REQUIRE(decoded.has_value());
        REQUIRE(std::holds_alternative<double>(decoded->kind));
        CHECK(std::isnan(std::get<double>(decoded->kind)));
    }
}

// Upstream: test_special. RFC 8949 3.3, Table 3: simple values 20 to 23.
TEST_CASE("cbor2 decoder: test_special")
{
    struct row {
        std::string_view payload;
        cbor::type type;
        cbor::simple_value expected;
    };
    std::vector<row> const rows{
        {"f4", cbor::type::false_value, cbor::simple_value::false_value},
        {"f5", cbor::type::true_value, cbor::simple_value::true_value},
        {"f6", cbor::type::null, cbor::simple_value::null},
        {"f7", cbor::type::undefined, cbor::simple_value::undefined},
    };
    for (row const &r : rows) {
        INFO("payload: ", r.payload);
        auto const tree = cbor::decode(unhexlify(r.payload));
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type, r.type);
        auto const decoded = loads(unhexlify(r.payload));
        REQUIRE(decoded.has_value());
        CHECK(*decoded == V(test::simple{std::to_underlying(r.expected)}));
    }
}

// Upstream: test_binary.
TEST_CASE("cbor2 decoder: test_binary")
{
    check_decoded({
        {"40", bytes_of("")},
        {"4401020304", bytes_of("01020304")},
    });
    std::string const long_payload = unhexlify("5a00011170") + std::string(70000, '\x12');
    auto const decoded = loads(long_payload);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == test::value{test::bytes{std::string(70000, '\x12')}});
}

// Upstream: test_string.
TEST_CASE("cbor2 decoder: test_string")
{
    check_decoded({
        {"60", V("")},
        {"6161", V("a")},
        {"6449455446", V("IETF")},
        {"62225c", V("\"\\")},
        {"62c3bc", V("ü")},
        {"63e6b0b4", V("水")},
    });
    std::string const split_unicode = unhexlify("7a00010001") + std::string(65535, 'a') + unhexlify("c3b6");
    auto const decoded = loads(split_unicode);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == V(std::string(65535, 'a') + "ö"));
}

// Upstream: test_string_invalid_utf8 and TestStrErrorsAttribute.test_str_errors_handling[strict_mode]. RFC 8949
// 5.3.1 calls a text string that is not UTF-8 invalid, and lets a decoder check it or not. cbor2 raises an
// error. decode.hpp says "It does not check that a text string is UTF-8", as the owner decided.
TEST_CASE("cbor2 decoder: test_string_invalid_utf8")
{
    // So decode gives the tree with the bytes as they are.
    std::string const long_payload = unhexlify("7a00010000") + std::string(65535, 'a') + unhexlify("c3");
    std::vector<std::pair<std::string, std::size_t>> const cases = {
        {unhexlify("6198"), 1}, {long_payload, 65536}, {unhexlify("6b68656c6c6fff776f726c64"), 11}};
    for (auto const &[payload, length] : cases) {
        INFO("payload: ", hexlify(payload.substr(0, 16)));
        auto const tree = cbor::decode(payload);
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->get<cbor::type::text_string>()->size(), length);
    }
    // The indefinite string 7f6198ff: decode.hpp: "@retval indefinite_length a string, an array or a map has an
    // indefinite length". The error comes before the content is read; RFC 8949 3.2.3 lets a decoder refuse
    // indefinite lengths.
    CHECK_EQ(decode_error_of(unhexlify("7f6198ff")), error::indefinite_length);
}

// Upstream: test_str_errors_valid_utf8_unchanged.
TEST_CASE("cbor2 decoder: test_str_errors_valid_utf8_unchanged")
{
    std::string const payload = "\x78\x19Hello \xc3\xbcnicode \xe6\xb0\xb4 world!";
    auto const decoded = loads(payload);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == V("Hello ünicode 水 world!"));
}

// Upstream: test_string_oversized. The counts of the maps are larger than the bytes that are left. decode.hpp:
// "@retval too_little_data the item is not complete, or a count is larger than the bytes that are left".
TEST_CASE("cbor2 decoder: test_string_oversized")
{
    CHECK_EQ(decode_error_of(unhexlify("aeaeaeaeaeaeaeaeae0108c29843d90100d8249f0000aeae00c26ca799")),
             error::too_little_data);
}

// Upstream: test_string_issue_264_multiple_chunks_utf8_boundary.
TEST_CASE("cbor2 decoder: test_string_issue_264_multiple_chunks_utf8_boundary")
{
    std::string const text = std::string(65535, 'a') + "€" + std::string(65533, 'b') + "€" + std::string(100, 'd');
    REQUIRE_EQ(text.size(), 131174u);
    std::string const payload = unhexlify("7a00020066") + text;
    auto const decoded = loads(payload);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == V(text));
}

// Upstream: test_indefinite_bytestring_many_chunks and test_indefinite_string_many_chunks. decode.hpp:
// "@retval indefinite_length a string, an array or a map has an indefinite length"; RFC 8949 3.2.3 lets a
// decoder refuse indefinite lengths. cbor::diagnostic_notation reads them, and prints each chunk.
TEST_CASE("cbor2 decoder: test_indefinite_string_many_chunks")
{
    constexpr std::size_t count = 400000;
    std::string bytes_payload = "\x5f";
    for (std::size_t i = 0; i < count; ++i)
        bytes_payload += "\x41\x2a";
    bytes_payload += "\xff";
    CHECK_EQ(decode_error_of(bytes_payload), error::indefinite_length);
    auto const bytes_text = cbor::diagnostic_notation(bytes_payload);
    REQUIRE(bytes_text.has_value());
    CHECK(bytes_text->starts_with("(_ h'2a', h'2a', "));
    CHECK(bytes_text->ends_with(", h'2a')"));

    std::string text_payload = "\x7f";
    for (std::size_t i = 0; i + 1 < count; ++i)
        text_payload += "\x61\x61";
    text_payload += "\x62\xc3\xb6\xff";
    CHECK_EQ(decode_error_of(text_payload), error::indefinite_length);
    auto const text_text = cbor::diagnostic_notation(text_payload);
    REQUIRE(text_text.has_value());
    CHECK(text_text->starts_with("(_ \"a\", \"a\", "));
    CHECK(text_text->ends_with(", \"ö\")"));
}

// Upstream: test_array, test_map, test_nested_map and test_mixed_array_map.
TEST_CASE("cbor2 decoder: test_array, test_map, test_mixed_array_map")
{
    test::array one_to_25;
    for (int i = 1; i <= 25; ++i)
        one_to_25.push_back(V(i));
    check_decoded({
        {"80", A()},
        {"83010203", A(1, 2, 3)},
        {"8301820203820405", A(1, A(2, 3), A(4, 5))},
        {"98190102030405060708090a0b0c0d0e0f101112131415161718181819", test::value{one_to_25}},
        {"a0", M()},
        {"a201020304", M(1, 2, 3, 4)},
        {"a101a102a103f6", M(1, M(2, M(3, test::simple{22})))},
        {"a26161016162820203", M("a", 1, "b", A(2, 3))},
        {"826161a161626163", A("a", M("b", "c"))},
        {"a56161614161626142616361436164614461656145", M("a", "A", "b", "B", "c", "C", "d", "D", "e", "E")},
    });
}

// Upstream: test_streaming and test_allow_indefinite. The expected text of each row is the diagnostic notation
// of RFC 8949 Appendix A, except the last row, which is not in Appendix A. decode.hpp: "@retval indefinite_length
// a string, an array or a map has an indefinite length"; RFC 8949 3.2.3 lets a decoder refuse indefinite
// lengths. cbor2 gives the values; cbor::diagnostic_notation shows that this library reads the same items.
TEST_CASE("cbor2 decoder: test_streaming")
{
    check_streaming({
        {"5f42010243030405ff", "(_ h'0102', h'030405')"},
        {"7f657374726561646d696e67ff", "(_ \"strea\", \"ming\")"},
        {"9fff", "[_ ]"},
        {"9f018202039f0405ffff", "[_ 1, [2, 3], [_ 4, 5]]"},
        {"9f01820203820405ff", "[_ 1, [2, 3], [4, 5]]"},
        {"83018202039f0405ff", "[1, [2, 3], [_ 4, 5]]"},
        {"83019f0203ff820405", "[1, [_ 2, 3], [4, 5]]"},
        {"9f0102030405060708090a0b0c0d0e0f101112131415161718181819ff",
         "[_ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]"},
        {"bf61610161629f0203ffff", "{_ \"a\": 1, \"b\": [_ 2, 3]}"},
        {"826161bf61626163ff", "[\"a\", {_ \"b\": \"c\"}]"},
        {"bf6346756ef563416d7421ff", "{_ \"Fun\": true, \"Amt\": -2}"},
        {"d901029f010203ff", "258([_ 1, 2, 3])"},
        {"7f6177ff", "(_ \"w\")"},
    });
}

// Upstream: test_bad_streaming_strings, test_embedded_indefinite_data and test_invalid_indefinite_data_item. RFC
// 8949 3.2.3: each chunk of an indefinite string is a definite string of the same major type, so these items are
// not well-formed. diagnostic_notation.hpp: "@retval syntax_error ... a chunk of an indefinite string has the wrong
// type". cbor::decode refuses the indefinite head first.
TEST_CASE("cbor2 decoder: test_bad_streaming_strings")
{
    for (std::string_view const payload :
         {"5f42010200", "7f63737472a0", "7f61777f6177ffff", "5f41775f4177ffff", "7f01ff", "5f01ff"}) {
        INFO("payload: ", payload);
        std::string const bytes = unhexlify(payload);
        CHECK_EQ(decode_error_of(bytes), error::indefinite_length);
        auto const text = cbor::diagnostic_notation(bytes);
        REQUIRE_FALSE(text.has_value());
        CHECK_EQ(text.error(), error::syntax_error);
    }
}

// Upstream: test_indefinite_map_missing_value. RFC 8949 3.2.1: a break where a value is expected makes the item
// not well-formed.
TEST_CASE("cbor2 decoder: test_indefinite_map_missing_value")
{
    for (std::string_view const payload : {"bf6161ff", "bf6161016162ff", "d9010281bf6161016162ff"}) {
        INFO("payload: ", payload);
        std::string const bytes = unhexlify(payload);
        CHECK_EQ(decode_error_of(bytes), error::indefinite_length);
        auto const text = cbor::diagnostic_notation(bytes);
        REQUIRE_FALSE(text.has_value());
        CHECK_EQ(text.error(), error::syntax_error);
    }
}

// Upstream: test_misplaced_break. RFC 8949 3.2.1: a break stop code where a data item is expected makes the item
// not well-formed. decode.hpp: "@retval syntax_error a head is not well-formed".
TEST_CASE("cbor2 decoder: test_misplaced_break")
{
    for (std::string_view const payload : {"ff", "8301ff02", "a101ff", "da000186a0ff", "d9010aff", "d90100ff"}) {
        INFO("payload: ", payload);
        std::string const bytes = unhexlify(payload);
        CHECK_EQ(decode_error_of(bytes), error::syntax_error);
        auto const text = cbor::diagnostic_notation(bytes);
        REQUIRE_FALSE(text.has_value());
        CHECK_EQ(text.error(), error::syntax_error);
    }
}

// Upstream: test_simple_value and test_simple_val_as_key. RFC 8949 3.3: simple values 0 to 19 in one byte, 32 to
// 255 after the byte f8.
TEST_CASE("cbor2 decoder: test_simple_value")
{
    struct row {
        std::string_view payload;
        std::uint8_t value;
    };
    for (row const &r : std::vector<row>{{"e0", 0}, {"e2", 2}, {"f3", 19}, {"f820", 32}}) {
        INFO("payload: ", r.payload);
        auto const tree = cbor::decode(unhexlify(r.payload));
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type, cbor::type::simple_value);
        CHECK_EQ((*tree)->get<cbor::type::simple_value>().value(), cbor::simple_value{r.value});
    }
    check_decoded({{"A1F86301", M(test::simple{99}, 1)}});
}

// Upstream: test_invalid_simple_value. RFC 8949 3.3: a simple value below 32 in the two-byte form is not
// well-formed.
TEST_CASE("cbor2 decoder: test_invalid_simple_value")
{
    for (std::string_view const payload : {"f800", "f817", "f81f"}) {
        INFO("payload: ", payload);
        CHECK_EQ(decode_error_of(unhexlify(payload)), error::syntax_error);
    }
}

// Upstream: test_reserved_special_tags. RFC 8949 3.3: the additional information 28 to 30 of major type 7 is
// reserved, so the head is not well-formed.
TEST_CASE("cbor2 decoder: test_reserved_special_tags")
{
    for (std::string_view const payload : {"fc", "fd", "fe"}) {
        INFO("payload: ", payload);
        CHECK_EQ(decode_error_of(unhexlify(payload)), error::syntax_error);
    }
}

// Upstream: test_date. The tags 1004 and 100 (RFC 8943) are dates. cbor::decode reads them as plain tags and keeps
// the tag number and the content.
TEST_CASE("cbor2 decoder: test_date")
{
    check_decoded({
        {"d903ec6a323031332d30332d3231", tag_of(1004, V("2013-03-21"))},
        {"d8641945e8", tag_of(100, V(17896))},
    });
    // structs.hpp: "std::chrono::year_month_day | tag 100 on an integer of days". RFC 8943 2: the days count from
    // 1970-01-01.
    auto const date = cbor::structs<std::chrono::year_month_day>::decode(unhexlify("d8641945e8"));
    REQUIRE(date.has_value());
    CHECK((**date == std::chrono::year_month_day{std::chrono::year{2018}, std::chrono::month{12}, std::chrono::day{31}}));
}

// Upstream: test_date_epoch_out_of_range. 2147483647 days after 1970 is not a date of std::chrono::year_month_day.
TEST_CASE("cbor2 decoder: test_date_epoch_out_of_range")
{
    check_decoded({{"d8641a7fffffff", tag_of(100, V(2147483647))}});
    // 2147483647 days lie after the last day that std::chrono::year_month_day holds, and the structs guide gives
    // number_out_of_range for it (RFC 8943 2).
    CHECK_EQ(cbor::structs<std::chrono::year_month_day>::decode(unhexlify("d8641a7fffffff")).error_or(error{}),
             error::number_out_of_range);
}

// Upstream: test_datetime, test_datetime_secfrac, test_datetime_secfrac_overflow, test_datetime_timezone,
// test_datetime_secfrac_non_ascii_digits, test_datetime_invalid_string and test_datetime_string_without_offset.
// cbor::decode reads tag 0 as a plain tag and keeps the tag number and the content. cbor2 raises an error for the
// last four strings because they are not RFC 3339 date-times. RFC 8949 5.3.2 lets a decoder present a tag with an
// inadmissible value as it presents an unknown tag, and 5.4 lets it pass the tag on; the documentation of
// cbor::decode names no check of tag 0.
TEST_CASE("cbor2 decoder: test_datetime")
{
    check_decoded({
        {"c074323031332d30332d32315432303a30343a30305a", tag_of(0, V("2013-03-21T20:04:00Z"))},
        {"c0781b323031332d30332d32315432303a30343a30302e3338303834315a", tag_of(0, V("2013-03-21T20:04:00.380841Z"))},
        {"c07819323031332d30332d32315432323a30343a30302b30323a3030", tag_of(0, V("2013-03-21T22:04:00+02:00"))},
        {"c11a514b67b0", tag_of(1, V(1363896240))},
    });
    std::vector<std::string_view> const strings{
        "2018-08-02T07:00:59.1Z",
        "2018-08-02T07:00:59.01Z",
        "2018-08-02T07:00:59.001Z",
        "2018-08-02T07:00:59.0001Z",
        "2018-08-02T07:00:59.00001Z",
        "2018-08-02T07:00:59.000001Z",
        "2018-08-02T07:00:59.0000001Z",
        "2018-08-02T07:00:59.100500999999999999+00:00",
        "2018-08-02T07:00:59.999999999999999999+00:00",
        "2018-08-02T07:00:59+00:30",
        "2018-08-02T07:00:59-00:30",
        "2018-08-02T07:00:59+01:30",
        "2018-08-02T07:00:59-01:30",
        "2020-01-01T00:00:00.1\xe2\x85\xa8\xe2\x85\xa8Z",
        "0000-123-01",
        "2020-01-01T00:00:00",
        "2020-01-01",
    };
    for (std::string_view const s : strings) {
        INFO("string: ", s);
        std::string payload = "\xc0";
        if (s.size() < 24) {
            payload += static_cast<char>(0x60 + s.size());
        } else {
            payload += '\x78';
            payload += static_cast<char>(s.size());
        }
        payload += s;
        auto const decoded = loads(payload);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == tag_of(0, V(std::string(s))));
    }
}

// Upstream: test_datetime[timestamp/utc] and test_datetime[timestamp/eet], read as a C++ time. structs.hpp:
// "std::chrono::sys_seconds | tag 1 on an integer". RFC 8949 3.4.2: 1363896240 seconds after 1970-01-01T00:00Z
// is 2013-03-21T20:04:00Z.
TEST_CASE("cbor2 decoder: test_datetime as std::chrono::sys_seconds")
{
    auto const t = cbor::structs<std::chrono::sys_seconds>::decode(unhexlify("c11a514b67b0"));
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->time_since_epoch().count(), 1363896240);
    CHECK((**t == std::chrono::sys_days{std::chrono::year{2013} / 3 / 21} + std::chrono::hours{20} + std::chrono::minutes{4}));
}

// Upstream: test_datetime_overflow. The count 0x9b9b9b0000000000 does not fit the signed 64 bits of
// std::chrono::sys_seconds.
TEST_CASE("cbor2 decoder: test_datetime_overflow")
{
    check_decoded({{"c11b9b9b9b0000000000", tag_of(1, test::value{std::uint64_t{0x9b9b9b0000000000}})}});
    CHECK_EQ(cbor::structs<std::chrono::sys_seconds>::decode(unhexlify("c11b9b9b9b0000000000")).error_or(error{}),
             error::number_out_of_range);
}

// Upstream: test_datetime_value_too_large. cbor2 raises an error because the year is past the range of a Python
// datetime. RFC 8949 3.4.2 sets no such range, and the count 0x1616161616161616 fits std::chrono::sys_seconds, so
// the RFC is followed here. The six bytes after the item are not read (decode.hpp: "A byte after the item is not
// read, so the input can be a CBOR sequence").
TEST_CASE("cbor2 decoder: test_datetime_value_too_large")
{
    std::string const payload = unhexlify("c11b1616161616161616161616161616");
    check_decoded({{"c11b1616161616161616161616161616", tag_of(1, test::value{std::uint64_t{0x1616161616161616}})}});
    auto const t = cbor::structs<std::chrono::sys_seconds>::decode(std::string(payload));
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->time_since_epoch().count(), 0x1616161616161616);
}

// Upstream: test_datetime_date_out_of_range. The map of six pairs has a break where its third value is
// expected, so it is not well-formed (RFC 8949 3.2.1).
TEST_CASE("cbor2 decoder: test_datetime_date_out_of_range")
{
    CHECK_EQ(decode_error_of(unhexlify("a6c11b00002401001b000000000000ff00")), error::syntax_error);
}

// Upstream: test_bignum_non_bytestring; cbor2 refuses tag 2 or 3 over an array or a map. RFC 8949 5.3.2 lets a
// decoder pass a tag that it does not convert. The owner decided that cbor::decode keeps tag 2 or 3 over a content
// that is not a byte string, and the decode doc says that it is an unsigned_bignum or negative_bignum item over that
// content.
TEST_CASE("cbor2 decoder: test_bignum_non_bytestring")
{
    for (std::string_view const payload : {"c28105", "c2a10509", "c38100", "c3a10509"}) {
        INFO("payload: ", payload);
        auto const tree = cbor::decode(unhexlify(payload));
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type, payload[1] == '2' ? cbor::type::unsigned_bignum : cbor::type::negative_bignum);
    }
}

// Upstream: test_fraction, test_decimal_precision, test_bigfloat, test_decimal_payload_non_integer and
// test_decimal_payload_unpacking. cbor::decode reads tags 4 and 5 as plain tags and keeps the tag number and the
// content. cbor2 raises an error for the last seven rows, whose content RFC 8949 3.4.4 does not allow. RFC 8949
// 5.3.2 asks the type check of a decoder that makes a native value of the tag, and 5.4 lets a decoder that does not
// process a tag pass it on; cbor::decode makes no native value of tags 4 and 5.
TEST_CASE("cbor2 decoder: test_fraction, test_bigfloat")
{
    check_decoded({
        {"c48221196ab3", tag_of(4, A(negative_of(1), 27315))},
        {"c5822003", tag_of(5, A(negative_of(0), 3))},
        {"c48200fb3ff8000000000000", tag_of(4, A(0, 1.5))},
        {"c482fb3ff800000000000001", tag_of(4, A(1.5, 1))},
        {"c482f501", tag_of(4, A(test::simple{21}, 1))},
        {"c58200fb3ff8000000000000", tag_of(5, A(0, 1.5))},
        {"c582fb3ff800000000000001", tag_of(5, A(1.5, 1))},
        {"c400", tag_of(4, V(0))},
        {"c500", tag_of(5, V(0))},
    });
    // c482384dc252011f1fe37d0c70ff50456ba8b891997b07d6: the mantissa is a bignum of 18 bytes, larger than 64 bits,
    // so it stays a tag 2 node.
    auto const tree = cbor::decode(unhexlify("c482384dc252011f1fe37d0c70ff50456ba8b891997b07d6"));
    REQUIRE(tree.has_value());
    auto const tag = (*tree)->get<cbor::type::tag>();
    REQUIRE(tag.has_value());
    CHECK_EQ(tag->first, 4u);
    auto const pair = tag->second->get<cbor::type::array>();
    REQUIRE(pair.has_value());
    REQUIRE_EQ(pair->size(), 2u);
    CHECK_EQ((*pair)[0].get<cbor::type::negative_integer>().value(), 77u);
    auto const mantissa = (*pair)[1].get<cbor::type::unsigned_bignum>();
    REQUIRE(mantissa.has_value());
    CHECK_EQ(mantissa->first, 2u);
    auto const magnitude = mantissa->second->get<cbor::type::byte_string>();
    REQUIRE(magnitude.has_value());
    CHECK_EQ(hexlify(std::string(reinterpret_cast<char const *>(magnitude->data()), magnitude->size())),
             "011f1fe37d0c70ff50456ba8b891997b07d6");
}

// Upstream: test_complex. Tag 43000 is a complex number of the IANA registry. cbor::decode reads it as a plain
// tag and keeps the tag number and the content.
TEST_CASE("cbor2 decoder: test_complex")
{
    double const inf = std::numeric_limits<double>::infinity();
    check_decoded({
        {"d9a7f882f90000f90000", tag_of(43000, A(0.0, 0.0))},
        {"d9a7f882fb0000000000000000fb0000000000000000", tag_of(43000, A(0.0, 0.0))},
        {"d9a7f882f98000f98000", tag_of(43000, A(-0.0, -0.0))},
        {"d9a7f882f90000f93c00", tag_of(43000, A(0.0, 1.0))},
        {"d9a7f882fb0000000000000000fb3ff199999999999a", tag_of(43000, A(0.0, 1.1))},
        {"d9a7f882f93e00f93e00", tag_of(43000, A(1.5, 1.5))},
        {"d9a7f882f97bfff97bff", tag_of(43000, A(65504.0, 65504.0))},
        {"d9a7f882fa47c35000fa47c35000", tag_of(43000, A(100000.0, 100000.0))},
        {"d9a7f882f90000fb7e37e43c8800759c", tag_of(43000, A(0.0, 1.0e300))},
        {"d9a7f882f90000f90001", tag_of(43000, A(0.0, 5.960464477539063e-8))},
        {"d9a7f882f90000f90400", tag_of(43000, A(0.0, 0.00006103515625))},
        {"d9a7f882f90000f9c400", tag_of(43000, A(0.0, -4.0))},
        {"d9a7f882f90000fbc010666666666666", tag_of(43000, A(0.0, -4.1))},
        {"d9a7f882f90000f97c00", tag_of(43000, A(0.0, inf))},
        {"d9a7f882f97c00f90000", tag_of(43000, A(inf, 0.0))},
        {"d9a7f882f90000f9fc00", tag_of(43000, A(0.0, -inf))},
        {"d9a7f882f90000fa7f800000", tag_of(43000, A(0.0, inf))},
        {"d9a7f882f90000faff800000", tag_of(43000, A(0.0, -inf))},
    });
    // The three rows with a NaN: a NaN is not equal to itself, so each part is checked on its own.
    struct row {
        std::string_view payload;
        bool real_nan;
        bool imag_nan;
    };
    for (row const &r : std::vector<row>{{"d9a7f882f97e00fb0000000000000000", true, false},
                                         {"d9a7f882fb0000000000000000f97e00", false, true},
                                         {"d9a7f882f97e00f97e00", true, true}}) {
        INFO("payload: ", r.payload);
        auto const decoded = loads(unhexlify(r.payload));
        REQUIRE(decoded.has_value());
        test::tagged const *const t = test::get_if<test::tagged>(*decoded);
        REQUIRE(t != nullptr);
        CHECK_EQ(t->tag, 43000u);
        test::array const *const parts = test::get_if<test::array>(t->content);
        REQUIRE(parts != nullptr);
        REQUIRE_EQ(parts->size(), 2u);
        double const real = std::get<double>(parts->front().kind);
        double const imag = std::get<double>(parts->back().kind);
        CHECK_EQ(std::isnan(real), r.real_nan);
        CHECK_EQ(std::isnan(imag), r.imag_nan);
        if (!r.real_nan)
            CHECK_EQ(real, 0.0);
        if (!r.imag_nan)
            CHECK_EQ(imag, 0.0);
    }
}

// Upstream: test_rational, test_rational_invalid_iterable, test_rational_zero_denominator, test_regex,
// test_regex_unbalanced_parentheses, test_mime, test_mime_invalid_type, test_uuid, test_uuid_invalid_length,
// test_uuid_invalid_type, test_ipaddress, test_ipnetwork_address_too_long, TestDeprecatedIPAddress and
// TestDeprecatedIPNetwork. These tags have a Python type in cbor2. cbor::decode reads each as a plain tag and keeps
// the tag number and the content. cbor2 raises an error for the rows after the first of each group whose content
// its Python type refuses. RFC 8949 5.3.2 and 5.4 let a decoder that does not process a tag pass it on; the
// documentation of cbor::decode names no check of these tags.
TEST_CASE("cbor2 decoder: Python semantic tags read as plain tags")
{
    check_decoded({
        {"d81e820205", tag_of(30, A(2, 5))},
        {"d81e01", tag_of(30, V(1))},
        {"d81e820100", tag_of(30, A(1, 0))},
        {"d8236d68656c6c6f2028776f726c6429", tag_of(35, V("hello (world)"))},
        {"d8236c68656c6c6f2028776f726c64", tag_of(35, V("hello (world"))},
        {"d824787b436f6e74656e742d547970653a20746578742f706c61696e3b20636861727365743d2269736f"
         "2d383835392d3135220a4d494d452d56657273696f6e3a20312e300a436f6e74656e742d5472616e7366"
         "65722d456e636f64696e673a2071756f7465642d7072696e7461626c650a0a48656c6c6f203d41347572"
         "6f",
         tag_of(36, V("Content-Type: text/plain; charset=\"iso-8859-15\"\nMIME-Version: 1.0\n"
                      "Content-Transfer-Encoding: quoted-printable\n\nHello =A4uro"))},
        {"d82401", tag_of(36, V(1))},
        {"d825505eaffac8b51e480581277fdcc7842faf", tag_of(37, bytes_of("5eaffac8b51e480581277fdcc7842faf"))},
        {"d8254f5eaffac8b51e480581277fdcc7842f", tag_of(37, bytes_of("5eaffac8b51e480581277fdcc7842f"))},
        {"d82501", tag_of(37, V(1))},
        {"d83444c0000201", tag_of(52, bytes_of("c0000201"))},
        {"d83482181843c00002", tag_of(52, A(24, bytes_of("c00002")))},
        {"d8348244c00002011818", tag_of(52, A(bytes_of("c0000201"), 24))},
        {"d8365020010db81234deedbeefcafefacefeed", tag_of(54, bytes_of("20010db81234deedbeefcafefacefeed"))},
        {"d8368218304620010db81234", tag_of(54, A(48, bytes_of("20010db81234")))},
        {"d8368350fe8000000000020202fffffffe03030318404465746830",
         tag_of(54, A(bytes_of("fe8000000000020202fffffffe030303"), 64, bytes_of("65746830")))},
        {"d8368350fe8000000000020202fffffffe030303184002",
         tag_of(54, A(bytes_of("fe8000000000020202fffffffe030303"), 64, 2))},
        {"d8368350fe8000000000020202fffffffe030303f64465746830",
         tag_of(54, A(bytes_of("fe8000000000020202fffffffe030303"), test::simple{22}, bytes_of("65746830")))},
        {"d8368350fe8000000000020202fffffffe030303f64132",
         tag_of(54, A(bytes_of("fe8000000000020202fffffffe030303"), test::simple{22}, bytes_of("32")))},
        {"d83482181846c0000200ffff", tag_of(52, A(24, bytes_of("c0000200ffff")))},
        {"d8368218305220010db8123400000000000000000000ffff",
         tag_of(54, A(48, bytes_of("20010db8123400000000000000000000ffff")))},
        {"d9010444c00a0a01", tag_of(260, bytes_of("c00a0a01"))},
        {"d901045020010db885a3000000008a2e03707334", tag_of(260, bytes_of("20010db885a3000000008a2e03707334"))},
        {"d9010446010203040506", tag_of(260, bytes_of("010203040506"))},
        {"d9010443c00a0a", tag_of(260, bytes_of("c00a0a"))},
        {"d9010401", tag_of(260, V(1))},
        {"D90105A144C0A800001818", tag_of(261, M(bytes_of("c0a80000"), 24))},
        {"d90105a144c0a800641818", tag_of(261, M(bytes_of("c0a80064"), 24))},
        {"d90105a15020010db885a3000000008a2e000000001860", tag_of(261, M(bytes_of("20010db885a3000000008a2e00000000"), 96))},
        {"d90105a244c0a80064181844c0a800001818", tag_of(261, M(bytes_of("c0a80064"), 24, bytes_of("c0a80000"), 24))},
        {"d90105a144c0a80064420102", tag_of(261, M(bytes_of("c0a80064"), bytes_of("0102")))},
    });
}

// Upstream: TestSharedReference, TestMaximumDepth.test_self_referential_tag_as_map_key and
// test_self_referential_tag_as_set_element, read by cbor::decode. The value sharing specification
// (refs/value-sharing.html): "index 0 refers to the first value marked as shareable in the CBOR stream". cbor::decode
// resolves the two tags (owner decision): a tag 28 makes no node and marks the node of its content, and a tag 29 is
// a shared_reference node whose target is the node of that mark. The reference model shows a shared_reference as
// 29(index). A tag 29 with no mark before it is an error, as in cbor2.
TEST_CASE("cbor2 decoder: TestSharedReference read by cbor::decode")
{
    CHECK_EQ(decode_error_of(unhexlify("d81d05")), error::sharedref_index_not_marked);
    auto const ref = [](int const index) { return tag_of(29, V(index)); };
    check_decoded({
        {"D90102D81C8181D81D00", tag_of(258, A(A(ref(0))))},
        {"d90102d81c82d81c82d81c83010203d81d02d81d02", tag_of(258, A(A(A(1, 2, 3), ref(2)), ref(2)))},
        {"d81c81d81d00", A(ref(0))},
        {"d81ca100d81d00", M(0, ref(0))},
        {"82d81c82d81c61616162d81d00", A(A("a", "b"), ref(0))},
        {"82d81ca1616101d81d00", A(M("a", 1), ref(0))},
        {"82d81cd917706548656c6c6fd81d00", A(tag_of(6000, V("Hello")), ref(0))},
        {"a1d81cd903e7d81d0001", M(tag_of(999, ref(0)), 1)},
        {"d9010281d81cd903e7d81d00", tag_of(258, A(tag_of(999, ref(0))))},
    });
    // A tag 29 gives the node of its mark: in 28([29(0)]) the element names the array itself, and in
    // [28([28("a"), "b"]), 29(0)] the second element names the first.
    auto const cyclic = cbor::decode(unhexlify("d81c81d81d00"));
    REQUIRE(cyclic.has_value());
    CHECK_EQ((*cyclic)->get<cbor::type::array>().value()[0].get<cbor::type::shared_reference>().value().second,
             &**cyclic);
    auto const shared = cbor::decode(unhexlify("82d81c82d81c61616162d81d00"));
    REQUIRE(shared.has_value());
    auto const elements = (*shared)->get<cbor::type::array>().value();
    CHECK_EQ(elements[1].get<cbor::type::shared_reference>().value().second, &elements[0]);
}

// Upstream: TestSharedReference.test_nested_shareable_in_array, test_object_hook and test_tag_hook, and
// TestDecoderReuse, read into C++ values. structs.hpp: "Reads values marked as shareable". The value sharing
// specification numbers the shared values in the order of their tags 28.
TEST_CASE("cbor2 decoder: TestSharedReference read by cbor::structs")
{
    // TestDecoderReuse.test_decoder_reuse_resets_shared_refs: each decode starts with no shared value.
    auto const first = cbor::structs<std::string>::decode(unhexlify("d81c6b66697273745f76616c7565"));
    REQUIRE(first.has_value());
    CHECK_EQ(**first, "first_value");
    CHECK_EQ(cbor::structs<std::uint64_t>::decode(unhexlify("d81d00")).error_or(error{}),
             error::sharedref_index_not_marked);

    // TestSharedReference.test_bad_reference: no value is marked, so the index 5 names none. error.hpp:
    // "sharedref_index_not_marked: A tag 29 names no tag 28 before it."
    auto const bad = cbor::structs<std::uint64_t>::decode(unhexlify("d81d05"));
    CHECK_EQ(bad.error_or(error{}), error::sharedref_index_not_marked);
}

// Upstream: TestStringReference. Tags 25 and 256 are the string references of the IANA registry. cbor::decode
// reads them as plain tags and keeps the tag number and the content, also where cbor2 raises an error (the second
// and third rows). RFC 8949 5.4 lets a generic decoder pass a tag on.
TEST_CASE("cbor2 decoder: TestStringReference")
{
    test::value const s0 = tag_of(25, V(0));
    test::value const s1 = tag_of(25, V(1));
    check_decoded({
        {"d9010085656669727374d81900667365636f6e64d81900d81901", tag_of(256, A("first", s0, "second", s0, s1))},
        {"85656669727374d81900667365636f6e64d81900d81901", A("first", s0, "second", s0, s1)},
        {"d9010086656669727374d81900667365636f6e64d81900d81901d81903",
         tag_of(256, A("first", s0, "second", s0, s1, tag_of(25, V(3))))},
    });
    std::string const big(65537, 'a');
    auto const decoded = loads(unhexlify("d90100827a00010001") + big + unhexlify("d81900"));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == tag_of(256, A(big, s0)));

    // test_string_ref_namespace_index_over_65536 has an indefinite array. decode.hpp: "@retval indefinite_length a
    // string, an array or a map has an indefinite length".
    CHECK_EQ(decode_error_of(unhexlify("d901009f6b6161616161616161616161ff")), error::indefinite_length);
}

// Upstream: test_self_describe_cbor. decode.hpp: "One leading tag 55799 is read as its content, so the root is
// that content". RFC 8949 3.4.6.
TEST_CASE("cbor2 decoder: test_self_describe_cbor")
{
    check_decoded({{"d9d9f71903e8", V(1000)}});
    auto const tree = cbor::decode(unhexlify("d9d9f7c249010000000000000000"));
    REQUIRE(tree.has_value());
    CHECK_EQ((*tree)->type, cbor::type::unsigned_bignum);
}

// Upstream: test_unhandled_tag and test_nested_dict. RFC 8949 3.4: a tag number and its content.
TEST_CASE("cbor2 decoder: test_unhandled_tag")
{
    check_decoded({
        {"d917706548656c6c6f", tag_of(6000, V("Hello"))},
        {"A1D9177082010201", M(tag_of(6000, A(1, 2)), 1)},
    });
}

// Upstream: test_premature_end_of_stream and test_oversized_read. decode.hpp: "@retval too_little_data the item is
// not complete".
TEST_CASE("cbor2 decoder: test_premature_end_of_stream")
{
    for (std::string_view const payload : {"437879", "41", "61"}) {
        INFO("payload: ", payload);
        CHECK_EQ(decode_error_of(unhexlify(payload)), error::too_little_data);
    }
}

namespace
{

struct a_b {
    int a;
    int b;
};

} // namespace

template <>
struct cbor::describe_members<a_b> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{"a", &a_b::a},
        cbor::member{"b", &a_b::b},
    };
};

// Upstream: test_object_hook. The object hook of cbor2 makes an object of a map; structs reads a map into a struct.
TEST_CASE("cbor2 decoder: test_object_hook")
{
    auto const decoded = cbor::structs<a_b>::decode(unhexlify("A2616103616205"));
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->a, 3);
    CHECK_EQ((*decoded)->b, 5);
}

// Upstream: TestAllowDuplicateKeys.test_default_allows_duplicates. cbor2 keeps the last value of a key by default.
// cbor::decode gives every pair (item.hpp: "map | each key and its value one after the other"), as the second
// option of RFC 8949 5.6 lets it. cbor::structs refuses the map (structs.hpp: "@retval duplicate_key a key that
// comes twice"), as the first option of RFC 8949 5.6 lets it.
TEST_CASE("cbor2 decoder: TestAllowDuplicateKeys")
{
    check_decoded({{"a2616101616102", M("a", 1, "a", 2)}});

    // test_raises_on_duplicate[definite]: cbor2 with allow_duplicate_keys=False raises the error.
    auto const definite = cbor::structs<std::map<std::string, int>>::decode(unhexlify("a2616101616102"));
    REQUIRE_FALSE(definite.has_value());
    CHECK_EQ(definite.error(), error::duplicate_key);

    // test_raises_on_duplicate[indefinite]: structs.hpp: "@retval indefinite_length a head uses indefinite length".
    auto const indefinite = cbor::structs<std::map<std::string, int>>::decode(unhexlify("bf616101616102ff"));
    REQUIRE_FALSE(indefinite.has_value());
    CHECK_EQ(indefinite.error(), error::indefinite_length);

    // test_raises_on_duplicate_int_and_simple_value: cbor2 sees the integer 5 and the simple value 5 as one key.
    // RFC 8949 5.6.1: "a simple value 2 is not equivalent to an integer 2". The RFC is followed: the map has two
    // distinct keys.
    check_decoded({{"a20501e502", M(5, 1, test::simple{5}, 2)}});
}

// Upstream: TestStrictHashableTypes.test_default_allows_any_type and test_allows_strings, and test_set,
// test_immutable_keys and TestCustomDecoder. cbor::decode has no option for the key types; it gives each map and
// keeps tag 258 as a plain tag. The rows with an indefinite length give indefinite_length (decode.hpp).
TEST_CASE("cbor2 decoder: TestStrictHashableTypes, test_immutable_keys")
{
    check_decoded({
        {"a10102", M(1, 2)},
        {"d9010281f5", tag_of(258, A(test::simple{21}))},
        {"a2616101416202", M("a", 1, bytes_of("62"), 2)},
        {"a1d81c616101", M("a", 1)},
        {"d901028261614161", tag_of(258, A("a", bytes_of("61")))},
        {"d9010280", tag_of(258, A())},
        {"d9010283616361626161", tag_of(258, A("c", "b", "a"))},
        {"a1a1616161626163", M(M("a", "b"), "c")},
        {"a1a1a10101a1666e6573746564f5a1666e6573746564f4",
         M(M(M(1, 1), M("nested", test::simple{21})), M("nested", test::simple{20}))},
        {"a182010203", M(A(1, 2), 3)},
        {"a1d901028301020304", M(tag_of(258, A(1, 2, 3)), 4)},
        {"d9010282d90102820102d90102820304", tag_of(258, A(tag_of(258, A(1, 2)), tag_of(258, A(3, 4))))},
        {"d9010283050763666f6f", tag_of(258, A(5, 7, "foo"))},
        {"d81cda00013880d81d00", tag_of(80000, tag_of(29, V(0)))},
        {"da00013880820102", tag_of(80000, A(1, 2))},
    });
    for (std::string_view const payload : {"bf616101416202ff", "a17f6161ff01", "d901029f61614161ff", "a17f657374726561646d696e67ff01"}) {
        INFO("payload: ", payload);
        CHECK_EQ(decode_error_of(unhexlify(payload)), error::indefinite_length);
    }
}

// Upstream: test_set and test_immutable_keys, read into C++ sets. structs.hpp: "std::set, std::flat_set | tag 258
// on an array". The CBOR sets specification (refs/CBOR_SETS.md) defines tag 258.
TEST_CASE("cbor2 decoder: test_set as std::set")
{
    auto const set = cbor::structs<std::set<std::string>>::decode(unhexlify("d9010283616361626161"));
    REQUIRE(set.has_value());
    CHECK_EQ(**set, std::set<std::string>{"a", "b", "c"});

    auto const sets = cbor::structs<std::set<std::set<int>>>::decode(unhexlify("d9010282d90102820102d90102820304"));
    REQUIRE(sets.has_value());
    CHECK_EQ(**sets, std::set<std::set<int>>{{1, 2}, {3, 4}});

    auto const key = cbor::structs<std::map<std::set<int>, int>>::decode(unhexlify("a1d901028301020304"));
    REQUIRE(key.has_value());
    CHECK_EQ(**key, std::map<std::set<int>, int>{{{1, 2, 3}, 4}});
}

// Upstream: test_set_non_array. The CBOR sets specification puts tag 258 on an array. structs.hpp: "@retval
// incorrect_type an item of another type".
TEST_CASE("cbor2 decoder: test_set_non_array")
{
    auto const set = cbor::structs<std::set<std::string>>::decode(unhexlify("d9010263616263"));
    REQUIRE_FALSE(set.has_value());
    CHECK_EQ(set.error(), error::incorrect_type);
}

// Upstream: TestMaximumDepth.test_default. cbor2 refuses nesting past its own limit. The depth here is the one of
// this library, test::levels_main_thread_reads (small_stack.hpp), not the limit of upstream. validity.hpp: "Nesting
// has no fixed limit"; cbor::decode stops only near the end of the stack of the thread. RFC 8949 sets no depth limit.
TEST_CASE("cbor2 decoder: TestMaximumDepth.test_default")
{
    std::string payload(test::levels_main_thread_reads, '\x81');
    payload += '\x80';
    CHECK(cbor::decode(payload).has_value());
}

// Upstream: test_stream_position_after_decode and test_non_seekable_fp. decode.hpp: "A byte after the item is not
// read"; cbor::item_size gives where the item ends.
TEST_CASE("cbor2 decoder: test_stream_position_after_decode")
{
    std::string const payload = "\x01" "extra";
    check_decoded({{"016578747261", V(1)}});
    auto const size = cbor::item_size(payload);
    REQUIRE(size.has_value());
    CHECK_EQ(*size, 1u);
    CHECK_EQ(payload.substr(*size), "extra");

    auto const owner = std::make_shared<std::string const>("\x01\x02");
    std::vector<test::value> items;
    for (auto const item : cbor::sequence(owner)) {
        REQUIRE(item.has_value());
        auto const decoded = loads(*item);
        REQUIRE(decoded.has_value());
        items.push_back(*decoded);
    }
    CHECK(items == std::vector<test::value>{V(1), V(2)});
}

// Upstream: test_huge_truncated_array, test_huge_truncated_string, test_huge_truncated_data,
// test_huge_truncated_indefinite_data, test_indefinite_overflow and test_invalid_cbor. The lengths are packed with
// struct.pack("Q"), in the byte order of a little-endian machine. Each item ends before its bytes do.
TEST_CASE("cbor2 decoder: corrupted or invalid data")
{
    std::string const will_overflow = unhexlify("0000000000000001");
    std::string const huge_index = unhexlify("0000000000000080");
    for (std::string const &payload : {
             unhexlify("9b") + will_overflow,
             unhexlify("7b") + huge_index + unhexlify("70717273"),
             unhexlify("7B") + will_overflow,
             unhexlify("5b") + will_overflow,
             unhexlify("c788370016b8965bdb2074bff82e5a20e09bec21f8406e86442b87ec3ff245b70a47624dc9cdc682"
                       "4b2a4c52e95ec9d6b0534b71c2b49e4bf9031500cee6869979c297bb5a8b381e98db714108415e5c"
                       "50db78974c271579b01633a3ef6271be5c225eb2"),
         }) {
        INFO("payload: ", hexlify(payload.substr(0, 16)));
        CHECK_EQ(decode_error_of(payload), error::too_little_data);
    }
    // The indefinite strings: cbor::decode refuses the head (decode.hpp: "@retval indefinite_length"), and
    // cbor::diagnostic_notation finds the chunk longer than the bytes.
    for (std::string const &payload : {
             unhexlify("7F7B") + huge_index + unhexlify("70717273ff"),
             unhexlify("5f5B") + huge_index + unhexlify("70717273ff"),
             unhexlify("7f7bff0000000000000471717272ff"),
             unhexlify("5f5bff0000000000000471717272ff"),
         }) {
        INFO("payload: ", hexlify(payload));
        CHECK_EQ(decode_error_of(payload), error::indefinite_length);
        CHECK_FALSE(cbor::diagnostic_notation(payload).has_value());
    }
}

// Upstream: test_string_stack_threshold_boundary.
TEST_CASE("cbor2 decoder: test_string_stack_threshold_boundary")
{
    for (std::size_t const length : {std::size_t{255}, std::size_t{256}, std::size_t{257}}) {
        INFO("length: ", length);
        std::string payload = length < 256 ? unhexlify("78") + static_cast<char>(length)
                                           : unhexlify("79") + static_cast<char>(length >> 8) + static_cast<char>(length & 0xff);
        payload += std::string(length, 'a');
        auto const decoded = loads(payload);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == V(std::string(length, 'a')));
    }
}

// Upstream: test_load_from_file and test_loads_buffer_input.
TEST_CASE("cbor2 decoder: test_load_from_file")
{
    check_decoded({{"82010a", A(1, 10)}});
}

// Upstream: test_load_exceeds_buffer_size and test_load_buffer_truncation. The payloads are dumps() of the values,
// in the preferred serialization of RFC 8949 4.2.2 that cbor2 writes.
TEST_CASE("cbor2 decoder: test_load_exceeds_buffer_size")
{
    std::string const x4096(4096, 'x');
    std::string const x4089(4089, 'x');
    auto const read = loads(unhexlify("81591000") + x4096);
    REQUIRE(read.has_value());
    CHECK(*read == A(test::bytes{x4096}));

    auto const exact = loads(unhexlify("82590ff9") + x4089 + unhexlify("81591000") + x4096);
    REQUIRE(exact.has_value());
    CHECK(*exact == A(test::bytes{x4089}, A(test::bytes{x4096})));

    constexpr std::size_t count = 455;
    std::string payload = unhexlify("9901c7");
    test::array expected;
    for (std::size_t i = 0; i < count; ++i) {
        payload += unhexlify("fb40091eb851eb851f");
        expected.push_back(V(3.14));
    }
    auto const truncation = loads(payload);
    REQUIRE(truncation.has_value());
    CHECK(*truncation == test::value{expected});
}
