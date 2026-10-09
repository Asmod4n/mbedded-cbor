#include "binding.hpp"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <memory>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

std::string bytes_of_hex(std::string_view const hex)
{
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned value = 0;
        std::from_chars(hex.substr(i, 2).data(), std::to_address(hex.substr(i, 2).end()), value, 16);
        bytes.push_back(static_cast<char>(value));
    }
    return bytes;
}

std::string inspected(std::string_view const hex)
{
    auto const r = cbor::diagnostic_notation(bytes_of_hex(hex));
    REQUIRE(r.has_value());
    return *r;
}

} // namespace

// RFC 8949 Appendix A gives the diagnostic notation of each example. The text is the table of the RFC, except where
// the draft of EDN asks for another form: an encoding indicator on a float that is not in its preferred
// serialization (Table 2 of the draft). A bignum shows as its tag, and a text string shows its UTF-8 unescaped.
TEST_CASE("diagnostic_notation: the examples of RFC 8949 Appendix A")
{
    std::pair<std::string_view, std::string_view> const examples[] = {
        {"00", "0"},
        {"01", "1"},
        {"0a", "10"},
        {"17", "23"},
        {"1818", "24"},
        {"1819", "25"},
        {"1864", "100"},
        {"1903e8", "1000"},
        {"1a000f4240", "1000000"},
        {"1b000000e8d4a51000", "1000000000000"},
        {"1bffffffffffffffff", "18446744073709551615"},
        {"c249010000000000000000", "2(h'010000000000000000')"},
        {"3bffffffffffffffff", "-18446744073709551616"},
        {"c349010000000000000000", "3(h'010000000000000000')"},
        {"20", "-1"},
        {"29", "-10"},
        {"3863", "-100"},
        {"3903e7", "-1000"},
        {"f90000", "0.0"},
        {"f98000", "-0.0"},
        {"f93c00", "1.0"},
        {"fb3ff199999999999a", "1.1"},
        {"f93e00", "1.5"},
        {"f97bff", "65504.0"},
        {"fa47c35000", "100000.0"},
        {"fa7f7fffff", "3.4028234663852886e+38"},
        {"fb7e37e43c8800759c", "1.0e+300"},
        {"f90001", "5.960464477539063e-8"},
        {"f90400", "0.00006103515625"},
        {"f9c400", "-4.0"},
        {"fbc010666666666666", "-4.1"},
        {"f97c00", "Infinity"},
        {"f97e00", "NaN"},
        {"f9fc00", "-Infinity"},
        {"fa7f800000", "Infinity_2"},
        {"fa7fc00000", "NaN_2"},
        {"faff800000", "-Infinity_2"},
        {"fb7ff0000000000000", "Infinity_3"},
        {"fb7ff8000000000000", "NaN_3"},
        {"fbfff0000000000000", "-Infinity_3"},
        {"f4", "false"},
        {"f5", "true"},
        {"f6", "null"},
        {"f7", "undefined"},
        {"f0", "simple(16)"},
        {"f8ff", "simple(255)"},
        {"c074323031332d30332d32315432303a30343a30305a", "0(\"2013-03-21T20:04:00Z\")"},
        {"c11a514b67b0", "1(1363896240)"},
        {"c1fb41d452d9ec200000", "1(1363896240.5)"},
        {"d74401020304", "23(h'01020304')"},
        {"d818456449455446", "24(h'6449455446')"},
        {"d82076687474703a2f2f7777772e6578616d706c652e636f6d", "32(\"http://www.example.com\")"},
        {"40", "h''"},
        {"4401020304", "h'01020304'"},
        {"60", "\"\""},
        {"6161", "\"a\""},
        {"6449455446", "\"IETF\""},
        {"62225c", "\"\\\"\\\\\""},
        {"62c3bc", "\"\xc3\xbc\""},
        {"63e6b0b4", "\"\xe6\xb0\xb4\""},
        {"64f0908591", "\"\xf0\x90\x85\x91\""},
        {"80", "[]"},
        {"83010203", "[1, 2, 3]"},
        {"8301820203820405", "[1, [2, 3], [4, 5]]"},
        {"98190102030405060708090a0b0c0d0e0f101112131415161718181819",
         "[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]"},
        {"a0", "{}"},
        {"a201020304", "{1: 2, 3: 4}"},
        {"a26161016162820203", "{\"a\": 1, \"b\": [2, 3]}"},
        {"826161a161626163", "[\"a\", {\"b\": \"c\"}]"},
        {"a56161614161626142616361436164614461656145", "{\"a\": \"A\", \"b\": \"B\", \"c\": \"C\", \"d\": \"D\", \"e\": \"E\"}"},
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
    };
    for (auto const &[hex, text] : examples) {
        CAPTURE(hex);
        CHECK_EQ(inspected(hex), text);
    }
}

