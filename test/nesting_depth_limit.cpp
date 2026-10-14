#include "reference.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each function that recurses reads an item nested as deep as validity::nesting_depth_limit allows, and
// refuses the item one level deeper. The nesting depth in force is the limit, so the stack of the thread
// holds the deepest item that the library accepts.

namespace
{

constexpr std::size_t depth_limit = cbor::validity::nesting_depth_limit;

std::string arrays_around(std::size_t const n, std::string_view const innermost)
{
    return std::string(n, '\x81') + std::string(innermost);
}

value arrays_around_zero(std::size_t const n)
{
    value v = V(0);
    for (std::size_t i = 0; i < n; ++i)
        v = A(v);
    return v;
}

} // namespace

// The decode of the item tree counts one level for each array.
TEST_CASE("nesting depth depth_limit: decode of the item tree")
{
    test::limits_guard const depth{{.nesting_depth = depth_limit}};
    CHECK(test::value_of(*cbor::lazy::copy(arrays_around(depth_limit, "\x00"sv))).has_value());
    CHECK_EQ(test::value_of(*cbor::lazy::copy(arrays_around(depth_limit + 1, "\x00"sv))).error(),
             error::nesting_depth_exceeded);
}

// lazy::tree_decoder::item_decode counts one level for each tag, and none for an array or a map.
TEST_CASE("nesting depth depth_limit: lazy::decode")
{
    test::limits_guard const depth{{.nesting_depth = depth_limit}};
    auto const deepest = cbor::lazy::copy(repeat("\xd8\x64"sv, depth_limit) + '\x00');
    REQUIRE(deepest.has_value());
    CHECK(deepest->decode().has_value());
    auto const deeper = cbor::lazy::copy(repeat("\xd8\x64"sv, depth_limit + 1) + '\x00');
    REQUIRE(deeper.has_value());
    CHECK_EQ(deeper->decode().error(), error::nesting_depth_exceeded);
}

// validity::keys_equivalent compares two keys that differ only in the innermost item, so it recurses through
// every level of both keys.
TEST_CASE("nesting depth depth_limit: keys_equivalent")
{
    auto const checked = [](std::size_t const n) {
        std::string const a = arrays_around(n, "\x00"sv);
        std::string const b = arrays_around(n, "\x01"sv);
        std::string_view const first = a;
        std::string_view const second = b;
        return cbor::validity::keys_equivalent(first, cbor::validity::offset{}, second, cbor::validity::offset{}, 1, depth_limit,
                                               cbor::validity::limit_checks<false>{});
    };
    CHECK(checked(depth_limit - 1).has_value());
    CHECK_EQ(checked(depth_limit).error(), error::nesting_depth_exceeded);
}

// jsonpath::value_equal compares two equal values of RFC 9535 2.3.5.2.2 through every level.
TEST_CASE("nesting depth depth_limit: a comparison in a filter")
{
    test::limits_guard const depth{{.nesting_depth = depth_limit}};
    auto const found = [](std::size_t const n) {
        std::string const doc = "\x81\xa3\x61"
                                "a"s +
                                arrays_around(n, "\x00"sv) +
                                "\x61"
                                "b"s +
                                arrays_around(n, "\x00"sv) +
                                "\x61"
                                "c\x01"s;
        return test::query("$[?@.a == @.b].c", *cbor::lazy::copy(doc));
    };
    CHECK(found(depth_limit).has_value());
    CHECK_EQ(found(depth_limit + 1).error(), error::nesting_depth_exceeded);
}

// jsonpath::segment_apply recurses once for each level that a descendant segment enters.
TEST_CASE("nesting depth depth_limit: a descendant segment")
{
    test::limits_guard const depth{{.nesting_depth = depth_limit}};
    CHECK(test::query("$..[0]", *cbor::lazy::copy(arrays_around(depth_limit, "\x00"sv))).has_value());
    CHECK_EQ(test::query("$..[0]", *cbor::lazy::copy(arrays_around(depth_limit + 1, "\x00"sv))).error(),
             error::nesting_depth_exceeded);
}

// extended_diagnostic_notation::diagnostic_write counts one level for each array.
TEST_CASE("nesting depth depth_limit: diagnostic_notation")
{
    test::limits_guard const depth{{.nesting_depth = depth_limit}};
    CHECK(cbor::diagnostic_notation(arrays_around(depth_limit, "\x00"sv)).has_value());
    CHECK_EQ(cbor::diagnostic_notation(arrays_around(depth_limit + 1, "\x00"sv)).error(), error::nesting_depth_exceeded);
}

// encode of a tree counts one level for each array, as decode does. The hard bound admits no deeper tree, so the
// tree at the bound is encoded under the bound and under one level less.
TEST_CASE("nesting depth depth_limit: encode")
{
    test::limits_guard const depth{{.nesting_depth = depth_limit}};
    auto const deepest = cbor::lazy::copy(preferred(arrays_around_zero(depth_limit)))->decode();
    REQUIRE(deepest.has_value());
    std::string out;
    CHECK(cbor::encode(**deepest, out).has_value());
    test::limits_guard const lower{{.nesting_depth = depth_limit - 1}};
    std::string more;
    auto const r = cbor::encode(**deepest, more);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::nesting_depth_exceeded);
}

#ifdef __cpp_impl_reflection

namespace
{

struct nest {
    std::vector<nest> c;
};

std::string nests_around(std::size_t const n)
{
    return repeat("\xa1\x61"
                  "c\x81"sv,
                  n) +
           "\xa1\x61"
           "c\x80"s;
}

} // namespace

// structs::generic_read counts one level for the array of each struct and one for its element, so the empty
// array of the innermost struct of n levels is at depth 2 n + 1.
TEST_CASE("nesting depth depth_limit: structs")
{
    test::limits_guard const depth{{.nesting_depth = depth_limit}};
    CHECK(cbor::structs<nest>::decode(nests_around(depth_limit / 2 - 1)).has_value());
    CHECK_EQ(cbor::structs<nest>::decode(nests_around(depth_limit / 2)).error(), error::nesting_depth_exceeded);
}

#endif
