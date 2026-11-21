#include "reference.hpp"
#include "owned_copy.hpp"
#include "pieces.hpp"
#include <cbor/diagnostic_notation.hpp>

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace
{

constexpr std::size_t copybreak = CBOR_ENCODER_COPYBREAK;
// RFC 8949 section 3: the longest head is the initial byte and an argument of 8 bytes.
constexpr std::size_t longest_head_size = 1 + 8;
using pieces = cbor::ref<std::span<iovec const>>;

std::string join_pieces_into_bytes(pieces const &p)
{
    auto const bytes = test::join_pieces_into_bytes(*p);
    REQUIRE(bytes.has_value());
    return *bytes;
}

std::size_t sum_piece_lengths(pieces const &p)
{
    return test::sum_piece_lengths(*p);
}

bool points_into(pieces const &p, void const *const data)
{
    return std::ranges::any_of(*p, [data](iovec const &piece) { return static_cast<void const *>(piece.iov_base) == data; });
}

// RFC 8949 3: a head with an argument of eight bytes. cbor::encode keeps that width, so the offsets of the input are
// the offsets of the output.
std::string wide_head(cbor::major_type const major, std::uint64_t const argument)
{
    return test::write_integer_at_native_width(major, argument);
}

std::string wide_text(std::string_view const text)
{
    return wide_head(cbor::major_type::text_string, text.size()) + std::string(text);
}

// The pieces of encode_iov of the tree of `wire`, joined, give `wire` again.
template <bool refer>
void check_pieces_give_the_wire(std::string const &wire)
{
    auto const tree = cbor::decode(wire);
    REQUIRE(tree.has_value());
    auto const finished = refer ? cbor::encode_iov(*tree) : cbor::encode_iov(**tree);
    REQUIRE(finished.has_value());
    CHECK_EQ(join_pieces_into_bytes(*finished), wire);
    CHECK_EQ(sum_piece_lengths(*finished), wire.size());
}

} // namespace

// A head and a float write nine bytes before they count their own size. At each of the nine offsets before the end of a
// block, encode_iov must take a new block in time and lose no byte. The overload for item copies every string,
// so the text before the head fills the block. A tag and an integer each test the space left on their own path.
TEST_CASE("encode_iov: a head and a float at each offset before the end of a block")
{
    std::string const wide_number = wide_head(cbor::major_type::unsigned_integer, std::uint64_t{1} << 40);
    std::string const wide_tag = wide_head(cbor::major_type::tag, std::uint64_t{1} << 40);
    std::string const float_item = "\xfb\x3f\xb9\x99\x99\x99\x99\x99\x9a";
    for (bool const tag : {false, true}) {
        for (std::size_t left = 1; left <= longest_head_size; ++left) {
            CAPTURE(tag);
            CAPTURE(left);
            std::size_t const fill = CBOR_ENCODER_BLOCK_SIZE - left - longest_head_size - 1;
            std::string const text = wide_text(std::string(fill, 'f'));
            std::string const wire = tag ? "\x83"s + text + wide_tag + text + float_item
                                         : "\x84"s + text + wide_number + text + float_item;
            check_pieces_give_the_wire<false>(wire);
        }
    }
}

// A text can fill the free space of a block exactly. It can also miss it by one byte either way. There the copy splits
// across two blocks.
TEST_CASE("encode_iov: texts at the free space of a block")
{
    std::size_t const free = CBOR_ENCODER_BLOCK_SIZE - longest_head_size - 1;
    for (std::size_t const size : {free - 1, free, free + 1}) {
        CAPTURE(size);
        check_pieces_give_the_wire<false>("\x82"s + wide_text(std::string(size, 's')) + "\x07"s);
    }
}

// A reference closes the open part of the block first. The order of the pieces must be the order of the bytes. This
// holds also when a reference follows a full block and when a block follows a reference. The fill holds small integers
// and no string, so it is copied also where the copy limit is 0.
TEST_CASE("encode_iov: the order of blocks and references")
{
    std::size_t const ones = CBOR_ENCODER_BLOCK_SIZE - longest_head_size - 1;
    std::string const fill = wide_head(cbor::major_type::array, ones) + std::string(ones, '\x01');
    std::string const text = wide_text(std::string(copybreak + 1, 'r'));
    check_pieces_give_the_wire<true>("\x84"s + fill + text + "\xc9"s + text + fill);
}

// writev takes at most IOV_MAX pieces in one call. test::joined reads an encoding with more pieces back in turns. It is
// the one place that splits. 1100 texts above the copy limit give more pieces than the 1024 of Linux.
TEST_CASE("encode_iov: more pieces than IOV_MAX are written in turns")
{
    constexpr std::size_t strings = 1100;
    std::string const wire =
        wide_head(cbor::major_type::array, strings) + test::repeat(wide_text(std::string(copybreak + 1, 'x')), strings);
    auto const tree = cbor::decode(wire);
    REQUIRE(tree.has_value());
    auto finished = cbor::encode_iov(*tree);
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_GT(p->size(), strings);
    CHECK_EQ(join_pieces_into_bytes(p), wire);
}

