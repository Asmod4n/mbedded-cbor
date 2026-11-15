#include "reference.hpp"
#include "owned_copy.hpp"
#include "pieces.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace std::string_view_literals;

namespace
{

using iov_encoder = cbor::encoder<cbor::iov_writer>;
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

template <class F>
std::string copied(F const &f)
{
    std::string out;
    cbor::encoder e{out};
    REQUIRE(f(e).has_value());
    REQUIRE(e.finish().has_value());
    return out;
}

std::vector<std::byte> bytes_of(std::string_view const s)
{
    std::vector<std::byte> b(s.size());
    std::ranges::transform(s, b.begin(), [](char const c) { return static_cast<std::byte>(c); });
    return b;
}

std::string text_item(std::string_view const text)
{
    return copied([text](auto &e) { return e.text_string(text); });
}

} // namespace

// Each of the three forms that carry an owner must give the same bytes as the form that copies. This must hold for sizes
// around the reference size and around the block size. A string at or above the reference size must stay where its owner
// keeps it. Otherwise the list holds a copy and the form gains nothing.
TEST_CASE("encode_iov: every form of a text gives the bytes of the copying form")
{
    for (std::size_t const size :
         {0uz, std::max(copybreak, 1uz) - 1, copybreak, copybreak + 1, 16383uz, 16384uz, 16385uz, 20000uz}) {
        CAPTURE(size);
        std::string const text(size, 'a');
        std::string const expected = copied([&](auto &e) { return e.text_string(std::string_view(text)); });

        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string(std::string_view(text)).has_value());
            auto finished = e.finish_iov();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(join_pieces_into_bytes(p), expected);
            CHECK_EQ(sum_piece_lengths(p), expected.size());
        }
        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string(std::string(text)).has_value());
            auto finished = e.finish_iov();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(join_pieces_into_bytes(p), expected);
            CHECK_EQ(sum_piece_lengths(p), expected.size());
            if (size > copybreak)
                CHECK(std::ranges::any_of(*p, [&](iovec const &piece) { return piece.iov_len == text.size(); }));
        }
        {
            auto const owner = std::make_shared<std::string const>(text);
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string(owner).has_value());
            auto finished = e.finish_iov();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(join_pieces_into_bytes(p), expected);
            CHECK_EQ(sum_piece_lengths(p), expected.size());
            CHECK_EQ(points_into(p, owner->data()), size > copybreak && size > 0);
        }
        {
            auto const read = test::make_lazy_from_copy(expected)->get<std::string_view>();
            REQUIRE(read.has_value());
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string(*read).has_value());
            auto finished = e.finish_iov();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(join_pieces_into_bytes(p), expected);
            CHECK_EQ(sum_piece_lengths(p), expected.size());
            CHECK_EQ(points_into(p, (**read).data()), size > copybreak && size > 0);
        }
    }
}

// The byte string forms follow the same rule as the text forms, through their own overloads.
TEST_CASE("encode_iov: every form of a byte string gives the bytes of the copying form")
{
    for (std::size_t const size : {0uz, std::max(copybreak, 1uz) - 1, copybreak, copybreak + 1, 16384uz, 20000uz}) {
        CAPTURE(size);
        std::vector<std::byte> const bytes = bytes_of(std::string(size, 'b'));
        std::string const expected =
            copied([&](auto &e) { return e.byte_string(std::span<std::byte const>(bytes)); });
        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string(std::vector<std::byte>(bytes)).has_value());
            auto finished = e.finish_iov();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(join_pieces_into_bytes(p), expected);
            CHECK_EQ(sum_piece_lengths(p), expected.size());
        }
        {
            auto const owner = std::make_shared<std::vector<std::byte> const>(bytes);
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string(owner).has_value());
            auto finished = e.finish_iov();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(join_pieces_into_bytes(p), expected);
            CHECK_EQ(points_into(p, owner->data()), size > copybreak && size > 0);
        }
        {
            auto const read = test::make_lazy_from_copy(expected)->get<std::span<std::byte const>>();
            REQUIRE(read.has_value());
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string(*read).has_value());
            auto finished = e.finish_iov();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(join_pieces_into_bytes(p), expected);
            CHECK_EQ(points_into(p, (**read).data()), size > copybreak && size > 0);
        }
    }
}

// The result must keep every owner alive by itself. The caller drops its own pointer before it reads. A sanitizer build
// reports a read of freed memory if the result does not hold the owner.
TEST_CASE("encode_iov: the pieces outlive the caller's owner")
{
    std::string const text(100, 'z');
    std::string const expected = text_item(text);
    cbor::iov_writer w;
    iov_encoder e{w};
    {
        auto const owner = std::make_shared<std::string const>(text);
        REQUIRE(e.text_string(owner).has_value());
    }
    std::string moved = text;
    REQUIRE(e.text_string(std::move(moved)).has_value());
    moved.assign(200, 'x');
    auto finished = e.finish_iov();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_EQ(join_pieces_into_bytes(p), expected + expected);
}

