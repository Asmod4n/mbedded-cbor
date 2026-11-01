#include "../owned_copy.hpp"
#include "../reference/path.hpp"
#include "../reference/value.hpp"
#include <cbor/error.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace path_edn_literals
{
namespace
{

std::string bytes_of_hex(std::string_view const hex)
{
    std::string bytes;
    int high = -1;
    for (char const c : hex) {
        int const v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0)
            continue;
        if (high < 0) {
            high = v;
        } else {
            bytes.push_back(static_cast<char>(high << 4 | v));
            high = -1;
        }
    }
    return bytes;
}

// A map with one entry: the key given in hex, the value the text "found".
cbor::lazy keyed(std::string_view const key_hex)
{
    auto const l = test::lazy_of_copy("\xa1"s + bytes_of_hex(key_hex) + "\x65" "found"s);
    REQUIRE(l.has_value());
    return *l;
}

// The literal, compiled at run time as the key of a name selector, selects the entry whose key has its value.
void selects(std::string_view const literal, std::string_view const key_hex)
{
    CAPTURE(literal);
    CAPTURE(key_hex);
    auto const v = test::at_path("$[" + std::string(literal) + "]", keyed(key_hex));
    REQUIRE(v.has_value());
    CHECK(*v == V("found"s));
}

void refused(std::string_view const literal)
{
    CAPTURE(literal);
    auto const q = cbor::jsonpath::compile<cbor::lazy>("$[" + std::string(literal) + "]");
    REQUIRE_FALSE(q.has_value());
    CHECK_EQ(q.error(), error::invalid_path);
}

} // namespace
} // namespace path_edn_literals

using namespace path_edn_literals;

// draft-ietf-cbor-edn-literals-28 2.2, Table 1: all the literals on a row are the same number, shown in CBOR. A
// key literal in a path is that number, in each of its forms.
TEST_CASE("path edn literals: the numbers of Table 1 of the EDN draft")
{
    for (std::string_view const l : {"4711"sv, "0x1267"sv, "0o11147"sv, "0b1001001100111"sv})
        selects(l, "191267");
    for (std::string_view const l : {"1.5"sv, "0.15e1"sv, "15e-1"sv, "0x1.8p0"sv, "0x18p-4"sv})
        selects(l, "f93e00");
    for (std::string_view const l : {"0"sv, "+0"sv, "-0"sv})
        selects(l, "00");
    for (std::string_view const l : {"0.0"sv, "+0.0"sv})
        selects(l, "f90000");
    selects("-0.0", "f98000");
    selects("Infinity", "f97c00");
    selects("-Infinity", "f9fc00");
    selects("NaN", "f97e00");
}

// 5.1: nonfin is Infinity, -Infinity or NaN; a sign in front of NaN or a plus sign in front of Infinity is no
// number. A hexfloat needs its p exponent, and no integer part has a leading zero.
TEST_CASE("path edn literals: forms the number grammar of the EDN draft does not have")
{
    for (std::string_view const l : {"+Infinity"sv, "-NaN"sv, "+NaN"sv, "0x1.8"sv, "0x1.8e0"sv, "00"sv, "01"sv, "0o8"sv, "0b2"sv})
        refused(l);
}

// 4.1, Table 2: an encoding indicator that leaves no room for the exact value is an error, and one that does gives
// the same number. A key selects by its value, so each form selects the key in preferred serialization.
TEST_CASE("path edn literals: the floats of Table 2 of the EDN draft")
{
    selects("1.1", "fb3ff199999999999a");
    refused("1.1_1");
    refused("1.1_2");
    selects("1.1_3", "fb3ff199999999999a");
    for (std::string_view const l : {"1.5"sv, "1.5_1"sv, "1.5_2"sv, "1.5_3"sv})
        selects(l, "f93e00");
    for (std::string_view const l : {"Infinity"sv, "Infinity_1"sv, "Infinity_2"sv, "Infinity_3"sv})
        selects(l, "f97c00");
    for (std::string_view const l : {"-Infinity"sv, "-Infinity_1"sv, "-Infinity_2"sv, "-Infinity_3"sv})
        selects(l, "f9fc00");
    for (std::string_view const l : {"NaN"sv, "NaN_1"sv, "NaN_2"sv, "NaN_3"sv})
        selects(l, "f97e00");
}

// 4.1, Table 7: the examples of definite length encoding indicators, one for each major type. Each literal is the
// data item that the column "ignoring e.i." encodes. 'A'_1 and "A"_1 are not here: in a path, a quoted string is
// a name selector of RFC 9535 2.3.1, which takes no encoding indicator.
TEST_CASE("path edn literals: the encoding indicators of Table 7 of the EDN draft")
{
    selects("1_1", "01");
    selects("0x4711_3", "194711");
    selects("-1_1", "20");
    selects("[_1 \"bar\"]", "8163626172");
    selects("{_1 \"bar\": 1}", "a16362617201");
    selects("1_1(4711)", "c1191267");
    selects("1.5_2", "f93e00");
    selects("0x4711p+03_3", "fa480e2200");
}

