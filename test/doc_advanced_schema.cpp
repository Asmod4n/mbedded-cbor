#include "reference.hpp"


#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/advanced/schema.md. The tests exist so that every example in the
// document compiles and gives the result that the document states.

namespace doc_advanced_schema
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
    std::optional<std::string> note;
};

struct untagged {
    std::uint8_t a;
};

struct twin {
    std::uint8_t a;
};

struct pair_of_tags {
    tire t;
    twin w;
};

struct chosen {
    std::uint8_t number;
    std::uint8_t kept;
};

struct frame {
    std::array<std::uint8_t, 4> code;
    std::vector<std::byte> payload;
};

} // namespace doc_advanced_schema

CBOR_DEFINE_TYPE(doc_advanced_schema::tire, cbor::tag{1700}, CBOR_MEMBER(doc_advanced_schema::tire, diameter),
                 CBOR_MEMBER(doc_advanced_schema::tire, pressure));
CBOR_DEFINE_TYPE(doc_advanced_schema::car, cbor::tag{1701}, CBOR_MEMBER(doc_advanced_schema::car, make),
                 CBOR_MEMBER(doc_advanced_schema::car, seats), CBOR_MEMBER(doc_advanced_schema::car, tires),
                 CBOR_MEMBER(doc_advanced_schema::car, track), CBOR_MEMBER(doc_advanced_schema::car, note));
CBOR_DEFINE_TYPE(doc_advanced_schema::twin, cbor::tag{1700}, CBOR_MEMBER(doc_advanced_schema::twin, a));
CBOR_DEFINE_TYPE(doc_advanced_schema::chosen, cbor::tag{1710}, cbor::member{"id", &doc_advanced_schema::chosen::number},
                 CBOR_MEMBER(doc_advanced_schema::chosen, kept));
CBOR_DEFINE_TYPE(doc_advanced_schema::pair_of_tags, cbor::tag{1702}, CBOR_MEMBER(doc_advanced_schema::pair_of_tags, t),
                 CBOR_MEMBER(doc_advanced_schema::pair_of_tags, w));
CBOR_DEFINE_TYPE(doc_advanced_schema::frame, cbor::tag{1711}, CBOR_MEMBER(doc_advanced_schema::frame, code),
                 CBOR_MEMBER(doc_advanced_schema::frame, payload));

namespace doc_advanced_schema
{

template <class A>
concept reads_seats = requires(A &&a) { std::forward<A>(a).template at_path<"$.seats">(); };

car const sample{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}, std::nullopt};

template <class T, cbor::fixed_string Path, class... Index>
concept schema_path_compiles = requires(std::string_view const s, Index const... i) {
    cbor::schema<T>::template at_path<Path>(s, i...);
};

// The section "The message" states that a message is one tag 113 of Packed CBOR, that a list of fixed-size structs is
// one byte string under tag 32873, that the message holds the description, and that item_size of a message is its
// size.
TEST_CASE("doc advanced schema: the message")
{
    auto const bytes = cbor::schema<car>::encode(sample);
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->substr(0, 2), "\xd8\x71"s);
    CHECK_NE(bytes->find("\xd9\x80\x69"sv), std::string::npos);
    auto const description = cbor::schema<car>::description();
    CHECK_NE(bytes->find(std::string_view(description.data(), description.size())), std::string::npos);
    CHECK_EQ(cbor::item_size(*bytes), bytes->size());
}

// The section "The message" states that a float field holds the bits of the value, so a NaN keeps its bits.
TEST_CASE("doc advanced schema: a NaN keeps its bits")
{
    float const nan = std::bit_cast<float>(std::uint32_t{0x7fc00123});
    auto const bytes = cbor::schema<car>::encode(car{"", 0, {{1, nan}}, {}, std::nullopt});
    REQUIRE(bytes.has_value());
    auto const read = cbor::schema<car>::at_path<"$.tires[0].pressure">(*bytes);
    REQUIRE(read.has_value());
    CHECK_EQ(std::bit_cast<std::uint32_t>(*read), 0x7fc00123u);
}

// The section "Structs and tags" says that every struct needs its own tag.
TEST_CASE("doc advanced schema: a struct needs a tag of its own")
{
    CHECK(cbor::tags_registered<car>());
    CHECK_EQ(cbor::describe_members<tire>::tag->number, 1700u);
    CHECK_FALSE(cbor::tags_registered<untagged>());
    CHECK_FALSE(cbor::tags_registered<pair_of_tags>());
}

// The section "Structs and tags" shows that the description gives the key of each member.
TEST_CASE("doc advanced schema: the description gives the keys")
{
    auto const a = cbor::schema<chosen>::encode(chosen{7, 8});
    REQUIRE(a.has_value());
    CHECK_NE(a->find("\x62" "id"sv), std::string::npos);
    CHECK_NE(a->find("\x64" "kept"sv), std::string::npos);
    CHECK_EQ(a->find("number"sv), std::string::npos);
    auto const back = cbor::schema<chosen>::decode(std::string(*a));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->number, 7u);
    CHECK_EQ((*back)->kept, 8u);
    CHECK_EQ(cbor::schema<chosen>::at_path<"$.id">(*a), 7u);
}

