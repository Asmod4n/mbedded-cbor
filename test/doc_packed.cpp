#include "reference.hpp"

#ifdef __cpp_impl_reflection

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/packed.md. The tests exist so that every example in the document
// compiles and gives the result that the document states.

namespace doc_packed
{

struct [[=cbor::tag(1700)]] sensor {
    std::string name;
    std::uint16_t id;
    std::vector<double> readings;
};

struct untagged {
    std::uint8_t a;
};

template <cbor::fixed_string Path, class... Index>
concept reads = requires(std::string_view const s, Index const... i) {
    cbor::packed<sensor>::template at_path<Path>(s, i...);
};

template <class A>
concept reads_name = requires(A &&a) { std::forward<A>(a).template at_path<"$.name">(); };

// The section "Write a message and read one field" shows the first program whole, with the check of the encode,
// because a new user meets the std::expected of encode first.
std::expected<std::pair<std::uint16_t, double>, cbor::error> first_message()
{
    auto const bytes = cbor::packed<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    if (!bytes)
        return std::unexpected(bytes.error());
    auto const id = cbor::packed<sensor>::at_path<"$.id">(*bytes);
    auto const second = cbor::packed<sensor>::at_path<"$.readings[1]">(*bytes);
    return std::pair{*id, *second};
}

TEST_CASE("doc schema: write a message and read one field")
{
    CHECK(first_message() == std::pair<std::uint16_t, double>{7, 2.5});
}

// The section "Tags" states that a struct with no tag leaves no schema to call.
TEST_CASE("doc schema: tags")
{
    CHECK(cbor::tags_registered<sensor>());
    CHECK_FALSE(cbor::tags_registered<untagged>());
}

// The section "Read one value" shows an index from an argument, and the paths that do not compile.
TEST_CASE("doc schema: read one value")
{
    std::string const bytes = *cbor::packed<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    std::size_t const i = 1;
    CHECK_EQ(cbor::packed<sensor>::at_path<"$.readings[]">(bytes, i), 2.5);
    CHECK_EQ(cbor::packed<sensor>::at_path<"$.readings[]">(bytes, 2uz).error(), error::index_out_of_bounds);
    CHECK(reads<"$.id">);
    CHECK_FALSE(reads<"$.place">);
    CHECK_FALSE(reads<"$.name">);
    CHECK(reads<"$.readings[]", std::size_t>);
}

// The section "Read a text, a list or a struct" opens the message with path and reads a text and a list of numbers.
TEST_CASE("doc schema: read a text, a list or a struct")
{
    std::string const bytes = *cbor::packed<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    std::vector<double> used;
    auto const use = [&used](double const r) { used.push_back(r); };
    auto const root = cbor::packed<sensor>::path(bytes);
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
    using accessor = cbor::packed<sensor>::accessor<>;
    CHECK(reads_name<accessor const &>);
    CHECK_FALSE(reads_name<accessor>);
}

// The section "Read the whole struct" decodes the message into a sensor.
TEST_CASE("doc schema: read the whole struct")
{
    std::string const bytes = *cbor::packed<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    std::size_t used = 0;
    auto const use = [&used](std::size_t const n) { used = n; };
    auto const back = cbor::packed<sensor>::decode(bytes);
    if (back)
        use((*back)->readings.size());
    CHECK_EQ(used, 2u);
    CHECK_EQ((*back)->name, "t1");
}

struct [[=cbor::tag(1701)]] other {
    std::uint8_t a;
};

// The section "Errors" gives one input for each row, and states that a field with a wrong head gives an error.
TEST_CASE("doc schema: errors")
{
    std::string const bytes = *cbor::packed<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
    CHECK_EQ(cbor::packed<sensor>::decode(std::string_view(bytes).substr(0, 3)).error(), error::too_little_data);
    CHECK_EQ(cbor::packed<other>::decode(bytes).error(), error::incorrect_type);
    CHECK_EQ(cbor::packed<sensor>::at_path<"$.readings[]">(bytes, 9uz).error(), error::index_out_of_bounds);
    std::size_t const head = bytes.find("\x19\x00\x07"sv);
    REQUIRE_NE(head, std::string::npos);
    std::string forged = bytes;
    forged[head] = '\x59';
    CHECK_FALSE(cbor::packed<sensor>::at_path<"$.id">(forged).has_value());
    CHECK_FALSE(cbor::packed<sensor>::decode(forged).has_value());
}

} // namespace doc_packed

#endif
