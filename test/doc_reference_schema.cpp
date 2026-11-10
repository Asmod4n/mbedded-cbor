#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "doc_types.hpp"
#include "reference.hpp"


#include <bit>
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

// Each test in this file is one example of a reference page of the schema group; the comments of schema.hpp and
// rfc8949.hpp take the examples through the snippet markers. The tests exist so that every example compiles and gives
// the result that its page states.

namespace doc_reference_schema
{

car const sample{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}};

std::string sample_bytes()
{
    auto bytes = cbor::schema<car>::encode(sample);
    REQUIRE(bytes.has_value());
    return std::move(*bytes);
}

unsigned char byte_at(std::string_view const bytes, std::size_t const i)
{
    return static_cast<unsigned char>(bytes[i]);
}

} // namespace doc_reference_schema

using namespace doc_reference_schema;

// schema.hpp, snippet encode: the first form returns a new string, the second appends to a std::string and returns
// the count it
// appended, and a span that is too small gives no_buffer_space.
TEST_CASE("doc reference schema: encode")
{
    // [encode]
    auto const bytes = cbor::schema<tire>::encode(tire{17, 2.5f});
    std::string out = "x";
    auto const n = cbor::schema<tire>::encode(tire{17, 2.5f}, out);  // out.size() == 1 + *n
    // [encode]
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::schema<tire>::at_path<"$.diameter">(*bytes), 17);
    REQUIRE(n.has_value());
    CHECK_EQ(out.size(), 1 + *n);
    CHECK_EQ(out.substr(1), *bytes);

    std::array<char, 4> small{};
    CHECK_EQ(cbor::schema<tire>::encode(tire{17, 2.5f}, std::span<char>(small)).error(), error::no_buffer_space);
}

// schema.hpp, snippet decode: a moved string gives the whole struct; a message of another struct gives
// incorrect_type, a message
// that is too short gives too_little_data, and an empty owner throws std::logic_error.
TEST_CASE("doc reference schema: decode")
{
    std::string bytes = sample_bytes();
    // [decode]
    // bytes: a std::string from schema<car>::encode(car{"Volvo", 5, ...})
    auto const back = cbor::schema<car>::decode(std::move(bytes));
    std::string make;
    if (back)
        make = (*back)->make;  // "Volvo"
    // [decode]
    REQUIRE(back.has_value());
    CHECK_EQ(make, "Volvo");
    CHECK_EQ((*back)->tires.size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>((*back)->track[2]), 0x4008000000000000u);

    CHECK_EQ(cbor::schema<tire>::decode(sample_bytes()).error(), error::incorrect_type);
    CHECK_EQ(cbor::schema<car>::decode(sample_bytes().substr(0, 3)).error(), error::too_little_data);
    CHECK_THROWS_AS(std::ignore = cbor::schema<car>::decode(std::shared_ptr<std::string const>()), std::logic_error);
}

// schema.hpp, snippet path: path takes a moved string or an owner and gives the root accessor; a null owner throws.
TEST_CASE("doc reference schema: path")
{
    std::string bytes = sample_bytes();
    // [path]
    // bytes: a std::string from schema<car>::encode(car{"Volvo", 5, ...})
    auto const root = cbor::schema<car>::path(std::move(bytes));
    auto const make = root->at_path<"$.make">();  // "Volvo"
    // [path]
    REQUIRE(root.has_value());
    CHECK_EQ(make, "Volvo"sv);

    auto const owner = std::make_shared<std::string const>(sample_bytes());
    auto const shared = cbor::schema<car>::path(owner);
    REQUIRE(shared.has_value());
    CHECK_EQ(shared->at_path<"$.seats">(), 5);

    CHECK_EQ(cbor::schema<tire>::path(sample_bytes()).error(), error::incorrect_type);
    CHECK_THROWS_AS(std::ignore = cbor::schema<car>::path(std::shared_ptr<std::string const>()), std::logic_error);
}

// schema.hpp, snippet at_path: the static at_path reads one number with no accessor; [] takes the index from the
// next argument.
TEST_CASE("doc reference schema: static at_path")
{
    std::string const bytes = sample_bytes();
    // [at_path]
    // bytes: from schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}})
    auto const seats = cbor::schema<car>::at_path<"$.seats">(bytes);                 // 5
    auto const second = cbor::schema<car>::at_path<"$.tires[1].diameter">(bytes);    // 18
    auto const past = cbor::schema<car>::at_path<"$.tires[].diameter">(bytes, 5uz); // error::index_out_of_bounds
    // [at_path]
    CHECK_EQ(seats, 5);
    CHECK_EQ(second, 18);
    CHECK_EQ(past.error(), error::index_out_of_bounds);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[].pressure">(bytes, 0uz), 2.5f);
    CHECK_EQ(cbor::schema<car>::at_path<"$.track[2]">(bytes), 3.0);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[].diameter">(bytes, 5uz).error(), error::index_out_of_bounds);
    CHECK_EQ(cbor::schema<car>::at_path<"$.seats">(bytes.substr(0, 3)).error(), error::too_little_data);
}

