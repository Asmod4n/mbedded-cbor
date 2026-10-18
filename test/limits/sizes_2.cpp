#include "../owned_copy.hpp"
#include "../reference.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// This binary is built with -DCBOR_STRING_LENGTH_DEFAULT=2 and -DCBOR_CONTAINER_ELEMENTS_DEFAULT=2, as a user
// sets the size limits. Each limit is read at the bound, one below and one above it. The expectation is the
// decision of the owner of 2026-10-09: a value up to the limit passes, a value above it fails with the error
// of that limit.

namespace
{

constexpr std::size_t limit = 2;

std::string text_of_length(std::size_t const n)
{
    return std::string(1, static_cast<char>(0x60 + n)) + std::string(n, 'a');
}

std::string bytes_of_length(std::size_t const n)
{
    return std::string(1, static_cast<char>(0x40 + n)) + std::string(n, 'b');
}

std::string array_of(std::size_t const n)
{
    return std::string(1, static_cast<char>(0x80 + n)) + std::string(n, '\x00');
}

std::string map_of(std::size_t const n)
{
    return std::string(1, static_cast<char>(0xa0 + n)) + std::string(2 * n, '\x00');
}

} // namespace

TEST_CASE("limits sizes 2: the build sets both size limits and keeps the default nesting depth")
{
    CHECK_EQ(cbor::limits::string_length, limit);
    CHECK_EQ(cbor::limits::container_elements, limit);
    CHECK_EQ(cbor::limits::nesting_depth, 128u);
}

// The length of each string head is checked in every reader: the well-formedness walk, the lazy item, the
// item tree and the diagnostic notation.
TEST_CASE("limits sizes 2: string_length on each string head")
{
    for (std::size_t const n : {limit - 1, limit, limit + 1}) {
        CAPTURE(n);
        bool const passes = n <= limit;
        for (std::string const &encoded :
             {text_of_length(n), bytes_of_length(n), "\x82"s + text_of_length(n) + "\x00"s}) {
            auto const size = cbor::item_size(encoded);
            auto const decoded = test::value_of(*test::lazy_of_copy(encoded));
            auto const notation = cbor::diagnostic_notation(encoded);
            CHECK_EQ(size.has_value(), passes);
            CHECK_EQ(decoded.has_value(), passes);
            CHECK_EQ(notation.has_value(), passes);
            if (!passes) {
                CHECK_EQ(size.error(), error::string_length_exceeded);
                CHECK_EQ(decoded.error(), error::string_length_exceeded);
                CHECK_EQ(notation.error(), error::string_length_exceeded);
            }
        }
        CHECK_EQ(test::lazy_of_copy(text_of_length(n))->get<std::string_view>().has_value(), passes);
    }
}

// The count of each array and map head is checked; a map counts its pairs.
TEST_CASE("limits sizes 2: container_elements on each array and map head")
{
    for (std::size_t const n : {limit - 1, limit, limit + 1}) {
        CAPTURE(n);
        bool const passes = n <= limit;
        for (std::string const &encoded : {array_of(n), map_of(n), "\x81"s + array_of(n)}) {
            auto const size = cbor::item_size(encoded);
            auto const decoded = test::value_of(*test::lazy_of_copy(encoded));
            auto const notation = cbor::diagnostic_notation(encoded);
            CHECK_EQ(size.has_value(), passes);
            CHECK_EQ(decoded.has_value(), passes);
            CHECK_EQ(notation.has_value(), passes);
            if (!passes) {
                CHECK_EQ(size.error(), error::container_elements_exceeded);
                CHECK_EQ(decoded.error(), error::container_elements_exceeded);
                CHECK_EQ(notation.error(), error::container_elements_exceeded);
            }
        }
        CHECK_EQ(test::lazy_of_copy(array_of(n))->elements().has_value(), passes);
        CHECK_EQ(test::lazy_of_copy(map_of(n))->entries().has_value(), passes);
    }
}

// A head that claims 2^64 - 1 bytes or elements over no content is refused at the head, before the bytes are
// read. The build with no size limit reads the same heads and finds too little data.
TEST_CASE("limits sizes 2: a head that claims 2^64 - 1 is refused at the head")
{
    std::string const text = "\x7b\xff\xff\xff\xff\xff\xff\xff\xff"s;
    std::string const array = "\x9b\xff\xff\xff\xff\xff\xff\xff\xff"s;
    CHECK_EQ(cbor::item_size(text).error(), error::string_length_exceeded);
    CHECK_EQ(cbor::item_size(array).error(), error::container_elements_exceeded);
    CHECK_EQ(cbor::diagnostic_notation(text).error(), error::string_length_exceeded);
    CHECK_EQ(cbor::diagnostic_notation(array).error(), error::container_elements_exceeded);
}

