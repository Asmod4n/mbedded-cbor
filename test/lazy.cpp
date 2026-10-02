#include "host.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

namespace
{

std::vector<value> elements_of(std::string const &document)
{
    auto const elements = cbor::lazy_elements_of<16>(lazy_of(document));
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

// A mark around the array is passed, as lazy_at passes it.
TEST_CASE("lazy: the elements of a marked array")
{
    CHECK(elements_of("\xd8\x1c\x82\x01\x02"s) == std::vector<value>{V(1), V(2)});
}

// The entries of a map come in wire order, each as a pair of key and value.
TEST_CASE("lazy: the entries of a map")
{
    auto const entries = cbor::lazy_entries_of<16>(lazy_of(encoded(M("a"s, 1, 2, "b"s))));
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
    CHECK_EQ(cbor::lazy_elements_of<16>(lazy_of(encoded(M("a"s, 1)))).error(), error::not_indexable);
    CHECK_EQ(cbor::lazy_entries_of<16>(lazy_of(encoded(A(1)))).error(), error::not_indexable);
    CHECK_EQ(cbor::lazy_elements_of<16>(lazy_of(encoded(V(1)))).error(), error::not_indexable);
}

// decode reads nothing ahead, so a step finds a truncated element. The step gives the error once and
// the walk ends after it.
TEST_CASE("lazy: a truncated element ends the walk with its error")
{
    auto const elements = cbor::lazy_elements_of<16>(lazy_of("\x83\x01\x62\x61"s));
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
std::expected<T, error> get(std::string const &document)
{
    return cbor::lazy_get<T>(lazy_of(document));
}

} // namespace

// The integers of RFC 8949 Appendix A, read without a host.
TEST_CASE("lazy: lazy_get reads an integer")
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
TEST_CASE("lazy: lazy_get refuses an integer it cannot hold")
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
TEST_CASE("lazy: lazy_get reads a float")
{
    CHECK_EQ(*get<double>("\xf9\x3c\x00"s), 1.0);
    CHECK_EQ(*get<double>("\xfa\x47\xc3\x50\x00"s), 100000.0);
    CHECK_EQ(*get<double>("\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"s), 1.1);
    CHECK_EQ(get<double>("\x01"s).error(), error::incorrect_type);
}

// The simple values false, true and null of Table 4.
TEST_CASE("lazy: lazy_get reads a simple value")
{
    CHECK_EQ(*get<bool>("\xf4"s), false);
    CHECK_EQ(*get<bool>("\xf5"s), true);
    CHECK_EQ(get<bool>("\xf6"s).error(), error::incorrect_type);
    CHECK(get<std::nullptr_t>("\xf6"s).has_value());
    CHECK_EQ(get<std::nullptr_t>("\xf7"s).error(), error::incorrect_type);
}

// A text string and a byte string come as views into the document, each only as its own type.
TEST_CASE("lazy: lazy_get reads a string as a view")
{
    CHECK_EQ(*get<std::string_view>("\x64IETF"s), "IETF"sv);
    CHECK_EQ(get<std::string_view>("\x44\x01\x02\x03\x04"s).error(), error::incorrect_type);
    auto const bytes = get<std::span<std::byte const>>("\x44\x01\x02\x03\x04"s);
    REQUIRE(bytes.has_value());
    CHECK(std::ranges::equal(*bytes, std::array{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}));
    CHECK_EQ(get<std::span<std::byte const>>("\x64IETF"s).error(), error::incorrect_type);
    CHECK_EQ(get<std::string_view>("\x62\x61"s).error(), error::too_little_data);
}

// RFC 8746 Table 3: tags 64 to 87 carry a typed array in a byte string; 76 is reserved. The length
// is a multiple of the element size 1 << (f + ll) of Table 2.
TEST_CASE("lazy: lazy_get reads a typed array")
{
    auto const u8 = get<cbor::typed_array>("\xd8\x40\x43\x01\x02\x03"s);
    REQUIRE(u8.has_value());
    CHECK_EQ(u8->tag, 64u);
    CHECK_EQ(u8->bytes.size(), 3u);
    auto const u16 = get<cbor::typed_array>("\xd8\x41\x44\x00\x01\x00\x02"s);
    REQUIRE(u16.has_value());
    CHECK_EQ(u16->tag, 65u);
    CHECK_EQ(u16->bytes.size(), 4u);
    auto const f64 = get<cbor::typed_array>("\xd8\x56\x48\x00\x00\x00\x00\x00\x00\xf0\x3f"s);
    REQUIRE(f64.has_value());
    CHECK_EQ(f64->tag, 86u);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x41\x43\x00\x01\x00"s).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x4c\x41\x00"s).error(), error::incorrect_type);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x3f\x41\x00"s).error(), error::incorrect_type);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x40\x01"s).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(get<cbor::typed_array>("\x43\x01\x02\x03"s).error(), error::incorrect_type);
}
