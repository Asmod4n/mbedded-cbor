#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "pieces.hpp"
#include "reference.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
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

// Each test in this file is one example of a reference page of the item group; the comments of item.hpp, decode.hpp
// and encode.hpp take the examples through the snippet markers. The tests exist so that every example compiles and
// gives the result that its page states.

namespace
{

template <class T>
concept unsigned_integer_accepts = requires(cbor::encoder<std::string> &e, T const v) { e.unsigned_integer(v); };

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
    std::string const bytes = test::preferred(test::A(1, test::M("a"s, 2)));
    // [item]
    // bytes: a std::string with [1, {"a": 2}]
    auto const tree = cbor::decode(bytes);
    auto const kind = (*tree)->type;                              // cbor::type::array
    auto const count = (*tree)->get<cbor::type::array>()->size();  // 2
    // [item]
    REQUIRE(tree);
    CHECK_EQ(kind, cbor::type::array);
    CHECK_EQ(count, 2u);
    CHECK_FALSE(std::is_constructible_v<cbor::item, cbor::type, std::uint8_t>);
    CHECK(std::is_trivially_destructible_v<cbor::item>);
}

// item.hpp, snippet get: a text key, the argument of a negative integer, and a wrong kind.
TEST_CASE("doc reference item: item::get")
{
    std::string const bytes = test::preferred(test::M("a"s, test::negative{2}));
    // [get]
    // bytes: a std::string with {"a": -3}
    auto const tree = cbor::decode(bytes);
    auto const pairs = *(*tree)->get<cbor::type::map>();
    auto const key = pairs[0].get<cbor::type::text_string>();        // "a"
    auto const argument = pairs[1].get<cbor::type::negative_integer>(); // 2, for -3
    auto const other = pairs[1].get<cbor::type::unsigned_integer>();    // incorrect_type
    // [get]
    REQUIRE(tree);
    REQUIRE_EQ(pairs.size(), 2u);
    CHECK_EQ(key.value(), "a"sv);
    CHECK_EQ(argument.value(), 2u);
    CHECK_EQ(other.error(), cbor::error::incorrect_type);
}

// decode.hpp, snippet decode: one item, a byte after it, a leading tag 55799, an indefinite length, and a tree that
// outlives its input.
TEST_CASE("doc reference item: decode")
{
    std::string const bytes = test::preferred(test::A("hi"s, 1));
    // [decode]
    // bytes: a std::string with ["hi", 1]
    auto const tree = cbor::decode(bytes);
    auto const count = (*tree)->get<cbor::type::array>()->size();  // 2
    auto const first = cbor::decode("\x01\x02"s);                    // the item 1; the byte 02 is not read
    // [decode]
    REQUIRE(tree);
    CHECK_EQ(count, 2u);
    CHECK(first.has_value());
    auto const self_described = cbor::decode("\xd9\xd9\xf7\x01"s);
    CHECK_EQ((*self_described)->type, cbor::type::unsigned_integer);
    auto const second = cbor::decode("\xd9\xd9\xf7\xd9\xd9\xf7\x01"s);
    REQUIRE(second);
    CHECK_EQ((*second)->get<cbor::type::tag>()->first, 55799u);
    auto const inside = cbor::decode("\x81\xd9\xd9\xf7\x01"s);
    REQUIRE(inside);
    CHECK_EQ((*inside)->get<cbor::type::array>().value()[0].type, cbor::type::tag);
    CHECK_EQ(cbor::decode("\x9f\xff"s).error(), cbor::error::indefinite_length);
    CHECK_EQ(cbor::decode("\x82\x01"s).error(), cbor::error::too_little_data);

    auto input = std::make_unique<std::string>(bytes);
    auto const copy = cbor::decode(*input);
    input.reset();
    REQUIRE(copy);
    CHECK_EQ((*copy)->get<cbor::type::array>().value()[0].get<cbor::type::text_string>().value(), "hi"sv);
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
    auto const text = inner->get().get<cbor::type::text_string>();  // "a"
    // [encoded_cbor_data_item]
    REQUIRE(tree);
    REQUIRE(inner);
    CHECK_EQ(text.value(), "a"sv);

    auto const no_bytes = cbor::decode("\xd8\x18\x01"s);
    REQUIRE(no_bytes);
    CHECK_EQ(cbor::encoded_cbor_data_item(*no_bytes, **no_bytes).error(), cbor::error::incorrect_type);

    auto const array = cbor::decode("\x81\x01"s);
    REQUIRE(array);
    CHECK_THROWS_AS((void)cbor::encoded_cbor_data_item(*array, (*array)->get<cbor::type::array>().value()[0]),
                    std::logic_error);
}

