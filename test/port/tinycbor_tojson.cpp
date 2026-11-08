// The tests of the TinyCBOR converter to JSON, ported to cbor::diagnostic_notation(). The tests show that
// mbedded-cbor prints the same text as another library where the diagnostic notation of RFC 8949 section 8 and JSON
// have the same form.
//
// Upstream: https://github.com/intel/tinycbor, commit 0cb8ba93637a18e4497e85a3f5c681ee7dba38f4,
// tests/tojson/tst_tojson.cpp. This file carries this license:
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
// RFC 8949 section 8 writes integers, true, false, null, text strings, arrays and maps as JSON writes them. The rows
// of upstream that hold only these items are ported. JSON and RFC 8949 section 8 differ in the space between items:
// upstream writes "[1,2]" and {"a":1}, and Appendix A of RFC 8949 writes "[1, 2]" and {"a": 1}, as
// cbor::diagnostic_notation() does. Space between items has no meaning in either notation (RFC 8259 section 2), so
// the expected text takes the space of RFC 8949 Appendix A.

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

namespace tinycbor_tojson
{

struct row {
    std::string_view name;
    std::string data;
    std::string expected;
};

// The rows of addFixedData() without the floats, and the definite rows of addTextStringsData().
std::vector<row> json_rows()
{
    return {
        {"0", "\x00"s, "0"},
        {"1", "\x01"s, "1"},
        {"2^53-1", "\x1b\0\x1f\xff\xff""\xff\xff\xff\xff"s, "9007199254740991"},
        {"2^53+1", "\x1b\0\x20\0\0""\0\0\0\1"s, "9007199254740993"},
        {"2^63-1", "\x1b\x7f\xff\xff\xff""\xff\xff\xff\xff"s, "9223372036854775807"},
        {"2^64-1", "\x1b\xff\xff\xff\xff""\xff\xff\xff\xff"s, "18446744073709551615"},
        {"-1", "\x20"s, "-1"},
        {"-2", "\x21"s, "-2"},
        {"-2^53+1", "\x3b\0\x1f\xff\xff""\xff\xff\xff\xfe"s, "-9007199254740991"},
        {"-2^53-1", "\x3b\0\x20\0\0""\0\0\0\0"s, "-9007199254740993"},
        {"-2^63+1", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xfe"s, "-9223372036854775807"},
        {"-2^63", "\x3b\x7f\xff\xff\xff""\xff\xff\xff\xff"s, "-9223372036854775808"},
        {"-2^63-1", "\x3b\x80\0\0\0""\0\0\0\0"s, "-9223372036854775809"},
        {"-2^64+1", "\x3b\xff\xff\xff\xff""\xff\xff\xff\xfe"s, "-18446744073709551615"},
        {"-2^64", "\x3b\xff\xff\xff\xff""\xff\xff\xff\xff"s, "-18446744073709551616"},
        {"false", "\xf4"s, "false"},
        {"true", "\xf5"s, "true"},
        {"null", "\xf6"s, "null"},
        {"emptytextstring", "\x60"s, "\"\""},
        {"textstring1", "\x61 "s, "\" \""},
        {"textstring5", "\x65Hello"s, "\"Hello\""},
        {"textstring24", "\x78\x18""123456789012345678901234"s, "\"123456789012345678901234\""},
        {"textstring256", "\x79\1\0"s + std::string(256, '3'), "\"" + std::string(256, '3') + "\""},
        {"null", "\x61\0"s, R"("\u0000")"},
        {"bell", "\x61\7"s, R"("\u0007")"},
        {"backspace", "\x61\b"s, R"("\b")"},
        {"tab", "\x61\t"s, R"("\t")"},
        {"carriage-return", "\x61\r"s, R"("\r")"},
        {"line-feed", "\x61\n"s, R"("\n")"},
        {"form-feed", "\x61\f"s, R"("\f")"},
        {"esc", "\x61\x1f"s, R"("\u001f")"},
        {"quote", "\x61\""s, R"("\"")"},
        {"backslash", "\x61\\"s, R"("\\")"},
        {"u0080", "\x62\xc2\x80"s, "\"\xc2\x80\""},
        {"u00ff", "\x62\xc3\xbf"s, "\"\xc3\xbf\""},
        {"u0100", "\x62\xc4\x80"s, "\"\xc4\x80\""},
        {"u07ff", "\x62\xdf\xbf"s, "\"\xdf\xbf\""},
        {"u0800", "\x63\xe0\xa0\x80"s, "\"\xe0\xa0\x80\""},
        {"u1000", "\x63\xe1\x80\x80"s, "\"\xe1\x80\x80\""},
        {"uffff", "\x63\xef\xbf\xbf"s, "\"\xef\xbf\xbf\""},
        {"U00010000", "\x64\xf0\x90\x80\x80"s, "\"\xf0\x90\x80\x80\""},
        {"U0010ffff", "\x64\xf4\x8f\xbf\xbf"s, "\"\xf4\x8f\xbf\xbf\""},
    };
}

void check_text(std::string const &data, std::string const &expected)
{
    CAPTURE(expected);
    CHECK_EQ(printed(data), expected);
}

}

using namespace tinycbor_tojson;

// fixed and textstrings: the integers, true, false, null and the definite text strings. A text string outside ASCII
// is UTF-8 in the JSON of upstream and in the text of cbor::diagnostic_notation().
TEST_CASE("tinycbor tojson: integers, simple values and text strings")
{
    for (row const &r : json_rows()) {
        CAPTURE(r.name);
        check_text(r.data, r.expected);
    }
}

// emptyContainers, the definite rows.
TEST_CASE("tinycbor tojson: empty containers")
{
    check_text("\x80"s, "[]");
    check_text("\xa0"s, "{}");
}

// arrays and nestedArrays.
TEST_CASE("tinycbor tojson: arrays")
{
    for (row const &r : json_rows()) {
        CAPTURE(r.name);
        std::string const &d = r.data;
        std::string const &x = r.expected;
        check_text("\x81"s + d, "[" + x + "]");
        check_text("\x82"s + d + d, "[" + x + ", " + x + "]");
        check_text("\x81\x81"s + d, "[[" + x + "]]");
        check_text("\x81\x81\x81"s + d, "[[[" + x + "]]]");
        check_text("\x81\x82"s + d + d, "[[" + x + ", " + x + "]]");
        check_text("\x82\x81"s + d + d, "[[" + x + "], " + x + "]");
        check_text("\x82\x81"s + d + "\x81" + d, "[[" + x + "], [" + x + "]]");
    }
}

// maps and nestedMaps.
TEST_CASE("tinycbor tojson: maps")
{
    for (row const &r : json_rows()) {
        CAPTURE(r.name);
        check_text("\xa1\x65" "Hello"s + r.data, "{\"Hello\": " + r.expected + "}");
        check_text("\xa1\x65Hello\xa1\x65World"s + r.data, "{\"Hello\": {\"World\": " + r.expected + "}}");
    }
}

// recursionLimit: 1026 arrays, tags or maps, one in the other, with no item in the deepest one. Upstream stops at its
// limit of nesting with CborErrorNestingTooDeep. The library has no fixed limit of nesting (validity.hpp, "Nesting has
// no fixed limit"), so it reads to the end of the bytes and finds that the item is not complete: too_little_data
// (RFC 8949 Appendix F).
TEST_CASE("tinycbor tojson: deep nesting with no end")
{
    constexpr std::size_t recursions = 1024 + 2;
    std::string maps;
    for (std::size_t i = 0; i < recursions; ++i)
        maps += "\xa1\x60"s;
    CHECK_EQ(outcome(cbor::diagnostic_notation(std::string(recursions, '\x81'))), error_text(error::too_little_data));
    CHECK_EQ(outcome(cbor::diagnostic_notation(std::string(recursions, '\xc1'))), error_text(error::too_little_data));
    CHECK_EQ(outcome(cbor::diagnostic_notation(maps)), error_text(error::too_little_data));
}

// invalidUtf8Strings: upstream refuses text that is not valid UTF-8. cbor::diagnostic_notation() "does not check tag
// content, UTF-8 or duplicate keys" (diagnostic_notation.hpp), so the port expects no error. RFC 8949 5.3.1 leaves
// the handling of a text string that is not valid UTF-8 to the decoder.
TEST_CASE("tinycbor tojson: text that is not valid UTF-8")
{
    struct utf8_row {
        std::string_view name;
        std::string data;
    };
    utf8_row const rows[] = {
        {"invalid-utf8-bad-continuation-1char", "\x61\x80"s},
        {"invalid-utf8-bad-continuation-2chars-1", "\x62\xc2\xc0"s},
        {"invalid-utf8-bad-continuation-2chars-2", "\x62\xc3\xdf"s},
        {"invalid-utf8-bad-continuation-2chars-3", "\x62\xc7\xf0"s},
        {"invalid-utf8-bad-continuation-3chars-1", "\x63\xe0\xa0\xc0"s},
        {"invalid-utf8-bad-continuation-3chars-2", "\x63\xe0\xc0\xa0"s},
        {"invalid-utf8-bad-continuation-4chars-1", "\x64\xf0\x90\x80\xc0"s},
        {"invalid-utf8-bad-continuation-4chars-2", "\x64\xf0\x90\xc0\x80"s},
        {"invalid-utf8-bad-continuation-4chars-3", "\x64\xf0\xc0\x80\x80"s},
        {"invalid-utf8-too-short-2chars", "\x82\x61\xc2\x80"s},
        {"invalid-utf8-too-short-3chars-1", "\x82\x61\xe0\x80"s},
        {"invalid-utf8-too-short-3chars-2", "\x82\x62\xe0\xa0\x80"s},
        {"invalid-utf8-too-short-4chars-1", "\x82\x61\xf0\x80"s},
        {"invalid-utf8-too-short-4chars-2", "\x82\x62\xf0\x90\x80"s},
        {"invalid-utf8-too-short-4chars-3", "\x82\x63\xf0\x90\x80\x80"s},
        {"invalid-utf8-hi-surrogate", "\x63\xed\xa0\x80"s},
        {"invalid-utf8-lo-surrogate", "\x63\xed\xb0\x80"s},
        {"invalid-utf8-surrogate-pair", "\x66\xed\xa0\x80\xed\xb0\x80"s},
        {"invalid-utf8-non-unicode-1", "\x64\xf4\x90\x80\x80"s},
        {"invalid-utf8-non-unicode-2", "\x65\xf8\x88\x80\x80\x80"s},
        {"invalid-utf8-non-unicode-3", "\x66\xfc\x84\x80\x80\x80\x80"s},
        {"invalid-utf8-non-unicode-4", "\x66\xfd\xbf\xbf\xbf\xbf\xbf"s},
        {"invalid-utf8-fe", "\x61\xfe"s},
        {"invalid-utf8-ff", "\x61\xff"s},
        {"invalid-utf8-overlong-1-2", "\x62\xc1\x81"s},
        {"invalid-utf8-overlong-1-3", "\x63\xe0\x81\x81"s},
        {"invalid-utf8-overlong-1-4", "\x64\xf0\x80\x81\x81"s},
        {"invalid-utf8-overlong-1-5", "\x65\xf8\x80\x80\x81\x81"s},
        {"invalid-utf8-overlong-1-6", "\x66\xfc\x80\x80\x80\x81\x81"s},
        {"invalid-utf8-overlong-2-3", "\x63\xe0\x82\x80"s},
        {"invalid-utf8-overlong-2-4", "\x64\xf0\x80\x82\x80"s},
        {"invalid-utf8-overlong-2-5", "\x65\xf8\x80\x80\x82\x80"s},
        {"invalid-utf8-overlong-2-6", "\x66\xfc\x80\x80\x80\x82\x80"s},
        {"invalid-utf8-overlong-3-4", "\x64\xf0\x80\xa0\x80"s},
        {"invalid-utf8-overlong-3-5", "\x65\xf8\x80\x80\xa0\x80"s},
        {"invalid-utf8-overlong-3-6", "\x66\xfc\x80\x80\x80\xa0\x80"s},
        {"invalid-utf8-overlong-4-5", "\x65\xf8\x80\x84\x80\x80"s},
        {"invalid-utf8-overlong-4-6", "\x66\xfc\x80\x80\x84\x80\x80"s},
        {"invalid-utf8-split-u0080", "\x7f\x61\xc2\x61\x80\xff"s},
        {"invalid-utf8-split-u00ff", "\x7f\x61\xc3\x61\xbf\xff"s},
        {"invalid-utf8-split-u0100", "\x7f\x61\xc4\x61\x80\xff"s},
        {"invalid-utf8-split-u07ff", "\x7f\x61\xdf\x61\xbf\xff"s},
        {"invalid-utf8-split-u0800", "\x7f\x61\xe0\x62\xa0\x80\xff"s},
        {"invalid-utf8-split-u1000", "\x7f\x62\xe1\x80\x61\x80\xff"s},
        {"invalid-utf8-split-uffff", "\x7f\x61\xef\x62\xbf\xbf\xff"s},
        {"invalid-utf8-split-U00010000", "\x7f\x61\xf0\x63\x90\x80\x80\xff"s},
        {"invalid-utf8-split-U0010ffff", "\x7f\x62\xf4\x8f\x62\xbf\xbf\xff"s},
    };
    for (utf8_row const &r : rows) {
        CAPTURE(r.name);
        CHECK_EQ(outcome(cbor::diagnostic_notation(r.data)), error_text(no_error));
    }
}