// The test vectors carry the diagnostic notation where the JSON value cannot say it. Each one is checked against
// the output, except the floats that are not in their preferred serialization, which the draft writes with an
// encoding indicator.
TEST_CASE("diagnostic_notation: the diagnostic field of the test vectors")
{
    std::ifstream in(TEST_VECTORS "/appendix_a.json");
    REQUIRE(in.good());
    std::string const json{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::size_t checked = 0;
    for (std::size_t at = 0; (at = json.find("\"hex\"", at)) != std::string::npos;) {
        std::size_t const open = json.find('"', json.find(':', at)) + 1;
        std::size_t const close = json.find('"', open);
        std::size_t const object_end = json.find('}', close);
        std::size_t const field = json.find("\"diagnostic\"", close);
        at = close;
        if (field > object_end)
            continue;
        std::string const hex = json.substr(open, close - open);
        if (hex.starts_with("fa") || hex.starts_with("fb"))
            continue;
        std::size_t const d_open = json.find('"', json.find(':', field)) + 1;
        std::string diagnostic;
        for (std::size_t i = d_open; json.at(i) != '"'; ++i) {
            if (json.at(i) == '\\')
                ++i;
            diagnostic.push_back(json.at(i));
        }
        CAPTURE(hex);
        CHECK_EQ(inspected(hex), diagnostic);
        ++checked;
    }
    CHECK_EQ(checked, 17u);
}

// draft-ietf-cbor-edn-literals-28 4.1 Table 7 and 4.2 (the table writes "A"_1 beside 79000161, whose content is
// the letter a): a head that is not the preferred one carries _0 to _3,
// an empty indefinite-length string is ''_ or ""_ (4.3), and 3.7 notes a NaN with a payload as float''.
TEST_CASE("diagnostic_notation: encoding indicators of the EDN draft")
{
    CHECK_EQ(inspected("190001"), "1_1");
    CHECK_EQ(inspected("1801"), "1_0");
    CHECK_EQ(inspected("1a00000002"), "2_2");
    CHECK_EQ(inspected("390000"), "-1_1");
    CHECK_EQ(inspected("59000141"), "h'41'_1");
    CHECK_EQ(inspected("79000161"), "\"a\"_1");
    CHECK_EQ(inspected("7805" "68656c6c6f"), "\"hello\"_0");
    CHECK_EQ(inspected("99000163626172"), "[_1 \"bar\"]");
    CHECK_EQ(inspected("b900016362617201"), "{_1 \"bar\": 1}");
    CHECK_EQ(inspected("d90001191267"), "1_1(4711)");
    CHECK_EQ(inspected("9802f4f5"), "[_0 false, true]");
    CHECK_EQ(inspected("fa3fc00000"), "1.5_2");
    CHECK_EQ(inspected("fb3ff8000000000000"), "1.5_3");
    CHECK_EQ(inspected("5fff"), "''_");
    CHECK_EQ(inspected("82d81c6178d81d00"), "[28(\"x\"), 29(0)]");
    CHECK_EQ(inspected("7fff"), "\"\"_");
    CHECK_EQ(inspected("5f40ff"), "(_ h'')");
    CHECK_EQ(inspected("f97e01"), "float'7e01'");
    CHECK_EQ(inspected("6a0a0d09085c2f0c011f7f"), "\"\\n\\r\\t\\b\\\\/\\f\\u0001\\u001f\x7f\"");
}

// An item that is not well-formed has no diagnostic notation: the error comes back.
TEST_CASE("diagnostic_notation: an item that is not well-formed is an error")
{
    CHECK_EQ(cbor::diagnostic_notation(""sv).error(), error::too_little_data);
    CHECK_EQ(cbor::diagnostic_notation("\xff"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::diagnostic_notation("\xf8\x18"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::diagnostic_notation("\xf8\x10"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::diagnostic_notation("\x1c"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::diagnostic_notation("\x1f"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::diagnostic_notation("\x62\x61"sv).error(), error::too_little_data);
    CHECK_EQ(cbor::diagnostic_notation("\x5f\x61\x61\xff"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::diagnostic_notation("\x5f\x5f\xff\xff"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::diagnostic_notation("\x9f\x01"sv).error(), error::too_little_data);
    CHECK_EQ(cbor::diagnostic_notation("\x00\x00"sv).error(), error::syntax_error);
    {
        test::nesting_depth_max_guard const depth{4};
        CHECK_EQ(cbor::diagnostic_notation("\x81\x81\x81\x81\x81\x00"sv).error(), error::nesting_depth_exceeded);
    }
    test::nesting_depth_max_guard const depth{5};
    CHECK(cbor::diagnostic_notation("\x81\x81\x81\x81\x81\x00"sv).has_value());
}
