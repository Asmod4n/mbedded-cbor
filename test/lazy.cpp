#include "binding.hpp"

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
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

cbor::lazy lazy_of(std::string const &document)
{
    auto const l = cbor::lazy::from(document);
    REQUIRE(l.has_value());
    return *l;
}

value value_at(cbor::lazy const &l)
{
    test_binding binding;
    auto const v = cbor::lazy_decode(binding, l);
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
// to a chain of at; a miss is an error value, and the binding makes nil of it. at takes a std::size_t, as
// std::vector::at does, so a binding counts a negative index from size(). A path keeps the negative index of
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

// Found by the fuzz corpus: a reference inside the mark it names, d8 1c d8 1d 00, led the navigation
// back to itself without end. A reference must name a mark that ends before it.
TEST_CASE("lazy: a reference to its own enclosing mark ends")
{
    std::string const doc = "\xd8\x1c\xd8\x1d\x00"s;
    CHECK_EQ(lazy_of(doc).at(0).error(), error::sharedref_not_complete);
    test_binding binding;
    CHECK_FALSE(cbor::lazy_decode(binding, lazy_of(doc)).has_value());
}

// Found by the fuzz corpus: a map that claims about 7.7 * 10^18 pairs. A lookup stops at the first key that
// matches, so it does not go on through the claimed pairs after it reached the target.
TEST_CASE("lazy: a value inside a huge claimed map")
{
    std::string const doc = "\xbb\x6a\xc9\xfb\x32\xf6\xd8\xd8\x27\x61\x61\x19\x00\x00"s;
    auto const a = lazy_of(doc).at("a");
    REQUIRE(a.has_value());
    CHECK(value_at(*a) == V(0));
}

// Found by the fuzzer: [28(28(29(0))), ...] leads from the reference through two marks back to the
// same reference. A reference that the walk meets a second time ends it.
TEST_CASE("lazy: a chain of marks back to the same reference ends")
{
    std::string const doc = "\x92\xd8\x1c\xd8\x1c\xd8\x1d\x00"s;
    auto const element = lazy_of(doc).at(0);
    REQUIRE(element.has_value());
    CHECK_EQ(element->at(0).error(), error::sharedref_not_complete);
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
    static_assert(std::forward_iterator<cbor::lazy_entries::iterator>);
    static_assert(std::forward_iterator<cbor::lazy_elements::iterator>);
    static_assert(std::sentinel_for<std::default_sentinel_t, cbor::lazy_entries::iterator>);
    static_assert(std::sentinel_for<std::default_sentinel_t, cbor::lazy_elements::iterator>);
    static_assert(std::ranges::forward_range<cbor::lazy_entries>);
    static_assert(std::ranges::forward_range<cbor::lazy_elements>);
    static_assert(std::ranges::view<cbor::lazy_entries>);
    static_assert(std::ranges::view<cbor::lazy_elements>);
    static_assert(std::ranges::sized_range<cbor::lazy_entries>);
    static_assert(std::ranges::sized_range<cbor::lazy_elements>);
    static_assert(std::ranges::borrowed_range<cbor::lazy_entries>);
    static_assert(std::ranges::borrowed_range<cbor::lazy_elements>);
    static_assert(!std::ranges::bidirectional_range<cbor::lazy_entries>);
    static_assert(!std::ranges::common_range<cbor::lazy_elements>);
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
    static_assert(!front_reads<cbor::lazy_elements>);
    static_assert(!front_reads<cbor::lazy_entries>);
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

// find gives the iterator at the pair with the key, as std::map::find does, and at gives its value.
TEST_CASE("lazy: find gives the pair of a present key")
{
    cbor::lazy const l = lazy_of("\xa4\x61" "a\x01\x21\x65" "minus\x07\x65" "seven\x61" "b\x02"s);
    auto const a = l.find("b");
    REQUIRE(a.has_value());
    REQUIRE(*a != std::default_sentinel);
    CHECK(value_at((**a)->first) == V("b"s));
    CHECK(value_at((**a)->second) == V(2));
    auto const seven = l.find(7);
    REQUIRE(seven.has_value());
    REQUIRE(*seven != std::default_sentinel);
    CHECK(value_at((**seven)->second) == V("seven"s));
    auto const minus = l.find(-2);
    REQUIRE(minus.has_value());
    REQUIRE(*minus != std::default_sentinel);
    CHECK(value_at((**minus)->second) == V("minus"s));
    auto next = *minus;
    ++next;
    CHECK(value_at((*next)->first) == V(7));
}

// A missing key gives the end, as std::map::find does. The end that find gives is equal to the end
// of a walk over all pairs.
TEST_CASE("lazy: find gives the end for a missing key")
{
    cbor::lazy const l = lazy_of(encoded(M("a"s, 1, 2, "b"s)));
    auto const missing = l.find("z");
    REQUIRE(missing.has_value());
    CHECK(*missing == std::default_sentinel);
    auto const entries = l.entries();
    REQUIRE(entries.has_value());
    CHECK(*missing == std::ranges::next(entries->begin(), entries->end()));
    auto const number = l.find(1);
    REQUIRE(number.has_value());
    CHECK(*number == std::default_sentinel);
    auto const empty = lazy_of(encoded(M())).find("a");
    REQUIRE(empty.has_value());
    CHECK(*empty == std::default_sentinel);
}

// find is a lookup in a map. An array or a scalar has no keys.
TEST_CASE("lazy: find in an item that is not a map")
{
    CHECK_EQ(lazy_of(encoded(A(1))).find(0).error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(V(1))).find("a").error(), error::not_indexable);
}

// A key behind a shared reference (tag 29) is compared by its content, in find as in at.
TEST_CASE("lazy: find resolves a shared key")
{
    cbor::lazy const l = lazy_of("\xa2\x61" "a\xd8\x1c\x61k\xd8\x1d\x00\x02"s);
    auto const shared = l.find("k");
    REQUIRE(shared.has_value());
    REQUIRE(*shared != std::default_sentinel);
    CHECK(value_at((**shared)->second) == V(2));
    CHECK(value_at(at(l, "k")) == V(2));
}

// A truncated map gives its error from find and from at.
TEST_CASE("lazy: find in a truncated map gives the error")
{
    cbor::lazy const l = lazy_of("\xa2\x61" "a\x01\x61"s);
    CHECK_EQ(l.find("z").error(), error::too_little_data);
    CHECK_EQ(l.at("z").error(), error::too_little_data);
    CHECK(value_at(at(l, "a")) == V(1));
}

// The owner wants a search that starts at a position the caller already has. find from first to the
// end reads the pairs from first on, so a key after first is found.
TEST_CASE("lazy: find from an iterator to the end finds a later key")
{
    cbor::lazy const l = lazy_of(encoded(M("a"s, 1, "b"s, 2, 3, "c"s, "d"s, 4)));
    auto const b = l.find("b");
    REQUIRE(b.has_value());
    auto const d = cbor::lazy::find(*b, std::default_sentinel, "d");
    REQUIRE(d.has_value());
    REQUIRE(*d != std::default_sentinel);
    CHECK(value_at((**d)->second) == V(4));
    auto const three = cbor::lazy::find(*b, std::default_sentinel, 3);
    REQUIRE(three.has_value());
    CHECK(value_at((**three)->second) == V("c"s));
    auto const self = cbor::lazy::find(*b, std::default_sentinel, "b");
    REQUIRE(self.has_value());
    CHECK(*self == *b);
}

// A key before first is outside [first, end), so the search gives the end. The search over [begin, first)
// is the backward search: it reads forward from begin and stops at first, and finds the key.
TEST_CASE("lazy: a key before the iterator is found only over the part before it")
{
    cbor::lazy const l = lazy_of(encoded(M("a"s, 1, "b"s, 2, 3, "c"s, "d"s, 4)));
    auto const entries = l.entries();
    REQUIRE(entries.has_value());
    auto const three = l.find(3);
    REQUIRE(three.has_value());
    auto const forward = cbor::lazy::find(*three, std::default_sentinel, "a");
    REQUIRE(forward.has_value());
    CHECK(*forward == std::default_sentinel);
    auto const backward = cbor::lazy::find(entries->begin(), *three, "a");
    REQUIRE(backward.has_value());
    REQUIRE(*backward != *three);
    CHECK(value_at((**backward)->second) == V(1));
    auto const after = cbor::lazy::find(entries->begin(), *three, "d");
    REQUIRE(after.has_value());
    CHECK(*after == *three);
    auto const whole = cbor::lazy::find(entries->begin(), std::default_sentinel, "a");
    REQUIRE(whole.has_value());
    CHECK(*whole == *backward);
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

// The iterator holds the owner of the encoded item. A search from it after the lazy and the string are
// gone reads memory that is alive; ASan checks it.
TEST_CASE("lazy: find from an iterator outlives the lazy that gave it")
{
    std::optional<cbor::lazy_entries::iterator> kept;
    {
        std::string message = encoded(M("a"s, 1, "b"s, 2, "c"s, 3));
        auto const l = cbor::lazy::from(std::move(message));
        REQUIRE(l.has_value());
        auto const a = l->find("a");
        REQUIRE(a.has_value());
        kept = *a;
    }
    auto const c = cbor::lazy::find(*kept, std::default_sentinel, "c");
    REQUIRE(c.has_value());
    REQUIRE(*c != std::default_sentinel);
    CHECK(value_at((**c)->second) == V(3));
}

// With two known positions a and b, a search reads only [a, b). In a map whose keys are sorted by their
// encoded bytes (RFC 8949 4.2.1), equal_range gives the pair of the key, as std::ranges::equal_range does
// over a sorted range.
TEST_CASE("lazy: equal_range between two iterators finds a key between them")
{
    cbor::lazy const l = lazy_of(encoded(M(1, 1, "b"s, 2, "d"s, 4, "f"s, 6, "h"s, 8)));
    auto const a = l.find("b");
    auto const b = l.find("h");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    auto const hit = cbor::lazy::equal_range(*a, *b, "f");
    REQUIRE(hit.has_value());
    REQUIRE(std::ranges::distance(*hit) == 1);
    CHECK(value_at((*hit->begin())->second) == V(6));
    auto const number = cbor::lazy::equal_range(l.entries()->begin(), *b, 1);
    REQUIRE(number.has_value());
    REQUIRE(std::ranges::distance(*number) == 1);
    CHECK(value_at((*number->begin())->second) == V(1));
}

// A missing key in a sorted map stops at the first key whose encoded bytes are greater. The empty range
// stands there, where the key would be, as std::ranges::equal_range gives it. Nothing after it is read.
TEST_CASE("lazy: equal_range stops at the first greater key for a missing key")
{
    cbor::lazy const l = lazy_of(encoded(M("b"s, 2, "d"s, 4, "f"s, 6, "h"s, 8)));
    auto const entries = l.entries();
    REQUIRE(entries.has_value());
    auto const h = l.find("h");
    REQUIRE(h.has_value());
    auto const miss = cbor::lazy::equal_range(entries->begin(), *h, "e");
    REQUIRE(miss.has_value());
    CHECK(miss->empty());
    CHECK(value_at((*miss->begin())->first) == V("f"s));
    auto const after = cbor::lazy::equal_range(entries->begin(), *h, "z");
    REQUIRE(after.has_value());
    CHECK(after->empty());
    CHECK(after->begin() == *h);

    cbor::lazy const truncated = lazy_of("\xa3\x61" "b\x02\x61" "f\x06\x61"s);
    auto const early = cbor::lazy::equal_range(truncated.entries()->begin(), std::default_sentinel, "e");
    REQUIRE(early.has_value());
    CHECK(value_at((*early->begin())->first) == V("f"s));
    CHECK_EQ(truncated.find("e").error(), error::too_little_data);
}

// A key under tag 28 sorts by its tagged bytes, after every text key, but a key under tag 29 is compared by
// the content it refers to. After a tagged key the search reads to b and does not stop early. Here the key
// 28("z") sorts after "q" by its bytes, and the key 29(0) after it refers to "q".
TEST_CASE("lazy: equal_range reads past a tagged key")
{
    cbor::lazy const l = lazy_of("\xa3\x61" "a\xd8\x1c\x61q\xd8\x1c\x61z\x03\xd8\x1d\x00\x02"s);
    auto const entries = l.entries();
    REQUIRE(entries.has_value());
    auto const q = cbor::lazy::equal_range(entries->begin(), std::default_sentinel, "q");
    REQUIRE(q.has_value());
    REQUIRE(std::ranges::distance(*q) == 1);
    CHECK(value_at((*q->begin())->second) == V(2));
}

// a and b of two different maps hold no range. The scan from a reaches the end of its map without b, and
// that is a wrong use of the library.
TEST_CASE("lazy: a range over two maps is a logic error")
{
    cbor::lazy const one = lazy_of(encoded(M("a"s, 1, "b"s, 2)));
    cbor::lazy const two = lazy_of(encoded(M("a"s, 1, "b"s, 2)));
    auto const a = one.find("a");
    auto const b = two.find("b");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK_THROWS_AS(std::ignore = cbor::lazy::equal_range(*a, *b, "x"), std::logic_error);
    CHECK_THROWS_AS(std::ignore = cbor::lazy::find(*a, *b, "x"), std::logic_error);
    auto const b_one = one.find("b");
    REQUIRE(b_one.has_value());
    CHECK_THROWS_AS(std::ignore = cbor::lazy::find(*b_one, *a, "x"), std::logic_error);
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

// The integers of RFC 8949 Appendix A, read without a binding.
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

// Floats of the three widths of Appendix A. An integer is not a float: CBOR keeps the two apart.
TEST_CASE("lazy: get reads a float")
{
    CHECK_EQ(*get<double>("\xf9\x3c\x00"s), 1.0);
    CHECK_EQ(*get<double>("\xfa\x47\xc3\x50\x00"s), 100000.0);
    CHECK_EQ(*get<double>("\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"s), 1.1);
    CHECK_EQ(get<double>("\x01"s).error(), error::incorrect_type);
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

// A binding whose only value is a typed array, as a language with ArrayBuffers has.
struct typed_binding {
    using value = cbor::typed_array;

    cbor::kind kind_of(cbor::typed_array const &)
    {
        return cbor::kind::typed_array;
    }

    cbor::typed_array typed_array_of(cbor::typed_array const &a)
    {
        return a;
    }
};

std::expected<std::string, cbor::error> typed_encoded(std::uint64_t const tag, std::string_view const bytes)
{
    typed_binding binding;
    test::string_writer w;
    auto const r = cbor::encode(binding, w, cbor::typed_array{tag, std::as_bytes(std::span(bytes))});
    if (!r)
        return std::unexpected(r.error());
    return w.encoded;
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
TEST_CASE("encode: a typed array the binding answers wrongly")
{
    CHECK((typed_encoded(76, "\x00"sv).error() == cbor::error{cbor::error::unsupported_value}));
    CHECK((typed_encoded(63, "\x00"sv).error() == cbor::error{cbor::error::unsupported_value}));
    CHECK((typed_encoded(66, "\x00\x01\x02"sv).error() == cbor::error{cbor::error::inadmissible_type_for_tag_content}));
}

namespace
{

// A binding that embeds every array as an encoded data item of its own.
struct embedding_binding : test_binding {
    bool embed_of(value const &v)
    {
        return std::holds_alternative<test::array *>(v.kind);
    }
};

} // namespace

// RFC 8949 3.4.5.1: tag 24 carries an encoded data item in a byte string. The encoder writes each embedded
// value as a top-level item of its own, and a view passes through the tag into it.
TEST_CASE("tag 24: an embedded value is written as a top-level item of its own and read through")
{
    embedding_binding binding;
    test::string_writer w;
    REQUIRE(cbor::encode(binding, w, A(1, A(2, 3))).has_value());
    CHECK_EQ(w.encoded, "\xd8\x18\x48\x82\x01\xd8\x18\x43\x82\x02\x03"s);
    auto const inner = lazy_of(w.encoded).at(1);
    REQUIRE(inner.has_value());
    auto const three = inner->at(1);
    REQUIRE(three.has_value());
    CHECK_EQ(*three->get<std::uint64_t>(), 3u);
    CHECK_EQ(get<std::uint64_t>("\xd8\x18\x05"s).error(), error::inadmissible_type_for_tag_content);
}

// The marks of an embedded data item are its own: the 29(0) inside names the 28 inside, not the one
// outside before it.
TEST_CASE("tag 24: an embedded data item has marks of its own")
{
    std::string const doc = "\x82\xd8\x1c\x07\xd8\x18\x47\x82\xd8\x1c\x09\xd8\x1d\x00"s;
    auto const embedded = lazy_of(doc).at(1);
    REQUIRE(embedded.has_value());
    auto const second = embedded->at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(*second->get<std::uint64_t>(), 9u);
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

// A tag 29 reference names a mark that lies before it. A mark later in the top-level item is no target: the
// reference counts only the marks before it, as the full decoder and at_path do, whatever navigation read first.
TEST_CASE("lazy: a reference forward to a mark that navigation read is not marked")
{
    auto const root = lazy_of("\x82\xd8\x1d\x00\xd8\x1c\x05"s);
    auto const second = root.at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(*second->get<std::uint64_t>(), 5u);
    auto const first = root.at(0);
    REQUIRE(first.has_value());
    test_binding binding;
    CHECK_EQ(cbor::lazy_decode(binding, *first).error(), error::sharedref_index_not_marked);
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

// lazy::from with an owner reads bytes that someone else holds, as the page of a read transaction of LMDB. Each
// lazy that a step gives holds the owner too, so the bytes live until the last lazy of the top-level item ends.
TEST_CASE("lazy: from an owner holds the owner until the last lazy ends")
{
    bool released = false;
    std::optional<cbor::lazy> name;
    {
        auto const owner = std::make_shared<buffer_owner>(encoded(M("user"s, M("name"s, "ann"s))), &released);
        auto const doc = cbor::lazy::from(owner, owner->bytes);
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

// lazy::from takes the bytes by move and copies nothing. Each step gives a std::expected, and_then gives it to the
// next step, so an error anywhere reaches the end of the chain.
TEST_CASE("lazy: from, at and get as a chain")
{
    std::string bytes = encoded(M("statuses"s, A(M("user"s, M("name"s, "ann"s)), M("user"s, M("name"s, "bob"s)))));
    char const *const data = bytes.data();
    cbor::lazy const doc = *cbor::lazy::from(std::move(bytes));
    auto const name = doc.at("statuses")
                          .and_then([](cbor::lazy const &s) { return s.at(1); })
                          .and_then([](cbor::lazy const &s) { return s.at("user"); })
                          .and_then([](cbor::lazy const &u) { return u.at("name"); })
                          .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "bob"sv);
    CHECK_EQ(static_cast<void const *>(doc.top_level->encoded.data()), static_cast<void const *>(data));
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

// RFC 8949 3.4 and the registration of tags 28 and 29: a tag 28 marks a value as shared and leaves the value as
// it is, and a tag 29 stands for the marked value. A key under either tag is the key it marks or names, as the
// full decoder reads it. The path fuzzer found a key under tag 28 that lazy::at did not match.
TEST_CASE("lazy: a key under tag 28 or tag 29 is the key it marks or names")
{
    auto const marked = lazy_of("\xa1\xd8\x1c\x61\x61\x01"s).at("a");
    REQUIRE(marked.has_value());
    CHECK_EQ(*marked->get<std::uint64_t>(), 1u);
    auto const number = lazy_of("\xa1\xd8\x1c\x07\x03"s).at(cbor::key{7});
    REQUIRE(number.has_value());
    CHECK_EQ(*number->get<std::uint64_t>(), 3u);
    auto const root = lazy_of("\x82\xd8\x1c\x61\x61\xa1\xd8\x1d\x00\x02"s);
    REQUIRE(root.at(0).has_value());
    auto const named = root.at(1)->at("a");
    REQUIRE(named.has_value());
    CHECK_EQ(*named->get<std::uint64_t>(), 2u);
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
    std::optional<cbor::owning_ref<std::string_view>> view;
    {
        auto const owner = std::make_shared<buffer_owner>(encoded(M("name"s, "ann"s)), &released);
        auto const name = cbor::lazy::from(owner, owner->bytes)
                              .and_then([](cbor::lazy const &d) { return d.at("name"); })
                              .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
        REQUIRE(name.has_value());
        view.emplace(*name);
    }
    CHECK_FALSE(released);
    CHECK_EQ(**view, "ann"sv);
    view.reset();
    CHECK(released);
    CHECK_FALSE(read_from_temporary<std::expected<cbor::owning_ref<std::string_view>, cbor::error>>);
    CHECK_THROWS_AS((void)cbor::lazy::from(std::shared_ptr<void const>{}, "\x00"sv), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. A temporary or a moved std::string beside an
// owner does not compile, because the owner does not hold it. The test exists because each of these let a view
// outlive its bytes, and a check of the stored pointer refused an owner that holds the bytes.
TEST_CASE("lazy: from checks that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>("\xa1\x61\x61\x63xyz"s);
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::lazy::from(o, std::string(*o)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o, std::string s) { cbor::lazy::from(o, std::move(s)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::lazy::from(o, *o); }; }(bytes)));
    std::shared_ptr<void const> const holds_nothing(std::shared_ptr<void const>{}, bytes->data());
    CHECK_THROWS_AS((void)cbor::lazy::from(holds_nothing, *bytes), std::logic_error);
    std::shared_ptr<void const> const holds_bytes(bytes, nullptr);
    auto const name = cbor::lazy::from(holds_bytes, *bytes)
                          .and_then([](cbor::lazy const &d) { return d.at("a"); })
                          .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "xyz"sv);
}

// The forms that take the bytes in a std::shared_ptr<std::string const> read through it. A null pointer was read
// before this test existed, and an empty pointer was kept as the owner of a view.
TEST_CASE("lazy: from and decode refuse a null or empty std::shared_ptr<std::string const>")
{
    std::shared_ptr<std::string const> const null_bytes;
    CHECK_THROWS_AS((void)cbor::lazy::from(null_bytes), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy::from(null_bytes), std::logic_error);
    std::string const bytes = "\x00"s;
    std::shared_ptr<std::string const> const holds_nothing(std::shared_ptr<std::string const>{}, &bytes);
    CHECK_THROWS_AS((void)cbor::lazy::from(holds_nothing), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy::from(holds_nothing), std::logic_error);
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
    test_binding binding;
    CHECK_THROWS_AS((void)cbor::lazy_decode(binding, none), std::logic_error);
}

// A shared reference may stand for the content of a tag: the magnitude of a bignum, the bytes of a typed array.
TEST_CASE("lazy: get follows a shared reference in the content of a tag")
{
    auto const root = lazy_of("\x83\xd8\x1c\x41\x05\xc2\xd8\x1d\x00\xd8\x40\xd8\x1d\x00"s);
    CHECK_EQ(*root.at(1)->get<std::uint64_t>(), 5u);
    auto const typed = root.at(2)->get<cbor::typed_array>();
    REQUIRE(typed.has_value());
    CHECK_EQ((*typed)->bytes.size(), 1u);
}

// The value category of the bytes says what cbor::decode and lazy::from do. A moved std::string becomes the owner and
// is not copied; an lvalue, a const rvalue and a literal are copied once. The top-level item stays valid after the caller
// reuses or destroys its buffer.
TEST_CASE("lazy: decode and from move an rvalue string and copy everything else")
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
    auto const moved = cbor::lazy::from(std::move(*buffer));
    REQUIRE(moved.has_value());
    CHECK_EQ(static_cast<void const *>(moved->top_level->encoded.data()), static_cast<void const *>(data));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(text_of(*moved->at("k")), text);

    std::string lvalue = message;
    auto const copied = cbor::lazy::from(lvalue);
    REQUIRE(copied.has_value());
    CHECK_NE(static_cast<void const *>(copied->top_level->encoded.data()), static_cast<void const *>(lvalue.data()));
    CHECK_EQ(lvalue, message);
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(text_of(*copied->at("k")), text);

    std::string const constant = message;
    auto const from_const = cbor::lazy::from(std::move(constant));
    REQUIRE(from_const.has_value());
    CHECK_NE(static_cast<void const *>(from_const->top_level->encoded.data()), static_cast<void const *>(constant.data()));
    CHECK_EQ(constant, message);

    std::string again = message;
    char const *const again_data = again.data();
    auto const from_moved = cbor::lazy::from(std::move(again));
    CHECK_EQ(static_cast<void const *>(from_moved->top_level->encoded.data()), static_cast<void const *>(again_data));
    auto const from_lvalue = cbor::lazy::from(message);
    CHECK_NE(static_cast<void const *>(from_lvalue->top_level->encoded.data()), static_cast<void const *>(message.data()));

    CHECK_EQ(text_of(*cbor::lazy::from("\x63" "abc")), "abc");
    char const *const pointer = "\x62" "ab";
    CHECK_EQ(text_of(*cbor::lazy::from(pointer)), "ab");
    CHECK_EQ(text_of(*cbor::lazy::from("\x61" "a"sv)), "a");
}

// RFC 8949 5.6.1 says when two keys are equal. Each pair is written from the text of 5.6.1. Two keys that
// stand next to each other in a map are found equal by the neighbour compare of a deterministic profile
// exactly when 5.6.1 makes them equal.
TEST_CASE("validity: check_sorted_keys_unique compares two neighbours by RFC 8949 5.6.1")
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
        // A big number is distinct from an integer, and a tagged value from an untagged one. Tagged values
        // are
        // equal when the tag numbers and the contents are equal.
        {"\x01"s, "\xc2\x41\x01"s, false},
        {"\xc2\x41\x01"s, "\xc2\x42\x00\x01"s, false},
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
        std::string const map = "\xa2"s + p.first + "\x00"s + p.second + "\x00"s;
        CAPTURE(map);
        std::string_view const encoded = map;
        auto const checked = cbor::validity::check_sorted_keys_unique(encoded, 1, 2, 1, 16,
                                                                      cbor::validity::limit_checks<false>{});
        if (p.equal)
            CHECK_EQ(checked.error(), error::duplicate_key);
        else
            CHECK(checked.has_value());
    }
}

// A deterministic profile sorts the keys of a map (RFC 8949 4.2.1), so two equal keys stand next to each other.
// The neighbour compare finds the pair at the start, in the middle and at the end, and accepts sorted distinct
// keys.
TEST_CASE("validity: check_sorted_keys_unique finds equal neighbours at any position")
{
    auto const checked = [](std::string_view const map, std::uint64_t const count) {
        return cbor::validity::check_sorted_keys_unique(map, 1, count, 1, 16,
                                                        cbor::validity::limit_checks<false>{});
    };
    CHECK(checked("\xa3\x01\x00\x02\x00\x03\x00"sv, 3).has_value());
    CHECK_EQ(checked("\xa3\x01\x00\x01\x00\x03\x00"sv, 3).error(), error::duplicate_key);
    CHECK_EQ(checked("\xa3\x01\x00\x02\x00\x02\x00"sv, 3).error(), error::duplicate_key);
    CHECK_EQ(checked("\xa3\x61\x61\x00\x61\x62\x00\x61\x62\x00"sv, 3).error(), error::duplicate_key);
    CHECK(checked("\xa0"sv, 0).has_value());
    CHECK(checked("\xa1\x01\x00"sv, 1).has_value());
    CHECK_EQ(checked("\xa2\x01\x00"sv, 2).error(), error::too_little_data);
}

// RFC 8949 5.6 lets a decoder that is not in a deterministic profile keep one entry of a repeated key. Every
// lookup stops at the first key that matches and gives its value with no error, also when the second key
// stands after it. The entries, decode and lazy_decode read such a map with no error.
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
    test_binding binding;
    CHECK(cbor::lazy_decode(binding, lazy_of(twice)).has_value());
    CHECK(cbor::lazy_decode(binding, lazy_of("\x81\xa2\x01\x00\x18\x01\x00"s)).has_value());
}

// A repeated key at depth 3 and a repeated key of the outer map after a nested map are read with no error.
// A lookup gives the value of the first key.
TEST_CASE("lazy: a repeated key at depth 3 is read with no error")
{
    test_binding binding;
    std::string const inner = "\xa1\x61\x61\xa1\x61\x62\xa3\x61\x63\x01\x61\x64\x02\x61\x64\x03"s;
    std::string const outer = "\xa2\x61\x61\xa1\x61\x62\xa1\x61\x63\x01\x61\x61\x00"s;
    CHECK(cbor::lazy_decode(binding, lazy_of(inner)).has_value());
    CHECK(cbor::lazy_decode(binding, lazy_of(outer)).has_value());
    CHECK_EQ(*lazy_of(inner).at("a")->at("b")->at("d")->get<std::uint64_t>(), 2u);
    CHECK(lazy_of(outer).at("a")->at("b").has_value());
    CHECK_EQ(*(cbor::at_path<"$.a.b.d", int>(inner)), 2);
    CHECK(cbor::query(binding, "$..*", lazy_of(inner)).has_value());
    CHECK(cbor::at_path(binding, "$.a.b.d", lazy_of(inner)).has_value());
    CHECK(lazy_of(inner).at("a")->at("b")->decode().has_value());
}

// The same map many times in one array is no repeated key: a key is compared only with the keys of its own
// map. Every reader takes the array.
TEST_CASE("lazy: an array of the same map many times is read with no error")
{
    std::string doc = "\x98\x40"s;
    for (int i = 0; i < 64; ++i)
        doc += "\xa2\x61\x61\x01\x61\x62\x02"s;
    test_binding binding;
    cbor::lazy const top = lazy_of(doc);
    CHECK(top.decode().has_value());
    CHECK(cbor::lazy_decode(binding, lazy_of(doc)).has_value());
    CHECK(cbor::query(binding, "$..*", lazy_of(doc)).has_value());
    CHECK(cbor::query(binding, "$[*].a", lazy_of(doc)).has_value());
    CHECK_EQ(*(cbor::at_path<"$[63].b", int>(doc)), 2);
    CHECK_EQ(*lazy_of(doc).at(63)->at("a")->get<std::uint64_t>(), 1u);
}

// contains and count answer as std::map::contains and std::map::count do, and size and empty as the size and
// empty of a container. Each comes in std::expected, because the bytes are not checked before the call.
TEST_CASE("lazy: contains, count, size and empty")
{
    cbor::lazy const map = lazy_of(encoded(M("a"s, 1, 2, "b"s)));
    CHECK(map.contains("a").value());
    CHECK_FALSE(map.contains("z").value());
    CHECK(map.contains(std::int64_t{2}).value());
    CHECK_FALSE(map.contains(std::int64_t{3}).value());
    CHECK_EQ(map.count("a").value(), 1u);
    CHECK_EQ(map.count("z").value(), 0u);
    CHECK_EQ(map.count(std::int64_t{2}).value(), 1u);
    CHECK_EQ(map.size().value(), 2u);
    CHECK_FALSE(map.empty().value());

    cbor::lazy const array = lazy_of(encoded(A(1, 2, 3)));
    CHECK_EQ(array.size().value(), 3u);
    CHECK_FALSE(array.empty().value());
    CHECK(lazy_of(encoded(A())).empty().value());
    CHECK(lazy_of("\xa0"s).empty().value());
    CHECK_EQ(array.contains("a").error(), error::not_indexable);
    CHECK_EQ(array.count("a").error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(V(1))).size().error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(V("a"s))).empty().error(), error::not_indexable);
    CHECK_EQ(lazy_of("\xd8\x1c\x82\x01\x02"s).size().value(), 2u);
}

// lazy does not reject a map with a repeated key (RFC 8949 5.6 leaves that to the decoder), so count is not
// limited to 0 or 1: it counts every pair with the key, as std::multimap::count does. contains and find see
// the first pair.
TEST_CASE("lazy: count counts every pair of a repeated key")
{
    cbor::lazy const twice = lazy_of("\xa3\x61" "a\x01\x61" "b\x02\x61" "a\x03"s);
    CHECK(twice.entries().has_value());
    CHECK_EQ(twice.count("a").value(), 2u);
    CHECK_EQ(twice.count("b").value(), 1u);
    CHECK(twice.contains("a").value());
    CHECK(value_at(twice.at("a").value()) == V(1));
    CHECK_EQ(lazy_of("\xa3\x01\x01\x01\x02\x01\x03"s).count(std::int64_t{1}).value(), 3u);
}

// count reads every pair after the first match, so a pair that is cut off after a match is an error, and a cut
// before the match is the error of find.
TEST_CASE("lazy: count of a map that is cut off")
{
    CHECK_EQ(lazy_of("\xa2\x61" "a\x01\x61"s).count("a").error(), error::too_little_data);
    CHECK_EQ(lazy_of("\xa2\x61"s).count("a").error(), error::too_little_data);
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
    auto const l = lazy_of("\xbb\xff\xff\xff\xff\xff\xff\xff\xff\x61" "a\x01"s);
    auto it = *l.find("a");
    REQUIRE(it != std::default_sentinel);
    CHECK(++it != std::default_sentinel);
    CHECK_FALSE((*it).has_value());
    CHECK(std::ranges::distance(it, std::default_sentinel) <= 3);
}

// Fault p14: each resolve of tag 24 makes a new top-level item, and the
// iterators compared that pointer, so two finds of one pair differed.
TEST_CASE("lazy: iterators into a map inside tag 24 compare equal at the same pair")
{
    auto const l = lazy_of("\xd8\x18\x47\xa2\x61" "a\x01\x61" "b\x02"s);
    auto const f1 = *l.find("b");
    auto const f2 = *l.find("b");
    CHECK(f1 == f2);
    auto it = l.entries()->begin();
    ++it;
    CHECK(it == f1);
    CHECK(it != l.entries()->begin());
    auto const a = *l.find("a");
    auto const found = cbor::lazy::find(a, f1, "b");
    REQUIRE(found.has_value());
    CHECK(*found == f1);
}

// The count check of elements() and entries() reads the argument of the
// head, and an indefinite length has no count, so it is refused first.
TEST_CASE("lazy: elements and entries refuse indefinite length")
{
    CHECK_EQ(lazy_of("\x9f\x01\xff"s).elements().error(), error::indefinite_length);
    CHECK_EQ(lazy_of("\xbf\x61" "a\x01\xff"s).entries().error(), error::indefinite_length);
}

// The owner decided on 2026-10-10 that a lazy belongs to one thread at a
// time: the library keeps no lock, no call_once and no atomic, and the
// application moves a value from one thread to another. This test gives
// each thread its own lazy of the same bytes, the form the library
// supports, and a thread sanitizer build reports any state they share.
TEST_CASE("lazy: each thread reads its own lazy of the same bytes")
{
    constexpr std::size_t threads = 4;
    std::string const s("\x83\xd8\x1c\x65hello\xd8\x1d\x00\xa1\x61k\xd8\x1d\x00", 18);
    std::array<bool, threads> right{};
    {
        std::vector<std::jthread> workers;
        for (std::size_t t = 0; t < threads; ++t)
            workers.emplace_back([&s, &right, t] {
                cbor::lazy const l = *cbor::lazy::from(s);
                auto const text = l.at(std::size_t{1}).and_then([](cbor::lazy const &r) { return r.get<std::string_view>(); });
                auto const named = l.at(std::size_t{2})
                                       .and_then([](cbor::lazy const &m) { return m.at("k"); })
                                       .and_then([](cbor::lazy const &r) { return r.decode(); });
                right[t] = text.has_value() && **text == "hello" && named.has_value() &&
                           std::get<std::string_view>((*named)->content) == "hello";
            });
    }
    CHECK(std::ranges::all_of(right, std::identity{}));
}

// Fault 2 for an array: in 82 82 01 02 the first element is the whole rest
// of the input, and the second element is missing. The walk gives the first
// element and then too_little_data, not a value past the end.
TEST_CASE("lazy: an element past the end of the input is too_little_data")
{
    auto const e = lazy_of("\x82\x82\x01\x02"s).elements();
    REQUIRE(e.has_value());
    std::vector<std::expected<cbor::lazy, error>> steps;
    for (auto const step : *e)
        steps.push_back(step);
    REQUIRE(steps.size() == 2);
    CHECK(steps.at(0).has_value());
    CHECK_EQ(steps.at(1).error(), error::too_little_data);
}

// A map of n pairs holds 2n data items, and each takes one byte at least.
// a1 61 holds one byte for two data items, so entries() refuses it as
// elements() refuses 81. In a1 61 61 the key fills the input, and the walk
// gives too_little_data for the value, not a value past the end.
TEST_CASE("lazy: a map counts two data items for each pair")
{
    CHECK_EQ(lazy_of("\xa1\x61"s).entries().error(), error::too_little_data);
    CHECK_EQ(lazy_of("\x81"s).elements().error(), error::too_little_data);
    auto const m = lazy_of("\xa1\x61\x61"s).entries();
    REQUIRE(m.has_value());
    std::vector<std::expected<std::pair<cbor::lazy, cbor::lazy>, error>> steps;
    for (auto const step : *m)
        steps.push_back(step);
    REQUIRE(steps.size() == 1);
    CHECK_EQ(steps.at(0).error(), error::too_little_data);
}
