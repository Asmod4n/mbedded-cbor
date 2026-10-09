#include "binding.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

value at(std::string_view const path, value const &data)
{
    std::string const doc = encoded(data);
    test_binding binding;
    auto const r = cbor::at_path(binding, path, *cbor::lazy::from(doc));
    REQUIRE(r.has_value());
    return *r;
}

error path_error(std::string_view const path, value const &data)
{
    std::string const doc = encoded(data);
    test_binding binding;
    auto const r = cbor::at_path(binding, path, *cbor::lazy::from(doc));
    REQUIRE_FALSE(r.has_value());
    return r.error();
}

template <cbor::fixed_string Path>
std::expected<value, cbor::error> compiled_at(std::string const &doc)
{
    test_binding binding;
    return cbor::at_path<Path>(binding, *cbor::lazy::from(doc));
}

value found(std::expected<value, cbor::error> const &r)
{
    CAPTURE(r.has_value() ? cbor::error{} : r.error());
    REQUIRE(r.has_value());
    return *r;
}

template <cbor::fixed_string Path>
concept path_compiles = requires(test_binding &b, cbor::lazy const &l) { cbor::at_path<Path>(b, l); };

} // namespace

// Ported from test.rb: 'path: plain point query (no wildcards)'. A query without a wildcard selects one node and
// gives its value.
TEST_CASE("path: a point query")
{
    CHECK(at("$.a.b", M("a"s, M("b"s, 42))) == V(42));
    CHECK(at("$['a']['b']", M("a"s, M("b"s, 42))) == V(42));
    CHECK(at("$", V(7)) == V(7));
}

