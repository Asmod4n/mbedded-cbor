#include "../owned_copy.hpp"
#include "../reference/path.hpp"
#include <cbor/error.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <string_view>

using namespace std::string_literals;
using cbor::error;

namespace path_equality
{
namespace
{

// The two items of a pair go into one document, [[x, y]], and the filter $[?@[0] == @[1]] selects the pair when the
// filter comparison of RFC 9535 2.3.5.2.2 holds. Each side keeps the bytes the test gives it, so the test chooses
// the encoding of each side.
std::expected<bool, error> values_equal(std::string const &x, std::string const &y)
{
    auto const l = test::lazy_of_copy("\x81\x82"s + x + y);
    if (!l)
        return std::unexpected(l.error());
    auto const q = cbor::path::compile<cbor::nodelist>("$[?@[0] == @[1]]");
    if (!q)
        return std::unexpected(q.error());
    auto const r = l->at_path(*q);
    if (!r)
        return std::unexpected(r.error());
    return r->size() == 1;
}

std::expected<bool, error> value_less(std::string const &x, std::string const &y)
{
    auto const l = test::lazy_of_copy("\x81\x82"s + x + y);
    if (!l)
        return std::unexpected(l.error());
    auto const q = cbor::path::compile<cbor::nodelist>("$[?@[0] < @[1]]");
    if (!q)
        return std::unexpected(q.error());
    auto const r = l->at_path(*q);
    if (!r)
        return std::unexpected(r.error());
    return r->size() == 1;
}

// A map with one pair, the key in the bytes the test gives, the value 1; the path names the key as an EDN literal
// (RFC 8949 5.6.1: the key of the path and the key of the map are equivalent or not).
std::expected<bool, error> key_found(std::string const &key, std::string_view const path)
{
    auto const l = test::lazy_of_copy("\xa1"s + key + "\x01"s);
    if (!l)
        return std::unexpected(l.error());
    auto const q = cbor::path::compile<cbor::lazy>(path);
    if (!q)
        return std::unexpected(q.error());
    auto const r = l->at_path(*q);
    if (!r) {
        if (r.error() == error::key_not_found)
            return false;
        return std::unexpected(r.error());
    }
    return true;
}

std::string const half_one = "\xf9\x3c\x00"s;
std::string const double_one = "\xfb\x3f\xf0\x00\x00\x00\x00\x00\x00"s;
std::string const half_negative_zero = "\xf9\x80\x00"s;
std::string const half_zero = "\xf9\x00\x00"s;
std::string const half_nan = "\xf9\x7e\x00"s;
std::string const half_negative_nan = "\xf9\xfe\x00"s;
std::string const double_nan = "\xfb\x7f\xf8\x00\x00\x00\x00\x00\x00"s;
std::string const double_nan_payload = "\xfb\x7f\xf8\x00\x00\x00\x00\x00\x01"s;

} // namespace
} // namespace path_equality

using namespace path_equality;

// RFC 9535 2.3.3: an integer alone in brackets is an index. With an encoding indicator of the EDN draft (4.1) it is a
// key literal, and RFC 8949 5.6.1 decides: numeric values are distinct unless they are numerically equal, whatever the
// length of the argument of the key.
TEST_CASE("path equality: a map key that is an integer with a longer argument")
{
    CHECK(key_found("\x18\x01"s, "$[1_0]") == true);
    CHECK(key_found("\x38\x00"s, "$[-1_0]") == true);
    CHECK(key_found("\x20"s, "$[1_0]") == false);
    CHECK(key_found("\x02"s, "$[1_0]") == false);
}

