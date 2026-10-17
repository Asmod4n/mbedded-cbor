#include "reference.hpp"
#include "owned_copy.hpp"
#include "item_head.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <random>
#include <ranges>
#include <span>
#include <memory_resource>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

cbor::lazy lazy_of(std::string const &document)
{
    auto const l = test::lazy_of_copy(document);
    REQUIRE(l.has_value());
    return *l;
}

value value_at(cbor::lazy const &l)
{
    auto const v = test::value_of(l);
    REQUIRE(v.has_value());
    return *v;
}

cbor::lazy at(cbor::lazy const &l, std::string_view const key)
{
    auto const r = l.at(key);
    REQUIRE(r.has_value());
    return *r;
}

cbor::lazy at(cbor::lazy const &l, std::size_t const index)
{
    auto const r = l.at(index);
    REQUIRE(r.has_value());
    return *r;
}

cbor::lazy at(cbor::lazy const &l, cbor::key const key)
{
    auto const r = l.at(key);
    REQUIRE(r.has_value());
    return *r;
}

} // namespace

// Ported from test.rb: 'lazy: basic key access (string, integer, mixed)'.
TEST_CASE("lazy: key access by string and by integer")
{
    std::string const a = encoded(M("a"s, 1, "b"s, 2));
    CHECK(value_at(at(lazy_of(a), "a")) == V(1));
    std::string const b = encoded(M(1, "one"s, 2, "two"s));
    CHECK(value_at(at(lazy_of(b), cbor::key{1})) == V("one"s));
    CHECK(value_at(at(lazy_of(b), cbor::key{2})) == V("two"s));
    std::string const c = encoded(M(1, "int"s, "str"s, "s"s, 100, "c"s));
    CHECK(value_at(at(lazy_of(c), cbor::key{1})) == V("int"s));
    CHECK(value_at(at(lazy_of(c), "str")) == V("s"s));
    CHECK(value_at(at(lazy_of(c), cbor::key{100})) == V("c"s));
}

// Ported from test.rb: 'lazy: access errors — empty, missing, out-of-bounds'. Each Ruby error class
// has its own error here: IndexError index_out_of_bounds, KeyError key_not_found, TypeError
// not_indexable.
TEST_CASE("lazy: access errors")
{
    std::string const empty_array = encoded(A());
    std::string const empty_map = encoded(M());
    std::string const one = encoded(M("a"s, 1));
    std::string const three = encoded(A(1, 2, 3));
    std::string const scalar = encoded(V(42));
    CHECK_EQ(lazy_of(empty_array).at(0).error(), error::index_out_of_bounds);
    CHECK_EQ(lazy_of(empty_map).at("x").error(), error::key_not_found);
    CHECK_EQ(lazy_of(one).at("missing").error(), error::key_not_found);
    CHECK_EQ(lazy_of(three).at(99).error(), error::index_out_of_bounds);
    CHECK_EQ(lazy_of(three).at("invalid").error(), error::not_indexable);
    CHECK_EQ(lazy_of(scalar).at("key").error(), error::not_indexable);
    CHECK_EQ(lazy_of(one).at(0).error(), error::not_indexable);
    CHECK_EQ(lazy_of(three).at(cbor::key{0}).error(), error::not_indexable);
    CHECK_EQ(lazy_of(scalar).at(0).error(), error::not_indexable);
    CHECK_EQ(lazy_of(empty_map).at(cbor::key{0}).error(), error::key_not_found);
}

// at(std::size_t) reads an array and at(cbor::key) reads a map, as std::vector::at and std::map::at do. A
// literal index chooses the array form, so at(1) never reads the map key 1.
TEST_CASE("lazy: at(std::size_t) reads an array, at(cbor::key) reads a map")
{
    std::string const both = encoded(A(M(1, "one"s), 7));
    cbor::lazy const root = lazy_of(both);
    CHECK(value_at(at(root, 1)) == V(7));
    CHECK(value_at(at(at(root, 0), cbor::key{1})) == V("one"s));
    CHECK_EQ(at(root, 0).at(1).error(), error::not_indexable);
    CHECK_EQ(root.at(cbor::key{1}).error(), error::not_indexable);
    CHECK(value_at(at(lazy_of("\xa1\x20\x65minus"s), cbor::key{-1})) == V("minus"s));
    CHECK_EQ(root.at(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
}

// Ported from test.rb: 'lazy: deep nesting + wide maps'.
TEST_CASE("lazy: deep nesting and a wide map")
{
    std::string const deep = encoded(A(A(A(A(A(42))))));
    CHECK(value_at(at(at(at(at(at(lazy_of(deep), 0), 0), 0), 0), 0)) == V(42));
    map wide;
    for (int i = 0; i < 100; ++i)
        wide.push_back(entry{V("key_" + std::to_string(i)), V(i)});
    std::string const w = encoded(value{wide});
    CHECK(value_at(at(lazy_of(w), "key_0")) == V(0));
    CHECK(value_at(at(lazy_of(w), "key_50")) == V(50));
    CHECK(value_at(at(lazy_of(w), "key_99")) == V(99));
}

// Ported from test.rb: 'lazy: dig — missing keys return nil, negative array indices work'. dig maps
// to a chain of at; a miss is an error value, and the application makes nil of it. at takes a std::size_t, as
// std::vector::at does, so an application counts a negative index from size(). A path keeps the negative index of
// RFC 9535.
TEST_CASE("lazy: negative indices and misses")
{
    std::string const h = encoded(M("a"s, 1, "b"s, M("c"s, 42)));
    CHECK_EQ(lazy_of(h).at("missing").error(), error::key_not_found);
    CHECK(value_at(at(at(lazy_of(h), "b"), "c")) == V(42));
    std::string const a = encoded(A(10, 20, 30, 40, 50));
    cbor::lazy const l = lazy_of(a);
    CHECK(value_at(at(l, static_cast<std::size_t>(*l.size() - 1))) == V(50));
    CHECK(value_at(at(l, static_cast<std::size_t>(*l.size() - 5))) == V(10));
    CHECK_EQ(cbor::at_path<"$[-1]", std::uint64_t>(a).value(), 50u);
    CHECK_EQ(cbor::at_path<"$[-5]", std::uint64_t>(a).value(), 10u);
    CHECK_EQ(cbor::at_path<"$[-99]", std::uint64_t>(a).error(), error::index_out_of_bounds);
}

// Ported from test.rb: 'lazy: can still navigate child lazies after calling .value on parent'.
TEST_CASE("lazy: a value of the parent does not change the children")
{
    std::string const doc = encoded(M("a"s, M("b"s, 42)));
    CHECK(value_at(lazy_of(doc)) == M("a"s, M("b"s, 42)));
    CHECK(value_at(at(at(lazy_of(doc), "a"), "b")) == V(42));
}

// Ported from test.rb: 'lazy: random-access stress'. A fixed seed, so a failure repeats.
TEST_CASE("lazy: random access")
{
    array statuses;
    for (int i = 1; i <= 50; ++i)
        statuses.push_back(M("id"s, i, "txt"s, "msg" + std::to_string(i)));
    std::string const doc = encoded(M("statuses"s, value{statuses}));
    std::mt19937 rng(1);
    for (int n = 0; n < 200; ++n) {
        std::size_t const i = rng() % 50;
        CHECK(value_at(at(at(at(lazy_of(doc), "statuses"), i), "txt")) == V("msg" + std::to_string(i + 1)));
    }
}

// Ported from test.rb: 'lazy: skip_cbor truncation raises RangeError'.
TEST_CASE("lazy: a truncated item before the target")
{
    std::string const doc = "\x82\x4a\x01\x02\x03\x18\x2a"s;
    CHECK_EQ(lazy_of(doc).at(1).error(), error::too_little_data);
}

// Ported from test.rb: 'lazy: huge aref index handled cleanly'.
TEST_CASE("lazy: a huge index")
{
    std::string const doc = encoded(A(1, 2, 3));
    CHECK_EQ(lazy_of(doc).at(0x7fffffff).error(), error::index_out_of_bounds);
}

// Found by the fuzz corpus: a reference inside the mark it names, d8 1c d8 1d 00, once led the navigation back to
// itself without end. A lazy now reads both as plain tags, so the step reaches the integer 0, which has no element.
TEST_CASE("lazy: a reference inside its own mark is a plain tag")
{
    std::string const doc = "\xd8\x1c\xd8\x1d\x00"s;
    CHECK_EQ(lazy_of(doc).at(0).error(), error::not_indexable);
    CHECK(test::value_of(lazy_of(doc)) == V(tagged{28, V(tagged{29, V(0)})}));
}

// Found by the fuzz corpus: a map that claims about 7.7 * 10^18 pairs. The count is larger than the bytes left, so
// the lookup gives too_little_data before it reads a key, by owner decision.
TEST_CASE("lazy: a value inside a huge claimed map")
{
    std::string const doc = "\xbb\x6a\xc9\xfb\x32\xf6\xd8\xd8\x27\x61\x61\x19\x00\x00"s;
    CHECK_EQ(lazy_of(doc).at("a").error(), error::too_little_data);
}

// Found by the fuzzer: [28(28(29(0))), ...] once led from the reference through two marks back to the same
// reference. A lazy now passes the three tags to the integer 0, which has no element.
TEST_CASE("lazy: a chain of marks over a reference is a chain of plain tags")
{
    std::string const doc = "\x82\xd8\x1c\xd8\x1c\xd8\x1d\x00\x00"s;
    auto const element = lazy_of(doc).at(0);
    REQUIRE(element.has_value());
    CHECK_EQ(element->at(0).error(), error::not_indexable);
}

namespace
{

std::vector<value> elements_of(std::string const &document)
{
    auto const elements = lazy_of(document).elements();
    REQUIRE(elements.has_value());
    std::vector<value> values;
    for (auto const element : *elements) {
        REQUIRE(element.has_value());
        values.push_back(value_at(*element));
    }
    return values;
}

} // namespace

// The elements of an array come in wire order, each as a view of its own.
TEST_CASE("lazy: the elements of an array")
{
    CHECK(elements_of(encoded(A(1, "a"s, A(2)))) == std::vector<value>{V(1), V("a"s), A(2)});
    CHECK(elements_of(encoded(A())).empty());
}

// A mark around the array is passed, as at passes it.
TEST_CASE("lazy: the elements of a marked array")
{
    CHECK(elements_of("\xd8\x1c\x82\x01\x02"s) == std::vector<value>{V(1), V(2)});
}

// The entries of a map come in wire order, each as a pair of key and value.
TEST_CASE("lazy: the entries of a map")
{
    auto const entries = lazy_of(encoded(M("a"s, 1, 2, "b"s))).entries();
    REQUIRE(entries.has_value());
    std::vector<value> keys;
    std::vector<value> values;
    for (auto const entry : *entries) {
        REQUIRE(entry.has_value());
        keys.push_back(value_at(entry->first));
        values.push_back(value_at(entry->second));
    }
    CHECK(keys == std::vector<value>{V("a"s), V(2)});
    CHECK(values == std::vector<value>{V(1), V("b"s)});
}

// Only an array has elements and only a map has entries.
TEST_CASE("lazy: elements and entries of the wrong kind")
{
    CHECK_EQ(lazy_of(encoded(M("a"s, 1))).elements().error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(A(1))).entries().error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(V(1))).elements().error(), error::not_indexable);
}

