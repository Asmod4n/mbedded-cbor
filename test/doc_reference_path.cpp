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
using namespace std::string_view_literals;

// Each test in this file is one example of a reference page of the path group; the comments of query.hpp,
// rfc9535.hpp and lazy.hpp take the examples through the snippet markers. The tests exist so that every example
// compiles and gives the result that its page states.

// query.hpp, snippet at_path: a number reads from a std::string in place, a text needs an owner, and a run-time
// query reads the same value as the compile-time path.
TEST_CASE("doc reference path: at_path")
{
    std::string bytes = test::preferred(test::M("user"s, test::M("id"s, 42, "name"s, "ann"s)));
    std::int64_t run_time = 0;
    // [at_path]
    // bytes: a std::string with {"user": {"id": 42, "name": "ann"}}
    auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);          // 42
    auto const owner = std::make_shared<std::string const>(std::move(bytes));
    auto const name = cbor::at_path<"$.user.name", std::string_view>(owner);  // **name: "ann"
    auto const q = cbor::jsonpath::compile<std::int64_t>("$.user.id");
    if (q)
        run_time = cbor::at_path(*owner, *q).value_or(0);                    // 42
    // [at_path]
    CHECK_EQ(*id, 42);
    CHECK_EQ(**name, "ann");
    CHECK_EQ(run_time, 42);
    REQUIRE(q.has_value());
    CHECK_EQ(cbor::at_path<"$.user.age", std::int64_t>(*owner).error(), cbor::error::key_not_found);
}

// query.hpp, the comment of at_path: a moved-from query is a wrong use and throws std::logic_error.
TEST_CASE("doc reference path: at_path with a moved-from query throws")
{
    std::string const bytes = test::preferred(test::M("id"s, 42));
    auto q = cbor::jsonpath::compile<std::int64_t>("$.id").value();
    auto const kept = std::move(q);
    CHECK_THROWS_AS((void)cbor::at_path(bytes, q), std::logic_error);
    CHECK_EQ(cbor::at_path(bytes, kept), 42);
}

// query.hpp, the comment of at_path: T std::string copies the text out of the bytes, so it takes a std::string_view.
TEST_CASE("doc reference path: at_path with a copied value")
{
    std::string const bytes = test::preferred(test::M("name"s, "ann"s));
    CHECK_EQ(cbor::at_path<"$.name", std::string>(bytes), "ann"s);
}

// query.hpp, snippet compile: compile parses a path at run time; a path with many results needs nodelist.
TEST_CASE("doc reference path: compile")
{
    auto const owner = std::make_shared<std::string const>(test::preferred(test::M("a"s, test::A(1, 2, 3))));
    std::size_t found = 0;
    // [compile]
    // owner: a std::shared_ptr<std::string const> with {"a": [1, 2, 3]}
    auto const q = cbor::jsonpath::compile<cbor::nodelist>("$.a[?@ > 1]");
    if (q)
        found = cbor::at_path(owner, *q)->size();                    // 2
    auto const many = cbor::jsonpath::compile<int>("$.a[*]");         // not_singular_query
    auto const broken = cbor::jsonpath::compile<cbor::lazy>("$.a[");  // invalid_path
    // [compile]
    REQUIRE(q.has_value());
    CHECK_EQ(found, 2u);
    CHECK_EQ(many.error(), cbor::error::not_singular_query);
    CHECK_EQ(broken.error(), cbor::error::invalid_path);
}

// query.hpp, snippet query: one compiled query reads many items.
TEST_CASE("doc reference path: query")
{
    std::string const first = test::preferred(test::M("id"s, 1));
    std::string const second = test::preferred(test::M("id"s, 2, "x"s, 0));
    // [query]
    // first: {"id": 1}, second: {"id": 2, "x": 0}
    auto const q = cbor::jsonpath::compile<std::int64_t>("$.id").value();
    auto const one = cbor::at_path(first, q);   // 1
    auto const two = cbor::at_path(second, q);  // 2
    // [query]
    CHECK_EQ(one, 1);
    CHECK_EQ(two, 2);
    CHECK((std::is_same_v<cbor::jsonpath::query<std::string_view>::result_type, cbor::ref<std::string_view>>));
    CHECK((std::is_same_v<cbor::jsonpath::query<int>::result_type, int>));
}

