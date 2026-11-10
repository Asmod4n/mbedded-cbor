#include "reference.hpp"

#include <cbor/sequence_reader.hpp>
#include <cbor/structs.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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
// and the application computes that value, because only it knows its integer type.
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

namespace
{

std::string tag_head_of(std::uint64_t const number)
{
    std::string out;
    cbor::encoder e{out};
    REQUIRE(e.tag(number).has_value());
    REQUIRE(e.finish().has_value());
    return out;
}

std::string array_head_of(std::uint64_t const count)
{
    std::string out;
    cbor::encoder e{out};
    REQUIRE(e.array(count).has_value());
    REQUIRE(e.finish().has_value());
    return out;
}

} // namespace

// Lengths and tag numbers take the shortest head of RFC 8949 4.1. The encoder writes an integer at a fixed
// width, tested below, so the shortest form is tested through the tag number and the count of an array.
// Ported from test.rb: the major 0 roundtrip tests, here as the exact bytes of RFC 8949 4.1 under major type 6.
TEST_CASE("tag: the head of a tag number takes the shortest form")
{
    CHECK_EQ(tag_head_of(0), "\xc0"sv);
    CHECK_EQ(tag_head_of(23), "\xd7"sv);
    CHECK_EQ(tag_head_of(24), "\xd8\x18"sv);
    CHECK_EQ(tag_head_of(255), "\xd8\xff"sv);
    CHECK_EQ(tag_head_of(256), "\xd9\x01\x00"sv);
    CHECK_EQ(tag_head_of(65535), "\xd9\xff\xff"sv);
    CHECK_EQ(tag_head_of(65536), "\xda\x00\x01\x00\x00"sv);
    CHECK_EQ(tag_head_of(0xffffffff), "\xda\xff\xff\xff\xff"sv);
    CHECK_EQ(tag_head_of(1ull << 40), "\xdb\x00\x00\x01\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(tag_head_of(~0ull), "\xdb\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// Ported from test.rb: the major 1 wire tests, in the other direction, here under major type 4.
TEST_CASE("array: the head of a count takes the shortest form")
{
    CHECK_EQ(array_head_of(0), "\x80"sv);
    CHECK_EQ(array_head_of(24), "\x98\x18"sv);
    CHECK_EQ(array_head_of(0xffffffff), "\x9a\xff\xff\xff\xff"sv);
    CHECK_EQ(array_head_of(1ull << 63), "\x9b\x80\x00\x00\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(array_head_of(~0ull), "\x9b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// The encoder writes an integer at its native width: the reference model gives std::uint64_t, so the head
// carries eight bytes, also for a small value.
TEST_CASE("major 0 and 1: the encoder writes an integer at its native width")
{
    CHECK_EQ(encoded(U(0)), "\x1b\x00\x00\x00\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded(N(0)), "\x3b\x00\x00\x00\x00\x00\x00\x00\x00"sv);
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
    CHECK_EQ(test::encode(w, U(0)).error(), cbor::error{cbor::error::not_enough_memory});
}

// A Writer error that has no member of its own must still fail, and never read as success.
TEST_CASE("head: encode returns io_error for any other error of the Writer")
{
    broken_writer w;
    CHECK_EQ(test::encode(w, U(0)).error(), cbor::error::io_error);
}

struct one_simple_value {
    cbor::simple_value v;
};

// The initial byte is a finite set of 256 members, and RFC 8949 decides each one. Not well-formed (section 3
// and Appendix C): a simple value below 32 in the two-byte form (f8 00 to f8 1f), the additional information
// 28 to 30 of every major type, the additional information 31 on major types 0, 1 and 6, and a break code
// outside of an indefinite-length item (ff, 3.2.1). Every reader of the library gives syntax_error for each
// of them, with no other error and no value. The additional information 31 on major types 2 to 5 is an
// indefinite length, which the library does not read (5.1 allows that): decode and lazy give
// indefinite_length. Every other initial byte starts a well-formed head, so decode and lazy give neither of
// the two errors; a rule of a tag content or a repeated key may still give another error.
TEST_CASE("head: every initial byte gives the result that RFC 8949 gives it")
{
    std::string const after(64, '\0');
    std::vector<std::string> not_well_formed;
    for (int argument = 0; argument < 32; ++argument)
        not_well_formed.push_back(std::string("\xf8") + static_cast<char>(argument));
    for (int major = 0; major < 8; ++major)
        for (int info = 28; info <= 30; ++info)
            not_well_formed.push_back(std::string(1, static_cast<char>(major << 5 | info)) +
                                      std::string(8, '\0'));
    for (int const major : {0, 1, 6, 7})
        not_well_formed.push_back(std::string(1, static_cast<char>(major << 5 | 31)) + std::string(8, '\0'));
    REQUIRE_EQ(not_well_formed.size(), 60u);
    for (std::string const &bytes : not_well_formed) {
        CAPTURE(bytes);
        CHECK_EQ(cbor::item_size(bytes).error(), cbor::error::syntax_error);
        CHECK_EQ(cbor::decode(bytes).error(), cbor::error::syntax_error);
        CHECK_EQ(cbor::diagnostic_notation(bytes).error(), cbor::error::syntax_error);
        CHECK_EQ((cbor::at_path<"$", int>(bytes)).error(), cbor::error::syntax_error);
        CHECK_EQ((cbor::at_path<"$", cbor::simple_value>(bytes)).error(), cbor::error::syntax_error);
        auto const l = cbor::lazy::adopt(std::string(bytes));
        if (l) {
            CHECK_EQ(l->type().error(), cbor::error::syntax_error);
            CHECK_EQ(l->get<cbor::simple_value>().error(), cbor::error::syntax_error);
        } else {
            CHECK_EQ(l.error(), cbor::error::syntax_error);
        }
        cbor::sequence_reader r;
        r.feed(std::string_view{bytes});
        CHECK_EQ(r.next().error(), cbor::error::syntax_error);
        CHECK_EQ(cbor::structs<one_simple_value>::decode("\xa1\x61v" + bytes).error(), cbor::error::syntax_error);
        CHECK_EQ(cbor::structs<one_simple_value>::decode("\xa1" + bytes + '\0').error(), cbor::error::syntax_error);
    }
    auto const lazy_error = [](std::string const &bytes) {
        auto const l = cbor::lazy::adopt(std::string(bytes));
        if (!l)
            return l.error();
        auto const c = l->clone();
        return c ? cbor::error{} : c.error();
    };
    for (int const major : {2, 3, 4, 5}) {
        std::string const bytes = std::string(1, static_cast<char>(major << 5 | 31)) + after;
        CAPTURE(bytes);
        CHECK_EQ(cbor::decode(bytes).error(), cbor::error::indefinite_length);
        CHECK_EQ(lazy_error(bytes), cbor::error::indefinite_length);
    }
    int others = 0;
    for (int initial = 0; initial < 256; ++initial) {
        if ((initial & 31) >= 28 || initial == 0xf8)
            continue;
        ++others;
        std::string const bytes = std::string(1, static_cast<char>(initial)) + after;
        CAPTURE(initial);
        auto const d = cbor::decode(bytes);
        cbor::error const decoded_error = d ? cbor::error{} : d.error();
        CHECK_NE(decoded_error, cbor::error::syntax_error);
        CHECK_NE(decoded_error, cbor::error::indefinite_length);
        CHECK_NE(lazy_error(bytes), cbor::error::syntax_error);
        CHECK_NE(lazy_error(bytes), cbor::error::indefinite_length);
    }
    CHECK_EQ(others, 223);
}
