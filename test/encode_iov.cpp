#include "reference.hpp"
#include "owned_copy.hpp"

#include <cstddef>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace
{

using iov_encoder = cbor::encoding::encoder<cbor::iov_writer>;
using pieces = cbor::ref<std::span<std::string_view const>>;

std::string joined(pieces const &p)
{
    std::string out;
    for (std::string_view const piece : *p)
        out.append(piece);
    return out;
}

std::size_t sum_of(pieces const &p)
{
    return std::ranges::fold_left(*p | std::views::transform(&std::string_view::size), std::size_t{0},
                                  std::plus<>{});
}

bool points_into(pieces const &p, char const *const data)
{
    return std::ranges::any_of(*p, [data](std::string_view const piece) { return piece.data() == data; });
}

template <class F>
std::string copied(F const &f)
{
    string_writer w;
    cbor::encoding::encoder<string_writer> e{w};
    REQUIRE(f(e).has_value());
    REQUIRE(e.flush().has_value());
    return w.encoded;
}

std::vector<std::byte> bytes_of(std::string_view const s)
{
    std::vector<std::byte> b(s.size());
    std::ranges::transform(s, b.begin(), [](char const c) { return static_cast<std::byte>(c); });
    return b;
}

std::string text_item(std::string_view const text)
{
    return copied([text](auto &e) { return e.text_string_encode(text); });
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
        std::string const expected = copied([&](auto &e) { return e.text_string_encode(std::string_view(text)); });

        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string_encode(std::string_view(text)).has_value());
            pieces const p = e.release();
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), e.written);
        }
        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string_encode(std::string(text)).has_value());
            pieces const p = e.release();
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), e.written);
            if (size >= iov_encoder::ref_size)
                CHECK(std::ranges::any_of(*p, [&](std::string_view const piece) { return piece == text; }));
        }
        {
            auto const owner = std::make_shared<std::string const>(text);
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string_encode(owner).has_value());
            pieces const p = e.release();
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), e.written);
            CHECK_EQ(points_into(p, owner->data()), size >= iov_encoder::ref_size && size > 0);
        }
        {
            auto const read = test::lazy_of_copy(expected)->get<std::string_view>();
            REQUIRE(read.has_value());
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.text_string_encode(*read).has_value());
            pieces const p = e.release();
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), e.written);
            CHECK_EQ(points_into(p, (**read).data()), size >= iov_encoder::ref_size && size > 0);
        }
    }
}

// The byte string forms follow the same rule as the text forms, through their own overloads.
TEST_CASE("encode_iov: every form of a byte string gives the bytes of the copying form")
{
    for (std::size_t const size : {0uz, 31uz, 32uz, 33uz, 16384uz, 20000uz}) {
        CAPTURE(size);
        std::vector<std::byte> const bytes = bytes_of(std::string(size, 'b'));
        std::string const expected = copied([&](auto &e) {
            return e.byte_string_encode(std::string_view(reinterpret_cast<char const *>(bytes.data()), size));
        });
        {
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string_encode(std::vector<std::byte>(bytes)).has_value());
            pieces const p = e.release();
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(sum_of(p), e.written);
        }
        {
            auto const owner = std::make_shared<std::vector<std::byte> const>(bytes);
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string_encode(owner).has_value());
            pieces const p = e.release();
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(points_into(p, reinterpret_cast<char const *>(owner->data())),
                     size >= iov_encoder::ref_size && size > 0);
        }
        {
            auto const read = test::lazy_of_copy(expected)->get<std::span<std::byte const>>();
            REQUIRE(read.has_value());
            cbor::iov_writer w;
            iov_encoder e{w};
            REQUIRE(e.byte_string_encode(*read).has_value());
            pieces const p = e.release();
            CHECK_EQ(joined(p), expected);
            CHECK_EQ(points_into(p, reinterpret_cast<char const *>((**read).data())),
                     size >= iov_encoder::ref_size && size > 0);
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
        auto owner = std::make_shared<std::string const>(text);
        REQUIRE(e.text_string_encode(std::move(owner)).has_value());
    }
    std::string moved = text;
    REQUIRE(e.text_string_encode(std::move(moved)).has_value());
    moved.assign(200, 'x');
    pieces const p = e.release();
    CHECK_EQ(joined(p), expected + expected);
}

// A flush in the middle of a block must hand over only what came after the last flush, so that no byte
// enters the list twice.
TEST_CASE("encode_iov: write, flush, write, flush in one block")
{
    cbor::iov_writer w;
    iov_encoder e{w};
    REQUIRE(e.head_encode(cbor::major_type::unsigned_integer, 1).has_value());
    REQUIRE(e.flush().has_value());
    REQUIRE(e.head_encode(cbor::major_type::unsigned_integer, 2).has_value());
    REQUIRE(e.flush().has_value());
    REQUIRE(e.flush().has_value());
    pieces const p = e.release();
    CHECK_EQ(joined(p), "\x01\x02"sv);
    CHECK_EQ(e.written, 2);
    CHECK_EQ((*p).size(), 1);
}

