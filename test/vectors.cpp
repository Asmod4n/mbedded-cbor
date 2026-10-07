#include "binding.hpp"

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

using cbor::error;

namespace
{

struct vector_case {
    std::string hex;
    bool roundtrip;
};

// Reads the "hex" and "roundtrip" members of each object of a test-vectors file. The files are
// plain arrays of flat objects, so a search for the two keys is enough.
std::vector<vector_case> cases_of(std::string const &file)
{
    std::ifstream in(file);
    REQUIRE(in.good());
    std::string const text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<vector_case> cases;
    std::size_t at = 0;
    while ((at = text.find("\"hex\"", at)) != std::string::npos) {
        std::size_t const open = text.find('"', text.find(':', at)) + 1;
        std::size_t const close = text.find('"', open);
        std::size_t const object_end = text.find('}', close);
        std::size_t const roundtrip = text.find("\"roundtrip\": true", at);
        cases.push_back({text.substr(open, close - open), roundtrip < object_end});
        at = close;
    }
    return cases;
}

std::string bytes_of_hex(std::string_view const hex)
{
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned value = 0;
        std::from_chars(hex.substr(i, 2).data(), std::to_address(hex.substr(i, 2).end()), value, 16);
        bytes.push_back(static_cast<char>(value));
    }
    return bytes;
}

} // namespace

// Ported from test-vectors.rb: 'passes the official test vectors', the examples of RFC 8949
// Appendix A. Indefinite lengths are refused, as in mruby-cbor. Every other example decodes, and
// one that a generic encoder writes back identically comes back byte for byte. The skip list of
// mruby-cbor is not needed: the test binding keeps bytes, text, simple values and tags apart.
TEST_CASE("test vectors: RFC 8949 Appendix A")
{
    auto const cases = cases_of(TEST_VECTORS "/appendix_a.json");
    REQUIRE_EQ(cases.size(), 82);
    for (vector_case const &c : cases) {
        CAPTURE(c.hex);
        std::string const wire = bytes_of_hex(c.hex);
        auto const v = decoded(wire);
        if (!v) {
            CHECK_EQ(v.error(), error::indefinite_length);
            continue;
        }
        if (c.roundtrip)
            CHECK_EQ(encoded(*v), wire);
    }
}

// fail.json holds data items that are not well-formed. Read as exactly one data item, none may
// succeed: decode fails, or bytes remain after the item, which RFC 8949 Appendix F calls too much
// data. cbor::decode itself leaves trailing bytes alone, as CBOR.decode of mruby-cbor does.
TEST_CASE("test vectors: not well-formed items fail")
{
    auto const cases = cases_of(TEST_VECTORS "/fail.json");
    REQUIRE_GT(cases.size(), 600);
    for (vector_case const &c : cases) {
        CAPTURE(c.hex);
        std::string const wire = bytes_of_hex(c.hex);
        auto const end = cbor::item_end<16>(wire);
        bool const one_item = end.has_value() && *end == wire.size();
        CHECK_FALSE((one_item && decoded(wire).has_value()));
    }
}

// RFC 8949 section 3: the head of every example gives the major type in the high 3 bits, the additional information
// in the low 5 bits and the argument after it. The item of each example keeps these fields from the wire, and its
// content follows section 3.1: a string is the bytes after the head, an array and a map hold as many members as the
// argument says, a tag points to its content, a float holds the bits of the argument in the width of the additional
// information. Indefinite lengths are refused, as cbor::decode refuses them.
TEST_CASE("test vectors: RFC 8949 Appendix A decodes to items with the head fields of the wire")
{
    auto const cases = cases_of(TEST_VECTORS "/appendix_a.json");
    REQUIRE_EQ(cases.size(), 82);
    for (vector_case const &c : cases) {
        CAPTURE(c.hex);
        std::string const wire = bytes_of_hex(c.hex);
        auto const top_level = cbor::lazy::from(wire);
        REQUIRE(top_level.has_value());
        auto const decoded_item = top_level->decode();
        auto const initial = static_cast<std::uint8_t>(wire.front());
        if ((initial & 0x1f) == 31) {
            CHECK_EQ(decoded_item.error(), error::indefinite_length);
            continue;
        }
        if (!decoded_item) {
            CHECK_EQ(decoded_item.error(), error::indefinite_length);
            continue;
        }
        cbor::item const &it = decoded_item->get();
        std::uint8_t const info = initial & 0x1f;
        std::size_t const argument_bytes = info < 24 ? 0 : std::size_t{1} << (info - 24);
        std::uint64_t argument = info < 24 ? info : 0;
        for (std::size_t i = 1; i <= argument_bytes; ++i)
            argument = argument << 8 | static_cast<std::uint8_t>(wire[i]);
        CHECK_EQ(std::to_underlying(it.major_type), initial >> 5);
        CHECK_EQ(it.additional_information, info);
        CHECK_EQ(it.argument, argument);
        std::string_view const after_head = std::string_view(wire).substr(1 + argument_bytes);
        switch (it.major_type) {
        case cbor::major_type::unsigned_integer:
        case cbor::major_type::negative_integer:
            CHECK(std::holds_alternative<std::monostate>(it.content));
            break;
        case cbor::major_type::byte_string:
            REQUIRE(std::holds_alternative<std::span<std::byte const>>(it.content));
            CHECK(std::ranges::equal(std::get<std::span<std::byte const>>(it.content), std::as_bytes(std::span(after_head))));
            break;
        case cbor::major_type::text_string:
            REQUIRE(std::holds_alternative<std::string_view>(it.content));
            CHECK_EQ(std::get<std::string_view>(it.content), after_head);
            break;
        case cbor::major_type::array:
        case cbor::major_type::map:
            REQUIRE(std::holds_alternative<std::span<std::byte const>>(it.content));
            CHECK(std::ranges::equal(std::get<std::span<std::byte const>>(it.content), std::as_bytes(std::span(after_head))));
            break;
        case cbor::major_type::tag:
            REQUIRE(std::holds_alternative<cbor::item const *>(it.content));
            CHECK_EQ(std::to_underlying(std::get<cbor::item const *>(it.content)->major_type),
                     static_cast<std::uint8_t>(after_head.front()) >> 5);
            break;
        case cbor::major_type::simple_float:
            if (info == 25) {
#if defined(__STDCPP_FLOAT16_T__)
                REQUIRE(std::holds_alternative<std::float16_t>(it.content));
                CHECK_EQ(std::bit_cast<std::uint16_t>(std::get<std::float16_t>(it.content)), argument);
#else
                REQUIRE(std::holds_alternative<float>(it.content));
#endif
            } else if (info == 26) {
                REQUIRE(std::holds_alternative<float>(it.content));
                CHECK_EQ(std::bit_cast<std::uint32_t>(std::get<float>(it.content)), argument);
            } else if (info == 27) {
                REQUIRE(std::holds_alternative<double>(it.content));
                CHECK_EQ(std::bit_cast<std::uint64_t>(std::get<double>(it.content)), argument);
            } else {
                CHECK(std::holds_alternative<std::monostate>(it.content));
            }
            break;
        }
    }
}
