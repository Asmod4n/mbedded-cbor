// The tests of the TinyCBOR parser, ported to the public API of mbedded-cbor. The tests show that mbedded-cbor reads
// the inputs of another library as that library reads them, or as RFC 8949 says where the two disagree.
//
// Upstream: https://github.com/intel/tinycbor, commit 0cb8ba93637a18e4497e85a3f5c681ee7dba38f4,
// tests/parser/tst_parser.cpp and tests/parser/data.cpp. These files carry this license:
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
// Upstream prints an item with cbor_value_to_pretty_stream(). Its text is the diagnostic notation of RFC 8949
// section 8 with these differences, and each expected text below follows the RFC and the EDN draft
// (refs/draft-ietf-cbor-edn-literals-28.txt), which cbor::diagnostic_notation() implements:
// - a float: upstream writes "0.f16", "0.f", "nan", "inf"; the RFC writes "0.0", "0.0_2", "NaN", "Infinity", with an
//   encoding indicator only where the float is not in its preferred serialization (EDN draft 3.1);
// - a text string with a character outside ASCII: upstream writes "\u00A0"; JSON (RFC 8259 section 7), and with it
//   RFC 8949 section 8, also allows the UTF-8 of the character, which cbor::diagnostic_notation() writes;
// - an empty indefinite-length string: upstream writes "(_ )"; the EDN draft (4.3) writes ''_ and ""_.
//
// The library does not read indefinite length outside cbor::diagnostic_notation() (tools/site/guides/advanced/
// basics.md, line 51: "The library does not read indefinite length. diagnostic_notation prints it."). Where upstream
// reads an indefinite-length item with another function, the port expects indefinite_length. RFC 8949 section 5.1
// lets a protocol leave out indefinite length.

#include "../small_stack.hpp"
#include "tinycbor.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;
using tinycbor_port::error_text;
using tinycbor_port::no_error;
using tinycbor_port::outcome;
using tinycbor_port::printed;