// The section "Encode and decode" shows the round trip of a struct and the encode that appends to a target.
TEST_CASE("doc advanced schema: encode and decode")
{
    auto const bytes = cbor::schema<car>::encode(sample);
    REQUIRE(bytes.has_value());
    auto const back = cbor::schema<car>::decode(std::string(*bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->make, "Volvo");
    CHECK_EQ((*back)->tires.size(), 2u);
    CHECK_EQ((*back)->tires[1].diameter, 18u);
    CHECK_FALSE((*back)->note.has_value());
    std::string out = "\x00"s;
    auto const appended = cbor::schema<car>::encode(sample, out);
    REQUIRE(appended.has_value());
    CHECK_EQ(*appended, bytes->size());
    CHECK_EQ(out.substr(1), *bytes);

    auto const owner = std::make_shared<std::string const>(*bytes);
    std::string_view const part(*owner);
    auto const beside = cbor::schema<car>::decode(owner, part);
    REQUIRE(beside.has_value());
    CHECK_EQ((*beside)->seats, 5u);
    car target{};
    auto const into = cbor::schema<car>::decode(owner, part, std::move(target));
    REQUIRE(into.has_value());
    CHECK_EQ((*into)->make, "Volvo");
    auto const shared = cbor::schema<car>::decode(owner);
    REQUIRE(shared.has_value());
    CHECK_EQ((*shared)->track.size(), 3u);
}

// The section "Encode and decode" says that tire has padding, and shows the message without the description.
TEST_CASE("doc advanced schema: the description apart")
{
    CHECK_GT(sizeof(tire), sizeof(std::uint16_t) + sizeof(float));
    std::string const bytes = *cbor::schema<car>::encode(sample);
    constexpr auto apart = cbor::description_place::apart;
    auto const data = cbor::schema<car>::encode<apart>(sample);
    REQUIRE(data.has_value());
    auto const back = cbor::schema<car>::decode<apart>(std::string(*data));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->make, "Volvo");
    CHECK_EQ((*back)->tires[1].pressure, 2.25f);
    CHECK_EQ(data->size() + cbor::schema<car>::description().size(), bytes.size());
}

// The section "One value with no accessor" shows schema<T>::at_path for a value that holds no view.
TEST_CASE("doc advanced schema: one value with no accessor")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    CHECK_EQ(cbor::schema<car>::at_path<"$.seats">(bytes), 5u);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[1].diameter">(bytes), 18u);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[].pressure">(bytes, 0uz), 2.5f);
    CHECK_EQ(cbor::schema<car>::at_path<"$.track[2]">(bytes), 3.0);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[].diameter">(bytes, 5uz).error(), error::index_out_of_bounds);
}

// The section "One value with no accessor" says that [n] on a byte string reads one byte.
TEST_CASE("doc advanced schema: one byte of a byte string")
{
    auto const packet = cbor::schema<frame>::encode(frame{{1, 2, 3, 4}, {std::byte{9}, std::byte{8}}});
    REQUIRE(packet.has_value());
    CHECK_EQ(cbor::schema<frame>::at_path<"$.code[2]">(*packet), std::uint8_t{3});
    CHECK_EQ(cbor::schema<frame>::at_path<"$.payload[1]">(*packet), std::byte{8});
    CHECK_EQ(cbor::schema<frame>::at_path<"$.payload[]">(*packet, 2uz).error(), error::index_out_of_bounds);
}

// The section "One value with no accessor" says that a path to a view, to a member that is not trivially copyable,
// to a missing member or with a wrong count of indexes does not compile.
TEST_CASE("doc advanced schema: a wrong path does not compile")
{
    CHECK(schema_path_compiles<car, "$.seats">);
    CHECK_FALSE(schema_path_compiles<car, "$.make">);
    CHECK_FALSE(schema_path_compiles<car, "$.tires">);
    CHECK_FALSE(schema_path_compiles<car, "$.wheels">);
    CHECK_FALSE(schema_path_compiles<car, "$.tires[].diameter">);
    CHECK(schema_path_compiles<car, "$.tires[].diameter", std::size_t>);
    CHECK(schema_path_compiles<frame, "$.code[3]">);
    CHECK_FALSE(schema_path_compiles<frame, "$.code[4]">);
}

// The section "The accessor" shows path, the root accessor, and relative paths that start with @.
TEST_CASE("doc advanced schema: the accessor")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    auto const root = cbor::schema<car>::path(std::string(bytes));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.make">(), "Volvo"sv);
    auto const tires = root->at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 2u);
    auto const second = tires->at_path<"@[]">(1uz);
    REQUIRE(second.has_value());
    CHECK_EQ(second->at_path<"@.diameter">(), 18u);
    CHECK_EQ(second->at_path<"$.seats">(), 5u);
    auto const note = root->at_path<"$.note">();
    REQUIRE(note.has_value());
    CHECK_FALSE(note->has_value());
    CHECK((std::ranges::random_access_range<decltype(*tires)>));
    std::vector<float> seen;
    for (auto const t : *tires | std::views::drop(1))
        seen.push_back(*t.at_path<"@.pressure">());
    CHECK_EQ(seen, std::vector<float>{2.25f});
}

