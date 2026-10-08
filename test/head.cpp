#include "binding.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <system_error>

using namespace std::string_view_literals;
using cbor::error;

namespace
{

void check_value(std::string_view wire, value const &expected)
{
    auto const v = decoded(wire);
    REQUIRE(v.has_value());
    CHECK(*v == expected);
}

value U(std::uint64_t n)
{
    return {n};
}

value N(std::uint64_t argument)
{
    return {negative{argument}};
}

// A Writer whose language has no more space.
struct broken_writer {
    broken_writer &allocate(std::size_t)
    {
        return *this;
    }

    std::expected<void, std::errc> append(std::string_view)
    {
        return std::unexpected(std::errc::io_error);
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

struct full_writer {
    full_writer &allocate(std::size_t)
    {
        return *this;
    }

    std::expected<void, std::errc> append(std::string_view)
    {
        return std::unexpected(std::errc::not_enough_memory);
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

} // namespace

// Ported from test.rb: 'major 0: small-inline integers (info < 24) roundtrip'.
TEST_CASE("major 0: small-inline integers")
{
    check_value("\x00"sv, U(0));
    check_value("\x01"sv, U(1));
    check_value("\x0a"sv, U(10));
    check_value("\x17"sv, U(23));
}

// Ported from test.rb: 'major 0: uint8 / uint16 / uint32 / uint64 boundary'.
TEST_CASE("major 0: every width")
{
    check_value("\x18\x18"sv, U(24));
    check_value("\x18\xff"sv, U(255));
    check_value("\x19\x01\x00"sv, U(256));
    check_value("\x19\xff\xff"sv, U(65535));
    check_value("\x1a\x00\x01\x00\x00"sv, U(65536));
    check_value("\x1a\xff\xff\xff\xff"sv, U(0xffffffff));
    check_value("\x1b\x00\x00\x01\x00\x00\x00\x00\x00"sv, U(1ull << 40));
    check_value("\x1b\x80\x00\x00\x00\x00\x00\x00\x00"sv, U(1ull << 63));
}

// Ported from test.rb: 'major 0: uint64 max-plus-one wire decodes to 2^64-1'.
TEST_CASE("major 0: uint64 max")
{
    check_value("\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv, U(~0ull));
}

// Ported from test.rb: the major 1 tests. The argument n means -1 - n,
// and the binding computes that value, because only it knows its integer type.
TEST_CASE("major 1: every width and boundary")
{
    check_value("\x20"sv, N(0));
    check_value("\x37"sv, N(23));
    check_value("\x38\x18"sv, N(24));
    check_value("\x3a\xff\xff\xff\xff"sv, N(0xffffffff));
    check_value("\x3b\x80\x00\x00\x00\x00\x00\x00\x00"sv, N(1ull << 63));
    check_value("\x3b\xff\xff\xff\xff\xff\xff\xff\xff"sv, N(~0ull));
}

// RFC 8949 Appendix F: kind 2, too little data.
TEST_CASE("head: a truncated argument is too_little_data")
{
    for (auto const w :
         {""sv, "\x18"sv, "\x19\x01"sv, "\x1a\x00\x00\x00"sv, "\x1b\x00\x00\x00\x00\x00\x00\x00"sv})
        CHECK_EQ(decode_error(w), error::too_little_data);
}

// RFC 8949 Appendix F: reserved info 28 to 30, and info 31 with major 0, 1, 6 or 7.
TEST_CASE("head: reserved and misplaced info is syntax_error")
{
    for (auto const w : {"\x1c"sv, "\x1d"sv, "\x1e"sv, "\x1f"sv, "\x3f"sv, "\xdf"sv, "\xff"sv})
        CHECK_EQ(decode_error(w), error::syntax_error);
}

// RFC 8949 3.2: mruby-cbor rejects an indefinite length with NotImplementedError.
TEST_CASE("head: an indefinite length is indefinite_length")
{
    for (auto const w : {"\x5f"sv, "\x7f"sv, "\x9f"sv, "\xbf"sv})
        CHECK_EQ(decode_error(w), error::indefinite_length);
}

// Ported from test.rb: the major 0 roundtrip tests, here as the exact bytes of RFC 8949 4.1.
TEST_CASE("major 0: head_encode writes the shortest form")
{
    CHECK_EQ(encoded(U(0)), "\x00"sv);
    CHECK_EQ(encoded(U(23)), "\x17"sv);
    CHECK_EQ(encoded(U(24)), "\x18\x18"sv);
    CHECK_EQ(encoded(U(255)), "\x18\xff"sv);
    CHECK_EQ(encoded(U(256)), "\x19\x01\x00"sv);
    CHECK_EQ(encoded(U(65535)), "\x19\xff\xff"sv);
    CHECK_EQ(encoded(U(65536)), "\x1a\x00\x01\x00\x00"sv);
    CHECK_EQ(encoded(U(0xffffffff)), "\x1a\xff\xff\xff\xff"sv);
    CHECK_EQ(encoded(U(1ull << 40)), "\x1b\x00\x00\x01\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded(U(~0ull)), "\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// Ported from test.rb: the major 1 wire tests, in the other direction.
TEST_CASE("major 1: head_encode writes the shortest form")
{
    CHECK_EQ(encoded(N(0)), "\x20"sv);
    CHECK_EQ(encoded(N(24)), "\x38\x18"sv);
    CHECK_EQ(encoded(N(0xffffffff)), "\x3a\xff\xff\xff\xff"sv);
    CHECK_EQ(encoded(N(1ull << 63)), "\x3b\x80\x00\x00\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded(N(~0ull)), "\x3b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// Ported from test.rb: 'major 0: all powers of 2 up to 31 bits roundtrip', extended to 63 bits,
// because the argument is 64 bits wide here.
TEST_CASE("head: every power of 2 survives encode and decode")
{
    for (int i = 0; i < 64; ++i) {
        check_value(encoded(U(1ull << i)), U(1ull << i));
        check_value(encoded(N(1ull << i)), N(1ull << i));
    }
}

// The error of the Writer reaches the caller unchanged, because only the language knows what it means.
// The encoder collects the bytes in a block, so the error arrives when the block goes to the Writer.
TEST_CASE("head: encode returns the error of the Writer")
{
    full_writer w;
    test_binding binding;
    CHECK_EQ(cbor::encode<16>(binding, w, U(0)).error(), cbor::error{cbor::error::not_enough_memory});
}

// A Writer error that has no member of its own must still fail, and never read as success.
TEST_CASE("head: encode returns io_error for any other error of the Writer")
{
    broken_writer w;
    test_binding binding;
    CHECK_EQ(cbor::encode<16>(binding, w, U(0)).error(), cbor::error::io_error);
}

namespace
{

template <std::size_t DepthMax>
concept encode_compiles = requires(test_binding &b, string_writer &w, value const &v) { cbor::encode<DepthMax>(b, w, v); };

} // namespace

// DepthMax has an upper bound of 1024 on every form that takes it, so no form can be given a depth that overflows the
// stack.
TEST_CASE("head: encode takes a DepthMax of at most 1024")
{
    CHECK(encode_compiles<1024>);
    CHECK_FALSE(encode_compiles<1025>);
}
