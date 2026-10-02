#include "host.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

cbor::lazy lazy_of(std::string const &document)
{
    auto const l = cbor::decode<16>(document);
    REQUIRE(l.has_value());
    return *l;
}

value value_at(cbor::lazy const &l)
{
    test_host host;
    auto const v = cbor::lazy_decode<16>(host, l);
    REQUIRE(v.has_value());
    return *v;
}

cbor::lazy at(cbor::lazy const &l, std::string_view const key)
{
    auto const r = cbor::lazy_at<16>(l, key);
    REQUIRE(r.has_value());
    return *r;
}

cbor::lazy at(cbor::lazy const &l, std::int64_t const index)
{
    auto const r = cbor::lazy_at<16>(l, index);
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
    CHECK(value_at(at(lazy_of(b), 1)) == V("one"s));
    CHECK(value_at(at(lazy_of(b), 2)) == V("two"s));
    std::string const c = encoded(M(1, "int"s, "str"s, "s"s, 100, "c"s));
    CHECK(value_at(at(lazy_of(c), 1)) == V("int"s));
    CHECK(value_at(at(lazy_of(c), "str")) == V("s"s));
    CHECK(value_at(at(lazy_of(c), 100)) == V("c"s));
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
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(empty_array), 0).error(), error::index_out_of_bounds);
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(empty_map), "x").error(), error::key_not_found);
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(one), "missing").error(), error::key_not_found);
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(three), 99).error(), error::index_out_of_bounds);
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(three), "invalid").error(), error::not_indexable);
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(scalar), "key").error(), error::not_indexable);
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
// to a chain of lazy_at; a miss is an error value, and the binding makes nil of it.
TEST_CASE("lazy: negative indices and misses")
{
    std::string const h = encoded(M("a"s, 1, "b"s, M("c"s, 42)));
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(h), "missing").error(), error::key_not_found);
    CHECK(value_at(at(at(lazy_of(h), "b"), "c")) == V(42));
    std::string const a = encoded(A(10, 20, 30, 40, 50));
    CHECK(value_at(at(lazy_of(a), -1)) == V(50));
    CHECK(value_at(at(lazy_of(a), -5)) == V(10));
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(a), -99).error(), error::index_out_of_bounds);
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
        int const i = static_cast<int>(rng() % 50);
        CHECK(value_at(at(at(at(lazy_of(doc), "statuses"), i), "txt")) == V("msg" + std::to_string(i + 1)));
    }
}

// Ported from test.rb: 'lazy: skip_cbor truncation raises RangeError'.
TEST_CASE("lazy: a truncated item before the target")
{
    std::string const doc = "\x82\x4a\x01\x02\x03\x18\x2a"s;
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(doc), 1).error(), error::too_little_data);
}

// Ported from test.rb: 'lazy: huge aref index handled cleanly'.
TEST_CASE("lazy: a huge index")
{
    std::string const doc = encoded(A(1, 2, 3));
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(doc), 0x7fffffff).error(), error::index_out_of_bounds);
}

// Found by the fuzz corpus: a reference inside the mark it names, d8 1c d8 1d 00, led the navigation
// back to itself without end. A reference must name a mark that ends before it.
TEST_CASE("lazy: a reference to its own enclosing mark ends")
{
    std::string const doc = "\xd8\x1c\xd8\x1d\x00"s;
    CHECK_EQ(cbor::lazy_at<16>(lazy_of(doc), 0).error(), error::sharedref_not_complete);
    test_host host;
    CHECK_FALSE(cbor::lazy_decode<16>(host, lazy_of(doc)).has_value());
}

// Found by the fuzz corpus: a map that claims about 7.7 * 10^18 pairs. Before the fix the scan for
// marks before the value of its first key went on through the claimed pairs after it reached the
// target; now it stops there.
TEST_CASE("lazy: a value inside a huge claimed map")
{
    std::string const doc = "\xbb\x6a\xc9\xfb\x32\xf6\xd8\xd8\x27\x61\x61\x19\x00\x00"s;
    auto const a = cbor::lazy_at<16>(lazy_of(doc), "a");
    REQUIRE(a.has_value());
    CHECK(value_at(*a) == V(0));
}

// Found by the fuzzer: [28(28(29(0))), ...] leads from the reference through two marks back to the
// same reference. A reference that the walk meets a second time ends it.
TEST_CASE("lazy: a chain of marks back to the same reference ends")
{
    std::string const doc = "\x92\xd8\x1c\xd8\x1c\xd8\x1d\x00"s;
    auto const element = cbor::lazy_at<16>(lazy_of(doc), 0);
    REQUIRE(element.has_value());
    CHECK_EQ(cbor::lazy_at<16>(*element, 0).error(), error::sharedref_not_complete);
}