// The nesting depth is checked in the form of the code that checks the size limits too: n tags 100(...)
// around 0 hold an integer at depth n.
TEST_CASE("limits sizes 2: the nesting depth is checked beside the size limits")
{
    constexpr std::size_t depth = cbor::limits::nesting_depth;
    std::string const deepest = test::repeat("\xd8\x64"sv, depth) + '\x00';
    std::string const deeper = test::repeat("\xd8\x64"sv, depth + 1) + '\x00';
    CHECK(test::lazy_of_copy(deepest)->clone().has_value());
    CHECK(cbor::diagnostic_notation(deepest).has_value());
    CHECK_EQ(test::lazy_of_copy(deeper)->clone().error(), error::nesting_depth_exceeded);
    CHECK_EQ(cbor::diagnostic_notation(deeper).error(), error::nesting_depth_exceeded);
}

// An iterator of elements() or entries() only skips the items it passes, and the owner decided on 2026-10-10
// that limits count only what is read or decoded. So a string longer than the limit does not stop the walk,
// and the read of that string does.
TEST_CASE("limits sizes 2: the iterators of elements() and entries() skip without counting")
{
    std::string const encoded = "\x82\x63"
                                "abc\x01"s;
    std::string const map = "\xa2\x63"
                            "abc\x01\x02\x03"s;
    auto const l = test::lazy_of_copy(encoded);
    auto const m = test::lazy_of_copy(map);
    REQUIRE(l.has_value());
    REQUIRE(m.has_value());
    auto const elements = l->elements();
    REQUIRE(elements.has_value());
    auto it = elements->begin();
    CHECK_EQ((*it)->get<std::string_view>().error(), error::string_length_exceeded);
    ++it;
    REQUIRE(it != std::default_sentinel);
    CHECK_EQ((*it)->get<int>(), 1);
    auto const entries = m->entries();
    REQUIRE(entries.has_value());
    auto pair = entries->begin();
    REQUIRE((*pair).has_value());
    CHECK_EQ((*pair)->first.get<std::string_view>().error(), error::string_length_exceeded);
    CHECK_EQ((*pair)->second.get<int>(), 1);
    ++pair;
    CHECK_EQ((*pair)->second.get<int>(), 3);
}

// RFC 8949 does not bound what a decoder skips; the bytes bound it (check_pending_items). The owner decided
// on 2026-10-10 and 2026-10-18 that string_length and container_elements count only the heads that a path or
// a lazy reads: the containers on the way, the keys it compares and the target. A string or an array above
// its limit that is only skipped does not count, in each form: the typed read over bytes, the typed read from
// a lazy, at_path, query and the lazy chain. The same item, read, is refused.
TEST_CASE(
    "limits sizes 2: an item that a path or a lazy only skips does not count, and the same item read does")
{
    std::string const strings = "\xa2\x61"
                                "a\x63xyz\x61"
                                "n\x01"s;
    std::string const arrays = "\xa2\x61"
                               "a\x83\x01\x02\x03\x61"
                               "n\x01"s;
    for (auto const &[doc, wanted] : {std::tuple{strings, error::string_length_exceeded},
                                      std::tuple{arrays, error::container_elements_exceeded}}) {
        auto const l = *test::lazy_of_copy(doc);
        CHECK_EQ(cbor::at_path<"$.n", int>(doc), 1);
        CHECK_EQ(l.at_path<"$.n", int>(), 1);
        CHECK_EQ(l.at("n")->get<int>(), 1);
        CHECK(test::at_path("$.n", l) == V(1));
        CHECK(test::query("$.n", l) == A(1));
        CHECK_EQ(test::at_path("$.a", l).error(), wanted);
        CHECK_EQ(test::value_of(*l.at("a")).error(), wanted);
    }
    std::string const two = "\x82\x63xyz\x83\x01\x02\x03"s;
    auto const l = *test::lazy_of_copy("\x82\x82\x63xyz\x83\x01\x02\x03\x05"s);
    CHECK_EQ(l.at_path<"$[1]", int>(), 5);
    CHECK_EQ(l.at(1)->get<int>(), 5);
    CHECK_EQ(l.at_path<"$[0][1]", int>().error(), error::container_elements_exceeded);
    CHECK_EQ(l.at_path<"$[0][0]", std::int64_t>().error(), error::string_length_exceeded);
    CHECK_EQ(cbor::item_size(two).error(), error::string_length_exceeded);
}