// The pieces are made for sending: sendmsg over a pair of Unix sockets carries the bytes of encode. The message
// stays below the 4 KiB that one socket buffer holds at least, so the single thread does not block.
TEST_CASE("encode_iov: sendmsg sends the bytes of encode")
{
    constexpr std::size_t socket_buffer_least = 4096;
    std::string const expected = "\x82\x78\x64"s + std::string(100, 'y') + "\x07"s;
    auto const tree = cbor::decode(expected);
    REQUIRE(tree.has_value());
    auto finished = cbor::encode_iov(*tree);
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    REQUIRE_LT(expected.size(), socket_buffer_least);
    std::array<int, 2> ends{};
    REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ends.data()), 0);
    msghdr message{};
    message.msg_iov = const_cast<iovec *>(p->data());
    auto const count = test::count_pieces<decltype(message.msg_iovlen)>(p->size());
    REQUIRE(count.has_value());
    message.msg_iovlen = *count;
    ssize_t const sent = sendmsg(ends[0], &message, 0);
    REQUIRE_EQ(sent, static_cast<ssize_t>(expected.size()));
    std::string received(expected.size(), '\0');
    REQUIRE_EQ(read(ends[1], received.data(), received.size()), static_cast<ssize_t>(expected.size()));
    CHECK_EQ(received, expected);
    close(ends[0]);
    close(ends[1]);
}

namespace
{

std::string array_of_text_bytes_and_seven(std::size_t const size)
{
    return "\x83"s + wide_text(std::string(size, 't')) + wide_head(cbor::major_type::byte_string, size) +
           std::string(size, 'b') + "\x07"s;
}

std::vector<std::size_t> sizes_around_copybreak()
{
    return {0uz, std::max(copybreak, 1uz) - 1, copybreak, copybreak + 1, CBOR_ENCODER_BLOCK_SIZE + 1uz};
}

} // namespace

// The overload for a tree that cbor::decode gave refers to its long strings and copies its short ones. Both overloads
// must still send the bytes of cbor::encode, because a receiver sees only the bytes. Sizes around the copy limit and
// past one block cover both paths and the change of block.
TEST_CASE("encode_iov: both overloads give the bytes of encode for a decoded tree")
{
    for (std::size_t const size : sizes_around_copybreak()) {
        CAPTURE(size);
        std::string const input = array_of_text_bytes_and_seven(size);
        auto const tree = cbor::decode(input);
        REQUIRE(tree.has_value());
        std::string expected;
        REQUIRE(cbor::encode(expected, **tree).has_value());
        auto const copied_pieces = cbor::encode_iov(**tree);
        REQUIRE(copied_pieces.has_value());
        CHECK_EQ(join_pieces_into_bytes(*copied_pieces), expected);
        auto const referred_pieces = cbor::encode_iov(*tree);
        REQUIRE(referred_pieces.has_value());
        CHECK_EQ(join_pieces_into_bytes(*referred_pieces), expected);
    }
}

// A string longer than the copy limit must be a piece that points at the bytes that the tree holds, or the overload
// copies and gains nothing. A string at the limit must not point there, or the limit is not the one that holds. The
// overload for item copies every string, so no piece of it points into the tree.
TEST_CASE("encode_iov: a decoded tree is referred to above the copy limit and copied up to it")
{
    for (std::size_t const size : sizes_around_copybreak()) {
        CAPTURE(size);
        std::string const input = array_of_text_bytes_and_seven(size);
        auto const tree = cbor::decode(input);
        REQUIRE(tree.has_value());
        auto const elements = (**tree).elements();
        REQUIRE(elements.has_value());
        REQUIRE_EQ(elements->size(), 3uz);
        auto const text = elements->front().get<std::string_view>();
        auto const bytes = elements->next(1).front().get<std::span<std::byte const>>();
        REQUIRE(text.has_value());
        REQUIRE(bytes.has_value());
        auto const referred = cbor::encode_iov(*tree);
        REQUIRE(referred.has_value());
        bool const long_string = size > copybreak && size > 0;
        CHECK_EQ(points_into(*referred, text->data()), long_string);
        CHECK_EQ(points_into(*referred, bytes->data()), long_string);
        auto const copied_pieces = cbor::encode_iov(**tree);
        REQUIRE(copied_pieces.has_value());
        CHECK_FALSE(points_into(*copied_pieces, text->data()));
        CHECK_FALSE(points_into(*copied_pieces, bytes->data()));
    }
}

// The pieces point into the tree, so the result must keep the tree alive when the caller has dropped it. A build with
// AddressSanitizer reports the read if it does not.
TEST_CASE("encode_iov: the pieces of a decoded tree outlive the tree")
{
    std::size_t const size = copybreak + CBOR_ENCODER_BLOCK_SIZE;
    std::string const input = array_of_text_bytes_and_seven(size);
    std::string expected;
    std::optional<pieces> kept;
    {
        auto tree = cbor::decode(input);
        REQUIRE(tree.has_value());
        REQUIRE(cbor::encode(expected, **tree).has_value());
        auto referred = cbor::encode_iov(*tree);
        REQUIRE(referred.has_value());
        kept = std::move(*referred);
    }
    REQUIRE(kept.has_value());
    CHECK_EQ(join_pieces_into_bytes(*kept), expected);
}

// A tree with a shared_reference is written through the search for cycles, not through the walk of a plain tree. That
// search must refer and copy as the walk does. The text stands in a cycle 28([text, 29(0)]) and is written once.
TEST_CASE("encode_iov: the overload for a decoded tree with a shared_reference refers to long strings")
{
    for (std::size_t const size : sizes_around_copybreak()) {
        CAPTURE(size);
        std::string const text_edn = "\"" + std::string(size, 'a') + "\"";
        std::string const encoded = cbor::diag2cbor("28([" + text_edn + ", 29(0)])").value();
        auto const tree = cbor::decode_cycles(encoded);
        REQUIRE(tree.has_value());
        auto const referred = cbor::encode_iov(*tree);
        REQUIRE(referred.has_value());
        CHECK_EQ(join_pieces_into_bytes(*referred), encoded);
        auto const text = (**tree).elements().value().front().get<std::string_view>();
        REQUIRE(text.has_value());
        CHECK_EQ(points_into(*referred, text->data()), size > copybreak && size > 0);
    }
}