// Ported from test.rb: 'path: single wildcard — terminal and with tail'. A query with a wildcard gives its
// nodelist as an array (RFC 9535 2.1.2).
TEST_CASE("path: one wildcard, at the end and with a tail")
{
    CHECK(at("$[*]", A(10, 20, 30)) == A(10, 20, 30));
    CHECK(at("$.*", A(10, 20, 30)) == A(10, 20, 30));
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

// RFC 9535 2.1.2: each segment maps the nodelist to the concatenation of what it selects, so two and three
// wildcards give one flat nodelist in the order of the query argument.
TEST_CASE("path: two and three wildcards")
{
    value const teams =
        M("teams"s, A(M("members"s, A(M("n"s, "a"s), M("n"s, "b"s))), M("members"s, A(M("n"s, "c"s)))));
    CHECK(at("$.teams[*].members[*].n", teams) == A("a"s, "b"s, "c"s));
    value const regions =
        M("regions"s,
          A(M("teams"s, A(M("members"s, A(M("n"s, "a"s), M("n"s, "b"s))), M("members"s, A(M("n"s, "c"s))))),
            M("teams"s, A(M("members"s, A(M("n"s, "d"s), M("n"s, "e"s), M("n"s, "f"s)))))));
    CHECK(at("$.regions[*].teams[*].members[*].n", regions) == A("a"s, "b"s, "c"s, "d"s, "e"s, "f"s));
}

// Ported from test.rb: 'path: adjacent wildcards [*][*]' and 'path: index then wildcard / wildcard then index'.
TEST_CASE("path: adjacent wildcards, index and wildcard in both orders")
{
    CHECK(at("$[*][*]", A(A(1, 2, 3), A(4, 5), A(), A(6))) == A(1, 2, 3, 4, 5, 6));
    CHECK(at("$.rows[1][*].v", M("rows"s, A(A(M("v"s, 10), M("v"s, 20)), A(M("v"s, 30), M("v"s, 40))))) ==
          A(30, 40));
    CHECK(at("$.rows[*][1]", M("rows"s, A(A(10, 20, 30), A(40, 50, 60), A(70, 80, 90)))) == A(20, 50, 80));
    CHECK(at("$.rows[*][-1]", M("rows"s, A(A(10, 20, 30), A(40, 50, 60)))) == A(30, 60));
}

// Ported from test.rb: 'path: edge cases — empty outer, empty inner, heterogeneous values'.
TEST_CASE("path: empty and mixed")
{
    CHECK(at("$.statuses[*].id", M("statuses"s, A())) == A());
    CHECK(at("$.groups[*].xs[*]", M("groups"s, A(M("xs"s, A()), M("xs"s, A(1, 2)), M("xs"s, A())))) == A(1, 2));
    value const xs = A(1, "two"s, simple{22}, simple{21}, A(3, 4), M("k"s, "v"s));
    CHECK(at("$.xs[*]", M("xs"s, xs)) == xs);
}

// RFC 9535 2.3.2.2: a wildcard selects the values of a map too. A selector that finds nothing adds no node to a
// nodelist; without a wildcard the missing node is the error.
TEST_CASE("path: a wildcard on a map and missing keys")
{
    CHECK(at("$[*]", M("a"s, 1, "b"s, 2)) == A(1, 2));
    CHECK(at("$.x[*]", M("x"s, M("y"s, 1))) == A(1));
    CHECK(at("$.statuses[*].id", M("not_statuses"s, A())) == A());
    CHECK(at("$.a[*].b", M("a"s, A(M("b"s, 1), M("c"s, 2), 3))) == A(1));
    CHECK(at("$[*][*]", A(1, A(2))) == A(2));
    CHECK_EQ(path_error("$.a", M("b"s, 1)), error::key_not_found);
    CHECK_EQ(path_error("$[3]", A(1)), error::index_out_of_bounds);
    CHECK_EQ(path_error("$.a", A(1)), error::not_indexable);
    CHECK_EQ(path_error("$[0]", V(1)), error::not_indexable);
}

// Ported from test.rb: 'path: compiled path is reusable across different lazies'.
TEST_CASE("path: one compiled path, two top-level items")
{
    std::string const d1 = encoded(M("items"s, A(M("id"s, 1), M("id"s, 2))));
    std::string const d2 = encoded(M("items"s, A(M("id"s, 9), M("id"s, 8), M("id"s, 7))));
    CHECK(found(compiled_at<"$.items[*].id">(d1)) == A(1, 2));
    CHECK(found(compiled_at<"$.items[*].id">(d2)) == A(9, 8, 7));
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

// RFC 9535 2.2 to 2.5: blanks before a segment and inside brackets, names in single or double quotes with their
// escapes, an index without leading zeros in the range of I-JSON. At run time a bracket holds the selectors of
// RFC 9535 and nothing else; every other form is invalid_path. A query has at most as many segments as the nesting depth.
TEST_CASE("path: the grammar")
{
    value const doc = M("a"s, M("b c"s, A(1, 2, 3)), "ü'\""s, 4);
    CHECK(at("$ .a [ 'b c' ] [-2]", doc) == V(2));
    CHECK(at("$.a[\"b c\"][ * ]", doc) == A(1, 2, 3));
    CHECK(at("$['\\u00fc\\'\"']", doc) == V(4));
    CHECK(at("$[\"\\u00FC'\\\"\"]", doc) == V(4));
    CHECK(at("$.\u00fc_1", M("\u00fc_1"s, 5)) == V(5));
    for (std::string_view const bad :
         {"a"sv, ""sv, "$.1a"sv, "$["sv, "$[*"sv, "$['open"sv, "$['k'"sv, "$[x]"sv, "$[1"sv, "$#"sv, "$[01]"sv,
          "$[-0]"sv, "$[1.5]"sv, "$[h'01']"sv, "$[true]"sv, "$.a "sv, "$.."sv, "$[1,]"sv, "$['\\x']"sv,
          "$['\\ud800']"sv, "$['\x01']"sv, "$[9007199254740992]"sv, "$[*]]"sv, "$."sv})
        CHECK_EQ(path_error(bad, A(1)), error::invalid_path);
    CHECK_EQ(path_error("$[9007199254740991]", A(1)), error::index_out_of_bounds);
    std::string const deep = encoded(A(A(A(A(1)))));
    test_binding binding;
    {
        test::nesting_depth_max_guard const depth{3};
        CHECK_EQ(cbor::at_path(binding, "$[0][0][0][0]", *cbor::lazy::from(deep)).error(), error::nesting_depth_exceeded);
    }
    test::nesting_depth_max_guard const depth{4};
    CHECK(cbor::at_path(binding, "$[0][0][0][0]", *cbor::lazy::from(deep)).has_value());
}

// The compile-time form reads the same grammar and, inside brackets, any literal of CBOR diagnostic notation
// (draft-ietf-cbor-edn-literals-28) as a map key. A query that is not valid, or that has more than the default nesting depth of
// segments, does not compile.
TEST_CASE("path: a query that is not valid does not compile")
{
    CHECK(path_compiles<"$.a[*][-1]['b']">);
    CHECK(path_compiles<"$[1.5][[1, 2]][{1: 2}][1000(\"x\")]">);
    CHECK(path_compiles<"$[h'01']">);
    CHECK_FALSE(path_compiles<"a">);
    CHECK_FALSE(path_compiles<"$[">);
    CHECK_FALSE(path_compiles<"$[01]">);
    CHECK_FALSE(path_compiles<"$[h'0']">);
    CHECK_FALSE(path_compiles<"$[1.1_1]">);
    CHECK_FALSE(path_compiles<"$[simple(24)]">);
    CHECK_FALSE(path_compiles<"$[1(]">);
    CHECK_FALSE(path_compiles<"$[0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0]">);
    CHECK(path_compiles<"$[0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0][0]">);
}

// Every key of the map below is a different kind of CBOR item. diagnostic_notation gives the diagnostic notation of each key,
// and that text in a compile-time query finds the entry. Other notations of the same item find it too: b64''
// for h'', 0x10 for 16, 0x1.8p0 for 1.5, <<1>> for h'01'. RFC 8949 5.6.1 compares keys by value, so an encoding
// indicator changes nothing: [1_0] finds the key 1_1 and [1.5_2] the key 1.5. An integer and a float stay apart.
TEST_CASE("path: the diagnostic notation of a key finds the entry")
{
    std::string const doc = "\xae"
                            "\x22\x01"
                            "\x42\x01\x02\x02"
                            "\xd9\x03\xe8\x61x\x03"
                            "\x82\x01\x02\x04"
                            "\xf9\x3e\x00\x05"
                            "\xf5\x06"
                            "\xf6\x07"
                            "\xa1\x61" "a\x01\x08"
                            "\x63" "b c\x09"
                            "\x19\x00\x01\x0a"
                            "\xf8\x63\x0b"
                            "\xf9\xfc\x00\x0c"
                            "\x41\x01\x0d"
                            "\x10\x0e"s;
    std::pair<std::string_view, std::string_view> const keys[] = {
        {"\x22"sv, "-3"sv},
        {"\x42\x01\x02"sv, "h'0102'"sv},
        {"\xd9\x03\xe8\x61x"sv, "1000(\"x\")"sv},
        {"\x82\x01\x02"sv, "[1, 2]"sv},
        {"\xf9\x3e\x00"sv, "1.5"sv},
        {"\xf5"sv, "true"sv},
        {"\xf6"sv, "null"sv},
        {"\xa1\x61" "a\x01"sv, "{\"a\": 1}"sv},
        {"\x63" "b c"sv, "\"b c\""sv},
        {"\x19\x00\x01"sv, "1_1"sv},
        {"\xf8\x63"sv, "simple(99)"sv},
        {"\xf9\xfc\x00"sv, "-Infinity"sv},
        {"\x41\x01"sv, "h'01'"sv},
        {"\x10"sv, "16"sv},
    };
    for (auto const &[bytes, text] : keys)
        CHECK_EQ(cbor::diagnostic_notation(bytes).value_or("not well-formed"), text);
    CHECK(found(compiled_at<"$[-3]">(doc)) == V(1));
    CHECK(found(compiled_at<"$[h'0102']">(doc)) == V(2));
    CHECK(found(compiled_at<"$[1000(\"x\")]">(doc)) == V(3));
    CHECK(found(compiled_at<"$[[1, 2]]">(doc)) == V(4));
    CHECK(found(compiled_at<"$[1.5]">(doc)) == V(5));
    CHECK(found(compiled_at<"$[true]">(doc)) == V(6));
    CHECK(found(compiled_at<"$[null]">(doc)) == V(7));
    CHECK(found(compiled_at<"$[{\"a\": 1}]">(doc)) == V(8));
    CHECK(found(compiled_at<"$[\"b c\"]">(doc)) == V(9));
    CHECK(found(compiled_at<"$[1_1]">(doc)) == V(10));
    CHECK(found(compiled_at<"$[simple(99)]">(doc)) == V(11));
    CHECK(found(compiled_at<"$[-Infinity]">(doc)) == V(12));
    CHECK(found(compiled_at<"$[h'01']">(doc)) == V(13));
    CHECK(found(compiled_at<"$[16]">(doc)) == V(14));
    CHECK(found(compiled_at<"$[b64'AQI']">(doc)) == V(2));
    CHECK(found(compiled_at<"$[0x10]">(doc)) == V(14));
    CHECK(found(compiled_at<"$[0x1.8p0]">(doc)) == V(5));
    CHECK(found(compiled_at<"$[<<1>>]">(doc)) == V(13));
    CHECK(found(compiled_at<"$[ [1,2,] ]">(doc)) == V(4));
    CHECK(found(compiled_at<"$[*]">(doc)) == A(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14));
    CHECK(found(compiled_at<"$[1]">(doc)) == V(10));
    CHECK(found(compiled_at<"$[1_0]">(doc)) == V(10));
    CHECK(found(compiled_at<"$[1.5_2]">(doc)) == V(5));
    CHECK(found(compiled_at<"$[1.5_3]">(doc)) == V(5));
    CHECK(found(compiled_at<"$[h'0102'_1]">(doc)) == V(2));
    CHECK(found(compiled_at<"$[1000_3(\"x\"_0)]">(doc)) == V(3));
    CHECK(found(compiled_at<"$[[_ 1, 2]]">(doc)) == V(4));
    CHECK_EQ(compiled_at<"$[1.0]">(doc).error(), error::key_not_found);
    std::string runtime_text = "$[16]";
    test_binding binding;
    CHECK(*cbor::at_path(binding, runtime_text, *cbor::lazy::from(doc)) == V(14));
}

// With value sharing (tags 28 and 29) a few bytes can name many nodes. A nodelist may hold as many nodes as the
// message has bytes; one more is an error. Each level below shares the level before it twice.
TEST_CASE("path: a nodelist is no longer than the message")
{
    std::string doc = "\x87\xd8\x1c\x82\x00\x00"s;
    for (char level = 0; level < 6; ++level)
        doc += "\xd8\x1c\x82\xd8\x1d"s + level + "\xd8\x1d"s + level;
    test_binding binding;
    auto const four = cbor::at_path(binding, "$[2][*][*][*]", *cbor::lazy::from(doc));
    REQUIRE(four.has_value());
    CHECK(*four == A(0, 0, 0, 0, 0, 0, 0, 0));
    CHECK_EQ(cbor::at_path(binding, "$[6][*][*][*][*][*][*][*]", *cbor::lazy::from(doc)).error(),
             error::nodelist_too_long);
}

// RFC 8949 5.6.1: keys compare by value. A head that is not preferred, a string in chunks, -0.0 against 0.0, a NaN
// by its significand widened with zeros, an array element by element, a map as a set of pairs whatever their
// order, and a tag by its number and content.
TEST_CASE("path: a key is found by its value, not by its bytes")
{
    std::string const doc = "\xaa"
                            "\x79\x00\x01" "a\x01"
                            "\x5a\x00\x00\x00\x01\x01\x02"
                            "\xfb\x80\x00\x00\x00\x00\x00\x00\x00\x03"
                            "\xfa\x7f\xc0\x00\x00\x04"
                            "\x82\x18\x01\x81\x19\x00\x02\x05"
                            "\xa2\x61" "b\x02\x61" "a\x01\x06"
                            "\xd8\x64\x59\x00\x01\x07\x07"
                            "\x3a\x00\x00\x00\x02\x08"
                            "\xf8\x63\x09"
                            "\xf9\x7c\x00\x0a"s;
    CHECK(found(compiled_at<"$[\"a\"]">(doc)) == V(1));
    CHECK(found(compiled_at<"$.a">(doc)) == V(1));
    CHECK(found(compiled_at<"$[h'01']">(doc)) == V(2));
    CHECK(found(compiled_at<"$[0.0]">(doc)) == V(3));
    CHECK(found(compiled_at<"$[-0.0]">(doc)) == V(3));
    CHECK(found(compiled_at<"$[NaN]">(doc)) == V(4));
    CHECK(found(compiled_at<"$[[1, [2]]]">(doc)) == V(5));
    CHECK(found(compiled_at<"$[{\"a\": 1, \"b\": 2}]">(doc)) == V(6));
    CHECK(found(compiled_at<"$[100(h'07')]">(doc)) == V(7));
    CHECK(found(compiled_at<"$[-3]">(doc)) == V(8));
    CHECK(found(compiled_at<"$[simple(99)]">(doc)) == V(9));
    CHECK(found(compiled_at<"$[Infinity_3]">(doc)) == V(10));
    CHECK_EQ(compiled_at<"$[[1, 2]]">(doc).error(), error::key_not_found);
    CHECK_EQ(compiled_at<"$[101(h'07')]">(doc).error(), error::key_not_found);
    CHECK_EQ(compiled_at<"$[0]">(doc).error(), error::key_not_found);
}

// RFC 8949 5.6.1 with value sharing: a shared reference anywhere inside a key stands for the item it names, so a
// key [1, 29(0)] equals [1, "x"] and {"k": 29(0)} equals {"k": "x"} when 28("x") is the first shared item.
TEST_CASE("path: a shared reference inside a key is followed")
{
    std::string const doc = "\x82\xd8\x1c\x61x\xa2\x82\x01\xd8\x1d\x00\x01\xa1\x61k\xd8\x1d\x00\x02"s;
    CHECK(found(compiled_at<"$[1][[1, \"x\"]]">(doc)) == V(1));
    CHECK(found(compiled_at<"$[1][{\"k\": \"x\"}]">(doc)) == V(2));
    CHECK_EQ(compiled_at<"$[1][[1, \"y\"]]">(doc).error(), error::key_not_found);
    CHECK_EQ(compiled_at<"$[1][[1, 29(0)]]">(doc).error(), error::key_not_found);
    std::string const forward = "\xa1\x82\x01\xd8\x1d\x00\x01"s;
    CHECK_EQ(compiled_at<"$[[1, \"x\"]]">(forward).error(), error::sharedref_index_not_marked);
}

// RFC 8949 5.6: a literal with a duplicate key does not compile. A key map of the top-level item with a
// repeated key is compared with a literal by RFC 8949 5.6.1 and not checked for a repeated key, so it is no
// match for a literal with other keys.
TEST_CASE("path: a key map with duplicate keys is no match for other keys")
{
    std::string const doc = "\xa1\xa2\x61k\x01\x61k\x01\x05"s;
    CHECK_EQ(compiled_at<"$[{\"k\": 1, \"j\": 1}]">(doc).error(), error::key_not_found);
    CHECK_EQ(compiled_at<"$[{\"k\": 1}]">(doc).error(), error::key_not_found);
    CHECK_FALSE(path_compiles<"$[{\"k\": 1, \"k\": 2}]">);
    CHECK_FALSE(path_compiles<"$[{\"k\": 1, \"k\"_0: 2}]">);
}

// RFC 8949 5.6 lets a decoder that is not in a deterministic profile keep one entry of a repeated key. Every
// form of at_path stops at the first key that matches (RFC 8949 5.6.1) and gives its value with no error: the
// run-time path, the compiled path, the typed read over the bytes, the typed read of a view, an integer key in
// two widths, a key under tags 28 and 29, and a repeated key one level down. A wildcard, a descendant segment
// and a filter take every entry of the map.
TEST_CASE("path: every form gives the first entry of a repeated key")
{
    std::string const doc = "\xa2\x61\x61\x01\x61\x61\x02"s;
    test_binding binding;
    CHECK(cbor::at_path(binding, "$.a", *cbor::lazy::from(doc)).has_value());
    CHECK(compiled_at<"$.a">(doc).has_value());
    CHECK_EQ((cbor::at_path<"$.a", int>(doc)), 1);
    auto const owner = std::make_shared<std::string const>("\xa2\x61\x61\x61x\x61\x61\x61y"s);
    auto const text = cbor::at_path<"$.a", std::string_view>(owner, *owner);
    REQUIRE(text.has_value());
    CHECK_EQ(**text, "x"sv);
    std::string const numbers = "\xa2\x01\x01\x18\x01\x02"s;
    CHECK_EQ((cbor::at_path<"$[1]", int>(numbers)), 1);
    CHECK(compiled_at<"$[1]">(numbers).has_value());
    std::string const shared = "\xa2\xd8\x1c\x61\x61\x01\xd8\x1d\x00\x02"s;
    CHECK_EQ((cbor::at_path<"$.a", int>(shared)), 1);
    std::string const deep = "\xa1\x61\x62\xa2\x61\x61\x01\x61\x61\x02"s;
    CHECK_EQ((cbor::at_path<"$.b.a", int>(deep)), 1);
    CHECK(compiled_at<"$.b.a">(deep).has_value());
    CHECK(cbor::at_path(binding, "$.*", *cbor::lazy::from(doc)).has_value());
    CHECK(cbor::at_path(binding, "$..c", *cbor::lazy::from(deep)).has_value());
    CHECK(cbor::at_path(binding, "$[?@ == 1]", *cbor::lazy::from(doc)).has_value());
    std::string const arrays = "\xa2\x81\x01\x00\x81\x18\x01\x01"s;
    CHECK(compiled_at<"$[[1]]">(arrays).has_value());
}

// RFC 9535 2.5.1.2: a child segment with several selectors gives, for each input node, the nodes of the first
// selector, then those of the second, in the order of the selectors. A node can occur twice.
TEST_CASE("path: several selectors in one segment")
{
    value const doc = M("a"s, 1, "b"s, 2, "c"s, A(10, 20, 30));
    CHECK(at("$['b', 'a']", doc) == A(2, 1));
    CHECK(at("$.c[2, 0, 2]", doc) == A(30, 10, 30));
    CHECK(at("$['a', 'x', *]", doc) == A(1, 1, 2, A(10, 20, 30)));
    CHECK(at("$.c[ 1 ,\t-1 ]", doc) == A(20, 30));
}

// RFC 9535 2.3.4.2.2: the defaults of start and end depend on the sign of step, negative bounds count from the end,
// bounds out of range are clamped, a negative step walks backwards, and step 0 selects nothing. A slice of a node
// that is not an array selects nothing.
TEST_CASE("path: the array slice selector")
{
    value const doc = A(0, 1, 2, 3, 4, 5, 6);
    CHECK(at("$[1:3]", doc) == A(1, 2));
    CHECK(at("$[5:]", doc) == A(5, 6));
    CHECK(at("$[1:5:2]", doc) == A(1, 3));
    CHECK(at("$[5:1:-2]", doc) == A(5, 3));
    CHECK(at("$[::-1]", doc) == A(6, 5, 4, 3, 2, 1, 0));
    CHECK(at("$[-2:]", doc) == A(5, 6));
    CHECK(at("$[-100:100:3]", doc) == A(0, 3, 6));
    CHECK(at("$[0:7:0]", doc) == A());
    CHECK(at("$[ 1 : 2 : 1 ]", doc) == A(1));
    CHECK(at("$[:]", M("a"s, 1)) == A());
    for (std::string_view const bad : {"$[1:2:3:4]"sv, "$[01:2]"sv, "$[1:-0]"sv, "$[:9007199254740992]"sv, "$[1:2:a]"sv})
        CHECK_EQ(path_error(bad, doc), error::invalid_path);
}

// RFC 9535 2.5.2.2: a descendant segment applies its selectors to the input node and then to every descendant,
// each node before its descendants and an array in its order. A descendant segment deeper than the nesting depth fails.
TEST_CASE("path: the descendant segment")
{
    value const doc = M("a"s, M("b"s, 1, "c"s, A(M("b"s, 2))), "b"s, 3);
    CHECK(at("$..b", doc) == A(3, 1, 2));
    CHECK(at("$..[0]", doc) == A(M("b"s, 2)));
    CHECK(at("$.a..*", doc) == A(1, A(M("b"s, 2)), M("b"s, 2), 2));
    CHECK(at("$..['b', 'c'][0]", doc) == A(M("b"s, 2)));
    for (std::string_view const bad : {"$..."sv, "$.. b"sv, "$..1"sv})
        CHECK_EQ(path_error(bad, doc), error::invalid_path);
    std::string const deep = encoded(A(A(A(A(A(1))))));
    test_binding binding;
    test::nesting_depth_max_guard const depth{3};
    CHECK_EQ(cbor::at_path(binding, "$..[0]", *cbor::lazy::from(deep)).error(), error::nesting_depth_exceeded);
}

// RFC 9535 2.3.5: a filter keeps the children for which the logical expression is true. A comparison of numbers
// compares by value, so 1 equals 1.0; < compares only numbers and strings; a side that selects nothing equals only
// another side that selects nothing. && binds more tightly than ||, ! negates an existence test or a group.
TEST_CASE("path: the filter selector")
{
    value const doc = A(M("a"s, 1), M("a"s, 2.0), M("a"s, "x"s), M("b"s, 1), M("a"s, simple{22}), M("a"s, A(1)));
    CHECK(at("$[?@.a == 1]", doc) == A(M("a"s, 1)));
    CHECK(at("$[?@.a == 2]", doc) == A(M("a"s, 2.0)));
    CHECK(at("$[?@.a > 1]", doc) == A(M("a"s, 2.0)));
    CHECK(at("$[?@.a <= 'x' && @.a >= 'x']", doc) == A(M("a"s, "x"s)));
    CHECK(at("$[?@.a == null]", doc) == A(M("a"s, simple{22})));
    CHECK(at("$[?@.a == $[0].a]", doc) == A(M("a"s, 1)));
    CHECK(at("$[?@.a == @.c]", doc) == A(M("b"s, 1)));
    CHECK(at("$[?@.b || @.a == 1 && @.a != 1]", doc) == A(M("b"s, 1)));
    CHECK(at("$[?(@.b || @.a == 1) && !@.b]", doc) == A(M("a"s, 1)));
    CHECK(at("$[?!(@.a)]", doc) == A(M("b"s, 1)));
    CHECK(at("$[?@.a[0]]", doc) == A(M("a"s, A(1))));
    CHECK(at("$[?@.a == -1e0 || @.a == 1.0e+0]", doc) == A(M("a"s, 1)));
    CHECK(at("$.*[?@ == 'x']", doc) == A("x"s));
    for (std::string_view const bad : {"$[?@.a]]"sv, "$[?1]"sv, "$[?@.a == @.*]"sv, "$[?!@.a == 1]"sv, "$[?@.a = 1]"sv,
                                       "$[?@.a == 01]"sv, "$[?@.a == 1.]"sv, "$[?@.a == True]"sv, "$[?(@.a]"sv,
                                       "$[?@.a == [1]]"sv, "$[?@.a == h'01']"sv, "$[?@.a == \"\\'\"]"sv})
        CHECK_EQ(path_error(bad, doc), error::invalid_path);
}

// RFC 9535 2.4.4 to 2.4.8: length counts Unicode scalar values, elements or members and gives Nothing for other
// values, and Nothing equals Nothing; count gives the size of a nodelist; value gives the value of a nodelist with one node. The owner decided
// that the library has no regular expressions, so match and search are refused as an invalid path. A function that
// is not well-typed (2.4.3) is an invalid path too.
TEST_CASE("path: the function extensions")
{
    value const doc = A(M("a"s, "\u00fcb"s), M("a"s, A(1, 2)), M("a"s, 3), M("a"s, M("x"s, 1, "y"s, 2)));
    CHECK(at("$[?length(@.a) == 2]", doc) == A(M("a"s, "\u00fcb"s), M("a"s, A(1, 2)), M("a"s, M("x"s, 1, "y"s, 2))));
    CHECK(at("$[?length(@.a) == length(@.b)]", doc) == A(M("a"s, 3)));
    CHECK(at("$[?count(@.a.*) == 2]", doc) == A(M("a"s, A(1, 2)), M("a"s, M("x"s, 1, "y"s, 2))));
    CHECK(at("$[?value(@..y) == 2]", doc) == A(M("a"s, M("x"s, 1, "y"s, 2))));
    CHECK(at("$[?length('abc') == 3]", A(1)) == A(1));
    for (std::string_view const bad :
         {"$[?match(@.a, 'a')]"sv, "$[?search(@.a, 'a')]"sv, "$[?length(@.a)]"sv, "$[?count(1) == 1]"sv,
          "$[?length(@.*) == 1]"sv, "$[?value(@.a, @.b) == 1]"sv, "$[?foo(@.a)]"sv, "$[?length() == 1]"sv})
        CHECK_EQ(path_error(bad, doc), error::invalid_path);
}

// The compile-time form takes an EDN literal (draft-ietf-cbor-edn-literals-28) wherever RFC 9535 takes a JSON
// literal, so a filter compares with byte strings, tags, arrays and maps too. A quoted string stays a text string
// as in RFC 9535, also with single quotes.
TEST_CASE("path: an EDN literal in a filter")
{
    std::string const doc = encoded(A(M("a"s, bytes{"\x01"}), M("a"s, tagged{1000, "x"s}), M("a"s, A(1, 2)),
                                      M("a"s, M("k"s, 1)), M("a"s, "s"s)));
    CHECK(found(compiled_at<"$[?@.a == h'01'].a">(doc)) == A(bytes{"\x01"}));
    CHECK(found(compiled_at<"$[?@.a == 1000(\"x\")].a">(doc)) == A(tagged{1000, "x"s}));
    CHECK(found(compiled_at<"$[?@.a == [1, 2]].a">(doc)) == A(A(1, 2)));
    CHECK(found(compiled_at<"$[?@.a == {\"k\": 1}].a">(doc)) == A(M("k"s, 1)));
    CHECK(found(compiled_at<"$[?@.a == 's'].a">(doc)) == A("s"s));
    CHECK(found(compiled_at<"$[?@.a == <<1>>].a">(doc)) == A(bytes{"\x01"}));
    CHECK(path_compiles<"$[?@.a == simple(99)]">);
    CHECK_FALSE(path_compiles<"$[?@.a == h'0']">);
    CHECK_FALSE(path_compiles<"$[?match(@.a, 'x')]">);
}

namespace
{

using step = std::variant<std::string_view, std::int64_t>;

// The tests compare the bits of a float, and the bytes of a view, never the view.
template <class T>
auto comparable(T const &v)
{
    if constexpr (std::is_same_v<T, std::string_view>)
        return std::string(v);
    else if constexpr (std::is_same_v<T, std::span<std::byte const>>)
        return std::vector<std::byte>(v.begin(), v.end());
    else if constexpr (std::is_same_v<T, cbor::typed_array>)
        return std::pair{v.tag, std::vector<std::byte>(v.bytes.begin(), v.bytes.end())};
    else if constexpr (std::is_same_v<T, double>)
        return std::bit_cast<std::uint64_t>(v);
    else
        return v;
}

template <class T>
using comparable_t = decltype(comparable(std::declval<T>()));

// The reference: lazy::from, one lazy::at for each step and lazy::get, as the chain of test/lazy.cpp.
template <class T>
std::expected<comparable_t<T>, error> lazy_get(std::string const &doc, std::vector<step> const &steps)
{
    auto const root = cbor::lazy::from(std::string(doc));
    if (!root)
        return std::unexpected(root.error());
    cbor::lazy node = *root;
    for (step const &s : steps) {
        auto const child = std::holds_alternative<std::int64_t>(s) ? node.at(std::get<std::int64_t>(s))
                                                                   : node.at(std::get<std::string_view>(s));
        if (!child)
            return std::unexpected(child.error());
        node = *child;
    }
    auto const v = node.get<T>();
    if (!v)
        return std::unexpected(v.error());
    if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                  std::is_same_v<T, cbor::typed_array>)
        return comparable(**v);
    else
        return comparable(*v);
}

template <cbor::fixed_string Path, class T>
std::expected<comparable_t<T>, error> path_get(std::string const &doc)
{
    if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                  std::is_same_v<T, cbor::typed_array>) {
        auto const owner = std::make_shared<std::string const>(doc);
        auto const r = cbor::at_path<Path, T>(owner, *owner);
        if (!r)
            return std::unexpected(r.error());
        return comparable(**r);
    } else {
        auto const r = cbor::at_path<Path, T>(doc);
        if (!r)
            return std::unexpected(r.error());
        return comparable(*r);
    }
}

// Each typed read is checked twice: against the value that the specification gives, and against the lazy chain
// over the same steps, so that the two forms give the same value or the same error.
template <cbor::fixed_string Path, class T, class Expected>
void check_path(std::string const &doc, std::vector<step> const &steps, Expected const &expected)
{
    auto const r = path_get<Path, T>(doc);
    CAPTURE(r.has_value() ? cbor::error{} : r.error());
    CHECK(r == expected);
    CHECK(r == lazy_get<T>(doc, steps));
}

std::vector<std::byte> byte_vector(std::string_view const s)
{
    return std::vector<std::byte>(std::as_bytes(std::span(s)).begin(), std::as_bytes(std::span(s)).end());
}

bool inside(std::string const &doc, void const *const p)
{
    auto const *const at = static_cast<char const *>(p);
    return std::less_equal<>{}(doc.data(), at) && std::less<>{}(at, doc.data() + doc.size());
}

} // namespace

