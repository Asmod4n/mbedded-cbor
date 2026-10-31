#include "reference.hpp"


#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/schema.md. The tests exist so that every example in the document
// compiles and gives the result that the document states.

namespace doc_packed
{

struct sensor {
    std::string name;
    std::uint16_t id;
    std::vector<double> readings;
};

struct untagged {
    std::uint8_t a;
};

} // namespace doc_packed

CBOR_DEFINE_TYPE_MEMBERS(doc_packed::sensor, cbor::tag{1700}, name, id, readings);

namespace doc_packed
{

template <cbor::fixed_string Path, class... Index>
concept reads = requires(std::string_view const s, Index const... i) {
    cbor::schema<sensor>::template at_path<Path>(s, i...);
};

template <class A>
concept reads_name = requires(A &&a) { std::forward<A>(a).template at_path<"$.name">(); };

// The section "Write a message and read one field" shows the first program whole, with the check of the encode,
// because a new user meets the std::expected of encode first.
std::expected<std::pair<std::uint16_t, double>, cbor::error> first_message()
{
    auto const bytes = cbor::schema<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    if (!bytes)
        return std::unexpected(bytes.error());
    auto const id = cbor::schema<sensor>::at_path<"$.id">(*bytes);
    auto const second = cbor::schema<sensor>::at_path<"$.readings[1]">(*bytes);
    return std::pair{*id, *second};
}

TEST_CASE("doc schema: write a message and read one field")
{
    CHECK(first_message() == std::pair<std::uint16_t, double>{7, 2.5});
}

// The section "Tags and keys" states that a struct with no description does not compile in schema.
TEST_CASE("doc schema: tags and keys")
{
    CHECK(cbor::tags_registered<sensor>());
    CHECK_FALSE(cbor::tags_registered<untagged>());
}

// The section "Read one value" shows an index from an argument, and the paths that do not compile.
TEST_CASE("doc schema: read one value")
{
    std::string const bytes = *cbor::schema<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    std::size_t const i = 1;
    CHECK_EQ(cbor::schema<sensor>::at_path<"$.readings[]">(bytes, i), 2.5);
    CHECK_EQ(cbor::schema<sensor>::at_path<"$.readings[]">(bytes, 2uz).error(), error::index_out_of_bounds);
    CHECK(reads<"$.id">);
    CHECK_FALSE(reads<"$.place">);
    CHECK_FALSE(reads<"$.name">);
    CHECK(reads<"$.readings[]", std::size_t>);
}

// The section "Read a text, a list or a struct" opens the message with path and reads a text and a list of numbers.
TEST_CASE("doc schema: read a text, a list or a struct")
{
    std::string const bytes = *cbor::schema<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    std::vector<double> used;
    auto const use = [&used](double const r) { used.push_back(r); };
    auto const owner = std::make_shared<std::string const>(bytes);
    auto const root = cbor::schema<sensor>::path(owner);
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.name">(), "t1"sv);
    auto const readings = root->view<"$.readings">();
    if (readings)
        for (double const r : *readings)
            use(r);
    CHECK(used == std::vector{1.5, 2.5});
    CHECK_EQ(readings->size(), 2u);
    CHECK_EQ(readings->at(1), 2.5);
    CHECK_EQ(readings->at(2).error(), error::index_out_of_bounds);
    using accessor = cbor::schema<sensor>::accessor<>;
    CHECK(reads_name<accessor const &>);
    CHECK_FALSE(reads_name<accessor>);
}

// The section "Read the whole struct" decodes the message into a sensor.
TEST_CASE("doc schema: read the whole struct")
{
    std::string const bytes = *cbor::schema<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    std::size_t used = 0;
    auto const use = [&used](std::size_t const n) { used = n; };
    auto const owner = std::make_shared<std::string const>(bytes);
    auto const back = cbor::schema<sensor>::decode(owner);
    if (back)
        use((*back)->readings.size());
    CHECK_EQ(used, 2u);
    CHECK_EQ((*back)->name, "t1");
}

// The section "Read from a peer that you trust" opens a message with unverified_path, which reads as path does.
TEST_CASE("doc schema: read from a peer that you trust")
{
    auto const owner = std::make_shared<std::string const>(*cbor::schema<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}}));
    auto const fast = cbor::schema<sensor>::unverified_path(owner);
    REQUIRE(fast.has_value());
    CHECK_EQ(fast->at_path<"$.id">(), 7);
    CHECK_EQ(fast->at_path<"$.readings[1]">(), 2.5);
}