namespace tinycbor_parser
{

struct row {
    std::string_view name;
    std::string data;
    std::string_view upstream;
    std::string expected;
    bool indefinite;
};

struct float_row {
    std::string_view name;
    std::string data;
    std::string_view upstream;
    double value;
    cbor::type type;
    std::string_view indicator;
};

// One item that a composition of upstream puts into an array or a map.
struct element {
    std::string name;
    std::string data;
    std::string text;
    bool indefinite;
};

// addFixedData() without its floats.
std::vector<row> fixed_rows()
{
    return {
        {"0", "\x00"s, "0", "0", false},
        {"1", "\x01"s, "1", "1", false},
        {"10", "\x0a"s, "10", "10", false},
        {"23", "\x17"s, "23", "23", false},
        {"24", "\x18\x18"s, "24", "24", false},
        {"UINT8_MAX", "\x18\xff"s, "255", "255", false},
        {"UINT8_MAX+1", "\x19\x01\x00"s, "256", "256", false},
        {"UINT16_MAX", "\x19\xff\xff"s, "65535", "65535", false},
        {"UINT16_MAX+1", "\x1a\0\1\x00\x00"s, "65536", "65536", false},
        {"UINT32_MAX", "\x1a\xff\xff\xff\xff"s, "4294967295", "4294967295", false},
        {"UINT32_MAX+1", "\x1b\0\0\0\1\0\0\0\0"s, "4294967296", "4294967296", false},
        {"UINT64_MAX", "\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s, "18446744073709551615", "18446744073709551615", false},
        {"-1", "\x20"s, "-1", "-1", false},
        {"-2", "\x21"s, "-2", "-2", false},
        {"-24", "\x37"s, "-24", "-24", false},
        {"-25", "\x38\x18"s, "-25", "-25", false},
        {"-UINT8_MAX", "\x38\xff"s, "-256", "-256", false},
        {"-UINT8_MAX-1", "\x39\x01\x00"s, "-257", "-257", false},
        {"-UINT16_MAX", "\x39\xff\xff"s, "-65536", "-65536", false},
        {"-UINT16_MAX-1", "\x3a\0\1\x00\x00"s, "-65537", "-65537", false},
        {"-UINT32_MAX", "\x3a\xff\xff\xff\xff"s, "-4294967296", "-4294967296", false},
        {"-UINT32_MAX-1", "\x3b\0\0\0\1\0\0\0\0"s, "-4294967297", "-4294967297", false},
        {"INT64_MIN+1", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xfe"s, "-9223372036854775807", "-9223372036854775807", false},
        {"INT64_MIN", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xff"s, "-9223372036854775808", "-9223372036854775808", false},
        {"INT64_MIN-1", "\x3b\x80\0\0\0""\0\0\0\0"s, "-9223372036854775809", "-9223372036854775809", false},
        {"-UINT64_MAX", "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xfe"s, "-18446744073709551615", "-18446744073709551615", false},
        {"-UINT64_MAX+1", "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s, "-18446744073709551616", "-18446744073709551616", false},
        {"0*1", "\x18\x00"s, "0_0", "0_0", false},
        {"0*2", "\x19\x00\x00"s, "0_1", "0_1", false},
        {"0*4", "\x1a\0\0\0\0"s, "0_2", "0_2", false},
        {"0*8", "\x1b\0\0\0\0\0\0\0\0"s, "0_3", "0_3", false},
        {"-1*1", "\x38\x00"s, "-1_0", "-1_0", false},
        {"-1*2", "\x39\x00\x00"s, "-1_1", "-1_1", false},
        {"-1*4", "\x3a\0\0\0\0"s, "-1_2", "-1_2", false},
        {"-1*8", "\x3b\0\0\0\0\0\0\0\0"s, "-1_3", "-1_3", false},
        {"simple0", "\xe0"s, "simple(0)", "simple(0)", false},
        {"simple19", "\xf3"s, "simple(19)", "simple(19)", false},
        {"false", "\xf4"s, "false", "false", false},
        {"true", "\xf5"s, "true", "true", false},
        {"null", "\xf6"s, "null", "null", false},
        {"undefined", "\xf7"s, "undefined", "undefined", false},
        {"simple32", "\xf8\x20"s, "simple(32)", "simple(32)", false},
        {"simple255", "\xf8\xff"s, "simple(255)", "simple(255)", false},
    };
}

// The floats of addFixedData(), with the value and the type of addFloatingPoint(). The indicator is the one of the
// EDN draft: a half float is always in its preferred serialization; a wider float is not where a narrower float holds
// the same value.
std::vector<float_row> float_rows()
{
    double const inf = std::numeric_limits<double>::infinity();
    double const nan = std::numeric_limits<double>::quiet_NaN();
    return {
        {"0.f16", "\xf9\0\0"s, "0.f16", 0., cbor::type::half_float, ""},
        {"0.f", "\xfa\0\0\0\0"s, "0.f", 0., cbor::type::single_float, "_2"},
        {"0.", "\xfb\0\0\0\0\0\0\0\0"s, "0.", 0., cbor::type::double_float, "_3"},
        {"-1.f16", "\xf9\xbc\x00"s, "-1.f16", -1., cbor::type::half_float, ""},
        {"-1.f", "\xfa\xbf\x80\0\0"s, "-1.f", -1., cbor::type::single_float, "_2"},
        {"-1.", "\xfb\xbf\xf0\0\0\0\0\0\0"s, "-1.", -1., cbor::type::double_float, "_3"},
        {"65504.f16", "\xf9\x7b\xff"s, "65504.f16", 65504., cbor::type::half_float, ""},
        {"16777215.f", "\xfa\x4b\x7f\xff\xff"s, "16777215.f", 16777215., cbor::type::single_float, ""},
        {"16777215.", "\xfb\x41\x6f\xff\xff\xe0\0\0\0"s, "16777215.", 16777215., cbor::type::double_float, "_3"},
        {"-16777215.f", "\xfa\xcb\x7f\xff\xff"s, "-16777215.f", -16777215., cbor::type::single_float, ""},
        {"-16777215.", "\xfb\xc1\x6f\xff\xff\xe0\0\0\0"s, "-16777215.", -16777215., cbor::type::double_float, "_3"},
        {"0.5f16", "\xf9\x38\0"s, "0.5f16", 0.5, cbor::type::half_float, ""},
        {"0.5f", "\xfa\x3f\0\0\0"s, "0.5f", 0.5, cbor::type::single_float, "_2"},
        {"0.5", "\xfb\x3f\xe0\0\0\0\0\0\0"s, "0.5", 0.5, cbor::type::double_float, "_3"},
        {"2.f16^11-1", "\xf9\x67\xff"s, "2047.f16", 2047., cbor::type::half_float, ""},
        {"2.f^24-1", "\xfa\x4b\x7f\xff\xff"s, "16777215.f", 16777215., cbor::type::single_float, ""},
        {"2.^53-1", "\xfb\x43\x3f\xff\xff""\xff\xff\xff\xff"s, "9007199254740991.", 9007199254740991., cbor::type::double_float, ""},
        {"2.f^64-epsilon", "\xfa\x5f\x7f\xff\xff"s, "18446742974197923840.f", 0x1.fffffep+63, cbor::type::single_float, ""},
        {"2.^64-epsilon", "\xfb\x43\xef\xff\xff""\xff\xff\xff\xff"s, "18446744073709549568.", 0x1.fffffffffffffp+63, cbor::type::double_float, ""},
        {"2.f^64", "\xfa\x5f\x80\0\0"s, "1.8446744073709552e+19f", 0x1p64, cbor::type::single_float, ""},
        {"2.^64", "\xfb\x43\xf0\0\0\0\0\0\0"s, "1.8446744073709552e+19", 0x1p64, cbor::type::double_float, "_3"},
        {"nan_f16", "\xf9\x7e\x00"s, "nan", nan, cbor::type::half_float, ""},
        {"nan_f", "\xfa\x7f\xc0\0\0"s, "nan", nan, cbor::type::single_float, "_2"},
        {"nan", "\xfb\x7f\xf8\0\0\0\0\0\0"s, "nan", nan, cbor::type::double_float, "_3"},
        {"-inf_f16", "\xf9\xfc\x00"s, "-inf", -inf, cbor::type::half_float, ""},
        {"-inf_f", "\xfa\xff\x80\0\0"s, "-inf", -inf, cbor::type::single_float, "_2"},
        {"-inf", "\xfb\xff\xf0\0\0\0\0\0\0"s, "-inf", -inf, cbor::type::double_float, "_3"},
        {"+inf_f16", "\xf9\x7c\x00"s, "inf", inf, cbor::type::half_float, ""},
        {"+inf_f", "\xfa\x7f\x80\0\0"s, "inf", inf, cbor::type::single_float, "_2"},
        {"+inf", "\xfb\x7f\xf0\0\0\0\0\0\0"s, "inf", inf, cbor::type::double_float, "_3"},
    };
}

// addStringsData(), which holds addNonChunkedStringsData().
std::vector<row> string_rows()
{
    return {
        {"emptybytestring", "\x40"s, "h''", "h''", false},
        {"bytestring1", "\x41 "s, "h'20'", "h'20'", false},
        {"bytestring1-nul", "\x41\0"s, "h'00'", "h'00'", false},
        {"bytestring5", "\x45Hello"s, "h'48656c6c6f'", "h'48656c6c6f'", false},
        {"bytestring24", "\x58\x18""123456789012345678901234"s, "h'313233343536373839303132333435363738393031323334'",
         "h'313233343536373839303132333435363738393031323334'", false},
        {"bytestring256", "\x59\1\0"s + std::string(256, '3'), "h'33...'", "h'" + std::string(512, '3') + "'", false},
        {"emptytextstring", "\x60"s, "\"\"", "\"\"", false},
        {"textstring1", "\x61 "s, "\" \"", "\" \"", false},
        {"textstring1-nul", "\x61\0"s, "\"\\u0000\"", "\"\\u0000\"", false},
        {"textstring5", "\x65Hello"s, "\"Hello\"", "\"Hello\"", false},
        {"textstring24", "\x78\x18""123456789012345678901234"s, "\"123456789012345678901234\"",
         "\"123456789012345678901234\"", false},
        {"textstring256", "\x79\1\0"s + std::string(256, '3'), "\"33...\"", "\"" + std::string(256, '3') + "\"", false},
        {"textstringutf8-2char", "\x62\xc2\xa0"s, "\"\\u00A0\"", "\"\xc2\xa0\"", false},
        {"textstringutf8-2char2", "\x64\xc2\xa0\xc2\xa9"s, "\"\\u00A0\\u00A9\"", "\"\xc2\xa0\xc2\xa9\"", false},
        {"textstringutf8-3char", "\x63\xe2\x88\x80"s, "\"\\u2200\"", "\"\xe2\x88\x80\"", false},
        {"textstringutf8-4char", "\x64\xf0\x90\x88\x83"s, "\"\\uD800\\uDE03\"", "\"\xf0\x90\x88\x83\"", false},
        {"emptybytestring*1", "\x58\x00"s, "h''_0", "h''_0", false},
        {"emptytextstring*1", "\x78\x00"s, "\"\"_0", "\"\"_0", false},
        {"emptybytestring*2", "\x59\x00\x00"s, "h''_1", "h''_1", false},
        {"emptytextstring*2", "\x79\x00\x00"s, "\"\"_1", "\"\"_1", false},
        {"emptybytestring*4", "\x5a\0\0\0\0"s, "h''_2", "h''_2", false},
        {"emptytextstring*4", "\x7a\0\0\0\0"s, "\"\"_2", "\"\"_2", false},
        {"emptybytestring*8", "\x5b\0\0\0\0\0\0\0\0"s, "h''_3", "h''_3", false},
        {"emptytextstring*8", "\x7b\0\0\0\0\0\0\0\0"s, "\"\"_3", "\"\"_3", false},
        {"bytestring5*1", "\x58\x05Hello"s, "h'48656c6c6f'_0", "h'48656c6c6f'_0", false},
        {"textstring5*1", "\x78\x05Hello"s, "\"Hello\"_0", "\"Hello\"_0", false},
        {"bytestring5*2", "\x59\0\5Hello"s, "h'48656c6c6f'_1", "h'48656c6c6f'_1", false},
        {"textstring5*2", "\x79\0\x05Hello"s, "\"Hello\"_1", "\"Hello\"_1", false},
        {"bytestring5*4", "\x5a\0\0\0\5Hello"s, "h'48656c6c6f'_2", "h'48656c6c6f'_2", false},
        {"textstring5*4", "\x7a\0\0\0\x05Hello"s, "\"Hello\"_2", "\"Hello\"_2", false},
        {"bytestring5*8", "\x5b\0\0\0\0\0\0\0\5Hello"s, "h'48656c6c6f'_3", "h'48656c6c6f'_3", false},
        {"textstring5*8", "\x7b\0\0\0\0\0\0\0\x05Hello"s, "\"Hello\"_3", "\"Hello\"_3", false},
        {"_emptybytestring", "\x5f\xff"s, "(_ )", "''_", true},
        {"_emptytextstring", "\x7f\xff"s, "(_ )", "\"\"_", true},
        {"_emptybytestring2", "\x5f\x40\xff"s, "(_ h'')", "(_ h'')", true},
        {"_emptytextstring2", "\x7f\x60\xff"s, "(_ \"\")", "(_ \"\")", true},
        {"_emptybytestring2*1", "\x5f\x58\x00\xff"s, "(_ h''_0)", "(_ h''_0)", true},
        {"_emptytextstring2*1", "\x7f\x78\x00\xff"s, "(_ \"\"_0)", "(_ \"\"_0)", true},
        {"_emptybytestring3", "\x5f\x40\x40\xff"s, "(_ h'', h'')", "(_ h'', h'')", true},
        {"_emptytextstring3", "\x7f\x60\x60\xff"s, "(_ \"\", \"\")", "(_ \"\", \"\")", true},
        {"_emptybytestring3*2", "\x5f\x59\x00\x00\x40\xff"s, "(_ h''_1, h'')", "(_ h''_1, h'')", true},
        {"_emptytextstring3*2", "\x7f\x79\x00\x00\x60\xff"s, "(_ \"\"_1, \"\")", "(_ \"\"_1, \"\")", true},
        {"_bytestring5x2", "\x5f\x43Hel\x42lo\xff"s, "(_ h'48656c', h'6c6f')", "(_ h'48656c', h'6c6f')", true},
        {"_textstring5x2", "\x7f\x63Hel\x62lo\xff"s, "(_ \"Hel\", \"lo\")", "(_ \"Hel\", \"lo\")", true},
        {"_bytestring5x2*8*4", "\x5f\x5b\0\0\0\0\0\0\0\3Hel\x5a\0\0\0\2lo\xff"s, "(_ h'48656c'_3, h'6c6f'_2)",
         "(_ h'48656c'_3, h'6c6f'_2)", true},
        {"_textstring5x2*8*4", "\x7f\x7b\0\0\0\0\0\0\0\3Hel\x7a\0\0\0\2lo\xff"s, "(_ \"Hel\"_3, \"lo\"_2)",
         "(_ \"Hel\"_3, \"lo\"_2)", true},
        {"_bytestring5x5", "\x5f\x41H\x41""e\x41l\x41l\x41o\xff"s, "(_ h'48', h'65', h'6c', h'6c', h'6f')",
         "(_ h'48', h'65', h'6c', h'6c', h'6f')", true},
        {"_textstring5x5", "\x7f\x61H\x61""e\x61l\x61l\x61o\xff"s, "(_ \"H\", \"e\", \"l\", \"l\", \"o\")",
         "(_ \"H\", \"e\", \"l\", \"l\", \"o\")", true},
        {"_bytestring5x6", "\x5f\x41H\x41""e\x40\x41l\x41l\x41o\xff"s, "(_ h'48', h'65', h'', h'6c', h'6c', h'6f')",
         "(_ h'48', h'65', h'', h'6c', h'6c', h'6f')", true},
        {"_textstring5x6", "\x7f\x61H\x61""e\x61l\x60\x61l\x61o\xff"s, "(_ \"H\", \"e\", \"l\", \"\", \"l\", \"o\")",
         "(_ \"H\", \"e\", \"l\", \"\", \"l\", \"o\")", true},
    };
}

// addTagsData(). The two tags on a float carry the float in the form of RFC 8949 Appendix A, "1(1363896240.5)".
std::vector<row> tag_rows()
{
    return {
        {"tag0", "\xc0\x00"s, "0(0)", "0(0)", false},
        {"tag1", "\xc1\x00"s, "1(0)", "1(0)", false},
        {"tag24", "\xd8\x18\x00"s, "24(0)", "24(0)", false},
        {"tag255", "\xd8\xff\x00"s, "255(0)", "255(0)", false},
        {"tag256", "\xd9\1\0\x00"s, "256(0)", "256(0)", false},
        {"tag65535", "\xd9\xff\xff\x00"s, "65535(0)", "65535(0)", false},
        {"tag65536", "\xda\0\1\0\0\x00"s, "65536(0)", "65536(0)", false},
        {"tagUINT32_MAX-1", "\xda\xff\xff\xff\xff\x00"s, "4294967295(0)", "4294967295(0)", false},
        {"tagUINT32_MAX", "\xdb\0\0\0\1\0\0\0\0\x00"s, "4294967296(0)", "4294967296(0)", false},
        {"tagUINT64_MAX", "\xdb" "\xff\xff\xff\xff" "\xff\xff\xff\xff" "\x00"s, "18446744073709551615(0)",
         "18446744073709551615(0)", false},
        {"tag0*1", "\xd8\0\x00"s, "0_0(0)", "0_0(0)", false},
        {"tag0*2", "\xd9\0\0\x00"s, "0_1(0)", "0_1(0)", false},
        {"tag0*4", "\xda\0\0\0\0\x00"s, "0_2(0)", "0_2(0)", false},
        {"tag0*8", "\xdb\0\0\0\0\0\0\0\0\x00"s, "0_3(0)", "0_3(0)", false},
        {"unixtime", "\xc1\x1a\x55\x4b\xbf\xd3"s, "1(1431027667)", "1(1431027667)", false},
        {"rfc3339date", "\xc0\x78\x19" "2015-05-07 12:41:07-07:00"s, "0(\"2015-05-07 12:41:07-07:00\")",
         "0(\"2015-05-07 12:41:07-07:00\")", false},
        {"tag6+false", "\xc6\xf4"s, "6(false)", "6(false)", false},
        {"tag25+true", "\xd8\x19\xf5"s, "25(true)", "25(true)", false},
        {"tag256+null", "\xd9\1\0\xf6"s, "256(null)", "256(null)", false},
        {"tag65536+simple32", "\xda\0\1\0\0\xf8\x20"s, "65536(simple(32))", "65536(simple(32))", false},
        {"float+unixtime", "\xc1\xfa\x4e\xaa\x97\x80"s, "1(1431027712.f)", "1(1431027712.0)", false},
        {"double+unixtime", "\xc1\xfb" "\x41\xd5\x52\xef" "\xf4\xc7\xce\xfe"s, "1(1431027667.1220088)",
         "1(1431027667.1220088)", false},
    };
}

std::vector<row> empty_container_rows()
{
    return {
        {"emptyarray", "\x80"s, "[]", "[]", false},
        {"emptymap", "\xa0"s, "{}", "{}", false},
        {"_emptyarray", "\x9f\xff"s, "[_ ]", "[_ ]", true},
        {"_emptymap", "\xbf\xff"s, "{_ }", "{_ }", true},
    };
}

std::vector<row> map_mixed_rows()
{
    return {
        {"map-0-24", "\xa1\0\x18\x18"s, "{0: 24}", "{0: 24}", false},
        {"map-0*1-24", "\xa1\x18\0\x18\x18"s, "{0_0: 24}", "{0_0: 24}", false},
        {"map-0*1-24*2", "\xa1\x18\0\x19\0\x18"s, "{0_0: 24_1}", "{0_0: 24_1}", false},
        {"map-0*4-24*2", "\xa1\x1a\0\0\0\0\x19\0\x18"s, "{0_2: 24_1}", "{0_2: 24_1}", false},
        {"map-24-0", "\xa1\x18\x18\0"s, "{24: 0}", "{24: 0}", false},
        {"map-24-0*1", "\xa1\x18\x18\x18\0"s, "{24: 0_0}", "{24: 0_0}", false},
        {"map-255-65535", "\xa1\x18\xff\x19\xff\xff"s, "{255: 65535}", "{255: 65535}", false},
        {"_map-0-24", "\xbf\0\x18\x18\xff"s, "{_ 0: 24}", "{_ 0: 24}", true},
        {"_map-0*1-24", "\xbf\x18\0\x18\x18\xff"s, "{_ 0_0: 24}", "{_ 0_0: 24}", true},
        {"_map-0*1-24*2", "\xbf\x18\0\x19\0\x18\xff"s, "{_ 0_0: 24_1}", "{_ 0_0: 24_1}", true},
        {"_map-0*4-24*2", "\xbf\x1a\0\0\0\0\x19\0\x18\xff"s, "{_ 0_2: 24_1}", "{_ 0_2: 24_1}", true},
        {"_map-24-0", "\xbf\x18\x18\0\xff"s, "{_ 24: 0}", "{_ 24: 0}", true},
        {"_map-24-0*1", "\xbf\x18\x18\x18\0\xff"s, "{_ 24: 0_0}", "{_ 24: 0_0}", true},
        {"_map-255-65535", "\xbf\x18\xff\x19\xff\xff\xff"s, "{_ 255: 65535}", "{_ 255: 65535}", true},
    };
}

void append_elements(std::vector<element> &to, std::vector<row> const &rows)
{
    for (row const &r : rows)
        to.push_back({std::string(r.name), r.data, r.expected, r.indefinite});
}

// A float takes the text that the library gives for it. The test case "parser fixed: floats" checks that text by
// value and by indicator, so a composition checks only the arrays and maps around it.
void append_elements(std::vector<element> &to, std::vector<float_row> const &rows)
{
    for (float_row const &r : rows)
        to.push_back({std::string(r.name), r.data, printed(r.data), false});
}

// arrays_data() of upstream: addFixedData(), addStringsData() and addTagsData().
std::vector<element> arrays_data()
{
    std::vector<element> all;
    append_elements(all, fixed_rows());
    append_elements(all, float_rows());
    append_elements(all, string_rows());
    append_elements(all, tag_rows());
    return all;
}

// compareOne() of upstream: the text of the item, and that the item ends at the end of the bytes. Upstream checks the
// end with cbor_value_validate_basic() and cbor_value_get_next_byte(); cbor::item_size() is the same check, but
// does not read indefinite length.
void check_one(std::string const &data, std::string const &expected, bool const indefinite)
{
    CAPTURE(expected);
    CHECK_EQ(printed(data), expected);
    auto const size = cbor::item_size(data);
    if (indefinite) {
        CHECK_EQ(outcome(size), error_text(error::indefinite_length));
    } else {
        CHECK_EQ(outcome(size), error_text(no_error));
        if (size)
            CHECK_EQ(*size, data.size());
    }
}

void check_rows(std::vector<row> const &rows)
{
    for (row const &r : rows) {
        CAPTURE(r.name);
        CAPTURE(r.upstream);
        check_one(r.data, r.expected, r.indefinite);
    }
}

}

using namespace tinycbor_parser;

// initParserEmpty: an empty buffer holds no item. RFC 8949 Appendix F: the input ends before the item.
TEST_CASE("tinycbor parser: an empty buffer is too little data")
{
    CHECK_EQ(outcome(cbor::decode(""sv)), error_text(error::too_little_data));
    CHECK_EQ(outcome(cbor::item_size(""sv)), error_text(error::too_little_data));
    CHECK_EQ(outcome(cbor::diagnostic_notation(""sv)), error_text(error::too_little_data));
}

// integers: the raw argument, the sign, and the value as std::int64_t and as int. Upstream reads the value with
// cbor_value_get_int64_checked() and cbor_value_get_int_checked(), which give CborErrorDataTooLarge where the value
// does not fit; the port reads with cbor::at_path(), which gives number_out_of_range.
TEST_CASE("tinycbor parser: integers")
{
    struct integer_row {
        std::string_view name;
        std::string data;
        std::uint64_t raw;
        std::int64_t value;
        bool negative;
        bool in_int64;
    };
    std::int64_t const min = std::numeric_limits<std::int64_t>::min();
    std::int64_t const max = std::numeric_limits<std::int64_t>::max();
    std::uint64_t const umax = std::numeric_limits<std::uint64_t>::max();
    integer_row const rows[] = {
        {"0", "\x00"s, 0, 0, false, true},
        {"1", "\x01"s, 1, 1, false, true},
        {"10", "\x0a"s, 10, 10, false, true},
        {"23", "\x17"s, 23, 23, false, true},
        {"24", "\x18\x18"s, 24, 24, false, true},
        {"UINT8_MAX", "\x18\xff"s, 255, 255, false, true},
        {"UINT8_MAX+1", "\x19\x01\x00"s, 256, 256, false, true},
        {"UINT16_MAX", "\x19\xff\xff"s, 65535, 65535, false, true},
        {"UINT16_MAX+1", "\x1a\0\1\x00\x00"s, 65536, 65536, false, true},
        {"UINT32_MAX", "\x1a\xff\xff\xff\xff"s, 4294967295, 4294967295, false, true},
        {"UINT32_MAX+1", "\x1b\0\0\0\1\0\0\0\0"s, 4294967296, 4294967296, false, true},
        {"INT64_MAX", "\x1b" "\x7f\xff\xff\xff" "\xff\xff\xff\xff"s, std::uint64_t{max}, max, false, true},
        {"UINT64_MAX", "\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s, umax, 0, false, false},
        {"-1", "\x20"s, 0, -1, true, true},
        {"-2", "\x21"s, 1, -2, true, true},
        {"-24", "\x37"s, 23, -24, true, true},
        {"-25", "\x38\x18"s, 24, -25, true, true},
        {"-UINT8_MAX", "\x38\xff"s, 255, -256, true, true},
        {"-UINT8_MAX-1", "\x39\x01\x00"s, 256, -257, true, true},
        {"-UINT16_MAX", "\x39\xff\xff"s, 65535, -65536, true, true},
        {"-UINT16_MAX-1", "\x3a\0\1\x00\x00"s, 65536, -65537, true, true},
        {"-UINT32_MAX", "\x3a\xff\xff\xff\xff"s, 4294967295, -4294967296, true, true},
        {"-UINT32_MAX-1", "\x3b\0\0\0\1\0\0\0\0"s, 4294967296, -4294967297, true, true},
        {"INT64_MIN+1", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xfe"s, std::uint64_t{max} - 1, min + 1, true, true},
        {"INT64_MIN", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xff"s, std::uint64_t{max}, min, true, true},
        {"INT64_MIN-1", "\x3b\x80\0\0\0""\0\0\0\0"s, 9223372036854775808u, 0, true, false},
        {"-UINT64_MAX", "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xfe"s, umax - 1, 0, true, false},
        {"-UINT64_MAX+1", "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s, umax, 0, true, false},
    };
    for (integer_row const &r : rows) {
        CAPTURE(r.name);
        auto const tree = cbor::decode(r.data);
        REQUIRE(tree.has_value());
        cbor::item const &node = **tree;
        CHECK_EQ(node.type, r.negative ? cbor::type::negative_integer : cbor::type::unsigned_integer);
        auto const raw = r.negative ? node.get<cbor::type::negative_integer>() : node.get<cbor::type::unsigned_integer>();
        CHECK_EQ(raw.value_or(0), r.raw);
        auto const as_int64 = cbor::at_path<"$", std::int64_t>(r.data);
        auto const as_int = cbor::at_path<"$", int>(r.data);
        bool const in_int = r.in_int64 && r.value >= std::numeric_limits<int>::min() &&
                            r.value <= std::numeric_limits<int>::max();
        if (r.in_int64) {
            CHECK_EQ(outcome(as_int64), error_text(no_error));
            CHECK_EQ(as_int64.value_or(0), r.value);
        } else {
            CHECK_EQ(outcome(as_int64), error_text(error::number_out_of_range));
        }
        if (in_int) {
            CHECK_EQ(outcome(as_int), error_text(no_error));
            CHECK_EQ(as_int.value_or(0), r.value);
        } else {
            CHECK_EQ(outcome(as_int), error_text(error::number_out_of_range));
        }
    }
}

// halfFloat: the value of each half float. Upstream also reads the raw 16 bits; cbor::encode() of the decoded tree
// writes the float in its width (encode.hpp, "A float keeps its width"), so the bytes that come back are the raw bits.
TEST_CASE("tinycbor parser: half floats")
{
    struct half_row {
        std::string_view name;
        std::string data;
        unsigned raw;
        double value;
    };
    double const inf = std::numeric_limits<double>::infinity();
    double const nan = std::numeric_limits<double>::quiet_NaN();
    half_row const rows[] = {
        {"+0", "\x00\x00"s, 0U, 0.0},
        {"-0", "\x80\x00"s, 0x8000U, 0.0},
        {"min.denorm", "\x00\x01"s, 1U, std::ldexp(1.0, -14) * std::ldexp(1.0, -10)},
        {"-min.denorm", "\x80\x01"s, 0x8001U, std::ldexp(-1.0, -14) * std::ldexp(1.0, -10)},
        {"max.denorm", "\x03\xff"s, 0x03ffU, std::ldexp(1.0, -14) * (1.0 - std::ldexp(1.0, -10))},
        {"-max.denorm", "\x83\xff"s, 0x83ffU, std::ldexp(-1.0, -14) * (1.0 - std::ldexp(1.0, -10))},
        {"min.norm", "\x04\x00"s, 0x0400U, std::ldexp(1.0, -14)},
        {"-min.norm", "\x84\x00"s, 0x8400U, std::ldexp(-1.0, -14)},
        {"1.0", "\x3c\x00"s, 0x3c00U, 1.0},
        {"-1.0", "\xbc\x00"s, 0xbc00U, -1.0},
        {"1.5", "\x3e\x00"s, 0x3e00U, 1.5},
        {"-1.5", "\xbe\x00"s, 0xbe00U, -1.5},
        {"max", "\x7b\xff"s, 0x7bffU, std::ldexp(1.0, 15) * (2.0 - std::ldexp(1.0, -10))},
        {"-max", "\xfb\xff"s, 0xfbffU, std::ldexp(-1.0, 15) * (2.0 - std::ldexp(1.0, -10))},
        {"inf", "\x7c\x00"s, 0x7c00U, inf},
        {"-inf", "\xfc\x00"s, 0xfc00U, -inf},
        {"nan", "\x7c\x01"s, 0x7c01U, nan},
        {"nan2", "\xfc\x01"s, 0xfc01U, nan},
        {"nan3", "\x7e\x00"s, 0x7e00U, nan},
        {"nan4", "\xfe\x00"s, 0xfe00U, nan},
    };
    double const epsilon = std::ldexp(1.0, -25);
    for (half_row const &r : rows) {
        CAPTURE(r.name);
        std::string const data = "\xf9"s + r.data;
        auto const tree = cbor::decode(data);
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type, cbor::type::half_float);
        double const value = (*tree)->get<cbor::type::half_float>().value_or(0);
        if (std::isnan(r.value))
            CHECK(std::isnan(value));
        else if (std::isinf(r.value))
            CHECK_EQ(value, r.value);
        else
            CHECK_LT(std::fabs(value - r.value), epsilon);
        std::string out;
        CHECK_EQ(outcome(cbor::encode(**tree, out)), error_text(no_error));
        CHECK_EQ(out, data);
    }
}

// floatingPoint: the type and the value of each float.
TEST_CASE("tinycbor parser: floats")
{
    for (float_row const &r : float_rows()) {
        CAPTURE(r.name);
        auto const tree = cbor::decode(r.data);
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type, r.type);
        double value = 0;
        if (r.type == cbor::type::half_float)
            value = (*tree)->get<cbor::type::half_float>().value_or(0);
        else if (r.type == cbor::type::single_float)
            value = (*tree)->get<cbor::type::single_float>().value_or(0);
        else
            value = (*tree)->get<cbor::type::double_float>().value_or(0);
        CHECK_EQ(std::isnan(value), std::isnan(r.value));
        if (!std::isnan(r.value))
            CHECK_EQ(value, r.value);
    }
}

// fixed: the text of each integer and simple value.
TEST_CASE("tinycbor parser fixed: integers and simple values")
{
    check_rows(fixed_rows());
}

// fixed: the text of each float. The rows show why the text differs from the text of upstream in the comment at the
// top of this file.
TEST_CASE("tinycbor parser fixed: floats")
{
    for (float_row const &r : float_rows()) {
        CAPTURE(r.name);
        CAPTURE(r.upstream);
        std::string const text = printed(r.data);
        CAPTURE(text);
        CHECK(tinycbor_port::float_text_holds(text, r.value, r.indicator));
        auto const size = cbor::item_size(r.data);
        CHECK_EQ(size.value_or(0), r.data.size());
    }
}

// strings: definite and indefinite length, overlong heads and UTF-8.
TEST_CASE("tinycbor parser: strings")
{
    check_rows(string_rows());
}

// tags: tag numbers of each width and overlong tags.
TEST_CASE("tinycbor parser: tags")
{
    check_rows(tag_rows());
}

// tagTags: tag 55799 in front of each tag row, once and twice. cbor::diagnostic_notation() prints it as a tag.
TEST_CASE("tinycbor parser: tags under tag 55799")
{
    for (row const &r : tag_rows()) {
        CAPTURE(r.name);
        check_one("\xd9\xd9\xf7"s + r.data, "55799(" + r.expected + ")", false);
        check_one("\xd9\xd9\xf7" "\xd9\xd9\xf7"s + r.data, "55799(55799(" + r.expected + "))", false);
    }
}

// emptyContainers.
TEST_CASE("tinycbor parser: empty containers")
{
    check_rows(empty_container_rows());
}

// arrays: one and two elements, overlong counts, 32 and 256 elements.
TEST_CASE("tinycbor parser: arrays")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        check_one("\x81"s + e.data, "[" + e.text + "]", e.indefinite);
        check_one("\x82"s + e.data + e.data, "[" + e.text + ", " + e.text + "]", e.indefinite);
        check_one("\x98\1"s + e.data, "[_0 " + e.text + "]", e.indefinite);
        check_one("\x99\0\1"s + e.data, "[_1 " + e.text + "]", e.indefinite);
        check_one("\x9a\0\0\0\1"s + e.data, "[_2 " + e.text + "]", e.indefinite);
        check_one("\x9b\0\0\0\0\0\0\0\1"s + e.data, "[_3 " + e.text + "]", e.indefinite);
        std::string data = e.data;
        std::string text = e.text + ", ";
        for (int i = 0; i < 5; ++i) {
            data += data;
            text += text;
        }
        text.resize(text.size() - 2);
        check_one("\x98\x20"s + data, "[" + text + "]", e.indefinite);
        text += ", ";
        for (int i = 0; i < 3; ++i) {
            data += data;
            text += text;
        }
        text.resize(text.size() - 2);
        check_one("\x99\1\0"s + data, "[" + text + "]", e.indefinite);
    }
}

// undefLengthArrays.
TEST_CASE("tinycbor parser: indefinite-length arrays")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        check_one("\x9f"s + e.data + "\xff", "[_ " + e.text + "]", true);
        check_one("\x9f"s + e.data + e.data + "\xff", "[_ " + e.text + ", " + e.text + "]", true);
    }
}

// nestedArrays.
TEST_CASE("tinycbor parser: nested arrays")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        std::string const &d = e.data;
        std::string const &x = e.text;
        bool const i = e.indefinite;
        check_one("\x81\x81"s + d, "[[" + x + "]]", i);
        check_one("\x81\x81\x81"s + d, "[[[" + x + "]]]", i);
        check_one("\x81\x82"s + d + d, "[[" + x + ", " + x + "]]", i);
        check_one("\x82\x81"s + d + d, "[[" + x + "], " + x + "]", i);
        check_one("\x82\x81"s + d + "\x81" + d, "[[" + x + "], [" + x + "]]", i);
        check_one("\x9f\x9f"s + d + d + "\xff\xff", "[_ [_ " + x + ", " + x + "]]", true);
        check_one("\x9f\x9f"s + d + "\xff\x9f" + d + "\xff\xff", "[_ [_ " + x + "], [_ " + x + "]]", true);
        check_one("\x9f\x9f"s + d + d + "\xff\x9f" + d + "\xff\xff",
                  "[_ [_ " + x + ", " + x + "], [_ " + x + "]]", true);
        check_one("\x81\x9f"s + d + "\xff", "[[_ " + x + "]]", true);
        check_one("\x9f\x81"s + d + "\xff", "[_ [" + x + "]]", true);
    }
}

