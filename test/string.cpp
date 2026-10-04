#include "binding.hpp"

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

std::string encoded_byte_string(std::string_view b)
{
    return encoded(value{bytes{std::string(b)}});
}

std::string encoded_text_string(std::string_view t)
{
    return encoded(value{std::string(t)});
}

std::string decoded_byte_string(std::string_view wire)
{
    auto const v = decoded(wire);
    REQUIRE(v.has_value());
    return std::get<bytes>(v->kind).b;
}

std::string decoded_text_string(std::string_view wire)
{
    auto const v = decoded(wire);
    REQUIRE(v.has_value());
    return std::get<std::string>(v->kind);
}

} // namespace

// Ported from test.rb: 'major 2: non-UTF-8 string encodes as major 2'.
// Which major a String gets is the binding's choice; the core encodes what it is told.
TEST_CASE("major 2: byte_string_encode writes major 2")
{
    CHECK_EQ(encoded_byte_string("\x00\xff\xfe\xfa"sv), "\x44\x00\xff\xfe\xfa"sv);
}

// Ported from test.rb: 'major 2: length-prefix widths (empty, short, long)'.
TEST_CASE("major 2: every length width survives encode and decode")
{
    for (std::string const &s : {""s, "\x00"s, std::string(30, '\xff'), std::string(300, '\xff')})
        CHECK_EQ(decoded_byte_string(encoded_byte_string(s)), s);
}

// Ported from test.rb: 'major 3: UTF-8 text encodes as major 3'.
TEST_CASE("major 3: text_string_encode writes major 3")
{
    CHECK_EQ(encoded_text_string("hello"sv), "\x65hello"sv);
}

// Ported from test.rb: 'major 3: empty / short / multibyte / long roundtrip'.
TEST_CASE("major 3: every length survives encode and decode")
{
    for (std::string const &s :
         {""s, "hello"s, "caf\xc3\xa9"s, std::string(24, 'a'), std::string(10000, 'x')})
        CHECK_EQ(decoded_text_string(encoded_text_string(s)), s);
}

// Ported from test.rb: 'major 3: decoder rejects invalid UTF-8 (TypeError)'.
// Only a build with simdutf checks UTF-8; RFC 8949 5.3.1 permits both.
TEST_CASE("major 3: invalid UTF-8")
{
#ifdef CBOR_SIMDUTF
    CHECK_EQ(decode_error("\x63\xff\xfe\xfd"sv), error::invalid_utf8_string);
#else
    CHECK_EQ(decoded_text_string("\x63\xff\xfe\xfd"sv), "\xff\xfe\xfd"s);
#endif
}

// Ported from the 'out of bounds' raises of decode_bytes and decode_text in src/mrb_cbor.c.
TEST_CASE("major 2 and 3: a length past the end is too_little_data")
{
    CHECK_EQ(decode_error("\x45\x01"sv), error::too_little_data);
    CHECK_EQ(decode_error("\x65\x61"sv), error::too_little_data);
}
