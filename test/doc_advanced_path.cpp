#include "reference.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/advanced/path.md. The tests exist so that every example in the document
// compiles and gives the result that the document states.

namespace
{

template <cbor::fixed_string Path, class T>
concept advanced_typed_path_compiles = requires(std::string_view const s) { cbor::at_path<Path, T>(s); };

template <cbor::fixed_string Path>
concept advanced_lazy_path_compiles = requires(cbor::lazy const &l) { l.at_path<Path>(); };

template <cbor::fixed_string Path>
concept advanced_lazy_query_compiles = requires(cbor::lazy const &l) { l.query<Path>(); };

} // namespace

// The section "The forms" shows the form that reads one number from bytes the caller holds.
TEST_CASE("doc advanced path: a scalar from bytes")
{
    std::string const bytes = preferred(M("user"s, M("id"s, 42, "score"s, 2.5, "admin"s, simple{21})));
    CHECK_EQ(cbor::at_path<"$.user.id", std::int64_t>(bytes), 42);
    CHECK_EQ(cbor::at_path<"$.user.score", double>(bytes), 2.5);
    CHECK_EQ(cbor::at_path<"$.user.admin", bool>(bytes), true);
}

// A user did not know that encoded is a std::string_view, so an element of a cbor::sequence goes in directly.
TEST_CASE("doc advanced path: encoded is a string_view")
{
    auto const bytes = std::make_shared<std::string const>(preferred(M("n"s, 1)) + preferred(M("n"s, 2)) +
                                                           preferred(M("n"s, 3)));
    std::int64_t sum = 0;
    for (auto const e : cbor::sequence{bytes})
        sum += *cbor::at_path<"$.n", std::int64_t>(*e);
    CHECK_EQ(sum, 6);
    CHECK_EQ(cbor::at_path<"$.n", std::int64_t>(std::string_view{*bytes}), 1);
}

// The section "A view and its owner" shows that a view needs an owner, and that the view
// points into the bytes of that owner.
TEST_CASE("doc advanced path: a view with its owner")
{
    auto const owner = std::make_shared<std::string const>(preferred(M("name"s, "ann"s, "key"s, bytes{"\x01\x02"})));
    auto const name = cbor::at_path<"$.name", std::string_view>(owner);
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "ann"sv);
    auto const key = cbor::at_path<"$.key", std::span<std::byte const>>(owner);
    REQUIRE(key.has_value());
    CHECK_EQ((*key)->size(), 2u);
    std::string const typed = "\xa1\x61v\xd8\x45\x44\x07\x00\x08\x00"s;
    auto const held = std::make_shared<std::string const>(typed);
    auto const array = cbor::at_path<"$.v", cbor::typed_array>(held);
    REQUIRE(array.has_value());
    CHECK_EQ((*array)->tag, 69u);
    CHECK_EQ((*array)->bytes.size(), 4u);
    CHECK(([]<class S>(S &&) {
        return requires(S &&s) { cbor::at_path<"$.name", std::string_view>(std::forward<S>(s)); };
    }(std::string_view(*owner))));
}

// The section "The forms" shows that a lazy is a start point for every typed form.
TEST_CASE("doc advanced path: a typed read from a lazy")
{
    std::string const bytes = preferred(M("cars"s, A(M("hp"s, 5), M("hp"s, 7, "make"s, "kia"s))));
    auto const cars = cbor::lazy::copy(bytes)->at("cars");
    REQUIRE(cars.has_value());
    CHECK_EQ(cars->at_path<"$[1].hp", std::int64_t>(), 7);
    auto const make = cars->at_path<"$[1].make", std::string_view>();
    REQUIRE(make.has_value());
    CHECK_EQ(**make, "kia"sv);
}

// The section "Any value into your own type" shows at_path that gives a lazy node, with the path at compile time and at
// run time, and cbor::query for a nodelist.
TEST_CASE("doc advanced path: a node at compile time and at run time")
{
    std::string const bytes = preferred(M("a"s, A(1, 2, 3)));
    auto const l = *cbor::lazy::copy(bytes);
    CHECK(*test::at_path<"$.a">(l) == A(1, 2, 3));
    std::string const path = "$.a[2]";
    CHECK(*test::at_path(path, l) == V(3));
    CHECK(*test::query<"$.a[*]">(l) == A(1, 2, 3));
    CHECK(*test::query("$.a[?@ > 1]", l) == A(2, 3));
    CHECK(*test::query("$.b", l) == A());
}