// RFC 8949 5.6.1: map keys are compared in the data model. The library folds a big number that fits 64 bits into its
// integer (its own data model; the generic one keeps them apart), also inside a key that is an array or a map. RFC 8949
// 3.4.3: the value of tag 3 is -1 - n, and the empty byte string is n = 0. A byte string and a tag other than 2 and 3
// stay distinct from a big number. A wider big number keeps its bytes, leading zeros included.
TEST_CASE("path equality: a map key that is a big number")
{
    CHECK(key_found("\xc2\x41\x01"s, "$[1]") == true);
    CHECK(key_found("\xc2\x42\x00\x01"s, "$[1]") == true);
    CHECK(key_found("\x01"s, "$[2(h'01')]") == true);
    CHECK(key_found("\xc3\x41\x00"s, "$[-1]") == true);
    CHECK(key_found("\xc3\x40"s, "$[-1_0]") == true);
    CHECK(key_found("\xc2\x41\x02"s, "$[1_0]") == false);
    CHECK(key_found("\xc3\x41\x01"s, "$[2(h'01')]") == false);
    CHECK(key_found("\xc1\x01"s, "$[2(h'01')]") == false);
    CHECK(key_found("\x41\x01"s, "$[2(h'01')]") == false);
    CHECK(key_found("\x81\xc2\x41\x01"s, "$[[1]]") == true);
    CHECK(key_found("\xa2\x01\x00\x01\x00"s, "$[{1: 0, 2(h'01'): 0}]") == true);
    CHECK(key_found("\xa2\x01\x00\xc2\x41\x01\x00"s, "$[{1: 0, 2: 0}]") == false);
    std::string const wide = "\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s;
    CHECK(key_found(wide, "$[2(h'010000000000000000')]") == true);
    CHECK(key_found("\xc2\x4a\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"s, "$[2(h'010000000000000000')]") == false);
}

// RFC 8949 5.6.1: a float key and an integer key are different keys; floats of any width compare by their value, so
// -0.0 and 0.0 are one key. A NaN key compares by its payload, widened to double, and its sign is not read.
TEST_CASE("path equality: a map key that is a float")
{
    CHECK(key_found(double_one, "$[1.0]") == true);
    CHECK(key_found(half_one, "$[1]") == false);
    CHECK(key_found("\x01"s, "$[1.0]") == false);
    CHECK(key_found(half_negative_zero, "$[0.0]") == true);
    CHECK(key_found(double_nan, "$[NaN]") == true);
    CHECK(key_found(half_negative_nan, "$[NaN]") == true);
    CHECK(key_found(double_nan_payload, "$[NaN]") == false);
    CHECK(key_found("\xf9\x40\x00"s, "$[1.0]") == false);
    CHECK(key_found("\xf9\x7c\x00"s, "$[NaN]") == false);
}

// RFC 8949 5.6.1: text strings are distinct from byte strings, even if composed of the same bytes. Simple values are
// equal if they have the same value, and a simple value is not an integer.
TEST_CASE("path equality: a map key that is a string or a simple value")
{
    CHECK(key_found("\x61\x61"s, "$[h'61']") == false);
    CHECK(key_found("\xf5"s, "$[false]") == false);
    CHECK(key_found("\xf8\x21"s, "$[simple(32)]") == false);
    CHECK(key_found("\x10"s, "$[simple(16)]") == false);
}

// RFC 8949 5.6.1: a map key that is a map is equivalent when it holds the same pairs, in any order. A key with a
// duplicate pair holds fewer distinct pairs than its count, and the key of the path holds no duplicate pair, so the
// two are never equal. Arrays are compared element by element, and an array is never a map. Tagged values are equal
// when the tag numbers and the contents are equal, and a tagged value is distinct from an untagged one.
TEST_CASE("path equality: a map key that is a map or an array")
{
    CHECK(key_found("\xa2\x61" "a\x01\x61" "b\x02"s, "$[{\"b\": 2, \"a\": 1}]") == true);
    CHECK(key_found("\xa2\x61" "a\x01\x61" "b\x03"s, "$[{\"b\": 2, \"a\": 1}]") == false);
    CHECK(key_found("\xa1\x03\x02"s, "$[{1: 2}]") == false);
    CHECK(key_found("\xa2\x61" "a\x01\x61" "a\x01"s, "$[{\"a\": 1}]") == false);
    CHECK(key_found("\xa2\x61" "k\x01\x61" "k\x01"s, "$[{\"k\": 1, \"j\": 1}]") == false);
    CHECK(key_found("\x82\x01\x02"s, "$[[1, 2]]") == true);
    CHECK(key_found("\x82\x01\x18\x02"s, "$[[1, 2]]") == true);
    CHECK(key_found("\x82\x02\x01"s, "$[[1, 2]]") == false);
    CHECK(key_found("\x81\x01"s, "$[[1, 2]]") == false);
    CHECK(key_found("\xa0"s, "$[[]]") == false);
    CHECK(key_found("\xc1\x18\x00"s, "$[1(0)]") == true);
    CHECK(key_found("\x00"s, "$[1(0)]") == false);
    CHECK(key_found("\x81\xa2\x61" "a\x01\x61" "b\x02"s, "$[[{\"b\": 2, \"a\": 1}]]") == true);
    CHECK(key_found("\xc6\xa2\x61" "a\x01\x61" "b\x02"s, "$[6({\"b\": 2, \"a\": 1})]") == true);
    CHECK(key_found("\xc7\xa2\x61" "a\x01\x61" "b\x02"s, "$[6({\"b\": 2, \"a\": 1})]") == false);
}