// schema.hpp, snippet unverified: the unverified forms give the values of the verified forms and still check an
// index against its
// count; an unverified accessor gives an unverified accessor.
TEST_CASE("doc reference schema: unverified")
{
    std::string const bytes = sample_bytes();
    // [unverified]
    // bytes: from schema<car>::encode(car{"Volvo", 5, ...}), a peer that the program trusts wrote them
    auto const seats = cbor::schema<car>::unverified_at_path<"$.seats">(bytes);  // 5
    auto const lane = cbor::schema<car>::unverified_path(std::string(bytes));
    auto const diameter = lane->at_path<"$.tires[1].diameter">();               // 18
    // [unverified]
    CHECK_EQ(seats, 5);
    CHECK_EQ(diameter, 18);
    CHECK_EQ(cbor::schema<car>::unverified_at_path<"$.tires[].diameter">(bytes, 5uz).error(), error::index_out_of_bounds);

    REQUIRE(lane.has_value());
    CHECK_EQ(lane->at_path<"$.tires[1].diameter">(), 18);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*lane)>, cbor::schema<car>::accessor<car, false>>);

    auto const back = cbor::schema<car>::unverified_decode(std::make_shared<std::string const>(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->make, "Volvo");
}

// schema.hpp, snippet description: the data form and the description together have the size of the message, the data
// form reads
// with apart, and a read without apart refuses it.
TEST_CASE("doc reference schema: description")
{
    // [description]
    constexpr auto apart = cbor::description_place::apart;
    auto const data = cbor::schema<car>::encode<apart>(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}});
    auto const description = cbor::schema<car>::description();
    std::size_t const size = data->size() + description.size();          // the size of the message
    auto const seats = cbor::schema<car>::at_path<"$.seats", apart>(*data);  // 5
    // [description]
    REQUIRE(data.has_value());
    CHECK_EQ(size, sample_bytes().size());
    CHECK_EQ(seats, 5);
    CHECK_EQ(data->size() + description.size(), sample_bytes().size());
    CHECK_EQ((cbor::schema<car>::at_path<"$.seats", apart>(*data)), 5);
    // The data form holds no description, so at_path with the description inside reads a message that is shorter
    // than its table: the doc of schema::path gives too_little_data for that.
    auto const refused = cbor::schema<car>::at_path<"$.seats">(*data).error();
    CHECK_EQ(refused, error::too_little_data);
    auto const root = cbor::schema<car>::path<apart>(std::string(*data));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.tires[1].diameter">(), 18);
}

// schema.hpp, snippet fixed_size: the root of tire takes 3 bytes of class tag, 2 bytes of tag 128, 5 bytes of record
// head, 3 bytes
// for the std::uint16_t and 5 for the float. A text is a reference 6(N) of 6 bytes. The root ends the message.
TEST_CASE("doc reference schema: fixed_size")
{
    // [fixed_size]
    constexpr std::size_t root_record = cbor::schema<tire>::fixed_size();         // 18
    constexpr std::size_t number = cbor::schema<tire>::fixed_size<std::uint16_t>(); // 3
    constexpr std::size_t text = cbor::schema<car>::fixed_size<std::string>();     // 6
    // [fixed_size]
    CHECK_EQ(root_record, 18u);
    CHECK_EQ(number, 3u);
    CHECK_EQ(text, 6u);
    CHECK_EQ(cbor::schema<tire>::fixed_size<double>(), 9u);
    CHECK_EQ(cbor::schema<car>::fixed_size<std::string>(), 6u);
    auto const bytes = cbor::schema<tire>::encode(tire{17, 2.5f});
    REQUIRE(bytes.has_value());
    std::size_t const root = bytes->size() - cbor::schema<tire>::fixed_size();
    CHECK_EQ(byte_at(*bytes, root), 0xd9);
}

// schema.hpp, snippet member_offset: the values follow the key order of RFC 8949 4.2.1. "diameter" and "pressure"
// have the same
// length and "d" sorts first, so diameter is at 10 and pressure behind its 3 bytes at 13. 0xfa is the head of a
// binary32.
TEST_CASE("doc reference schema: member_offset")
{
    // [member_offset]
    // tire: std::uint16_t diameter; float pressure;
    constexpr std::size_t diameter = cbor::schema<tire>::member_offset<&tire::diameter>();  // 10
    constexpr std::size_t pressure = cbor::schema<tire>::member_offset<&tire::pressure>();  // 13
    // bytes[bytes.size() - fixed_size() + 13] is 0xfa, the head of a float
    // [member_offset]
    CHECK_EQ(diameter, 10u);
    CHECK_EQ(pressure, 13u);
    auto const bytes = cbor::schema<tire>::encode(tire{17, 2.5f});
    REQUIRE(bytes.has_value());
    std::size_t const root = bytes->size() - cbor::schema<tire>::fixed_size();
    CHECK_EQ(byte_at(*bytes, root + cbor::schema<tire>::member_offset<&tire::pressure>()), 0xfa);
}