// doc/basics.md, section "Limits": the table names the error of each size limit.
TEST_CASE("doc basics: the error of each size limit")
{
    CHECK_EQ(test::lazy_of_copy(preferred(V("xyz"s)))->get<std::string_view>().error(),
             error::string_length_exceeded);
    std::string const three = preferred(A(1, 2, 3));
    CHECK_EQ(test::lazy_of_copy(three)->size().error(), error::container_elements_exceeded);
    CHECK_EQ(cbor::at_path<"$[0]", int>(three).error(), error::container_elements_exceeded);
}

// doc/advanced/basics.md, section "Limits": a limit is checked by the step that reads the item, so a string
// that is too long gives string_length_exceeded at get, not at lazy::copy. A user expected it at lazy::copy.
TEST_CASE("doc advanced basics: a string limit is checked at get")
{
    auto const l = test::lazy_of_copy(preferred(V("xyz"s)));
    REQUIRE(l.has_value());
    CHECK_EQ(l->get<std::string_view>().error(), error::string_length_exceeded);
    CHECK_EQ(test::lazy_of_copy(array_of(3))->size().error(), error::container_elements_exceeded);
}

// doc/advanced/basics.md, section "Limits": the limits count what a call reads, and a skip does not count.
TEST_CASE("doc advanced basics: a skip does not count")
{
    std::string const bytes = preferred(M("a"s, "xyz"s, "n"s, 1));
    CHECK_EQ(test::lazy_of_copy(bytes)->at("n")->get<std::int64_t>(), 1);
    CHECK_EQ(test::lazy_of_copy(bytes)->at("a")->get<std::string_view>().error(),
             error::string_length_exceeded);
    CHECK_EQ(cbor::item_size(bytes).error(), error::string_length_exceeded);
    CHECK_EQ(test::value_of(*test::lazy_of_copy(bytes)).error(), error::string_length_exceeded);
}

// doc/advanced/path.md, section "Limits": a string that a path reads counts, a string that it only skips does
// not, and every container that the walk reads counts.
TEST_CASE("doc advanced path: the size limits")
{
    std::string const bytes = preferred(M("long"s, "xyz"s, "n"s, 1));
    CHECK_EQ(cbor::at_path<"$.n", int>(bytes).error(), error::string_length_exceeded);
    std::string const skipped = preferred(M("a"s, "xyz"s, "n"s, 1));
    CHECK_EQ(cbor::at_path<"$.n", int>(skipped), 1);
    CHECK_EQ(cbor::at_path<"$.a", std::string_view>(std::string(skipped)).error(),
             error::string_length_exceeded);
    std::string const three = preferred(M("a"s, 1, "b"s, 2, "n"s, 3));
    CHECK_EQ(cbor::at_path<"$.n", int>(three).error(), error::container_elements_exceeded);
}

#ifdef __cpp_impl_reflection

namespace
{

struct[[= cbor::tag(1590)]] note {
    std::string text;
    std::vector<std::int32_t> numbers;
};

struct[[= cbor::tag(1591)]] names {
    std::vector<std::string> list;
};

} // namespace

// packed checks the string and container limits on each head it reads. The list of std::int32_t is a typed
// array (RFC 8746), a tag over a byte string of 4 bytes for each element.
TEST_CASE("limits sizes 2: the string and container limits of packed")
{
    CHECK(cbor::packed<note>::decode(*cbor::packed<note>::encode(note{"ab", {}})).has_value());
    CHECK_EQ(cbor::packed<note>::decode(*cbor::packed<note>::encode(note{"abc", {}})).error(),
             error::string_length_exceeded);
    CHECK_EQ(cbor::packed<note>::decode(*cbor::packed<note>::encode(note{"ab", {1}})).error(),
             error::string_length_exceeded);
    CHECK(cbor::packed<names>::decode(*cbor::packed<names>::encode(names{{"a", "b"}})).has_value());
    CHECK_EQ(cbor::packed<names>::decode(*cbor::packed<names>::encode(names{{"a", "b", "c"}})).error(),
             error::container_elements_exceeded);
}

#endif
