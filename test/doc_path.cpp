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
