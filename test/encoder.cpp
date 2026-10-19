#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

#include "reference.hpp"

namespace encoder_test
{

using namespace std::string_literals;
using namespace std::string_view_literals;

// The bytes that one call of the encoder writes into a std::string.
template <class Write>
std::string written(Write const &write)
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(write(e).has_value());
    REQUIRE(e.finish().has_value());
    return out;
}

template <class T>
concept unsigned_integer_accepts = requires(cbor::encoder<std::string> &e, T const v) { e.unsigned_integer(v); };

template <class T>
concept negative_integer_accepts = requires(cbor::encoder<std::string> &e, T const v) { e.negative_integer(v); };

// RFC 8949 3.1: major type 0 and 1 carry the argument in the head. The encoder writes it in the width of the
// type the caller gives, as the owner decided for every encoder: one byte for std::uint8_t, eight for
// std::uint64_t, even when a shorter head would hold the value.
TEST_CASE("encoder: an integer is written in the width of its type")
{
    CHECK_EQ(written([](auto &e) { return e.unsigned_integer(std::uint8_t{5}); }), "\x18\x05"sv);
    CHECK_EQ(written([](auto &e) { return e.unsigned_integer(std::uint16_t{5}); }), "\x19\x00\x05"sv);
    CHECK_EQ(written([](auto &e) { return e.unsigned_integer(std::uint32_t{5}); }), "\x1a\x00\x00\x00\x05"sv);
    CHECK_EQ(written([](auto &e) { return e.unsigned_integer(std::uint64_t{5}); }),
             "\x1b\x00\x00\x00\x00\x00\x00\x00\x05"sv);
    CHECK_EQ(written([](auto &e) { return e.negative_integer(std::uint8_t{0}); }), "\x38\x00"sv);
    CHECK_EQ(written([](auto &e) { return e.negative_integer(std::uint16_t{499}); }), "\x39\x01\xf3"sv);
}

// A bool and a character type are no integers of the data model; a call with one does not compile.
TEST_CASE("encoder: a bool or a character type is no integer argument")
{
    CHECK(unsigned_integer_accepts<std::uint8_t>);
    CHECK(unsigned_integer_accepts<unsigned long long>);
    CHECK_FALSE(unsigned_integer_accepts<bool>);
    CHECK_FALSE(unsigned_integer_accepts<char>);
    CHECK_FALSE(unsigned_integer_accepts<wchar_t>);
    CHECK_FALSE(unsigned_integer_accepts<char8_t>);
    CHECK_FALSE(unsigned_integer_accepts<char16_t>);
    CHECK_FALSE(unsigned_integer_accepts<char32_t>);
    CHECK_FALSE(negative_integer_accepts<bool>);
    CHECK_FALSE(negative_integer_accepts<char>);
    CHECK_FALSE(negative_integer_accepts<char32_t>);
}

// RFC 8949 3.1: a length, a count and a tag number take the shortest head.
TEST_CASE("encoder: strings, arrays, maps and tags take the shortest head")
{
    std::array<std::byte, 2> const bytes{std::byte{1}, std::byte{2}};
    CHECK_EQ(written([&](auto &e) { return e.byte_string(std::span<std::byte const>(bytes)); }), "\x42\x01\x02"sv);
    CHECK_EQ(written([](auto &e) { return e.text_string("IETF"sv); }), "\x64IETF"sv);
    CHECK_EQ(written([](auto &e) { return e.array(3); }), "\x83"sv);
    CHECK_EQ(written([](auto &e) { return e.map(24); }), "\xb8\x18"sv);
    CHECK_EQ(written([](auto &e) { return e.tag(1); }), "\xc1"sv);
    CHECK_EQ(written([](auto &e) { return e.tag(1000); }), "\xd9\x03\xe8"sv);
}

// RFC 8949 3.3: simple values 20 to 23 and 0 to 19 stand in the initial byte, 32 to 255 in one more byte.
// The one-byte form of a value below 32 is not well-formed, so the encoder refuses 24 to 31.
TEST_CASE("encoder: simple values")
{
    CHECK_EQ(written([](auto &e) { return e.simple_value(cbor::simple_value::false_value); }), "\xf4"sv);
    CHECK_EQ(written([](auto &e) { return e.simple_value(cbor::simple_value::null); }), "\xf6"sv);
    CHECK_EQ(written([](auto &e) { return e.simple_value(static_cast<cbor::simple_value>(16)); }), "\xf0"sv);
    CHECK_EQ(written([](auto &e) { return e.simple_value(static_cast<cbor::simple_value>(255)); }), "\xf8\xff"sv);
    for (int v = 24; v < 32; ++v) {
        std::string out;
        cbor::encoder e(out);
        auto const r = e.simple_value(static_cast<cbor::simple_value>(v));
        REQUIRE_FALSE(r.has_value());
        CHECK_EQ(r.error(), cbor::error::reserved_simple_value);
    }
}