// maps.
TEST_CASE("tinycbor parser: maps")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        std::string const &d = e.data;
        std::string const &x = e.text;
        bool const i = e.indefinite;
        check_one("\xa1\1"s + d, "{1: " + x + "}", i);
        check_one("\xa1\x65" "Hello"s + d, "{\"Hello\": " + x + "}", i);
        check_one("\xa1"s + d + d, "{" + x + ": " + x + "}", i);
        check_one("\xa2\1"s + d + "\2" + d, "{1: " + x + ", 2: " + x + "}", i);
        check_one("\xa2\1"s + d + "\x65" "Hello" + d, "{1: " + x + ", \"Hello\": " + x + "}", i);
    }
}

// undefLengthMaps.
TEST_CASE("tinycbor parser: indefinite-length maps")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        std::string const &d = e.data;
        std::string const &x = e.text;
        check_one("\xbf\1"s + d + "\xff", "{_ 1: " + x + "}", true);
        check_one("\xbf\1"s + d + "\2" + d + "\xff", "{_ 1: " + x + ", 2: " + x + "}", true);
        check_one("\xbf\1"s + d + "\x65Hello" + d + "\xff", "{_ 1: " + x + ", \"Hello\": " + x + "}", true);
        check_one("\xbf\x65Hello"s + d + "\1" + d + "\xff", "{_ \"Hello\": " + x + ", 1: " + x + "}", true);
    }
}