// encode.hpp, snippet encode: a container target gives the input again; a short span and a writer of the program.
TEST_CASE("doc reference item: encode")
{
    std::string const bytes = test::preferred(test::A(1, "a"s));
    // [encode]
    // bytes: a std::string with [1, "a"]
    auto const tree = cbor::decode(bytes);
    std::string out;
    auto const size = cbor::encode(**tree, out);  // 4; out == bytes
    // [encode]
    REQUIRE(tree);
    CHECK_EQ(size.value(), 4u);
    CHECK_EQ(out, bytes);

    std::array<char, 2> small{};
    CHECK_EQ(cbor::encode(**tree, std::span<char>(small)).error(), cbor::error::no_buffer_space);

    std::string written;
    CHECK_EQ(cbor::encode(**tree, writer_target{&written, false}).value(), 4u);
    CHECK_EQ(written, bytes);
    CHECK_EQ(cbor::encode(**tree, writer_target{&written, true}).error(), cbor::error::io_error);
}

// The iov forms exist where the platform has <sys/uio.h>, as in the library (encode.hpp); Windows has none.
#if __has_include(<sys/uio.h>)
// encode.hpp, snippet encode_iov: one piece; written with writev, the pieces are the bytes of encode, shown as EDN on
// the page.
TEST_CASE("doc reference item: encode_iov")
{
    std::string const bytes = test::preferred(test::A(1, "a"s));
    // [encode_iov]
    // bytes: a std::string with [1, "a"]
    auto const tree = cbor::decode(bytes);
    auto const pieces = cbor::encode_iov(**tree);  // 1 piece, the 4 bytes 82 01 61 61
    // writev(fd, (*pieces)->data(), static_cast<int>((*pieces)->size()));
    // [encode_iov]
    REQUIRE(tree);
    REQUIRE(pieces);
    CHECK_EQ((*pieces)->size(), 1u);
    auto const joined = test::joined(**pieces);
    REQUIRE(joined.has_value());
    CHECK_EQ(*joined, bytes);
    CHECK_EQ(cbor::diagnostic_notation(*joined), "[1, \"a\"]");
}
#endif

// encode.hpp, snippet encoder: a map written call by call; a copy does not compile.
TEST_CASE("doc reference item: encoder")
{
    CHECK_FALSE(std::is_copy_constructible_v<cbor::encoder<std::string>>);
    // [encoder]
    std::string out;
    cbor::encoder e(out);
    bool const written = e.map(1) && e.text_string("a") && e.unsigned_integer(std::uint8_t{5});
    auto const size = e.finish();  // 5: out holds {"a": 5}, a1 61 61 18 05
    // [encoder]
    CHECK(written);
    CHECK_EQ(size.value(), 5u);
    CHECK_EQ(out, "\xa1\x61\x61\x18\x05"s);
}

// encode.hpp, snippet unsigned_integer: the head takes the width of the type; bool is no integer argument.
TEST_CASE("doc reference item: encoder::unsigned_integer")
{
    CHECK_FALSE(unsigned_integer_accepts<bool>);
    CHECK_FALSE(unsigned_integer_accepts<char8_t>);
    // [unsigned_integer]
    std::string out;
    cbor::encoder e(out);
    bool const written = e.unsigned_integer(std::uint16_t{5}).has_value();
    auto const size = e.finish();  // out holds 19 00 05
    // [unsigned_integer]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\x19\x00\x05"s);
}

