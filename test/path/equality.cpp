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
    auto const q = cbor::jsonpath::compile<cbor::nodelist>("$[?@[0] == @[1]]");
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
    auto const q = cbor::jsonpath::compile<cbor::nodelist>("$[?@[0] < @[1]]");
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
    auto const q = cbor::jsonpath::compile<cbor::lazy>(path);
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

// RFC 8949 5.6.1: map keys are compared in the data model. The library folds a big number that fits 64 bits into its
// integer (its own data model; the generic one keeps them apart). A wider big number keeps its bytes, leading zeros
// included.
TEST_CASE("path equality: a map key that is a big number")
{
    CHECK(key_found("\xc2\x41\x01"s, "$[1]") == true);
    CHECK(key_found("\xc2\x42\x00\x01"s, "$[1]") == true);
    CHECK(key_found("\x01"s, "$[2(h'01')]") == true);
    CHECK(key_found("\xc3\x41\x00"s, "$[-1]") == true);
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
}

// RFC 8949 5.6.1: a map key that is a map is equivalent when it holds the same pairs, in any order. A key with a
// duplicate pair has more pairs than the key of the path, which has none.
TEST_CASE("path equality: a map key that is a map or an array")
{
    CHECK(key_found("\xa2\x61" "a\x01\x61" "b\x02"s, "$[{\"b\": 2, \"a\": 1}]") == true);
    CHECK(key_found("\xa2\x61" "a\x01\x61" "b\x03"s, "$[{\"b\": 2, \"a\": 1}]") == false);
    CHECK(key_found("\xa2\x61" "a\x01\x61" "a\x01"s, "$[{\"a\": 1}]") == false);
    CHECK(key_found("\x82\x01\x02"s, "$[[1, 2]]") == true);
    CHECK(key_found("\x82\x02\x01"s, "$[[1, 2]]") == false);
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

// The filter reads the item that tag 24 embeds and the content of tag 55799 at each level, as the walk does; a big
// number stays a tag in a filter and is not an integer there.
TEST_CASE("path equality: tags in a filter")
{
    CHECK(values_equal("\xd8\x18\x43\x82\x01\x02"s, "\x82\x01\x02"s) == true);
    CHECK(values_equal("\xd9\xd9\xf7\x82\x01\x02"s, "\x82\x01\x02"s) == true);
    CHECK(values_equal("\xc2\x41\x01"s, "\x01"s) == false);
    CHECK(values_equal("\xc2\x41\x01"s, "\xc2\x41\x01"s) == true);
}

// An element of a typed array (RFC 8746) is a number in a filter, compared with a number of the path.
TEST_CASE("path equality: an element of a typed array in a filter")
{
    auto const l = test::lazy_of_copy("\x81\xd8\x40\x42\x01\x02"s);
    REQUIRE(l.has_value());
    auto const q = cbor::jsonpath::compile<cbor::nodelist>("$[0][?@ == 2]");
    REQUIRE(q.has_value());
    auto const r = l->at_path(*q);
    REQUIRE(r.has_value());
    CHECK_EQ(r->size(), 1u);
}