// The three reads of bench/runtime.cpp, on small messages of the same shape: a text deep in a map of arrays, one
// float of an array of floats, a text in an array of records.
TEST_CASE("path: a typed read of each shape of the bench")
{
    std::string const statuses = encoded(M("statuses"s, A(M("user"s, M("screen_name"s, "ann"s)), M("user"s, M("screen_name"s, "bob"s)),
                                                          M("user"s, M("screen_name"s, "cy"s)))));
    check_path<"$.statuses[2].user.screen_name", std::string_view>(statuses, {"statuses"sv, std::int64_t{2}, "user"sv, "screen_name"sv},
                                                                   "cy"sv);
    std::string const floats = encoded(A(0.5, 1.5, 2.5));
    check_path<"$[1]", double>(floats, {std::int64_t{1}}, std::bit_cast<std::uint64_t>(1.5));
    std::string const records = encoded(A(M("id"s, 1, "user"s, M("name"s, "ann"s)), M("id"s, 2, "user"s, M("name"s, "bob"s))));
    check_path<"$[1].user.name", std::string_view>(records, {std::int64_t{1}, "user"sv, "name"sv}, "bob"sv);
}

// The typed read gives a view into the bytes of the caller, held by the owner that the caller gives. The view stays
// inside those bytes also where a tag 29 on the walk makes the read go through lazy.
TEST_CASE("path: a typed read gives a view into the bytes of the caller")
{
    auto const doc = std::make_shared<std::string const>("\x82\xd8\x1c\x61x\xa1\x61k\x63xyz"s);
    auto const text = cbor::at_path<"$[1].k", std::string_view>(doc, *doc);
    REQUIRE(text.has_value());
    CHECK_EQ(**text, "xyz"sv);
    CHECK(inside(*doc, (*text)->data()));
    auto const shared = std::make_shared<std::string const>("\x82\xd8\x1c\x61x\xd8\x1d\x00"s);
    auto const named = cbor::at_path<"$[1]", std::string_view>(shared, *shared);
    REQUIRE(named.has_value());
    CHECK_EQ(**named, "x"sv);
    CHECK(inside(*shared, (*named)->data()));
    auto const blob = std::make_shared<std::string const>("\x81\x43\x01\x02\x03"s);
    auto const span = cbor::at_path<"$[0]", std::span<std::byte const>>(blob, *blob);
    REQUIRE(span.has_value());
    CHECK(inside(*blob, (*span)->data()));
    auto const typed = std::make_shared<std::string const>("\x81\xd8\x48\x43\x01\x02\x03"s);
    auto const array = cbor::at_path<"$[0]", cbor::typed_array>(typed, *typed);
    REQUIRE(array.has_value());
    CHECK(inside(*typed, (*array)->bytes.data()));
}