// encode.hpp, snippet negative_integer: the argument 499 stands for -500.
TEST_CASE("doc reference item: encoder::negative_integer")
{
    // [negative_integer]
    std::string out;
    cbor::encoder e(out);
    bool const written = e.negative_integer(std::uint16_t{499}).has_value();
    auto const size = e.finish();  // out holds 39 01 f3, the value -500
    // [negative_integer]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\x39\x01\xf3"s);
}

// encode.hpp, snippet byte_string: a span of two bytes; a null owner is a wrong use.
TEST_CASE("doc reference item: encoder::byte_string")
{
    // [byte_string]
    std::array<std::uint8_t, 2> const ab{97, 98};
    std::string out;
    cbor::encoder e(out);
    bool const written = e.byte_string(std::as_bytes(std::span(ab))).has_value();
    auto const size = e.finish();  // out holds 42 61 62
    // [byte_string]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\x42\x61\x62"s);

    std::string other;
    cbor::encoder f(other);
    CHECK_THROWS_AS((void)f.byte_string(std::shared_ptr<std::vector<std::byte> const>()), std::logic_error);
}

// encode.hpp, snippet text_string: a literal as a view.
TEST_CASE("doc reference item: encoder::text_string")
{
    // [text_string]
    std::string out;
    cbor::encoder e(out);
    bool const written = e.text_string("abc").has_value();
    auto const size = e.finish();  // out holds 63 61 62 63
    // [text_string]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\x63\x61\x62\x63"s);
}

// encode.hpp, snippet array: the head and then the two elements.
TEST_CASE("doc reference item: encoder::array")
{
    // [array]
    std::string out;
    cbor::encoder e(out);
    bool const written = e.array(2) && e.unsigned_integer(std::uint8_t{1}) && e.unsigned_integer(std::uint8_t{2});
    auto const size = e.finish();  // out holds 82 18 01 18 02
    // [array]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\x82\x18\x01\x18\x02"s);
}

// encode.hpp, snippet map: the head and then one key and its value.
TEST_CASE("doc reference item: encoder::map")
{
    // [map]
    std::string out;
    cbor::encoder e(out);
    bool const written = e.map(1) && e.text_string("a") && e.unsigned_integer(std::uint8_t{5});
    auto const size = e.finish();  // out holds a1 61 61 18 05
    // [map]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\xa1\x61\x61\x18\x05"s);
}

// encode.hpp, snippet tag: tag 55799 takes a head of three bytes.
TEST_CASE("doc reference item: encoder::tag")
{
    // [tag]
    std::string out;
    cbor::encoder e(out);
    bool const written = e.tag(55799) && e.unsigned_integer(std::uint8_t{1});
    auto const size = e.finish();  // out holds d9 d9 f7 18 01
    // [tag]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\xd9\xd9\xf7\x18\x01"s);
}

// encode.hpp, snippet simple_value: null, a value of two bytes, and a reserved value that ends the encoder.
TEST_CASE("doc reference item: encoder::simple_value")
{
    // [simple_value]
    std::string out;
    cbor::encoder e(out);
    bool const written =
        e.simple_value(cbor::simple_value::null) && e.simple_value(static_cast<cbor::simple_value>(32));
    auto const size = e.finish();  // out holds f6 f8 20
    // [simple_value]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\xf6\xf8\x20"s);

    std::string other;
    cbor::encoder f(other);
    CHECK_EQ(f.simple_value(static_cast<cbor::simple_value>(24)).error(), cbor::error::reserved_simple_value);
    CHECK_THROWS_AS((void)f.simple_value(cbor::simple_value::null), std::logic_error);
}