// The standard algorithms and views take entries and elements as they take any standard view. Each
// concept that the two types claim is checked here at compile time: the iterators are forward
// iterators, the ranges are views, sized ranges and borrowed ranges, and an iterator outlives the range.
TEST_CASE("lazy: entries and elements model the standard concepts they claim")
{
    CHECK((std::forward_iterator<cbor::lazy_entries::iterator>));
    CHECK((std::forward_iterator<cbor::lazy_elements::iterator>));
    CHECK((std::sentinel_for<std::default_sentinel_t, cbor::lazy_entries::iterator>));
    CHECK((std::sentinel_for<std::default_sentinel_t, cbor::lazy_elements::iterator>));
    CHECK((std::ranges::forward_range<cbor::lazy_entries>));
    CHECK((std::ranges::forward_range<cbor::lazy_elements>));
    CHECK((std::ranges::view<cbor::lazy_entries>));
    CHECK((std::ranges::view<cbor::lazy_elements>));
    CHECK((std::ranges::sized_range<cbor::lazy_entries>));
    CHECK((std::ranges::sized_range<cbor::lazy_elements>));
    CHECK((std::ranges::borrowed_range<cbor::lazy_entries>));
    CHECK((std::ranges::borrowed_range<cbor::lazy_elements>));
    CHECK((!std::ranges::bidirectional_range<cbor::lazy_entries>));
    CHECK((!std::ranges::common_range<cbor::lazy_elements>));
}

namespace
{

template <class R>
concept front_reads = requires(R const &r) { r.front(); };

} // namespace

// size and empty come from the count in the head, and read no element. front is not there: on an
// empty range it would read the byte after the array.
TEST_CASE("lazy: size and empty of entries and elements")
{
    auto const entries = lazy_of(encoded(M("a"s, 1, "b"s, 2))).entries();
    REQUIRE(entries.has_value());
    CHECK_EQ(entries->size(), 2u);
    CHECK_EQ(std::ranges::size(*entries), 2u);
    CHECK_FALSE(entries->empty());
    CHECK(static_cast<bool>(*entries));
    auto const none = lazy_of(encoded(A())).elements();
    REQUIRE(none.has_value());
    CHECK_EQ(none->size(), 0u);
    CHECK(none->empty());
    CHECK_FALSE(static_cast<bool>(*none));
    CHECK(none->begin() == none->end());
    CHECK((!front_reads<cbor::lazy_elements>));
    CHECK((!front_reads<cbor::lazy_entries>));
}

// std::views::take and std::views::drop work on elements and entries as on any forward view, and an
// iterator that std::ranges::find_if gives stays valid after the range is gone (borrowed_range).
TEST_CASE("lazy: std::views::take, std::views::drop and find_if over entries and elements")
{
    auto const elements = lazy_of(encoded(A(1, 2, 3, 4))).elements();
    REQUIRE(elements.has_value());
    std::vector<value> taken;
    for (auto const element : *elements | std::views::take(2))
        taken.push_back(value_at(*element));
    CHECK(taken == std::vector<value>{V(1), V(2)});
    std::vector<value> dropped;
    for (auto const element : *elements | std::views::drop(3))
        dropped.push_back(value_at(*element));
    CHECK(dropped == std::vector<value>{V(4)});
    CHECK_EQ(std::ranges::size(*elements | std::views::drop(1)), 3u);

    cbor::lazy_entries::iterator found;
    {
        cbor::lazy const map = lazy_of(encoded(M("a"s, 1, "b"s, 2, "c"s, 3)));
        found = std::ranges::find_if(*map.entries(), [](auto const &entry) {
            return entry.has_value() && value_at(entry->second) == V(2);
        });
    }
    REQUIRE(found != std::default_sentinel);
    CHECK(value_at((*found)->first) == V("b"s));
    std::vector<value> rest;
    for (auto const entry : std::ranges::subrange(found, std::default_sentinel) | std::views::drop(1))
        rest.push_back(value_at(entry->first));
    CHECK(rest == std::vector<value>{V("c"s)});
}

// A forward iterator can be copied and walked twice, and a copy that has not moved stays equal to
// the place it was copied from.
TEST_CASE("lazy: a copy of an entries iterator walks the same pairs")
{
    auto const entries = lazy_of(encoded(M("a"s, 1, "b"s, 2))).entries();
    REQUIRE(entries.has_value());
    auto first = entries->begin();
    auto const copy = first;
    CHECK(first == copy);
    auto const before = first++;
    CHECK(before == copy);
    CHECK(first != copy);
    CHECK(value_at((*first)->first) == V("b"s));
    CHECK(value_at((*copy)->first) == V("a"s));
    CHECK(cbor::lazy_entries::iterator{} == cbor::lazy_entries::iterator{});
}

// The std::ranges algorithms walk the entries and the elements.
TEST_CASE("lazy: std::ranges algorithms over entries and elements")
{
    auto const entries = lazy_of(encoded(M("a"s, 1, 2, "b"s, "c"s, 3))).entries();
    REQUIRE(entries.has_value());
    CHECK_EQ(std::ranges::distance(*entries), 3);
    auto const found = std::ranges::find_if(*entries, [](auto const &entry) {
        return entry.has_value() && value_at(entry->first) == V(2);
    });
    REQUIRE(found != entries->end());
    CHECK(value_at((*found)->second) == V("b"s));
    CHECK_EQ(std::ranges::count_if(*entries, [](auto const &entry) {
                 return entry.has_value() && entry->first.template get<std::string_view>().has_value();
             }),
             2);

    auto const elements = lazy_of(encoded(A(1, 2, 3))).elements();
    REQUIRE(elements.has_value());
    CHECK_EQ(std::ranges::distance(*elements), 3);
    CHECK(std::ranges::all_of(*elements, [](auto const &element) { return element.has_value(); }));
    auto const two = std::ranges::find_if(*elements, [](auto const &element) { return value_at(*element) == V(2); });
    REQUIRE(two != elements->end());
    auto const next = std::ranges::next(two);
    CHECK(value_at(**next) == V(3));
}

// at gives the value of the pair with the key, for a text key and for an integer key of either sign.
TEST_CASE("lazy: at gives the value of a present key")
{
    cbor::lazy const l = lazy_of("\xa4\x61" "a\x01\x21\x65" "minus\x07\x65" "seven\x61" "b\x02"s);
    CHECK(value_at(*l.at("b")) == V(2));
    CHECK(value_at(*l.at(cbor::key{7})) == V("seven"s));
    CHECK(value_at(*l.at(cbor::key{-2})) == V("minus"s));
    CHECK_EQ(lazy_of(encoded(M())).at("a").error(), error::key_not_found);
}

// at is a lookup in a map. An array or a scalar has no keys.
TEST_CASE("lazy: a key in an item that is not a map")
{
    CHECK_EQ(lazy_of(encoded(A(1))).at(cbor::key{0}).error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(V(1))).at("a").error(), error::not_indexable);
}

// A key under tag 29 is a tag and no text key.
TEST_CASE("lazy: at does not resolve a tag 29 key")
{
    cbor::lazy const l = lazy_of("\xa2\x61" "a\xd8\x1c\x61k\xd8\x1d\x00\x02"s);
    CHECK_EQ(l.at("k").error(), error::key_not_found);
}

// A truncated map gives its error from at for a key after the cut, and the value for a key before it.
TEST_CASE("lazy: at in a truncated map gives the error")
{
    cbor::lazy const l = lazy_of("\xa2\x61" "a\x01\x61"s);
    CHECK_EQ(l.at("z").error(), error::too_little_data);
    CHECK(value_at(at(l, "a")) == V(1));
}

// An element iterator holds its position too. std::ranges::next with the end as bound steps n elements
// from it and stops at the end; std::ranges::find_if over [begin, from) searches the part before it.
TEST_CASE("lazy: an element iterator steps on and searches back with std::ranges")
{
    auto const elements = lazy_of(encoded(A(1, 2, 3, 4))).elements();
    REQUIRE(elements.has_value());
    auto const two = std::ranges::next(elements->begin(), 1, std::default_sentinel);
    auto const four = std::ranges::next(two, 2, std::default_sentinel);
    CHECK(value_at(**two) == V(2));
    CHECK(value_at(**four) == V(4));
    CHECK(std::ranges::next(four, 5, std::default_sentinel) == std::default_sentinel);
    auto const one = std::ranges::find_if(elements->begin(), four, [](auto const &e) { return value_at(*e) == V(1); });
    REQUIRE(one != four);
    CHECK(value_at(**one) == V(1));
    auto const missing = std::ranges::find_if(elements->begin(), two, [](auto const &e) { return value_at(*e) == V(3); });
    CHECK(missing == two);
}

// The iterator holds the owner of the encoded item. A step from it after the lazy and the string are gone
// reads memory that is alive; ASan checks it.
TEST_CASE("lazy: an entry iterator outlives the lazy that gave it")
{
    std::optional<cbor::lazy_entries::iterator> kept;
    {
        std::string message = encoded(M("a"s, 1, "b"s, 2, "c"s, 3));
        auto const l = cbor::lazy::take(std::move(message));
        REQUIRE(l.has_value());
        kept = l->entries()->begin();
    }
    auto const c = std::ranges::next(*kept, 2, std::default_sentinel);
    REQUIRE(c != std::default_sentinel);
    CHECK(value_at((*c)->second) == V(3));
}

// decode reads nothing ahead, so a step finds a truncated element. The step gives the error once and
// the walk ends after it.
TEST_CASE("lazy: a truncated element ends the walk with its error")
{
    auto const elements = lazy_of("\x83\x01\x62\x61"s).elements();
    REQUIRE(elements.has_value());
    std::vector<std::expected<cbor::lazy, error>> steps;
    for (auto const step : *elements)
        steps.push_back(step);
    REQUIRE(steps.size() == 3);
    CHECK(steps.at(0).has_value());
    CHECK(steps.at(1).has_value());
    CHECK_EQ(steps.at(2).error(), error::too_little_data);
}

namespace
{

template <class T>
auto get(std::string const &document)
{
    return lazy_of(document).get<T>();
}

} // namespace

// The integers of RFC 8949 Appendix A, read with get.
TEST_CASE("lazy: get reads an integer")
{
    CHECK_EQ(*get<std::uint64_t>("\x00"s), 0u);
    CHECK_EQ(*get<std::uint64_t>("\x1b\xff\xff\xff\xff\xff\xff\xff\xff"s), 18446744073709551615u);
    CHECK_EQ(*get<std::int64_t>("\x20"s), -1);
    CHECK_EQ(*get<std::int64_t>("\x39\x03\xe7"s), -1000);
    CHECK_EQ(*get<std::int64_t>("\x3b\x7f\xff\xff\xff\xff\xff\xff\xff"s), std::numeric_limits<std::int64_t>::min());
    CHECK_EQ(*get<std::uint64_t>("\xc2\x42\x01\x00"s), 256u);
    CHECK_EQ(*get<std::int64_t>("\xc3\x41\x01"s), -2);
    CHECK_EQ(*get<std::uint64_t>("\xd8\x1c\x05"s), 5u);
}