// A head and a float write nine bytes before they count their own size. At each of the nine offsets
// before the end of a block, the encoder must take a new block in time and lose no byte.
TEST_CASE("encode_iov: a head and a float at each offset before the end of a block")
{
    for (std::size_t left = 1; left <= iov_encoder::item_size_max; ++left) {
        CAPTURE(left);
        std::size_t const fill = iov_encoder::block_size - left - 3;
        std::string const text(fill, 'f');
        auto const write = [&](auto &e) -> std::expected<void, std::errc> {
            if (auto const r = e.text_string_encode(std::string_view(text)); !r)
                return r;
            if (auto const r = e.head_encode(cbor::major_type::unsigned_integer, std::uint64_t{1} << 40); !r)
                return r;
            if (auto const r = e.text_string_encode(std::string_view(text)); !r)
                return r;
            return e.float_encode(0.1);
        };
        std::string const expected = copied(write);
        cbor::iov_writer w;
        iov_encoder e{w};
        REQUIRE(write(e).has_value());
        pieces const p = e.release();
        CHECK_EQ(joined(p), expected);
        CHECK_EQ(sum_of(p), e.written);
    }
}

// A text that fills the free space of a block exactly, or misses it by one byte either way, is where
// the copy splits across two blocks.
TEST_CASE("encode_iov: texts at the free space of a block")
{
    std::size_t const free = iov_encoder::block_size - 3;
    for (std::size_t const size : {free - 1, free, free + 1}) {
        CAPTURE(size);
        std::string const text(size, 's');
        std::string const expected = copied([&](auto &e) {
            if (auto const r = e.text_string_encode(std::string_view(text)); !r)
                return r;
            return e.head_encode(cbor::major_type::unsigned_integer, 7);
        });
        cbor::iov_writer w;
        iov_encoder e{w};
        REQUIRE(e.text_string_encode(std::string_view(text)).has_value());
        REQUIRE(e.head_encode(cbor::major_type::unsigned_integer, 7).has_value());
        pieces const p = e.release();
        CHECK_EQ(joined(p), expected);
        CHECK_EQ(sum_of(p), e.written);
    }
}

// A reference closes the open part of the block first. The order of the pieces must be the order of
// the bytes, also when a reference follows a full block and a block follows a reference.
TEST_CASE("encode_iov: the order of blocks and references")
{
    auto const owner = std::make_shared<std::string const>(64, 'r');
    std::string const fill(iov_encoder::block_size - 3, 'f');
    auto const write = [&](auto &e) -> std::expected<void, std::errc> {
        if (auto const r = e.text_string_encode(std::string_view(fill)); !r)
            return r;
        if (auto const r = e.text_string_encode(owner); !r)
            return r;
        if (auto const r = e.head_encode(cbor::major_type::unsigned_integer, 9); !r)
            return r;
        if (auto const r = e.text_string_encode(owner); !r)
            return r;
        return e.text_string_encode(std::string_view(fill));
    };
    std::string const expected = copied(write);
    cbor::iov_writer w;
    iov_encoder e{w};
    REQUIRE(write(e).has_value());
    pieces const p = e.release();
    CHECK_EQ(joined(p), expected);
    CHECK_EQ(sum_of(p), e.written);
}

// release must hand over the open part of the block itself, so a caller who did not flush loses
// nothing. Nothing written gives no piece at all.
TEST_CASE("encode_iov: release without a flush, and an empty result")
{
    {
        cbor::iov_writer w;
        iov_encoder e{w};
        REQUIRE(e.head_encode(cbor::major_type::unsigned_integer, 5).has_value());
        CHECK_EQ(joined(e.release()), "\x05"sv);
    }
    {
        cbor::iov_writer w;
        iov_encoder e{w};
        pieces const p = e.release();
        CHECK(p->empty());
    }
}

// After release the result belongs to the caller. A further write or a second release would change
// or lose it, so both are a wrong use.
TEST_CASE("encode_iov: a write after release and a second release are a wrong use")
{
    cbor::iov_writer w;
    iov_encoder e{w};
    REQUIRE(e.head_encode(cbor::major_type::unsigned_integer, 5).has_value());
    pieces const p = e.release();
    CHECK_THROWS_AS(std::ignore = e.head_encode(cbor::major_type::unsigned_integer, 6), std::logic_error);
    CHECK_THROWS_AS(std::ignore = e.release(), std::logic_error);
    CHECK_EQ(joined(p), "\x05"sv);
}

// The forms that carry an owner must still copy with every other writer, and give the same bytes.
TEST_CASE("encode_iov: the owner forms copy with the other writers")
{
    std::string const text(64, 'o');
    std::string const expected = text_item(text);
    auto const owner = std::make_shared<std::string const>(text);
    CHECK_EQ(copied([&](auto &e) { return e.text_string_encode(owner); }), expected);
    CHECK_EQ(copied([&](auto &e) { return e.text_string_encode(std::string(text)); }), expected);
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

// An error of encode_iov of a tree must come back as the error value, and the writer must free what it holds.
TEST_CASE("encode_iov: an error of the tree comes back as the error value")
{
    auto const tree = test::lazy_of_copy(std::string(100, '\x81') + '\x80')->decode();
    REQUIRE(tree.has_value());
    test::limits_guard const depth{{.nesting_depth = 16}};
    auto const p = cbor::encode_iov(**tree);
    REQUIRE_FALSE(p.has_value());
    CHECK_EQ(p.error(), error::nesting_depth_exceeded);
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