// The section "Which types a typed read gives" shows the allowed types, the conversions that are refused and
// the types that do not compile.
TEST_CASE("doc advanced path: the types of a typed read")
{
    CHECK_EQ(cbor::at_path<"$", std::uint8_t>(preferred(V(255))), std::uint8_t{255});
    CHECK_EQ(cbor::at_path<"$", std::uint8_t>(preferred(V(256))).error(), error::number_out_of_range);
    CHECK_EQ(cbor::at_path<"$", unsigned>(preferred(V(-1))).error(), error::number_out_of_range);
    CHECK_EQ(cbor::at_path<"$", double>(preferred(V(1))), 1.0);
    CHECK_EQ(cbor::at_path<"$", double>("\x1b\x00\x20\x00\x00\x00\x00\x00\x01"s).error(), error::number_out_of_range);
    CHECK_EQ(cbor::at_path<"$", std::int64_t>(preferred(V(1.0))).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$", std::uint64_t>("\xc2\x42\x01\x00"s), 256u);
    CHECK_EQ(cbor::at_path<"$", std::nullptr_t>(preferred(V(simple{22}))), nullptr);
    CHECK_EQ(cbor::at_path<"$", cbor::simple_value>("\xf8\x63"s), cbor::simple_value{99});
    CHECK_EQ(cbor::at_path<"$", cbor::simple_value>("\xf5"s), cbor::simple_value{21});
    CHECK_EQ(cbor::at_path<"$", cbor::simple_value>("\xf6"s), cbor::simple_value{22});
    CHECK_EQ(cbor::at_path<"$", char>(preferred(V(65))), 'A');
    CHECK_EQ(cbor::at_path<"$", int>("\xc3\x41\x01"s), -2);
    CHECK_EQ(cbor::at_path<"$", int>("\xc1\x05"s), 5);
    CHECK(*cbor::lazy::copy("\xd8\x20\x61x"s)->at_path<"$", std::string_view>() == "x"sv);
    CHECK_FALSE((advanced_typed_path_compiles<"$", float>));
    CHECK_FALSE((advanced_typed_path_compiles<"$", std::string>));
    CHECK((advanced_typed_path_compiles<"$", std::string_view>));
}

// The section "Path syntax" shows each selector and segment of RFC 9535 with its result.
TEST_CASE("doc advanced path: the selectors and segments")
{
    value const doc = M("store"s, M("book"s, A(M("title"s, "A"s, "price"s, 8), M("title"s, "B"s, "price"s, 12),
                                              M("title"s, "C"s, "price"s, 9, "isbn"s, "x"s)),
                                    "bicycle"s, M("price"s, 20)));
    auto const l = *cbor::lazy::copy(preferred(doc));
    CHECK(*test::query("$", l) == A(doc));
    CHECK(*test::query("$.store.bicycle.price", l) == A(20));
    CHECK(*test::query("$['store']['bicycle']['price']", l) == A(20));
    CHECK(*test::query("$.store.book[0].title", l) == A("A"s));
    CHECK(*test::query("$.store.book[-1].title", l) == A("C"s));
    CHECK(*test::query("$.store.book[*].price", l) == A(8, 12, 9));
    CHECK(*test::query("$.store.book[0, 2].title", l) == A("A"s, "C"s));
    CHECK(*test::query("$.store.book[0:2].title", l) == A("A"s, "B"s));
    CHECK(*test::query("$.store.book[::-1].title", l) == A("C"s, "B"s, "A"s));
    CHECK(*test::query("$..price", l) == A(8, 12, 9, 20));
    CHECK(*test::query("$.store.book[?@.price < 10].title", l) == A("A"s, "C"s));
    CHECK(*test::query("$.store.book[?@.isbn].title", l) == A("C"s));
    CHECK(*test::query("$.store.book[?!@.isbn && @.price > 10].title", l) == A("B"s));
    CHECK(*test::query("$.store.book[?@.price == 8 || @.title == 'B'].title", l) == A("A"s, "B"s));
    CHECK(*test::query("$.store.book[?@.price > $.store.bicycle.price].title", l) == A());
}

// The section "Functions" shows length, count and value, and that match and search are refused.
TEST_CASE("doc advanced path: the functions")
{
    auto const l = *cbor::lazy::copy(preferred(A(M("t"s, "abc"s), M("t"s, A(1, 2)), M("t"s, 5))));
    CHECK(*test::query("$[?length(@.t) == 3].t", l) == A("abc"s));
    CHECK(*test::query("$[?count(@.t[*]) == 2].t", l) == A(A(1, 2)));
    CHECK(*test::query("$[?value(@..t) == 5].t", l) == A(5));
    CHECK_EQ(test::query("$[?match(@.t, 'a.c')]", l).error(), error::invalid_path);
    CHECK_EQ(test::query("$[?search(@.t, 'b')]", l).error(), error::invalid_path);
    CHECK_FALSE(cbor::is_valid_path_v<"$[?match(@.t, 'a.c')]">);
}

// The section "Keys that are not text" shows integer keys, a negative integer key and EDN literals as keys, in a
// compile-time path and in a run-time path alike, and a typed read through a key that is not text.
TEST_CASE("doc advanced path: keys that are not text")
{
    std::string const keyed = "\xa4\x01\x61x\x21\x61y\x41\x01\x61z\x82\x01\x02\x61w"s;
    auto const l = *cbor::lazy::copy(keyed);
    CHECK(*test::at_path<"$[1]">(l) == V("x"s));
    CHECK(*test::at_path<"$[-2]">(l) == V("y"s));
    CHECK(*test::at_path<"$[h'01']">(l) == V("z"s));
    CHECK(*test::at_path<"$[[1, 2]]">(l) == V("w"s));
    CHECK(*test::at_path("$[1]", l) == V("x"s));
    CHECK(*test::at_path("$[h'01']", l) == V("z"s));
    CHECK(*test::at_path("$[[1, 2]]", l) == V("w"s));
    CHECK(*test::query<"$[?@ == h'01']">(*cbor::lazy::copy(preferred(A(bytes{"\x01"}, 1)))) == A(bytes{"\x01"}));
    CHECK(*test::query("$[?@ == h'01']", *cbor::lazy::copy(preferred(A(bytes{"\x01"}, 1)))) == A(bytes{"\x01"}));
    CHECK(advanced_lazy_path_compiles<"$[h'01']">);
    CHECK((advanced_typed_path_compiles<"$[h'01']", int>));
    auto const z = l.at_path<"$[h'01']", std::string_view>();
    REQUIRE(z.has_value());
    CHECK_EQ(**z, "z"sv);
    std::string const counted = "\xa1\x41\x01\x07"s;
    CHECK_EQ(cbor::at_path<"$[h'01']", int>(std::string_view(counted)), 7);
    CHECK_EQ(cbor::lazy::copy(counted)->at_path<"$[h'01']", int>(), 7);
    CHECK_EQ(cbor::at_path<"$[h'02']", int>(std::string_view(counted)).error(), error::key_not_found);
}

// The typed forms that take a lazy refuse a lazy that holds no item, as the node forms do.
TEST_CASE("doc advanced path: a typed read of an empty lazy throws")
{
    CHECK_THROWS_AS(((void)(cbor::lazy{}).at_path<"$.a", int>()), std::logic_error);
    CHECK_THROWS_AS(((void)(cbor::lazy{}).at_path<"$.a", std::string_view>()), std::logic_error);
    CHECK_THROWS_AS((void)test::at_path<"$.a">(cbor::lazy{}), std::logic_error);
}

// The section "Tags on the way" shows that tags 24, 28, 29 and 55799 are passed, and that a tag the path does
// not know is passed to its content (RFC 8949 6.1), while a path that ends at the tag gives the node of the whole
// tagged item.
TEST_CASE("doc advanced path: tags on the way")
{
    CHECK_EQ(cbor::at_path<"$.a[1]", int>("\xa1\x61\x61\xd8\x18\x43\x82\x01\x02"s), 2);
    CHECK_EQ(cbor::at_path<"$[0].k", int>("\x82\xd8\x1c\xa1\x61k\x07\xd8\x1d\x00"s), 7);
    CHECK_EQ(cbor::at_path<"$[1]", int>("\x82\xd8\x1c\xa1\x61k\x07\xd8\x1d\x00"s), 0);
    CHECK_EQ(cbor::at_path<"$.a", int>("\xd9\xd9\xf7\xa1\x61\x61\x05"s), 5);
    std::string const other = preferred(M("v"s, tagged{1000, A(1, 2)}));
    CHECK_EQ(cbor::at_path<"$.v[1]", int>(other), 2);
    CHECK(*test::query("$.v[*]", *cbor::lazy::copy(other)) == A(1, 2));
    CHECK(*test::at_path("$.v", *cbor::lazy::copy(other)) == V(tagged{1000, A(1, 2)}));
}

// The section "Typed arrays" shows that an index reads one element, that a wildcard gives every element, and
// that a path that ends at the tag reads the whole array.
TEST_CASE("doc advanced path: a typed array")
{
    std::string const bytes = "\xa1\x61v\xd8\x45\x44\x07\x00\x08\x00"s;
    CHECK_EQ(cbor::at_path<"$.v[1]", int>(bytes), 8);
    CHECK_EQ(cbor::at_path<"$.v[1]", double>(bytes), 8.0);
    CHECK_EQ(cbor::at_path<"$.v[2]", int>(bytes).error(), error::index_out_of_bounds);
    CHECK(*test::query("$.v[*]", *cbor::lazy::copy(bytes)) == A(7, 8));
    auto const view = cbor::lazy::copy(bytes)->at_path<"$.v", cbor::typed_array>();
    REQUIRE(view.has_value());
    CHECK_EQ((*view)->bytes.size(), 4u);
}

// The section "Singular queries" shows the compile-time refusal and the run-time error.
TEST_CASE("doc advanced path: a singular query")
{
    CHECK((cbor::is_singular_query_v<"$.a[0]['b']">));
    CHECK((!cbor::is_singular_query_v<"$.a[*]">));
    CHECK((!cbor::is_singular_query_v<"$..a">));
    CHECK((cbor::is_singular_query_v<"$[h'01']">));
    CHECK((cbor::is_valid_path_v<"$..a">));
    CHECK((!cbor::is_valid_path_v<"$.a[">));
    CHECK_FALSE((advanced_typed_path_compiles<"$.a[*]", int>));
    CHECK_EQ(test::at_path("$.a[*]", *cbor::lazy::copy(preferred(M("a"s, A(1))))).error(), error::invalid_path);
}

// The section "Errors" shows each error that a path gives and the input that gives it.
TEST_CASE("doc advanced path: the errors")
{
    std::string const bytes = preferred(M("a"s, A(1, 2), "s"s, "x"s));
    auto const l = *cbor::lazy::copy(bytes);
    CHECK_EQ(test::at_path("$.a[", l).error(), error::invalid_path);
    CHECK_EQ(cbor::at_path<"$.b", int>(bytes).error(), error::key_not_found);
    CHECK_EQ(cbor::at_path<"$.a[2]", int>(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(cbor::at_path<"$.s[0]", int>(bytes).error(), error::not_indexable);
    CHECK_EQ(cbor::at_path<"$.s", int>(bytes).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$.a[1]", int>(bytes.substr(0, 5)).error(), error::too_little_data);
    CHECK_THROWS_AS((void)test::at_path("$", cbor::lazy{}), std::logic_error);
}

// The section "Limits" shows each limit as a path meets it, and that an item the path only skips does not count.
TEST_CASE("doc advanced path: the limits")
{
    std::string const deep = preferred(A(A(A(1))));
    {
        test::limits_guard const g{{.nesting_depth = 2}};
        CHECK_EQ(cbor::at_path<"$[0][0][0]", int>(deep).error(), error::nesting_depth_exceeded);
        CHECK_EQ(test::at_path("$", *cbor::lazy::copy(deep)).error(), error::nesting_depth_exceeded);
    }
    std::string const bytes = preferred(M("long"s, "xyz"s, "n"s, 1));
    {
        test::limits_guard const g{{.string_length = 2}};
        CHECK_EQ(cbor::at_path<"$.n", int>(bytes).error(), error::string_length_exceeded);
        std::string const skipped = preferred(M("a"s, "xyz"s, "n"s, 1));
        CHECK_EQ(cbor::at_path<"$.n", int>(skipped), 1);
        CHECK_EQ(cbor::at_path<"$.a", std::string_view>(std::make_shared<std::string const>(skipped)).error(),
                 error::string_length_exceeded);
    }
    {
        test::limits_guard const g{{.container_elements = 1}};
        CHECK_EQ(cbor::at_path<"$.n", int>(bytes).error(), error::container_elements_exceeded);
    }
    {
        test::limits_guard const g{{.input_bytes = 4}};
        CHECK_EQ(cbor::at_path<"$.n", int>(bytes).error(), error::input_bytes_exceeded);
        CHECK_EQ(cbor::lazy::copy(bytes).error(), error::input_bytes_exceeded);
    }
}

// The section "Threads" shows that a lazy is read from another thread, and that a tag 29 is a plain tag there too.
TEST_CASE("doc advanced path: a lazy in another thread")
{
    auto const l = *cbor::lazy::copy(preferred(A(1, 2)));
    std::int64_t read = 0;
    std::jthread([&read, &l] { read = *l.at_path<"$[1]", std::int64_t>(); }).join();
    CHECK_EQ(read, 2);
    auto const shared = *cbor::lazy::copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s);
    std::int64_t content = -1;
    std::jthread([&content, &shared] { content = *shared.at_path<"$[1]", std::int64_t>(); }).join();
    CHECK_EQ(content, 0);
}


// The section "The forms" says that a path that is not valid leaves no form to
// call, and section 1 says which wrong uses throw.
TEST_CASE("doc advanced path: the forms in detail")
{
    CHECK_FALSE(advanced_lazy_query_compiles<"$.a[">);
    CHECK_FALSE(advanced_lazy_path_compiles<"$.a[">);
    CHECK_FALSE((advanced_typed_path_compiles<"$.a[", int>));
    CHECK_THROWS_AS((void)test::query("$", cbor::lazy{}), std::logic_error);
    CHECK_THROWS_AS((void)test::query<"$">(cbor::lazy{}), std::logic_error);
    CHECK_THROWS_AS(((void)cbor::at_path<"$", std::string_view>(std::shared_ptr<std::string const>{})), std::logic_error);
}

// The section "Path syntax" gives the shorthands, the range of an index, the EDN literal that stands where RFC 9535
// has none, and the singular operand of a comparison.
TEST_CASE("doc advanced path: the syntax in detail")
{
    auto const sh = *cbor::lazy::copy(preferred(M("a"s, M("b"s, 1), "c"s, A(2))));
    CHECK(*test::query("$.*", sh) == A(M("b"s, 1), A(2)));
    CHECK(*test::query("$..*", sh) == A(M("b"s, 1), A(2), 1, 2));
    CHECK(*test::query("$..[0]", sh) == A(2));
    CHECK_EQ(test::query("$[?@.* == 1]", sh).error(), error::invalid_path);
    auto const array = *cbor::lazy::copy(preferred(A(1)));
    CHECK_EQ(test::at_path("$[9007199254740992]", array).error(), error::invalid_path);
    CHECK_EQ(test::at_path("$[01]", array).error(), error::invalid_path);
    CHECK_EQ(test::at_path("$[99999999999999999]", array).error(), error::not_indexable);
    CHECK(*test::at_path("$[99999999999999999]", *cbor::lazy::copy("\xa1\x1b\x01\x63\x45\x78\x5d\x89\xff\xff\x01"s)) == V(1));
    CHECK_EQ(test::at_path("$[-0]", array).error(), error::not_indexable);
    CHECK(*test::at_path("$[-0]", *cbor::lazy::copy(preferred(M(0, "a"s)))) == V("a"s));
    CHECK(*test::query("$[0:3:0]", array) == A());
    CHECK(*test::query("$[0:1]", *cbor::lazy::copy(preferred(M("a"s, 1)))) == A());
    CHECK(*test::query("$[?@ == 1.]", array) == A(1));
}

// The section "Comparisons" states how == and < treat each kind of value, and an empty side.
TEST_CASE("doc advanced path: comparisons")
{
    auto const eqs = *cbor::lazy::copy(preferred(A(A(1, 2), A(2, 1), M("a"s, 1), bytes{"\x01"}, tagged{1000, V(1)}, 1, 1.0, "x"s,
                                                 tagged{2, bytes{"\x01"}})));
    CHECK(*test::query("$[?@ == [1, 2]]", eqs) == A(A(1, 2)));
    CHECK(*test::query("$[?@ == {\"a\": 1}]", eqs) == A(M("a"s, 1)));
    CHECK(*test::query("$[?@ == h'01']", eqs) == A(bytes{"\x01"}));
    CHECK(*test::query("$[?@ == 1000(1)]", eqs) == A(tagged{1000, V(1)}));
    CHECK(*test::query("$[?@ == 1]", eqs) == A(1, 1.0));
    CHECK(*test::query("$[?@ < 2]", eqs) == A(1, 1.0));
    CHECK(*test::query("$[?@ < [3]]", eqs) == A());
    CHECK(*test::query("$[?@ >= 'x']", eqs) == A("x"s));
    auto const one = *cbor::lazy::copy(preferred(A(1)));
    CHECK(*test::query("$[?@.x == @.y]", one) == A(1));
    CHECK(*test::query("$[?@.x <= @.y]", one) == A(1));
    CHECK(*test::query("$[?@.x < @.y]", one) == A());
}

// The section "Functions" states that length counts the bytes of a text that are not UTF-8 continuation bytes, and
// that length of a byte string is Nothing, which equals only Nothing.
TEST_CASE("doc advanced path: length in detail")
{
    auto const l = *cbor::lazy::copy(preferred(A(bytes{"\x01\x02"}, "ab"s, "\xc3\xbc"s)));
    CHECK(*test::query("$[?length(@) == 2]", l) == A("ab"s));
    CHECK(*test::query("$[?length(@) == 1]", l) == A("\xc3\xbc"s));
    CHECK(*test::query("$[?length(@) == length(@.x)]", l) == A(bytes{"\x01\x02"}));
}

// The section "Keys that are not text" states how keys compare, and which pair a duplicate key gives.
TEST_CASE("doc advanced path: keys compare by value")
{
    std::string const keyed = "\xa4\x01\x61x\x21\x61y\x41\x01\x61z\x82\x01\x02\x61w"s;
    auto const l = *cbor::lazy::copy(keyed);
    CHECK(*test::at_path<"$[b64'AQ']">(l) == V("z"s));
    CHECK(*test::at_path<"$[h'01']">(*cbor::lazy::copy("\xa1\x58\x01\x01\x61q"s)) == V("q"s));
    CHECK(*test::at_path<"$[1]">(*cbor::lazy::copy("\xa1\x18\x01\x61q"s)) == V("q"s));
    CHECK_EQ(test::at_path<"$[1.0]">(l).error(), error::key_not_found);
    CHECK_EQ(test::at_path("$[1.0]", l).error(), error::key_not_found);
    std::string const odd = preferred(M(1.5, 1, simple{21}, 2, tagged{1000, "x"s}, 3, M("a"s, 1), 4));
    auto const o = *cbor::lazy::copy(odd);
    CHECK(*test::at_path<"$[1.5]">(o) == V(1));
    CHECK(*test::at_path<"$[true]">(o) == V(2));
    CHECK(*test::at_path<"$[1000(\"x\")]">(o) == V(3));
    CHECK(*test::at_path<"$[{\"a\": 1}]">(o) == V(4));
    CHECK(*test::at_path("$[{\"a\": 1}]", o) == V(4));
    std::string const dup = "\xa2\x61\x61\x01\x61\x61\x02"s;
    auto const d = *cbor::lazy::copy(dup);
    CHECK_EQ(cbor::at_path<"$.a", int>(dup), 1);
    CHECK(*test::at_path<"$.a">(d) == V(1));
    CHECK(*test::at_path("$.a", d) == V(1));
    CHECK(*test::query("$.*", d) == A(1, 2));
    CHECK(*test::query("$..*", d) == A(1, 2));
    CHECK(*test::query("$[?@ > 0]", d) == A(1, 2));
}

// The section "Tags on the way" states that a key behind tag 55799 is matched and a key behind tag 28 is a tag,
// that tag 55799 inside tag 24 is passed, and the errors of a tag whose content is wrong.
TEST_CASE("doc advanced path: tags in detail")
{
    CHECK_EQ(cbor::at_path<"$.k", int>("\xa1\xd8\x1c\x61k\x01"s).error(), error::key_not_found);
    CHECK_EQ(cbor::at_path<"$.k", int>("\xa1\xd9\xd9\xf7\x61k\x01"s), 1);
    CHECK_EQ(test::at_path<"$.k">(*cbor::lazy::copy("\xa1\xd8\x1c\x61k\x01"s)).error(), error::key_not_found);
    std::string const inside = "\xa1\x61\x61\xd8\x18\x46\xd9\xd9\xf7\x82\x01\x02"s;
    CHECK_EQ(cbor::at_path<"$.a[1]", int>(inside), 2);
    CHECK(*test::at_path("$.a[1]", *cbor::lazy::copy(inside)) == V(2));
    CHECK_EQ(cbor::at_path<"$.a", int>("\xa1\x61\x61\xd8\x18\x05"s).error(), error::inadmissible_type_for_tag_content);
    CHECK(cbor::lazy::copy("\xd8\x1c\x81\xd8\x1d\x00"s)->at_path("$[0]").has_value());
    std::string const many = "\x82\xd8\x1c\x88\x00\x00\x00\x00\x00\x00\x00\x00\xd8\x1d\x00"s;
    CHECK(*test::query("$[*][*]", *cbor::lazy::copy(many)) == A(0, 0, 0, 0, 0, 0, 0, 0));
}

// The section "What the form over bytes checks" states that a read stops at the target, so bytes after it are not read.
TEST_CASE("doc advanced path: bytes after the target are not read")
{
    CHECK_EQ(cbor::at_path<"$[0]", int>("\x82\x01"s).error(), error::too_little_data);
    CHECK_EQ(cbor::at_path<"$[0]", int>("\x82\x01\xff"s), 1);
    CHECK_EQ(cbor::at_path<"$", int>("\x01\x02"s), 1);
    CHECK(cbor::lazy::copy("\x82\x01"s).has_value());
    CHECK(cbor::lazy::copy("\x01\x02"s).has_value());
    CHECK_EQ(cbor::at_path<"$[1]", int>("\x82\x01"s).error(), error::too_little_data);
}

// The section "Limits" states the defaults, the nesting of a path itself, that input_bytes is not checked again on
// a lazy that exists, and that cbor::limits is one object for every thread.
TEST_CASE("doc advanced path: limits in detail")
{
    CHECK((cbor::limit_values{}.string_length == SIZE_MAX));
    CHECK((cbor::limit_values{}.container_elements == SIZE_MAX));
    CHECK((cbor::limit_values{}.input_bytes == SIZE_MAX));
    CHECK((cbor::limit_values{}.nesting_depth == CBOR_NESTING_DEPTH_DEFAULT));
    auto const one = *cbor::lazy::copy(preferred(A(1)));
    auto const big = *cbor::lazy::copy(preferred(M("pad"s, "0123456789"s, "a"s, A(1, 2))));
    auto const a = *big.at("a");
    {
        test::limits_guard const g{{.nesting_depth = 2}};
        CHECK_EQ(test::query("$[?@ == [[[1]]]]", one).error(), error::nesting_depth_exceeded);
        CHECK_EQ(test::query("$[?@[?@[?@]]]", one).error(), error::nesting_depth_exceeded);
        CHECK_EQ(test::query("$[?(((@)))]", one).error(), error::nesting_depth_exceeded);
        CHECK_EQ(test::query("$[?length(value(value(@))) == 1]", one).error(), error::nesting_depth_exceeded);
    }
    {
        test::limits_guard const g{{.input_bytes = 4}};
        CHECK_EQ(a.at_path<"$[1]", int>(), 2);
        CHECK(*test::query("$[*]", a) == A(1, 2));
    }
    {
        test::limits_guard const g{{.string_length = 2}};
        std::size_t seen = 0;
        std::jthread([&seen] { seen = cbor::limits.string_length; }).join();
        CHECK_EQ(seen, 2u);
    }
}

// The section "Path syntax" states that an index counts from 0, because a task that names "element 2" leaves the base
// open.
TEST_CASE("doc advanced path: an index counts from 0")
{
    std::string const bytes = "\x83\x0a\x0b\x0c"s;
    CHECK_EQ(cbor::at_path<"$[0]", int>(bytes).value(), 10);
    CHECK_EQ(cbor::at_path<"$[-1]", int>(bytes).value(), 12);
}
