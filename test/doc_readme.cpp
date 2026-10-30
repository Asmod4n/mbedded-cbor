#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using namespace std::string_literals;

// Each test in this file is one example of README.md. The tests exist so that every example in the README
// compiles and gives the result that the README states.

// "Read a message" moves the bytes into the lazy and loops over the elements of an array.
TEST_CASE("doc readme: read a message")
{
    std::string bytes = test::preferred(test::M("sensor"s, "t1"s, "value"s, 21.5, "samples"s, test::A(3, 4, 5)));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes)).value();

    int sum = 0;
    if (auto const samples = doc.at("samples")) {
        auto const elements = samples->elements().value();
        for (auto const &x : elements)
            if (x)
                sum += x->get<int>().value_or(0);
    }
    CHECK_EQ(sum, 12);
}

// "Read one value by its path" reads the bytes in place through the shared_ptr; a missing member gives
// key_not_found.
TEST_CASE("doc readme: read one value by its path")
{
    auto const owner =
        std::make_shared<std::string const>(test::preferred(test::M("user"s, test::M("id"s, 42, "name"s, "ann"s))));
    auto const doc = cbor::lazy::view(owner).value();

    auto const id = doc.at_path<"$.user.id", std::int64_t>();
    auto const name = doc.at_path<"$.user.name", std::string_view>();
    auto const age = doc.at_path<"$.user.age", int>();
    CHECK_EQ(*id, 42);
    CHECK_EQ(**name, "ann");
    CHECK_EQ(age.error(), cbor::error::key_not_found);
}


namespace
{

struct point {
    float x;
    float y;
};

struct track {
    std::vector<point> points;
};

} // namespace

CBOR_DEFINE_TYPE(point, cbor::tag{1703}, CBOR_MEMBER(point, x), CBOR_MEMBER(point, y));
CBOR_DEFINE_TYPE(track, cbor::tag{1704}, CBOR_MEMBER(track, points));

// "Write structs, read one field" reads one leaf at its offset and a page of the fixed-size vector.
TEST_CASE("doc readme: write structs, read one field")
{
    auto message = cbor::schema<track>::encode(track{{{1, 2}, {3, 4}, {5, 6}}});
    REQUIRE(message.has_value());
    auto const lane = cbor::schema<track>::path(std::move(*message));
    REQUIRE(lane.has_value());
    CHECK_EQ(lane->at_path<"$.points[2].x">(), 5.0f);
    auto const points = lane->at_path<"$.points">();
    REQUIRE(points.has_value());
    std::vector<float> seen;
    for (auto const p : *points | std::views::drop(1) | std::views::take(1))
        seen.push_back(*p.at_path<"@.y">());
    CHECK_EQ(seen, (std::vector<float>{4}));
}

