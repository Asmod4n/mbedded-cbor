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

// Each test in this file is one example of a reference page of the lazy group; the comments of lazy.hpp take the
// examples through the snippet markers. The tests exist so that every example compiles and gives the result that its
// page states.

namespace
{

template <class T>
concept adopt_accepts = requires(T &&t) { cbor::lazy::adopt(std::forward<T>(t)); };

template <class V>
concept front_callable = requires(V const &v) { v.front(); };

}

// The example of the group lazy, in the text of its page. The test exists so that the example compiles and gives the
// values that the page states. The bytes are checked against the encoding of RFC 8949, so that the test does not rely
// on the helpers alone.
TEST_CASE("doc reference lazy: the example of the group lazy")
{
    std::string bytes = test::preferred(test::M("name"s, "ann"s, "scores"s, test::A(4, 5, 6)));
    CHECK_EQ(bytes, "\xa2\x64\x6e\x61\x6d\x65\x63\x61\x6e\x6e\x66\x73\x63\x6f\x72\x65\x73\x83\x04\x05\x06"s);
    // [lazy group]
    // bytes: a std::string with {"name": "ann", "scores": [4, 5, 6]}
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    auto const name = doc->at("name")->get<std::string_view>();  // a ref to "ann"
    auto const scores = doc->at("scores");
    auto const count = scores->size();                             // 3
    auto const elements = scores->elements();
    int total = 0;
    for (auto const element : *elements)
        total += element->get<int>().value_or(0);
    // total: 15
    // [lazy group]
    REQUIRE(doc);
    CHECK_EQ(**name, "ann"sv);
    CHECK_EQ(count.value(), 3u);
    CHECK_EQ(total, 15);
}

// lazy.hpp, snippet adopt: the bytes move into the lazy; an lvalue, a view and a literal do not compile.
TEST_CASE("doc reference lazy: adopt")
{
    std::string bytes = test::preferred(test::M("id"s, 42));
    std::int64_t id = 0;
    // [adopt]
    // bytes: a std::string with {"id": 42}
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    if (doc)
        id = doc->at("id")->get<std::int64_t>().value_or(0);  // 42
    // [adopt]
    REQUIRE(doc);
    CHECK_EQ(id, 42);
    CHECK(adopt_accepts<std::string>);
    CHECK_FALSE(adopt_accepts<std::string &>);
    CHECK_FALSE(adopt_accepts<std::string const &>);
    CHECK_FALSE(adopt_accepts<std::string_view>);
    CHECK_FALSE(adopt_accepts<char const (&)[3]>);
}

// lazy.hpp, snippet view: both forms read the bytes in place; a null or an empty owner is a wrong use.
TEST_CASE("doc reference lazy: view")
{
    auto const owner = std::make_shared<std::string const>(test::preferred(test::M("id"s, 42)));
    // [view]
    // owner: a std::shared_ptr<std::string const> with {"id": 42}
    auto const doc = cbor::lazy::view(owner);
    auto const id = doc->at("id")->get<std::int64_t>();  // 42
    // [view]
    REQUIRE(doc);
    CHECK_EQ(id.value(), 42);
    auto const in_place = cbor::lazy::view(std::shared_ptr<void const>(owner), std::string_view(*owner));
    CHECK_EQ(in_place->at("id")->get<std::int64_t>().value(), 42);
    CHECK_THROWS_AS((void)cbor::lazy::view(std::shared_ptr<std::string const>()), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy::view(std::shared_ptr<void const>(), std::string_view(*owner)),
                    std::logic_error);
}

// lazy.hpp, snippet lazy: a copy is a second position in the same bytes; a lazy with no top-level item is a wrong
// use.
TEST_CASE("doc reference lazy: lazy")
{
    std::string bytes = test::preferred(test::A(1, 2));
    // [lazy]
    // bytes: a std::string with [1, 2]
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    cbor::lazy const copy = *doc;
    auto const second = copy.at(1)->get<int>();  // 2
    // cbor::lazy{}.size() throws std::logic_error
    // [lazy]
    CHECK_EQ(second.value(), 2);
    CHECK_THROWS_AS((void)cbor::lazy{}.size(), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.at(0), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.type(), std::logic_error);
}

// lazy.hpp, snippet at: a text key, an integer key and an index; the three errors of the example.
TEST_CASE("doc reference lazy: at")
{
    std::string bytes =
        test::preferred(test::M("cars"s, test::A(test::M("hp"s, 5), test::M("hp"s, 7)), 7, "seven"s));
    // [at]
    // bytes: a std::string with {"cars": [{"hp": 5}, {"hp": 7}], 7: "seven"}
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    auto const hp = doc->at("cars")->at(1)->at("hp")->get<int>();  // 7
    auto const seven = doc->at(cbor::key{7})->get<std::string_view>();  // a ref to "seven"
    auto const bus = doc->at("bus");                                   // key_not_found
    auto const first = doc->at(0);                                     // not_indexable
    // [at]
    CHECK_EQ(hp.value(), 7);
    CHECK_EQ(**seven, "seven"sv);
    CHECK_EQ(bus.error(), cbor::error::key_not_found);
    CHECK_EQ(first.error(), cbor::error::not_indexable);
    CHECK_EQ(doc->at("cars")->at(5).error(), cbor::error::index_out_of_bounds);
}

