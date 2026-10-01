#include "host.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

value at(std::string_view const path, value const &data)
{
    auto const steps = cbor::path_compile(path);
    REQUIRE(steps.has_value());
    std::string const doc = encoded(data);
    test_host host;
    auto const r = cbor::path_decode<16>(host, *steps, cbor::lazy{doc, 0});
    REQUIRE(r.has_value());
    return *r;
}

error path_error(std::string_view const path, value const &data)
{
    auto const steps = cbor::path_compile(path);
    REQUIRE(steps.has_value());
    std::string const doc = encoded(data);
    test_host host;
    auto const r = cbor::path_decode<16>(host, *steps, cbor::lazy{doc, 0});
    REQUIRE_FALSE(r.has_value());
    return r.error();
}

} // namespace

// Ported from test.rb: 'path: plain point query (no wildcards)'.
TEST_CASE("path: a point query")
{
    CHECK(at("$.a.b", M("a"s, M("b"s, 42))) == V(42));
}

// Ported from test.rb: 'path: single wildcard — terminal and with tail'.
TEST_CASE("path: one wildcard, at the end and with a tail")
{
    CHECK(at("$[*]", A(10, 20, 30)) == A(10, 20, 30));
    value const h = M("users"s, A(M("name"s, "A"s, "age"s, 30), M("name"s, "B"s, "age"s, 25),
                                  M("name"s, "C"s, "age"s, 35)));
    CHECK(at("$.users[*].name", h) == A("A"s, "B"s, "C"s));
}

// Ported from test.rb: 'path: single wildcard — nested tail + bracket-index tail'.
TEST_CASE("path: one wildcard with a nested tail and an index tail")
{
    value const h = M("rows"s, A(M("d"s, M("v"s, 1)), M("d"s, M("v"s, 2)), M("d"s, M("v"s, 3))));
    CHECK(at("$.rows[*].d.v", h) == A(1, 2, 3));
    CHECK(at("$.pairs[*][1]", M("pairs"s, A(A(1, 2), A(3, 4), A(5, 6)))) == A(2, 4, 6));
}

// Ported from test.rb: 'path: two wildcards — nested results mirror the structure' and
// 'path: three wildcards — triple-nested results'.
TEST_CASE("path: two and three wildcards")
{
    value const teams =
        M("teams"s, A(M("members"s, A(M("n"s, "a"s), M("n"s, "b"s))), M("members"s, A(M("n"s, "c"s)))));
    CHECK(at("$.teams[*].members[*].n", teams) == A(A("a"s, "b"s), A("c"s)));
    value const regions =
        M("regions"s,
          A(M("teams"s, A(M("members"s, A(M("n"s, "a"s), M("n"s, "b"s))), M("members"s, A(M("n"s, "c"s))))),
            M("teams"s, A(M("members"s, A(M("n"s, "d"s), M("n"s, "e"s), M("n"s, "f"s)))))));
    CHECK(at("$.regions[*].teams[*].members[*].n", regions) ==
          A(A(A("a"s, "b"s), A("c"s)), A(A("d"s, "e"s, "f"s))));
}

// Ported from test.rb: 'path: adjacent wildcards [*][*]' and 'path: index then wildcard / wildcard
// then index'.
TEST_CASE("path: adjacent wildcards, index and wildcard in both orders")
{
    CHECK(at("$[*][*]", A(A(1, 2, 3), A(4, 5), A(), A(6))) == A(A(1, 2, 3), A(4, 5), A(), A(6)));
    CHECK(at("$.rows[1][*].v", M("rows"s, A(A(M("v"s, 10), M("v"s, 20)), A(M("v"s, 30), M("v"s, 40))))) ==
          A(30, 40));
    CHECK(at("$.rows[*][1]", M("rows"s, A(A(10, 20, 30), A(40, 50, 60), A(70, 80, 90)))) == A(20, 50, 80));
}

// Ported from test.rb: 'path: edge cases — empty outer, empty inner, heterogeneous values'.
TEST_CASE("path: empty and mixed")
{
    CHECK(at("$.statuses[*].id", M("statuses"s, A())) == A());
    CHECK(at("$.groups[*].xs[*]", M("groups"s, A(M("xs"s, A()), M("xs"s, A(1, 2)), M("xs"s, A())))) ==
          A(A(), A(1, 2), A()));
    value const xs = A(1, "two"s, simple{22}, simple{21}, A(3, 4), M("k"s, "v"s));
    CHECK(at("$.xs[*]", M("xs"s, xs)) == xs);
}

// Ported from test.rb: 'path: errors — wildcard on non-array, missing keys'.
TEST_CASE("path: a wildcard on a map and missing keys")
{
    CHECK_EQ(path_error("$[*]", M("a"s, 1)), error::not_indexable);
    CHECK_EQ(path_error("$.x[*]", M("x"s, M("y"s, 1))), error::not_indexable);
    CHECK_EQ(path_error("$.statuses[*].id", M("not_statuses"s, A())), error::key_not_found);
    CHECK_EQ(path_error("$.a[*].b", M("a"s, A(M("b"s, 1), M("c"s, 2)))), error::key_not_found);
}

// Ported from test.rb: 'path: compiled path is reusable across different lazies'.
TEST_CASE("path: one compiled path, two documents")
{
    auto const steps = cbor::path_compile("$.items[*].id");
    REQUIRE(steps.has_value());
    std::string const d1 = encoded(M("items"s, A(M("id"s, 1), M("id"s, 2))));
    std::string const d2 = encoded(M("items"s, A(M("id"s, 9), M("id"s, 8), M("id"s, 7))));
    test_host host;
    CHECK(*cbor::path_decode<16>(host, *steps, cbor::lazy{d1, 0}) == A(1, 2));
    CHECK(*cbor::path_decode<16>(host, *steps, cbor::lazy{d2, 0}) == A(9, 8, 7));
}

// Ported from test.rb: 'path: [*] skips untouched fields cheaply (regression for greedy decode)'.
TEST_CASE("path: a wildcard over records with large fields")
{
    std::string const big(10000, 'x');
    array users;
    for (int i = 1; i <= 5; ++i)
        users.push_back(M("name"s, "u" + std::to_string(i), "blob"s, big));
    CHECK(at("$.users[*].name", M("users"s, value{users})) == A("u1"s, "u2"s, "u3"s, "u4"s, "u5"s));
}

// The grammar of mruby-cbor's cbor_path_parse: $, .identifier, [index], ["key"], [*], spaces.
// Each malformed form it raises ArgumentError for is invalid_path here.
TEST_CASE("path: the grammar")
{
    REQUIRE(cbor::path_compile("$ .a [ \"b c\"] [-2] [*]").has_value() == false);
    auto const ok = cbor::path_compile("$.a[\"b c\"][-2][*]");
    REQUIRE(ok.has_value());
    CHECK_EQ(ok->size(), 4);
    for (std::string_view const bad :
         {"$.1a"sv, "$["sv, "$[*"sv, "$[\"open"sv, "$[\"k\""sv, "$[x]"sv, "$[1"sv, "$#"sv})
        CHECK_EQ(cbor::path_compile(bad).error(), error::invalid_path);
}