// The view outlives every name of the bytes that the caller had: the result holds the owner. Under ASan a read of
// bytes that are freed is reported, so this test fails there if the owner is not held.
TEST_CASE("path: a typed read holds the owner of the bytes")
{
    auto const read = [] {
        auto const owner = std::make_shared<std::string const>("\xa1\x61\x61\x63xyz"s);
        return cbor::at_path<"$.a", std::string_view>(owner, *owner);
    };
    auto const text = read();
    REQUIRE(text.has_value());
    CHECK_EQ(**text, "xyz"sv);
    auto const tagged = [] {
        auto const owner = std::make_shared<std::string const>("\x82\xd8\x1c\x61x\xd8\x1d\x00"s);
        return cbor::at_path<"$[1]", std::string_view>(owner, *owner);
    }();
    REQUIRE(tagged.has_value());
    CHECK_EQ(**tagged, "x"sv);
}

// An empty owner is a wrong use that the compiler cannot see.
TEST_CASE("path: a typed read with an empty owner throws std::logic_error")
{
    std::string const doc = "\xa1\x61\x61\x63xyz"s;
    auto const read = [&doc] { return cbor::at_path<"$.a", std::string_view>(std::shared_ptr<void const>{}, doc); };
    CHECK_THROWS_AS((void)read(), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. The test exists because a check of the stored
// pointer takes an aliasing std::shared_ptr that holds nothing, and the view then outlives its bytes; it also refuses
// a std::shared_ptr that holds the bytes and stores a null pointer.
TEST_CASE("path: a typed read checks that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>("\xa1\x61\x61\x63xyz"s);
    std::shared_ptr<void const> const holds_nothing(std::shared_ptr<void const>{}, bytes->data());
    auto const read = [&bytes](std::shared_ptr<void const> const &owner) {
        return cbor::at_path<"$.a", std::string_view>(owner, *bytes);
    };
    CHECK_THROWS_AS((void)read(holds_nothing), std::logic_error);
    std::shared_ptr<void const> const holds_bytes(bytes, nullptr);
    auto const r = read(holds_bytes);
    REQUIRE(r.has_value());
    CHECK_EQ(**r, "xyz"sv);
}

// A move gives the owner and the view to the target. The test exists because a source that kept its view after it
// lost its owner read freed memory once the target and the bytes were gone. The source keeps an empty view.
TEST_CASE("path: a moved owning_ref keeps no view")
{
    auto const read = [] {
        auto const owner = std::make_shared<std::string const>("\xa1\x61\x61\x63xyz"s);
        return *cbor::at_path<"$.a", std::string_view>(owner, *owner);
    };
    std::optional<cbor::owning_ref<std::string_view>> source(read());
    {
        auto const target = std::move(*source);
        CHECK_EQ(*target, "xyz"sv);
    }
    CHECK(source->operator->()->empty());
    CHECK_EQ(source->operator->()->data(), nullptr);
    std::optional<cbor::owning_ref<std::string_view>> assigned(read());
    source.emplace(read());
    *source = std::move(*assigned);
    assigned.reset();
    CHECK_EQ(**source, "xyz"sv);
    auto &same = *source;
    *source = std::move(same);
    CHECK_EQ(**source, "xyz"sv);
}

// RFC 8949 3: a data item has at least its initial byte, and an argument or a string has as many bytes as its head
// says. A message that ends before is too little data, at the target and in a sibling that the walk skips.
TEST_CASE("path: a typed read of a message that ends too early")
{
    check_path<"$", std::int64_t>(""s, {}, std::unexpected(error::too_little_data));
    check_path<"$.a", std::int64_t>("\xa1\x61\x61"s, {"a"sv}, std::unexpected(error::too_little_data));
    check_path<"$.a", std::string_view>("\xa1\x61\x61\x63xy"s, {"a"sv}, std::unexpected(error::too_little_data));
    check_path<"$[1]", std::int64_t>("\x82\x19\x01"s, {std::int64_t{1}}, std::unexpected(error::too_little_data));
    check_path<"$.a", std::int64_t>("\xa1\x61"s, {"a"sv}, std::unexpected(error::too_little_data));
}

// RFC 8949 3: additional information 28 to 30 is reserved, and RFC 8949 3.3 a simple value below 32 in the two-byte
// form is not well-formed. Both are a syntax error, at the target and in a skipped sibling.
TEST_CASE("path: a typed read of a head that is not well-formed")
{
    check_path<"$", std::int64_t>("\x1c"s, {}, std::unexpected(error::syntax_error));
    check_path<"$[1]", std::int64_t>("\x82\xf8\x10\x01"s, {std::int64_t{1}}, std::unexpected(error::syntax_error));
    check_path<"$[0]", std::int64_t>("\x81\xdc\x01"s, {std::int64_t{0}}, std::unexpected(error::syntax_error));
    check_path<"$[0]", std::int64_t>("\x81\xdf\x01"s, {std::int64_t{0}}, std::unexpected(error::syntax_error));
}

// The decoder refuses indefinite length (RFC 8949 3.2) everywhere: a map on the walk, an array in a skipped
// sibling, a text at the target.
TEST_CASE("path: a typed read refuses indefinite length")
{
    check_path<"$.a", std::int64_t>("\xbf\x61\x61\x01\xff"s, {"a"sv}, std::unexpected(error::indefinite_length));
    check_path<"$[1]", std::int64_t>("\x82\x9f\xff\x01"s, {std::int64_t{1}}, std::unexpected(error::indefinite_length));
    check_path<"$.a", std::string_view>("\xa1\x61\x61\x7f\x61x\xff"s, {"a"sv}, std::unexpected(error::indefinite_length));
    check_path<"$[0]", std::int64_t>("\x9f\x01\xff"s, {std::int64_t{0}}, std::unexpected(error::indefinite_length));
}

// RFC 9535 2.3.1.2 and 2.3.3.2: a name selects only from a map and an index from an array or, here, an integer key
// of a map. A singular query that selects nothing is an error, and the error says why.
TEST_CASE("path: a typed read of a path that is not in the message")
{
    check_path<"$.a", std::int64_t>("\x83\x01\x02\x03"s, {"a"sv}, std::unexpected(error::not_indexable));
    check_path<"$[0]", std::int64_t>("\x05"s, {std::int64_t{0}}, std::unexpected(error::not_indexable));
    check_path<"$.a.b", std::int64_t>("\xa1\x61\x61\x61x"s, {"a"sv, "b"sv}, std::unexpected(error::not_indexable));
    check_path<"$.b", std::int64_t>("\xa1\x61\x61\x01"s, {"b"sv}, std::unexpected(error::key_not_found));
    check_path<"$.a", std::int64_t>("\xa1\x41\x61\x01"s, {"a"sv}, std::unexpected(error::key_not_found));
    check_path<"$[2]", std::int64_t>("\xa2\x01\x61x\x21\x61y"s, {std::int64_t{2}}, std::unexpected(error::key_not_found));
    check_path<"$[2]", std::int64_t>("\x82\x01\x02"s, {std::int64_t{2}}, std::unexpected(error::index_out_of_bounds));
    check_path<"$[-3]", std::int64_t>("\x82\x01\x02"s, {std::int64_t{-3}}, std::unexpected(error::index_out_of_bounds));
    check_path<"$[0]", std::int64_t>("\x80"s, {std::int64_t{0}}, std::unexpected(error::index_out_of_bounds));
}

// RFC 8949 5.6.1: text strings are compared byte by byte, so a key whose head is not in the preferred serialization
// of RFC 8949 4.1 is the same key. The path keeps its key in the preferred form; a key on the wire with another head
// for the same text is found, and a key with the same head byte and another length is not.
TEST_CASE("path: a typed read finds a key whose head is not preferred")
{
    check_path<"$.a", std::uint64_t>("\xa1\x78\x01" "a\x05"s, {"a"sv}, 5u);
    check_path<"$.a", std::uint64_t>("\xa2\x79\x00\x01" "b\x01\x7a\x00\x00\x00\x01" "a\x02"s, {"a"sv}, 2u);
    std::string const long_key = "abcdefghijklmnopqrstuvwx";
    check_path<"$.abcdefghijklmnopqrstuvwx", std::uint64_t>("\xa2\x78\x19"s + long_key + "y\x01\x79\x00\x18"s + long_key + "\x02"s,
                                                            {std::string_view(long_key)}, 2u);
    check_path<"$.abcdefghijklmnopqrstuvwx", std::uint64_t>("\xa1\x78\x19"s + long_key + "y\x01"s, {std::string_view(long_key)},
                                                            std::unexpected(error::key_not_found));
}

// RFC 9535 2.3.3.1: a negative index counts from the end of the array. An index selector on a map selects the value
// under the equal integer key, positive or negative (RFC 8949 3.1).
TEST_CASE("path: a typed read with a negative index and with an integer key")
{
    std::string const three = "\x83\x01\x02\x03"s;
    check_path<"$[-1]", std::uint64_t>(three, {std::int64_t{-1}}, 3u);
    check_path<"$[-3]", std::uint64_t>(three, {std::int64_t{-3}}, 1u);
    std::string const keyed = "\xa2\x01\x61x\x21\x61y"s;
    check_path<"$[1]", std::string_view>(keyed, {std::int64_t{1}}, "x"sv);
    check_path<"$[-2]", std::string_view>(keyed, {std::int64_t{-2}}, "y"sv);
    check_path<"$['a'][-1]['b']", std::string_view>(encoded(M("a"s, A(1, M("b"s, "z"s)))), {"a"sv, std::int64_t{-1}, "b"sv}, "z"sv);
}

// Each type of the read takes the CBOR items that RFC 8949 maps to it and refuses every other item as incorrect_type.
// An integer that the type cannot hold is out of range (RFC 8949 3.1: a 64-bit magnitude and the sign in the major
// type).
TEST_CASE("path: a typed read of each type")
{
    check_path<"$", std::int64_t>("\x38\x63"s, {}, std::int64_t{-100});
    check_path<"$", std::int64_t>("\x3b\x7f\xff\xff\xff\xff\xff\xff\xff"s, {}, std::numeric_limits<std::int64_t>::min());
    check_path<"$", std::uint64_t>("\x1b\xff\xff\xff\xff\xff\xff\xff\xff"s, {}, std::numeric_limits<std::uint64_t>::max());
    check_path<"$", std::uint8_t>("\x18\xff"s, {}, std::uint8_t{255});
    check_path<"$", std::int8_t>("\x38\x7f"s, {}, std::int8_t{-128});
    check_path<"$", std::int16_t>("\x39\x7f\xff"s, {}, std::int16_t{-32768});
    check_path<"$", std::int32_t>("\x1a\x7f\xff\xff\xff"s, {}, std::int32_t{2147483647});
    check_path<"$", std::uint64_t>("\x20"s, {}, std::unexpected(error::number_out_of_range));
    check_path<"$", std::int64_t>("\x1b\x80\x00\x00\x00\x00\x00\x00\x00"s, {}, std::unexpected(error::number_out_of_range));
    check_path<"$", std::int8_t>("\x18\x80"s, {}, std::unexpected(error::number_out_of_range));
    check_path<"$", std::uint16_t>("\x1a\x00\x01\x00\x00"s, {}, std::unexpected(error::number_out_of_range));
    check_path<"$", std::uint64_t>("\xf9\x3c\x00"s, {}, std::unexpected(error::incorrect_type));
    check_path<"$", std::uint64_t>("\x61\x61"s, {}, std::unexpected(error::incorrect_type));

    check_path<"$", double>("\xf9\x3c\x00"s, {}, std::bit_cast<std::uint64_t>(1.0));
    check_path<"$", double>("\xfa\x47\xc3\x50\x00"s, {}, std::bit_cast<std::uint64_t>(100000.0));
    check_path<"$", double>("\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"s, {}, std::bit_cast<std::uint64_t>(1.1));
    check_path<"$", double>("\x01"s, {}, std::unexpected(error::incorrect_type));
    check_path<"$", double>("\xf5"s, {}, std::unexpected(error::incorrect_type));

    check_path<"$", bool>("\xf5"s, {}, true);
    check_path<"$", bool>("\xf4"s, {}, false);
    check_path<"$", bool>("\xf6"s, {}, std::unexpected(error::incorrect_type));
    check_path<"$", std::nullptr_t>("\xf6"s, {}, nullptr);
    check_path<"$", std::nullptr_t>("\xf4"s, {}, std::unexpected(error::incorrect_type));
    check_path<"$", std::nullptr_t>("\xf7"s, {}, std::unexpected(error::incorrect_type));

    check_path<"$", std::string_view>("\x63\x61\x62\x63"s, {}, "abc"sv);
    check_path<"$", std::string_view>("\x60"s, {}, ""sv);
    check_path<"$", std::string_view>("\x43\x61\x62\x63"s, {}, std::unexpected(error::incorrect_type));
    check_path<"$", std::span<std::byte const>>("\x43\x01\x02\x03"s, {}, byte_vector("\x01\x02\x03"));
    check_path<"$", std::span<std::byte const>>("\x63\x61\x62\x63"s, {}, std::unexpected(error::incorrect_type));
    check_path<"$", cbor::typed_array>("\xd8\x48\x43\x01\x02\x03"s, {}, std::pair{std::uint64_t{72}, byte_vector("\x01\x02\x03")});
    check_path<"$", cbor::typed_array>("\x43\x01\x02\x03"s, {}, std::unexpected(error::incorrect_type));
    check_path<"$", cbor::typed_array>("\xd8\x4c\x41\x01"s, {}, std::unexpected(error::incorrect_type));
    check_path<"$", cbor::typed_array>("\xd8\x41\x43\x01\x02\x03"s, {}, std::unexpected(error::inadmissible_type_for_tag_content));
}

// RFC 8949 3.4.3: the content of a bignum is a byte string. A magnitude that fits 64 bits is the integer.
TEST_CASE("path: a typed read of a bignum")
{
    check_path<"$", std::uint64_t>("\xc2\x42\x01\x00"s, {}, std::uint64_t{256});
    check_path<"$", std::int64_t>("\xc3\x41\x01"s, {}, std::int64_t{-2});
    check_path<"$", std::uint64_t>("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s, {}, std::unexpected(error::number_out_of_range));
    check_path<"$", std::uint64_t>("\xc2\x01"s, {}, std::unexpected(error::inadmissible_type_for_tag_content));
    check_path<"$", std::uint64_t>("\xc2\x42\x01"s, {}, std::unexpected(error::too_little_data));
}

// RFC 8949 3.4.5.1: tag 24 holds an encoded data item in a byte string, and the walk goes into it. A key under tag
// 24 is a tag, not a text, so it is not the name. The content of a bignum or a typed array under tag 24 is no byte
// string. An embedded data item has marks of its own (as in test/lazy.cpp).
TEST_CASE("path: a typed read through tag 24")
{
    check_path<"$.a[1]", std::uint64_t>("\xa1\x61\x61\xd8\x18\x43\x82\x01\x02"s, {"a"sv, std::int64_t{1}}, 2u);
    check_path<"$.a", std::uint64_t>("\xa1\xd8\x18\x42\x61\x61\x01"s, {"a"sv}, std::unexpected(error::key_not_found));
    check_path<"$.a", std::string_view>("\xa1\x61\x61\xd8\x18\x44\x63xyz"s, {"a"sv}, "xyz"sv);
    check_path<"$", std::uint64_t>("\xc2\xd8\x18\x42\x41\x05"s, {}, std::unexpected(error::inadmissible_type_for_tag_content));
    check_path<"$", cbor::typed_array>("\xd8\x40\xd8\x18\x42\x41\x05"s, {}, std::unexpected(error::inadmissible_type_for_tag_content));
    check_path<"$", std::uint64_t>("\xd8\x18\x05"s, {}, std::unexpected(error::inadmissible_type_for_tag_content));
    check_path<"$[0]", std::uint64_t>("\xd8\x18\x42\x81"s, {std::int64_t{0}}, std::unexpected(error::too_little_data));
    check_path<"$[1][1]", std::uint64_t>("\xd8\x18\x48\x82\x01\xd8\x18\x43\x82\x02\x03"s, {std::int64_t{1}, std::int64_t{1}}, 3u);
    check_path<"$[1][1]", std::uint64_t>("\x82\xd8\x1c\x07\xd8\x18\x47\x82\xd8\x1c\x09\xd8\x1d\x00"s,
                                         {std::int64_t{1}, std::int64_t{1}}, 9u);
}

// RFC 8949 3.4 and the registration of tag 28: a mark leaves the value as it is. A mark on the walk, on a key, on the
// target and on the content of a bignum or a typed array changes nothing.
TEST_CASE("path: a typed read through tag 28")
{
    check_path<"$.a[1]", std::uint64_t>("\xa1\x61\x61\xd8\x1c\x82\x01\x02"s, {"a"sv, std::int64_t{1}}, 2u);
    check_path<"$.a", std::uint64_t>("\xa1\xd8\x1c\x61\x61\x01"s, {"a"sv}, 1u);
    check_path<"$[7]", std::uint64_t>("\xa1\xd8\x1c\x07\x03"s, {std::int64_t{7}}, 3u);
    check_path<"$[0]", std::uint64_t>("\x81\xd8\x1c\x05"s, {std::int64_t{0}}, 5u);
    check_path<"$[0]", std::uint64_t>("\x81\xd8\x1c\xd8\x1c\x05"s, {std::int64_t{0}}, 5u);
    check_path<"$", std::uint64_t>("\xc2\xd8\x1c\x42\x01\x00"s, {}, std::uint64_t{256});
    check_path<"$", cbor::typed_array>("\xd8\x48\xd8\x1c\x43\x01\x02\x03"s, {}, std::pair{std::uint64_t{72}, byte_vector("\x01\x02\x03")});
    check_path<"$", std::uint64_t>("\xd8\x1c"s, {}, std::unexpected(error::too_little_data));
}

// The registration of tag 29: a reference stands for the value of the mark it names, and the mark lies before it. A
// reference on the walk, on a key, on the target, in the content of a bignum or of a typed array, and to a mark inside
// a skipped sibling gives that value. A reference with no mark, to a mark that is not complete, or with content that
// is no unsigned integer is an error.
TEST_CASE("path: a typed read through tag 29")
{
    check_path<"$[1][0]", std::uint64_t>("\x82\xd8\x1c\x82\x07\x08\xd8\x1d\x00"s, {std::int64_t{1}, std::int64_t{0}}, 7u);
    check_path<"$[1].a", std::uint64_t>("\x82\xd8\x1c\x61\x61\xa1\xd8\x1d\x00\x02"s, {std::int64_t{1}, "a"sv}, 2u);
    check_path<"$[1]", std::string_view>("\x82\xd8\x1c\x61x\xd8\x1d\x00"s, {std::int64_t{1}}, "x"sv);
    check_path<"$[1]", std::uint64_t>("\x82\xd8\x1c\x41\x05\xc2\xd8\x1d\x00"s, {std::int64_t{1}}, 5u);
    check_path<"$[2]", cbor::typed_array>("\x83\xd8\x1c\x41\x05\xc2\xd8\x1d\x00\xd8\x40\xd8\x1d\x00"s, {std::int64_t{2}},
                                          std::pair{std::uint64_t{64}, byte_vector("\x05")});
    check_path<"$[1]", std::uint64_t>("\x82\x81\xd8\x1c\x09\xd8\x1d\x00"s, {std::int64_t{1}}, 9u);
    check_path<"$.a", std::uint64_t>("\xa1\x61\x61\xd8\x1d\x05"s, {"a"sv}, std::unexpected(error::sharedref_index_not_marked));
    check_path<"$[1]", std::uint64_t>("\x82\xd8\x1c\x01\xd8\x1d\x61x"s, {std::int64_t{1}}, std::unexpected(error::inadmissible_type_for_tag_content));
    check_path<"$", std::uint64_t>("\xd8\x1c\xd8\x1d\x00"s, {}, std::unexpected(error::sharedref_not_complete));
}

// Ported from test/lazy.cpp: the inputs that the fuzzers found for lazy give the same answer through the typed
// read. Each typed read starts with no marks, so the reference forward to a mark is not marked here, where lazy
// had recorded the mark in an earlier step.
TEST_CASE("path: a typed read of the fuzzer findings of lazy")
{
    check_path<"$[0]", std::uint64_t>("\xd8\x1c\xd8\x1d\x00"s, {std::int64_t{0}}, std::unexpected(error::sharedref_not_complete));
    check_path<"$.a", std::uint64_t>("\xbb\x6a\xc9\xfb\x32\xf6\xd8\xd8\x27\x61\x61\x19\x00\x00"s, {"a"sv}, 0u);
    check_path<"$[0][0]", std::uint64_t>("\x92\xd8\x1c\xd8\x1c\xd8\x1d\x00"s, {std::int64_t{0}, std::int64_t{0}},
                                         std::unexpected(error::sharedref_not_complete));
    std::string const forward = "\x82\xd8\x1d\x00\xd8\x1c\x05"s;
    check_path<"$[1]", std::uint64_t>(forward, {std::int64_t{1}}, 5u);
    check_path<"$[0]", std::uint64_t>(forward, {std::int64_t{0}}, std::unexpected(error::sharedref_index_not_marked));
    check_path<"$.a", std::uint64_t>("\xa2\x41\x61\x01\x61\x61\x02"s, {"a"sv}, 2u);
}

// A skip keeps one count of the items still to read and no stack, and nothing recurses in it, so the walk skips an
// item of any nesting depth. The count of segments is what the nesting depth bounds.
TEST_CASE("path: a typed read skips an item of any nesting depth")
{
    std::string const deeper = "\x82"s + repeat("\x81", 2000) + "\x00\x01"s;
    check_path<"$[1]", std::uint64_t>(deeper, {std::int64_t{1}}, 1u);
    std::string const keyed = "\xa2\x61k"s + repeat("\x81", 2000) + "\x00\x61\x61\x01"s;
    check_path<"$.a", std::uint64_t>(keyed, {"a"sv}, 1u);
    test::nesting_depth_max_guard const depth{4};
    CHECK(path_get<"$[1]", std::uint64_t>("\x82\x81\x81\x81\x81\x00\x01"s) == 1u);
}

// The typed read compiles only where it is safe. A view is read only with an owner of the bytes, so it cannot outlive
// them: a std::string, a temporary std::string, a std::string_view and a literal alone give no view. A temporary or a
// moved std::string beside an owner does not compile either, because the owner does not hold it and the view would
// read freed memory. A std::string that the caller keeps is read beside an owner. A scalar holds no bytes and is read
// from any of them. The path is a singular query of RFC 9535 2.3.5.1 with names and indexes only: a wildcard, a
// descendant segment and a literal key of EDN go to at_path with a binding. A path with more segments than the
// default nesting depth does not compile.
TEST_CASE("path: a typed read that is not safe does not compile")
{
    std::string text = "\xa1\x61\x61\x61x"s;
    auto const owner = std::make_shared<std::string const>(text);
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::at_path<"$.a", std::string_view>(o, std::string_view(*o)); }; }(owner)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::at_path<"$.a", std::span<std::byte const>>(o, std::string_view(*o)); }; }(owner)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::at_path<"$.a", cbor::typed_array>(o, std::string_view(*o)); }; }(owner)));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::string_view>(std::forward<S>(s)); }; }(text)));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::string_view>(std::forward<S>(s)); }; }(std::as_const(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::string_view>(std::forward<S>(s)); }; }(std::string{})));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::string_view>(std::forward<S>(s)); }; }(std::move(std::as_const(text)))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::string_view>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::string_view>(std::forward<S>(s)); }; }("\xa1\x61\x61\x61x")));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::at_path<"$.a", std::string_view>(o, std::string(*o)); }; }(owner)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o, std::string s) { cbor::at_path<"$.a", std::string_view>(o, std::move(s)); }; }(owner)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::at_path<"$.a", std::string_view>(o, std::declval<std::string const>()); }; }(owner)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::at_path<"$.a", cbor::typed_array>(o, std::string(*o)); }; }(owner)));
    CHECK(([]<class O>(O const &) { return requires(O const &o, std::string const &s) { cbor::at_path<"$.a", std::string_view>(o, s); }; }(owner)));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::span<std::byte const>>(std::forward<S>(s)); }; }(text)));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", cbor::typed_array>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::at_path<"$.a", std::int64_t>(o, std::string_view(*o)); }; }(owner)));
    CHECK(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::int64_t>(std::forward<S>(s)); }; }(std::string{})));
    CHECK(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK(cbor::at_path<"$.a", std::int64_t>(std::string(text)) == std::unexpected(error::incorrect_type));

    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a[*]", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$..a", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$[1.5]", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$[h'01']", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$[0,1]", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$[0:1]", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$[?@.a]", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"a", std::int64_t>(std::forward<S>(s)); }; }(std::string_view(text))));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&s) { cbor::at_path<"$.a", float>(std::forward<S>(s)); }; }(std::string_view(text))));
}

// A path that compiles has at most the default nesting depth of segments. The depth in force at run time can be
// lower, so the walk checks the count of segments against it again.
TEST_CASE("at_path: a compiled path with more segments than the nesting depth in force is refused")
{
    std::string const text = "\x81\x81\x81\x07";
    test::nesting_depth_max_guard const depth{2};
    CHECK_EQ(cbor::at_path<"$[0][0]", std::int64_t>(std::string_view(text)).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$[0][0][0]", std::int64_t>(std::string_view(text)).error(), error::nesting_depth_exceeded);
    auto const owner = std::make_shared<std::string const>(text);
    CHECK_EQ(cbor::at_path<"$[0][0][0]", std::string_view>(owner, *owner).error(), error::nesting_depth_exceeded);
    auto const l = cbor::lazy::from(text);
    REQUIRE(l.has_value());
    test_binding binding;
    CHECK_EQ(cbor::at_path<"$[0][0][0]">(binding, *l).error(), error::nesting_depth_exceeded);
    CHECK_EQ(cbor::at_path(binding, "$[0][0][0]", *l).error(), error::nesting_depth_exceeded);
}
