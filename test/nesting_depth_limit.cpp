#include "reference.hpp"
#include "owned_copy.hpp"
#include "sequence_reader.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each function that recurses reads an item nested as deep as validity::nesting_depth_limit allows, and
// refuses the item one level deeper. This binary is built with the nesting depth at the limit, so the stack
// of the thread holds the deepest item that the library accepts.

namespace
{

constexpr std::size_t depth_limit = cbor::limits::nesting_depth;

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

// The build of this binary sets the nesting depth to its hard bound, so every test below runs at the bound.
TEST_CASE("nesting depth depth_limit: the build sets the nesting depth to the hard bound")
{
    CHECK_EQ(depth_limit, cbor::validity::nesting_depth_limit);
}

// The decode of the item tree counts one level for each array.
TEST_CASE("nesting depth depth_limit: decode of the item tree")
{
    CHECK(test::value_of(*test::lazy_of_copy(arrays_around(depth_limit, "\x00"sv))).has_value());
    CHECK_EQ(test::value_of(*test::lazy_of_copy(arrays_around(depth_limit + 1, "\x00"sv))).error(),
             error::nesting_depth_exceeded);
}

// lazy::tree_decoder::item_decode counts one level for each tag, and none for an array or a map.
TEST_CASE("nesting depth depth_limit: lazy::clone")
{
    auto const deepest = test::lazy_of_copy(repeat("\xd8\x64"sv, depth_limit) + '\x00');
    REQUIRE(deepest.has_value());
    CHECK(deepest->clone().has_value());
    auto const deeper = test::lazy_of_copy(repeat("\xd8\x64"sv, depth_limit + 1) + '\x00');
    REQUIRE(deeper.has_value());
    CHECK_EQ(deeper->clone().error(), error::nesting_depth_exceeded);
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
        return cbor::validity::keys_equivalent<false>(first, cbor::validity::offset{}, second,
                                                      cbor::validity::offset{}, 1, depth_limit);
    };
    CHECK(checked(depth_limit - 1).has_value());
    CHECK_EQ(checked(depth_limit).error(), error::nesting_depth_exceeded);
}

// jsonpath::value_equal compares two equal values of RFC 9535 2.3.5.2.2 through every level.
TEST_CASE("nesting depth depth_limit: a comparison in a filter")
{
    auto const found = [](std::size_t const n) {
        std::string const doc = "\x81\xa3\x61"
                                "a"s +
                                arrays_around(n, "\x00"sv) +
                                "\x61"
                                "b"s +
                                arrays_around(n, "\x00"sv) +
                                "\x61"
                                "c\x01"s;
        return test::query("$[?@.a == @.b].c", *test::lazy_of_copy(doc));
    };
    CHECK(found(depth_limit).has_value());
    CHECK_EQ(found(depth_limit + 1).error(), error::nesting_depth_exceeded);
}

// jsonpath::segment_apply recurses once for each level that a descendant segment enters.
TEST_CASE("nesting depth depth_limit: a descendant segment")
{
    CHECK(test::query("$..[0]", *test::lazy_of_copy(arrays_around(depth_limit, "\x00"sv))).has_value());
    CHECK_EQ(test::query("$..[0]", *test::lazy_of_copy(arrays_around(depth_limit + 1, "\x00"sv))).error(),
             error::nesting_depth_exceeded);
}

// extended_diagnostic_notation::diagnostic_write counts one level for each array.
TEST_CASE("nesting depth depth_limit: diagnostic_notation")
{
    CHECK(cbor::diagnostic_notation(arrays_around(depth_limit, "\x00"sv)).has_value());
    CHECK_EQ(cbor::diagnostic_notation(arrays_around(depth_limit + 1, "\x00"sv)).error(), error::nesting_depth_exceeded);
}

// encode of a tree counts one level for each array, as decode does. The hard bound admits no deeper tree, so
// the deepest tree that decode gives is encoded, and the stack of the thread holds it.
TEST_CASE("nesting depth depth_limit: encode")
{
    auto const deepest = test::lazy_of_copy(preferred(arrays_around_zero(depth_limit)))->clone();
    REQUIRE(deepest.has_value());
    std::string out;
    CHECK(cbor::encode(**deepest, out).has_value());
}

// The decode recurses once for each level of nesting. At the hard bound of the nesting depth, an array in an
// array 512 times and a tag on a tag 512 times decode with no overflow of the stack, in a debug build with
// AddressSanitizer too.
TEST_CASE("decode: the hard bound of the nesting depth fits the stack")
{
    constexpr std::size_t bound = depth_limit;
    auto const arrays = test::lazy_of_copy(std::string(bound, '\x81') + '\x00');
    REQUIRE(arrays.has_value());
    CHECK(arrays->clone().has_value());
    std::string tags;
    for (std::size_t i = 0; i < bound; ++i)
        tags += "\xd8\x64"sv;
    tags += '\x00';
    auto const tagged = cbor::lazy::take_ownership(std::move(tags));
    REQUIRE(tagged.has_value());
    CHECK(tagged->clone().has_value());
    // [28([[...[0]...]]), [[...[29(0)]...]]]: a lazy at the second element meets the reference near the bound
    // and reads the mark at the depth of its own position, so the stack holds the frames of both nests.
    std::string const shared = "\x82\xd8\x1c"s + std::string(bound - 2, '\x81') + '\x00' +
                               std::string(bound - 3, '\x81') + "\xd8\x1d\x00"s;
    auto const whole = test::lazy_of_copy(shared);
    REQUIRE(whole.has_value());
    CHECK(whole->clone().has_value());
    auto const second = whole->at(1);
    REQUIRE(second.has_value());
    CHECK(second->clone().has_value());
}


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
    CHECK(cbor::structs<nest>::decode(nests_around(depth_limit / 2 - 1)).has_value());
    CHECK_EQ(cbor::structs<nest>::decode(nests_around(depth_limit / 2)).error(), error::nesting_depth_exceeded);
}


// The stack of the reader has one count per level up to the hard bound of the nesting depth, so an item at the
// bound is read and one level more is refused, without a write past the end of the stack.
TEST_CASE("nesting depth limit: sequence_reader reads an item at the bound and refuses one level more")
{
    std::string const at_bound = std::string(depth_limit, '\x81') + "\x00"s;
    std::string const deeper = std::string(depth_limit + 1, '\x81') + "\x00"s;
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        CHECK_EQ(test::reader_outcome_split_at(at_bound, depth_limit / 2, form).items.size(), 1u);
        CHECK_EQ(test::reader_outcome_split_at(deeper, depth_limit / 2, form).last,
                 error::nesting_depth_exceeded);
    }
}