// Two encoders can share one writer. When the first finishes, the writer hands its pieces over. A later write
// of the second through that writer would change the result of the first, so it is a wrong use.
TEST_CASE("encode_iov: the second encoder on a writer that has finished is a wrong use")
{
    cbor::iov_writer w;
    iov_encoder first{w};
    iov_encoder second{w};
    REQUIRE(first.unsigned_integer(std::uint8_t{1}).has_value());
    REQUIRE(second.unsigned_integer(std::uint8_t{2}).has_value());
    auto finished = first.finish_iov();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_THROWS_AS(std::ignore = second.finish_iov(), std::logic_error);
    CHECK_EQ(join_pieces_into_bytes(p), "\x18\x01"sv);
}

// A head and a float write nine bytes before they count their own size. At each of the nine offsets
// before the end of a block, the encoder must take a new block in time and lose no byte. A tag takes the
// shortest head and an integer the fixed width; each form tests the space left on its own path.
TEST_CASE("encode_iov: a head and a float at each offset before the end of a block")
{
    for (bool const fixed : {false, true}) {
        for (std::size_t left = 1; left <= longest_head_size; ++left) {
            CAPTURE(fixed);
            CAPTURE(left);
            std::size_t const fill = CBOR_ENCODER_BLOCK_SIZE - left - 3;
            std::string const text(fill, 'f');
            auto const write = [&](auto &e) -> std::expected<void, cbor::error> {
                if (auto const r = e.text_string(std::string_view(text)); !r)
                    return r;
                if (auto const r =
                        fixed ? e.unsigned_integer(std::uint64_t{1} << 40) : e.tag(std::uint64_t{1} << 40);
                    !r)
                    return r;
                if (auto const r = e.text_string(std::string_view(text)); !r)
                    return r;
                return e.floating_point(0.1);
            };
            std::string const expected = copied(write);
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(write(e).has_value());
            auto finished = e.finish_iov();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(join_pieces_into_bytes(p), expected);
            CHECK_EQ(sum_piece_lengths(p), expected.size());
        }
    }
}

// A text can fill the free space of a block exactly. It can also miss it by one byte either way. There the copy splits
// across two blocks.
TEST_CASE("encode_iov: texts at the free space of a block")
{
    std::size_t const free = CBOR_ENCODER_BLOCK_SIZE - 3;
    for (std::size_t const size : {free - 1, free, free + 1}) {
        CAPTURE(size);
        std::string const text(size, 's');
        auto const write = [&](auto &e) -> std::expected<void, cbor::error> {
            if (auto const r = e.text_string(std::string_view(text)); !r)
                return r;
            return e.tag(7);
        };
        std::string const expected = copied(write);
        cbor::iov_writer w;
        iov_encoder e{w};
        REQUIRE(write(e).has_value());
        auto finished = e.finish_iov();
        REQUIRE(finished.has_value());
        pieces const p = std::move(*finished);
        CHECK_EQ(join_pieces_into_bytes(p), expected);
        CHECK_EQ(sum_piece_lengths(p), expected.size());
    }
}

// A reference closes the open part of the block first. The order of the pieces must be the order of the bytes. This
// holds also when a reference follows a full block and when a block follows a reference.
TEST_CASE("encode_iov: the order of blocks and references")
{
    auto const owner = std::make_shared<std::string const>(64, 'r');
    std::string const fill(CBOR_ENCODER_BLOCK_SIZE - 3, 'f');
    auto const write = [&](auto &e) -> std::expected<void, cbor::error> {
        if (auto const r = e.text_string(std::string_view(fill)); !r)
            return r;
        if (auto const r = e.text_string(owner); !r)
            return r;
        if (auto const r = e.tag(9); !r)
            return r;
        if (auto const r = e.text_string(owner); !r)
            return r;
        return e.text_string(std::string_view(fill));
    };
    std::string const expected = copied(write);
    cbor::iov_writer w;
    iov_encoder e{w};
    REQUIRE(write(e).has_value());
    auto finished = e.finish_iov();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_EQ(join_pieces_into_bytes(p), expected);
    CHECK_EQ(sum_piece_lengths(p), expected.size());
}

// finish must hand over the open part of the block, so the last item is not lost. Nothing written gives
// no piece at all.
TEST_CASE("encode_iov: finish after one item, and after none")
{
    {
        cbor::iov_writer w;
        iov_encoder e{w};
        REQUIRE(e.unsigned_integer(std::uint8_t{5}).has_value());
        auto finished = e.finish_iov();
        REQUIRE(finished.has_value());
        CHECK_EQ(join_pieces_into_bytes(*finished), "\x18\x05"sv);
    }
    {
        cbor::iov_writer w;
        iov_encoder e{w};
        auto finished = e.finish_iov();
        REQUIRE(finished.has_value());
        pieces const p = std::move(*finished);
        CHECK(p->empty());
    }
}

