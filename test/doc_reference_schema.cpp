#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"


#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of a page in doc/reference/schema/. The tests exist so that every example
// compiles and gives the result that its page states.

namespace doc_reference_schema
{

struct tire {
    std::uint16_t diameter;
    float pressure;
};

struct car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
};

struct point {
    float x;
    float y;
};

} // namespace doc_reference_schema

template <>
struct cbor::describe_members<doc_reference_schema::tire> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1800};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_reference_schema::tire, diameter),
        CBOR_MEMBER(doc_reference_schema::tire, pressure),
    };
};

template <>
struct cbor::describe_members<doc_reference_schema::car> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1801};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_reference_schema::car, make),
        CBOR_MEMBER(doc_reference_schema::car, seats),
        CBOR_MEMBER(doc_reference_schema::car, tires),
        CBOR_MEMBER(doc_reference_schema::car, track),
    };
};

template <>
struct cbor::describe_members<doc_reference_schema::point> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1802};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_reference_schema::point, x),
        CBOR_MEMBER(doc_reference_schema::point, y),
    };
};

namespace doc_reference_schema
{

car const sample{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}};

std::string sample_bytes()
{
    auto bytes = cbor::packed<car>::encode(sample);
    REQUIRE(bytes.has_value());
    return std::move(*bytes);
}

unsigned char byte_at(std::string_view const bytes, std::size_t const i)
{
    return static_cast<unsigned char>(bytes[i]);
}

} // namespace doc_reference_schema

using namespace doc_reference_schema;

// encode.md: the first form returns a new string, the second appends to a std::string and returns the count it
// appended, and a span that is too small gives no_buffer_space.
TEST_CASE("doc reference schema: encode")
{
    auto const bytes = cbor::packed<tire>::encode(tire{17, 2.5f});
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::packed<tire>::at_path<"$.diameter">(*bytes), 17);

    std::string out = "x";
    auto const n = cbor::packed<tire>::encode(tire{17, 2.5f}, out);
    REQUIRE(n.has_value());
    CHECK_EQ(out.size(), 1 + *n);
    CHECK_EQ(out.substr(1), *bytes);

    std::array<char, 4> small{};
    CHECK_EQ(cbor::packed<tire>::encode(tire{17, 2.5f}, std::span<char>(small)).error(), error::no_buffer_space);
}

// decode.md: a moved string gives the whole struct; a message of another struct gives incorrect_type, a message
// that is too short gives too_little_data, and an empty owner throws std::logic_error.
TEST_CASE("doc reference schema: decode")
{
    auto const back = cbor::packed<car>::decode(sample_bytes());
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->make, "Volvo");
    CHECK_EQ((*back)->tires.size(), 2u);
    CHECK_EQ((*back)->track[2], 3.0);

    CHECK_EQ(cbor::packed<tire>::decode(sample_bytes()).error(), error::incorrect_type);
    CHECK_EQ(cbor::packed<car>::decode(sample_bytes().substr(0, 3)).error(), error::too_little_data);
    CHECK_THROWS_AS(std::ignore = cbor::packed<car>::decode(std::shared_ptr<std::string const>()), std::logic_error);
}

// path.md: path takes a moved string or an owner and gives the root accessor; a null owner throws.
TEST_CASE("doc reference schema: path")
{
    auto const root = cbor::packed<car>::path(sample_bytes());
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.make">(), "Volvo"sv);

    auto const owner = std::make_shared<std::string const>(sample_bytes());
    auto const shared = cbor::packed<car>::path(owner);
    REQUIRE(shared.has_value());
    CHECK_EQ(shared->at_path<"$.seats">(), 5);

    CHECK_EQ(cbor::packed<tire>::path(sample_bytes()).error(), error::incorrect_type);
    CHECK_THROWS_AS(std::ignore = cbor::packed<car>::path(std::shared_ptr<std::string const>()), std::logic_error);
}

// at_path.md: the static at_path reads one number with no accessor; [] takes the index from the next argument.
TEST_CASE("doc reference schema: static at_path")
{
    std::string const bytes = sample_bytes();
    CHECK_EQ(cbor::packed<car>::at_path<"$.seats">(bytes), 5);
    CHECK_EQ(cbor::packed<car>::at_path<"$.tires[1].diameter">(bytes), 18);
    CHECK_EQ(cbor::packed<car>::at_path<"$.tires[].pressure">(bytes, 0uz), 2.5f);
    CHECK_EQ(cbor::packed<car>::at_path<"$.track[2]">(bytes), 3.0);
    CHECK_EQ(cbor::packed<car>::at_path<"$.tires[].diameter">(bytes, 5uz).error(), error::index_out_of_bounds);
    CHECK_EQ(cbor::packed<car>::at_path<"$.seats">(bytes.substr(0, 3)).error(), error::too_little_data);
}