// The map side strips tag 55799 (self-described CBOR, RFC 8949 3.4.6) at each item it reads.
TEST_CASE("path equality: a map key under tag 55799")
{
    CHECK(key_found("\xd9\xd9\xf7\x82\x01\x02"s, "$[[1, 2]]") == true);
    CHECK(key_found("\x82\xd9\xd9\xf7\x01\x02"s, "$[[1, 2]]") == true);
}

// A map key of indefinite length is not read: the library refuses indefinite lengths in keys.
TEST_CASE("path equality: a map key of indefinite length")
{
    auto const r = key_found("\x9f\x01\x02\xff"s, "$[[1, 2]]");
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::indefinite_length);
}

// RFC 9535 2.3.5.2.2: numbers compare by value, so an integer and a float of the same value are equal.
TEST_CASE("path equality: numbers in a filter")
{
    CHECK(values_equal("\x01"s, half_one) == true);
    CHECK(values_equal(double_one, "\x01"s) == true);
    CHECK(values_equal("\x01"s, "\xf9\x3e\x00"s) == false);
    CHECK(values_equal(half_negative_zero, "\x00"s) == true);
    CHECK(values_equal(half_negative_zero, half_zero) == true);
    CHECK(value_less("\x01"s, "\xf9\x3e\x00"s) == true);
    CHECK(value_less("\xf9\x3e\x00"s, "\x02"s) == true);
}

// RFC 9535 2.3.5.2.2 compares numbers as numbers; a NaN equals nothing, also inside an array or a map.
TEST_CASE("path equality: NaN in a filter")
{
    CHECK(values_equal(half_nan, half_nan) == false);
    CHECK(values_equal("\x81"s + half_nan, "\x81"s + half_nan) == false);
    CHECK(values_equal("\xa1\x61" "a"s + half_nan, "\xa1\x61" "a"s + half_nan) == false);
    CHECK(values_equal("\xa1"s + half_nan + "\x01"s, "\xa1"s + half_nan + "\x01"s) == false);
}

// RFC 9535 2.3.5.2.2: objects are equal when they have the same names, each with an equal value; order does not
// matter. A map with a duplicate key is no object of I-JSON and equals nothing.
TEST_CASE("path equality: maps in a filter")
{
    CHECK(values_equal("\xa2\x61" "a\x01\x61" "b\x02"s, "\xa2\x61" "b\x02\x61" "a\x01"s) == true);
    CHECK(values_equal("\xa2\x61" "a\x01\x61" "b\x02"s, "\xa2\x61" "b\x02\x61" "a\x03"s) == false);
    CHECK(values_equal("\xa2\x61" "a\x01\x61" "a\x01"s, "\xa2\x61" "a\x01\x61" "a\x01"s) == false);
    CHECK(values_equal("\xa2\x01\x01\xf9\x3c\x00\x01"s, "\xa2\x01\x01\xf9\x3c\x00\x01"s) == false);
    CHECK(values_equal("\xa1\x01\x61" "x"s, "\xa1\xf9\x3c\x00\x61" "x"s) == true);
    CHECK(values_equal("\x81\xa2\x61" "a\x01\x61" "b\x02"s, "\x81\xa2\x61" "b\x02\x61" "a\x01"s) == true);
    CHECK(values_equal("\xc6\xa2\x61" "a\x01\x61" "b\x02"s, "\xc6\xa2\x61" "b\x02\x61" "a\x01"s) == true);
}