// nestedMaps.
TEST_CASE("tinycbor parser: nested maps")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        std::string const &d = e.data;
        std::string const &x = e.text;
        bool const i = e.indefinite;
        check_one("\xa1\1\xa1\2"s + d, "{1: {2: " + x + "}}", i);
        check_one("\xa1\x65Hello\xa1\2"s + d, "{\"Hello\": {2: " + x + "}}", i);
        check_one("\xa1\1\xa2\2"s + d + "\x20" + d, "{1: {2: " + x + ", -1: " + x + "}}", i);
        check_one("\xa2\1\xa1\2"s + d + "\2\xa1\x20" + d, "{1: {2: " + x + "}, 2: {-1: " + x + "}}", i);
        check_one("\xa1\xa1\xf4"s + d + "\xf5", "{{false: " + x + "}: true}", i);
        check_one("\xa1\xa1"s + d + d + "\xa1" + d + d, "{{" + x + ": " + x + "}: {" + x + ": " + x + "}}", i);
        check_one("\xbf\1\xbf\2"s + d + "\xff\xff", "{_ 1: {_ 2: " + x + "}}", true);
        check_one("\xbf\1\xbf\2"s + d + "\x20" + d + "\xff\xff", "{_ 1: {_ 2: " + x + ", -1: " + x + "}}", true);
        check_one("\xbf\1\xbf\2"s + d + "\xff\2\xbf\x20" + d + "\xff\xff",
                  "{_ 1: {_ 2: " + x + "}, 2: {_ -1: " + x + "}}", true);
        check_one("\xbf\xbf"s + d + d + "\xff\xbf" + d + d + "\xff\xff",
                  "{_ {_ " + x + ": " + x + "}: {_ " + x + ": " + x + "}}", true);
        check_one("\xa1\1\xbf\2"s + d + "\xff", "{1: {_ 2: " + x + "}}", true);
        check_one("\xbf\1\xa1\2"s + d + "\xff", "{_ 1: {2: " + x + "}}", true);
    }
}

// mapMixed: arrays() of upstream over the rows of addMapMixedData().
TEST_CASE("tinycbor parser: maps with overlong keys and values in arrays")
{
    std::vector<element> mixed;
    append_elements(mixed, map_mixed_rows());
    for (element const &e : mixed) {
        CAPTURE(e.name);
        check_one(e.data, e.text, e.indefinite);
        check_one("\x81"s + e.data, "[" + e.text + "]", e.indefinite);
        check_one("\x82"s + e.data + e.data, "[" + e.text + ", " + e.text + "]", e.indefinite);
        check_one("\x98\1"s + e.data, "[_0 " + e.text + "]", e.indefinite);
        check_one("\x99\0\1"s + e.data, "[_1 " + e.text + "]", e.indefinite);
        check_one("\x9a\0\0\0\1"s + e.data, "[_2 " + e.text + "]", e.indefinite);
        check_one("\x9b\0\0\0\0\0\0\0\1"s + e.data, "[_3 " + e.text + "]", e.indefinite);
    }
}

// mapsAndArrays.
TEST_CASE("tinycbor parser: maps in arrays and arrays in maps")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        std::string const &d = e.data;
        std::string const &x = e.text;
        bool const i = e.indefinite;
        check_one("\x81\xa1\1"s + d, "[{1: " + x + "}]", i);
        check_one("\x82\xa1\1"s + d + "\xa1\2" + d, "[{1: " + x + "}, {2: " + x + "}]", i);
        check_one("\x81\xa2\1"s + d + "\2" + d, "[{1: " + x + ", 2: " + x + "}]", i);
        check_one("\x9f\xa1\1"s + d + "\xff", "[_ {1: " + x + "}]", true);
        check_one("\x81\xbf\1"s + d + "\xff", "[{_ 1: " + x + "}]", true);
        check_one("\x9f\xbf\1"s + d + "\xff\xff", "[_ {_ 1: " + x + "}]", true);
        check_one("\xa1\1\x81"s + d, "{1: [" + x + "]}", i);
        check_one("\xa1\1\x82"s + d + d, "{1: [" + x + ", " + x + "]}", i);
        check_one("\xa2\1\x81"s + d + "\x65Hello\x81" + d, "{1: [" + x + "], \"Hello\": [" + x + "]}", i);
        check_one("\xa1\1\x9f"s + d + "\xff", "{1: [_ " + x + "]}", true);
        check_one("\xa1\1\x9f"s + d + d + "\xff", "{1: [_ " + x + ", " + x + "]}", true);
        check_one("\xbf\1\x81"s + d + "\xff", "{_ 1: [" + x + "]}", true);
        check_one("\xbf\1\x9f"s + d + "\xff\xff", "{_ 1: [_ " + x + "]}", true);
        check_one("\xbf\1\x9f"s + d + d + "\xff\xff", "{_ 1: [_ " + x + ", " + x + "]}", true);
        check_one("\xbf\1\x9f"s + d + "\xff\x65Hello\xbf" + d + "\x7f\xff\xff\xff",
                  "{_ 1: [_ " + x + "], \"Hello\": {_ " + x + ": \"\"_}}", true);
    }
}

// readerApi: upstream reads each item through its reader interface. cbor::sequence_reader is the reader of the
// library: it gets all the bytes and gives the item, and nothing stays.
TEST_CASE("tinycbor parser: an item through cbor::sequence_reader")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        cbor::sequence_reader reader;
        reader.feed(std::string(e.data));
        auto const item = reader.next();
        if (e.indefinite) {
            CHECK_EQ(outcome(item), error_text(error::indefinite_length));
            continue;
        }
        REQUIRE_EQ(outcome(item), error_text(no_error));
        CHECK_EQ(printed(item->top_level.encoded.substr(item->offset)), e.text);
        CHECK(reader.empty());
    }
}

// reparse: upstream gives the reader one more byte at each step; each step before the last is too little data, and
// no byte is taken.
TEST_CASE("tinycbor parser: an item that arrives one byte at a time")
{
    std::vector<element> fixed;
    append_elements(fixed, fixed_rows());
    append_elements(fixed, float_rows());
    for (element const &e : fixed) {
        CAPTURE(e.name);
        cbor::sequence_reader reader;
        for (std::size_t i = 0; i + 1 < e.data.size(); ++i) {
            reader.feed(e.data.substr(i, 1));
            CHECK_EQ(outcome(reader.next()), error_text(error::too_little_data));
        }
        reader.feed(e.data.substr(e.data.size() - 1));
        auto const item = reader.next();
        REQUIRE_EQ(outcome(item), error_text(no_error));
        CHECK_EQ(printed(item->top_level.encoded.substr(item->offset)), e.text);
        CHECK(reader.empty());
    }
}

// chunkedString and chunkedStringInUndefArray: an array of the string and an empty string of the same type. The text
// shows each chunk. Upstream also reads the string as one value. The library has no call that reads an
// indefinite-length string (see the top of this file), so the port reads the value of a definite string only.
TEST_CASE("tinycbor parser: chunks of a string")
{
    struct chunk_row {
        std::string_view name;
        std::string data;
        std::string concatenated;
        std::vector<std::string_view> chunks;
    };
    chunk_row const rows[] = {
        {"emptybytestring", "\x40"s, "h''", {"h''"}},
        {"bytestring1", "\x41 "s, "h'20'", {"h'20'"}},
        {"emptytextstring", "\x60"s, "\"\"", {"\"\""}},
        {"textstring1", "\x61 "s, "\" \"", {"\" \""}},
        {"_emptybytestring", "\x5f\xff"s, "h''", {}},
        {"_emptytextstring", "\x7f\xff"s, "\"\"", {}},
        {"_emptybytestring2", "\x5f\x40\xff"s, "h''", {"h''"}},
        {"_emptytextstring2", "\x7f\x60\xff"s, "\"\"", {"\"\""}},
        {"_emptybytestring3", "\x5f\x40\x40\xff"s, "h''", {"h''", "h''"}},
        {"_emptytextstring3", "\x7f\x60\x60\xff"s, "\"\"", {"\"\"", "\"\""}},
        {"_bytestring1", "\x5f\x41 \xff"s, "h'20'", {"h'20'"}},
        {"_bytestring2", "\x5f\x41 \x41z\xff"s, "h'207a'", {"h'20'", "h'7a'"}},
        {"_bytestring3", "\x5f\x41 \x58\x18""123456789012345678901234\x41z\xff"s,
         "h'203132333435363738393031323334353637383930313233347a'",
         {"h'20'", "h'313233343536373839303132333435363738393031323334'", "h'7a'"}},
        {"_textstring1", "\x7f\x61 \xff"s, "\" \"", {"\" \""}},
        {"_textstring2", "\x7f\x61 \x61z\xff"s, "\" z\"", {"\" \"", "\"z\""}},
        {"_textstring3", "\x7f\x61 \x78\x18""123456789012345678901234\x61z\xff"s, "\" 123456789012345678901234z\"",
         {"\" \"", "\"123456789012345678901234\"", "\"z\""}},
    };
    for (chunk_row const &r : rows) {
        CAPTURE(r.name);
        bool const text = (static_cast<unsigned char>(r.data.front()) & 0xe0) == 0x60;
        bool const chunked = (static_cast<unsigned char>(r.data.front()) & 0x1f) == 0x1f;
        std::string const empty = text ? "\"\"" : "h''";
        std::string shown;
        if (!chunked) {
            shown = std::string(r.chunks.front());
        } else if (r.chunks.empty()) {
            shown = text ? "\"\"_" : "''_";
        } else {
            shown = "(_ ";
            for (std::string_view const c : r.chunks)
                shown += std::string(c) + ", ";
            shown.resize(shown.size() - 2);
            shown += ")";
        }
        std::string const type = text ? "\x60"s : "\x40"s;
        check_one("\x82"s + r.data + type, "[" + shown + ", " + empty + "]", chunked);
        check_one("\x9f"s + r.data + type + "\xff", "[_ " + shown + ", " + empty + "]", true);
        if (!chunked)
            CHECK_EQ(printed(r.data), r.concatenated);
    }
}

