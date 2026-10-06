#include "binding.hpp"

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
    std::string const doc = encoded(data);
    test_binding binding;
    auto const r = cbor::at_path<16>(binding, path, *cbor::decode<16>(doc));
    REQUIRE(r.has_value());
    return *r;
}

error path_error(std::string_view const path, value const &data)
{
    std::string const doc = encoded(data);
    test_binding binding;
    auto const r = cbor::at_path<16>(binding, path, *cbor::decode<16>(doc));
    REQUIRE_FALSE(r.has_value());
    return r.error();
}

template <cbor::fixed_string Path>
cbor::result<value> compiled_at(std::string const &doc)
{
    test_binding binding;
    return cbor::at_path<Path, 16>(binding, *cbor::decode<16>(doc));
}

value found(cbor::result<value> const &r)
{
    CAPTURE(r.has_value() ? cbor::error{} : r.error());
    REQUIRE(r.has_value());
    return *r;
}

template <cbor::fixed_string Path>
concept path_compiles = requires(test_binding &b, cbor::lazy const &l) { cbor::at_path<Path, 4>(b, l); };

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
// RFC 9535 and nothing else; every other form is invalid_path. A query has at most DepthMax segments.
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
    CHECK_EQ(cbor::at_path<3>(binding, "$[0][0][0][0]", *cbor::decode<16>(deep)).error(), error::nesting_depth_exceeded);
    CHECK(cbor::at_path<4>(binding, "$[0][0][0][0]", *cbor::decode<16>(deep)).has_value());
}

// The compile-time form reads the same grammar and, inside brackets, any literal of CBOR diagnostic notation
// (draft-ietf-cbor-edn-literals-28) as a map key. A query that is not valid, or that has more than DepthMax
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
    CHECK_FALSE(path_compiles<"$[0][0][0][0][0]">);
    CHECK(path_compiles<"$[0][0][0][0]">);
}

// Every key of the map below is a different kind of CBOR item. inspect gives the diagnostic notation of each key,
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
        CHECK_EQ(cbor::inspect(bytes).value_or("not well-formed"), text);
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
    CHECK(*cbor::at_path<16>(binding, runtime_text, *cbor::decode<16>(doc)) == V(14));
}

// With value sharing (tags 28 and 29) a few bytes can name many nodes. A nodelist may hold as many nodes as the
// message has bytes; one more is an error. Each level below shares the level before it twice.
TEST_CASE("path: a nodelist is no longer than the message")
{
    std::string doc = "\x87\xd8\x1c\x82\x00\x00"s;
    for (char level = 0; level < 6; ++level)
        doc += "\xd8\x1c\x82\xd8\x1d"s + level + "\xd8\x1d"s + level;
    test_binding binding;
    auto const four = cbor::at_path<16>(binding, "$[2][*][*][*]", *cbor::decode<16>(doc));
    REQUIRE(four.has_value());
    CHECK(*four == A(0, 0, 0, 0, 0, 0, 0, 0));
    CHECK_EQ(cbor::at_path<16>(binding, "$[6][*][*][*][*][*][*][*]", *cbor::decode<16>(doc)).error(),
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

// RFC 8949 5.6: a map with duplicate keys is not valid. A key map of the top-level item with a duplicate pair is not taken
// as equal to a literal with as many pairs: the lookup gives duplicate_key. A literal with a duplicate key does not
// compile.
TEST_CASE("path: a key map with duplicate keys is not valid")
{
    std::string const doc = "\xa1\xa2\x61k\x01\x61k\x01\x05"s;
    CHECK_EQ(compiled_at<"$[{\"k\": 1, \"j\": 1}]">(doc).error(), error::duplicate_key);
    CHECK_EQ(compiled_at<"$[{\"k\": 1}]">(doc).error(), error::key_not_found);
    CHECK_FALSE(path_compiles<"$[{\"k\": 1, \"k\": 2}]">);
    CHECK_FALSE(path_compiles<"$[{\"k\": 1, \"k\"_0: 2}]">);
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
// each node before its descendants and an array in its order. A descendant segment deeper than DepthMax fails.
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
    CHECK_EQ(cbor::at_path<3>(binding, "$..[0]", *cbor::decode<16>(deep)).error(), error::nesting_depth_exceeded);
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
