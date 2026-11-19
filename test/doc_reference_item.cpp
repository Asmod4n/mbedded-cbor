#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "pieces.hpp"
#include "reference.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

using namespace std::string_literals;
using namespace std::string_view_literals;

// Each test in this file is one example of a reference page of the item group. The comments of item.hpp, decode.hpp and
// encode.hpp take the examples through the snippet markers. The tests exist so that every example compiles and gives the
// result that its page states.

namespace
{

struct appending_writer {
    std::string *out;
    bool fails;

    std::expected<void, std::errc> append(std::string_view const part)
    {
        if (fails)
            return std::unexpected(std::errc::io_error);
        out->append(part);
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

struct writer_target {
    std::string *out;
    bool fails;

    appending_writer allocate(std::size_t)
    {
        return {out, fails};
    }
};

}

// item.hpp, snippet item: the kind of the root and the count of its elements; only a decode makes an item.
TEST_CASE("doc reference item: item")
{
    std::string const bytes = test::encode_in_preferred_form(test::A(1, test::M("a"s, 2)));
    // [item]
    // bytes: a std::string with [1, {"a": 2}]
    auto const tree = cbor::decode(bytes);
    auto const kind = (*tree)->type();                              // cbor::type::array
    auto const count = (*tree)->get<cbor::array_range>()->size();  // 2
    // [item]
    REQUIRE(tree);
    CHECK_EQ(kind, cbor::type::array);
    CHECK_EQ(count, 2u);
    CHECK_FALSE(std::is_constructible_v<cbor::item, cbor::type, std::uint8_t>);
    CHECK(std::is_trivially_destructible_v<cbor::item>);
}

// item.hpp, snippet get: a text key, the value of a negative integer, and a type that does not hold it.
TEST_CASE("doc reference item: item::get")
{
    std::string const bytes = test::encode_in_preferred_form(test::M("a"s, test::negative{2}));
    // [get]
    // bytes: a std::string with {"a": -3}
    auto const tree = cbor::decode(bytes);
    auto const pairs = *(*tree)->get<cbor::map_range>();
    auto const [k, v] = pairs.front();
    auto const key = k.get<std::string_view>();       // "a"
    auto const value = v.get<std::int64_t>();         // -3
    auto const other = v.get<std::uint64_t>();        // number_out_of_range
    // [get]
    REQUIRE(tree);
    REQUIRE_EQ(pairs.size(), 1u);
    CHECK_EQ(key.value(), "a"sv);
    CHECK_EQ(value.value(), -3);
    CHECK_EQ(other.error(), cbor::error::number_out_of_range);
}

#ifdef CBOR_HAS_INT128
// item.hpp, snippet visit: f receives each element as its own C++ type.
TEST_CASE("doc reference item: item::visit")
{
    std::string const bytes = test::encode_in_preferred_form(test::A(1, test::negative{1}, "a"s));
    // [visit]
    // bytes: a std::string with [1, -2, "a"]
    auto const tree = cbor::decode(bytes);
    std::string kinds;
    for (cbor::item const element : *(*tree)->get<cbor::array_range>())
        kinds += element.visit([](auto const v) {
            if constexpr (std::is_same_v<decltype(v), std::uint64_t const>)
                return "unsigned "s;
            else if constexpr (std::is_same_v<decltype(v), cbor::int128 const>)
                return "negative "s;
            else if constexpr (std::is_same_v<decltype(v), std::string_view const>)
                return "text "s;
            else
                return "other "s;
        });
    // kinds: "unsigned negative text "
    // [visit]
    CHECK_EQ(kinds, "unsigned negative text "s);
}
#endif

// The snippet of the group decode, between the markers "decode group". The example on the page of the group decode
// uses the encoder, decode and encode together. So a reader sees one round trip. The test checks each value that the
// comments of the example state.
TEST_CASE("doc reference item: the example of the group decode")
{
    // [decode group]
    std::string const bytes = "\x82\x18\x01\x61\x61"s;  // [1, "a"], the 1 with a one-byte argument
    auto const tree = cbor::decode(bytes);
    auto const second = (*tree)->get<cbor::array_range>()->next().front().get<std::string_view>();  // "a"
    std::string out;
    auto const size = cbor::encode(out, **tree);  // 5; out == bytes
    // [decode group]
    REQUIRE(tree);
    CHECK_EQ(second.value(), "a"sv);
    CHECK_EQ(size.value(), 5u);
    CHECK_EQ(out, bytes);
}

// decode.hpp, snippet decode: one item, a byte after it, a leading tag 55799, an indefinite length, and a tree that
// outlives its input.
TEST_CASE("doc reference item: decode")
{
    std::string const bytes = test::encode_in_preferred_form(test::A("hi"s, 1));
    // [decode]
    // bytes: a std::string with ["hi", 1]
    auto const tree = cbor::decode(bytes);
    auto const count = (*tree)->get<cbor::array_range>()->size();  // 2
    auto const first = cbor::decode("\x01\x02"s);                    // the item 1; the byte 02 is not read
    // [decode]
    REQUIRE(tree);
    CHECK_EQ(count, 2u);
    CHECK(first.has_value());
    auto const self_described = cbor::decode("\xd9\xd9\xf7\x01"s);
    CHECK_EQ((*self_described)->type(), cbor::type::unsigned_integer);
    auto const second = cbor::decode("\xd9\xd9\xf7\xd9\xd9\xf7\x01"s);
    REQUIRE(second);
    CHECK_EQ((*second)->get<std::pair<std::uint64_t, cbor::item>>()->first, 55799u);
    auto const inside = cbor::decode("\x81\xd9\xd9\xf7\x01"s);
    REQUIRE(inside);
    CHECK_EQ((*inside)->get<cbor::array_range>().value().front().type(), cbor::type::tag);
    CHECK_EQ(cbor::decode("\x9f\xff"s).error(), cbor::error::indefinite_length);
    CHECK_EQ(cbor::decode("\x82\x01"s).error(), cbor::error::too_little_data);

    auto input = std::make_unique<std::string>(bytes);
    auto const copy = cbor::decode(*input);
    input.reset();
    REQUIRE(copy);
    CHECK_EQ((*copy)->get<cbor::array_range>().value().front().get<std::string_view>().value(), "hi"sv);
}

// decode.hpp, snippet encoded_cbor_data_item: the content of a tag 24, a content that is no byte string, and a wrong
// node.
TEST_CASE("doc reference item: encoded_cbor_data_item")
{
    std::string const bytes = "\xd8\x18\x42\x61\x61"s;
    // [encoded_cbor_data_item]
    // bytes: a std::string with 24(h'6161'), a tag 24 over the text "a"
    auto const tree = cbor::decode(bytes);
    auto const inner = cbor::encoded_cbor_data_item(*tree, **tree);
    auto const text = inner->get<std::string_view>();  // "a"
    // [encoded_cbor_data_item]
    REQUIRE(tree);
    REQUIRE(inner);
    CHECK_EQ(text.value(), "a"sv);

    auto const no_bytes = cbor::decode("\xd8\x18\x01"s);
    REQUIRE(no_bytes);
    CHECK_EQ(cbor::encoded_cbor_data_item(*no_bytes, **no_bytes).error(), cbor::error::incorrect_type);

    auto const array = cbor::decode("\x81\x01"s);
    REQUIRE(array);
    CHECK_THROWS_AS((void)cbor::encoded_cbor_data_item(*array, (*array)->get<cbor::array_range>().value().front()),
                    std::logic_error);
}

// encode.hpp, snippet encode: a container target gives the input again; a short span and a writer of the program.
TEST_CASE("doc reference item: encode")
{
    std::string const bytes = test::encode_in_preferred_form(test::A(1, "a"s));
    // [encode]
    // bytes: a std::string with [1, "a"]
    auto const tree = cbor::decode(bytes);
    std::string out;
    auto const size = cbor::encode(out, **tree);  // 4; out == bytes
    // [encode]
    REQUIRE(tree);
    CHECK_EQ(size.value(), 4u);
    CHECK_EQ(out, bytes);

    std::array<char, 2> small{};
    CHECK_EQ(cbor::encode(std::span<char>(small), **tree).error(), cbor::error::no_buffer_space);

    std::string written;
    CHECK_EQ(cbor::encode(writer_target{&written, false}, **tree).value(), 4u);
    CHECK_EQ(written, bytes);
    CHECK_EQ(cbor::encode(writer_target{&written, true}, **tree).error(), cbor::error::io_error);
}

// The iov forms exist where the platform has <sys/uio.h>, as in the library (encode.hpp); Windows has none.
#if __has_include(<sys/uio.h>)
// encode.hpp, snippet encode_iov: one piece; written with writev, the pieces are the bytes of encode, shown as EDN on
// the page.
TEST_CASE("doc reference item: encode_iov")
{
    std::string const bytes = test::encode_in_preferred_form(test::A(1, "a"s));
    // [encode_iov]
    // bytes: a std::string with [1, "a"]
    auto const tree = cbor::decode(bytes);
    auto const pieces = cbor::encode_iov(*tree);  // 1 piece, the 4 bytes 82 01 61 61
    // writev(fd, (*pieces)->data(), static_cast<int>((*pieces)->size()));
    // [encode_iov]
    REQUIRE(tree);
    REQUIRE(pieces);
    CHECK_EQ((*pieces)->size(), 1u);
    auto const joined = test::join_pieces_into_bytes(**pieces);
    REQUIRE(joined.has_value());
    CHECK_EQ(*joined, bytes);
    CHECK_EQ(cbor::diagnostic_notation(*joined), "[1, \"a\"]");
}
#endif