// A number that the type cannot hold is out of range; anything that is not a number has the wrong type.
TEST_CASE("lazy: get refuses an integer it cannot hold")
{
    CHECK_EQ(get<std::uint64_t>("\x20"s).error(), error::number_out_of_range);
    CHECK_EQ(get<std::int64_t>("\x1b\x80\x00\x00\x00\x00\x00\x00\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(get<std::int64_t>("\x3b\x80\x00\x00\x00\x00\x00\x00\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(get<std::uint64_t>("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s).error(),
             error::number_out_of_range);
    CHECK_EQ(get<std::uint64_t>("\x61\x61"s).error(), error::incorrect_type);
    CHECK_EQ(get<std::uint64_t>("\xf9\x3c\x00"s).error(), error::incorrect_type);
}

// Floats of the three widths of Appendix A. An integer is a double where the double holds it exactly, up to 2^53 in
// magnitude (IEEE 754 binary64 has 53 bits of significand). The owner decided this on 2026-10-10.
TEST_CASE("lazy: get reads a float")
{
    CHECK_EQ(*get<double>("\xf9\x3c\x00"s), 1.0);
    CHECK_EQ(*get<double>("\xfa\x47\xc3\x50\x00"s), 100000.0);
    CHECK_EQ(*get<double>("\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"s), 1.1);
    CHECK_EQ(*get<double>("\x01"s), 1.0);
    CHECK_EQ(*get<double>("\x3b\x00\x1f\xff\xff\xff\xff\xff\xff"s), -0x1p53);
    CHECK_EQ(get<double>("\x1b\x00\x20\x00\x00\x00\x00\x00\x01"s).error(), error::number_out_of_range);
    CHECK_EQ(get<double>("\xf5"s).error(), error::incorrect_type);
}

// The simple values false, true and null of Table 4.
TEST_CASE("lazy: get reads a simple value")
{
    CHECK_EQ(*get<bool>("\xf4"s), false);
    CHECK_EQ(*get<bool>("\xf5"s), true);
    CHECK_EQ(get<bool>("\xf6"s).error(), error::incorrect_type);
    CHECK(get<std::nullptr_t>("\xf6"s).has_value());
    CHECK_EQ(get<std::nullptr_t>("\xf7"s).error(), error::incorrect_type);
}

// A text string and a byte string come as views into the top-level item, each only as its own type. The view holds the
// top-level item, so it stays valid after the lazy that gave it ends.
TEST_CASE("lazy: get reads a string as a view")
{
    auto const text = lazy_of("\x64IETF"s).get<std::string_view>();
    REQUIRE(text.has_value());
    CHECK_EQ(**text, "IETF"sv);
    CHECK_EQ(get<std::string_view>("\x44\x01\x02\x03\x04"s).error(), error::incorrect_type);
    auto const four = lazy_of("\x44\x01\x02\x03\x04"s);
    auto const bytes = four.get<std::span<std::byte const>>();
    REQUIRE(bytes.has_value());
    CHECK(std::ranges::equal(**bytes, std::array{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}));
    CHECK_EQ(get<std::span<std::byte const>>("\x64IETF"s).error(), error::incorrect_type);
    CHECK_EQ(get<std::string_view>("\x62\x61"s).error(), error::too_little_data);
}

// RFC 8746 Table 3: tags 64 to 87 carry a typed array in a byte string; 76 is reserved. The length
// is a multiple of the element size 1 << (f + ll) of Table 2.
TEST_CASE("lazy: get reads a typed array")
{
    auto const u8 = get<cbor::typed_array>("\xd8\x40\x43\x01\x02\x03"s);
    REQUIRE(u8.has_value());
    CHECK_EQ((*u8)->tag, 64u);
    CHECK_EQ((*u8)->bytes.size(), 3u);
    auto const u16 = get<cbor::typed_array>("\xd8\x41\x44\x00\x01\x00\x02"s);
    REQUIRE(u16.has_value());
    CHECK_EQ((*u16)->tag, 65u);
    CHECK_EQ((*u16)->bytes.size(), 4u);
    auto const f64 = get<cbor::typed_array>("\xd8\x56\x48\x00\x00\x00\x00\x00\x00\xf0\x3f"s);
    REQUIRE(f64.has_value());
    CHECK_EQ((*f64)->tag, 86u);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x41\x43\x00\x01\x00"s).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x4c\x41\x00"s).error(), error::incorrect_type);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x3f\x41\x00"s).error(), error::incorrect_type);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x40\x01"s).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(get<cbor::typed_array>("\x43\x01\x02\x03"s).error(), error::incorrect_type);
}

namespace
{

// The bytes of the public encoder for one typed array.
std::expected<std::string, cbor::error> typed_encoded(std::uint64_t const tag, std::string_view const bytes)
{
    std::string out;
    cbor::encoder e(out);
    if (auto const r = e.typed_array(cbor::typed_array{tag, std::as_bytes(std::span(bytes))}); !r)
        return std::unexpected(r.error());
    REQUIRE(e.finish().has_value());
    return out;
}

} // namespace

// The encoder writes the tag and one byte string, and get reads both back.
TEST_CASE("encode: a typed array is a tag and a byte string")
{
    auto const wire = typed_encoded(65, "\x00\x01\x00\x02"sv);
    REQUIRE(wire.has_value());
    CHECK_EQ(*wire, "\xd8\x41\x44\x00\x01\x00\x02"s);
    auto const document = lazy_of(*wire);
    auto const back = document.get<cbor::typed_array>();
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->tag, 65u);
    CHECK(std::ranges::equal((*back)->bytes, std::as_bytes(std::span("\x00\x01\x00\x02"sv))));
}

// A tag outside 64 to 87, or the reserved 76, is no typed array; a length that is not a multiple of the
// element size is inadmissible content.
TEST_CASE("encode: a typed array with a wrong tag or length")
{
    CHECK((typed_encoded(76, "\x00"sv).error() == cbor::error{cbor::error::unsupported_value}));
    CHECK((typed_encoded(63, "\x00"sv).error() == cbor::error{cbor::error::unsupported_value}));
    CHECK((typed_encoded(66, "\x00\x01\x02"sv).error() == cbor::error{cbor::error::inadmissible_type_for_tag_content}));
}

// RFC 8949 3.4.5.1: tag 24 carries an encoded data item in a byte string. The library never writes tag 24 by
// itself, so the application writes the tag and the bytes of the embedded item, and a view passes through the tag
// into it.
TEST_CASE("tag 24: an embedded item written by the application is read through")
{
    std::string const embedded = test::encoded(A(2, 3));
    test::string_writer w;
    {
        cbor::encoder e(w);
        REQUIRE(e.array(2).has_value());
        REQUIRE(e.unsigned_integer(std::uint64_t{1}).has_value());
        REQUIRE(e.tag(24).has_value());
        REQUIRE(e.byte_string(std::as_bytes(std::span(embedded))).has_value());
        REQUIRE(e.finish().has_value());
    }
    CHECK_EQ(w.encoded, "\x82"s + test::native_unsigned(1) + "\xd8\x18\x53\x82"s + test::native_unsigned(2) + test::native_unsigned(3));
    auto const inner = lazy_of(w.encoded).at(1);
    REQUIRE(inner.has_value());
    auto const three = inner->at(1);
    REQUIRE(three.has_value());
    CHECK_EQ(*three->get<std::uint64_t>(), 3u);
    CHECK_EQ(get<std::uint64_t>("\xd8\x18\x05"s).error(), error::inadmissible_type_for_tag_content);
}

// A 29(0) inside an embedded data item is a plain tag over its index.
TEST_CASE("tag 24: a tag 29 inside an embedded data item is a plain tag")
{
    std::string const doc = "\x82\xd8\x1c\x07\xd8\x18\x47\x82\xd8\x1c\x09\xd8\x1d\x00"s;
    auto const embedded = lazy_of(doc).at(1);
    REQUIRE(embedded.has_value());
    auto const second = embedded->at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(*second->get<std::uint64_t>(), 0u);
}

