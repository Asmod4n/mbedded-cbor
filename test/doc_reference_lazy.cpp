#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;

// Each test in this file is one example of a page in doc/reference/lazy/. The tests exist so that every
// example compiles and gives the result that its page states.

namespace
{

template <class T>
concept take_ownership_accepts = requires(T &&t) { cbor::lazy::take_ownership(std::forward<T>(t)); };

template <class V>
concept front_callable = requires(V const &v) { v.front(); };

}

// take_ownership.md: the bytes move into the lazy; an lvalue, a view and a literal do not compile.
TEST_CASE("doc reference lazy: take_ownership")
{
    std::string bytes = test::preferred(test::M("id"s, 42));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    REQUIRE(doc);
    CHECK_EQ(doc->at("id")->get<std::int64_t>().value(), 42);
    CHECK(take_ownership_accepts<std::string>);
    CHECK_FALSE(take_ownership_accepts<std::string &>);
    CHECK_FALSE(take_ownership_accepts<std::string const &>);
    CHECK_FALSE(take_ownership_accepts<std::string_view>);
    CHECK_FALSE(take_ownership_accepts<char const (&)[3]>);
}

// view.md: both forms read the bytes in place; a null or an empty owner is a wrong use.
TEST_CASE("doc reference lazy: view")
{
    auto const owner = std::make_shared<std::string const>(test::preferred(test::M("id"s, 42)));
    auto const doc = cbor::lazy::view(owner);
    REQUIRE(doc);
    CHECK_EQ(doc->at("id")->get<std::int64_t>().value(), 42);
    auto const in_place = cbor::lazy::view(std::shared_ptr<void const>(owner), std::string_view(*owner));
    CHECK_EQ(in_place->at("id")->get<std::int64_t>().value(), 42);
    CHECK_THROWS_AS((void)cbor::lazy::view(std::shared_ptr<std::string const>()), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy::view(std::shared_ptr<void const>(), std::string_view(*owner)),
                    std::logic_error);
}

// lazy.md: a copy is a second position in the same bytes; a lazy with no top-level item is a wrong use.
TEST_CASE("doc reference lazy: lazy")
{
    std::string bytes = test::preferred(test::A(1, 2));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    cbor::lazy const copy = *doc;
    CHECK_EQ(copy.at(1)->get<int>().value(), 2);
    CHECK_THROWS_AS((void)cbor::lazy{}.size(), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.at(0), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.type(), std::logic_error);
}

// at.md: a text key, an integer key and an index; the three errors of the example.
TEST_CASE("doc reference lazy: at")
{
    std::string bytes =
        test::preferred(test::M("cars"s, test::A(test::M("hp"s, 5), test::M("hp"s, 7)), 7, "seven"s));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    CHECK_EQ(doc->at("cars")->at(1)->at("hp")->get<int>().value(), 7);
    auto const seven = doc->at(cbor::key{7})->get<std::string_view>();
    CHECK_EQ(**seven, "seven"sv);
    CHECK_EQ(doc->at("bus").error(), cbor::error::key_not_found);
    CHECK_EQ(doc->at(0).error(), cbor::error::not_indexable);
    CHECK_EQ(doc->at("cars")->at(5).error(), cbor::error::index_out_of_bounds);
}

// operator_subscript.md: operator[] gives the result of at and does not throw.
TEST_CASE("doc reference lazy: operator[]")
{
    std::string bytes = test::preferred(test::M("cars"s, test::A(5, 7)));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    CHECK_EQ((*(*doc)["cars"])[1]->get<int>().value(), 7);
    CHECK_EQ((*doc)["bus"].error(), cbor::error::key_not_found);
}

// at_path.md: each row of the table of examples (a scalar, a text as a ref, a nodelist, a missing key), the lazy of
// a node, and a query compiled at run time.
TEST_CASE("doc reference lazy: at_path")
{
    std::string bytes = test::preferred(test::M("user"s, test::M("id"s, 42, "tags"s, test::A("a"s, "b"s))));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    CHECK_EQ(doc->at_path<"$.user.id", std::int64_t>().value(), 42);
    auto const last = doc->at_path<"$.user.tags[-1]", std::string_view>();
    CHECK_EQ(**last, "b"sv);
    CHECK_EQ(doc->at_path<"$.user.tags[*]", cbor::nodelist>()->size(), 2u);
    CHECK_EQ(doc->at_path<"$.user.id">()->get<int>().value(), 42);
    auto const query = cbor::jsonpath::compile<std::int64_t>("$.user.id");
    REQUIRE(query);
    CHECK_EQ(doc->at_path(*query).value(), 42);
    CHECK_EQ(doc->at_path<"$.user.age", int>().error(), cbor::error::key_not_found);
}

// size.md: the count of the pairs of a map; a value that is not a container has no size.
TEST_CASE("doc reference lazy: size")
{
    std::string bytes = test::preferred(test::M("a"s, 1, "b"s, 2));
    auto const map = cbor::lazy::take_ownership(std::move(bytes));
    CHECK_EQ(map->size().value(), 2u);
    CHECK_EQ(map->at("a")->size().error(), cbor::error::not_indexable);
}

// empty.md: an empty and a full array.
TEST_CASE("doc reference lazy: empty")
{
    std::string bytes = test::preferred(test::M("a"s, test::A(), "b"s, test::A(1)));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    CHECK(doc->at("a")->empty().value());
    CHECK_FALSE(doc->at("b")->empty().value());
}