// RFC 8949 3.3: a float is written in the width of its type: 1.5 as float32 0x3fc00000, as float64
// 0x3ff8000000000000 and as float16 0x3e00.
TEST_CASE("encoder: a float is written in the width of its type")
{
    CHECK_EQ(written([](auto &e) { return e.floating_point(1.5f); }), "\xfa\x3f\xc0\x00\x00"sv);
    CHECK_EQ(written([](auto &e) { return e.floating_point(1.5); }), "\xfb\x3f\xf8\x00\x00\x00\x00\x00\x00"sv);
#if defined(__STDCPP_FLOAT16_T__)
    CHECK_EQ(written([](auto &e) { return e.floating_point(1.5f16); }), "\xf9\x3e\x00"sv);
#endif
}

// RFC 8746 2: a typed array is its tag over a byte string whose length is a multiple of the element size.
// Tag 76 is reserved, and a length that is no multiple of the element size is not admitted.
TEST_CASE("encoder: a typed array")
{
    std::array<std::byte, 4> const bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    CHECK_EQ(written([&](auto &e) { return e.typed_array(cbor::typed_array{64, bytes}); }),
             "\xd8\x40\x44\x01\x02\x03\x04"sv);
    CHECK_EQ(written([&](auto &e) { return e.typed_array(cbor::typed_array{70, bytes}); }),
             "\xd8\x46\x44\x01\x02\x03\x04"sv);
    {
        std::string out;
        cbor::encoder e(out);
        CHECK_EQ(e.typed_array(cbor::typed_array{76, bytes}).error(), cbor::error::unsupported_value);
    }
    std::string out;
    cbor::encoder e(out);
    CHECK_EQ(e.typed_array(cbor::typed_array{67, std::span(bytes).first(2)}).error(),
             cbor::error::inadmissible_type_for_tag_content);
}

// A whole data item is a sequence of calls: [1, {"a": h'01'}, 2("big")] with the counts that the caller gives.
// The encoder keeps no count; the bytes are what RFC 8949 3.1 gives for each head in order.
TEST_CASE("encoder: a data item is a sequence of calls")
{
    std::array<std::byte, 1> const one{std::byte{1}};
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.array(3).has_value());
    REQUIRE(e.unsigned_integer(std::uint8_t{1}).has_value());
    REQUIRE(e.map(1).has_value());
    REQUIRE(e.text_string("a"sv).has_value());
    REQUIRE(e.byte_string(std::span<std::byte const>(one)).has_value());
    REQUIRE(e.tag(2).has_value());
    REQUIRE(e.text_string(std::string("big")).has_value());
    auto const size = e.finish();
    REQUIRE(size.has_value());
    CHECK_EQ(*size, out.size());
    CHECK_EQ(out, "\x83\x18\x01\xa1\x61" "a\x41\x01\xc2\x63" "big"sv);
}

// The span target writes into memory of the caller. A head that does not fit gives no_buffer_space, and the
// encoder is then unusable: every later call is a wrong use and throws std::logic_error.
TEST_CASE("encoder: a span that is too short, then a call after the failure")
{
    std::array<char, 3> buffer{};
    cbor::encoder e{std::span<char>(buffer)};
    REQUIRE(e.array(1).has_value());
    auto const r = e.unsigned_integer(std::uint64_t{1});
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::no_buffer_space);
    CHECK_THROWS_AS((void)e.unsigned_integer(std::uint8_t{1}), std::logic_error);
    CHECK_THROWS_AS((void)e.finish(), std::logic_error);
}

// finish returns the bytes written into the span; a call after finish is a wrong use.
TEST_CASE("encoder: finish gives the size, and a call after it throws")
{
    std::array<char, 16> buffer{};
    cbor::encoder e{std::span<char>(buffer)};
    REQUIRE(e.text_string("abc"sv).has_value());
    auto const size = e.finish();
    REQUIRE(size.has_value());
    CHECK_EQ(*size, 4u);
    CHECK_EQ(std::string_view(buffer.data(), *size), "\x63" "abc"sv);
    CHECK_THROWS_AS((void)e.array(0), std::logic_error);
}

// The iov target gives pieces: the heads and short strings in blocks of the encoder, a long string whose
// owner the caller handed over as a piece of its own with no copy. Joined, the pieces are the bytes of the
// string target.
TEST_CASE("encoder: the iov target gives the bytes of the string target in pieces")
{
    auto const text = std::make_shared<std::string const>(std::string(40, 'x'));
    cbor::iov_writer writer;
    cbor::encoder e(writer);
    REQUIRE(e.array(2).has_value());
    REQUIRE(e.text_string(text).has_value());
    REQUIRE(e.unsigned_integer(std::uint8_t{7}).has_value());
    auto const pieces = e.finish();
    REQUIRE(pieces.has_value());
    std::string joined;
    bool referenced = false;
    for (std::string_view const piece : **pieces) {
        joined.append(piece);
        referenced = referenced || piece.data() == text->data();
    }
    CHECK(referenced);
    CHECK_EQ(joined, "\x82\x78\x28"s + *text + "\x18\x07"s);
}

}