// RFC 9535 2.3.5.2.2: arrays are equal when they have the same length and equal elements in order.
TEST_CASE("path equality: arrays in a filter")
{
    CHECK(values_equal("\x82\x01\x02"s, "\x82\x01\x02"s) == true);
    CHECK(values_equal("\x82\x01\x02"s, "\x82\x02\x01"s) == false);
    CHECK(values_equal("\x82\x01\x02"s, "\x81\x01"s) == false);
}

// The filter reads the item that tag 24 embeds and the content of tag 55799 at each level, as the walk does. The owner
// decided on 2026-10-12 that correct means the same value in the RFC 8949 data model (3.4.3 for tags 2 and 3), so a
// bignum whose magnitude fits in 64 bits is that integer in a filter, also inside an array and a map, as a value and
// as a key. A leading zero byte does not count. A larger bignum and a tag 2 over a content that is
// not a byte string stay tags.
TEST_CASE("path equality: tags in a filter")
{
    CHECK(values_equal("\xd8\x18\x43\x82\x01\x02"s, "\x82\x01\x02"s) == true);
    CHECK(values_equal("\xd9\xd9\xf7\x82\x01\x02"s, "\x82\x01\x02"s) == true);
    CHECK(values_equal("\xc2\x41\x01"s, "\x01"s) == true);
    CHECK(values_equal("\xc2\x42\x00\x01"s, "\x01"s) == true);
    CHECK(values_equal("\xc3\x41\x00"s, "\x20"s) == true);
    CHECK(values_equal("\xc2\x41\x01"s, "\xf9\x3c\x00"s) == true);
    CHECK(values_equal("\x81\xc2\x41\x01"s, "\x81\x01"s) == true);
    CHECK(values_equal("\xc2\x41\x01"s, "\xc2\x41\x01"s) == true);
    CHECK(values_equal("\xc2\x41\x02"s, "\x01"s) == false);
    CHECK(values_equal("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s, "\x1b\xff\xff\xff\xff\xff\xff\xff\xff"s) == false);
    CHECK(values_equal("\xc2\x61\x61"s, "\x01"s) == false);
    CHECK(values_equal("\xc3\x40"s, "\x20"s) == true);
    CHECK(values_equal("\xc2\x49\x00\xff\xff\xff\xff\xff\xff\xff\xff"s, "\x1b\xff\xff\xff\xff\xff\xff\xff\xff"s) == true);
    CHECK(values_equal("\xa1\x61\x61\xc2\x41\x01"s, "\xa1\x61\x61\x01"s) == true);
    CHECK(values_equal("\xa1\xc2\x41\x01\x61\x61"s, "\xa1\x01\x61\x61"s) == true);
}

// RFC 9535 2.3.5.2.2 orders two numbers by value, and a bignum that fits in 64 bits is a number (owner, 2026-10-12).
TEST_CASE("path equality: a bignum in an order comparison of a filter")
{
    std::string const items = "\x83\xc2\x41\x01\xc2\x42\x00\x01\xc3\x41\x00"s;
    auto const count = [&](std::string_view const path) {
        auto const l = test::lazy_of_copy(items);
        REQUIRE(l.has_value());
        auto const q = cbor::path::compile<cbor::nodelist>(path);
        REQUIRE(q.has_value());
        auto const r = l->at_path(*q);
        REQUIRE(r.has_value());
        return r->size();
    };
    CHECK_EQ(count("$[?@ == 1]"), 2u);
    CHECK_EQ(count("$[?@ == -1]"), 1u);
    CHECK_EQ(count("$[?@ == 1.0]"), 2u);
    CHECK_EQ(count("$[?@ < 2]"), 3u);
    CHECK_EQ(count("$[?@ > 0]"), 2u);
    CHECK_EQ(count("$[?@ < 0]"), 1u);
}

// An element of a typed array (RFC 8746) is a number in a filter, compared with a number of the path.
TEST_CASE("path equality: an element of a typed array in a filter")
{
    auto const l = test::lazy_of_copy("\x81\xd8\x40\x42\x01\x02"s);
    REQUIRE(l.has_value());
    auto const q = cbor::path::compile<cbor::nodelist>("$[0][?@ == 2]");
    REQUIRE(q.has_value());
    auto const r = l->at_path(*q);
    REQUIRE(r.has_value());
    CHECK_EQ(r->size(), 1u);
}

// RFC 9535 2.3.5.2.2 asks for mathematical comparison of numbers that interoperate under I-JSON and leaves the rest to
// the implementation. The library compares an integer and a float by their exact values, so that ==, < and > agree
// and equality is transitive: 2^53 + 1 is greater than the float 2^53, and 2^64 - 1 is less than the float 2^64.
TEST_CASE("path equality: an integer and a float compare by their exact values")
{
    std::string const two_53_plus_1 = "\x1b\x00\x20\x00\x00\x00\x00\x00\x01"s;
    std::string const float_two_53 = "\xfb\x43\x40\x00\x00\x00\x00\x00\x00"s;
    CHECK(values_equal(two_53_plus_1, float_two_53) == false);
    CHECK(value_less(float_two_53, two_53_plus_1) == true);
    CHECK(value_less(two_53_plus_1, float_two_53) == false);
    std::string const two_64_minus_1 = "\x1b\xff\xff\xff\xff\xff\xff\xff\xff"s;
    std::string const float_two_64 = "\xfa\x5f\x80\x00\x00"s;
    CHECK(value_less(two_64_minus_1, float_two_64) == true);
    CHECK(values_equal(two_64_minus_1, float_two_64) == false);
    std::string const minus_two_64 = "\x3b\xff\xff\xff\xff\xff\xff\xff\xff"s;
    std::string const float_minus_two_64 = "\xfa\xdf\x80\x00\x00"s;
    CHECK(values_equal(minus_two_64, float_minus_two_64) == true);
    CHECK(value_less("\x20"s, "\xf9\xbc\x00"s) == false);
    CHECK(values_equal("\x20"s, "\xf9\xbc\x00"s) == true);
    CHECK(value_less("\x20"s, "\xf9\xbd\x00"s) == false);
    CHECK(value_less("\xf9\xbd\x00"s, "\x20"s) == true);
    CHECK(value_less("\x20"s, "\x00"s) == true);
}

namespace path_equality
{
namespace
{

// levels maps, each {"a": inner, "b": 1}, the innermost 1; the second form writes "b" first. Its size is linear in
// levels.
std::string nested_maps(std::size_t const levels, bool const b_first)
{
    std::string bytes = "\x01"s;
    for (std::size_t i = 0; i < levels; ++i)
        bytes = b_first ? "\xa2\x61" "b\x01\x61" "a"s + bytes : "\xa2\x61" "a"s + bytes + "\x61" "b\x01"s;
    return bytes;
}

// The EDN literal of nested_maps(levels, true), for a key literal of a path.
std::string nested_maps_in_edn(std::size_t const levels)
{
    std::string text = "1";
    for (std::size_t i = 0; i < levels; ++i)
        text = "{\"b\": 1, \"a\": " + text + "}";
    return text;
}

} // namespace
} // namespace path_equality

// The comparison of two maps reads each item once and gives each distinct subtree one id (tree isomorphism, Aho,
// Hopcroft and Ullman). A count of the pairs of each map in both maps, as before, made two recursive comparisons for
// each level and took time 2^levels; 40 levels compare here at once, in a filter and as the map key of a path.
TEST_CASE("path equality: nested maps compare in time linear in their size")
{
    CHECK(values_equal(nested_maps(40, false), nested_maps(40, true)) == true);
    CHECK(values_equal(nested_maps(40, false), nested_maps(39, true)) == false);
    CHECK(key_found(nested_maps(40, false), "$[" + nested_maps_in_edn(40) + "]") == true);
    CHECK(key_found(nested_maps(40, false), "$[" + nested_maps_in_edn(39) + "]") == false);
}

// RFC 8949 3.3: a simple value below 24 in the two-byte form is not well-formed. The comparison of two maps reads
// both whole and refuses such an item with syntax_error, in the filter as in the map key.
TEST_CASE("path equality: a map that holds a simple value in the two-byte form below 24")
{
    std::string const malformed = "\xa1\x61" "a\xf8\x14"s;
    auto const r = values_equal(malformed, "\xa1\x61" "a\xf4"s);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::syntax_error);
    auto const k = key_found("\xa1\x61" "a\xf8\x14"s, "$[{\"a\": false}]");
    REQUIRE_FALSE(k.has_value());
    CHECK_EQ(k.error(), error::syntax_error);
}