// schema.hpp, snippet accessor: @ starts at the part that the accessor names and $ at the root; size() counts a
// list; a vector of
// fixed-size structs is a range of element accessors; view gives a list of numbers.
TEST_CASE("doc reference schema: accessor")
{
    std::string bytes = sample_bytes();
    std::vector<std::uint16_t> seen;
    // [accessor]
    // bytes: from schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}})
    auto const root = cbor::schema<car>::path(std::move(bytes));
    auto const tires = root->at_path<"$.tires">();
    auto const count = tires->size();                       // 2
    auto const second = tires->at_path<"@[1].diameter">();  // 18
    auto const seats = tires->at_path<"$.seats">();         // 5
    for (auto const t : *tires)
        seen.push_back(*t.at_path<"@.diameter">());         // 17, then 18
    auto const track = root->view<"$.track">();             // 3 numbers
    // [accessor]
    REQUIRE(root.has_value());
    REQUIRE(tires.has_value());
    CHECK_EQ(count, 2u);
    CHECK_EQ(second, 18);
    CHECK_EQ(seats, 5);
    CHECK_EQ(seen, std::vector<std::uint16_t>{17, 18});
    CHECK(std::ranges::random_access_range<cbor::schema<car>::accessor<std::vector<tire>> const &>);
    REQUIRE(track.has_value());
    CHECK_EQ(track->size(), 3u);
    CHECK_EQ(tires->at_path<"@[]">(2uz).error(), error::index_out_of_bounds);
}

// schema.hpp, snippet accessor variant: the accessor of a variant member gives the index of the held alternative and
// its accessor; the other
// alternative is incorrect_type.
TEST_CASE("doc reference schema: accessor of a variant member")
{
    auto const bytes = cbor::schema<home>::encode(home{dog{{"rex"}, 3}});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes,
             "\xd8\x71\x82\x9a\x00\x00\x00\x12\xd8\x72\x81\x63\x70\x65\x74\xd8\x72\x82\x64\x62\x61\x72\x6b\x64"
             "\x6e\x61\x6d\x65\xd8\x72\x82\x64\x6e\x61\x6d\x65\x64\x74\x6f\x79\x73\x5a\x00\x00\x00\x08\x00\x00"
             "\x00\x42\x00\x00\x00\x54\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xd9\x07\x0b\xd8\x81\x9a"
             "\x00\x00\x00\x02\x18\x03\xc6\x3a\x00\x00\x00\x00\x7a\x00\x00\x00\x03\x72\x65\x78\xd9\x07\x0d\xd8"
             "\x80\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x00"s);
    // [accessor variant]
    // home: struct { std::variant<dog, cat> pet; }; bytes: from schema<home>::encode(home{dog{{"rex"}, 3}})
    auto const doc = cbor::schema<home>::path(std::string(*bytes));
    auto const pet = doc->at_path<"$.pet">();
    auto const held = pet->index();                         // 0
    auto const name = pet->get<dog>()->at_path<"@.name">();  // "rex"
    auto const other = pet->get<cat>();                     // error::incorrect_type
    // [accessor variant]
    REQUIRE(doc.has_value());
    REQUIRE(pet.has_value());
    CHECK_EQ(held, 0u);
    CHECK_EQ(name, "rex"sv);
    CHECK_EQ(other.error(), error::incorrect_type);
    CHECK_EQ(pet->get<dog>()->at_path<"@.name">(), "rex"sv);
    CHECK_EQ(pet->get<dog>()->at_path<"@.bark">(), 3);
    CHECK_EQ(pet->get<cat>().error(), error::incorrect_type);
}

// schema.hpp, the comment of accessor: a variant member of values gives the index of the held value and the value
// itself; the other
// alternative is incorrect_type.
TEST_CASE("doc reference schema: accessor of a variant member of values")
{
    auto const bytes = cbor::schema<property>::encode(property{"width", std::int64_t{240}});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes,
             "\xd8\x71\x82\x9a\x00\x00\x00\x12\xd8\x72\x82\x64\x6e\x61\x6d\x65\x65\x76\x61\x6c\x75\x65\x5a\x00"
             "\x00\x00\x08\x00\x00\x00\x31\x00\x00\x00\x3b\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
             "\xf7\x7a\x00\x00\x00\x05\x77\x69\x64\x74\x68\x1b\x00\x00\x00\x00\x00\x00\x00\xf0\xd9\x07\x0e\xd8"
             "\x80\x9a\x00\x00\x00\x02\xc6\x1a\x00\x00\x00\x00\xc6\x3a\x00\x00\x00\x00"s);
    auto const width = cbor::schema<property>::path(std::string(*bytes));
    REQUIRE(width.has_value());
    auto const value = width->at_path<"$.value">();
    REQUIRE(value.has_value());
    CHECK_EQ(value->index(), 1u);
    CHECK_EQ(value->get<std::int64_t>(), 240);
    CHECK_EQ(value->get<std::string>().error(), error::incorrect_type);
}

