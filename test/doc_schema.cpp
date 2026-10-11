#include "binding.hpp"

#ifdef __cpp_impl_reflection

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/schema.md. The tests exist so that every example in the document
// compiles and gives the result that the document states.

namespace doc_schema
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
    cbor::schema<T>::template at_path<Path>(s, i...);
};


// The section "Encode and decode" shows the round trip of a struct. The message is one tag 113 of Packed CBOR.
TEST_CASE("doc schema: encode and decode")
{
    auto const bytes = cbor::schema<car>::encode(sample);
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->substr(0, 2), "\xd8\x71"s);
    CHECK_EQ(cbor::item_size(*bytes), bytes->size());
    auto const back = cbor::schema<car>::decode(*bytes);
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->make, "Volvo");
    CHECK_EQ((*back)->tires.size(), 2u);
    CHECK_EQ((*back)->tires[1].diameter, 18u);
    CHECK_FALSE((*back)->note.has_value());
}

// The section "One value with no accessor" shows schema<T>::at_path for a value that holds no view.
TEST_CASE("doc schema: one value with no accessor")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    CHECK_EQ(cbor::schema<car>::at_path<"$.seats">(bytes), 5u);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[1].diameter">(bytes), 18u);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[].pressure">(bytes, 0uz), 2.5f);
    CHECK_EQ(cbor::schema<car>::at_path<"$.track[2]">(bytes), 3.0);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[].diameter">(bytes, 5uz).error(), error::index_out_of_bounds);
}

// The section "One value with no accessor" says that a path to a view, a missing member or a wrong count of
// indexes does not compile.
TEST_CASE("doc schema: a wrong path does not compile")
{
    CHECK(schema_path_compiles<car, "$.seats">);
    CHECK_FALSE(schema_path_compiles<car, "$.make">);
    CHECK_FALSE(schema_path_compiles<car, "$.wheels">);
    CHECK_FALSE(schema_path_compiles<car, "$.tires[].diameter">);
    CHECK(schema_path_compiles<car, "$.tires[].diameter", std::size_t>);
}

// The section "The accessor" shows path, the root accessor, and relative paths that start with @.
TEST_CASE("doc schema: the accessor")
{
    auto const root = cbor::schema<car>::path(*cbor::schema<car>::encode(sample));
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

// The section "The owner of the bytes" says that the accessor holds the owner, so it stays valid after the caller
// releases the bytes, and that an empty owner throws.
TEST_CASE("doc schema: the accessor holds the owner")
{
    auto owner = std::make_shared<std::string const>(*cbor::schema<car>::encode(sample));
    auto const root = cbor::schema<car>::path(owner, *owner);
    owner.reset();
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.make">(), "Volvo"sv);
    std::string const bytes = *cbor::schema<car>::encode(sample);
    CHECK_THROWS_AS((void)cbor::schema<car>::path(std::shared_ptr<void const>{}, bytes), std::logic_error);
}

// The section "Typed arrays" shows view, which gives a typed_array_view of a list of a fixed-width number.
TEST_CASE("doc schema: a typed array view")
{
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
}

// The section "Classes and tags" says that every struct needs its own tag, and that a temporary accessor gives no
// value.
TEST_CASE("doc schema: a struct needs a tag of its own")
{
    CHECK(cbor::tags_registered<car>());
    CHECK_FALSE(cbor::tags_registered<untagged>());
    CHECK_FALSE(cbor::tags_registered<pair_of_tags>());
    using A = cbor::schema<car>::accessor<>;
    CHECK(reads_seats<A const &>);
    CHECK_FALSE(reads_seats<A>);
}

// The section "Errors" gives the error of a cut message and of a message of another class.
TEST_CASE("doc schema: errors")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    CHECK_EQ(cbor::schema<car>::decode(std::string_view(bytes).substr(0, 3)).error(), error::too_little_data);
    std::string const small = *cbor::schema<tire>::encode(tire{1, 1.0f});
    CHECK_EQ(cbor::schema<car>::decode(small).error(), error::too_little_data);
    CHECK_EQ(cbor::schema<tire>::decode(bytes).error(), error::incorrect_type);
    CHECK_EQ(cbor::schema<tire>::path(bytes).error(), error::incorrect_type);
}

} // namespace doc_schema

#endif
