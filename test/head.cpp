#include "reference.hpp"
#include "rfc8949.hpp"

#include <cbor/sequence_reader.hpp>
#include <cbor/structs.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
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
    auto const v = decode_wire_into_value(wire);
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

// A Writer whose language gives one error for each append.
struct refusing_writer {
    std::errc refusal;

    refusing_writer &allocate(std::size_t)
    {
        return *this;
    }

    std::expected<void, std::errc> append(std::string_view)
    {
        return std::unexpected(refusal);
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
        CHECK_EQ(read_decode_error_of_wire(w), error::too_little_data);
}

// The error of the Writer reaches the caller as the member of the same name, because only the language knows what it
// means. A Writer error that has no member of its own must still fail, and never read as success.
TEST_CASE("head: encode returns the error of the Writer")
{
    auto const tree = cbor::decode("\x00"sv);
    REQUIRE(tree.has_value());
    struct {
        std::errc refusal;
        cbor::error expected;
    } const cases[] = {
        {std::errc::no_buffer_space, error::no_buffer_space},
        {std::errc::value_too_large, error::value_too_large},
        {std::errc::not_enough_memory, error::not_enough_memory},
        {std::errc::permission_denied, error::io_error},
    };
    for (auto const &c : cases)
        CHECK_EQ(cbor::encode(refusing_writer{c.refusal}, **tree), std::unexpected(c.expected));
}

struct one_simple_value {
    cbor::simple_value v;
};

// The initial byte is a finite set of 256 members, and RFC 8949 decides each one. These items are not well-formed
// (section 3 and Appendix C). One is a simple value below 32 in the two-byte form (f8 00 to f8 1f). One is the
// additional information 28 to 30 of every major type. One is the additional information 31 on major types 0, 1 and 6. A
// break code outside of an indefinite-length item (ff, 3.2.1). Every reader of the library gives syntax_error for each
// of them, with no other error and no value. The additional information 31 on major types 2 to 5 is an indefinite length
// (5.1 allows that). The library does not read it. decode and lazy give indefinite_length. Every other initial byte
// starts a well-formed head, so decode and lazy give neither of the two errors. A rule of a tag content or a repeated
// key may still give another error.
TEST_CASE("head: every initial byte gives the result that RFC 8949 gives it")
{
    // An argument of eight bytes is the widest (RFC 8949 3). A head with no content to read needs no more; the other
    // initial bytes get room for a short content.
    constexpr std::size_t widest_argument_bytes = 8;
    constexpr std::size_t room_after_head = 64;
    std::string const after(room_after_head, '\0');
    std::vector<std::string> not_well_formed;
    for (unsigned argument = 0; argument < test::rfc8949::additional_information_values; ++argument)
        not_well_formed.push_back(std::string("\xf8") + static_cast<char>(argument));
    for (unsigned major = 0; major < test::rfc8949::major_types; ++major)
        for (unsigned info = test::rfc8949::first_reserved_information; info <= test::rfc8949::last_reserved_information; ++info)
            not_well_formed.push_back(std::string(1, static_cast<char>(major << test::rfc8949::additional_information_bits | info)) +
                                      std::string(widest_argument_bytes, '\0'));
    for (unsigned const major : {0u, 1u, 6u, 7u})
        not_well_formed.push_back(std::string(1, static_cast<char>(major << test::rfc8949::additional_information_bits | test::rfc8949::indefinite_length)) +
                                  std::string(widest_argument_bytes, '\0'));
    REQUIRE_EQ(not_well_formed.size(), 60u);
    for (std::string const &bytes : not_well_formed) {
        CAPTURE(bytes);
        auto const length = cbor::item_size(bytes);
        CHECK_EQ(length.error(), cbor::error::syntax_error);
        auto const decoded = cbor::decode(bytes);
        CHECK_EQ(decoded.error(), cbor::error::syntax_error);
        CHECK_EQ(cbor::diagnostic_notation(bytes), std::unexpected(cbor::error::syntax_error));
        CHECK_EQ((cbor::at_path<"$", int>(bytes)), std::unexpected(cbor::error::syntax_error));
        CHECK_EQ((cbor::at_path<"$", cbor::simple_value>(bytes)), std::unexpected(cbor::error::syntax_error));
        auto const l = cbor::lazy::adopt(std::string(bytes));
        if (l) {
            auto const kind = l->type();
            CHECK_EQ(kind.error(), cbor::error::syntax_error);
            auto const read = l->get<cbor::simple_value>();
            CHECK_EQ(read.error(), cbor::error::syntax_error);
        } else {
            CHECK_EQ(l.error(), cbor::error::syntax_error);
        }
        cbor::sequence_reader r;
        r.feed(std::string_view{bytes});
        CHECK_EQ(r.next(), std::unexpected(cbor::error::syntax_error));
        CHECK_EQ(cbor::structs<one_simple_value>::decode("\xa1\x61v" + bytes), std::unexpected(cbor::error::syntax_error));
        CHECK_EQ(cbor::structs<one_simple_value>::decode("\xa1" + bytes + '\0'), std::unexpected(cbor::error::syntax_error));
    }
    auto const lazy_error = [](std::string const &bytes) {
        auto const l = cbor::lazy::adopt(std::string(bytes));
        if (!l)
            return l.error();
        auto const c = l->to_item();
        return c ? cbor::error{} : c.error();
    };
    for (unsigned const major : {2u, 3u, 4u, 5u}) {
        std::string const bytes =
            std::string(1, static_cast<char>(major << test::rfc8949::additional_information_bits | test::rfc8949::indefinite_length)) + after;
        CAPTURE(bytes);
        auto const tree = cbor::decode(bytes);
        CHECK_EQ(tree.error(), cbor::error::indefinite_length);
        CHECK_EQ(lazy_error(bytes), cbor::error::indefinite_length);
    }
    int others = 0;
    for (unsigned initial = 0; initial < test::rfc8949::initial_byte_values; ++initial) {
        if ((initial & (test::rfc8949::additional_information_values - 1)) >= test::rfc8949::first_reserved_information || initial == 0xf8)
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
