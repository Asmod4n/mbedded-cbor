#include "binding.hpp"

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

// Each test in this file is one example of doc/path.md. The tests exist so that every example in the document
// compiles and gives the result that the document states.

namespace
{

template <cbor::fixed_string Path, class T>
concept doc_typed_path_compiles = requires(std::string_view const s) { cbor::at_path<Path, T>(s); };

template <cbor::fixed_string Path>
concept doc_binding_path_compiles = requires(test_binding &b, cbor::lazy const &l) { cbor::at_path<Path>(b, l); };

} // namespace

// The section "A scalar from bytes" shows the form that reads one number from bytes the caller holds.
TEST_CASE("doc path: a scalar from bytes")
{
    std::string const bytes = encoded(M("user"s, M("id"s, 42, "score"s, 2.5, "admin"s, simple{21})));
    CHECK_EQ(cbor::at_path<"$.user.id", std::int64_t>(bytes), 42);
    CHECK_EQ(cbor::at_path<"$.user.score", double>(bytes), 2.5);
    CHECK_EQ(cbor::at_path<"$.user.admin", bool>(bytes), true);
}

// The section "A string, a byte string or a typed array" shows that a view needs an owner, and that the view
// points into the bytes of that owner.
TEST_CASE("doc path: a view with its owner")
{
    auto const owner = std::make_shared<std::string const>(encoded(M("name"s, "ann"s, "key"s, bytes{"\x01\x02"})));
    auto const name = cbor::at_path<"$.name", std::string_view>(owner, *owner);
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "ann"sv);
    auto const key = cbor::at_path<"$.key", std::span<std::byte const>>(owner, *owner);
    REQUIRE(key.has_value());
    CHECK_EQ((*key)->size(), 2u);
    std::string const typed = "\xa1\x61v\xd8\x45\x44\x07\x00\x08\x00"s;
    auto const held = std::make_shared<std::string const>(typed);
    auto const array = cbor::at_path<"$.v", cbor::typed_array>(held, *held);
    REQUIRE(array.has_value());
    CHECK_EQ((*array)->tag, 69u);
    CHECK_EQ((*array)->bytes.size(), 4u);
    CHECK_FALSE(([]<class S>(S &&) {
        return requires(S &&s) { cbor::at_path<"$.name", std::string_view>(std::forward<S>(s)); };
    }(std::string_view(*owner))));
}

// The section "A path from a lazy" shows that a lazy is a start point for every typed form.
TEST_CASE("doc path: a typed read from a lazy")
{
    std::string const bytes = encoded(M("cars"s, A(M("hp"s, 5), M("hp"s, 7, "make"s, "kia"s))));
    auto const cars = cbor::lazy::from(bytes)->at("cars");
    REQUIRE(cars.has_value());
    CHECK_EQ(cbor::at_path<"$[1].hp", std::int64_t>(*cars), 7);
    auto const make = cbor::at_path<"$[1].make", std::string_view>(*cars);
    REQUIRE(make.has_value());
    CHECK_EQ(**make, "kia"sv);
}

// The section "Any value into your own type" shows at_path with a binding, with the path at compile time and at
// run time, and cbor::query for a nodelist.
TEST_CASE("doc path: a binding at compile time and at run time")
{
    std::string const bytes = encoded(M("a"s, A(1, 2, 3)));
    auto const l = *cbor::lazy::from(bytes);
    test_binding binding;
    CHECK(*cbor::at_path<"$.a">(binding, l) == A(1, 2, 3));
    std::string const path = "$.a[2]";
    CHECK(*cbor::at_path(binding, path, l) == V(3));
    CHECK(*cbor::query<"$.a[*]">(binding, l) == A(1, 2, 3));
    CHECK(*cbor::query(binding, "$.a[?@ > 1]", l) == A(2, 3));
    CHECK(*cbor::query(binding, "$.b", l) == A());
}

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1700)]] doc_engine {
    std::uint16_t hp;
    std::uint32_t cc;
};

struct [[=cbor::tag(1701)]] doc_car {
    std::string make;
    doc_engine motor;
    std::vector<doc_engine> spares;
};

} // namespace