// unverified.md: the unverified forms give the values of the verified forms and still check an index against its
// count; an unverified accessor gives an unverified accessor.
TEST_CASE("doc reference schema: unverified")
{
    std::string const bytes = sample_bytes();
    CHECK_EQ(cbor::packed<car>::unverified_at_path<"$.seats">(bytes), 5);
    CHECK_EQ(cbor::packed<car>::unverified_at_path<"$.tires[].diameter">(bytes, 5uz).error(), error::index_out_of_bounds);

    auto const lane = cbor::packed<car>::unverified_path(std::string(bytes));
    REQUIRE(lane.has_value());
    CHECK_EQ(lane->at_path<"$.tires[1].diameter">(), 18);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*lane)>, cbor::packed<car>::accessor<car, false>>);

    auto const back = cbor::packed<car>::unverified_decode(std::make_shared<std::string const>(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->make, "Volvo");
}

// description.md: the data form and the description together have the size of the message, the data form reads
// with apart, and a read without apart refuses it.
TEST_CASE("doc reference schema: description")
{
    constexpr auto apart = cbor::description_place::apart;
    auto const data = cbor::packed<car>::encode<apart>(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}});
    REQUIRE(data.has_value());
    auto const description = cbor::packed<car>::description();
    CHECK_EQ(data->size() + description.size(), sample_bytes().size());
    CHECK_EQ((cbor::packed<car>::at_path<"$.seats", apart>(*data)), 5);
    auto const refused = cbor::packed<car>::at_path<"$.seats">(*data).error();
    CHECK((refused == error::incorrect_type || refused == error::too_little_data));
    auto const root = cbor::packed<car>::path<apart>(std::string(*data));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.tires[1].diameter">(), 18);
}

// fixed_size.md: the root of tire takes 3 bytes of class tag, 2 bytes of tag 128, 5 bytes of record head, 3 bytes
// for the std::uint16_t and 5 for the float. A text is a reference 6(N) of 6 bytes. The root ends the message.
TEST_CASE("doc reference schema: fixed_size")
{
    static_assert(cbor::packed<tire>::fixed_size() == 18);
    CHECK_EQ(cbor::packed<tire>::fixed_size(), 18u);
    CHECK_EQ(cbor::packed<tire>::fixed_size<std::uint16_t>(), 3u);
    CHECK_EQ(cbor::packed<tire>::fixed_size<double>(), 9u);
    CHECK_EQ(cbor::packed<car>::fixed_size<std::string>(), 6u);
    auto const bytes = cbor::packed<tire>::encode(tire{17, 2.5f});
    REQUIRE(bytes.has_value());
    std::size_t const root = bytes->size() - cbor::packed<tire>::fixed_size();
    CHECK_EQ(byte_at(*bytes, root), 0xd9);
}

// member_offset.md: the values follow the key order of RFC 8949 4.2.1. "diameter" and "pressure" have the same
// length and "d" sorts first, so diameter is at 10 and pressure behind its 3 bytes at 13. 0xfa is the head of a
// binary32.
TEST_CASE("doc reference schema: member_offset")
{
    CHECK_EQ(cbor::packed<tire>::member_offset<&tire::diameter>(), 10u);
    CHECK_EQ(cbor::packed<tire>::member_offset<&tire::pressure>(), 13u);
    auto const bytes = cbor::packed<tire>::encode(tire{17, 2.5f});
    REQUIRE(bytes.has_value());
    std::size_t const root = bytes->size() - cbor::packed<tire>::fixed_size();
    CHECK_EQ(byte_at(*bytes, root + cbor::packed<tire>::member_offset<&tire::pressure>()), 0xfa);
}

