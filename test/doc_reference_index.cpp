#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <string>
#include <string_view>
#include <utility>

using namespace std::string_literals;
using namespace std::string_view_literals;

// The example of doc/reference/index.md, the start page of the site. The test exists so that the example
// compiles and gives the results that its comments state.
TEST_CASE("doc reference index: the example")
{
    std::string bytes = test::preferred(test::M("user"s, test::M("id"s, 42, "name"s, "ann"s)));
    auto const id = cbor::at_path<"$.user.id", int>(bytes);
    CHECK_EQ(id.value(), 42);
    auto const age = cbor::at_path<"$.user.age", int>(bytes);
    CHECK_EQ(age.error(), cbor::error::key_not_found);

    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    auto const name = doc->at_path<"$.user.name", std::string_view>();
    REQUIRE(name);
    CHECK_EQ(**name, "ann"sv);
}