// encode.hpp, snippet floating_point: a float takes 32 bits; a 16-bit type takes 16 bits.
TEST_CASE("doc reference item: encoder::floating_point")
{
    // [floating_point]
    std::string out;
    cbor::encoder e(out);
    bool const written = e.floating_point(1.5f).has_value();
    auto const size = e.finish();  // out holds fa 3f c0 00 00
    // [floating_point]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\xfa\x3f\xc0\x00\x00"s);
#if defined(__STDCPP_FLOAT16_T__)
    std::string half;
    cbor::encoder h(half);
    REQUIRE(h.floating_point(static_cast<std::float16_t>(1.5f)));
    REQUIRE(h.finish());
    CHECK_EQ(half, "\xf9\x3e\x00"s);
#endif
}

// encode.hpp, snippet typed_array: tag 64 over two bytes; a tag that names no typed array; bytes that are no whole
// elements.
TEST_CASE("doc reference item: encoder::typed_array")
{
    // [typed_array]
    std::array<std::uint8_t, 2> const two{1, 2};
    std::string out;
    cbor::encoder e(out);
    bool const written = e.typed_array(cbor::typed_array{64, std::as_bytes(std::span(two))}).has_value();
    auto const size = e.finish();  // out holds d8 40 42 01 02, the tag 64 for uint8
    // [typed_array]
    CHECK(written);
    CHECK(size.has_value());
    CHECK_EQ(out, "\xd8\x40\x42\x01\x02"s);

    std::array<std::uint8_t, 3> const three{1, 2, 3};
    std::string other;
    cbor::encoder f(other);
    CHECK_EQ(f.typed_array(cbor::typed_array{1, std::as_bytes(std::span(two))}).error(),
             cbor::error::unsupported_value);
    std::string third;
    cbor::encoder g(third);
    CHECK_EQ(g.typed_array(cbor::typed_array{65, std::as_bytes(std::span(three))}).error(),
             cbor::error::inadmissible_type_for_tag_content);
}

// encode.hpp, snippet finish: the count of bytes in a span; every call after finish is a wrong use.
TEST_CASE("doc reference item: encoder::finish")
{
    // [finish]
    std::array<char, 16> buffer{};
    cbor::encoder e{std::span<char>(buffer)};
    bool const written = e.text_string("abc").has_value();
    auto const size = e.finish();  // 4: buffer starts with 63 61 62 63
    // e.array(0) now throws std::logic_error
    // [finish]
    CHECK(written);
    REQUIRE(size);
    CHECK_EQ(*size, 4u);
    CHECK_EQ(std::string_view(buffer.data(), *size), "\x63\x61\x62\x63"sv);
    CHECK_THROWS_AS((void)e.array(0), std::logic_error);
    CHECK_THROWS_AS((void)e.finish(), std::logic_error);
}

// The iov forms exist where the platform has <sys/uio.h>, as in the library (encode.hpp); Windows has none.
#if __has_include(<sys/uio.h>)
// encode.hpp, snippet iov_writer: a long text with its owner becomes a piece of its own, with no copy.
TEST_CASE("doc reference item: iov_writer")
{
    auto const text = std::make_shared<std::string const>(std::string(40, 'x'));
    // [iov_writer]
    // text: a std::shared_ptr<std::string const> with 40 times "x"
    cbor::iov_writer w;
    cbor::encoder e(w);
    bool const written = e.text_string(text).has_value();
    auto const pieces = e.finish();  // 2 pieces: the head 78 28, then *text with no copy
    // [iov_writer]
    CHECK(written);
    REQUIRE(pieces);
    REQUIRE_EQ((*pieces)->size(), 2u);
    auto const head = test::joined((*pieces)->first(1));
    REQUIRE(head.has_value());
    CHECK_EQ(*head, "\x78\x28"sv);
    CHECK_EQ(static_cast<void const *>((**pieces)[1].iov_base), static_cast<void const *>(text->data()));
    CHECK_EQ((**pieces)[1].iov_len, text->size());
    auto const sent = test::joined(**pieces);
    REQUIRE(sent.has_value());
    CHECK_EQ(cbor::diagnostic_notation(*sent), "\"" + *text + "\"");
}
#endif