// accessor.md: @ starts at the part that the accessor names and $ at the root; size() counts a list; a vector of
// fixed-size structs is a range of element accessors; view gives a list of numbers.
TEST_CASE("doc reference schema: accessor")
{
    auto const root = cbor::packed<car>::path(sample_bytes());
    REQUIRE(root.has_value());
    auto const tires = root->at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 2u);
    CHECK_EQ(tires->at_path<"@[1].diameter">(), 18);
    CHECK_EQ(tires->at_path<"$.seats">(), 5);
    std::vector<std::uint16_t> seen;
    for (auto const t : *tires)
        seen.push_back(*t.at_path<"@.diameter">());
    CHECK_EQ(seen, std::vector<std::uint16_t>{17, 18});
    CHECK(std::ranges::random_access_range<cbor::packed<car>::accessor<std::vector<tire>> const &>);
    auto const track = root->view<"$.track">();
    REQUIRE(track.has_value());
    CHECK_EQ(track->size(), 3u);
    CHECK_EQ(tires->at_path<"@[]">(2uz).error(), error::index_out_of_bounds);
}

// fixed_element.md: an element of a vector of fixed-size structs reads its members with @ and the root with $.
TEST_CASE("doc reference schema: fixed_element")
{
    auto const root = cbor::packed<car>::path(sample_bytes());
    REQUIRE(root.has_value());
    auto const second = root->at_path<"$.tires[]">(1uz);
    REQUIRE(second.has_value());
    CHECK(std::same_as<std::remove_cvref_t<decltype(*second)>, cbor::packed<car>::accessor<cbor::fixed_element<tire>>>);
    CHECK_EQ(second->at_path<"@.diameter">(), 18);
    CHECK_EQ(second->at_path<"@.pressure">(), 2.25f);
    CHECK_EQ(second->at_path<"$.seats">(), 5);
}

// typed_array_view.md: the view is a random access range of values; at, first and last give an error as a value,
// operator[] outside the view throws.
TEST_CASE("doc reference schema: typed_array_view")
{
    auto const root = cbor::packed<car>::path(sample_bytes());
    REQUIRE(root.has_value());
    auto const track = root->view<"$.track">();
    REQUIRE(track.has_value());
    CHECK(std::ranges::random_access_range<cbor::typed_array_view<double> const &>);
    CHECK(std::ranges::sized_range<cbor::typed_array_view<double> const &>);
    CHECK_EQ(track->size(), 3u);
    CHECK_FALSE(track->empty());
    CHECK_EQ(std::accumulate(track->begin(), track->end(), 0.0), 6.0);
    CHECK_EQ((*track)[1], 2.0);
    CHECK_EQ(track->front(), 1.0);
    CHECK_EQ(track->back(), 3.0);
    CHECK_EQ(*track->rbegin(), 3.0);
    CHECK_EQ(track->at(3).error(), error::index_out_of_bounds);
    CHECK_THROWS_AS(std::ignore = (*track)[3], std::logic_error);
    CHECK_EQ(track->last(2)->front(), 2.0);
    CHECK_EQ(track->first(4).error(), error::index_out_of_bounds);
}

// tag.md: the class tag is the first item of the root; 1802 is 0x070a and takes the two-byte argument d9 07 0a.
TEST_CASE("doc reference schema: tag")
{
    auto const bytes = cbor::packed<point>::encode(point{1, 2});
    REQUIRE(bytes.has_value());
    std::size_t const root = bytes->size() - cbor::packed<point>::fixed_size();
    CHECK_EQ(byte_at(*bytes, root), 0xd9);
    CHECK_EQ(byte_at(*bytes, root + 1), 0x07);
    CHECK_EQ(byte_at(*bytes, root + 2), 0x0a);
}

// wire.md: the message starts with tag 113 on an array of two and a table with a four-byte count, the first entry
// of the table is a tag 114. The vector of tires is tag 32873 (d9 80 69), the list of double is RFC 8746 tag 86
// (binary64, little endian, d8 56), and the text is a reference 6(N) with a four-byte N (c6 1a).
TEST_CASE("doc reference schema: wire")
{
    std::string const bytes = sample_bytes();
    CHECK_EQ(byte_at(bytes, 0), 0xd8);
    CHECK_EQ(byte_at(bytes, 1), 0x71);
    CHECK_EQ(byte_at(bytes, 2), 0x82);
    CHECK_EQ(byte_at(bytes, 3), 0x9a);
    CHECK_EQ(byte_at(bytes, 8), 0xd8);
    CHECK_EQ(byte_at(bytes, 9), 0x72);
    CHECK_NE(bytes.find("\xd9\x80\x69"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x56"sv), std::string::npos);
    std::size_t const make = bytes.size() - cbor::packed<car>::fixed_size() + cbor::packed<car>::member_offset<&car::make>();
    CHECK_EQ(byte_at(bytes, make), 0xc6);
    CHECK_EQ(byte_at(bytes, make + 1), 0x1a);
    CHECK_EQ(cbor::item_size(bytes), bytes.size());
}