// type.md: a bignum that fits 64 bits reads as an integer; another tag stays a tag.
TEST_CASE("doc reference lazy: type")
{
    std::string bytes = "\x84\x01\x61\x61\xc2\x41\x05\xc1\x00"s;
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    CHECK_EQ(doc->at(0)->type().value(), cbor::type::unsigned_integer);
    CHECK_EQ(doc->at(1)->type().value(), cbor::type::text_string);
    CHECK_EQ(doc->at(2)->type().value(), cbor::type::unsigned_integer);
    CHECK_EQ(doc->at(3)->type().value(), cbor::type::tag);
}

// get.md: a text as a ref, an integer, and the two errors of the example.
TEST_CASE("doc reference lazy: get")
{
    std::string bytes = test::preferred(test::M("name"s, "ann"s, "age"s, 300));
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    auto const name = doc->at("name")->get<std::string_view>();
    CHECK_EQ(**name, "ann"sv);
    CHECK_EQ(doc->at("age")->get<std::int64_t>().value(), 300);
    CHECK_EQ(doc->at("age")->get<std::uint8_t>().error(), cbor::error::number_out_of_range);
    CHECK_EQ(doc->at("name")->get<int>().error(), cbor::error::incorrect_type);
}

// elements.md: a loop over an array, and an array of indefinite length.
TEST_CASE("doc reference lazy: elements")
{
    std::string bytes = test::preferred(test::A(1, 2, 3));
    auto const array = cbor::lazy::take_ownership(std::move(bytes));
    int sum = 0;
    for (auto const element : *array->elements())
        if (element)
            sum += element->get<int>().value_or(0);
    CHECK_EQ(sum, 6);
    CHECK_EQ(cbor::lazy::take_ownership("\x9f\x01\xff"s)->elements().error(), cbor::error::indefinite_length);
    CHECK_EQ(array->at(0)->elements().error(), cbor::error::not_indexable);
}

// entries.md: the pairs of a map in the order of the bytes.
TEST_CASE("doc reference lazy: entries")
{
    std::string bytes = test::preferred(test::M("a"s, 1, "b"s, 2));
    auto const map = cbor::lazy::take_ownership(std::move(bytes));
    std::string keys;
    int values = 0;
    for (auto const pair : *map->entries()) {
        REQUIRE(pair);
        auto const key = pair->first.get<std::string_view>();
        keys += **key;
        values = values * 10 + *pair->second.get<int>();
    }
    CHECK_EQ(keys, "ab");
    CHECK_EQ(values, 12);
}

// clone.md: the tree of one child; it stays valid after the lazy and its owner end.
TEST_CASE("doc reference lazy: clone")
{
    auto const tree = [] {
        std::string bytes = test::preferred(test::M("a"s, test::A(1, "x"s)));
        auto const doc = cbor::lazy::take_ownership(std::move(bytes));
        return doc->at("a")->clone();
    }();
    REQUIRE(tree);
    CHECK_EQ((*tree)->type, cbor::type::array);
    auto const children = (*tree)->get<cbor::type::array>().value();
    REQUIRE_EQ(children.size(), 2u);
    CHECK_EQ(children[1].get<cbor::type::text_string>().value(), "x"sv);
}

// lazy_elements.md: the range concepts, count_if, a deleted front, and an error that repeats.
TEST_CASE("doc reference lazy: lazy_elements")
{
    CHECK(std::ranges::forward_range<cbor::lazy_elements>);
    CHECK(std::ranges::sized_range<cbor::lazy_elements>);
    CHECK(std::ranges::borrowed_range<cbor::lazy_elements>);
    CHECK_FALSE(front_callable<cbor::lazy_elements>);
    std::string bytes = test::preferred(test::A(test::M("age"s, 31), test::M("age"s, 20), test::M("age"s, 40)));
    auto const people = cbor::lazy::take_ownership(std::move(bytes));
    auto const view = *people->elements();
    CHECK_EQ(view.size(), 3u);
    CHECK_EQ(std::ranges::count_if(view,
                                   [](std::expected<cbor::lazy, cbor::error> const &e) {
                                       return e && e->at("age")->get<int>().value_or(0) > 30;
                                   }),
             2);

    auto const broken = cbor::lazy::take_ownership("\x83\x1c\x00\x00"s)->elements();
    REQUIRE(broken);
    std::vector<bool> read;
    for (auto const element : *broken)
        read.push_back(element.has_value());
    CHECK_EQ(read, std::vector<bool>{true, false, false});
}

// lazy_entries.md: the range concepts, size, and a standard view over the pairs.
TEST_CASE("doc reference lazy: lazy_entries")
{
    CHECK(std::ranges::forward_range<cbor::lazy_entries>);
    CHECK(std::ranges::sized_range<cbor::lazy_entries>);
    CHECK(std::ranges::borrowed_range<cbor::lazy_entries>);
    CHECK_FALSE(front_callable<cbor::lazy_entries>);
    std::string bytes = test::preferred(test::M("a"s, 1, "b"s, 2, "c"s, 3));
    auto const map = cbor::lazy::take_ownership(std::move(bytes));
    auto const view = *map->entries();
    CHECK_EQ(view.size(), 3u);
    CHECK_EQ(std::ranges::distance(view | std::views::drop(1)), 2);
}
