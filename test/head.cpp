#include <cbor/internal/head.hpp>
#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>

using namespace std::string_view_literals;
using cbor::internal::error;

namespace
{

auto head_of(std::string_view wire)
{
    return cbor::internal::decoder{wire}.head_decode();
}

void check_head(std::string_view wire, std::uint8_t major, std::uint64_t argument)
{
    auto const h = head_of(wire);
    REQUIRE(h.has_value());
    CHECK_EQ(h->major, major);
    CHECK_EQ(h->argument, argument);
}

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

    std::string done(std::size_t size)
    {
        bytes.resize(size);
        return bytes;
    }
};

// A Writer whose language has no more space.
struct full_writer {
    std::expected<void, std::errc> append(std::string_view)
    {
        return std::unexpected(std::errc::not_enough_memory);
    }
};

std::string encoded(std::uint8_t major, std::uint64_t argument)
{
    string_writer w;
    cbor::internal::encoder<string_writer> e{w};
    REQUIRE(e.head_encode(major, argument).has_value());
    return w.done(w.bytes.size());
}

} // namespace

// Ported from test.rb: 'major 0: small-inline integers (info < 24) roundtrip'.
TEST_CASE("major 0: small-inline integers")
{
    check_head("\x00"sv, 0, 0);
    check_head("\x01"sv, 0, 1);
    check_head("\x0a"sv, 0, 10);
    check_head("\x17"sv, 0, 23);
}

// Ported from test.rb: 'major 0: uint8 / uint16 / uint32 / uint64 boundary'.
TEST_CASE("major 0: every width")
{
    check_head("\x18\x18"sv, 0, 24);
    check_head("\x18\xff"sv, 0, 255);
    check_head("\x19\x01\x00"sv, 0, 256);
    check_head("\x19\xff\xff"sv, 0, 65535);
    check_head("\x1a\x00\x01\x00\x00"sv, 0, 65536);
    check_head("\x1a\xff\xff\xff\xff"sv, 0, 0xffffffff);
    check_head("\x1b\x00\x00\x01\x00\x00\x00\x00\x00"sv, 0, 1ull << 40);
    check_head("\x1b\x80\x00\x00\x00\x00\x00\x00\x00"sv, 0, 1ull << 63);
}

// Ported from test.rb: 'major 0: uint64 max-plus-one wire decodes to 2^64-1'.
TEST_CASE("major 0: uint64 max")
{
    check_head("\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv, 0, ~0ull);
}

// Ported from test.rb: the major 1 tests. The argument n means -1 - n,
// and the binding computes that value, because only it knows its integer type.
TEST_CASE("major 1: every width and boundary")
{
    check_head("\x20"sv, 1, 0);
    check_head("\x37"sv, 1, 23);
    check_head("\x38\x18"sv, 1, 24);
    check_head("\x3a\xff\xff\xff\xff"sv, 1, 0xffffffff);
    check_head("\x3b\x80\x00\x00\x00\x00\x00\x00\x00"sv, 1, 1ull << 63);
    check_head("\x3b\xff\xff\xff\xff\xff\xff\xff\xff"sv, 1, ~0ull);
}

// RFC 8949 Appendix F: kind 2, too little data.
TEST_CASE("head: a truncated argument is too_little_data")
{
    for (auto const w :
         {""sv, "\x18"sv, "\x19\x01"sv, "\x1a\x00\x00\x00"sv, "\x1b\x00\x00\x00\x00\x00\x00\x00"sv})
        CHECK_EQ(head_of(w).error(), error::too_little_data);
}

// RFC 8949 Appendix F: reserved info 28 to 30, and info 31 with major 0, 1, 6 or 7.
TEST_CASE("head: reserved and misplaced info is syntax_error")
{
    for (auto const w : {"\x1c"sv, "\x1d"sv, "\x1e"sv, "\x1f"sv, "\x3f"sv, "\xdf"sv, "\xff"sv})
        CHECK_EQ(head_of(w).error(), error::syntax_error);
}

// RFC 8949 3.2: mruby-cbor rejects an indefinite length with NotImplementedError.
TEST_CASE("head: an indefinite length is indefinite_length")
{
    for (auto const w : {"\x5f"sv, "\x7f"sv, "\x9f"sv, "\xbf"sv})
        CHECK_EQ(head_of(w).error(), error::indefinite_length);
}

// Ported from test.rb: the major 0 roundtrip tests, here as the exact bytes of RFC 8949 4.1.
TEST_CASE("major 0: head_encode writes the shortest form")
{
    CHECK_EQ(encoded(0, 0), "\x00"sv);
    CHECK_EQ(encoded(0, 23), "\x17"sv);
    CHECK_EQ(encoded(0, 24), "\x18\x18"sv);
    CHECK_EQ(encoded(0, 255), "\x18\xff"sv);
    CHECK_EQ(encoded(0, 256), "\x19\x01\x00"sv);
    CHECK_EQ(encoded(0, 65535), "\x19\xff\xff"sv);
    CHECK_EQ(encoded(0, 65536), "\x1a\x00\x01\x00\x00"sv);
    CHECK_EQ(encoded(0, 0xffffffff), "\x1a\xff\xff\xff\xff"sv);
    CHECK_EQ(encoded(0, 1ull << 40), "\x1b\x00\x00\x01\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded(0, ~0ull), "\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// Ported from test.rb: the major 1 wire tests, in the other direction.
TEST_CASE("major 1: head_encode writes the shortest form")
{
    CHECK_EQ(encoded(1, 0), "\x20"sv);
    CHECK_EQ(encoded(1, 24), "\x38\x18"sv);
    CHECK_EQ(encoded(1, 0xffffffff), "\x3a\xff\xff\xff\xff"sv);
    CHECK_EQ(encoded(1, 1ull << 63), "\x3b\x80\x00\x00\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded(1, ~0ull), "\x3b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// Ported from test.rb: 'major 0: all powers of 2 up to 31 bits roundtrip', extended to 63 bits,
// because the argument is 64 bits wide here.
TEST_CASE("head: every power of 2 survives head_encode and head_decode")
{
    for (std::uint8_t major : {0, 1})
        for (int i = 0; i < 64; ++i)
            check_head(encoded(major, 1ull << i), major, 1ull << i);
}

// The error of the Writer reaches the caller unchanged, because only the language knows what it means.
TEST_CASE("head: head_encode returns the error of the Writer")
{
    full_writer w;
    cbor::internal::encoder<full_writer> e{w};
    CHECK_EQ(e.head_encode(0, 0).error(), std::errc::not_enough_memory);
}