// lazy.hpp, snippet nodelist: a path with a wildcard gives every match as a lazy in a nodelist; no match is an empty
// nodelist and no error.
TEST_CASE("doc reference path: nodelist")
{
    std::string bytes = test::preferred(test::M("a"s, test::A(1, 2, 3)));
    // [nodelist]
    // bytes: a std::string with {"a": [1, 2, 3]}
    auto const doc = cbor::lazy::take_ownership(std::move(bytes)).value();
    auto const nodes = doc.at_path<"$.a[*]", cbor::nodelist>();  // 3 nodes
    auto const third = (*nodes)[2].get<int>();                   // 3
    auto const none = doc.at_path<"$.b", cbor::nodelist>();      // an empty nodelist
    // [nodelist]
    REQUIRE(nodes.has_value());
    CHECK_EQ(nodes->size(), 3u);
    CHECK_EQ(third, 3);
    CHECK(none->empty());
}

namespace
{

// [fixed_string]
template <cbor::fixed_string Path>
std::int64_t read_or_zero(std::string_view const bytes)
{
    return cbor::at_path<Path, std::int64_t>(bytes).value_or(0);
}
// [fixed_string]

} // namespace

// rfc9535.hpp, snippet fixed_string: a template of the program passes a path on as a fixed_string.
TEST_CASE("doc reference path: fixed_string")
{
    std::string const bytes = test::preferred(test::M("id"s, 42));
    // [fixed_string use]
    // bytes: {"id": 42}
    auto const id = read_or_zero<"$.id">(bytes);    // 42
    auto const age = read_or_zero<"$.age">(bytes);  // 0
    // [fixed_string use]
    CHECK_EQ(id, 42);
    CHECK_EQ(age, 0);
}

// rfc9535.hpp, snippet is_valid_path_v: the grammar check of a path at compile time.
TEST_CASE("doc reference path: is_valid_path_v")
{
    // [is_valid_path_v]
    bool const descendant = cbor::is_valid_path_v<"$..a">;                // true
    bool const open = cbor::is_valid_path_v<"$.a[">;                      // false
    bool const match = cbor::is_valid_path_v<"$[?match(@.t, 'a.c')]">;    // false
    // [is_valid_path_v]
    CHECK(descendant);
    CHECK_FALSE(open);
    CHECK_FALSE(match);
}

// rfc9535.hpp, snippet is_singular_query_v: a singular query has names and indexes only.
TEST_CASE("doc reference path: is_singular_query_v")
{
    // [is_singular_query_v]
    bool const names = cbor::is_singular_query_v<"$.a[0]['b']">;  // true
    bool const wildcard = cbor::is_singular_query_v<"$.a[*]">;    // false
    bool const descendant = cbor::is_singular_query_v<"$..a">;    // false
    bool const open = cbor::is_singular_query_v<"$.a[">;          // false: not valid
    // [is_singular_query_v]
    CHECK(names);
    CHECK_FALSE(wildcard);
    CHECK_FALSE(descendant);
    CHECK_FALSE(open);
}

// path/index.md, CBOR beyond JSON: an integer key, a key in EDN, a path through tag 24 and an element of a typed
// array. The bytes are those of the page.
TEST_CASE("doc reference path: CBOR beyond JSON")
{
    auto const keys = cbor::lazy::take_ownership("\xa2\x01\x61\x78\x41\x01\x61\x7a"s);
    auto const x = keys->at_path<"$[1]">()->get<std::string_view>();
    REQUIRE(x);
    CHECK_EQ(**x, "x"sv);
    auto const z = keys->at_path<"$[h'01']">()->get<std::string_view>();
    REQUIRE(z);
    CHECK_EQ(**z, "z"sv);
    auto const embedded = cbor::lazy::take_ownership("\xa1\x61\x61\xd8\x18\x43\x82\x01\x02"s);
    CHECK_EQ(embedded->at_path<"$.a[1]">()->get<int>().value(), 2);
    auto const typed = cbor::lazy::take_ownership("\xa1\x61\x76\xd8\x45\x44\x07\x00\x08\x00"s);
    CHECK_EQ(typed->at_path<"$.v[1]">()->get<int>().value(), 8);
}