// stringLength: the length of each string. A definite string gives it through cbor::decode(); an indefinite string
// gives indefinite_length (decode.hpp, "@retval indefinite_length a string, an array or a map has an indefinite
// length").
TEST_CASE("tinycbor parser: string length")
{
    struct length_row {
        std::string_view name;
        std::string data;
        std::size_t expected;
    };
    length_row const rows[] = {
        {"emptybytestring", "\x40"s, 0},
        {"bytestring1", "\x41 "s, 1},
        {"bytestring1-nul", "\x41\0"s, 1},
        {"bytestring5", "\x45Hello"s, 5},
        {"bytestring24", "\x58\x18""123456789012345678901234"s, 24},
        {"bytestring256", "\x59\1\0"s + std::string(256, '3'), 256},
        {"emptytextstring", "\x60"s, 0},
        {"textstring1", "\x61 "s, 1},
        {"textstring1-nul", "\x61\0"s, 1},
        {"textstring5", "\x65Hello"s, 5},
        {"textstring24", "\x78\x18""123456789012345678901234"s, 24},
        {"textstring256", "\x79\1\0"s + std::string(256, '3'), 256},
        {"emptybytestring*1", "\x58\x00"s, 0},
        {"emptytextstring*1", "\x78\x00"s, 0},
        {"emptybytestring*2", "\x59\x00\x00"s, 0},
        {"emptytextstring*2", "\x79\x00\x00"s, 0},
        {"emptybytestring*4", "\x5a\0\0\0\0"s, 0},
        {"emptytextstring*4", "\x7a\0\0\0\0"s, 0},
        {"emptybytestring*8", "\x5b\0\0\0\0\0\0\0\0"s, 0},
        {"emptytextstring*8", "\x7b\0\0\0\0\0\0\0\0"s, 0},
        {"bytestring5*1", "\x58\x05Hello"s, 5},
        {"textstring5*1", "\x78\x05Hello"s, 5},
        {"bytestring5*2", "\x59\0\5Hello"s, 5},
        {"textstring5*2", "\x79\0\x05Hello"s, 5},
        {"bytestring5*4", "\x5a\0\0\0\5Hello"s, 5},
        {"textstring5*4", "\x7a\0\0\0\x05Hello"s, 5},
        {"bytestring5*8", "\x5b\0\0\0\0\0\0\0\5Hello"s, 5},
        {"textstring5*8", "\x7b\0\0\0\0\0\0\0\x05Hello"s, 5},
        {"_emptybytestring", "\x5f\xff"s, 0},
        {"_emptytextstring", "\x7f\xff"s, 0},
        {"_emptybytestring2", "\x5f\x40\xff"s, 0},
        {"_emptytextstring2", "\x7f\x60\xff"s, 0},
        {"_emptybytestring3", "\x5f\x40\x40\xff"s, 0},
        {"_emptytextstring3", "\x7f\x60\x60\xff"s, 0},
        {"_bytestring5*2", "\x5f\x43Hel\x42lo\xff"s, 5},
        {"_textstring5*2", "\x7f\x63Hel\x62lo\xff"s, 5},
        {"_bytestring5*5", "\x5f\x41H\x41""e\x41l\x41l\x41o\xff"s, 5},
        {"_textstring5*5", "\x7f\x61H\x61""e\x61l\x61l\x61o\xff"s, 5},
        {"_bytestring5*6", "\x5f\x41H\x41""e\x40\x41l\x41l\x41o\xff"s, 5},
        {"_textstring5*6", "\x7f\x61H\x61""e\x61l\x60\x61l\x61o\xff"s, 5},
    };
    for (length_row const &r : rows) {
        CAPTURE(r.name);
        auto const tree = cbor::decode(r.data);
        if (r.name.starts_with("_")) {
            CHECK_EQ(outcome(tree), error_text(error::indefinite_length));
            continue;
        }
        REQUIRE_EQ(outcome(tree), error_text(no_error));
        cbor::item const &node = **tree;
        if (node.type == cbor::type::text_string)
            CHECK_EQ(node.get<cbor::type::text_string>()->size(), r.expected);
        else
            CHECK_EQ(node.get<cbor::type::byte_string>()->size(), r.expected);
    }
}

// stringCompare: whether the item is the text string. cbor::lazy::get<std::string_view>() reads the text and passes a
// tag that it does not interpret (lazy.hpp), so tag 1 in front changes nothing.
// Two kinds of row differ from upstream:
// - an indefinite-length text gives indefinite_length (the top of this file), so it is not equal;
// - under "\xc1\xc2", tag 2 holds a text string. lazy passes a tag that it does not interpret, and it interprets
//   tag 2 (lazy.hpp; heads::is_tag_passed leaves out tags 2, 3 and 24), so the read meets tag 2 and gives
//   incorrect_type. Upstream skips the tag and finds the text equal.
TEST_CASE("tinycbor parser: compare with a text string")
{
    struct compare_row {
        std::string_view name;
        std::string data;
        std::string_view string;
        bool expected;
    };
    compare_row const rows[] = {
        {"empty-empty", "\x60"s, "", true},
        {"_empty-empty", "\x7f\xff"s, "", true},
        {"_empty*1-empty", "\x7f\x60\xff"s, "", true},
        {"_empty*2-empty", "\x7f\x60\x60\xff"s, "", true},
        {"empty-nonempty", "\x60"s, "Hello", false},
        {"_empty-nonempty", "\x7f\xff"s, "Hello", false},
        {"_empty*1-nonempty", "\x7f\x60\xff"s, "Hello", false},
        {"_empty*2-nonempty", "\x7f\x60\x60\xff"s, "Hello", false},
        {"same-short-short", "\x65Hello"s, "Hello", true},
        {"same-_short*1-short", "\x7f\x65Hello\xff"s, "Hello", true},
        {"same-_short*2-short", "\x7f\x63Hel\x62lo\xff"s, "Hello", true},
        {"same-_short*5-short", "\x7f\x61H\x61""e\x61l\x61l\x61o\xff"s, "Hello", true},
        {"same-_short*8-short", "\x7f\x61H\x60\x61""e\x60\x61l\x61l\x60\x61o\xff"s, "Hello", true},
        {"same-long-long", "\x78\x2aGood morning, good afternoon and goodnight"s,
         "Good morning, good afternoon and goodnight", true},
        {"same-_long*1-long", "\x7f\x78\x2aGood morning, good afternoon and goodnight\xff"s,
         "Good morning, good afternoon and goodnight", true},
        {"same-_long*2-long", "\x7f\x78\x1cGood morning, good afternoon\x6e and goodnight\xff"s,
         "Good morning, good afternoon and goodnight", true},
        {"diff-same-length-short-short", "\x65Hello"s, "World", false},
        {"diff-same-length-_short*1-short", "\x7f\x65Hello\xff"s, "World", false},
        {"diff-same-length-_short*2-short", "\x7f\x63Hel\x62lo\xff"s, "World", false},
        {"diff-same-length-_short*5-short", "\x7f\x61H\x61""e\x61l\x61l\x61o\xff"s, "World", false},
        {"diff-same-length-_short*8-short", "\x7f\x61H\x60\x61""e\x60\x61l\x61l\x60\x61o\xff"s, "World", false},
        {"diff-same-length-long-long", "\x78\x2aGood morning, good afternoon and goodnight"s,
         "Good morning, good afternoon and goodnight, world", false},
        {"diff-same-length-_long*1-long", "\x7f\x78\x2aGood morning, good afternoon and goodnight\xff"s,
         "Good morning, good afternoon and goodnight, world", false},
        {"diff-same-length-_long*2-long", "\x7f\x78\x1cGood morning, good afternoon\x6e and goodnight\xff"s,
         "Good morning, good afternoon and goodnight, world", false},
        {"diff-diff-length-short-short", "\x65Hello"s, "Hello World", false},
        {"diff-diff-length-_short*1-short", "\x7f\x65Hello\xff"s, "Hello World", false},
        {"diff-diff-length-_short*2-short", "\x7f\x63Hel\x62lo\xff"s, "Hello World", false},
        {"diff-diff-length-_short*5-short", "\x7f\x61H\x61""e\x61l\x61l\x61o\xff"s, "Hello World", false},
        {"diff-diff-length-_short*8-short", "\x7f\x61H\x60\x61""e\x60\x61l\x61l\x60\x61o\xff"s, "Hello World", false},
        {"diff-diff-length-long-long", "\x78\x2aGood morning, good afternoon and goodnight"s,
         "Good morning, good afternoon and goodnight World", false},
        {"diff-diff-length-_long*1-long", "\x7f\x78\x2aGood morning, good afternoon and goodnight\xff"s,
         "Good morning, good afternoon and goodnight World", false},
        {"diff-diff-length-_long*2-long", "\x7f\x78\x1cGood morning, good afternoon\x6e and goodnight\xff"s,
         "Good morning, good afternoon and goodnight World", false},
        {"unsigned", "\0"s, "0", false},
        {"negative", "\x20"s, "-1", false},
        {"emptybytestring", "\x40"s, "", false},
        {"_emptybytestring", "\x5f\xff"s, "", false},
        {"shortbytestring", "\x45Hello"s, "Hello", false},
        {"longbytestring", "\x58\x2aGood morning, good afternoon and goodnight"s,
         "Good morning, good afternoon and goodnight", false},
        {"emptyarray", "\x80"s, "", false},
        {"emptymap", "\xa0"s, "", false},
        {"array", "\x81\x65Hello"s, "Hello", false},
        {"map", "\xa1\x65Hello\x65World"s, "Hello World", false},
        {"false", "\xf4"s, "false", false},
        {"true", "\xf5"s, "true", false},
        {"null", "\xf6"s, "null", false},
    };
    for (compare_row const &r : rows) {
        CAPTURE(r.name);
        bool const indefinite_text = static_cast<unsigned char>(r.data.front()) == 0x7f;
        std::string const prefixes[] = {""s, "\xc1"s, "\xc1\xc2"s};
        for (std::string const &prefix : prefixes) {
            CAPTURE(prefix.size());
            auto const doc = cbor::lazy::adopt(prefix + r.data);
            REQUIRE(doc.has_value());
            auto const text = doc->get<std::string_view>();
            bool const equal = text.has_value() && **text == r.string;
            if (indefinite_text && prefix.size() < 2) {
                CHECK_EQ(outcome(text), error_text(error::indefinite_length));
                CHECK_FALSE(equal);
            } else if (prefix.size() == 2) {
                CHECK_EQ(outcome(text), error_text(error::incorrect_type));
                CHECK_FALSE(equal);
            } else {
                CHECK_EQ(equal, r.expected);
            }
        }
    }
}

