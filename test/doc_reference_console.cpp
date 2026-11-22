#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <string>
#include <string_view>
#include <utility>

using namespace std::string_literals;
using namespace std::string_view_literals;

// Each test in this file is one example of the console page of the docs site. The tests exist so that every example
// compiles in C++ and gives the result that the page states. The site checks the same examples in the console.

namespace {

std::string user_message()
{
    return test::encode_in_preferred_form(test::M("user"s, test::M("id"s, 42, "name"s, "ann"s)));
}

}

// Read a field by its key: a text comes back as a ref into the message.
TEST_CASE("doc reference console: a field by its key")
{
    auto const doc = cbor::lazy::adopt(user_message());
    auto const user = doc->at("user");
    auto const name_2 = user->at("name");
    auto const name = name_2->get<std::string_view>();
    REQUIRE(name);
    CHECK_EQ(**name, "ann"sv);
}

// Read an element by its index: the index counts from 0.
TEST_CASE("doc reference console: an element by its index")
{
    auto const doc = cbor::lazy::adopt(test::encode_in_preferred_form(test::M("tags"s, test::A("a"s, "b"s, "c"s))));
    auto const tags = doc->at("tags");
    auto const at_1 = tags->at(1);
    auto const tag = at_1->get<std::string_view>();
    REQUIRE(tag);
    CHECK_EQ(**tag, "b"sv);
}

// Read one value by its path.
TEST_CASE("doc reference console: one value by its path")
{
    auto const doc = cbor::lazy::adopt(user_message());
    auto const read = doc->at_path<"$.user.id">()->get<int>();
    CHECK_EQ(read.value(), 42);
}

// Read all values of a path: [*] with cbor::nodelist gives every match.
TEST_CASE("doc reference console: all values of a path")
{
    auto const doc = cbor::lazy::adopt(
        test::encode_in_preferred_form(test::M("users"s, test::A(test::M("name"s, "ann"s), test::M("name"s, "bob"s)))));
    CHECK_EQ(doc->at_path<"$.users[*].name", cbor::nodelist>()->size(), 2u);
}

// Show the message as text: the diagnostic notation of RFC 8949 section 8.
TEST_CASE("doc reference console: the message as text")
{
    CHECK_EQ(cbor::diagnostic_notation(user_message()), "{\"user\": {\"id\": 42, \"name\": \"ann\"}}"s);
}

// Read a value with the wrong type: the error is a value, not an exception.
TEST_CASE("doc reference console: a value with the wrong type")
{
    auto const doc = cbor::lazy::adopt(user_message());
    auto const user = doc->at("user");
    auto const name = user->at("name");
    auto const read = name->get<int>();
    CHECK_EQ(read.error(), cbor::error::incorrect_type);
}
