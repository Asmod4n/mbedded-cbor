// The tests of the TinyCBOR pretty printer, ported to cbor::diagnostic_notation(). The tests show that mbedded-cbor
// prints the text of another library for the same bytes, in the diagnostic notation of RFC 8949 section 8.
//
// Upstream: https://github.com/intel/tinycbor, commit 0cb8ba93637a18e4497e85a3f5c681ee7dba38f4,
// tests/pretty/tst_pretty.cpp. This file carries this license:
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
// Upstream has flags that leave out the encoding indicators, the marks of indefinite length and the chunks of a
// string. cbor::diagnostic_notation() has no such options: it always prints them (diagnostic_notation.hpp). A row is
// ported where its flags print all that the bytes hold. The floats and an empty indefinite-length string take the
// form of RFC 8949 section 8 and the EDN draft, as the comment at the top of tinycbor_parser.cpp says.

#include "../small_stack.hpp"
#include "tinycbor.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using cbor::error;
using tinycbor_port::error_text;
using tinycbor_port::no_error;
using tinycbor_port::outcome;
using tinycbor_port::printed;

namespace tinycbor_pretty
{

struct row {
    std::string_view name;
    std::string data;
    std::string_view upstream;
    std::string expected;
};

// The rows of validInput_data() whose flags print all that the bytes hold.
std::vector<row> valid_rows()
{
    return {
        {"0", "\x00"s, "0", "0"},
        {"1", "\x01"s, "1", "1"},
        {"23", "\x17"s, "23", "23"},
        {"24", "\x18\x18"s, "24", "24"},
        {"UINT8_MAX", "\x18\xff"s, "255", "255"},
        {"UINT16_MAX", "\x19\xff\xff"s, "65535", "65535"},
        {"UINT32_MAX", "\x1a\xff\xff\xff\xff"s, "4294967295", "4294967295"},
        {"UINT64_MAX", "\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff"s, "18446744073709551615", "18446744073709551615"},
        {"-1", "\x20"s, "-1", "-1"},
        {"-24", "\x37"s, "-24", "-24"},
        {"-25", "\x38\x18"s, "-25", "-25"},
        {"INT64_MIN", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xff"s, "-9223372036854775808", "-9223372036854775808"},
        {"INT64_MIN-1", "\x3b\x80\x00\x00\x00""\x00\x00\x00\x00"s, "-9223372036854775809", "-9223372036854775809"},
        {"-UINT64_MAX-1", "\x3b\xff\xff\xff\xff""\xff\xff\xff\xff"s, "-18446744073709551616", "-18446744073709551616"},
        {"0*1-all", "\x18\x00"s, "0_0", "0_0"},
        {"0*2-all", "\x19\x00\x00"s, "0_1", "0_1"},
        {"0*4-all", "\x1a\0\0\0\0"s, "0_2", "0_2"},
        {"0*8-all", "\x1b\0\0\0\0\0\0\0\0"s, "0_3", "0_3"},
        {"-1*1-all", "\x38\x00"s, "-1_0", "-1_0"},
        {"-1*2-all", "\x39\x00\x00"s, "-1_1", "-1_1"},
        {"simple0", "\xe0"s, "simple(0)", "simple(0)"},
        {"simple255", "\xf8\xff"s, "simple(255)", "simple(255)"},
        {"false", "\xf4"s, "false", "false"},
        {"true", "\xf5"s, "true", "true"},
        {"null", "\xf6"s, "null", "null"},
        {"undefined", "\xf7"s, "undefined", "undefined"},
        {"0.f16", "\xf9\0\0"s, "0.f16", "0.0"},
        {"0.f", "\xfa\0\0\0\0"s, "0.f", "0.0_2"},
        {"0.", "\xfb\0\0\0\0\0\0\0\0"s, "0.", "0.0_3"},
        {"-1.f16", "\xf9\xbc\x00"s, "-1.f16", "-1.0"},
        {"nan", "\xfb\x7f\xf8\0\0\0\0\0\0"s, "nan", "NaN_3"},
        {"+inf", "\xfb\x7f\xf0\0\0\0\0\0\0"s, "inf", "Infinity_3"},
        {"emptybytestring", "\x40"s, "h''", "h''"},
        {"bytestring5", "\x45Hello"s, "h'48656c6c6f'", "h'48656c6c6f'"},
        {"emptytextstring", "\x60"s, "\"\"", "\"\""},
        {"textstring5", "\x65Hello"s, "\"Hello\"", "\"Hello\""},
        {"textstring1-nul", "\x61\0"s, "\"\\u0000\"", "\"\\u0000\""},
        {"emptybytestring*1-all", "\x58\x00"s, "h''_0", "h''_0"},
        {"emptytextstring*1-all", "\x78\x00"s, "\"\"_0", "\"\"_0"},
        {"bytestring5*2-all", "\x59\0\5Hello"s, "h'48656c6c6f'_1", "h'48656c6c6f'_1"},
        {"tag0", "\xc0\x00"s, "0(0)", "0(0)"},
        {"tag1", "\xc1\x00"s, "1(0)", "1(0)"},
        {"tag256", "\xd9\1\0\x00"s, "256(0)", "256(0)"},
        {"tag0*1-all", "\xd8\0\x00"s, "0_0(0)", "0_0(0)"},
        {"tag0*2-all", "\xd9\0\0\x00"s, "0_1(0)", "0_1(0)"},
        {"tag0*3-all", "\xda\0\0\0\0\x00"s, "0_2(0)", "0_2(0)"},
        {"tag0*4-all", "\xdb\0\0\0\0\0\0\0\0\x00"s, "0_3(0)", "0_3(0)"},
        {"unixtime", "\xc1\x1a\x55\x4b\xbf\xd3"s, "1(1431027667)", "1(1431027667)"},
        {"emptyarray", "\x80"s, "[]", "[]"},
        {"emptymap", "\xa0"s, "{}", "{}"},
        {"array123", "\x83\1\2\3"s, "[1, 2, 3]", "[1, 2, 3]"},
        {"map-0-24", "\xa1\0\x18\x18"s, "{0: 24}", "{0: 24}"},
        {"nestedarray", "\x81\x81\1"s, "[[1]]", "[[1]]"},
        {"array1*1-all", "\x98\1\1"s, "[_0 1]", "[_0 1]"},
        {"array1*1-overlongonly", "\x98\1\1"s, "[_0 1]", "[_0 1]"},
        {"map-0*1-24-all", "\xa1\x18\0\x18\x18"s, "{0_0: 24}", "{0_0: 24}"},
        {"_emptybytestring-frag-indet", "\x5f\xff"s, "(_ )", "''_"},
        {"_bytestring2-frag-indet", "\x5f\x41 \x41z\xff"s, "(_ h'20', h'7a')", "(_ h'20', h'7a')"},
        {"_textstring2-frag-indet", "\x7f\x61 \x61z\xff"s, "(_ \" \", \"z\")", "(_ \" \", \"z\")"},
        {"_bytestring5x2-overlong-indet-frag", "\x5f\x5b\0\0\0\0\0\0\0\3Hel\x5a\0\0\0\2lo\xff"s,
         "(_ h'48656c'_3, h'6c6f'_2)", "(_ h'48656c'_3, h'6c6f'_2)"},
        {"_emptyarray-indet", "\x9f\xff"s, "[_ ]", "[_ ]"},
        {"_emptymap-indet", "\xbf\xff"s, "{_ }", "{_ }"},
        {"_array1-indet", "\x9f\1\xff"s, "[_ 1]", "[_ 1]"},
        {"_array1-indet-overlong", "\x9f\1\xff"s, "[_ 1]", "[_ 1]"},
        {"_map-0-24-indet", "\xbf\0\x18\x18\xff"s, "{_ 0: 24}", "{_ 0: 24}"},
        {"_nestedarray-indet", "\x9f\x9f\1\xff\xff"s, "[_ [_ 1]]", "[_ [_ 1]]"},
        {"_arrayofmap-indet", "\x9f\xa1\1\2\xff"s, "[_ {1: 2}]", "[_ {1: 2}]"},
    };
}

// The data of the rows of validInput_data() that print less than the bytes hold. The flags do not change an error,
// so the test of incomplete input takes these rows too.
std::vector<std::string> other_valid_data()
{
    return {
        "\x18\x00"s,
        "\x19\x00\x00"s,
        "\x1a\0\0\0\0"s,
        "\x1b\0\0\0\0\0\0\0\0"s,
        "\x38\x00"s,
        "\x39\x00\x00"s,
        "\x58\x00"s,
        "\x78\x00"s,
        "\x59\0\5Hello"s,
        "\xd8\0\x00"s,
        "\x98\1\1"s,
        "\xa1\x18\0\x18\x18"s,
        "\x5f\xff"s,
        "\x5f\xff"s,
        "\x5f\xff"s,
        "\x7f\xff"s,
        "\x5f\x41 \x41z\xff"s,
        "\x5f\x41 \x41z\xff"s,
        "\x7f\x61 \x61z\xff"s,
        "\x7f\x61 \x61z\xff"s,
        "\x5f\x5b\0\0\0\0\0\0\0\3Hel\x5a\0\0\0\2lo\xff"s,
        "\x5f\x5b\0\0\0\0\0\0\0\3Hel\x5a\0\0\0\2lo\xff"s,
        "\x9f\xff"s,
        "\xbf\xff"s,
        "\x9f\1\xff"s,
        "\xbf\0\x18\x18\xff"s,
        "\x9f\xff"s,
        "\xbf\xff"s,
    };
}

struct invalid_row {
    std::string_view name;
    std::string data;
    std::string_view upstream;
    error expected;
};

// invalidInput_data(). Each error of upstream maps to the error of cbor::diagnostic_notation() as in the validation
// test of tinycbor_parser.cpp: IllegalNumber, UnknownType, UnexpectedBreak, IllegalSimpleType and IllegalType are
// syntax_error, UnexpectedEOF is too_little_data, and InvalidUtf8TextString is no error, because the call "does not
// check tag content, UTF-8 or duplicate keys" (diagnostic_notation.hpp). Upstream runs each row with seven sets of
// flags; the flags do not change the error, and the library has no flags, so each row runs once.
std::vector<invalid_row> invalid_rows()
{
    return {
        {"illegal-number-in-unsigned-1", "\x1c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-unsigned-2", "\x1d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-unsigned-3", "\x1e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-unsigned-4", "\x1f"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-negative-1", "\x3c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-negative-2", "\x3d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-negative-3", "\x3e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-negative-4", "\x3f"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-bytearray-length-1", "\x5c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-bytearray-length-2", "\x5d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-bytearray-length-3", "\x5e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-string-length-1", "\x7c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-string-length-2", "\x7d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-string-length-3", "\x7e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-array-length-1", "\x9c"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-array-length-2", "\x9d"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-array-length-3", "\x9e"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-map-length-1", "\xbc"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-map-length-2", "\xbd"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-map-length-3", "\xbe"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-tag-1", "\xdc"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-tag-2", "\xdd"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-tag-3", "\xde"s, "IllegalNumber", cbor::error::syntax_error},
        {"illegal-number-in-tag-4", "\xdf"s, "IllegalNumber", cbor::error::syntax_error},
        {"future-type-28", "\xfc"s, "UnknownType", cbor::error::syntax_error},
        {"future-type-29", "\xfd"s, "UnknownType", cbor::error::syntax_error},
        {"future-type-30", "\xfe"s, "UnknownType", cbor::error::syntax_error},
        {"unexpected-break", "\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"illegal-simple-0", "\xf8\0"s, "IllegalSimpleType", cbor::error::syntax_error},
        {"illegal-simple-31", "\xf8\x1f"s, "IllegalSimpleType", cbor::error::syntax_error},
        {"array-element-is-break", "\x81\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-key-is-break", "\xa1\xff\0"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-value-is-break", "\xa1\0\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-break-after-key", "\xbf\0\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-break-after-second-key", "\xbf\x64xyzw\x04\x00\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-break-after-value-tag", "\xbf\0\xc0\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"map-break-after-value-tag2", "\xbf\0\xd8\x20\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"tagged-break", "\xc0\xff"s, "UnexpectedBreak", cbor::error::syntax_error},
        {"bytearray-wraparound1", "\x5a\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-wraparound1", "\x7a\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-wraparound2", "\x5b\0\0\0\1\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-wraparound2", "\x7b\0\0\0\1\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-wraparound3", "\x5b\xff\xff\xff\xff\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-wraparound3", "\x7b\xff\xff\xff\xff\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunk-wraparound1", "\x5f\x5a\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunk-wraparound1", "\x7f\x7a\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunk-wraparound2", "\x5f\x5b\0\0\0\1\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunk-wraparound2", "\x7f\x7b\0\0\0\1\0\0\0\0"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"bytearray-chunk-wraparound3", "\x5f\x5b\xff\xff\xff\xff\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunk-wraparound3", "\x7f\x7b\xff\xff\xff\xff\xff\xff\xff\xff"s, "UnexpectedEOF", cbor::error::too_little_data},
        {"string-chunk-unsigned", "\x7f\0\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-negative", "\x7f\x20\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-bytearray", "\x7f\x40\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-array", "\x7f\x80\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-map", "\x7f\xa0\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-tag", "\x7f\xc0\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-tagged-string", "\x7f\xc0\x60\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-simple0", "\x7f\xe0\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-false", "\x7f\xf4\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-true", "\x7f\xf5\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-null", "\x7f\xf6\xff"s, "IllegalType", cbor::error::syntax_error},
        {"string-chunk-undefined", "\x7f\xf7\xff"s, "IllegalType", cbor::error::syntax_error},
        {"bytearray-chunk-string", "\x5f\x60\xff"s, "IllegalType", cbor::error::syntax_error},
        {"bytearray-chunk-tagged-bytearray", "\x7f\xc0\x40\xff"s, "IllegalType", cbor::error::syntax_error},
        {"invalid-utf8-bad-continuation-1char", "\x61\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-bad-continuation-2chars-1", "\x62\xc2\xc0"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-bad-continuation-2chars-2", "\x62\xc3\xdf"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-bad-continuation-2chars-3", "\x62\xc7\xf0"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-bad-continuation-3chars-1", "\x63\xe0\xa0\xc0"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-bad-continuation-3chars-2", "\x63\xe0\xc0\xa0"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-bad-continuation-4chars-1", "\x64\xf0\x90\x80\xc0"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-bad-continuation-4chars-2", "\x64\xf0\x90\xc0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-bad-continuation-4chars-3", "\x64\xf0\xc0\x80\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-too-short-2chars", "\x82\x61\xc2\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-too-short-3chars-1", "\x82\x61\xe0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-too-short-3chars-2", "\x82\x62\xe0\xa0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-too-short-4chars-1", "\x82\x61\xf0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-too-short-4chars-2", "\x82\x62\xf0\x90\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-too-short-4chars-3", "\x82\x63\xf0\x90\x80\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-hi-surrogate", "\x63\xed\xa0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-lo-surrogate", "\x63\xed\xb0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-surrogate-pair", "\x66\xed\xa0\x80\xed\xb0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-non-unicode-1", "\x64\xf4\x90\x80\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-non-unicode-2", "\x65\xf8\x88\x80\x80\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-non-unicode-3", "\x66\xfc\x84\x80\x80\x80\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-non-unicode-4", "\x66\xfd\xbf\xbf\xbf\xbf\xbf"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-fe", "\x61\xfe"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-ff", "\x61\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-1-2", "\x62\xc1\x81"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-1-3", "\x63\xe0\x81\x81"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-1-4", "\x64\xf0\x80\x81\x81"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-1-5", "\x65\xf8\x80\x80\x81\x81"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-1-6", "\x66\xfc\x80\x80\x80\x81\x81"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-2-3", "\x63\xe0\x82\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-2-4", "\x64\xf0\x80\x82\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-2-5", "\x65\xf8\x80\x80\x82\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-2-6", "\x66\xfc\x80\x80\x80\x82\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-3-4", "\x64\xf0\x80\xa0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-3-5", "\x65\xf8\x80\x80\xa0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-3-6", "\x66\xfc\x80\x80\x80\xa0\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-4-5", "\x65\xf8\x80\x84\x80\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-overlong-4-6", "\x66\xfc\x80\x80\x84\x80\x80"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-u0080", "\x7f\x61\xc2\x61\x80\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-u00ff", "\x7f\x61\xc3\x61\xbf\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-u0100", "\x7f\x61\xc4\x61\x80\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-u07ff", "\x7f\x61\xdf\x61\xbf\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-u0800", "\x7f\x61\xe0\x62\xa0\x80\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-u1000", "\x7f\x62\xe1\x80\x61\x80\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-uffff", "\x7f\x61\xef\x62\xbf\xbf\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-U00010000", "\x7f\x61\xf0\x63\x90\x80\x80\xff"s, "InvalidUtf8TextString", no_error},
        {"invalid-utf8-split-U0010ffff", "\x7f\x62\xf4\x8f\x62\xbf\xbf\xff"s, "InvalidUtf8TextString", no_error},
    };
}

std::string repeated(std::string const &piece, std::size_t const count)
{
    std::string out;
    for (std::size_t i = 0; i < count; ++i)
        out += piece;
    return out;
}

}

using namespace tinycbor_pretty;

// incompleteValidInput: the text of each row.
TEST_CASE("tinycbor pretty: valid input")
{
    for (row const &r : valid_rows()) {
        CAPTURE(r.name);
        CAPTURE(r.upstream);
        CHECK_EQ(printed(r.data), r.expected);
    }
}

// validInput: each row in an array of one element, cut after 1 to n - 2 bytes, is too little data.
TEST_CASE("tinycbor pretty: input that ends too early")
{
    std::vector<std::string> all = other_valid_data();
    for (row const &r : valid_rows())
        all.push_back(r.data);
    for (std::string const &d : all) {
        std::string const data = "\x81"s + d;
        CAPTURE(printed(d));
        for (std::size_t length = 1; length + 1 < data.size(); ++length) {
            CAPTURE(length);
            CHECK_EQ(outcome(cbor::diagnostic_notation(data.substr(0, length))), error_text(error::too_little_data));
        }
    }
}

// invalidInput: each row in an array of one element.
TEST_CASE("tinycbor pretty: input that is not well-formed")
{
    for (invalid_row const &r : invalid_rows()) {
        CAPTURE(r.name);
        CAPTURE(r.upstream);
        CHECK_EQ(outcome(cbor::diagnostic_notation("\x81"s + r.data)), error_text(r.expected));
    }
}

// recursionLimit: deep nesting. Upstream prints
// "<nesting too deep, recursion stopped>" at its limit. The library has no fixed limit of nesting: a reader that
// recurses stops where the stack of its thread ends (validity.hpp, "Nesting has no fixed limit"). The port expects the
// whole text.
// The depth is the one of this library, not the limit of upstream: test::levels_main_thread_reads, the depth that the
// main thread of each build holds (small_stack.hpp).
TEST_CASE("tinycbor pretty: deep nesting")
{
    constexpr std::size_t recursions = test::levels_main_thread_reads;
    struct deep_row {
        std::string_view name;
        std::string data;
        std::string expected;
    };
    deep_row const rows[] = {
        {"array", repeated("\x81"s, recursions) + "\x20", repeated("[", recursions) + "-1" + repeated("]", recursions)},
        {"_array", repeated("\x9f"s, recursions) + "\x20" + repeated("\xff"s, recursions),
         repeated("[_ ", recursions) + "-1" + repeated("]", recursions)},
        {"map", repeated("\xa1\x65Hello"s, recursions) + "\2",
         repeated("{\"Hello\": ", recursions) + "2" + repeated("}", recursions)},
        {"_map", repeated("\xbf\x65World"s, recursions) + "\2" + repeated("\xff"s, recursions),
         repeated("{_ \"World\": ", recursions) + "2" + repeated("}", recursions)},
        {"tag", repeated("\xc1"s, recursions) + "\x20", repeated("1(", recursions) + "-1" + repeated(")", recursions)},
    };
    for (deep_row const &r : rows) {
        CAPTURE(r.name);
        CHECK_EQ(printed(r.data), r.expected);
    }
}
