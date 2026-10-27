#include "../owned_copy.hpp"
#include "../reference.hpp"
#include "../sequence_reader.hpp"

#include <cstddef>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// This binary is built with -DCBOR_NESTING_DEPTH_DEFAULT=2, as a user sets the nesting depth. The examples of
// the documents that show the nesting depth at 2 run here, so each one is held by the limit as the user is.

TEST_CASE("limits depth 2: the build sets the nesting depth and no size limit")
{
    CHECK_EQ(cbor::limits::nesting_depth, 2u);
    CHECK_EQ(cbor::limits::string_length, SIZE_MAX);
    CHECK_EQ(cbor::limits::container_elements, SIZE_MAX);
}

// doc/basics.md, section "Limits": the table names the error of the nesting depth.
TEST_CASE("doc basics: the error of the nesting depth")
{
    std::string const deep = preferred(A(A(A(1))));
    CHECK_EQ(cbor::at_path<"$[0][0]", int>(deep).error(), error::incorrect_type);
    CHECK_EQ(test::lazy_of_copy(deep)->clone().error(), error::nesting_depth_exceeded);
}

// doc/advanced/basics.md, section "Nesting depth": the top-level item has the depth 0, and item_size keeps no
// stack.
TEST_CASE("doc advanced basics: nesting depth")
{
    std::string const two = preferred(A(A(1)));
    std::string const three = preferred(A(A(A(1))));
    CHECK_EQ(cbor::diagnostic_notation(two), "[[1]]");
    CHECK_EQ(cbor::diagnostic_notation(three).error(), error::nesting_depth_exceeded);
    CHECK_EQ(cbor::item_size(three), 4u);
    CHECK(*test::value_of(*test::lazy_of_copy(two)) == A(A(1)));
    CHECK_EQ(test::value_of(*test::lazy_of_copy(three)).error(), error::nesting_depth_exceeded);
}

// doc/advanced/path.md, section "Limits": the nesting depth bounds the decode of a node, a filter, a group, a
// function call and a literal in the path.
TEST_CASE("doc advanced path: the nesting depth")
{
    std::string const deep = preferred(A(A(A(1))));
    CHECK_EQ(test::at_path("$", *test::lazy_of_copy(deep)).error(), error::nesting_depth_exceeded);
    auto const one = *test::lazy_of_copy(preferred(A(1)));
    CHECK_EQ(test::query("$[?@ == [[[1]]]]", one).error(), error::nesting_depth_exceeded);
    CHECK_EQ(test::query("$[?@[?@[?@]]]", one).error(), error::nesting_depth_exceeded);
    CHECK_EQ(test::query("$[?(((@)))]", one).error(), error::nesting_depth_exceeded);
    CHECK_EQ(test::query("$[?length(value(value(@))) == 1]", one).error(), error::nesting_depth_exceeded);
}

// A tag counts one level, so 100(100(0)) holds an integer at depth 2 and 100(100(100(0))) at depth 3.
TEST_CASE("limits depth 2: a tag counts one level")
{
    CHECK(test::lazy_of_copy("\xd8\x64\xd8\x64\x00"s)->clone().has_value());
    CHECK_EQ(test::lazy_of_copy("\xd8\x64\xd8\x64\xd8\x64\x00"s)->clone().error(),
             error::nesting_depth_exceeded);
    CHECK_EQ(cbor::diagnostic_notation("\xd8\x64\xd8\x64\xd8\x64\x00"sv).error(),
             error::nesting_depth_exceeded);
}

// The reader keeps a stack of open containers bounded by the nesting depth of the build, so with the depth at 2
// it reads [[1]] and refuses [[[1]]], at every split and in every form.
TEST_CASE("limits depth 2: sequence_reader refuses an item one level deeper than the build allows")
{
    std::string const two = preferred(A(A(1)));
    std::string const three = preferred(A(A(A(1))));
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        CHECK_EQ(test::reader_outcome_byte_by_byte(two, form), test::sequence_outcome{{two}, error::too_little_data});
        for (std::size_t at = 0; at <= three.size(); ++at) {
            CAPTURE(at);
            test::sequence_outcome const read = test::reader_outcome_split_at(three, at, form);
            CHECK(read.items.empty());
            CHECK_EQ(read.last, error::nesting_depth_exceeded);
        }
    }
}
