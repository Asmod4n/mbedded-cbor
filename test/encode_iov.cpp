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
using pieces = cbor::ref<std::span<iovec const>>;

std::string joined(pieces const &p)
{
    auto const bytes = test::joined(*p);
    REQUIRE(bytes.has_value());
    return *bytes;
}

std::size_t sum_of(pieces const &p)
{
    return test::sum_of(*p);
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

// Each of the three forms that carry an owner must give the same bytes as the form that copies, for
// sizes around the reference size and around the block size. A string at or above the reference size
// must stay where its owner keeps it, else the list holds a copy and the form gains nothing.
TEST_CASE("encode_iov: every form of a text gives the bytes of the copying form")
{
    for (std::size_t const size : {0uz, 31uz, 32uz, 33uz, 16383uz, 16384uz, 16385uz, 20000uz}) {
        CAPTURE(size);
        std::string const text(size, 'a');
        std::string const expected = copied([&](auto &e) { return e.text_string(std::string_view(text)); });

        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string(std::string_view(text)).has_value());
            auto finished = e.finish();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), expected.size());
        }
        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string(std::string(text)).has_value());
            auto finished = e.finish();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), expected.size());
            if (size > cbor::encoding::copybreak)
                CHECK(std::ranges::any_of(*p, [&](iovec const &piece) { return piece.iov_len == text.size(); }));
        }
        {
            auto const owner = std::make_shared<std::string const>(text);
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string(owner).has_value());
            auto finished = e.finish();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), expected.size());
            CHECK_EQ(points_into(p, owner->data()), size > cbor::encoding::copybreak && size > 0);
        }
        {
            auto const read = test::lazy_of_copy(expected)->get<std::string_view>();
            REQUIRE(read.has_value());
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string(*read).has_value());
            auto finished = e.finish();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), expected.size());
            CHECK_EQ(points_into(p, (**read).data()), size > cbor::encoding::copybreak && size > 0);
        }
    }
}

// The byte string forms follow the same rule as the text forms, through their own overloads.
TEST_CASE("encode_iov: every form of a byte string gives the bytes of the copying form")
{
    for (std::size_t const size : {0uz, 31uz, 32uz, 33uz, 16384uz, 20000uz}) {
        CAPTURE(size);
        std::vector<std::byte> const bytes = bytes_of(std::string(size, 'b'));
        std::string const expected =
            copied([&](auto &e) { return e.byte_string(std::span<std::byte const>(bytes)); });
        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string(std::vector<std::byte>(bytes)).has_value());
            auto finished = e.finish();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), expected.size());
        }
        {
            auto const owner = std::make_shared<std::vector<std::byte> const>(bytes);
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string(owner).has_value());
            auto finished = e.finish();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(points_into(p, owner->data()), size > cbor::encoding::copybreak && size > 0);
        }
        {
            auto const read = test::lazy_of_copy(expected)->get<std::span<std::byte const>>();
            REQUIRE(read.has_value());
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string(*read).has_value());
            auto finished = e.finish();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(points_into(p, (**read).data()), size > cbor::encoding::copybreak && size > 0);
        }
    }
}

// The result must keep every owner alive by itself. The caller drops its own pointer before it reads;
// a sanitizer build reports a read of freed memory if the result does not hold the owner.
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
    auto finished = e.finish();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_EQ(joined(p), expected + expected);
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
    auto finished = first.finish();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_THROWS_AS(std::ignore = second.finish(), std::logic_error);
    CHECK_EQ(joined(p), "\x18\x01"sv);
}

// A head and a float write nine bytes before they count their own size. At each of the nine offsets
// before the end of a block, the encoder must take a new block in time and lose no byte. A tag takes the
// shortest head and an integer the fixed width; each form tests the space left on its own path.
TEST_CASE("encode_iov: a head and a float at each offset before the end of a block")
{
    for (bool const fixed : {false, true}) {
        for (std::size_t left = 1; left <= cbor::encoding::item_size_max; ++left) {
            CAPTURE(fixed);
            CAPTURE(left);
            std::size_t const fill = cbor::encoding::block_size - left - 3;
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
            auto finished = e.finish();
            REQUIRE(finished.has_value());
            pieces const p = std::move(*finished);
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), expected.size());
        }
    }
}

// A text that fills the free space of a block exactly, or misses it by one byte either way, is where
// the copy splits across two blocks.
TEST_CASE("encode_iov: texts at the free space of a block")
{
    std::size_t const free = cbor::encoding::block_size - 3;
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
        auto finished = e.finish();
        REQUIRE(finished.has_value());
        pieces const p = std::move(*finished);
        CHECK_EQ(joined(p), expected);
        CHECK_EQ(sum_of(p), expected.size());
    }
}