// The section "The accessor" says that the accessor holds the owner, so it stays valid after the caller releases the
// bytes, that a caller who wants a copy makes it with std::string, that an empty owner throws and that a temporary
// accessor reads nothing.
TEST_CASE("doc advanced schema: the accessor holds the owner")
{
    auto owner = std::make_shared<std::string const>(*cbor::schema<car>::encode(sample));
    auto const root = cbor::schema<car>::path(owner);
    owner.reset();
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.make">(), "Volvo"sv);
    std::string temp = *cbor::schema<car>::encode(sample);
    auto const copied = cbor::schema<car>::path(std::string(temp));
    temp.assign(temp.size(), '\0');
    REQUIRE(copied.has_value());
    CHECK_EQ(*copied->at_path<"$.make">(), "Volvo"sv);
    CHECK_THROWS_AS((void)cbor::schema<car>::path(std::shared_ptr<std::string const>{}), std::logic_error);
    auto const held = std::make_shared<std::string const>(*cbor::schema<car>::encode(sample));
    auto const beside = cbor::schema<car>::path(held, std::string_view(*held));
    REQUIRE(beside.has_value());
    CHECK_EQ(beside->at_path<"$.seats">(), 5u);
    auto moved = cbor::schema<car>::path(std::string(*held));
    REQUIRE(moved.has_value());
    auto const kept = std::move(*moved);
    CHECK_EQ(kept.at_path<"$.seats">(), 5u);
    CHECK_THROWS_AS((void)moved->at_path<"$.seats">(), std::logic_error);
    using A = cbor::schema<car>::accessor<>;
    CHECK(reads_seats<A const &>);
    CHECK_FALSE(reads_seats<A>);
}

// A list accessor taken from a temporary root accessor read freed bytes, because only the root held the owner of the
// bytes. The section "The accessor" states that every accessor holds the owner.
TEST_CASE("doc advanced schema: a list accessor keeps the bytes alive")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    auto const tires = cbor::schema<car>::path(std::string(bytes))->at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 2u);
    CHECK_EQ(*tires->at_path<"@[1].diameter">(), 18u);
}

// The section "Typed arrays" shows view, which gives a typed_array_view of a list of a fixed-width number.
TEST_CASE("doc advanced schema: a typed array view")
{
    CHECK((std::ranges::random_access_range<cbor::typed_array_view<double>>));
    CHECK((std::ranges::sized_range<cbor::typed_array_view<double>>));
    auto const root = cbor::schema<car>::path(*cbor::schema<car>::encode(sample));
    REQUIRE(root.has_value());
    auto const track = root->view<"$.track">();
    REQUIRE(track.has_value());
    CHECK_EQ(track->size(), 3u);
    CHECK_EQ(std::accumulate(track->begin(), track->end(), 0.0), 6.0);
    CHECK_EQ((*track)[1], 2.0);
    CHECK_EQ(track->at(3).error(), error::index_out_of_bounds);
    CHECK_THROWS_AS((*track)[3], std::logic_error);
    auto const tail = track->last(2);
    REQUIRE(tail.has_value());
    CHECK_EQ(tail->front(), 2.0);
    CHECK_EQ(track->first(4).error(), error::index_out_of_bounds);
    CHECK_EQ(std::ranges::fold_left(*track, 0.0, std::plus<>{}), 6.0);
}

// The section "Errors" gives the error of a cut message, of a message of another struct and of a forged head.
TEST_CASE("doc advanced schema: errors")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    CHECK_EQ(cbor::schema<car>::decode(bytes.substr(0, 3)).error(), error::too_little_data);
    std::string_view const view = bytes;
    CHECK_EQ(cbor::schema<car>::at_path<"$.seats">(view.substr(0, view.size() - 1)).error(), error::too_little_data);
    CHECK_EQ(cbor::schema<car>::path(bytes.substr(0, bytes.size() / 2)).error(), error::too_little_data);
    std::string const small = *cbor::schema<tire>::encode(tire{1, 1.0f});
    CHECK_EQ(cbor::schema<car>::decode(std::string(small)).error(), error::too_little_data);
    CHECK_EQ(cbor::schema<tire>::decode(std::string(bytes)).error(), error::incorrect_type);
    CHECK_EQ(cbor::schema<tire>::path(std::string(bytes)).error(), error::incorrect_type);
    std::size_t const seats = bytes.find("\x18\x05"sv);
    REQUIRE_NE(seats, std::string::npos);
    std::string forged = bytes;
    forged[seats] = '\x58';
    CHECK_EQ(cbor::schema<car>::at_path<"$.seats">(forged).error(), error::incorrect_type);
}

} // namespace doc_advanced_schema