// 4.1, Table 6: _i is ai 0 to 23 and _0 to _3 are ai 24 to 27. An indicator with less room than the value needs is
// an error, as 4.1 says for floats; _4 and other words are reserved, not defined.
TEST_CASE("path edn literals: every encoding indicator of Table 6 on an integer")
{
    for (std::string_view const l : {"23_i"sv, "23_0"sv, "23_1"sv, "23_2"sv, "23_3"sv})
        selects(l, "17");
    refused("24_i");
    refused("256_0");
    refused("65536_1");
    refused("4294967296_2");
    selects("4294967296_3", "1b0000000100000000");
    refused("1_4");
    refused("1_x");
}

// 2.3.4: an unprefixed sequence literal is the byte string of the CBOR sequence it holds, with the encoding
// indicators of its items honored. Each pair of the draft's examples stands for one byte string.
TEST_CASE("path edn literals: the sequence literals of 2.3.4 of the EDN draft")
{
    selects("<<1>>", "4101");
    selects("<<1, 2>>", "420102");
    selects("<<\"hello\", null>>", "476568656c6c6ff6");
    selects("<<>>", "40");
    selects("<<1_1>>", "43190001");
    selects("<<1_0, 2_2>>", "4718011a00000002");
    selects("<<\"hello\"_0, null>>", "48780568656c6c6ff6");
    refused("<<1 2");
    refused("<<1,, 2>>");
}

// 2.6: simple(n) is major type 7 with value n, so simple(20) is false; the grammar has no value from 24 to 31,
// none above 255 and no leading zero.
TEST_CASE("path edn literals: the simple values of 2.6 of the EDN draft")
{
    selects("simple(42)", "f82a");
    selects("simple(20)", "f4");
    selects("simple( 0 )", "e0");
    selects("simple(255)", "f8ff");
    for (std::string_view const l : {"simple(24)"sv, "simple(31)"sv, "simple(256)"sv, "simple(042)"sv, "simple()"sv, "simple(1"sv})
        refused(l);
}

// 3.1: h and b64 give a byte string in base16 or in base64 or base64url without padding, with blank space ignored;
// the examples of the section stand for the same bytes.
TEST_CASE("path edn literals: the base-encoded byte strings of 3.1 of the EDN draft")
{
    selects("h'12345678'", "4412345678");
    selects("b64'EjRWeA'", "4412345678");
    for (std::string_view const l : {"h'48656c6c6f20776f726c64'"sv, "h'48 65 6c 6c 6f 20 77 6f 72 6c 64'"sv,
                                     "h'4 86 56c 6c6f\n  20776 f726c64'"sv})
        selects(l, "4b48656c6c6f20776f726c64");
    selects("h'FDB6AC 7BAE27A2D69CA2699E9EDFDBBADA2779FA25 968C2C'", "58" "18" "FDB6AC7BAE27A2D69CA2699E9EDFDBBADA2779FA25968C2C");
}

// 5.2.2, Figure 4: base64 may end in one or two padding characters that complete the last group; two digits take
// two pads or none, three digits one pad or none. One digit alone, a pad that does not complete the group, or a
// character outside both alphabets is no base64.
TEST_CASE("path edn literals: the padding of b64 of the EDN draft")
{
    selects("b64'AQ'", "4101");
    selects("b64'AQ=='", "4101");
    selects("b64'AQI'", "420102");
    selects("b64'AQI='", "420102");
    for (std::string_view const l : {"b64'A'"sv, "b64'AQ='"sv, "b64'AQI=='"sv, "b64'AQ*'"sv, "b64'AQ.'"sv, "b64'AQ"sv})
        refused(l);
}

// RFC 4648 4 and 5: the 64 characters of each alphabet in the order of their values 0 to 63. In that order they
// encode the bits 000000 000001 ... 111111, 48 bytes; base64url replaces + and / by - and _.
TEST_CASE("path edn literals: every character of both base64 alphabets")
{
    std::string bits;
    for (unsigned value = 0; value < 64; value += 4) {
        unsigned const group = value << 18 | (value + 1) << 12 | (value + 2) << 6 | (value + 3);
        bits.push_back(static_cast<char>(group >> 16));
        bits.push_back(static_cast<char>(group >> 8 & 0xff));
        bits.push_back(static_cast<char>(group & 0xff));
    }
    std::string key_hex = "5830";
    for (char const c : bits)
        key_hex += std::array{"0123456789abcdef"[static_cast<unsigned char>(c) >> 4], "0123456789abcdef"[static_cast<unsigned char>(c) & 0xf], '\0'}.data();
    selects("b64'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'", key_hex);
    selects("b64'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'", key_hex);
}

// 5.3.1: h'' takes pairs of hex digits; an odd count or another character is an error, and so is a literal that
// does not end.
TEST_CASE("path edn literals: forms h of the EDN draft does not have")
{
    for (std::string_view const l : {"h'123'"sv, "h'zz'"sv, "h'12"sv})
        refused(l);
}

// 4.2 and 2.4: [_ and {_ give indefinite-length encoding of the same array or map, and a key selects by value.
// 2.4.2: a map with a repeated key is well-formed but not valid CBOR, and the error names the repeated key.
TEST_CASE("path edn literals: indefinite-length containers of the EDN draft and a repeated key")
{
    selects("[_ 1, 2]", "820102");
    selects("{_ 1: 2}", "a10102");
    refused("[1, 2");
    auto const q = cbor::jsonpath::compile<cbor::lazy>("$[{1: 2, 1: 3}]");
    REQUIRE_FALSE(q.has_value());
    CHECK_EQ(q.error(), error::duplicate_key);
}