// lazy.hpp, snippet operator_subscript: operator[] gives the result of at and does not throw.
TEST_CASE("doc reference lazy: operator[]")
{
    std::string bytes = test::preferred(test::M("cars"s, test::A(5, 7)));
    // [operator_subscript]
    // bytes: a std::string with {"cars": [5, 7]}
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    auto const second = (*(*doc)["cars"])[1]->get<int>();  // 7
    auto const bus = (*doc)["bus"];                        // key_not_found
    // [operator_subscript]
    CHECK_EQ(second.value(), 7);
    CHECK_EQ(bus.error(), cbor::error::key_not_found);
}

// lazy.hpp, snippet at_path: a scalar, a text as a ref, a nodelist, a missing key, the lazy of a node, and a query
// compiled at run time.
TEST_CASE("doc reference lazy: at_path")
{
    std::string bytes = test::preferred(test::M("user"s, test::M("id"s, 42, "tags"s, test::A("a"s, "b"s))));
    // [at_path]
    // bytes: a std::string with {"user": {"id": 42, "tags": ["a", "b"]}}
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    auto const id = doc->at_path<"$.user.id", std::int64_t>();           // 42
    auto const last = doc->at_path<"$.user.tags[-1]", std::string_view>(); // a ref to "b"
    auto const tags = doc->at_path<"$.user.tags[*]", cbor::nodelist>();    // 2 nodes
    // [at_path]
    CHECK_EQ(id.value(), 42);
    CHECK_EQ(**last, "b"sv);
    CHECK_EQ(tags->size(), 2u);
    CHECK_EQ(doc->at_path<"$.user.id">()->get<int>().value(), 42);
    auto const query = cbor::path::compile<std::int64_t>("$.user.id");
    REQUIRE(query);
    CHECK_EQ(doc->at_path(*query).value(), 42);
    CHECK_EQ(doc->at_path<"$.user.age", int>().error(), cbor::error::key_not_found);
}

// lazy.hpp, snippet size: the count of the pairs of a map; a value that is not a container has no size.
TEST_CASE("doc reference lazy: size")
{
    std::string bytes = test::preferred(test::M("a"s, 1, "b"s, 2));
    // [size]
    // bytes: a std::string with {"a": 1, "b": 2}
    auto const map = cbor::lazy::adopt(std::move(bytes));
    auto const pairs = map->size();          // 2
    auto const none = map->at("a")->size();  // not_indexable
    // [size]
    CHECK_EQ(pairs.value(), 2u);
    CHECK_EQ(none.error(), cbor::error::not_indexable);
}

// lazy.hpp, snippet empty: an empty and a full array.
TEST_CASE("doc reference lazy: empty")
{
    std::string bytes = test::preferred(test::M("a"s, test::A(), "b"s, test::A(1)));
    // [empty]
    // bytes: a std::string with {"a": [], "b": [1]}
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    auto const a = doc->at("a")->empty();  // true
    auto const b = doc->at("b")->empty();  // false
    // [empty]
    CHECK(a.value());
    CHECK_FALSE(b.value());
}

// lazy.hpp, snippet type: a bignum that fits 64 bits reads as an integer; another tag stays a tag.
TEST_CASE("doc reference lazy: type")
{
    std::string bytes = "\x84\x01\x61\x61\xc2\x41\x05\xc1\x00"s;
    // [type]
    // bytes: a std::string with [1, "a", 2(h'05'), 1(0)]
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    auto const one = doc->at(0)->type();     // cbor::type::unsigned_integer
    auto const text = doc->at(1)->type();    // cbor::type::text_string
    auto const bignum = doc->at(2)->type();  // cbor::type::unsigned_integer
    auto const time = doc->at(3)->type();    // cbor::type::tag
    // [type]
    CHECK_EQ(one.value(), cbor::type::unsigned_integer);
    CHECK_EQ(text.value(), cbor::type::text_string);
    CHECK_EQ(bignum.value(), cbor::type::unsigned_integer);
    CHECK_EQ(time.value(), cbor::type::tag);
}

// lazy.hpp, snippet get: a text as a ref, an integer, and the two errors of the example.
TEST_CASE("doc reference lazy: get")
{
    std::string bytes = test::preferred(test::M("name"s, "ann"s, "age"s, 300));
    // [get]
    // bytes: a std::string with {"name": "ann", "age": 300}
    auto const doc = cbor::lazy::adopt(std::move(bytes));
    auto const name = doc->at("name")->get<std::string_view>();  // a ref to "ann"
    auto const age = doc->at("age")->get<std::int64_t>();        // 300
    auto const small = doc->at("age")->get<std::uint8_t>();      // number_out_of_range
    auto const other = doc->at("name")->get<int>();              // incorrect_type
    // [get]
    CHECK_EQ(**name, "ann"sv);
    CHECK_EQ(age.value(), 300);
    CHECK_EQ(small.error(), cbor::error::number_out_of_range);
    CHECK_EQ(other.error(), cbor::error::incorrect_type);
}

