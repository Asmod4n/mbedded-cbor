#include <cbor/internal/head.hpp>
#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::internal::error;

namespace
{

// The Writer of a test: the output lives in a std::string, as a binding keeps it in the string of its
// language.
struct string_writer {
    std::string bytes;

    std::expected<void, std::errc> reserve(std::size_t size)
    {
        bytes.reserve(bytes.size() + size);
        return {};
    }

    std::expected<void, std::errc> append(std::string_view part)
    {
        bytes.append(part);
        return {};
    }
};

std::string encoded_byte_string(std::string_view bytes)
{
    string_writer w;
    cbor::internal::encoder<string_writer> e{w};
    REQUIRE(e.byte_string_encode(bytes).has_value());
    return w.bytes;
}

std::string encoded_text_string(std::string_view text)
{
    string_writer w;
    cbor::internal::encoder<string_writer> e{w};
    REQUIRE(e.text_string_encode(text).has_value());
    return w.bytes;
}

std::expected<std::string_view, error> byte_string_of(std::string_view wire)
{
    cbor::internal::decoder d{wire};
    auto const h = d.head_decode();
    REQUIRE(h.has_value());
    REQUIRE_EQ(h->major, 2);
    return d.byte_string_decode(h->argument);
}

std::expected<std::string_view, error> text_string_of(std::string_view wire)
{
    cbor::internal::decoder d{wire};
    auto const h = d.head_decode();
    REQUIRE(h.has_value());
    REQUIRE_EQ(h->major, 3);
    return d.text_string_decode(h->argument);
}

std::string decoded_byte_string(std::string_view wire)
{
    auto const s = byte_string_of(wire);
    REQUIRE(s.has_value());
    return std::string(*s);
}

std::string decoded_text_string(std::string_view wire)
{
    auto const s = text_string_of(wire);
    REQUIRE(s.has_value());
    return std::string(*s);
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
#if CBOR_SIMDUTF
    CHECK_EQ(text_string_of("\x63\xff\xfe\xfd"sv).error(), error::invalid_utf8_string);
#else
    CHECK_EQ(*text_string_of("\x63\xff\xfe\xfd"sv), "\xff\xfe\xfd"sv);
#endif
}

// Ported from the 'out of bounds' raises of decode_bytes and decode_text in src/mrb_cbor.c.
TEST_CASE("major 2 and 3: a length past the end is too_little_data")
{
    CHECK_EQ(byte_string_of("\x45\x01"sv).error(), error::too_little_data);
    CHECK_EQ(text_string_of("\x65\x61"sv).error(), error::too_little_data);
}
