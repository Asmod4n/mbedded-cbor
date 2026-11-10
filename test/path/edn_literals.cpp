#include "edn.hpp"
#include "../small_stack.hpp"

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
    // RFC 4648 4: the alphabet of base64 has 64 characters; four of them make three bytes.
    constexpr unsigned base64_alphabet_size = 64;
    for (unsigned value = 0; value < base64_alphabet_size; value += 4) {
        unsigned const group = value << 18 | (value + 1) << 12 | (value + 2) << 6 | (value + 3);
        bits.push_back(static_cast<char>(group >> 16));
        bits.push_back(static_cast<char>(group >> 8 & 0xff));
        bits.push_back(static_cast<char>(group & 0xff));
    }
    std::string key_hex = "5830";
    constexpr std::string_view digits = "0123456789abcdef";
    for (char const c : bits) {
        key_hex += digits[static_cast<unsigned char>(c) >> 4];
        key_hex += digits[static_cast<unsigned char>(c) & 0xf];
    }
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

// 2.4.2: a map with a repeated key is well-formed but not valid CBOR, and the error names the repeated key. A
// container that does not end is no literal.
TEST_CASE("path edn literals: a container that does not end and a repeated key")
{
    refused("[1, 2");
    auto const q = cbor::path::compile<cbor::lazy>("$[{1: 2, 1: 3}]");
    REQUIRE_FALSE(q.has_value());
    CHECK_EQ(q.error(), error::duplicate_key);
}

// The owner decided on 2026-11-08 that the EDN literals of a path write no indefinite length, because the
// library reads none. Each form of the EDN draft that writes one (4.2 [_ and {_, 4.3 (_ and the empty ''_ and
// ""_) is refused at run time and at compile time.
TEST_CASE("path edn literals: an indefinite-length literal is invalid_path")
{
    for (std::string_view const l :
         {"[_ 1, 2]"sv, "{_ 1: 2}"sv, "[_ ]"sv, "(_ h'01')"sv, "(_ \"a\")"sv, "''_"sv, "\"\"_"sv})
        refused(l);
    CHECK_FALSE(cbor::is_valid_path_v<"$[[_ 1, 2]]">);
    CHECK_FALSE(cbor::is_valid_path_v<"$[(_ \"a\")]">);
    CHECK_FALSE(cbor::is_valid_path_v<"$[''_]">);
}

// RFC 8949 Appendix A: the diagnostic notation that cbor::diagnostic_notation writes for an example is a
// literal of the same value. Every example that a selector reads as a key literal (not an integer, which is
// an index, and not a text, which is a name of RFC 9535) selects the key of its own bytes. Not here: the
// examples whose key is of indefinite length, as at_path does not read indefinite-length input
// (tools/site/guides/basics.md). Three floats whose decimal form has more digits than an exact conversion
// takes read back only where std::from_chars converts them.
TEST_CASE("path edn literals: the diagnostic notation of each example of RFC 8949 Appendix A is a key")
{
    for (std::string_view const hex : {"c249010000000000000000"sv,
                                       "c349010000000000000000"sv,
                                       "f90000"sv,
                                       "f98000"sv,
                                       "f93c00"sv,
                                       "fb3ff199999999999a"sv,
                                       "f93e00"sv,
                                       "f97bff"sv,
                                       "fa47c35000"sv,
                                       "f90400"sv,
                                       "f9c400"sv,
                                       "fbc010666666666666"sv,
                                       "f97c00"sv,
                                       "f97e00"sv,
                                       "f9fc00"sv,
                                       "fa7f800000"sv,
                                       "fa7fc00000"sv,
                                       "faff800000"sv,
                                       "fb7ff0000000000000"sv,
                                       "fb7ff8000000000000"sv,
                                       "fbfff0000000000000"sv,
                                       "f4"sv,
                                       "f5"sv,
                                       "f6"sv,
                                       "f7"sv,
                                       "f0"sv,
                                       "f8ff"sv,
                                       "c074323031332d30332d32315432303a30343a30305a"sv,
                                       "c11a514b67b0"sv,
                                       "c1fb41d452d9ec200000"sv,
                                       "d74401020304"sv,
                                       "d818456449455446"sv,
                                       "d82076687474703a2f2f7777772e6578616d706c652e636f6d"sv,
                                       "40"sv,
                                       "4401020304"sv,
                                       "80"sv,
                                       "83010203"sv,
                                       "8301820203820405"sv,
                                       "98190102030405060708090a0b0c0d0e0f101112131415161718181819"sv,
                                       "a0"sv,
                                       "a201020304"sv,
                                       "a26161016162820203"sv,
                                       "826161a161626163"sv,
                                       "a56161614161626142616361436164614461656145"sv}) {
        auto const text = cbor::diagnostic_notation(test::unhexlify(hex));
        REQUIRE(text.has_value());
        selects(*text, hex);
    }
    if (from_chars_reads_double)
        for (std::string_view const hex : {"fa7f7fffff"sv, "fb7e37e43c8800759c"sv, "f90001"sv}) {
            auto const text = cbor::diagnostic_notation(test::unhexlify(hex));
            REQUIRE(text.has_value());
            selects(*text, hex);
        }
}

// The parser writes each container and each embedded item with a placeholder for its head and fills it at the
// close, then closes the gaps in one pass, so a literal is parsed in linear time. These literals check a head at
// every level, a tag before a container, empty containers, an embedded item that holds containers, the
// encoding indicators of a head (a key selects by value, so in preferred serialization), and a map as the key of
// a map. The bytes are written from RFC 8949 3, 3.4.5.1
// and 4.2.1 (key order) and from draft-ietf-cbor-edn-literals-28 2.4 to 4.1.
TEST_CASE("path edn literals: nested and embedded literals give the bytes of their heads")
{
    for (std::size_t const levels : {std::size_t{64}, test::levels_main_thread_reads}) {
        CAPTURE(levels);
        std::string arrays_hex;
        std::string maps_hex;
        for (std::size_t i = 0; i < levels; ++i) {
            arrays_hex += "81";
            maps_hex += "a101";
        }
        selects(std::string(levels, '[') + "1" + std::string(levels, ']'), arrays_hex + "01");
        std::string maps;
        for (std::size_t i = 0; i < levels; ++i)
            maps += "{1: ";
        selects(maps + "2" + std::string(levels, '}'), maps_hex + "02");
    }
    selects("[0(\"a\"), {}, [], <<[1, [2]]>>]", "84c06161a0804482018102");
    selects("<<<<[1]>>>>", "43428101");
    selects("[_1 1]", "8101");
    selects("<<1>>_0", "4101");
    selects("{{2: 0, 1: 0}: 1}", "a1a20100020001");
}