// The section "A path over a schema message" shows schema<T>::at_path, the argument for a run-time index and the
// accessor that gives a text as a view.
TEST_CASE("doc path: schema at_path")
{
    auto const bytes = cbor::schema<doc_car>::encode(doc_car{"kia", {120, 1600}, {{90, 1200}, {75, 1000}}});
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::schema<doc_car>::at_path<"$.motor.cc">(*bytes), 1600u);
    CHECK_EQ(cbor::schema<doc_car>::at_path<"$.spares[1].hp">(*bytes), 75u);
    CHECK_EQ(cbor::schema<doc_car>::at_path<"$.spares[].hp">(*bytes, 0uz), 90u);
    CHECK_EQ(cbor::schema<doc_car>::at_path<"$.spares[].hp">(*bytes, 2uz).error(), error::index_out_of_bounds);
    auto const owner = std::make_shared<std::string const>(*bytes);
    auto const root = cbor::schema<doc_car>::path(owner, *owner);
    REQUIRE(root.has_value());
    CHECK_EQ(*root->at_path<"$.make">(), "kia"sv);
}

struct [[=cbor::tag(1702)]] doc_log {
    std::vector<std::uint16_t> readings;
};

// The section "A path over a schema message" states which results the static form refuses, that the accessor reads
// a path relative to its node with @ and only as an lvalue, that path(std::string_view) copies the bytes, and that a
// typed array gives one element.
TEST_CASE("doc path: schema at_path in detail")
{
    auto const bytes = *cbor::schema<doc_car>::encode(doc_car{"kia", {120, 1600}, {{90, 1200}, {75, 1000}}});
    CHECK_FALSE(([]<class S>(S const &) { return requires(std::string_view v) { S::template at_path<"$.make">(v); }; }(cbor::schema<doc_car>{})));
    CHECK_FALSE(([]<class S>(S const &) { return requires(std::string_view v) { S::template at_path<"$.spares">(v); }; }(cbor::schema<doc_car>{})));
    auto const owner = std::make_shared<std::string const>(bytes);
    auto root = *cbor::schema<doc_car>::path(owner, *owner);
    auto const motor = root.at_path<"$.motor">();
    REQUIRE(motor.has_value());
    CHECK_EQ(*motor->at_path<"@.cc">(), 1600u);
    CHECK_FALSE(([]<class R>(R &r) { return requires { std::move(r).template at_path<"$.make">(); }; }(root)));
    std::string temp = bytes;
    auto const copied = cbor::schema<doc_car>::path(std::string_view(temp));
    temp.assign(temp.size(), '\0');
    REQUIRE(copied.has_value());
    CHECK_EQ(*copied->at_path<"$.make">(), "kia"sv);
    auto const log = *cbor::schema<doc_log>::encode(doc_log{{7, 8, 9}});
    CHECK_EQ(cbor::schema<doc_log>::at_path<"$.readings[1]">(log), 8u);
    CHECK_EQ(cbor::schema<doc_log>::at_path<"$.readings[]">(log, 2uz), 9u);
    CHECK_EQ(cbor::schema<doc_log>::at_path<"$.readings[]">(log, 3uz).error(), error::index_out_of_bounds);
    auto const held = cbor::schema<doc_log>::path(log);
    REQUIRE(held.has_value());
    auto const readings = held->at_path<"$.readings">();
    REQUIRE(readings.has_value());
    CHECK_EQ(*readings->at_path<"@[1]">(), 8u);
    CHECK_THROWS_AS((void)cbor::schema<doc_car>::path(std::shared_ptr<void const>{}, bytes), std::logic_error);
}

// A list accessor taken from a temporary root accessor read freed bytes,
// because only the root held the owner of the bytes. Every accessor now
// holds the owner, so the list outlives the temporary it came from.
TEST_CASE("doc path: a list accessor keeps the bytes alive")
{
    auto const log = *cbor::schema<doc_log>::encode(doc_log{{7, 8, 9}});
    auto const readings = cbor::schema<doc_log>::path(log)->at_path<"$.readings">();
    REQUIRE(readings.has_value());
    CHECK_EQ(readings->size(), 3u);
    CHECK_EQ(*readings->at_path<"@[2]">(), 9u);
}

#endif