struct other {
    std::uint8_t a;
};

} // namespace doc_packed

template <>
struct cbor::describe_members<doc_packed::other> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1701};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_packed::other, a),
    };
};

namespace doc_packed
{

// The section "Store the description apart" reads the data form with apart, and the data form and the description
// together have the size of the message.
TEST_CASE("doc schema: store the description apart")
{
    using namespace doc_packed;
    constexpr auto apart = cbor::description_place::apart;
    auto const data = cbor::schema<sensor>::encode<apart>(sensor{"t1", 7, {1.5, 2.5}});
    REQUIRE(data.has_value());
    CHECK_EQ((cbor::schema<sensor>::at_path<"$.id", apart>(*data)), 7);
    auto const whole = cbor::schema<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    REQUIRE(whole.has_value());
    CHECK_EQ(data->size() + cbor::schema<sensor>::description().size(), whole->size());
}

// The section "Errors" gives one input for each row, and states that a field with a wrong head gives an error.
TEST_CASE("doc schema: errors")
{
    std::string const bytes = *cbor::schema<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    CHECK_EQ(cbor::schema<sensor>::decode(bytes.substr(0, 3)).error(), error::too_little_data);
    CHECK_EQ(cbor::schema<other>::decode(std::string(bytes)).error(), error::incorrect_type);
    CHECK_EQ(cbor::schema<sensor>::at_path<"$.readings[]">(bytes, 9uz).error(), error::index_out_of_bounds);
    std::size_t const head = bytes.find("\x19\x00\x07"sv);
    REQUIRE_NE(head, std::string::npos);
    std::string forged = bytes;
    forged[head] = '\x59';
    CHECK_FALSE(cbor::schema<sensor>::at_path<"$.id">(forged).has_value());
    CHECK_FALSE(cbor::schema<sensor>::decode(std::string(forged)).has_value());
}

} // namespace doc_packed

namespace doc_packed
{

struct point {
    float x;
    float y;
};

struct track {
    std::vector<point> points;
};

} // namespace doc_packed

CBOR_DEFINE_TYPE_MEMBERS(doc_packed::point, cbor::tag{1703}, x, y);
CBOR_DEFINE_TYPE_MEMBERS(doc_packed::track, cbor::tag{1704}, points);

TEST_CASE("doc schema: a list of fixed-size structs is a range of elements")
{
    using namespace doc_packed;
    auto message = cbor::schema<track>::encode(track{{{1, 2}, {3, 4}, {5, 6}}});
    REQUIRE(message.has_value());
    auto const lane = cbor::schema<track>::path(std::move(*message));
    REQUIRE(lane.has_value());
    auto const points = lane->at_path<"$.points">();
    REQUIRE(points.has_value());
    std::vector<float> seen;
    for (auto const p : *points | std::views::drop(1) | std::views::take(1))
        seen.push_back(*p.at_path<"@.y">());
    CHECK_EQ(seen, (std::vector<float>{4}));
    CHECK_EQ(lane->at_path<"$.points[2].x">(), 5.0f);
}

namespace doc_packed
{

// begin define-type
struct gauge {
    std::uint16_t level;
};
// end define-type

} // namespace doc_packed

CBOR_DEFINE_TYPE_MEMBERS(doc_packed::gauge, cbor::tag{1710}, level);

// The section "Tags and keys" writes a description with CBOR_DEFINE_TYPE, which schema reads as the explicit form.
TEST_CASE("doc schema: a description through CBOR_DEFINE_TYPE")
{
    auto const bytes = cbor::schema<doc_packed::gauge>::encode(doc_packed::gauge{3});
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::schema<doc_packed::gauge>::at_path<"$.level">(*bytes), 3);
}