// mapFind: the value of the key "needle", which is "haystack" under tag 42. cbor::lazy::at() finds the key, and
// cbor::lazy::get<std::string_view>() passes the tag. Three kinds of row differ from upstream:
// - a key under a tag ("tagged", "doubletagged", "tagged-1before", "doubletagged-1before2"): upstream skips the tag of
//   a key. In the data model of RFC 8949 (section 2), a tagged item is another item than its content, so the key is
//   not the text "needle"; the port expects key_not_found;
// - an indefinite-length key, value or map gives indefinite_length (the top of this file);
// - "absent-string-map2" holds bytes after the item ("\61z\62yx" in upstream is octal, not hexadecimal); a step does
//   not read them (lazy.hpp, "No step reads bytes after the item").
TEST_CASE("tinycbor parser: find a key in a map")
{
    struct find_row {
        std::string_view name;
        std::string data;
        bool expected;
        error port;
    };
    error const nf = error::key_not_found;
    error const il = error::indefinite_length;
    find_row const rows[] = {
        {"emptymap", "\xa0"s, false, nf},
        {"_emptymap", "\xbf\xff"s, false, il},
        {"absent-unsigned-unsigned", "\xa1\0\0"s, false, nf},
        {"absent-taggedunsigned-unsigned", "\xa1\xc0\0\0"s, false, nf},
        {"absent-unsigned-taggedunsigned", "\xa1\0\xc0\0"s, false, nf},
        {"absent-taggedunsigned-taggedunsigned", "\xa1\xc0\0\xc0\0"s, false, nf},
        {"absent-string-unsigned", "\xa1\x68haystack\0"s, false, nf},
        {"absent-taggedstring-unsigned", "\xa1\xc0\x68haystack\0"s, false, nf},
        {"absent-string-taggedunsigned", "\xa1\x68haystack\xc0\0"s, false, nf},
        {"absent-taggedstring-taggedunsigned", "\xa1\xc0\x68haystack\xc0\0"s, false, nf},
        {"absent-string-string", "\xa1\x68haystack\x66needle"s, false, nf},
        {"absent-string-taggedstring", "\xa1\x68haystack\xc0\x66needle"s, false, nf},
        {"absent-taggedstring-string", "\xa1\xc0\x68haystack\x66needle"s, false, nf},
        {"absent-string-taggedstring", "\xa1\xc0\x68haystack\xc0\x66needle"s, false, nf},
        {"absent-string-emptyarray", "\xa1\x68haystack\x80"s, false, nf},
        {"absent-string-_emptyarray", "\xa1\x68haystack\x9f\xff"s, false, il},
        {"absent-string-array1", "\xa1\x68haystack\x81\0"s, false, nf},
        {"absent-string-array2", "\xa1\x68haystack\x85\0\1\2\3\4"s, false, nf},
        {"absent-string-array3", "\xa1\x68haystack\x85\x63one\x63two\x65three\x64""four\x64""five"s, false, nf},
        {"absent-string-emptymap", "\xa1\x68haystack\xa0"s, false, nf},
        {"absent-string-_emptymap", "\xa1\x68haystack\xbf\xff"s, false, il},
        {"absent-string-map", "\xa1\x68haystack\xa1\x68haystack\x66needle"s, false, nf},
        {"absent-string-map2", "\xa1\x68haystack\xa1\x68haystack\x66needle\61z\62yx"s, false, nf},
        {"alone", "\xa1\x66needle\xd8\x2a\x68haystack"s, true, no_error},
        {"tagged", "\xa1\xc1\x66needle\xd8\x2a\x68haystack"s, true, nf},
        {"doubletagged", "\xa1\xc1\xc2\x66needle\xd8\x2a\x68haystack"s, true, nf},
        {"chunked", "\xa1\x7f\x66needle\xff\xd8\x2a\x68haystack"s, true, il},
        {"chunked*2", "\xa1\x7f\x60\x66needle\xff\xd8\x2a\x68haystack"s, true, il},
        {"chunked*2bis", "\xa1\x7f\x66needle\x60\xff\xd8\x2a\x68haystack"s, true, il},
        {"chunked*3", "\xa1\x7f\x62ne\x62""ed\x62le\xff\xd8\x2a\x68haystack"s, true, il},
        {"chunked*8", "\xa1\x7f\x61n\x61""e\x60\x61""e\x61""d\x60\x62le\x60\xff\xd8\x2a\x68haystack"s, true, il},
        {"1before", "\xa2\x68haystack\x66needle\x66needle\xd8\x2a\x68haystack"s, true, no_error},
        {"tagged-1before", "\xa2\xc1\x68haystack\x66needle\xc1\x66needle\xd8\x2a\x68haystack"s, true, nf},
        {"doubletagged-1before2", "\xa2\xc1\xc2\x68haystack\x66needle\xc1\xc2\x66needle\xd8\x2a\x68haystack"s, true,
         nf},
        {"arraybefore", "\xa2\x61z\x80\x66needle\xd8\x2a\x68haystack"s, true, no_error},
        {"nestedarraybefore", "\xa2\x61z\x81\x81\0\x66needle\xd8\x2a\x68haystack"s, true, no_error},
        {"arrayarraybefore", "\xa2\x82\1\2\x80\x66needle\xd8\x2a\x68haystack"s, true, no_error},
        {"mapbefore", "\xa2\x61z\xa0\x66needle\xd8\x2a\x68haystack"s, true, no_error},
        {"nestedmapbefore", "\xa2\x61z\xa1\0\x81\0\x66needle\xd8\x2a\x68haystack"s, true, no_error},
        {"mapmapbefore", "\xa2\xa1\1\2\xa0\x66needle\xd8\x2a\x68haystack"s, true, no_error},
    };
    for (find_row const &r : rows) {
        CAPTURE(r.name);
        CAPTURE(r.expected);
        auto const doc = cbor::lazy::adopt(std::string(r.data));
        REQUIRE(doc.has_value());
        auto const value = doc->at("needle");
        CHECK_EQ(outcome(value), error_text(r.port));
        if (!value)
            continue;
        CHECK_EQ(outcome(value->type()), error_text(no_error));
        CHECK_EQ(value->type().value_or(cbor::type::null), cbor::type::tag);
        auto const tree = value->clone();
        REQUIRE_EQ(outcome(tree), error_text(no_error));
        CHECK_EQ((*tree)->get<cbor::type::tag>().value_or(std::pair<std::uint64_t, cbor::item const *>{}).first, 42u);
        auto const text = value->get<std::string_view>();
        REQUIRE_EQ(outcome(text), error_text(no_error));
        CHECK_EQ(**text, "haystack"sv);
    }
}

