#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

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

// Each test in this file is one example of a page in doc/reference/item/. The tests exist so that every
// example compiles and gives the result that its page states.

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

// item.md: the kind of the root and the count of its elements; only a decode makes an item.
TEST_CASE("doc reference item: item")
{
    std::string const bytes = test::preferred(test::A(1, test::M("a"s, 2)));
    auto const tree = cbor::decode(bytes);
    REQUIRE(tree);
    CHECK_EQ((*tree)->type, cbor::type::array);
    CHECK_EQ((*tree)->get<cbor::type::array>()->size(), 2u);
    CHECK_FALSE(std::is_constructible_v<cbor::item, cbor::type, std::uint8_t>);
    CHECK(std::is_trivially_destructible_v<cbor::item>);
}

// get.md: a text key, the argument of a negative integer, and a wrong kind.
TEST_CASE("doc reference item: item::get")
{
    std::string const bytes = test::preferred(test::M("a"s, test::negative{2}));
    auto const tree = cbor::decode(bytes);
    REQUIRE(tree);
    auto const pairs = *(*tree)->get<cbor::type::map>();
    REQUIRE_EQ(pairs.size(), 2u);
    CHECK_EQ(pairs[0].get<cbor::type::text_string>().value(), "a"sv);
    CHECK_EQ(pairs[1].get<cbor::type::negative_integer>().value(), 2u);
    CHECK_EQ(pairs[1].get<cbor::type::unsigned_integer>().error(), cbor::error::incorrect_type);
}

// decode.md: one item, a byte after it, a leading tag 55799, an indefinite length, and a tree that outlives
// its input.
TEST_CASE("doc reference item: decode")
{
    std::string const bytes = test::preferred(test::A("hi"s, 1));
    auto const tree = cbor::decode(bytes);
    REQUIRE(tree);
    CHECK_EQ((*tree)->get<cbor::type::array>()->size(), 2u);
    CHECK(cbor::decode("\x01\x02"s).has_value());
    auto const self_described = cbor::decode("\xd9\xd9\xf7\x01"s);
    CHECK_EQ((*self_described)->type, cbor::type::tag);
    CHECK_EQ(cbor::decode("\x9f\xff"s).error(), cbor::error::indefinite_length);
    CHECK_EQ(cbor::decode("\x82\x01"s).error(), cbor::error::too_little_data);

    auto input = std::make_unique<std::string>(bytes);
    auto const copy = cbor::decode(*input);
    input.reset();
    REQUIRE(copy);
    CHECK_EQ((*copy)->get<cbor::type::array>().value()[0].get<cbor::type::text_string>().value(), "hi"sv);
}

// encoded_cbor_data_item.md: the content of a tag 24, a content that is no byte string, and a wrong node.
TEST_CASE("doc reference item: encoded_cbor_data_item")
{
    std::string const bytes = "\xd8\x18\x42\x61\x61"s;
    auto const tree = cbor::decode(bytes);
    REQUIRE(tree);
    auto const inner = cbor::encoded_cbor_data_item(*tree, **tree);
    REQUIRE(inner);
    CHECK_EQ(inner->get().get<cbor::type::text_string>().value(), "a"sv);

    auto const no_bytes = cbor::decode("\xd8\x18\x01"s);
    REQUIRE(no_bytes);
    CHECK_EQ(cbor::encoded_cbor_data_item(*no_bytes, **no_bytes).error(), cbor::error::incorrect_type);

    auto const array = cbor::decode("\x81\x01"s);
    REQUIRE(array);
    CHECK_THROWS_AS((void)cbor::encoded_cbor_data_item(*array, (*array)->get<cbor::type::array>().value()[0]),
                    std::logic_error);
}

// encode.md: a container target gives the input again; a short span and a writer of the program.
TEST_CASE("doc reference item: encode")
{
    std::string const bytes = test::preferred(test::A(1, "a"s));
    auto const tree = cbor::decode(bytes);
    REQUIRE(tree);
    std::string out;
    CHECK_EQ(cbor::encode(**tree, out).value(), 4u);
    CHECK_EQ(out, bytes);

    std::array<char, 2> small{};
    CHECK_EQ(cbor::encode(**tree, std::span<char>(small)).error(), cbor::error::no_buffer_space);

    std::string written;
    CHECK_EQ(cbor::encode(**tree, writer_target{&written, false}).value(), 4u);
    CHECK_EQ(written, bytes);
    CHECK_EQ(cbor::encode(**tree, writer_target{&written, true}).error(), cbor::error::io_error);
}

// encode_iov.md: the joined pieces are the bytes of encode.
TEST_CASE("doc reference item: encode_iov")
{
    std::string const bytes = test::preferred(test::A(1, "a"s));
    auto const tree = cbor::decode(bytes);
    REQUIRE(tree);
    auto const pieces = cbor::encode_iov(**tree);
    REQUIRE(pieces);
    std::string joined;
    for (std::string_view const piece : **pieces)
        joined += piece;
    CHECK_EQ(joined, bytes);
}

