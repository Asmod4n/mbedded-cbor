#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace std::string_literals;

// Each test in this file is one example of a page in doc/reference/path/. The tests exist so that every
// example compiles and gives the result that its page states.

// at_path.md: a number reads from a std::string in place, a text needs an owner, and a run-time query
// reads the same value as the compile-time path.
TEST_CASE("doc reference path: at_path")
{
    std::string bytes = test::preferred(test::M("user"s, test::M("id"s, 42, "name"s, "ann"s)));
    auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
    CHECK_EQ(*id, 42);
    CHECK_EQ(cbor::at_path<"$.user.age", std::int64_t>(bytes).error(), cbor::error::key_not_found);
    auto const owner = std::make_shared<std::string const>(std::move(bytes));
    auto const name = cbor::at_path<"$.user.name", std::string_view>(owner);
    CHECK_EQ(**name, "ann");
    auto const q = cbor::jsonpath::compile<std::int64_t>("$.user.id");
    REQUIRE(q.has_value());
    CHECK_EQ(cbor::at_path(*owner, *q), 42);
}

// at_path.md, section Errors: a moved-from query is a wrong use and throws std::logic_error.
TEST_CASE("doc reference path: at_path with a moved-from query throws")
{
    std::string const bytes = test::preferred(test::M("id"s, 42));
    auto q = cbor::jsonpath::compile<std::int64_t>("$.id").value();
    auto const kept = std::move(q);
    CHECK_THROWS_AS((void)cbor::at_path(bytes, q), std::logic_error);
    CHECK_EQ(cbor::at_path(bytes, kept), 42);
}

// at_path.md: T std::string copies the text out of the bytes, so it takes a std::string_view.
TEST_CASE("doc reference path: at_path with a copied value")
{
    std::string const bytes = test::preferred(test::M("name"s, "ann"s));
    CHECK_EQ(cbor::at_path<"$.name", std::string>(bytes), "ann"s);
}

// compile.md: compile parses a path at run time; a path with many results needs nodelist.
TEST_CASE("doc reference path: compile")
{
    auto const owner = std::make_shared<std::string const>(test::preferred(test::M("a"s, test::A(1, 2, 3))));
    auto const q = cbor::jsonpath::compile<cbor::nodelist>("$.a[?@ > 1]");
    REQUIRE(q.has_value());
    CHECK_EQ(cbor::at_path(owner, *q)->size(), 2u);
    CHECK_EQ(cbor::jsonpath::compile<int>("$.a[*]").error(), cbor::error::not_singular_query);
    CHECK_EQ(cbor::jsonpath::compile<cbor::lazy>("$.a[").error(), cbor::error::invalid_path);
}

// query.md: one compiled query reads many items.
TEST_CASE("doc reference path: query")
{
    auto const q = cbor::jsonpath::compile<std::int64_t>("$.id").value();
    std::string const first = test::preferred(test::M("id"s, 1));
    std::string const second = test::preferred(test::M("id"s, 2, "x"s, 0));
    CHECK_EQ(cbor::at_path(first, q), 1);
    CHECK_EQ(cbor::at_path(second, q), 2);
    CHECK((std::is_same_v<cbor::jsonpath::query<std::string_view>::result_type, cbor::ref<std::string_view>>));
    CHECK((std::is_same_v<cbor::jsonpath::query<int>::result_type, int>));
}

// nodelist.md: a path with a wildcard gives every match as a lazy in a nodelist; no match is an empty
// nodelist and no error.
TEST_CASE("doc reference path: nodelist")
{
    std::string bytes = test::preferred(test::M("a"s, test::A(1, 2, 3)));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes)).value();
    auto const nodes = doc.at_path<"$.a[*]", cbor::nodelist>();
    REQUIRE(nodes.has_value());
    CHECK_EQ(nodes->size(), 3u);
    CHECK_EQ((*nodes)[2].get<int>(), 3);
    CHECK(doc.at_path<"$.b", cbor::nodelist>()->empty());
}

namespace
{

template <cbor::fixed_string Path>
std::int64_t read_or_zero(std::string_view const bytes)
{
    return cbor::at_path<Path, std::int64_t>(bytes).value_or(0);
}

} // namespace

// fixed_string.md: a template of the program passes a path on as a fixed_string.
TEST_CASE("doc reference path: fixed_string")
{
    std::string const bytes = test::preferred(test::M("id"s, 42));
    CHECK_EQ(read_or_zero<"$.id">(bytes), 42);
    CHECK_EQ(read_or_zero<"$.age">(bytes), 0);
}

// is_valid_path_v.md: the grammar check of a path at compile time.
TEST_CASE("doc reference path: is_valid_path_v")
{
    CHECK(cbor::is_valid_path_v<"$..a">);
    CHECK_FALSE(cbor::is_valid_path_v<"$.a[">);
    CHECK_FALSE(cbor::is_valid_path_v<"$[?match(@.t, 'a.c')]">);
}

// is_singular_query_v.md: a singular query has names and indexes only.
TEST_CASE("doc reference path: is_singular_query_v")
{
    CHECK(cbor::is_singular_query_v<"$.a[0]['b']">);
    CHECK_FALSE(cbor::is_singular_query_v<"$.a[*]">);
    CHECK_FALSE(cbor::is_singular_query_v<"$..a">);
    CHECK_FALSE(cbor::is_singular_query_v<"$.a[">);
}