// lazy.hpp, snippet elements: a loop over an array, and an array of indefinite length.
TEST_CASE("doc reference lazy: elements")
{
    std::string bytes = test::preferred(test::A(1, 2, 3));
    // [elements]
    // bytes: a std::string with [1, 2, 3]
    auto const array = cbor::lazy::adopt(std::move(bytes));
    int sum = 0;
    for (auto const element : *array->elements())
        if (element)
            sum += element->get<int>().value_or(0);
    // sum: 6
    // [elements]
    CHECK_EQ(sum, 6);
    CHECK_EQ(cbor::lazy::adopt("\x9f\x01\xff"s)->elements().error(), cbor::error::indefinite_length);
    CHECK_EQ(array->at(0)->elements().error(), cbor::error::not_indexable);
}

// lazy.hpp, snippet entries: the pairs of a map in the order of the bytes.
TEST_CASE("doc reference lazy: entries")
{
    std::string bytes = test::preferred(test::M("a"s, 1, "b"s, 2));
    std::string keys;
    int values = 0;
    // [entries]
    // bytes: a std::string with {"a": 1, "b": 2}
    auto const map = cbor::lazy::adopt(std::move(bytes));
    for (auto const pair : *map->entries()) {
        if (!pair)
            break;
        auto const key = pair->first.get<std::string_view>();  // "a", then "b"
        keys += **key;
        values = values * 10 + pair->second.get<int>().value_or(0);  // 1, then 2
    }
    // [entries]
    CHECK_EQ(keys, "ab");
    CHECK_EQ(values, 12);
}

// lazy.hpp, snippet clone: the tree of one child; it stays valid after the lazy and its owner end.
TEST_CASE("doc reference lazy: clone")
{
    auto const tree = [] {
        std::string bytes = test::preferred(test::M("a"s, test::A(1, "x"s)));
        // [clone]
        // bytes: a std::string with {"a": [1, "x"]}
        auto const doc = cbor::lazy::adopt(std::move(bytes));
        auto const copy = doc->at("a")->clone();  // the tree of [1, "x"], valid after doc ends
        // [clone]
        return copy;
    }();
    REQUIRE(tree);
    CHECK_EQ((*tree)->type, cbor::type::array);
    auto const children = (*tree)->get<cbor::type::array>().value();
    REQUIRE_EQ(children.size(), 2u);
    CHECK_EQ(children[1].get<cbor::type::text_string>().value(), "x"sv);
}

// lazy.hpp, snippet lazy_elements: the range concepts, count_if, a deleted front, and an error that repeats.
TEST_CASE("doc reference lazy: lazy_elements")
{
    CHECK(std::ranges::forward_range<cbor::lazy_elements>);
    CHECK(std::ranges::sized_range<cbor::lazy_elements>);
    CHECK(std::ranges::borrowed_range<cbor::lazy_elements>);
    CHECK_FALSE(front_callable<cbor::lazy_elements>);
    std::string bytes = test::preferred(test::A(test::M("age"s, 31), test::M("age"s, 20), test::M("age"s, 40)));
    // [lazy_elements]
    // bytes: a std::string with [{"age": 31}, {"age": 20}, {"age": 40}]
    auto const people = cbor::lazy::adopt(std::move(bytes));
    auto const view = *people->elements();  // 3 elements
    auto const older = std::ranges::count_if(view, [](std::expected<cbor::lazy, cbor::error> const &e) {
        return e && e->at("age")->get<int>().value_or(0) > 30;
    });  // 2
    // [lazy_elements]
    CHECK_EQ(view.size(), 3u);
    CHECK_EQ(older, 2);

    auto const broken = cbor::lazy::adopt("\x83\x1c\x00\x00"s)->elements();
    REQUIRE(broken);
    std::vector<bool> read;
    for (auto const element : *broken)
        read.push_back(element.has_value());
    CHECK_EQ(read, std::vector<bool>{true, false, false});
}

// lazy.hpp, snippet lazy_entries: the range concepts, size, and a standard view over the pairs.
TEST_CASE("doc reference lazy: lazy_entries")
{
    CHECK(std::ranges::forward_range<cbor::lazy_entries>);
    CHECK(std::ranges::sized_range<cbor::lazy_entries>);
    CHECK(std::ranges::borrowed_range<cbor::lazy_entries>);
    CHECK_FALSE(front_callable<cbor::lazy_entries>);
    std::string bytes = test::preferred(test::M("a"s, 1, "b"s, 2, "c"s, 3));
    // [lazy_entries]
    // bytes: a std::string with {"a": 1, "b": 2, "c": 3}
    auto const map = cbor::lazy::adopt(std::move(bytes));
    auto const view = *map->entries();                                // 3 pairs
    auto const rest = std::ranges::distance(view | std::views::drop(1));  // 2
    // [lazy_entries]
    CHECK_EQ(view.size(), 3u);
    CHECK_EQ(rest, 2);
}