// encoder.md: a map written call by call; a copy does not compile.
TEST_CASE("doc reference item: encoder")
{
    CHECK_FALSE(std::is_copy_constructible_v<cbor::encoder<std::string>>);
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.map(1));
    REQUIRE(e.text_string("a"));
    REQUIRE(e.unsigned_integer(std::uint8_t{5}));
    CHECK_EQ(e.finish().value(), 5u);
    CHECK_EQ(out, "\xa1\x61\x61\x18\x05"s);
}

// encoder_unsigned_integer.md: the head takes the width of the type; bool is no integer argument.
TEST_CASE("doc reference item: encoder::unsigned_integer")
{
    CHECK_FALSE(unsigned_integer_accepts<bool>);
    CHECK_FALSE(unsigned_integer_accepts<char8_t>);
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.unsigned_integer(std::uint16_t{5}));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\x19\x00\x05"s);
}

// encoder_negative_integer.md: the argument 499 stands for -500.
TEST_CASE("doc reference item: encoder::negative_integer")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.negative_integer(std::uint16_t{499}));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\x39\x01\xf3"s);
}

// encoder_byte_string.md: a span of two bytes; a null owner is a wrong use.
TEST_CASE("doc reference item: encoder::byte_string")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.byte_string(std::as_bytes(std::span("ab", 2))));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\x42\x61\x62"s);

    std::string other;
    cbor::encoder f(other);
    CHECK_THROWS_AS((void)f.byte_string(std::shared_ptr<std::vector<std::byte> const>()), std::logic_error);
}

// encoder_text_string.md: a literal as a view.
TEST_CASE("doc reference item: encoder::text_string")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.text_string("abc"));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\x63\x61\x62\x63"s);
}

// encoder_array.md: the head and then the two elements.
TEST_CASE("doc reference item: encoder::array")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.array(2));
    REQUIRE(e.unsigned_integer(std::uint8_t{1}));
    REQUIRE(e.unsigned_integer(std::uint8_t{2}));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\x82\x18\x01\x18\x02"s);
}

// encoder_map.md: the head and then one key and its value.
TEST_CASE("doc reference item: encoder::map")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.map(1));
    REQUIRE(e.text_string("a"));
    REQUIRE(e.unsigned_integer(std::uint8_t{5}));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\xa1\x61\x61\x18\x05"s);
}

// encoder_tag.md: tag 55799 takes a head of three bytes.
TEST_CASE("doc reference item: encoder::tag")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.tag(55799));
    REQUIRE(e.unsigned_integer(std::uint8_t{1}));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\xd9\xd9\xf7\x18\x01"s);
}

// encoder_simple_value.md: null, a value of two bytes, and a reserved value that ends the encoder.
TEST_CASE("doc reference item: encoder::simple_value")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.simple_value(cbor::simple_value::null));
    REQUIRE(e.simple_value(static_cast<cbor::simple_value>(32)));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\xf6\xf8\x20"s);

    std::string other;
    cbor::encoder f(other);
    CHECK_EQ(f.simple_value(static_cast<cbor::simple_value>(24)).error(), cbor::error::reserved_simple_value);
    CHECK_THROWS_AS((void)f.simple_value(cbor::simple_value::null), std::logic_error);
}

// encoder_floating_point.md: a float takes 32 bits; a 16-bit type takes 16 bits.
TEST_CASE("doc reference item: encoder::floating_point")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.floating_point(1.5f));
    REQUIRE(e.finish());
    CHECK_EQ(out, "\xfa\x3f\xc0\x00\x00"s);
#if defined(__STDCPP_FLOAT16_T__)
    std::string half;
    cbor::encoder h(half);
    REQUIRE(h.floating_point(static_cast<std::float16_t>(1.5f)));
    REQUIRE(h.finish());
    CHECK_EQ(half, "\xf9\x3e\x00"s);
#endif
}

// encoder_typed_array.md: tag 64 over two bytes; a tag that names no typed array; bytes that are no whole
// elements.
TEST_CASE("doc reference item: encoder::typed_array")
{
    std::array<std::uint8_t, 2> const two{1, 2};
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.typed_array(cbor::typed_array{64, std::as_bytes(std::span(two))}));
    REQUIRE(e.finish());
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

// encoder_finish.md: the count of bytes in a span; every call after finish is a wrong use.
TEST_CASE("doc reference item: encoder::finish")
{
    std::array<char, 16> buffer{};
    cbor::encoder e{std::span<char>(buffer)};
    REQUIRE(e.text_string("abc"));
    auto const size = e.finish();
    REQUIRE(size);
    CHECK_EQ(*size, 4u);
    CHECK_EQ(std::string_view(buffer.data(), *size), "\x63\x61\x62\x63"sv);
    CHECK_THROWS_AS((void)e.array(0), std::logic_error);
    CHECK_THROWS_AS((void)e.finish(), std::logic_error);
}

// iov_writer.md: a long text with its owner becomes a piece of its own, with no copy.
TEST_CASE("doc reference item: iov_writer")
{
    auto const text = std::make_shared<std::string const>(std::string(40, 'x'));
    cbor::iov_writer w;
    cbor::encoder e(w);
    REQUIRE(e.text_string(text));
    auto const pieces = e.finish();
    REQUIRE(pieces);
    REQUIRE_EQ((*pieces)->size(), 2u);
    CHECK_EQ((**pieces)[0], "\x78\x28"sv);
    CHECK_EQ((**pieces)[1].data(), text->data());
}
