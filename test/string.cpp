#include "reference.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

std::string decoded_byte_string(std::string_view wire)
{
    auto const v = decode_wire_into_value(wire);
    REQUIRE(v.has_value());
    return std::get<bytes *>(v->kind)->b;
}

std::string decoded_text_string(std::string_view wire)
{
    auto const v = decode_wire_into_value(wire);
    REQUIRE(v.has_value());
    return std::string(v->text());
}

} // namespace

// Ported from test.rb: 'major 2: non-UTF-8 string encodes as major 2'.
// Which major a String gets is the choice of the application; the decode gives the major of the wire.
TEST_CASE("major 2: a byte string reads from major 2")
{
    CHECK_EQ(decoded_byte_string("\x44\x00\xff\xfe\xfa"sv), "\x00\xff\xfe\xfa"s);
}

// Ported from test.rb: 'major 2: length-prefix widths (empty, short, long)'.
// RFC 8949 3: a length under 24 stands in the initial byte, a longer one in the one or two bytes after it.
TEST_CASE("major 2: every length width decodes")
{
    struct {
        std::string wire;
        std::string payload;
    } const cases[] = {
        {"\x40"s, ""s},
        {"\x41\x00"s, "\x00"s},
        {"\x58\x1e"s + std::string(30, '\xff'), std::string(30, '\xff')},
        {"\x59\x01\x2c"s + std::string(300, '\xff'), std::string(300, '\xff')},
    };
    for (auto const &c : cases)
        CHECK_EQ(decoded_byte_string(c.wire), c.payload);
}

// Ported from test.rb: 'major 3: UTF-8 text encodes as major 3'.
TEST_CASE("major 3: a text string reads from major 3")
{
    CHECK_EQ(decoded_text_string("\x65hello"sv), "hello"s);
}

// Ported from test.rb: 'major 3: empty / short / multibyte / long roundtrip'.
// RFC 8949 3: the length counts bytes, so the multibyte text "caf\u00e9" has length 5.
TEST_CASE("major 3: every length decodes")
{
    struct {
        std::string wire;
        std::string text;
    } const cases[] = {
        {"\x60"s, ""s},
        {"\x65hello"s, "hello"s},
        {"\x65" "caf\xc3\xa9"s, "caf\xc3\xa9"s},
        {"\x78\x18"s + std::string(24, 'a'), std::string(24, 'a')},
        {"\x79\x27\x10"s + std::string(10000, 'x'), std::string(10000, 'x')},
    };
    for (auto const &c : cases)
        CHECK_EQ(decoded_text_string(c.wire), c.text);
}

// Ported from test.rb: 'major 3: decoder rejects invalid UTF-8 (TypeError)'.
// The library never checks UTF-8; the language of the application checks it. RFC 8949 5.3.1 permits this.
// So the bytes of a text string that is not UTF-8 pass through unchanged.
TEST_CASE("major 3: a text string that is not UTF-8 passes through unchanged")
{
    CHECK_EQ(decoded_text_string("\x63\xff\xfe\xfd"sv), "\xff\xfe\xfd"s);
}

// Ported from the 'out of bounds' raises of decode_bytes and decode_text in src/mrb_cbor.c.
TEST_CASE("major 2 and 3: a length past the end is too_little_data")
{
    CHECK_EQ(read_decode_error_of_wire("\x45\x01"sv), error::too_little_data);
    CHECK_EQ(read_decode_error_of_wire("\x65\x61"sv), error::too_little_data);
}
