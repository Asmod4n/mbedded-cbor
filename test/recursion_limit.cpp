#include "small_stack.hpp"
#include "reference.hpp"
#include "owned_copy.hpp"
#include "sequence_reader.hpp"
#include <cbor/structs.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each function that recurses reads an item that every stack holds, and refuses an item deeper than a small stack
// holds with nesting_depth_exceeded, on a thread with that small stack, and does not run past the stack. No depth is
// fixed: the end of the stack of the thread decides.

namespace
{

constexpr std::size_t held = test::levels_every_stack_holds;
constexpr std::size_t beyond = test::levels_beyond_small_stack;

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

// A read on the small stack stops with nesting_depth_exceeded.
template <class Read>
void refused_on_small_stack(Read const &read)
{
    CHECK_EQ(test::error_on_small_stack(read), error::nesting_depth_exceeded);
}

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

// The decode of the item tree recurses once for each array.
TEST_CASE("recursion limit: decode of the item tree")
{
    CHECK(test::value_of(*test::lazy_of_copy(arrays_around(held, "\x00"sv))).has_value());
    std::string const deep = arrays_around(beyond, "\x00"sv);
    refused_on_small_stack([&deep] { return test::value_of(*test::lazy_of_copy(deep)); });
}

// lazy::clone decodes through cbor::detail::tree_decoder::decode_item. It recurses once for each tag.
TEST_CASE("recursion limit: lazy::clone")
{
    auto const read = test::lazy_of_copy(repeat("\xd8\x64"sv, held) + '\x00');
    REQUIRE(read.has_value());
    CHECK(read->clone().has_value());
    auto const deep = test::lazy_of_copy(repeat("\xd8\x64"sv, beyond) + '\x00');
    REQUIRE(deep.has_value());
    refused_on_small_stack([&deep] { return deep->clone(); });
}

// detail::validity::keys_equivalent compares two keys that differ only in the innermost item, so it recurses
// through every level of both keys.
TEST_CASE("recursion limit: keys_equivalent")
{
    auto const checked = [](std::size_t const n) {
        std::string const a = arrays_around(n, "\x00"sv);
        std::string const b = arrays_around(n, "\x01"sv);
        std::string_view const first = a;
        std::string_view const second = b;
        return cbor::detail::validity::keys_equivalent<false>(first, cbor::detail::validity::offset{}, second,
                                                              cbor::detail::validity::offset{});
    };
    auto const read = checked(held);
    REQUIRE(read.has_value());
    CHECK_FALSE(*read);
    refused_on_small_stack([&checked] { return checked(beyond); });
}

// jsonpath::value_equal compares two equal values of RFC 9535 2.3.5.2.2 through every level.
TEST_CASE("recursion limit: a comparison in a filter")
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
    CHECK(found(held).has_value());
    refused_on_small_stack([&found] { return found(beyond); });
}

// jsonpath::apply_segment recurses once for each level that a descendant segment enters.
TEST_CASE("recursion limit: a descendant segment")
{
    CHECK(test::query("$..[0]", *test::lazy_of_copy(arrays_around(held, "\x00"sv))).has_value());
    std::string const deep = arrays_around(beyond, "\x00"sv);
    refused_on_small_stack([&deep] { return test::query("$..[0]", *test::lazy_of_copy(deep)); });
}

// extended_diagnostic_notation::write_diagnostic recurses once for each array.
TEST_CASE("recursion limit: diagnostic_notation")
{
    CHECK(cbor::diagnostic_notation(arrays_around(held, "\x00"sv)).has_value());
    std::string const deep = arrays_around(beyond, "\x00"sv);
    refused_on_small_stack([&deep] { return cbor::diagnostic_notation(deep); });
}

// encode of a tree recurses once for each array, as decode does. The tree comes from a decode on the main thread;
// its encode on the small stack stops where that stack ends.
TEST_CASE("recursion limit: encode")
{
    auto const read = test::lazy_of_copy(preferred(arrays_around_zero(held)))->clone();
    REQUIRE(read.has_value());
    std::string out;
    CHECK(cbor::encode(**read, out).has_value());
    auto const deep = test::lazy_of_copy(arrays_around(test::levels_main_thread_holds, "\x00"sv))->clone();
    if (!deep) {
        CHECK_EQ(deep.error(), error::nesting_depth_exceeded);
        return;
    }
    auto const failure = test::error_on_small_stack([&deep] {
        std::string written;
        return cbor::encode(**deep, written);
    });
    if (failure)
        CHECK_EQ(*failure, error::nesting_depth_exceeded);
}

// A shared value and a reference to it: a lazy at the second element meets the reference and reads the mark at the
// depth of its own position, so the stack holds the frames of both nests.
TEST_CASE("recursion limit: a shared value read through its reference")
{
    std::string const shared = "\x82\xd8\x1c"s + std::string(held, '\x81') + '\x00' + std::string(held, '\x81') +
                               "\xd8\x1d\x00"s;
    auto const whole = test::lazy_of_copy(shared);
    REQUIRE(whole.has_value());
    CHECK(whole->clone().has_value());
    auto const second = whole->at(1);
    REQUIRE(second.has_value());
    CHECK(second->clone().has_value());
}

// structs::read_generic recurses once for the array of each struct and once for its element.
TEST_CASE("recursion limit: structs")
{
    CHECK(cbor::structs<nest>::decode(nests_around(held)).has_value());
    std::string const deep = nests_around(beyond);
    refused_on_small_stack([&deep] { return cbor::structs<nest>::decode(std::string(deep)); });
}

// The sequence reader counts the items that are still to read and does not recurse: it reads an item of any depth,
// also one that a small stack does not hold, and also when the item arrives in two parts.
TEST_CASE("recursion limit: sequence_reader reads an item of any depth")
{
    std::string const deep = std::string(beyond, '\x81') + "\x00"s;
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        CHECK_EQ(test::reader_outcome_split_at(deep, beyond / 2, form).items.size(), 1u);
    }
}