// checkedIntegers: the value as std::int64_t and as int, or number_out_of_range where upstream gives
// CborErrorDataTooLarge.
TEST_CASE("tinycbor parser: checked integers")
{
    struct checked_row {
        std::string_view name;
        std::string data;
        bool in_int64;
        std::int64_t value;
    };
    std::int64_t const min = std::numeric_limits<std::int64_t>::min();
    checked_row const rows[] = {
        {"0", "\x00"s, true, 0},
        {"1", "\x01"s, true, 1},
        {"10", "\x0a"s, true, 10},
        {"23", "\x17"s, true, 23},
        {"24", "\x18\x18"s, true, 24},
        {"UINT8_MAX", "\x18\xff"s, true, 255},
        {"UINT8_MAX+1", "\x19\x01\x00"s, true, 256},
        {"UINT16_MAX", "\x19\xff\xff"s, true, 65535},
        {"UINT16_MAX+1", "\x1a\0\1\x00\x00"s, true, 65536},
        {"INT32_MAX", "\x1a\x7f\xff\xff\xff"s, true, 2147483647},
        {"INT32_MAX+1", "\x1a\x80\x00\x00\x00"s, true, 2147483648},
        {"UINT32_MAX", "\x1a\xff\xff\xff\xff"s, true, 4294967295},
        {"UINT32_MAX+1", "\x1b\0\0\0\1\0\0\0\0"s, true, 4294967296},
        {"UINT64_MAX", "\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s, false, 0},
        {"-1", "\x20"s, true, -1},
        {"-2", "\x21"s, true, -2},
        {"-24", "\x37"s, true, -24},
        {"-25", "\x38\x18"s, true, -25},
        {"-UINT8_MAX", "\x38\xff"s, true, -256},
        {"-UINT8_MAX-1", "\x39\x01\x00"s, true, -257},
        {"-UINT16_MAX", "\x39\xff\xff"s, true, -65536},
        {"-UINT16_MAX-1", "\x3a\0\1\x00\x00"s, true, -65537},
        {"INT32_MIN", "\x3a\x7f\xff\xff\xff"s, true, -2147483648},
        {"INT32_MIN-1", "\x3a\x80\x00\x00\x00"s, true, -2147483649},
        {"-UINT32_MAX", "\x3a\xff\xff\xff\xff"s, true, -4294967296},
        {"-UINT32_MAX-1", "\x3b\0\0\0\1\0\0\0\0"s, true, -4294967297},
        {"INT64_MIN+1", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xfe"s, true, min + 1},
        {"INT64_MIN", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xff"s, true, min},
        {"INT64_MIN-1", "\x3b\x80\0\0\0""\0\0\0\0"s, false, 0},
        {"-UINT64_MAX", "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xfe"s, false, 0},
        {"-UINT64_MAX+1", "\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s, false, 0},
    };
    for (checked_row const &r : rows) {
        CAPTURE(r.name);
        auto const as_int64 = cbor::at_path<"$", std::int64_t>(r.data);
        auto const as_int = cbor::at_path<"$", int>(r.data);
        if (r.in_int64) {
            CHECK_EQ(outcome(as_int64), error_text(no_error));
            CHECK_EQ(as_int64.value_or(0), r.value);
        } else {
            CHECK_EQ(outcome(as_int64), error_text(error::number_out_of_range));
        }
        if (r.in_int64 && r.value >= std::numeric_limits<int>::min() && r.value <= std::numeric_limits<int>::max()) {
            CHECK_EQ(outcome(as_int), error_text(no_error));
            CHECK_EQ(as_int.value_or(0), r.value);
        } else {
            CHECK_EQ(outcome(as_int), error_text(error::number_out_of_range));
        }
    }
}

// validationValid: each item of arrays_data() is well-formed.
TEST_CASE("tinycbor parser: valid items are well-formed")
{
    for (element const &e : arrays_data()) {
        CAPTURE(e.name);
        CHECK(cbor::diagnostic_notation(e.data).has_value());
        auto const size = cbor::item_size(e.data);
        if (e.indefinite)
            CHECK_EQ(outcome(size), error_text(error::indefinite_length));
        else
            CHECK_EQ(size.value_or(0), e.data.size());
    }
}

namespace tinycbor_parser
{

struct validation_row {
    std::string_view name;
    std::string data;
    std::string_view upstream;
    error expected;
};

// addValidationData() of a 64-bit build. Each upstream error maps to the error of cbor::diagnostic_notation(), which
// reads indefinite length:
// - IllegalNumber, UnknownType: additional information 28 to 30 is reserved (RFC 8949 3, Appendix F.1): syntax_error;
// - UnexpectedBreak: a break where no indefinite-length item is open (RFC 8949 3.2.1): syntax_error;
// - IllegalSimpleType: a two-byte simple value below 32 (RFC 8949 3.3): syntax_error;
// - IllegalType, a nested indefinite-length string: a chunk that is not a definite string of the same major type
//   (RFC 8949 3.2.3): syntax_error;
// - UnexpectedEOF: too_little_data;
// - DataTooLarge: upstream refuses a count that its size_t cannot hold. The bytes end before the item (RFC 8949
//   Appendix F.1), and diagnostic_notation.hpp gives too_little_data where "the bytes end before the item ends";
// - InvalidUtf8TextString: "The call does not check tag content, UTF-8 or duplicate keys" (diagnostic_notation.hpp),
//   so the port expects no error.
std::vector<validation_row> validation_rows()
{
    return {
        {"illegal-number-in-unsigned-1", "\x81\x1c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-unsigned-2", "\x81\x1d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-unsigned-3", "\x81\x1e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-unsigned-4", "\x81\x1f"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-negative-1", "\x81\x3c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-negative-2", "\x81\x3d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-negative-3", "\x81\x3e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-negative-4", "\x81\x3f"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-bytearray-length-1", "\x81\x5c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-bytearray-length-2", "\x81\x5d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-bytearray-length-3", "\x81\x5e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-string-length-1", "\x81\x7c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-string-length-2", "\x81\x7d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-string-length-3", "\x81\x7e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-array-length-1", "\x81\x9c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-array-length-2", "\x81\x9d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-array-length-3", "\x81\x9e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-map-length-1", "\x81\xbc"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-map-length-2", "\x81\xbd"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-map-length-3", "\x81\xbe"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-tag-1", "\x81\xdc"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-tag-2", "\x81\xdd"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-tag-3", "\x81\xde"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-tag-4", "\x81\xdf"s, "IllegalNumber", cbor::error::syntax_error},
        {"unsigned-too-short-1-0", "\x81\x18"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"unsigned-too-short-2-0", "\x81\x19"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"unsigned-too-short-2-1", "\x81\x19\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"unsigned-too-short-4-0", "\x81\x1a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"unsigned-too-short-4-3", "\x81\x1a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"unsigned-too-short-8-0", "\x81\x1b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"unsigned-too-short-8-7", "\x81\x1b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"negative-length-too-short-1-0", "\x81\x38"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"negative-length-too-short-2-0", "\x81\x39"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"negative-length-too-short-2-1", "\x81\x39\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"negative-length-too-short-4-0", "\x81\x3a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"negative-length-too-short-4-3", "\x81\x3a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"negative-length-too-short-8-0", "\x81\x3b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"negative-length-too-short-8-7", "\x81\x3b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-length-too-short-1-0", "\x81\x58"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-length-too-short-2-0", "\x81\x59"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-length-too-short-2-1", "\x81\x59\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-length-too-short-4-0", "\x81\x5a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-length-too-short-4-3", "\x81\x5a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-length-too-short-8-0", "\x81\x5b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-length-too-short-8-7", "\x81\x5b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-length-too-short-1-0", "\x81\x78"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-length-too-short-2-0", "\x81\x79"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-length-too-short-2-1", "\x81\x79\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-length-too-short-4-0", "\x81\x7a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-length-too-short-4-3", "\x81\x7a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-length-too-short-8-0", "\x81\x7b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-length-too-short-8-7", "\x81\x7b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-length-too-short-1-0", "\x81\x5f\x58"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-length-too-short-2-0", "\x81\x5f\x59"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-length-too-short-2-1", "\x81\x5f\x59\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-length-too-short-4-0", "\x81\x5f\x5a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-length-too-short-4-3", "\x81\x5f\x5a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-length-too-short-8-0", "\x81\x5f\x5b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-length-too-short-8-7", "\x81\x5f\x5b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-length-too-short-1-0", "\x81\x7f\x78"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-length-too-short-2-0", "\x81\x7f\x79"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-length-too-short-2-1", "\x81\x7f\x79\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-length-too-short-4-0", "\x81\x7f\x7a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-length-too-short-4-3", "\x81\x7f\x7a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-length-too-short-8-0", "\x81\x7f\x7b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-length-too-short-8-7", "\x81\x7f\x7b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-2-length-too-short-1-0", "\x81\x5f\x40\x58"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-2-length-too-short-2-0", "\x81\x5f\x40\x59"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-2-length-too-short-2-1", "\x81\x5f\x40\x59\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-2-length-too-short-4-0", "\x81\x5f\x40\x5a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-2-length-too-short-4-3", "\x81\x5f\x40\x5a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-2-length-too-short-8-0", "\x81\x5f\x40\x5b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-2-length-too-short-8-7", "\x81\x5f\x40\x5b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-2-length-too-short-1-0", "\x81\x7f\x60\x78"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-2-length-too-short-2-0", "\x81\x7f\x60\x79"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-2-length-too-short-2-1", "\x81\x7f\x60\x79\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-2-length-too-short-4-0", "\x81\x7f\x60\x7a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-2-length-too-short-4-3", "\x81\x7f\x60\x7a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-2-length-too-short-8-0", "\x81\x7f\x60\x7b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-2-length-too-short-8-7", "\x81\x7f\x60\x7b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-length-too-short-1-0", "\x81\x98"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-length-too-short-2-0", "\x81\x99"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-length-too-short-2-1", "\x81\x99\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-length-too-short-4-0", "\x81\x9a"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-length-too-short-4-3", "\x81\x9a\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-length-too-short-8-0", "\x81\x9b"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-length-too-short-8-7", "\x81\x9b\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-length-too-short-1-0", "\x81\xb8"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-length-too-short-2-0", "\x81\xb9"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-length-too-short-2-1", "\x81\xb9\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-length-too-short-4-0", "\x81\xba"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-length-too-short-4-3", "\x81\xba\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-length-too-short-8-0", "\x81\xbb"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-length-too-short-8-7", "\x81\xbb\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"tag-too-short-1-0", "\x81\xd8"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"tag-too-short-2-0", "\x81\xd9"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"tag-too-short-2-1", "\x81\xd9\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"tag-too-short-4-0", "\x81\xda"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"tag-too-short-4-3", "\x81\xda\x01\x02\x03"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"tag-too-short-8-0", "\x81\xdb"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"tag-too-short-8-7", "\x81\xdb\1\2\3\4\5\6\7"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"fp16-too-short1", "\x81\xf9"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"fp16-too-short2", "\x81\xf9\x00"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"float-too-short1", "\x81\xfa"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"float-too-short2", "\x81\xfa\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"double-too-short1", "\x81\xfb"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"double-too-short2", "\x81\xfb\0\0\0\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-too-short1", "\x81\x42z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-too-short2", "\x81\x58\x02z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-too-short3", "\x81\x5a\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-too-short4", "\x81\x5b\0\0\0\0\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-too-short1", "\x81\x62z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-too-short2", "\x81\x78\x02z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-too-short3", "\x81\x7a\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-too-short4", "\x81\x7b\0\0\0\0\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-too-short1", "\x81\x5f\x42z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-too-short2", "\x81\x5f\x58\x02z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-too-short3", "\x81\x5f\x5a\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-too-short4", "\x81\x5f\x5b\0\0\0\0\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-too-short1", "\x81\x7f\x62z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-too-short2", "\x81\x7f\x78\x02z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-too-short3", "\x81\x7f\x7a\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-too-short4", "\x81\x7f\x7b\0\0\0\0\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-too-short1x2", "\x81\x5f\x40\x42z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-too-short2x2", "\x81\x5f\x40\x58\x02z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-too-short3x2", "\x81\x5f\x40\x5a\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunked-too-short4x2", "\x81\x5f\x40\x5b\0\0\0\0\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-too-short1x2", "\x81\x7f\x60\x62z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-too-short2x2", "\x81\x7f\x60\x78\x02z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-too-short3x2", "\x81\x7f\x60\x7a\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunked-too-short4x2", "\x81\x7f\x60\x7b\0\0\0\0\0\0\0\2z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-no-break1", "\x81\x5f"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-no-break2", "\x81\x5f\x40"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-no-break1", "\x81\x7f"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-no-break2", "\x81\x7f\x60"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-no-break1", "\x81\x9f"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-no-break2", "\x81\x9f\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-no-break1", "\x81\xbf"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"map-no-break2", "\x81\xbf\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"array-element-is-break", "\x81\x81\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-key-is-break", "\x81\xa1\xff\0"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-value-is-break", "\x81\xa1\0\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-break-after-key", "\x81\xbf\0\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-break-after-second-key", "\x81\xbf\x64xyzw\x04\x00\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-break-after-value-tag", "\x81\xbf\0\xc0\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-break-after-value-tag2", "\x81\xbf\0\xd8\x20\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"tagged-break", "\xc0\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"bytearray-wraparound1", "\x81\x5a\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-wraparound1", "\x81\x7a\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-wraparound2", "\x81\x5b\0\0\0\1\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-wraparound2", "\x81\x7b\0\0\0\1\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-wraparound3", "\x81\x5b\xff\xff\xff\xff\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-wraparound3", "\x81\x7b\xff\xff\xff\xff\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunk-wraparound1", "\x81\x5f\x5a\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunk-wraparound1", "\x81\x7f\x7a\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunk-wraparound2", "\x81\x5f\x5b\0\0\0\1\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunk-wraparound2", "\x81\x7f\x7b\0\0\0\1\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunk-wraparound3", "\x81\x5f\x5b\xff\xff\xff\xff\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunk-wraparound3", "\x81\x7f\x7b\xff\xff\xff\xff\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-array", "\x81"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-array2", "\x81\x78\x20"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-array-element", "\x81\x82\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-object", "\x81\xa1"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-object2", "\x81\xb8\x20"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-object-key", "\x81\xa1\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-object-value", "\x81\xa2\x01\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-tag", "\x81\xc0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"eof-after-tag2", "\x81\xd8\x20"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"future-type-28", "\x81\xfc"s, "UnknownType", cbor::error::syntax_error},
        {"future-type-29", "\x81\xfd"s, "UnknownType", cbor::error::syntax_error},
        {"future-type-30", "\x81\xfe"s, "UnknownType", cbor::error::syntax_error},
        {"unexpected-break", "\x81\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"illegal-simple-0", "\x81\xf8\0"s, "IllegalSimpleType", cbor::error::syntax_error},
        {"illegal-simple-31", "\x81\xf8\x1f"s, "IllegalSimpleType", cbor::error::syntax_error},
        {"array-too-big1", "\x81\x9a\xff\xff\xff\xff\0\0\0\0"s, "DataTooLarge", cbor::error::too_little_data},
        {"array-too-big2", "\x81\x9b\0\0\0\1\0\0\0\0"s, "DataTooLarge", cbor::error::too_little_data},
        {"object-too-big1", "\x81\xba\xff\xff\xff\xff\0\0\0\0"s, "DataTooLarge", cbor::error::too_little_data},
        {"object-too-big2", "\x81\xbb\0\0\0\1\0\0\0\0"s, "DataTooLarge", cbor::error::too_little_data},
        {"no-break-for-array0", "\x81\x9f"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"no-break-for-array1", "\x81\x9f\x01"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"no-break-string0", "\x81\x7f"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"no-break-string1", "\x81\x7f\x61Z"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"nested-indefinite-length-bytearrays", "\x81\x5f\x5f\xff\xff"s, "IllegalNumber", cbor::error::syntax_error},
        {"nested-indefinite-length-strings", "\x81\x7f\x7f\xff\xff"s, "IllegalNumber", cbor::error::syntax_error},
        {"string-chunk-unsigned", "\x81\x7f\0\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-negative", "\x81\x7f\x20\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-bytearray", "\x81\x7f\x40\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-array", "\x81\x7f\x80\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-map", "\x81\x7f\xa0\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-tag", "\x81\x7f\xc0\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-tagged-string", "\x81\x7f\xc0\x60\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-simple0", "\x81\x7f\xe0\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-false", "\x81\x7f\xf4\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-true", "\x81\x7f\xf5\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-null", "\x81\x7f\xf6\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-undefined", "\x81\x7f\xf7\xff"s, "IllegalType", cbor::error::syntax_error},
        {"bytearray-chunk-string", "\x81\x5f\x60\xff"s, "IllegalType", cbor::error::syntax_error},
        {"bytearray-chunk-tagged-bytearray", "\x81\x7f\xc0\x40\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-utf8-chunk-split", "\x81\x7f\x61\xc2\x61\xa0\xff"s, "InvalidUtf8TextString", no_error},
    };
}

// The first item in the array of a validation row has an indefinite-length head: major type 2, 3, 4 or 5 with
// additional information 31 (RFC 8949 3.2).
bool opens_indefinite(std::string const &data)
{
    if (data.size() < 2 || static_cast<unsigned char>(data.front()) != 0x81)
        return false;
    unsigned const head = static_cast<unsigned char>(data.at(1));
    return head == 0x5f || head == 0x7f || head == 0x9f || head == 0xbf;
}

}

// validation: each row is not well-formed. cbor::diagnostic_notation() reads indefinite length and gives the error of
// the row. cbor::decode() and cbor::item_size() give indefinite_length where the item in the array opens with an
// indefinite-length head (decode.hpp and item_size.hpp, "@retval indefinite_length"), and the same error otherwise.
// For a row whose name starts with "map", upstream also looks for a key in the map; cbor::lazy::at() gives the same
// error.
TEST_CASE("tinycbor parser: validation of items that are not well-formed")
{
    for (validation_row const &r : validation_rows()) {
        CAPTURE(r.name);
        CAPTURE(r.upstream);
        CHECK_EQ(outcome(cbor::diagnostic_notation(r.data)), error_text(r.expected));
        error const strict = opens_indefinite(r.data) ? error::indefinite_length : r.expected;
        CHECK_EQ(outcome(cbor::decode(r.data)), error_text(strict));
        CHECK_EQ(outcome(cbor::item_size(r.data)), error_text(strict));
        if (r.name.starts_with("map")) {
            auto const doc = cbor::lazy::adopt(std::string(r.data));
            REQUIRE(doc.has_value());
            auto const map = doc->at(std::size_t{0});
            auto const value = map ? map->at("foobar") : map;
            CHECK_EQ(outcome(value), error_text(strict));
        }
    }
}

// strictValidation, the rows of CborValidateStrictMode on tags. RFC 8949 5.3.2 lets a decoder pass a tag that it does
// not convert, and the owner decided that cbor::decode keeps a tag over a content that its definition does not allow
// as a tag (decode.hpp: inadmissible_type_for_tag_content only for a typed array; "decode checks the content of no
// other tag"). So each row where upstream gives InappropriateTagForType gives no error here. Three more kinds of row
// differ from upstream:
// - tags 21, 22 and 23 on a text string: the RFC table gives "(any)" as their content, so the port expects no error;
// - tag 35 on a byte string: RFC 8949 3.4.5.3 does not define tag 35 any more, so no content is wrong;
// - tag 24 on an empty byte string: RFC 8949 3.4.5.1 says that the content is valid only if it encodes a well-formed
//   item, and no bytes are no item. cbor::decode() decodes the content (decode.hpp, "Decodes the content of a tag 24
//   as one more data item"), so the port expects too_little_data.
TEST_CASE("tinycbor parser: strict validation of tag content")
{
    struct tag_row {
        std::string_view name;
        std::string data;
        std::string_view upstream;
        error expected;
    };
    tag_row const rows[] = {
        {"tag-0-unsigned", "\xc0\x00"s, "InappropriateTagForType", no_error},
        {"tag-0-bytearray", "\xc0\x40"s, "InappropriateTagForType", no_error},
        {"tag-0-string", "\xc0\x60"s, "NoError", no_error},
        {"tag-0-tag-0-string", "\xc0\xc0\x60"s, "InappropriateTagForType", no_error},
        {"tag-1-unsigned", "\xc1\x00"s, "NoError", no_error},
        {"tag-1-negative", "\xc1\x20"s, "NoError", no_error},
        {"tag-1-bytearray", "\xc1\x40"s, "InappropriateTagForType", no_error},
        {"tag-2-bytearray", "\xc2\x40"s, "NoError", no_error},
        {"tag-2-string", "\xc2\x60"s, "InappropriateTagForType", no_error},
        {"tag-3-bytearray", "\xc3\x40"s, "NoError", no_error},
        {"tag-3-string", "\xc3\x60"s, "InappropriateTagForType", no_error},
        {"tag-4-string", "\xc4\x60"s, "InappropriateTagForType", no_error},
        {"tag-4-array", "\xc4\x82\0\1"s, "NoError", no_error},
        {"tag-5-string", "\xc5\x60"s, "InappropriateTagForType", no_error},
        {"tag-5-array", "\xc5\x82\0\1"s, "NoError", no_error},
        {"tag-21-bytearray", "\xd5\x40"s, "NoError", no_error},
        {"tag-21-string", "\xd5\x60"s, "InappropriateTagForType", no_error},
        {"tag-21-array", "\xd5\x80"s, "NoError", no_error},
        {"tag-21-map", "\xd5\xa0"s, "NoError", no_error},
        {"tag-22-bytearray", "\xd6\x40"s, "NoError", no_error},
        {"tag-22-string", "\xd6\x60"s, "InappropriateTagForType", no_error},
        {"tag-22-array", "\xd6\x80"s, "NoError", no_error},
        {"tag-22-map", "\xd6\xa0"s, "NoError", no_error},
        {"tag-23-bytearray", "\xd7\x40"s, "NoError", no_error},
        {"tag-23-string", "\xd7\x60"s, "InappropriateTagForType", no_error},
        {"tag-23-array", "\xd7\x80"s, "NoError", no_error},
        {"tag-23-map", "\xd7\xa0"s, "NoError", no_error},
        {"tag-24-bytearray", "\xd8\x18\x40"s, "NoError", error::too_little_data},
        {"tag-24-string", "\xd8\x18\x60"s, "InappropriateTagForType", no_error},
        {"tag-32-bytearray", "\xd8\x20\x40"s, "InappropriateTagForType", no_error},
        {"tag-32-string", "\xd8\x20\x60"s, "NoError", no_error},
        {"tag-33-bytearray", "\xd8\x21\x40"s, "InappropriateTagForType", no_error},
        {"tag-33-string", "\xd8\x21\x60"s, "NoError", no_error},
        {"tag-34-bytearray", "\xd8\x22\x40"s, "InappropriateTagForType", no_error},
        {"tag-34-string", "\xd8\x22\x60"s, "NoError", no_error},
        {"tag-35-bytearray", "\xd8\x23\x40"s, "InappropriateTagForType", no_error},
        {"tag-35-string", "\xd8\x23\x60"s, "NoError", no_error},
        {"tag-36-bytearray", "\xd8\x24\x40"s, "InappropriateTagForType", no_error},
        {"tag-36-string", "\xd8\x24\x60"s, "NoError", no_error},
        {"tag-55799-unsigned", "\xd9\xd9\xf7\x00"s, "NoError", no_error},
        {"tag-55799-negative", "\xd9\xd9\xf7\x20"s, "NoError", no_error},
        {"tag-55799-bytearray", "\xd9\xd9\xf7\x40"s, "NoError", no_error},
        {"tag-55799-string", "\xd9\xd9\xf7\x60"s, "NoError", no_error},
        {"tag-55799-array", "\xd9\xd9\xf7\x80"s, "NoError", no_error},
        {"tag-55799-map", "\xd9\xd9\xf7\xa0"s, "NoError", no_error},
        {"tag-55799-tag-0-unsigned", "\xd9\xd9\xf7\xc0\x00"s, "InappropriateTagForType", no_error},
        {"tag-55799-tag-0-string", "\xd9\xd9\xf7\xc0\x60"s, "NoError", no_error},
        {"tag-55799-simple0", "\xd9\xd9\xf7\xe0"s, "NoError", no_error},
        {"tag-55799-false", "\xd9\xd9\xf7\xf4"s, "NoError", no_error},
        {"tag-55799-true", "\xd9\xd9\xf7\xf5"s, "NoError", no_error},
        {"tag-55799-null", "\xd9\xd9\xf7\xf6"s, "NoError", no_error},
        {"tag-55799-undefined", "\xd9\xd9\xf7\xf7"s, "NoError", no_error},
        {"tag-55799-simple32", "\xd9\xd9\xf7\xf8\x20"s, "NoError", no_error},
        {"tag-55799-half", "\xd9\xd9\xf7\xf9\0\0"s, "NoError", no_error},
        {"tag-55799-float", "\xd9\xd9\xf7\xfa\0\0\0\0"s, "NoError", no_error},
        {"tag-55799-double", "\xd9\xd9\xf7\xfb\0\0\0\0\0\0\0\0"s, "NoError", no_error},
    };
    for (tag_row const &r : rows) {
        CAPTURE(r.name);
        CAPTURE(r.upstream);
        CHECK_EQ(outcome(cbor::decode(r.data)), error_text(r.expected));
    }
}

// strictValidation, the rows of CborValidateCompleteData: a byte after the item. cbor::diagnostic_notation() reads one
// item and gives syntax_error when bytes follow it (diagnostic_notation.hpp). cbor::decode() does not read a byte
// after the item, so that the input can be a CBOR sequence (decode.hpp, RFC 8742), and cbor::item_size() gives the
// size of the first item (item_size.hpp, "Bytes after the item do not change the result").
TEST_CASE("tinycbor parser: strict validation of a byte after the item")
{
    struct garbage_row {
        std::string_view name;
        std::string data;
    };
    garbage_row const rows[] = {
        {"garbage-data-0", "\0\1"s},
        {"garbage-data-1", "\x20\1"s},
        {"garbage-data-2", "\x40\1"s},
        {"garbage-data-3", "\x60\1"s},
        {"garbage-data-4", "\x80\1"s},
        {"garbage-data-5", "\xa0\1"s},
        {"garbage-data-6", "\xc0\x60\1"s},
        {"garbage-data-7", "\xf4\1"s},
        {"garbage-data-f16", "\xf9\0\0\1"s},
        {"garbage-data-f32", "\xfa\0\0\0\0\1"s},
        {"garbage-data-f64", "\xfb\0\0\0\0\0\0\0\0\1"s},
    };
    for (garbage_row const &r : rows) {
        CAPTURE(r.name);
        CHECK_EQ(outcome(cbor::diagnostic_notation(r.data)), error_text(error::syntax_error));
        CHECK_EQ(outcome(cbor::decode(r.data)), error_text(no_error));
        CHECK_EQ(cbor::item_size(r.data).value_or(0), r.data.size() - 1);
    }
}

// strictValidation, the rows of CborValidateStrictMode on keys that are not unique. cbor::structs gives duplicate_key
// for "a key that comes twice" (structs.hpp). The row with arrays as keys differs from upstream: every item can be a
// key (owner decision), but the key type std::string holds no array, so the first key is an item of another type and
// gives incorrect_type (structs.hpp) before a second key is read. With std::vector<std::string> as the key type the
// two keys are read, and the second gives duplicate_key, as upstream intends.
TEST_CASE("tinycbor parser: strict validation of keys that are not unique")
{
    CHECK_EQ(outcome(cbor::structs<std::map<std::int64_t, std::int64_t>>::decode("\xa2\0\1\0\2"s)),
             error_text(error::duplicate_key));
    CHECK_EQ(outcome(cbor::structs<std::map<std::string, std::int64_t>>::decode("\xa2\x61z\1\x61z\2"s)),
             error_text(error::duplicate_key));
    CHECK_EQ(outcome(cbor::structs<std::map<std::string, std::int64_t>>::decode("\xa2\x81\x65Hello\1\x81\x65Hello\2"s)),
             error_text(error::incorrect_type));
    CHECK_EQ(outcome(cbor::structs<std::map<std::vector<std::string>, std::int64_t>>::decode(
                 "\xa2\x81\x65Hello\1\x81\x65Hello\2"s)),
             error_text(error::duplicate_key));
}

// incompleteData: each prefix of an item that ends before the last byte but one is too little data. A prefix that
// holds a complete indefinite-length head gives indefinite_length in cbor::decode() and cbor::item_size(), so these
// two calls are checked on the definite rows only.
TEST_CASE("tinycbor parser: incomplete data")
{
    std::vector<element> all;
    append_elements(all, fixed_rows());
    append_elements(all, float_rows());
    append_elements(all, string_rows());
    append_elements(all, tag_rows());
    append_elements(all, map_mixed_rows());
    for (element const &e : all) {
        CAPTURE(e.name);
        for (std::size_t length = 0; length + 1 < e.data.size(); ++length) {
            CAPTURE(length);
            std::string const prefix = e.data.substr(0, length);
            CHECK_EQ(outcome(cbor::diagnostic_notation(prefix)), error_text(error::too_little_data));
            if (e.indefinite)
                continue;
            CHECK_EQ(outcome(cbor::decode(prefix)), error_text(error::too_little_data));
            CHECK_EQ(outcome(cbor::item_size(prefix)), error_text(error::too_little_data));
        }
    }
}

// endPointer: the first item ends at the offset. cbor::item_size() gives that offset for a definite item, and the
// bytes up to the offset are one item.
TEST_CASE("tinycbor parser: the end of the first item")
{
    struct end_row {
        std::string_view name;
        std::string data;
        std::size_t offset;
        bool indefinite;
    };
    end_row const rows[] = {
        {"number1", "\x81\x01\x01"s, 2, false},
        {"number24", "\x81\x18\x18\x01"s, 3, false},
        {"string", "\x81\x61Z\x01"s, 3, false},
        {"indefinite-string", "\x81\x7f\x61Z\xff\x01"s, 5, true},
        {"array", "\x81\x02\x01"s, 2, false},
        {"indefinite-array", "\x81\x9f\x02\xff\x01"s, 4, true},
        {"object", "\x81\xa1\x03\x02\x01"s, 4, false},
        {"indefinite-object", "\x81\xbf\x03\x02\xff\x01"s, 5, true},
    };
    for (end_row const &r : rows) {
        CAPTURE(r.name);
        CHECK_EQ(outcome(cbor::diagnostic_notation(r.data.substr(0, r.offset))), error_text(no_error));
        CHECK_EQ(outcome(cbor::diagnostic_notation(r.data)), error_text(error::syntax_error));
        auto const size = cbor::item_size(r.data);
        if (r.indefinite)
            CHECK_EQ(outcome(size), error_text(error::indefinite_length));
        else
            CHECK_EQ(size.value_or(0), r.offset);
    }
}

// recursionLimit: deep nesting. Upstream prints the
// item and then refuses to skip it with CborErrorNestingTooDeep. The library has no fixed limit of nesting: a reader
// that recurses stops where the stack of its thread ends, and cbor::item_size() does not recurse (validity.hpp,
// "Nesting has no fixed limit"). The port expects the size of the item, and key_not_found where upstream looks for
// the key "foo" in a map.
// The depth is the one of this library, not the limit of upstream: test::levels_main_thread_reads, the depth that the
// main thread of each build holds (small_stack.hpp).
TEST_CASE("tinycbor parser: deep nesting")
{
    constexpr std::size_t recursions = test::levels_main_thread_reads;
    struct deep_row {
        std::string_view name;
        std::string data;
        bool indefinite;
        error find;
    };
    auto const repeated = [](std::string const &piece, std::size_t const count) {
        std::string out;
        for (std::size_t i = 0; i < count; ++i)
            out += piece;
        return out;
    };
    deep_row const rows[] = {
        {"array", repeated("\x81"s, recursions) + "\x20", false, no_error},
        {"_array", repeated("\x9f"s, recursions) + "\x20" + repeated("\xff"s, recursions), true, no_error},
        {"map-recursive-values", repeated("\xa1\x65Hello"s, recursions) + "\2", false, error::key_not_found},
        {"_map-recursive-values", repeated("\xbf\x65World"s, recursions) + "\2" + repeated("\xff"s, recursions), true,
         error::indefinite_length},
        {"map-recursive-keys", repeated("\xa1"s, recursions) + "\2" + repeated("\x7f\x64quux\xff"s, recursions), true,
         error::indefinite_length},
        {"_map-recursive-keys", repeated("\xbf"s, recursions) + "\2" + repeated("\1\xff"s, recursions), true,
         error::indefinite_length},
        {"mixed", repeated("\x81\xa1\1"s, recursions / 2) + "\2", false, no_error},
    };
    for (deep_row const &r : rows) {
        CAPTURE(r.name);
        CHECK_EQ(outcome(cbor::diagnostic_notation(r.data)), error_text(no_error));
        auto const size = cbor::item_size(r.data);
        if (r.indefinite)
            CHECK_EQ(outcome(size), error_text(error::indefinite_length));
        else
            CHECK_EQ(size.value_or(0), r.data.size());
        if (r.find != no_error) {
            auto const doc = cbor::lazy::adopt(std::string(r.data));
            REQUIRE(doc.has_value());
            CHECK_EQ(outcome(doc->at("foo")), error_text(r.find));
        }
    }
}