// A reference closes the open part of the block first. The order of the pieces must be the order of
// the bytes, also when a reference follows a full block and a block follows a reference.
TEST_CASE("encode_iov: the order of blocks and references")
{
    auto const owner = std::make_shared<std::string const>(64, 'r');
    std::string const fill(cbor::encoding::block_size - 3, 'f');
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
    auto finished = e.finish();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_EQ(joined(p), expected);
    CHECK_EQ(sum_of(p), expected.size());
}

// finish must hand over the open part of the block, so the last item is not lost. Nothing written gives
// no piece at all.
TEST_CASE("encode_iov: finish after one item, and after none")
{
    {
        cbor::iov_writer w;
        iov_encoder e{w};
        REQUIRE(e.unsigned_integer(std::uint8_t{5}).has_value());
        auto finished = e.finish();
        REQUIRE(finished.has_value());
        CHECK_EQ(joined(*finished), "\x18\x05"sv);
    }
    {
        cbor::iov_writer w;
        iov_encoder e{w};
        auto finished = e.finish();
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
    auto finished = e.finish();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_THROWS_AS(std::ignore = e.unsigned_integer(std::uint8_t{6}), std::logic_error);
    CHECK_THROWS_AS(std::ignore = e.finish(), std::logic_error);
    CHECK_EQ(joined(p), "\x18\x05"sv);
}

// The forms that carry an owner must still copy with every other writer, and give the same bytes.
TEST_CASE("encode_iov: the owner forms copy with the other writers")
{
    std::string const text(64, 'o');
    std::string const expected = text_item(text);
    auto const owner = std::make_shared<std::string const>(text);
    CHECK_EQ(copied([&](auto &e) { return e.text_string(owner); }), expected);
    CHECK_EQ(copied([&](auto &e) { return e.text_string(std::string(text)); }), expected);
    std::string span_out(expected.size(), '\0');
    REQUIRE(test::encode(std::span<char>(span_out), value{std::string(text)}).has_value());
    CHECK_EQ(span_out, expected);
}

// The encoder copies every byte of a view that it is given. Its result must equal the
// bytes that encode writes into a std::string, for examples of RFC 8949 Appendix A of each major type.
TEST_CASE("encode_iov: the encoder over views gives the bytes of encode")
{
    for (std::string_view const wire :
         {"\x00"sv, "\x18\x64"sv, "\x3b\xff\xff\xff\xff\xff\xff\xff\xff"sv, "\xf9\x3c\x00"sv,
          "\xfb\x7e\x37\xe4\x3c\x88\x00\x75\x9c"sv, "\x64\x49\x45\x54\x46"sv, "\x83\x01\x82\x02\x03\x82\x04\x05"sv,
          "\xa2\x61\x61\x01\x61\x62\x82\x02\x03"sv, "\xc1\x1a\x51\x4b\x67\xb0"sv}) {
        auto const v = decoded(wire);
        REQUIRE(v.has_value());
        auto const p = test::encode_iov(*v);
        REQUIRE(p.has_value());
        CHECK_EQ(joined(*p), encoded(*v));
    }
    value const long_text{std::string(40000, 'w')};
    auto const p = test::encode_iov(long_text);
    REQUIRE(p.has_value());
    CHECK_EQ(joined(*p), encoded(long_text));
}

// Without preferred serialization the encoder writes every float as a double, the width that needs no
// test of the value. With it, the shortest width that keeps the value, as RFC 8949 4.2.2 prefers.
TEST_CASE("encode: a float is a double unless preferred serialization is on")
{
    string_writer w;
    REQUIRE(test::encode(w, value{1.0}).has_value());
    CHECK_EQ(w.encoded, "\xfb\x3f\xf0\x00\x00\x00\x00\x00\x00"sv);
    string_writer p;
    REQUIRE(test::encode(p, value{1.0}).has_value());
    CHECK_EQ(p.encoded, "\xfb\x3f\xf0\x00\x00\x00\x00\x00\x00"sv);
}

// writev takes at most IOV_MAX pieces in one call. An encoding with more pieces is read back in turns by
// test::joined, the one place that splits; 1100 long strings with owners give more than the 1024 of Linux.
TEST_CASE("encode_iov: more pieces than IOV_MAX are written in turns")
{
    constexpr std::size_t strings = 1100;
    auto const text = std::make_shared<std::string const>(std::string(cbor::encoding::copybreak + 1, 'x'));
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
    auto finished = e.finish();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    CHECK_GT(p->size(), strings);
    CHECK_EQ(joined(p), expected);
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
    auto finished = e.finish();
    REQUIRE(finished.has_value());
    pieces const p = std::move(*finished);
    std::string const expected = joined(p);
    REQUIRE_LT(expected.size(), socket_buffer_least);
    std::array<int, 2> ends{};
    REQUIRE_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ends.data()), 0);
    msghdr message{};
    message.msg_iov = const_cast<iovec *>(p->data());
    auto const count = test::count_of<decltype(message.msg_iovlen)>(p->size());
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
