#include "binding.hpp"

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

constexpr std::size_t limit = cbor::validity::nesting_depth_limit;

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

// clang with AddressSanitizer gives decoding::value_decode and jsonpath::segment_apply more than 8 KiB of
// stack for each level, so the item at the limit needs more than 8 MiB of stack there. The test runs in every
// other build.
#if !(defined(__clang__) && defined(__SANITIZE_ADDRESS__))
// decoding::value_decode counts one level for each array.
TEST_CASE("nesting depth limit: lazy_decode with a binding")
{
    test::nesting_depth_max_guard const depth{limit};
    test_binding binding;
    CHECK(cbor::lazy_decode(binding, *cbor::decode(arrays_around(limit, "\x00"sv))).has_value());
    CHECK_EQ(cbor::lazy_decode(binding, *cbor::decode(arrays_around(limit + 1, "\x00"sv))).error(),
             error::nesting_depth_exceeded);
}
#endif

// value_sharing::item_decode counts one level for each tag, and none for an array or a map.
TEST_CASE("nesting depth limit: lazy::decode")
{
    test::nesting_depth_max_guard const depth{limit};
    auto const deepest = cbor::lazy::from(repeat("\xd8\x64"sv, limit) + '\x00');
    REQUIRE(deepest.has_value());
    CHECK(deepest->decode().has_value());
    auto const deeper = cbor::lazy::from(repeat("\xd8\x64"sv, limit + 1) + '\x00');
    REQUIRE(deeper.has_value());
    CHECK_EQ(deeper->decode().error(), error::nesting_depth_exceeded);
}

// validity::keys_equivalent compares two keys that differ only in the innermost item, so it recurses through
// every level of both keys.
TEST_CASE("nesting depth limit: check_sorted_keys_unique")
{
    auto const checked = [](std::size_t const n) {
        std::string const map =
            "\xa2"s + arrays_around(n, "\x00"sv) + "\x00"s + arrays_around(n, "\x01"sv) + "\x00"s;
        std::string_view const encoded = map;
        return cbor::validity::check_sorted_keys_unique(encoded, 1, 2, 1, limit);
    };
    CHECK(checked(limit - 1).has_value());
    CHECK_EQ(checked(limit).error(), error::nesting_depth_exceeded);
}

// jsonpath::value_equal compares two equal values of RFC 9535 2.3.5.2.2 through every level.
TEST_CASE("nesting depth limit: a comparison in a filter")
{
    test::nesting_depth_max_guard const depth{limit};
    auto const found = [](std::size_t const n) {
        std::string const doc = "\x81\xa3\x61"
                                "a"s +
                                arrays_around(n, "\x00"sv) +
                                "\x61"
                                "b"s +
                                arrays_around(n, "\x00"sv) +
                                "\x61"
                                "c\x01"s;
        test_binding binding;
        return cbor::at_path(binding, "$[?@.a == @.b].c", *cbor::decode(doc));
    };
    CHECK(found(limit).has_value());
    CHECK_EQ(found(limit + 1).error(), error::nesting_depth_exceeded);
}

// clang with AddressSanitizer gives decoding::value_decode and jsonpath::segment_apply more than 8 KiB of
// stack for each level, so the item at the limit needs more than 8 MiB of stack there. The test runs in every
// other build.
#if !(defined(__clang__) && defined(__SANITIZE_ADDRESS__))
// jsonpath::segment_apply recurses once for each level that a descendant segment enters.
TEST_CASE("nesting depth limit: a descendant segment")
{
    test::nesting_depth_max_guard const depth{limit};
    test_binding binding;
    CHECK(cbor::at_path(binding, "$..[0]", *cbor::decode(arrays_around(limit, "\x00"sv))).has_value());
    CHECK_EQ(cbor::at_path(binding, "$..[0]", *cbor::decode(arrays_around(limit + 1, "\x00"sv))).error(),
             error::nesting_depth_exceeded);
}
#endif

// inspect::diagnostic_write counts one level for each array.
TEST_CASE("nesting depth limit: inspect")
{
    test::nesting_depth_max_guard const depth{limit};
    CHECK(cbor::inspect(arrays_around(limit, "\x00"sv)).has_value());
    CHECK_EQ(cbor::inspect(arrays_around(limit + 1, "\x00"sv)).error(), error::nesting_depth_exceeded);
}

// The walker of encode counts one level for each array.
TEST_CASE("nesting depth limit: encode")
{
    test::nesting_depth_max_guard const depth{limit};
    test_binding binding;
    string_writer deepest;
    CHECK(cbor::encode(binding, deepest, arrays_around_zero(limit)).has_value());
    string_writer deeper;
    auto const r = cbor::encode(binding, deeper, arrays_around_zero(limit + 1));
    REQUIRE_FALSE(r.has_value());
    CHECK((r.error() == cbor::error{error::nesting_depth_exceeded}));
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

// databind::generic_read counts one level for the array of each struct and one for its element, so the empty
// array of the innermost struct of n levels is at depth 2 n + 1.
TEST_CASE("nesting depth limit: databind")
{
    test::nesting_depth_max_guard const depth{limit};
    CHECK(cbor::databind<nest>::decode(nests_around(limit / 2 - 1)).has_value());
    CHECK_EQ(cbor::databind<nest>::decode(nests_around(limit / 2)).error(), error::nesting_depth_exceeded);
}

#endif