// After finish the result belongs to the caller. A further write or a second finish would change
// or lose it, so both are a wrong use.
TEST_CASE("encode_iov: a write after finish and a second finish are a wrong use")
{
    cbor::iov_writer w;
    iov_encoder e{w};
    REQUIRE(e.unsigned_integer(std::uint8_t{5}).has_value());
    auto finished = e.finish_iov();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_THROWS_AS(std::ignore = e.unsigned_integer(std::uint8_t{6}), std::logic_error);
    CHECK_THROWS_AS(std::ignore = e.finish_iov(), std::logic_error);
    CHECK_EQ(join_pieces_into_bytes(p), "\x18\x05"sv);
}

// writev takes at most IOV_MAX pieces in one call. test::joined reads an encoding with more pieces back in turns. It is
// the one place that splits. 1100 long strings with owners give more pieces than the 1024 of Linux.
TEST_CASE("encode_iov: more pieces than IOV_MAX are written in turns")
{
    constexpr std::size_t strings = 1100;
    auto const text = std::make_shared<std::string const>(std::string(copybreak + 1, 'x'));
    cbor::iov_writer w;
    iov_encoder e{w};
    std::string expected;
    cbor::encoder copy{expected};
    REQUIRE(e.array(strings).has_value());
    REQUIRE(copy.array(strings).has_value());
    for (std::size_t i = 0; i < strings; ++i) {
        REQUIRE(e.text_string(text).has_value());
        REQUIRE(copy.text_string(*text).has_value());
    }
    REQUIRE(copy.finish().has_value());
    auto finished = e.finish_iov();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_GT(p->size(), strings);
    CHECK_EQ(join_pieces_into_bytes(p), expected);
}

// The pieces are made for sending: sendmsg over a pair of Unix sockets carries the bytes of encode. The message
// stays below the 4 KiB that one socket buffer holds at least, so the single thread does not block.
TEST_CASE("encode_iov: sendmsg sends the bytes of encode")
{
    constexpr std::size_t socket_buffer_least = 4096;
    auto const text = std::make_shared<std::string const>(std::string(100, 'y'));
    cbor::iov_writer w;
    iov_encoder e{w};
    REQUIRE(e.array(2).has_value());
    REQUIRE(e.text_string(text).has_value());
    REQUIRE(e.unsigned_integer(std::uint8_t{7}).has_value());
    auto finished = e.finish_iov();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    std::string const expected = join_pieces_into_bytes(p);
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
    std::string const text(size, 't');
    std::vector<std::byte> const bytes = bytes_of(std::string(size, 'b'));
    return copied([&](auto &e) -> std::expected<void, cbor::error> {
        if (auto const r = e.array(3); !r)
            return r;
        if (auto const r = e.text_string(std::string_view(text)); !r)
            return r;
        if (auto const r = e.byte_string(std::span<std::byte const>(bytes)); !r)
            return r;
        return e.unsigned_integer(std::uint8_t{7});
    });
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
        REQUIRE(cbor::encode(**tree, expected).has_value());
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
// overload for item const & copies every string, so no piece of it points into the tree.
TEST_CASE("encode_iov: a decoded tree is referred to above the copy limit and copied up to it")
{
    for (std::size_t const size : sizes_around_copybreak()) {
        CAPTURE(size);
        std::string const input = array_of_text_bytes_and_seven(size);
        auto const tree = cbor::decode(input);
        REQUIRE(tree.has_value());
        auto const elements = (**tree).get<cbor::type::array>();
        REQUIRE(elements.has_value());
        REQUIRE_EQ(elements->size(), 3uz);
        auto const text = (*elements)[0].get<cbor::type::text_string>();
        auto const bytes = (*elements)[1].get<cbor::type::byte_string>();
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
    std::expected<pieces, cbor::error> kept = std::unexpected(cbor::error::unsupported_value);
    {
        auto tree = cbor::decode(input);
        REQUIRE(tree.has_value());
        REQUIRE(cbor::encode(**tree, expected).has_value());
        kept = cbor::encode_iov(*tree);
        REQUIRE(kept.has_value());
    }
    CHECK_EQ(join_pieces_into_bytes(*kept), expected);
}

// The cycles overload walks the tree another way, through its own search. It must refer and copy as the other overload
// does, and give the bytes of encode_cycles.
TEST_CASE("encode_iov_cycles: the overload for a decoded tree gives the bytes of encode_cycles")
{
    for (std::size_t const size : sizes_around_copybreak()) {
        CAPTURE(size);
        std::string const input = array_of_text_bytes_and_seven(size);
        auto const tree = cbor::decode(input);
        REQUIRE(tree.has_value());
        std::string expected;
        REQUIRE(cbor::encode_cycles(**tree, expected).has_value());
        auto const referred = cbor::encode_iov_cycles(*tree);
        REQUIRE(referred.has_value());
        CHECK_EQ(join_pieces_into_bytes(*referred), expected);
        auto const text = (**tree).get<cbor::type::array>().value()[0].get<cbor::type::text_string>();
        REQUIRE(text.has_value());
        CHECK_EQ(points_into(*referred, text->data()), size > copybreak && size > 0);
    }
}
