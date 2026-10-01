#include <cbor/internal/head.hpp>
#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <string_view>

using namespace std::string_view_literals;
using cbor::internal::error;

namespace
{

auto head_of(std::string_view wire)
{
    return cbor::internal::reader{wire}.head_read();
}

void check_head(std::string_view wire, std::uint8_t major, std::uint64_t argument)
{
    auto const h = head_of(wire);
    REQUIRE(h.has_value());
    CHECK_EQ(h->major, major);
    CHECK_EQ(h->argument, argument);
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