// The section "Which types a typed read gives" shows the allowed types, the conversions that are refused and
// the types that do not compile.
TEST_CASE("doc path: the types of a typed read")
{
    CHECK_EQ(cbor::at_path<"$", std::uint8_t>(encoded(V(255))), std::uint8_t{255});
    CHECK_EQ(cbor::at_path<"$", std::uint8_t>(encoded(V(256))).error(), error::number_out_of_range);
    CHECK_EQ(cbor::at_path<"$", unsigned>(encoded(V(-1))).error(), error::number_out_of_range);
    CHECK_EQ(cbor::at_path<"$", double>(encoded(V(1))).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$", std::int64_t>(encoded(V(1.0))).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$", std::uint64_t>("\xc2\x42\x01\x00"s), 256u);
    CHECK_EQ(cbor::at_path<"$", std::nullptr_t>(encoded(V(simple{22}))), nullptr);
    CHECK_EQ(cbor::at_path<"$", cbor::simple_value>("\xf8\x63"s), cbor::simple_value{99});
    CHECK_EQ(cbor::at_path<"$", cbor::simple_value>("\xf5"s), cbor::simple_value{21});
    CHECK_EQ(cbor::at_path<"$", cbor::simple_value>("\xf6"s), cbor::simple_value{22});
    CHECK_EQ(cbor::at_path<"$", char>(encoded(V(65))), 'A');
    CHECK_EQ(cbor::at_path<"$", int>("\xc3\x41\x01"s), -2);
    CHECK_EQ(cbor::at_path<"$", int>("\xc1\x05"s).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$", std::string_view>(*cbor::lazy::from("\xd8\x20\x61x"s)).error(), error::incorrect_type);
    CHECK_FALSE((doc_typed_path_compiles<"$", float>));
    CHECK_FALSE((doc_typed_path_compiles<"$", std::string>));
    CHECK_FALSE((doc_typed_path_compiles<"$", std::string_view>));
}

// The section "Path syntax" shows each selector and segment of RFC 9535 with its result.
TEST_CASE("doc path: the selectors and segments")
{
    value const doc = M("store"s, M("book"s, A(M("title"s, "A"s, "price"s, 8), M("title"s, "B"s, "price"s, 12),
                                              M("title"s, "C"s, "price"s, 9, "isbn"s, "x"s)),
                                    "bicycle"s, M("price"s, 20)));
    auto const l = *cbor::lazy::from(encoded(doc));
    test_binding b;
    CHECK(*cbor::query(b, "$", l) == A(doc));
    CHECK(*cbor::query(b, "$.store.bicycle.price", l) == A(20));
    CHECK(*cbor::query(b, "$['store']['bicycle']['price']", l) == A(20));
    CHECK(*cbor::query(b, "$.store.book[0].title", l) == A("A"s));
    CHECK(*cbor::query(b, "$.store.book[-1].title", l) == A("C"s));
    CHECK(*cbor::query(b, "$.store.book[*].price", l) == A(8, 12, 9));
    CHECK(*cbor::query(b, "$.store.book[0, 2].title", l) == A("A"s, "C"s));
    CHECK(*cbor::query(b, "$.store.book[0:2].title", l) == A("A"s, "B"s));
    CHECK(*cbor::query(b, "$.store.book[::-1].title", l) == A("C"s, "B"s, "A"s));
    CHECK(*cbor::query(b, "$..price", l) == A(8, 12, 9, 20));
    CHECK(*cbor::query(b, "$.store.book[?@.price < 10].title", l) == A("A"s, "C"s));
    CHECK(*cbor::query(b, "$.store.book[?@.isbn].title", l) == A("C"s));
    CHECK(*cbor::query(b, "$.store.book[?!@.isbn && @.price > 10].title", l) == A("B"s));
    CHECK(*cbor::query(b, "$.store.book[?@.price == 8 || @.title == 'B'].title", l) == A("A"s, "B"s));
    CHECK(*cbor::query(b, "$.store.book[?@.price > $.store.bicycle.price].title", l) == A());
}

// The section "Functions" shows length, count and value, and that match and search are refused.
TEST_CASE("doc path: the functions")
{
    auto const l = *cbor::lazy::from(encoded(A(M("t"s, "abc"s), M("t"s, A(1, 2)), M("t"s, 5))));
    test_binding b;
    CHECK(*cbor::query(b, "$[?length(@.t) == 3].t", l) == A("abc"s));
    CHECK(*cbor::query(b, "$[?count(@.t[*]) == 2].t", l) == A(A(1, 2)));
    CHECK(*cbor::query(b, "$[?value(@..t) == 5].t", l) == A(5));
    CHECK_EQ(cbor::query(b, "$[?match(@.t, 'a.c')]", l).error(), error::invalid_path);
    CHECK_EQ(cbor::query(b, "$[?search(@.t, 'b')]", l).error(), error::invalid_path);
    CHECK_FALSE(cbor::is_valid_path_v<"$[?match(@.t, 'a.c')]">);
}

// The section "Keys that are not text" shows integer keys, a negative integer key and EDN literals as keys, in a
// compile-time path and in a run-time path alike, and a typed read through a key that is not text.
TEST_CASE("doc path: keys that are not text")
{
    std::string const keyed = "\xa4\x01\x61x\x21\x61y\x41\x01\x61z\x82\x01\x02\x61w"s;
    auto const l = *cbor::lazy::from(keyed);
    test_binding b;
    CHECK(*cbor::at_path<"$[1]">(b, l) == V("x"s));
    CHECK(*cbor::at_path<"$[-2]">(b, l) == V("y"s));
    CHECK(*cbor::at_path<"$[h'01']">(b, l) == V("z"s));
    CHECK(*cbor::at_path<"$[[1, 2]]">(b, l) == V("w"s));
    CHECK(*cbor::at_path(b, "$[1]", l) == V("x"s));
    CHECK(*cbor::at_path(b, "$[h'01']", l) == V("z"s));
    CHECK(*cbor::at_path(b, "$[[1, 2]]", l) == V("w"s));
    CHECK(*cbor::query<"$[?@ == h'01']">(b, *cbor::lazy::from(encoded(A(bytes{"\x01"}, 1)))) == A(bytes{"\x01"}));
    CHECK(*cbor::query(b, "$[?@ == h'01']", *cbor::lazy::from(encoded(A(bytes{"\x01"}, 1)))) == A(bytes{"\x01"}));
    CHECK(doc_binding_path_compiles<"$[h'01']">);
    CHECK((doc_typed_path_compiles<"$[h'01']", int>));
    auto const z = cbor::at_path<"$[h'01']", std::string_view>(l);
    REQUIRE(z.has_value());
    CHECK_EQ(**z, "z"sv);
    std::string const counted = "\xa1\x41\x01\x07"s;
    CHECK_EQ(cbor::at_path<"$[h'01']", int>(std::string_view(counted)), 7);
    CHECK_EQ(cbor::at_path<"$[h'01']", int>(*cbor::lazy::from(counted)), 7);
    CHECK_EQ(cbor::at_path<"$[h'02']", int>(std::string_view(counted)).error(), error::key_not_found);
}

// The typed forms that take a lazy refuse a lazy that holds no item, as the binding forms do.
TEST_CASE("doc path: a typed read of an empty lazy throws")
{
    CHECK_THROWS_AS(((void)cbor::at_path<"$.a", int>(cbor::lazy{})), std::logic_error);
    CHECK_THROWS_AS(((void)cbor::at_path<"$.a", std::string_view>(cbor::lazy{})), std::logic_error);
    test_binding b;
    CHECK_THROWS_AS((void)cbor::at_path<"$.a">(b, cbor::lazy{}), std::logic_error);
}

// The section "Tags on the way" shows that tags 24, 28, 29 and 55799 are passed, and that any other tag stops
// the walk.
TEST_CASE("doc path: tags on the way")
{
    CHECK_EQ(cbor::at_path<"$.a[1]", int>("\xa1\x61\x61\xd8\x18\x43\x82\x01\x02"s), 2);
    CHECK_EQ(cbor::at_path<"$[1].k", int>("\x82\xd8\x1c\xa1\x61k\x07\xd8\x1d\x00"s), 7);
    CHECK_EQ(cbor::at_path<"$.a", int>("\xd9\xd9\xf7\xa1\x61\x61\x05"s), 5);
    std::string const other = encoded(M("v"s, tagged{1000, A(1, 2)}));
    CHECK_EQ(cbor::at_path<"$.v[1]", int>(other).error(), error::not_indexable);
    test_binding b;
    CHECK(*cbor::query(b, "$.v[*]", *cbor::lazy::from(other)) == A());
    CHECK(*cbor::at_path(b, "$.v", *cbor::lazy::from(other)) == V(tagged{1000, A(1, 2)}));
}

// The section "Typed arrays" shows that a typed array is one value: a path ends at it and does not enter it.
TEST_CASE("doc path: a typed array is one value")
{
    std::string const bytes = "\xa1\x61v\xd8\x45\x44\x07\x00\x08\x00"s;
    CHECK_EQ(cbor::at_path<"$.v[1]", int>(bytes).error(), error::not_indexable);
    auto const view = cbor::at_path<"$.v", cbor::typed_array>(*cbor::lazy::from(bytes));
    REQUIRE(view.has_value());
    CHECK_EQ((*view)->bytes.size(), 4u);
}

// The section "Singular queries" shows the compile-time refusal and the run-time error.
TEST_CASE("doc path: a singular query")
{
    static_assert(cbor::is_singular_query_v<"$.a[0]['b']">);
    static_assert(!cbor::is_singular_query_v<"$.a[*]">);
    static_assert(!cbor::is_singular_query_v<"$..a">);
    static_assert(cbor::is_singular_query_v<"$[h'01']">);
    static_assert(cbor::is_valid_path_v<"$..a">);
    static_assert(!cbor::is_valid_path_v<"$.a[">);
    CHECK_FALSE((doc_typed_path_compiles<"$.a[*]", int>));
    test_binding b;
    CHECK_EQ(cbor::at_path(b, "$.a[*]", *cbor::lazy::from(encoded(M("a"s, A(1))))).error(), error::invalid_path);
}

// The section "Errors" shows each error that a path gives and the input that gives it.
TEST_CASE("doc path: the errors")
{
    std::string const bytes = encoded(M("a"s, A(1, 2), "s"s, "x"s));
    test_binding b;
    auto const l = *cbor::lazy::from(bytes);
    CHECK_EQ(cbor::at_path(b, "$.a[", l).error(), error::invalid_path);
    CHECK_EQ(cbor::at_path<"$.b", int>(bytes).error(), error::key_not_found);
    CHECK_EQ(cbor::at_path<"$.a[2]", int>(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(cbor::at_path<"$.s[0]", int>(bytes).error(), error::not_indexable);
    CHECK_EQ(cbor::at_path<"$.s", int>(bytes).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$.a[1]", int>(bytes.substr(0, 5)).error(), error::too_little_data);
    CHECK_EQ(cbor::at_path<"$.a", int>("\xa1\x61\x61\xd8\x1d\x05"s).error(), error::sharedref_index_not_marked);
    CHECK_THROWS_AS((void)cbor::at_path(b, "$", cbor::lazy{}), std::logic_error);
}

// The section "Limits" shows each limit as a path meets it, also in an item that the path only skips.
TEST_CASE("doc path: the limits")
{
    test_binding b;
    std::string const deep = encoded(A(A(A(1))));
    {
        test::limits_guard const g{{.nesting_depth = 2}};
        CHECK_EQ(cbor::at_path<"$[0][0][0]", int>(deep).error(), error::nesting_depth_exceeded);
        CHECK_EQ(cbor::at_path(b, "$", *cbor::lazy::from(deep)).error(), error::nesting_depth_exceeded);
    }
    std::string const bytes = encoded(M("long"s, "xyz"s, "n"s, 1));
    {
        test::limits_guard const g{{.string_length = 2}};
        CHECK_EQ(cbor::at_path<"$.n", int>(bytes).error(), error::string_length_exceeded);
    }
    {
        test::limits_guard const g{{.container_elements = 1}};
        CHECK_EQ(cbor::at_path<"$.n", int>(bytes).error(), error::container_elements_exceeded);
    }
    {
        test::limits_guard const g{{.input_bytes = 4}};
        CHECK_EQ(cbor::at_path<"$.n", int>(bytes).error(), error::input_bytes_exceeded);
        CHECK_EQ(cbor::lazy::from(bytes).error(), error::input_bytes_exceeded);
    }
}

// The section "Threads" shows that a lazy is bound to one thread and that cbor::transfer hands it on.
TEST_CASE("doc path: a lazy in another thread")
{
    static_assert(cbor::is_thread_bound_v<cbor::lazy>);
    static_assert(cbor::is_thread_bound_v<cbor::owning_ref<std::string_view>>);
    std::int64_t read = 0;
    std::jthread([&read](cbor::sendable<cbor::lazy> const &v) { read = *cbor::at_path<"$[1]", std::int64_t>(v.value); },
                 cbor::transfer(*cbor::lazy::from(encoded(A(1, 2)))))
        .join();
    CHECK_EQ(read, 2);
}


// The section "Forms" says that a binding is taken as an lvalue and that a path that is not valid leaves no form to
// call, and section 1 says which wrong uses throw.
TEST_CASE("doc path: the forms in detail")
{
    test_binding b;
    CHECK_FALSE(([]<class B>(B &&) { return requires(cbor::lazy const &l) { cbor::at_path(B{}, "$", l); }; }(test_binding{})));
    CHECK_FALSE(([]<class B>(B &bb) { return requires(cbor::lazy const &l) { cbor::query<"$.a[">(bb, l); }; }(b)));
    CHECK_FALSE(([]<class B>(B &bb) { return requires(cbor::lazy const &l) { cbor::at_path<"$.a[">(bb, l); }; }(b)));
    CHECK_FALSE((doc_typed_path_compiles<"$.a[", int>));
    CHECK_THROWS_AS((void)cbor::query(b, "$", cbor::lazy{}), std::logic_error);
    CHECK_THROWS_AS((void)cbor::query<"$">(b, cbor::lazy{}), std::logic_error);
    CHECK_THROWS_AS(((void)cbor::at_path<"$", std::string_view>(std::shared_ptr<void const>{}, "\x61x"sv)), std::logic_error);
}

// The section "Path syntax" gives the shorthands, the range of an index, the EDN literal that stands where RFC 9535
// has none, and the singular operand of a comparison.
TEST_CASE("doc path: the syntax in detail")
{
    test_binding b;
    auto const sh = *cbor::lazy::from(encoded(M("a"s, M("b"s, 1), "c"s, A(2))));
    CHECK(*cbor::query(b, "$.*", sh) == A(M("b"s, 1), A(2)));
    CHECK(*cbor::query(b, "$..*", sh) == A(M("b"s, 1), A(2), 1, 2));
    CHECK(*cbor::query(b, "$..[0]", sh) == A(2));
    CHECK_EQ(cbor::query(b, "$[?@.* == 1]", sh).error(), error::invalid_path);
    auto const array = *cbor::lazy::from(encoded(A(1)));
    CHECK_EQ(cbor::at_path(b, "$[9007199254740992]", array).error(), error::invalid_path);
    CHECK_EQ(cbor::at_path(b, "$[01]", array).error(), error::invalid_path);
    CHECK_EQ(cbor::at_path(b, "$[99999999999999999]", array).error(), error::not_indexable);
    CHECK(*cbor::at_path(b, "$[99999999999999999]", *cbor::lazy::from("\xa1\x1b\x01\x63\x45\x78\x5d\x89\xff\xff\x01"s)) == V(1));
    CHECK_EQ(cbor::at_path(b, "$[-0]", array).error(), error::not_indexable);
    CHECK(*cbor::at_path(b, "$[-0]", *cbor::lazy::from(encoded(M(0, "a"s)))) == V("a"s));
    CHECK(*cbor::query(b, "$[0:3:0]", array) == A());
    CHECK(*cbor::query(b, "$[0:1]", *cbor::lazy::from(encoded(M("a"s, 1)))) == A());
    CHECK(*cbor::query(b, "$[?@ == 1.]", array) == A(1));
}

// The section "Comparisons" states how == and < treat each kind of value, and an empty side.
TEST_CASE("doc path: comparisons")
{
    test_binding b;
    auto const eqs = *cbor::lazy::from(encoded(A(A(1, 2), A(2, 1), M("a"s, 1), bytes{"\x01"}, tagged{1000, V(1)}, 1, 1.0, "x"s,
                                                 tagged{2, bytes{"\x01"}})));
    CHECK(*cbor::query(b, "$[?@ == [1, 2]]", eqs) == A(A(1, 2)));
    CHECK(*cbor::query(b, "$[?@ == {\"a\": 1}]", eqs) == A(M("a"s, 1)));
    CHECK(*cbor::query(b, "$[?@ == h'01']", eqs) == A(bytes{"\x01"}));
    CHECK(*cbor::query(b, "$[?@ == 1000(1)]", eqs) == A(tagged{1000, V(1)}));
    CHECK(*cbor::query(b, "$[?@ == 1]", eqs) == A(1, 1.0));
    CHECK(*cbor::query(b, "$[?@ < 2]", eqs) == A(1, 1.0));
    CHECK(*cbor::query(b, "$[?@ < [3]]", eqs) == A());
    CHECK(*cbor::query(b, "$[?@ >= 'x']", eqs) == A("x"s));
    auto const one = *cbor::lazy::from(encoded(A(1)));
    CHECK(*cbor::query(b, "$[?@.x == @.y]", one) == A(1));
    CHECK(*cbor::query(b, "$[?@.x <= @.y]", one) == A(1));
    CHECK(*cbor::query(b, "$[?@.x < @.y]", one) == A());
}

// The section "Functions" states that length counts the bytes of a text that are not UTF-8 continuation bytes, and
// that length of a byte string is Nothing, which equals only Nothing.
TEST_CASE("doc path: length in detail")
{
    test_binding b;
    auto const l = *cbor::lazy::from(encoded(A(bytes{"\x01\x02"}, "ab"s, "\xc3\xbc"s)));
    CHECK(*cbor::query(b, "$[?length(@) == 2]", l) == A("ab"s));
    CHECK(*cbor::query(b, "$[?length(@) == 1]", l) == A("\xc3\xbc"s));
    CHECK(*cbor::query(b, "$[?length(@) == length(@.x)]", l) == A(bytes{"\x01\x02"}));
}

// The section "Keys that are not text" states how keys compare, and which pair a duplicate key gives.
TEST_CASE("doc path: keys compare by value")
{
    test_binding b;
    std::string const keyed = "\xa4\x01\x61x\x21\x61y\x41\x01\x61z\x82\x01\x02\x61w"s;
    auto const l = *cbor::lazy::from(keyed);
    CHECK(*cbor::at_path<"$[b64'AQ']">(b, l) == V("z"s));
    CHECK(*cbor::at_path<"$[h'01']">(b, *cbor::lazy::from("\xa1\x58\x01\x01\x61q"s)) == V("q"s));
    CHECK(*cbor::at_path<"$[1]">(b, *cbor::lazy::from("\xa1\x18\x01\x61q"s)) == V("q"s));
    CHECK_EQ(cbor::at_path<"$[1.0]">(b, l).error(), error::key_not_found);
    CHECK_EQ(cbor::at_path(b, "$[1.0]", l).error(), error::key_not_found);
    std::string const odd = encoded(M(1.5, 1, simple{21}, 2, tagged{1000, "x"s}, 3, M("a"s, 1), 4));
    auto const o = *cbor::lazy::from(odd);
    CHECK(*cbor::at_path<"$[1.5]">(b, o) == V(1));
    CHECK(*cbor::at_path<"$[true]">(b, o) == V(2));
    CHECK(*cbor::at_path<"$[1000(\"x\")]">(b, o) == V(3));
    CHECK(*cbor::at_path<"$[{\"a\": 1}]">(b, o) == V(4));
    CHECK(*cbor::at_path(b, "$[{\"a\": 1}]", o) == V(4));
    std::string const dup = "\xa2\x61\x61\x01\x61\x61\x02"s;
    auto const d = *cbor::lazy::from(dup);
    CHECK_EQ(cbor::at_path<"$.a", int>(dup), 1);
    CHECK(*cbor::at_path<"$.a">(b, d) == V(1));
    CHECK(*cbor::at_path(b, "$.a", d) == V(1));
    CHECK(*cbor::query(b, "$.*", d) == A(1, 2));
    CHECK(*cbor::query(b, "$..*", d) == A(1, 2));
    CHECK(*cbor::query(b, "$[?@ > 0]", d) == A(1, 2));
}

// The section "Tags on the way" states that a key behind tag 28 or 55799 is matched, that tag 55799 inside tag 24
// is passed, and the errors of a tag whose content is wrong.
TEST_CASE("doc path: tags in detail")
{
    test_binding b;
    CHECK_EQ(cbor::at_path<"$.k", int>("\xa1\xd8\x1c\x61k\x01"s), 1);
    CHECK_EQ(cbor::at_path<"$.k", int>("\xa1\xd9\xd9\xf7\x61k\x01"s), 1);
    CHECK(*cbor::at_path<"$.k">(b, *cbor::lazy::from("\xa1\xd8\x1c\x61k\x01"s)) == V(1));
    std::string const inside = "\xa1\x61\x61\xd8\x18\x46\xd9\xd9\xf7\x82\x01\x02"s;
    CHECK_EQ(cbor::at_path<"$.a[1]", int>(inside), 2);
    CHECK(*cbor::at_path(b, "$.a[1]", *cbor::lazy::from(inside)) == V(2));
    CHECK_EQ(cbor::at_path<"$.a", int>("\xa1\x61\x61\xd8\x18\x05"s).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(cbor::at_path(b, "$", *cbor::lazy::from("\xd8\x1c\x81\xd8\x1d\x00"s)).error(), error::sharedref_not_complete);
    std::string const many = "\x82\xd8\x1c\x88\x00\x00\x00\x00\x00\x00\x00\x00\xd8\x1d\x00"s;
    CHECK_EQ(cbor::query(b, "$[*][*]", *cbor::lazy::from(many)).error(), error::nodelist_too_long);
}

// The section "What the bytes form checks" states that a read stops at the target, so bytes after it are not read.
TEST_CASE("doc path: bytes after the target are not read")
{
    CHECK_EQ(cbor::at_path<"$[0]", int>("\x82\x01"s), 1);
    CHECK_EQ(cbor::at_path<"$[0]", int>("\x82\x01\xff"s), 1);
    CHECK_EQ(cbor::at_path<"$", int>("\x01\x02"s), 1);
    CHECK(cbor::lazy::from("\x82\x01"s).has_value());
    CHECK(cbor::lazy::from("\x01\x02"s).has_value());
    CHECK_EQ(cbor::at_path<"$[1]", int>("\x82\x01"s).error(), error::too_little_data);
}

// The section "Limits" states the defaults, the nesting of a path itself, that input_bytes is not checked again on
// a lazy that exists, and that cbor::limits is one object for every thread.
TEST_CASE("doc path: limits in detail")
{
    static_assert(cbor::limit_values{}.string_length == SIZE_MAX);
    static_assert(cbor::limit_values{}.container_elements == SIZE_MAX);
    static_assert(cbor::limit_values{}.input_bytes == SIZE_MAX);
    static_assert(cbor::limit_values{}.nesting_depth == CBOR_NESTING_DEPTH_DEFAULT);
    test_binding b;
    auto const one = *cbor::lazy::from(encoded(A(1)));
    auto const big = *cbor::lazy::from(encoded(M("pad"s, "0123456789"s, "a"s, A(1, 2))));
    auto const a = *big.at("a");
    {
        test::limits_guard const g{{.nesting_depth = 2}};
        CHECK_EQ(cbor::query(b, "$[?@ == [[[1]]]]", one).error(), error::nesting_depth_exceeded);
        CHECK_EQ(cbor::query(b, "$[?@[?@[?@]]]", one).error(), error::nesting_depth_exceeded);
        CHECK_EQ(cbor::query(b, "$[?(((@)))]", one).error(), error::nesting_depth_exceeded);
        CHECK_EQ(cbor::query(b, "$[?length(value(value(@))) == 1]", one).error(), error::nesting_depth_exceeded);
    }
    {
        test::limits_guard const g{{.input_bytes = 4}};
        CHECK_EQ(cbor::at_path<"$[1]", int>(a), 2);
        CHECK(*cbor::query(b, "$[*]", a) == A(1, 2));
    }
    {
        test::limits_guard const g{{.string_length = 2}};
        std::size_t seen = 0;
        std::jthread([&seen] { seen = cbor::limits.string_length; }).join();
        CHECK_EQ(seen, 2u);
    }
}

// The section "Threads" states that the first thread that reads the marks of a lazy owns it, and that only a debug
// build checks it.
TEST_CASE("doc path: the owner thread of a lazy")
{
    auto const shared = *cbor::lazy::from("\x82\xd8\x1c\x01\xd8\x1d\x00"s);
    test_binding b;
    CHECK(*cbor::at_path(b, "$[1]", shared) == V(1));
    bool threw = false;
    std::jthread([&threw, &shared] {
        test_binding t;
        try {
            (void)cbor::at_path(t, "$[1]", shared);
        } catch (std::logic_error const &) {
            threw = true;
        }
    }).join();
#ifdef NDEBUG
    CHECK_FALSE(threw);
#else
    CHECK(threw);
#endif
}