// schema.hpp, snippet fixed_element: an element of a vector of fixed-size structs reads its members with @ and the
// root with $.
TEST_CASE("doc reference schema: fixed_element")
{
    std::string bytes = sample_bytes();
    // [fixed_element]
    // bytes: from schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}})
    auto const root = cbor::schema<car>::path(std::move(bytes));
    auto const second = root->at_path<"$.tires[]">(1uz);
    auto const diameter = second->at_path<"@.diameter">();  // 18
    auto const seats = second->at_path<"$.seats">();        // 5
    // [fixed_element]
    REQUIRE(root.has_value());
    REQUIRE(second.has_value());
    CHECK_EQ(diameter, 18);
    CHECK_EQ(seats, 5);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*second)>, cbor::schema<car>::accessor<cbor::fixed_element<tire>>>);
    CHECK_EQ(second->at_path<"@.diameter">(), 18);
    CHECK_EQ(second->at_path<"@.pressure">(), 2.25f);
    CHECK_EQ(second->at_path<"$.seats">(), 5);
}

// schema.hpp, snippet typed_array_view: the view is a random access range of values; at, first and last give an
// error as a value,
// operator[] outside the view throws.
TEST_CASE("doc reference schema: typed_array_view")
{
    std::string bytes = sample_bytes();
    // [typed_array_view]
    // bytes: from schema<car>::encode(car{..., {1.0, 2.0, 3.0}}), the member track
    auto const root = cbor::schema<car>::path(std::move(bytes));
    auto const track = root->view<"$.track">();
    double const sum = std::accumulate(track->begin(), track->end(), 0.0);  // 6
    auto const past = track->at(3);                                        // error::index_out_of_bounds
    // [typed_array_view]
    REQUIRE(root.has_value());
    REQUIRE(track.has_value());
    CHECK_EQ(std::bit_cast<std::uint64_t>(sum), 0x4018000000000000u);
    CHECK_EQ(past.error(), error::index_out_of_bounds);
    CHECK(std::ranges::random_access_range<cbor::typed_array_view<double> const &>);
    CHECK(std::ranges::sized_range<cbor::typed_array_view<double> const &>);
    CHECK_EQ(track->size(), 3u);
    CHECK_FALSE(track->empty());
    CHECK_EQ(std::bit_cast<std::uint64_t>(std::accumulate(track->begin(), track->end(), 0.0)), 0x4018000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>((*track)[1]), 0x4000000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(track->front()), 0x3ff0000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(track->back()), 0x4008000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*track->rbegin()), 0x4008000000000000u);
    CHECK_EQ(track->at(3).error(), error::index_out_of_bounds);
    CHECK_THROWS_AS(std::ignore = (*track)[3], std::logic_error);
    CHECK_EQ(std::bit_cast<std::uint64_t>(track->last(2)->front()), 0x4000000000000000u);
    CHECK_EQ(track->first(4).error(), error::index_out_of_bounds);
}

// rfc8949.hpp, snippet tag: the class tag is the first item of the root; 1802 is 0x070a and takes the two-byte
// argument d9 07 0a.
TEST_CASE("doc reference schema: tag")
{
    // [tag]
    // point: a struct whose description has the tag 1802
    auto const bytes = cbor::schema<point>::encode(point{1, 2});
    std::size_t const root = bytes->size() - cbor::schema<point>::fixed_size();  // the root record starts with d9 07 0a
    // [tag]
    REQUIRE(bytes.has_value());
    CHECK_EQ(byte_at(*bytes, root), 0xd9);
    CHECK_EQ(byte_at(*bytes, root + 1), 0x07);
    CHECK_EQ(byte_at(*bytes, root + 2), 0x0a);
}

// schema/wire.md: the message starts with tag 113 on an array of two and a table with a four-byte count, the first
// entry
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
    std::size_t const make = bytes.size() - cbor::schema<car>::fixed_size() + cbor::schema<car>::member_offset<&car::make>();
    CHECK_EQ(byte_at(bytes, make), 0xc6);
    CHECK_EQ(byte_at(bytes, make + 1), 0x1a);
    CHECK_EQ(cbor::item_size(bytes), bytes.size());
}