// Every integer width reads its own edges. A CBOR integer has a 64-bit magnitude and a sign in the major type
// (RFC 8949 3.1), so a narrower type refuses the first value past its edge as out of range.
TEST_CASE("lazy: get reads every integer width up to its edge")
{
    CHECK_EQ(*get<std::uint8_t>("\x18\xff"s), 255u);
    CHECK_EQ(get<std::uint8_t>("\x19\x01\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::uint16_t>("\x19\xff\xff"s), 65535u);
    CHECK_EQ(get<std::uint16_t>("\x1a\x00\x01\x00\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::uint32_t>("\x1a\xff\xff\xff\xff"s), 4294967295u);
    CHECK_EQ(get<std::uint32_t>("\x1b\x00\x00\x00\x01\x00\x00\x00\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(get<std::uint8_t>("\x20"s).error(), error::number_out_of_range);

    CHECK_EQ(*get<std::int8_t>("\x18\x7f"s), 127);
    CHECK_EQ(get<std::int8_t>("\x18\x80"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::int8_t>("\x38\x7f"s), -128);
    CHECK_EQ(get<std::int8_t>("\x38\x80"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::int16_t>("\x19\x7f\xff"s), 32767);
    CHECK_EQ(*get<std::int16_t>("\x39\x7f\xff"s), -32768);
    CHECK_EQ(get<std::int16_t>("\x39\x80\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::int32_t>("\x1a\x7f\xff\xff\xff"s), 2147483647);
    CHECK_EQ(*get<std::int32_t>("\x3a\x7f\xff\xff\xff"s), std::numeric_limits<std::int32_t>::min());
    CHECK_EQ(get<std::int32_t>("\x1a\x80\x00\x00\x00"s).error(), error::number_out_of_range);
}

// RFC 8949 5.6: a byte string key and a text string key with the same bytes are two different keys. A lookup
// by text matches the text key only.
TEST_CASE("lazy: a byte string key is not the text key with the same bytes")
{
    auto const root = lazy_of("\xa2\x41\x61\x01\x61\x61\x02"s);
    auto const a = root.at("a");
    REQUIRE(a.has_value());
    CHECK_EQ(*a->get<std::uint64_t>(), 2u);
}

// A tag 29 before its mark is a plain tag, whatever navigation read first.
TEST_CASE("lazy: a reference forward to a mark is a plain tag")
{
    auto const root = lazy_of("\x82\xd8\x1d\x00\xd8\x1c\x05"s);
    auto const second = root.at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(*second->get<std::uint64_t>(), 5u);
    auto const first = root.at(0);
    REQUIRE(first.has_value());
    CHECK(test::value_of(*first) == V(tagged{29, V(0)}));
}

namespace
{

struct buffer_owner {
    std::string bytes;
    bool *released;

    ~buffer_owner()
    {
        *released = true;
    }
};

} // namespace

// lazy::view reads bytes that a larger object holds, through a std::shared_ptr that aliases the bytes and shares the
// object. Each lazy that a step gives holds the object too, so the bytes live until the last lazy of the top-level
// item ends.
TEST_CASE("lazy: view of an aliasing owner holds the owner until the last lazy ends")
{
    bool released = false;
    std::optional<cbor::lazy> name;
    {
        auto const owner = std::make_shared<buffer_owner>(encoded(M("user"s, M("name"s, "ann"s))), &released);
        auto const doc = cbor::lazy::view(owner, std::string_view(owner->bytes));
        REQUIRE(doc.has_value());
        name.emplace(*doc->at("user").and_then([](cbor::lazy const &u) { return u.at("name"); }));
    }
    CHECK_FALSE(released);
    {
        auto const text = name->get<std::string_view>();
        REQUIRE(text.has_value());
        CHECK_EQ(**text, "ann"sv);
    }
    name.reset();
    CHECK(released);
}

// lazy::copy takes an rvalue std::string by move and copies nothing. Each step gives a std::expected, and_then gives it to the
// next step, so an error anywhere reaches the end of the chain.
TEST_CASE("lazy: copy, at and get as a chain")
{
    std::string bytes = encoded(M("statuses"s, A(M("user"s, M("name"s, "ann"s)), M("user"s, M("name"s, "bob"s)))));
    char const *const data = bytes.data();
    cbor::lazy const doc = *cbor::lazy::take(std::move(bytes));
    auto const name = doc.at("statuses")
                          .and_then([](cbor::lazy const &s) { return s.at(1); })
                          .and_then([](cbor::lazy const &s) { return s.at("user"); })
                          .and_then([](cbor::lazy const &u) { return u.at("name"); })
                          .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "bob"sv);
    CHECK_EQ(static_cast<void const *>(doc.top_level.encoded.data()), static_cast<void const *>(data));
    auto const missing = doc.at("statuses")
                             .and_then([](cbor::lazy const &s) { return s.at(5); })
                             .and_then([](cbor::lazy const &s) { return s.at("user"); })
                             .and_then([](cbor::lazy const &u) { return u.get<std::string_view>(); });
    REQUIRE_FALSE(missing.has_value());
    CHECK_EQ(missing.error(), error::index_out_of_bounds);
    CHECK_FALSE(doc.at("nope").and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); }).has_value());
    std::size_t count = 0;
    for (auto const e : *doc.at("statuses").and_then([](cbor::lazy const &s) { return s.elements(); })) {
        REQUIRE(e.has_value());
        ++count;
    }
    CHECK_EQ(count, 2u);
}

// The owner decided that a lazy reads tags 28 and 29 as plain tags. A key under either tag is a tag and matches no
// text key and no integer key; a tag 55799 before a key is still passed.
TEST_CASE("lazy: a key under tag 28 or tag 29 is a tag")
{
    CHECK_EQ(lazy_of("\xa1\xd8\x1c\x61\x61\x01"s).at("a").error(), error::key_not_found);
    CHECK_EQ(lazy_of("\xa1\xd8\x1c\x07\x03"s).at(cbor::key{7}).error(), error::key_not_found);
    auto const root = lazy_of("\x82\xd8\x1c\x61\x61\xa1\xd8\x1d\x00\x02"s);
    REQUIRE(root.at(0).has_value());
    CHECK_EQ(root.at(1)->at("a").error(), error::key_not_found);
    CHECK_EQ(*lazy_of("\xa1\xd9\xd9\xf7\x61\x61\x01"s).at("a")->get<std::uint64_t>(), 1u);
}

namespace
{

template <class R>
concept read_from_temporary = requires(R &&r) { *std::move(*r); };

} // namespace

// A view that get gives holds the bytes of the top-level item: it outlives the lazy and the owner that made it, and it
// cannot be read through a temporary.
TEST_CASE("lazy: a view holds the top-level item")
{
    bool released = false;
    std::optional<cbor::ref<std::string_view>> view;
    {
        auto const owner = std::make_shared<buffer_owner>(encoded(M("name"s, "ann"s)), &released);
        auto const name = cbor::lazy::view(owner, std::string_view(owner->bytes))
                              .and_then([](cbor::lazy const &d) { return d.at("name"); })
                              .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
        REQUIRE(name.has_value());
        view.emplace(*name);
    }
    CHECK_FALSE(released);
    CHECK_EQ(**view, "ann"sv);
    view.reset();
    CHECK(released);
    CHECK_FALSE(read_from_temporary<std::expected<cbor::ref<std::string_view>, cbor::error>>);
    CHECK_THROWS_AS((void)cbor::lazy::view(std::shared_ptr<void const>{}, "\x00"sv), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. A view takes an owner and the bytes it
// keeps alive; a string alone, owned or not, does not compile with view. Owner decision: no form takes bytes that
// nothing keeps alive. The test exists because each of these let a view outlive its bytes, and a check of the stored
// pointer refused an owner that holds the bytes.
TEST_CASE("lazy: view checks that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>("\xa1\x61\x61\x63xyz"s);
    auto const viewable = []<class S>(S &&) { return requires(S &&t) { cbor::lazy::view(std::forward<S>(t)); }; };
    CHECK_FALSE(viewable(std::string(*bytes)));
    CHECK_FALSE(viewable(*bytes));
    CHECK(viewable(bytes));
    CHECK_THROWS_AS((void)cbor::lazy::view(std::shared_ptr<std::string const>{}), std::logic_error);
    std::shared_ptr<void const> const holds_nothing(std::shared_ptr<void const>{}, bytes.get());
    CHECK_THROWS_AS((void)cbor::lazy::view(holds_nothing, std::string_view(*bytes)), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy::view(nullptr, std::string_view(*bytes)), std::logic_error);
    auto const name = cbor::lazy::view(bytes)
                          .and_then([](cbor::lazy const &d) { return d.at("a"); })
                          .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "xyz"sv);
}

// take moves a range that owns its bytes into the lazy, so nothing is copied. A range that the caller still names, a
// const range and a range that owns nothing do not compile, because each would either copy or dangle.
TEST_CASE("lazy::take: only an owning rvalue range compiles")
{
    auto const takeable = []<class S>(S &&) { return requires(S &&t) { cbor::lazy::take(std::forward<S>(t)); }; };
    std::string named = "\x78\x20"s + std::string(32, 'x');
    std::string const constant = "\x01"s;
    CHECK(takeable(std::string("\x01")));
    CHECK(takeable(std::vector<char>{'\x01'}));
    CHECK(takeable(std::pmr::string("\x01")));
    CHECK_FALSE(takeable(named));
    CHECK_FALSE(takeable(std::move(constant)));
    CHECK_FALSE(takeable("\x01"));
    CHECK_FALSE(takeable(std::string_view("\x01")));
    CHECK_FALSE(takeable(std::span<char const>(constant)));
    char const *const data = named.data();
    auto const l = cbor::lazy::take(std::move(named));
    REQUIRE(l.has_value());
    CHECK_EQ(l->top_level.encoded.data(), data);
}

// cbor::lazy is an aggregate, so cbor::lazy{} holds no top-level item, and so does the lazy inside a cache
// entry that was not built. Each member read through the null pointer before this test existed; each one now
// refuses it as a wrong use.
TEST_CASE("lazy: every member refuses a lazy that holds no top-level item")
{
    cbor::lazy const none{};
    CHECK_THROWS_AS((void)none.at("a"sv), std::logic_error);
    CHECK_THROWS_AS((void)none.at(0), std::logic_error);
    CHECK_THROWS_AS((void)none.at(cbor::key{0}), std::logic_error);
    CHECK_THROWS_AS((void)none.get<std::uint64_t>(), std::logic_error);
    CHECK_THROWS_AS((void)none.get<cbor::typed_array>(), std::logic_error);
    CHECK_THROWS_AS((void)none.elements(), std::logic_error);
    CHECK_THROWS_AS((void)none.entries(), std::logic_error);
    CHECK_THROWS_AS((void)none.decode(), std::logic_error);
    CHECK_THROWS_AS((void)test::value_of(none), std::logic_error);
}

// A tag 29 in the content of a bignum or a typed array is no byte string, so the get that interprets the tag
// refuses it.
TEST_CASE("lazy: get does not follow a tag 29 in the content of a tag")
{
    auto const root = lazy_of("\x83\xd8\x1c\x41\x05\xc2\xd8\x1d\x00\xd8\x40\xd8\x1d\x00"s);
    CHECK_EQ(root.at(1)->get<std::uint64_t>().error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(root.at(2)->get<cbor::typed_array>().error(), error::inadmissible_type_for_tag_content);
}

// The value category of the bytes says what lazy::copy does. A moved std::string becomes the owner and
// is not copied; an lvalue, a const rvalue and a literal are copied once. The top-level item stays valid after the caller
// reuses or destroys its buffer.
TEST_CASE("lazy: copy moves an rvalue string and copies everything else")
{
    auto text_of = [](cbor::lazy const &l) {
        auto const r = l.get<std::string_view>();
        REQUIRE(r.has_value());
        return std::string(**r);
    };
    std::string const text(40, 't');
    std::string const message = encoded(M("k"s, text));
    auto buffer = std::make_unique<std::string>(message);
    char const *const data = buffer->data();
    auto const moved = cbor::lazy::take(std::move(*buffer));
    REQUIRE(moved.has_value());
    CHECK_EQ(static_cast<void const *>(moved->top_level.encoded.data()), static_cast<void const *>(data));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(text_of(*moved->at("k")), text);

    std::string lvalue = message;
    auto const copied = test::lazy_of_copy(lvalue);
    REQUIRE(copied.has_value());
    CHECK_NE(static_cast<void const *>(copied->top_level.encoded.data()), static_cast<void const *>(lvalue.data()));
    CHECK_EQ(lvalue, message);
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(text_of(*copied->at("k")), text);

    std::string again = message;
    char const *const again_data = again.data();
    auto const from_moved = cbor::lazy::take(std::move(again));
    CHECK_EQ(static_cast<void const *>(from_moved->top_level.encoded.data()), static_cast<void const *>(again_data));
    auto const from_lvalue = test::lazy_of_copy(message);
    CHECK_NE(static_cast<void const *>(from_lvalue->top_level.encoded.data()), static_cast<void const *>(message.data()));

    CHECK_EQ(text_of(*test::lazy_of_copy("\x63" "abc")), "abc");
    char const *const pointer = "\x62" "ab";
    CHECK_EQ(text_of(*test::lazy_of_copy(pointer)), "ab");
    CHECK_EQ(text_of(*test::lazy_of_copy("\x61" "a"sv)), "a");
}

// RFC 8949 5.6.1 says when two keys are equal. Each pair is written from the text of 5.6.1. keys_equivalent
// finds two keys equal exactly when 5.6.1 makes them equal.
TEST_CASE("validity: keys_equivalent follows RFC 8949 5.6.1")
{
    struct pair {
        std::string first;
        std::string second;
        bool equal;
    };
    std::vector<pair> const pairs{
        // Numeric values are distinct unless they are numerically equal.
        {"\x01"s, "\x18\x01"s, true},
        {"\x20"s, "\x38\x00"s, true},
        {"\x01"s, "\x20"s, false},
        {"\x01"s, "\x02"s, false},
        // An integer and a floating-point value are distinct, also when they are numerically equal.
        {"\x01"s, "\xf9\x3c\x00"s, false},
        // -0.0 is equal to 0.0, and a value is equal in every width.
        {"\xf9\x00\x00"s, "\xfb\x80\x00\x00\x00\x00\x00\x00\x00"s, true},
        {"\xf9\x3c\x00"s, "\xfa\x3f\x80\x00\x00"s, true},
        {"\xf9\x3c\x00"s, "\xf9\x40\x00"s, false},
        // Two NaN are equal when their significands are equal after zero-extension at the right to 64 bits.
        // The
        // sign is no part of the significand.
        {"\xf9\x7e\x00"s, "\xfb\x7f\xf8\x00\x00\x00\x00\x00\x00"s, true},
        {"\xf9\x7e\x00"s, "\xf9\xfe\x00"s, true},
        {"\xf9\x7e\x00"s, "\xf9\x7e\x01"s, false},
        {"\xf9\x7c\x00"s, "\xf9\x7e\x00"s, false},
        // Strings are compared byte by byte. A text string is distinct from a byte string of the same bytes.
        {"\x62\x61\x62"s, "\x62\x61\x62"s, true},
        {"\x62\x61\x62"s, "\x62\x61\x63"s, false},
        {"\x61\x61"s, "\x62\x61\x62"s, false},
        {"\x61\x61"s, "\x41\x61"s, false},
        // RFC 8949 3.4.3: a big number that fits 64 bits is the integer of the same value, and leading zeros do
        // not change the value. A tagged value is distinct from an untagged one. Tagged values are equal when
        // the tag numbers and the contents are equal.
        {"\x01"s, "\xc2\x41\x01"s, true},
        {"\xc2\x41\x01"s, "\xc2\x42\x00\x01"s, true},
        {"\xc2\x41\x01"s, "\xc3\x41\x01"s, false},
        {"\xc1\x00"s, "\x00"s, false},
        {"\xc1\x00"s, "\xc1\x18\x00"s, true},
        // Simple values are equal when they have the same value. A simple value is never an integer.
        {"\xf4"s, "\xf4"s, true},
        {"\xf4"s, "\xf5"s, false},
        {"\xf8\x20"s, "\xf8\x20"s, true},
        {"\xf8\x20"s, "\xf8\x21"s, false},
        {"\xf0"s, "\x10"s, false},
        // Arrays are compared element by element.
        {"\x82\x01\x02"s, "\x82\x01\x18\x02"s, true},
        {"\x82\x01\x02"s, "\x82\x02\x01"s, false},
        {"\x81\x01"s, "\x82\x01\x02"s, false},
        // Maps are equal when they have the same set of pairs, in any order. An array is never a map.
        {"\xa2\x01\x02\x03\x04"s, "\xa2\x03\x04\x01\x02"s, true},
        {"\xa1\x01\x02"s, "\xa1\x01\x03"s, false},
        {"\xa1\x01\x02"s, "\xa1\x03\x02"s, false},
        {"\x80"s, "\xa0"s, false},
        // A map with a repeated pair holds fewer pairs than its count says, so it is no map of other pairs.
        {"\xa2\x61k\x01\x61k\x01"s, "\xa2\x61k\x01\x61j\x01"s, false},
        {"\xa2\x61k\x01\x61j\x01"s, "\xa2\x61k\x01\x61k\x01"s, false},
        {"\xa2\x61k\x01\x61k\x02"s, "\xa2\x61k\x02\x61k\x01"s, true},
    };
    for (pair const &p : pairs) {
        std::string_view const first = p.first;
        std::string_view const second = p.second;
        CAPTURE(p.first);
        CAPTURE(p.second);
        auto const equal =
            cbor::validity::keys_equivalent(first, cbor::validity::offset{}, second, cbor::validity::offset{}, 1, 16, cbor::validity::limit_checks<false>{});
        REQUIRE(equal.has_value());
        CHECK_EQ(*equal, p.equal);
    }
}

// RFC 8949 5.6 lets a decoder that is not in a deterministic profile keep one entry of a repeated key. Every
// lookup stops at the first key that matches and gives its value with no error, also when the second key
// stands after it. The entries and decode read such a map with no error.
TEST_CASE("lazy: a repeated key gives the first entry with no error")
{
    std::string const twice = "\xa2\x61\x61\x01\x61\x61\x02"s;
    CHECK_EQ(*lazy_of(twice).at("a")->get<std::uint64_t>(), 1u);
    CHECK_EQ(*lazy_of("\xa3\x61\x61\x01\x61\x62\x02\x61\x61\x03"s).at("a")->get<std::uint64_t>(), 1u);
    CHECK_EQ(*lazy_of("\xa2\x01\x01\x18\x01\x02"s).at(cbor::key{1})->get<std::uint64_t>(), 1u);
    CHECK_EQ(*lazy_of("\xa2\x20\x01\x38\x00\x02"s).at(cbor::key{-1})->get<std::uint64_t>(), 1u);
    CHECK_EQ(*lazy_of("\xa3\x61\x61\x01\x61\x61\x02\x61\x62\x03"s).at("b")->get<std::uint64_t>(), 3u);
    CHECK(lazy_of(twice).entries().has_value());
    cbor::lazy const top = lazy_of(twice);
    CHECK(top.decode().has_value());
    CHECK(test::value_of(lazy_of(twice)).has_value());
    CHECK(test::value_of(lazy_of("\x81\xa2\x01\x00\x18\x01\x00"s)).has_value());
}

// A repeated key at depth 3 and a repeated key of the outer map after a nested map are read with no error.
// A lookup gives the value of the first key.
TEST_CASE("lazy: a repeated key at depth 3 is read with no error")
{
    std::string const inner = "\xa1\x61\x61\xa1\x61\x62\xa3\x61\x63\x01\x61\x64\x02\x61\x64\x03"s;
    std::string const outer = "\xa2\x61\x61\xa1\x61\x62\xa1\x61\x63\x01\x61\x61\x00"s;
    CHECK(test::value_of(lazy_of(inner)).has_value());
    CHECK(test::value_of(lazy_of(outer)).has_value());
    CHECK_EQ(*lazy_of(inner).at("a")->at("b")->at("d")->get<std::uint64_t>(), 2u);
    CHECK(lazy_of(outer).at("a")->at("b").has_value());
    CHECK_EQ(*(cbor::at_path<"$.a.b.d", int>(inner)), 2);
    CHECK(test::query("$..*", lazy_of(inner)).has_value());
    CHECK(test::at_path("$.a.b.d", lazy_of(inner)).has_value());
    CHECK(lazy_of(inner).at("a")->at("b")->decode().has_value());
}

// The same map many times in one array is no repeated key: a key is compared only with the keys of its own
// map. Every reader takes the array.
TEST_CASE("lazy: an array of the same map many times is read with no error")
{
    std::string doc = "\x98\x40"s;
    for (int i = 0; i < 64; ++i)
        doc += "\xa2\x61\x61\x01\x61\x62\x02"s;
    cbor::lazy const top = lazy_of(doc);
    CHECK(top.decode().has_value());
    CHECK(test::value_of(lazy_of(doc)).has_value());
    CHECK(test::query("$..*", lazy_of(doc)).has_value());
    CHECK(test::query("$[*].a", lazy_of(doc)).has_value());
    CHECK_EQ(*(cbor::at_path<"$[63].b", int>(doc)), 2);
    CHECK_EQ(*lazy_of(doc).at(63)->at("a")->get<std::uint64_t>(), 1u);
}

// size and empty answer as the size and empty of a container. Each comes in std::expected, because the bytes
// are not checked before the call.
TEST_CASE("lazy: size and empty")
{
    cbor::lazy const map = lazy_of(encoded(M("a"s, 1, 2, "b"s)));
    CHECK_EQ(map.size().value(), 2u);
    CHECK_FALSE(map.empty().value());

    cbor::lazy const array = lazy_of(encoded(A(1, 2, 3)));
    CHECK_EQ(array.size().value(), 3u);
    CHECK_FALSE(array.empty().value());
    CHECK(lazy_of(encoded(A())).empty().value());
    CHECK(lazy_of("\xa0"s).empty().value());
    CHECK_EQ(lazy_of(encoded(V(1))).size().error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(V("a"s))).empty().error(), error::not_indexable);
    CHECK_EQ(lazy_of("\xd8\x1c\x82\x01\x02"s).size().value(), 2u);
}

// Fault p30: a truncated array or map gave one value more than size(),
// so ranges::distance and the loop disagreed with size().
TEST_CASE("lazy: a cut-off array or map yields exactly size() values")
{
    CHECK_EQ(lazy_of("\x82\x01"s).elements().error(), error::too_little_data);
    auto const one = *lazy_of("\xa1\x61\x61\x61"s).entries();
    CHECK_EQ(std::ranges::distance(one.begin(), one.end()), 1);
    auto const e = *lazy_of("\x83\x01\x19\x00"s).elements();
    std::uint64_t n = 0, with_value = 0;
    for (auto const x : e) {
        ++n;
        with_value += x.has_value();
    }
    CHECK_EQ(n, e.size());
    CHECK_EQ(with_value, 2u);
    CHECK_EQ(static_cast<std::uint64_t>(std::ranges::distance(e)), e.size());
    auto const m = *lazy_of("\xa2\x61" "a\x01\x61"s).entries();
    n = 0;
    with_value = 0;
    for (auto const x : m) {
        ++n;
        with_value += x.has_value();
    }
    CHECK_EQ(n, m.size());
    CHECK_EQ(with_value, 1u);
    CHECK_EQ(static_cast<std::uint64_t>(std::ranges::distance(m)), m.size());
}

// Fault p09: a head that claims 2^64-1 elements gave ssize -1 and
// distance -1, because size() came from the head with no check.
TEST_CASE("lazy: a head that claims more items than bytes is too little data")
{
    CHECK_EQ(lazy_of("\x9b\xff\xff\xff\xff\xff\xff\xff\xff"s).elements().error(), error::too_little_data);
    CHECK_EQ(lazy_of("\xbb\xff\xff\xff\xff\xff\xff\xff\xff"s).entries().error(), error::too_little_data);
    CHECK_EQ(lazy_of("\xbb\xff\xff\xff\xff\xff\xff\xff\xff\x61" "a\x01"s).at("a").error(),
             error::too_little_data);
}

// Fault p14: each resolve of tag 24 makes a new top-level item, and the
// iterators compared that pointer, so two walks to one pair differed.
TEST_CASE("lazy: iterators into a map inside tag 24 compare equal at the same pair")
{
    auto const l = lazy_of("\xd8\x18\x47\xa2\x61" "a\x01\x61" "b\x02"s);
    auto const f1 = std::ranges::next(l.entries()->begin());
    auto const f2 = std::ranges::next(l.entries()->begin());
    CHECK(f1 == f2);
    CHECK(f1 != l.entries()->begin());
}

// The count check of elements() and entries() reads the argument of the
// head, and an indefinite length has no count, so it is refused first.
TEST_CASE("lazy: elements and entries refuse indefinite length")
{
    CHECK_EQ(lazy_of("\x9f\x01\xff"s).elements().error(), error::indefinite_length);
    CHECK_EQ(lazy_of("\xbf\x61" "a\x01\xff"s).entries().error(), error::indefinite_length);
}

// The owner decided on 2026-10-13 that a lazy holds only its bytes and its offset, so several threads read one
// lazy at the same time. Two threads read one root lazy through every reader; a thread sanitizer build reports
// any state that they share. The document holds tags 28 and 29, which a lazy reads as plain tags, and a typed
// array, whose element is a lazy of its own kind.
TEST_CASE("lazy: two threads read one root lazy")
{
    constexpr std::size_t threads = 2;
    std::string const s("\x84\xd8\x1c\x65hello\xd8\x1d\x00\xa1\x61k\xd8\x1d\x00\xd8\x40\x42\x01\x02", 23);
    cbor::lazy const l = *test::lazy_of_copy(s);
    std::array<std::array<bool, 6>, threads> right{};
    {
        std::vector<std::jthread> workers;
        for (std::size_t t = 0; t < threads; ++t)
            workers.emplace_back([&l, &right, t] {
                auto const text = l.at(std::size_t{0}).and_then([](cbor::lazy const &r) { return r.get<std::string_view>(); });
                auto const index = l.at_path<"$[1]", std::uint64_t>();
                auto const named = l.at(std::size_t{2})
                                       .and_then([](cbor::lazy const &m) { return m.at("k"); })
                                       .and_then([](cbor::lazy const &r) { return r.decode(); });
                auto const count = l.elements().transform([](cbor::lazy_elements const &e) {
                    return std::ranges::distance(e.begin(), e.end());
                });
                auto const pairs = l.at(std::size_t{2})
                                       .and_then([](cbor::lazy const &m) { return m.entries(); })
                                       .transform([](cbor::lazy_entries const &e) {
                                           return std::ranges::distance(e.begin(), e.end());
                                       });
                auto const element = l.at_path<"$[3][1]", std::uint64_t>();
                bool const reference = named.has_value() && (*named)->type == cbor::type::tag &&
                                       (*named)->tag_number().value() == 29 && (*named)->tag_content().value()->argument().value() == 0;
                right[t] = {text.has_value() && **text == "hello", index == 0u, reference, count == 4, pairs == 1,
                            element == 2u};
            });
    }
    for (auto const &r : right)
        for (std::size_t i = 0; i < r.size(); ++i) {
            CAPTURE(i);
            CHECK(r[i]);
        }
}

// A tag 28 may hold a tag 29, as CBOR::XS, cbor2 and cbor-x accept. A lazy passes both tags and reads the index:
// in [28(1), 28(29(0)), 29(1)] the elements read 1, 0 and 1. A chain of such values, 28(1) then 28(29(k - 1)) for
// each k, reads k - 1 at element k, and reading all of them is linear.
TEST_CASE("lazy: a tag 28 that holds a tag 29 reads the index")
{
    std::string const small("\x83\xd8\x1c\x01\xd8\x1c\xd8\x1d\x00\xd8\x1d\x01", 12);
    auto const l = *test::lazy_of_copy(small);
    std::array<std::int64_t, 3> const expected{1, 0, 1};
    for (std::size_t i = 0; i < 3; ++i)
        CHECK_EQ(*l.at(i).and_then([](cbor::lazy const &e) { return e.get<std::int64_t>(); }), expected[i]);
    CHECK_EQ(test::lazy_of_copy(std::string("\xd8\x1c\xd8\x1d\x00", 5))->get<std::int64_t>(), 0);

    constexpr std::size_t n = 20000;
    std::string chain("\x9a", 1);
    for (int b = 3; b >= 0; --b)
        chain += static_cast<char>((n >> (8 * b)) & 0xff);
    chain += "\xd8\x1c\x01";
    for (std::size_t k = 1; k < n; ++k) {
        chain += "\xd8\x1c\xd8\x1d\x1a";
        for (int b = 3; b >= 0; --b)
            chain += static_cast<char>(((k - 1) >> (8 * b)) & 0xff);
    }
    auto const all = test::lazy_of_copy(chain)->elements();
    REQUIRE(all.has_value());
    std::size_t right = 0;
    std::int64_t k = 0;
    for (auto const e : *all) {
        right += e.and_then([](cbor::lazy const &x) { return x.get<std::int64_t>(); }).value_or(-1) == (k == 0 ? 1 : k - 1);
        ++k;
    }
    CHECK_EQ(right, n);
}

// A step into an element that is not in the input gave a lazy with no
// error, and the error came only from the next read. A caller that
// checked only the step went on with a value that does not exist. The
// step now reads the head of its target and gives too_little_data.
TEST_CASE("lazy: a step to an element past the end of the input fails at once")
{
    CHECK_EQ(test::lazy_of_copy(std::string("\x82\x01", 2))->at(std::size_t{1}).error(), error::too_little_data);
    CHECK_EQ(test::lazy_of_copy(std::string("\xa1\x61k", 3))->at("k").error(), error::too_little_data);
    CHECK_EQ(test::lazy_of_copy(std::string("\xa1\x01", 2))->at(cbor::key{1}).error(), error::too_little_data);
    CHECK_EQ(test::lazy_of_copy(std::string("\x82\x01\x19\x01", 4))->at(std::size_t{1}).error(), error::too_little_data);
}

// size() returned the count of the head, also when the bytes after the
// head cannot hold that many items. A caller who reserves with size()
// then reserved what the input claimed. size() now checks the count
// against the bytes that are left, as elements() and entries() do.
TEST_CASE("lazy size refuses a count that the bytes cannot hold")
{
    using namespace std::string_literals;
    for (std::string const &bytes : {"\x82\x01"s, "\x98\xff"s, "\x9b\x7f\xff\xff\xff\xff\xff\xff\xff\x01"s, "\xa1\x01"s,
                                    "\xbb\xff\xff\xff\xff\xff\xff\xff\xff"s}) {
        auto const doc = test::lazy_of_copy(bytes);
        REQUIRE(doc.has_value());
        CHECK_EQ(doc->size().error(), cbor::error::too_little_data);
    }
    auto const pair = test::lazy_of_copy("\x82\x01\x02"s);
    REQUIRE(pair.has_value());
    CHECK_EQ(pair->size().value(), 2);
}

// cbor::limits = {} was ambiguous between the deleted copy assignment
// and the assignment of limit_values. It now sets every limit to its
// default.
TEST_CASE("cbor::limits = {} sets the defaults")
{
    test::limits_guard const guard{{.nesting_depth = 4}};
    cbor::limits = {};
    CHECK_EQ(cbor::limits.load().nesting_depth, cbor::limit_values{}.nesting_depth);
}

// Found by fuzz-path on the input d0. RFC 8949 3.4: a tag is followed by its content, so d0 alone ends before
// its content and is not well-formed. A step of a lazy gave not_indexable, and the typed read gave
// too_little_data. A step now passes a tag that it does not read, as the typed read and lazy::get do (RFC 8949
// 6.1), so both read the missing content and give too_little_data.
TEST_CASE("lazy: a step over a tag with no content gives too_little_data")
{
    std::string const doc = "\xd0"s;
    CHECK_EQ(cbor::at_path<"$[0]", int>(doc).error(), error::too_little_data);
    CHECK_EQ(lazy_of(doc).at(0).error(), error::too_little_data);
    CHECK_EQ(lazy_of(doc).at("a").error(), error::too_little_data);
    CHECK_EQ(lazy_of(doc).at(cbor::key{0}).error(), error::too_little_data);
    CHECK_EQ(lazy_of(doc).size().error(), error::too_little_data);
    CHECK_EQ(lazy_of(doc).elements().error(), error::too_little_data);
    CHECK_EQ(lazy_of(doc).entries().error(), error::too_little_data);
}

// The same rule on a well-formed item: tag 1000 over [1, 2] is passed by a step as by the typed read.
TEST_CASE("lazy: a step passes a tag that it does not read")
{
    std::string const doc = "\xd9\x03\xe8\x82\x01\x02"s;
    CHECK_EQ(cbor::at_path<"$[1]", int>(doc), 2);
    CHECK_EQ(*lazy_of(doc).at(1)->get<int>(), 2);
    CHECK_EQ(*lazy_of(doc).size(), 2u);
}

namespace two_forms
{

// A text of 100 bytes, longer than any small-string buffer, so a moved std::string keeps its heap bytes.
std::string long_text()
{
    return "\x78\x64"s + std::string(100, 'a');
}

char const *text_data(std::expected<cbor::lazy, error> const &l)
{
    auto const text = l->get<std::string_view>();
    return (**text).data();
}

} // namespace two_forms

// Owner decision: bytes enter the library in two forms only. An lvalue std::string goes to copy(std::string_view)
// and is copied; the caller's string stays as it was.
TEST_CASE("lazy::copy: an lvalue std::string is copied")
{
    std::string const bytes = two_forms::long_text();
    auto const l = test::lazy_of_copy(bytes);
    REQUIRE(l.has_value());
    CHECK_NE(two_forms::text_data(l), bytes.data() + 2);
    CHECK_EQ(bytes, two_forms::long_text());
}

// An rvalue std::string goes to copy(std::string&&) and is moved: the lazy reads the same heap bytes.
TEST_CASE("lazy::copy: an rvalue std::string is moved")
{
    std::string bytes = two_forms::long_text();
    char const *const data = bytes.data();
    auto const l = cbor::lazy::take(std::move(bytes));
    REQUIRE(l.has_value());
    CHECK_EQ(two_forms::text_data(l), data + 2);
}

// view copies nothing and keeps the owner alive after the caller drops it.
TEST_CASE("lazy::view: the lazy reads the bytes of the owner and keeps it")
{
    auto owner = std::make_shared<std::string const>(two_forms::long_text());
    char const *const data = owner->data();
    auto const l = cbor::lazy::view(owner);
    REQUIRE(l.has_value());
    CHECK_EQ(two_forms::text_data(l), data + 2);
    CHECK_GT(owner.use_count(), 1);
    owner.reset();
    auto const text = l->get<std::string_view>();
    CHECK_EQ((**text).size(), 100u);
}

// The typed read of a path has the same two forms for a type that views the bytes.
TEST_CASE("at_path: the copy form and the view form of a text")
{
    auto const owner = std::make_shared<std::string const>("\x81"s + two_forms::long_text());
    auto const viewed = cbor::at_path<"$[0]", std::string_view>(owner);
    REQUIRE(viewed.has_value());
    CHECK_EQ((**viewed).data(), owner->data() + 3);
    auto const copied = cbor::at_path<"$[0]", std::string_view>(std::string(*owner));
    REQUIRE(copied.has_value());
    CHECK_NE((**copied).data(), owner->data() + 3);
    CHECK_EQ(**copied, **viewed);
    std::string moved = *owner;
    char const *const data = moved.data();
    auto const taken = cbor::at_path<"$[0]", std::string_view>(std::move(moved));
    REQUIRE(taken.has_value());
    CHECK_EQ((**taken).data(), data + 3);
}

// RFC 8949 3: each data item has at least its initial byte. A step into an array or a map whose count is larger than
// the bytes left is too_little_data before any element is read, by owner decision, as size() gives.
TEST_CASE("lazy: a count larger than the bytes left is too_little_data before the elements")
{
    for (std::string const &wire : {"\x8c\xff"s, "\x95\xf8\x04"s, "\x95\xbf"s, "\x88\x00\x7f"s, "\x87\xd8\x1c\x1f"s,
                                   "\x85\xd8\x1c\xd8\x1c"s}) {
        CAPTURE(wire);
        CHECK_EQ(lazy_of(wire).size().error(), error::too_little_data);
        CHECK_EQ(lazy_of(wire).at(0).error(), error::too_little_data);
        CHECK_EQ(lazy_of(wire).elements().error(), error::too_little_data);
    }
    CHECK_EQ(lazy_of("\xa2\xff\x00\x00"s).at("a").error(), error::too_little_data);
    CHECK_EQ(lazy_of("\xa2\xff\x00\x00"s).at(cbor::key{std::int64_t{0}}).error(), error::too_little_data);
    CHECK_EQ(lazy_of("\xa2\xff\x00\x00"s).entries().error(), error::too_little_data);
}

namespace typed_array_index
{

std::string typed_array_of(int const tag, std::string_view const bytes)
{
    std::string wire = "\xd8"s + static_cast<char>(tag);
    if (bytes.size() < 24)
        wire += static_cast<char>(0x40 + bytes.size());
    else
        wire += "\x58"s + static_cast<char>(bytes.size());
    return wire + std::string(bytes);
}

} // namespace typed_array_index

// Owner decision: an array is an array. RFC 8746 2.1 defines tags 64 to 87, except 76, as typed arrays, so a lazy
// step indexes them as at_path does. Each row holds two elements; the second value is written from the table of
// RFC 8746 2.1 (endianness, sign and width of the tag).
TEST_CASE("lazy: an index reads an element of each typed array")
{
    using typed_array_index::typed_array_of;
    struct integer_row {
        int tag;
        std::string_view bytes;
        std::int64_t second;
    };
    integer_row const integers[] = {
        {64, "\x00\xff"sv, 255},
        {65, "\x00\x00\x01\x02"sv, 258},
        {66, "\x00\x00\x00\x00\x01\x02\x03\x04"sv, 0x01020304},
        {67, "\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08"sv, 0x0102030405060708},
        {68, "\x00\xff"sv, 255},
        {69, "\x00\x00\x02\x01"sv, 258},
        {70, "\x00\x00\x00\x00\x04\x03\x02\x01"sv, 0x01020304},
        {71, "\x00\x00\x00\x00\x00\x00\x00\x00\x08\x07\x06\x05\x04\x03\x02\x01"sv, 0x0102030405060708},
        {72, "\x00\xff"sv, -1},
        {73, "\x00\x00\xff\xfe"sv, -2},
        {74, "\x00\x00\x00\x00\xff\xff\xff\xfd"sv, -3},
        {75, "\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xfc"sv, -4},
        {77, "\x00\x00\xfe\xff"sv, -2},
        {78, "\x00\x00\x00\x00\xfd\xff\xff\xff"sv, -3},
        {79, "\x00\x00\x00\x00\x00\x00\x00\x00\xfc\xff\xff\xff\xff\xff\xff\xff"sv, -4},
    };
    for (auto const &[tag, bytes, second] : integers) {
        CAPTURE(tag);
        auto const l = lazy_of(typed_array_of(tag, bytes));
        CHECK_EQ(*l.size(), 2u);
        CHECK_EQ(*l.at(0)->get<std::int64_t>(), 0);
        CHECK_EQ(*l.at(1)->get<std::int64_t>(), second);
        CHECK_EQ(l.at(2).error(), error::index_out_of_bounds);
        CHECK_EQ(std::ranges::distance(*l.elements()), 2);
        CHECK_EQ(*(*std::ranges::next(l.elements()->begin()))->get<std::int64_t>(), second);
    }
    struct float_row {
        int tag;
        std::string_view bytes;
    };
    float_row const floats[] = {
        {80, "\x00\x00\x3e\x00"sv},
        {81, "\x00\x00\x00\x00\x3f\xc0\x00\x00"sv},
        {82, "\x00\x00\x00\x00\x00\x00\x00\x00\x3f\xf8\x00\x00\x00\x00\x00\x00"sv},
        {84, "\x00\x00\x00\x3e"sv},
        {85, "\x00\x00\x00\x00\x00\x00\xc0\x3f"sv},
        {86, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf8\x3f"sv},
    };
    for (auto const &[tag, bytes] : floats) {
        CAPTURE(tag);
        auto const l = lazy_of(typed_array_of(tag, bytes));
        CHECK_EQ(*l.size(), 2u);
        CHECK_EQ(*l.at(1)->get<double>(), 1.5);
        CHECK_EQ(l.at(1)->get<std::int64_t>().error(), error::incorrect_type);
    }
    // A float of 128 bits has no C++ type that the library reads. The index selects the element, and the read
    // of its value is incorrect_type.
    for (int const tag : {83, 87}) {
        CAPTURE(tag);
        auto const l = lazy_of(typed_array_of(tag, std::string(32, '\0')));
        CHECK_EQ(*l.size(), 2u);
        auto const element = l.at(1);
        REQUIRE(element.has_value());
        CHECK_EQ(element->get<double>().error(), error::incorrect_type);
        CHECK_EQ(element->get<std::int64_t>().error(), error::incorrect_type);
    }
    // An element is a number and no container, and its type and its decoded item are those of the number.
    auto const element = lazy_of(typed_array_of(81, "\x00\x00\x00\x00\x3f\xc0\x00\x00"sv)).at(1);
    REQUIRE(element.has_value());
    CHECK_EQ(*element->type(), cbor::type::single_float);
    CHECK_EQ(element->at(0).error(), error::not_indexable);
    auto const element_tree = element->decode();
    CHECK_EQ((*element_tree)->type, cbor::type::single_float);
    auto const negative = lazy_of(typed_array_of(72, "\x00\xff"sv)).at(1);
    REQUIRE(negative.has_value());
    CHECK_EQ(*negative->type(), cbor::type::negative_integer);
    // Tag 76 is reserved in RFC 8746 2.1, so it is no typed array.
    CHECK_EQ(lazy_of(typed_array_of(76, "\x00\x00"sv)).at(0).error(), error::not_indexable);
}

// The raw bytes of an element are the input bytes in place, for each of the 23 typed array tags of RFC 8746 2.1.
// They are the only read of a float of 128 bits where C++ has no std::float128_t.
TEST_CASE("lazy: an element of each typed array gives its raw bytes")
{
    using typed_array_index::typed_array_of;
    for (int tag = 64; tag <= 87; ++tag) {
        if (tag == 76)
            continue;
        CAPTURE(tag);
        std::size_t const size = std::size_t{1} << ((tag >> 4 & 1) + (tag & 3));
        std::string bytes(2 * size, '\0');
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<char>(i + 1);
        auto const wire = typed_array_of(tag, bytes);
        auto const l = lazy_of(wire);
        auto const element = l.at(1);
        REQUIRE(element.has_value());
        auto const raw = element->get<std::span<std::byte const>>();
        REQUIRE(raw.has_value());
        std::span<std::byte const> const view = **raw;
        CHECK_EQ(view.size(), size);
        CHECK(std::ranges::equal(view, std::as_bytes(std::span(bytes).subspan(size))));
        auto const &encoded = l.top_level.encoded;
        CHECK(std::ranges::distance(std::as_bytes(std::span(encoded)).data(), view.data()) >= 0);
        CHECK(std::ranges::distance(view.data(), std::as_bytes(std::span(encoded)).data() + encoded.size()) >= 0);
    }
}

// CBOR has no data item of 128 bits for a float (RFC 8949 3.3), so decode() of such an element is incorrect_type.
// The bit patterns below are IEEE 754 binary128: sign bit, 15 bits of exponent with bias 16383, 112 bits of
// fraction. 1.0 is 3fff 0..0, -2.0 is c000 0..0, infinity is 7fff 0..0, a quiet NaN is 7fff 8000 0..0.
TEST_CASE("lazy: an element of a float128 typed array")
{
    using typed_array_index::typed_array_of;
    std::string const zeros(12, '\0');
    std::string const one = "\x3f\xff\x00\x00"s + zeros;
    std::string const minus_two = "\xc0\x00\x00\x00"s + zeros;
    std::string const infinity = "\x7f\xff\x00\x00"s + zeros;
    std::string const nan = "\x7f\xff\x80\x00"s + zeros;
    std::string const big_endian = one + minus_two + infinity + nan;
    std::string little_endian;
    for (std::string const &v : {one, minus_two, infinity, nan})
        little_endian += std::string(v.rbegin(), v.rend());
    for (auto const &[tag, bytes] : {std::pair{83, big_endian}, std::pair{87, little_endian}}) {
        CAPTURE(tag);
        auto const l = lazy_of(typed_array_of(tag, bytes));
        CHECK_EQ(*l.size(), 4u);
        for (std::size_t i = 0; i < 4; ++i) {
            auto const element = l.at(i);
            REQUIRE(element.has_value());
            CHECK_EQ(element->decode().error(), error::incorrect_type);
            CHECK_EQ(element->get<double>().error(), error::incorrect_type);
            auto const raw = element->get<std::span<std::byte const>>();
            REQUIRE(raw.has_value());
            CHECK(std::ranges::equal(**raw, std::as_bytes(std::span(bytes).subspan(16 * i, 16))));
        }
#ifdef __STDCPP_FLOAT128_T__
        CHECK_EQ(*l.at(0)->get<std::float128_t>(), std::float128_t{1});
        CHECK_EQ(*l.at(1)->get<std::float128_t>(), std::float128_t{-2});
        CHECK_EQ(*l.at(2)->get<std::float128_t>(), std::numeric_limits<std::float128_t>::infinity());
        auto const n = *l.at(3)->get<std::float128_t>();
        CHECK(n != n);
#endif
    }
#ifdef __STDCPP_FLOAT128_T__
    // A float128 is read only from an element of tag 83 or 87; every other item has no such value.
    CHECK_EQ(lazy_of(typed_array_of(82, std::string(8, '\0'))).at(0)->get<std::float128_t>().error(),
             error::incorrect_type);
    CHECK_EQ(lazy_of("\xf9\x3c\x00"s).get<std::float128_t>().error(), error::incorrect_type);
#endif
}

// at_path gives a float128 element with the same value as lazy::get, both from the encoded bytes and from a
// lazy. The walk over the encoded bytes reads no element of 128 bits, so this test exists to show that the
// value still comes back and that every other item stays incorrect_type.
TEST_CASE("path: at_path reads an element of a float128 typed array")
{
#ifdef __STDCPP_FLOAT128_T__
    using typed_array_index::typed_array_of;
    std::string const zeros(12, '\0');
    std::string const one = "\x3f\xff\x00\x00"s + zeros;
    std::string const minus_two = "\xc0\x00\x00\x00"s + zeros;
    std::string const little_endian = std::string(one.rbegin(), one.rend()) + std::string(minus_two.rbegin(), minus_two.rend());
    for (auto const &[tag, bytes] : {std::pair{83, one + minus_two}, std::pair{87, little_endian}}) {
        CAPTURE(tag);
        std::string const encoded = typed_array_of(tag, bytes);
        CHECK_EQ(*cbor::at_path<"$[0]", std::float128_t>(std::string_view(encoded)), std::float128_t{1});
        CHECK_EQ(*cbor::at_path<"$[1]", std::float128_t>(std::string_view(encoded)), std::float128_t{-2});
        CHECK_EQ(*lazy_of(encoded).at_path<"$[1]", std::float128_t>(), std::float128_t{-2});
        CHECK_EQ(cbor::at_path<"$[2]", std::float128_t>(std::string_view(encoded)).error(),
                 error::index_out_of_bounds);
    }
    std::string const float64 = typed_array_of(82, std::string(8, '\0'));
    CHECK_EQ(cbor::at_path<"$[0]", std::float128_t>(std::string_view(float64)).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$", std::float128_t>("\xf9\x3c\x00"sv).error(), error::incorrect_type);
#endif
}

// A typed array whose byte string claims more bytes than the input holds is too_little_data at the step; a length
// that is not a multiple of the element size is refused as RFC 8746 2 requires.
TEST_CASE("lazy: a cut typed array is too_little_data")
{
    auto const cut = lazy_of("\xd8\x41\x44\x00\x01"s);
    CHECK_EQ(cut.at(0).error(), error::too_little_data);
    CHECK_EQ(cut.size().error(), error::too_little_data);
    CHECK_EQ(cut.elements().error(), error::too_little_data);
    CHECK_EQ(lazy_of("\xd8\x41\x43\x00\x01\x02"s).at(0).error(), error::inadmissible_type_for_tag_content);
}

// A typed array inside tag 24 and a typed array under tag 28 are indexed like a typed array in place. A tag 29 is
// a plain tag over its index, so it has no element.
TEST_CASE("lazy: an index reads a typed array through tag 24 and tag 28")
{
    CHECK_EQ(*lazy_of("\xd8\x18\x45\xd8\x40\x42\x01\x02"s).at(1)->get<int>(), 2);
    auto const doc = lazy_of("\x82\xd8\x1c\xd8\x40\x42\x01\x02\xd8\x1d\x00"s);
    auto const marked = doc.at(0);
    REQUIRE(marked.has_value());
    CHECK_EQ(*marked->at(1)->get<int>(), 2);
    CHECK_EQ(*marked->size(), 2u);
    CHECK_EQ(doc.at(1)->at(1).error(), error::not_indexable);
}

// operator[] is the same call as at, so that a lazy reads as a container. It gives std::expected and does not
// throw, because the bytes are not checked before the call and a missing key is a value, not a fault of ours.
TEST_CASE("lazy: operator[] gives what at gives")
{
    cbor::lazy const map = lazy_of(encoded(M("a"s, A(1, 2), 2, "two"s)));
    CHECK(value_at(*map["a"]) == value_at(*map.at("a")));
    CHECK(value_at(*map[cbor::key{2}]) == V("two"s));
    CHECK(value_at(*(*map["a"])[std::size_t{1}]) == V(2));
    CHECK_EQ(map["z"].error(), error::key_not_found);
    CHECK_EQ(map["z"].error(), map.at("z").error());
    cbor::lazy const array = lazy_of(encoded(A(1, 2)));
    CHECK(value_at(*array[std::size_t{0}]) == V(1));
    CHECK_EQ(array["a"].error(), error::not_indexable);
    CHECK_EQ(array[std::size_t{2}].error(), array.at(std::size_t{2}).error());
    cbor::lazy const cut = lazy_of("\xa2\x61" "a\x01\x61"s);
    CHECK_EQ(cut["b"].error(), cut.at("b").error());
}

// lazy does not reject a map with a repeated key (RFC 8949 5.6 leaves that to the decoder). Every way to read
// a key gives the first pair, so that the result does not depend on the form of the call.
TEST_CASE("lazy: a repeated key gives the first pair")
{
    cbor::lazy const twice = lazy_of("\xa3\x61" "a\x01\x61" "b\x02\x61" "a\x03"s);
    CHECK(value_at(*twice.at("a")) == V(1));
    CHECK(value_at(*twice["a"]) == V(1));
    CHECK(value_at(*twice.at_path(*cbor::jsonpath::compile<cbor::lazy>("$.a"))) == V(1));
    CHECK_EQ(twice.at_path<"$.a", int>(), 1);
    cbor::lazy const numbers = lazy_of("\xa3\x01\x01\x01\x02\x01\x03"s);
    CHECK(value_at(*numbers[cbor::key{1}]) == V(1));
}

// RFC 9535 2.3.5.1: a single result needs a singular query. compile refuses any other query for every
// result type that is not cbor::nodelist, and a nodelist takes both.
TEST_CASE("lazy: compile refuses a query that is not singular for a single result")
{
    auto const each = []<class T>() {
        CHECK_EQ(cbor::jsonpath::compile<T>("$.a[*]").error(), error::not_singular_query);
        CHECK_EQ(cbor::jsonpath::compile<T>("$..a").error(), error::not_singular_query);
        CHECK(cbor::jsonpath::compile<T>("$.a[0]").has_value());
    };
    each.template operator()<int>();
    each.template operator()<std::uint64_t>();
    each.template operator()<double>();
    each.template operator()<bool>();
    each.template operator()<std::nullptr_t>();
    each.template operator()<cbor::simple_value>();
    each.template operator()<std::string_view>();
    each.template operator()<std::span<std::byte const>>();
    each.template operator()<cbor::typed_array>();
    each.template operator()<cbor::lazy>();
    CHECK(cbor::jsonpath::compile<cbor::nodelist>("$.a[*]").has_value());
    CHECK(cbor::jsonpath::compile<cbor::nodelist>("$.a[0]").has_value());
}

// The first paths a user tries that are not JSONPath give invalid_path, for a single result and for a nodelist.
TEST_CASE("lazy: compile gives invalid_path for a text that is not a query")
{
    for (std::string_view const text : {""sv, "$["sv, "a"sv, "$.a["sv}) {
        CHECK_EQ(cbor::jsonpath::compile<cbor::nodelist>(text).error(), error::invalid_path);
        CHECK_EQ(cbor::jsonpath::compile<cbor::lazy>(text).error(), error::invalid_path);
    }
}

namespace
{
template <class T>
constexpr bool bytes_at_path_compiles = requires(
    std::string_view const encoded, cbor::jsonpath::query<T> const &e) { cbor::at_path(encoded, e); };
}

// A lazy or a nodelist over a bare string_view would read the buffer of the caller with no owner, so that form does
// not compile; a scalar is copied out, and a view gets a ref with a copy of the bytes.
TEST_CASE("lazy: at_path over bare bytes compiles for a scalar only")
{
    CHECK(bytes_at_path_compiles<int>);
    CHECK(bytes_at_path_compiles<double>);
    CHECK(bytes_at_path_compiles<bool>);
    CHECK(bytes_at_path_compiles<std::nullptr_t>);
    CHECK(bytes_at_path_compiles<cbor::simple_value>);
    CHECK_FALSE(bytes_at_path_compiles<std::string_view>);
    CHECK_FALSE(bytes_at_path_compiles<std::span<std::byte const>>);
    CHECK_FALSE(bytes_at_path_compiles<cbor::typed_array>);
    CHECK_FALSE(bytes_at_path_compiles<cbor::lazy>);
    CHECK_FALSE(bytes_at_path_compiles<cbor::nodelist>);
}

// A query is parsed once and applied many times: to two documents, twice to one, and in each form of
// at_path. The literal form gives the same nodelist.
TEST_CASE("lazy: one query applies to many documents")
{
    std::string const one = encoded(M("a"s, A(1, 2)));
    std::string const two = encoded(M("a"s, A(3, 4, 5)));
    auto const all = *cbor::jsonpath::compile<cbor::nodelist>("$.a[*]");
    auto const first = *cbor::jsonpath::compile<int>("$.a[0]");
    cbor::lazy const l1 = *test::lazy_of_copy(one);
    cbor::lazy const l2 = *test::lazy_of_copy(two);
    CHECK_EQ(l1.at_path(all)->size(), 2u);
    CHECK_EQ(l2.at_path(all)->size(), 3u);
    CHECK(*test::values_of(l1.at_path(all)) == *test::values_of(l1.at_path(all)));
    CHECK(*test::values_of(l1.at_path(all)) == *test::values_of(l1.at_path<"$.a[*]", cbor::nodelist>()));
    CHECK_EQ(*l1.at_path(first), 1);
    CHECK_EQ(*l2.at_path(first), 3);
    CHECK_EQ(*cbor::at_path(one, first), 1);
    CHECK_EQ(*cbor::at_path(two, first), 3);
    auto const shared = std::make_shared<std::string const>(two);
    CHECK_EQ(cbor::at_path(shared, all)->size(), 3u);
    CHECK_EQ(cbor::at_path(std::string(two), all)->size(), 3u);
    auto const node = cbor::at_path(shared, *cbor::jsonpath::compile<cbor::lazy>("$.a[2]"));
    CHECK(test::value_of(node) == V(5));
    std::string const text = encoded(M("s"s, "x"s));
    auto const s = *cbor::jsonpath::compile<std::string_view>("$.s");
    auto const from_view = cbor::at_path(std::string(text), s);
    CHECK_EQ(**from_view, "x"sv);
    auto const from_string = cbor::at_path(std::string(text), s);
    CHECK_EQ(**from_string, "x"sv);
    auto const from_lazy = test::lazy_of_copy(text)->at_path(s);
    CHECK_EQ(**from_lazy, "x"sv);
}

// A copy owns its data: it is applied after the original is gone. The run under the address sanitizer shows that
// no read reaches freed memory.
TEST_CASE("lazy: a copy of a query outlives the original")
{
    std::string const doc = encoded(M("k"s, A(7)));
    std::optional<cbor::jsonpath::query<int>> original = *cbor::jsonpath::compile<int>("$['k'][0]");
    cbor::jsonpath::query<int> const copy = *original;
    original.reset();
    CHECK_EQ(*cbor::at_path(doc, copy), 7);
}

// A moved-from query holds no parsed path. Each apply sees that state and throws std::logic_error, as for any
// other wrong use; the moved-to query applies as before.
TEST_CASE("lazy: a moved-from query is a wrong use")
{
    std::string const doc = encoded(M("a"s, "x"s));
    cbor::lazy const l = *test::lazy_of_copy(doc);
    auto number = *cbor::jsonpath::compile<int>("$.a");
    auto const moved_number = std::move(number);
    CHECK_THROWS_AS((void)cbor::at_path(doc, number), std::logic_error);
    CHECK_THROWS_AS((void)l.at_path(number), std::logic_error);
    CHECK_EQ(cbor::at_path(doc, moved_number).error(), error::incorrect_type);
    auto text = *cbor::jsonpath::compile<std::string_view>("$.a");
    auto other = *cbor::jsonpath::compile<std::string_view>("$.b");
    other = std::move(text);
    CHECK_THROWS_AS((void)cbor::at_path(std::string(doc), text), std::logic_error);
    auto const read = cbor::at_path(std::string(doc), other);
    CHECK_EQ(**read, "x"sv);
    auto nodes = *cbor::jsonpath::compile<cbor::nodelist>("$.*");
    auto const moved_nodes = std::move(nodes);
    CHECK_THROWS_AS((void)l.at_path(nodes), std::logic_error);
    CHECK_EQ(l.at_path(moved_nodes)->size(), 1u);
}

// The nesting depth limit can change between compile and an apply. Each apply checks the deepest
// recursion of the parse against the limit in force, for the run time and for the literal form.
TEST_CASE("lazy: an apply checks the depth of the query against the limit in force")
{
    std::string const doc = encoded(A(1));
    cbor::lazy const l = *test::lazy_of_copy(doc);
    auto const e = *cbor::jsonpath::compile<cbor::nodelist>("$[?@[?@[?@.a]]]");
    {
        test::limits_guard const low{{.nesting_depth = 3}};
        CHECK_EQ(l.at_path(e).error(), error::nesting_depth_exceeded);
        CHECK_EQ((l.at_path<"$[?@[?@[?@.a]]]", cbor::nodelist>().error()), error::nesting_depth_exceeded);
    }
    CHECK(l.at_path(e)->empty());
}

// A query has no mutable member, so threads apply one const query at once. The run under the thread
// sanitizer shows that no access races.
TEST_CASE("lazy: threads apply one query at once")
{
    std::string const doc = encoded(M("a"s, A(1, 2, 3)));
    cbor::lazy const l = *test::lazy_of_copy(doc);
    auto const e = *cbor::jsonpath::compile<cbor::nodelist>("$.a[?@ > 1]");
    std::array<std::size_t, 4> found{};
    {
        std::array<std::jthread, 4> threads;
        for (std::size_t i = 0; i < threads.size(); ++i)
            threads[i] = std::jthread([&l, &e, &found, i] { found[i] = l.at_path(e)->size(); });
    }
    for (std::size_t const n : found)
        CHECK_EQ(n, 2u);
}

// A container count above 2^32 - 1 does not fit a 32-bit size_t. The count must be compared with the bytes left
// before it is narrowed, so that every target gives the error of the 64-bit build.
TEST_CASE("lazy: a decode checks a container count that does not fit 32 bits")
{
    std::string const array_count = "\x82\x9b\x00\x00\x00\x01\x00\x00\x00\x00\x01"s;
    std::string const map_count = "\x82\xbb\x00\x00\x00\x00\x80\x00\x00\x00\x01"s;
    CHECK_EQ(test::lazy_of_copy(array_count)->decode().error(), error::too_little_data);
    CHECK_EQ(test::lazy_of_copy(map_count)->decode().error(), error::too_little_data);
}
