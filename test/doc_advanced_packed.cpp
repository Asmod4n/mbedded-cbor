#include "reference.hpp"

#ifdef __cpp_impl_reflection

#include <algorithm>
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

// Each test in this file is one example of doc/advanced/packed.md. The tests exist so that every example in the
// document compiles and gives the result that the document states.

namespace doc_advanced_packed
{

struct [[=cbor::tag(1700)]] tire {
    std::uint16_t diameter;
    float pressure;
};

struct [[=cbor::tag(1701)]] car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
    std::optional<std::string> note;
};

struct untagged {
    std::uint8_t a;
};

struct [[=cbor::tag(1700)]] twin {
    std::uint8_t a;
};

struct [[=cbor::tag(1702)]] pair_of_tags {
    tire t;
    twin w;
};

template <class A>
concept reads_seats = requires(A &&a) { std::forward<A>(a).template at_path<"$.seats">(); };

car const sample{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}, std::nullopt};

template <class T, cbor::fixed_string Path, class... Index>
concept schema_path_compiles = requires(std::string_view const s, Index const... i) {
    cbor::packed<T>::template at_path<Path>(s, i...);
};

// The section "The message" states that a message is one tag 113 of Packed CBOR, and that item_size of a message is
// its size.
TEST_CASE("doc advanced schema: the message")
{
    auto const bytes = cbor::packed<car>::encode(sample);
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->substr(0, 2), "\xd8\x71"s);
    CHECK_EQ(cbor::item_size(*bytes), bytes->size());
}

// The section "Structs and tags" says that every struct needs its own tag.
TEST_CASE("doc advanced schema: a struct needs a tag of its own")
{
    CHECK(cbor::tags_registered<car>());
    CHECK_FALSE(cbor::tags_registered<untagged>());
    CHECK_FALSE(cbor::tags_registered<pair_of_tags>());
}

// The section "Encode and decode" shows the round trip of a struct and the encode that appends to a target.
TEST_CASE("doc advanced schema: encode and decode")
{
    auto const bytes = cbor::packed<car>::encode(sample);
    REQUIRE(bytes.has_value());
    auto const back = cbor::packed<car>::decode(*bytes);
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->make, "Volvo");
    CHECK_EQ((*back)->tires.size(), 2u);
    CHECK_EQ((*back)->tires[1].diameter, 18u);
    CHECK_FALSE((*back)->note.has_value());
    std::string out = "\x00"s;
    auto const appended = cbor::packed<car>::encode(sample, out);
    REQUIRE(appended.has_value());
    CHECK_EQ(*appended, bytes->size());
    CHECK_EQ(out.substr(1), *bytes);
}

// The section "One value with no accessor" shows packed<T>::at_path for a value that holds no view.
TEST_CASE("doc advanced schema: one value with no accessor")
{
    std::string const bytes = *cbor::packed<car>::encode(sample);
    CHECK_EQ(cbor::packed<car>::at_path<"$.seats">(bytes), 5u);
    CHECK_EQ(cbor::packed<car>::at_path<"$.tires[1].diameter">(bytes), 18u);
    CHECK_EQ(cbor::packed<car>::at_path<"$.tires[].pressure">(bytes, 0uz), 2.5f);
    CHECK_EQ(cbor::packed<car>::at_path<"$.track[2]">(bytes), 3.0);
    CHECK_EQ(cbor::packed<car>::at_path<"$.tires[].diameter">(bytes, 5uz).error(), error::index_out_of_bounds);
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
}

// The section "The accessor" shows path, the root accessor, and relative paths that start with @.
TEST_CASE("doc advanced schema: the accessor")
{
    std::string const bytes = *cbor::packed<car>::encode(sample);
    auto const root = cbor::packed<car>::path(bytes);
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
}

// The section "The accessor" says that the accessor holds the owner, so it stays valid after the caller releases the
// bytes, that path(std::string_view) copies, that an empty owner throws and that a temporary accessor reads nothing.
TEST_CASE("doc advanced schema: the accessor holds the owner")
{
    auto owner = std::make_shared<std::string const>(*cbor::packed<car>::encode(sample));
    auto const root = cbor::packed<car>::path(owner, *owner);
    owner.reset();
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.make">(), "Volvo"sv);
    std::string temp = *cbor::packed<car>::encode(sample);
    auto const copied = cbor::packed<car>::path(std::string_view(temp));
    temp.assign(temp.size(), '\0');
    REQUIRE(copied.has_value());
    CHECK_EQ(*copied->at_path<"$.make">(), "Volvo"sv);
    std::string const bytes = *cbor::packed<car>::encode(sample);
    CHECK_THROWS_AS((void)cbor::packed<car>::path(std::shared_ptr<void const>{}, bytes), std::logic_error);
    using A = cbor::packed<car>::accessor<>;
    CHECK(reads_seats<A const &>);
    CHECK_FALSE(reads_seats<A>);
}

// A list accessor taken from a temporary root accessor read freed bytes, because only the root held the owner of the
// bytes. The section "The accessor" states that every accessor holds the owner.
TEST_CASE("doc advanced schema: a list accessor keeps the bytes alive")
{
    std::string const bytes = *cbor::packed<car>::encode(sample);
    auto const tires = cbor::packed<car>::path(bytes)->at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 2u);
    CHECK_EQ(*tires->at_path<"@[1].diameter">(), 18u);
}

// The section "Typed arrays" shows view, which gives a typed_array_view of a list of a fixed-width number.
TEST_CASE("doc advanced schema: a typed array view")
{
    static_assert(std::ranges::random_access_range<cbor::typed_array_view<double>>);
    static_assert(std::ranges::sized_range<cbor::typed_array_view<double>>);
    auto const root = cbor::packed<car>::path(*cbor::packed<car>::encode(sample));
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
    std::string const bytes = *cbor::packed<car>::encode(sample);
    CHECK_EQ(cbor::packed<car>::decode(std::string_view(bytes).substr(0, 3)).error(), error::too_little_data);
    std::string const small = *cbor::packed<tire>::encode(tire{1, 1.0f});
    CHECK_EQ(cbor::packed<car>::decode(small).error(), error::too_little_data);
    CHECK_EQ(cbor::packed<tire>::decode(bytes).error(), error::incorrect_type);
    CHECK_EQ(cbor::packed<tire>::path(bytes).error(), error::incorrect_type);
    std::size_t const seats = bytes.find("\x18\x05"sv);
    REQUIRE_NE(seats, std::string::npos);
    std::string forged = bytes;
    forged[seats] = '\x58';
    CHECK_EQ(cbor::packed<car>::at_path<"$.seats">(forged).error(), error::incorrect_type);
}

} // namespace doc_advanced_packed

#endif
