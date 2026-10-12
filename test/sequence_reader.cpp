#include "sequence_reader.hpp"
#include "reference.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

std::string nested_arrays(std::size_t const depth)
{
    return std::string(depth, '\x81') + "\x00"s;
}

} // namespace

// RFC 8742 2: a CBOR Sequence is the concatenation of encoded data items. A reader hands out each item as
// soon as its last byte has arrived, and too_little_data while it waits.
TEST_CASE("sequence_reader: hands out each item when its last byte arrives")
{
    cbor::sequence_reader r;
    CHECK_EQ(r.next().error(), error::too_little_data);
    r.feed("\x01\x82\x01"sv);
    auto const first = r.next();
    REQUIRE(first.has_value());
    CHECK_EQ(first->get<std::uint64_t>().value(), 1u);
    CHECK_EQ(r.next().error(), error::too_little_data);
    r.feed("\x02\x63"sv);
    auto const second = r.next();
    REQUIRE(second.has_value());
    CHECK_EQ(second->size().value(), 2u);
    CHECK_EQ(r.next().error(), error::too_little_data);
    r.feed("abc"sv);
    auto const third = r.next();
    REQUIRE(third.has_value());
    CHECK_EQ(third->get<std::string_view>().value(), "abc"sv);
    CHECK_EQ(r.next().error(), error::too_little_data);
}

// A lazy that the reader gave owns its bytes. Later feeds move the unread bytes into a new buffer or compact
// the buffer; neither may change or free the bytes of an item that is still held. The sanitizer build reads
// freed memory as an error.
TEST_CASE("sequence_reader: an item stays valid after later feeds and after the reader is gone")
{
    std::vector<cbor::lazy> held;
    {
        cbor::sequence_reader r;
        for (std::size_t i = 0; i < 2000; ++i) {
            r.feed("\x63"sv);
            r.feed("ab"sv);
            r.feed("c"sv);
            auto item = r.next();
            REQUIRE(item.has_value());
            held.push_back(std::move(*item));
            if (i % 2 == 0)
                held.pop_back();
        }
    }
    REQUIRE_EQ(held.size(), 1000u);
    for (cbor::lazy const &l : held)
        CHECK_EQ(l.get<std::string_view>().value(), "abc"sv);
}

// RFC 8949 3: the initial byte alone decides that additional information 28 to 30 is not well-formed, and
// that 31 is an indefinite length (a break for major type 7). Appendix F names these errors. The reader gives
// the error as soon as the byte is there, at every split, and never too_little_data.
TEST_CASE("sequence_reader: a malformed item gives its error at every split")
{
    struct malformed {
        std::string bytes;
        error expected;
    };
    std::vector<malformed> const set = {
        {"\x1c"s, error::syntax_error},
        {"\x3d"s, error::syntax_error},
        {"\xfe"s, error::syntax_error},
        {"\xff"s, error::syntax_error},
        {"\x1f"s, error::syntax_error},
        {"\x5f\x41\x00\xff"s, error::indefinite_length},
        {"\x9f\x01\xff"s, error::indefinite_length},
        {"\xbf\xff"s, error::indefinite_length},
        {"\x83\x01\xff\x02"s, error::syntax_error},
        {"\xa1\x01\x1c\x00\x00"s, error::syntax_error},
        {"\xc1\x5f\x41\x00\xff"s, error::indefinite_length},
        {"\xf8\x1f"s, error::syntax_error},
        {"\x82\xf8\x00\x01"s, error::syntax_error},
        {"\x01\x9b\x00\x00\x00\x00\x00\x00\x00\x02\x01\xfd"s, error::syntax_error},
    };
    for (malformed const &m : set) {
        CAPTURE(m.bytes);
        for (std::size_t at = 0; at <= m.bytes.size(); ++at) {
            CAPTURE(at);
            CHECK_EQ(test::reader_outcome_split_at(m.bytes, at).last, m.expected);
        }
        CHECK_EQ(test::reader_outcome_byte_by_byte(m.bytes).last, m.expected);
    }
}

// RFC 8742 2: a decoder cannot find the start of the next item after an item that is not well-formed. The
// error stays, and the reader hands out nothing more.
TEST_CASE("sequence_reader: an error other than too_little_data stays")
{
    cbor::sequence_reader r;
    r.feed("\x01\x1c\x01"sv);
    CHECK(r.next().has_value());
    CHECK_EQ(r.next().error(), error::syntax_error);
    r.feed("\x02"sv);
    CHECK_EQ(r.next().error(), error::syntax_error);
}

// A count in the input reserves nothing (rule): an array that claims 2^64 - 1 elements, and a string that
// claims 2^64 - 1 bytes, wait for bytes like any other item.
TEST_CASE("sequence_reader: a large count waits for bytes")
{
    for (std::string const &bytes :
         {"\x9b\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00"s, "\xbb\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00"s,
          "\x5b\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00"s}) {
        CAPTURE(bytes);
        CHECK_EQ(test::reader_outcome_byte_by_byte(bytes),
                 test::sequence_outcome{{}, error::too_little_data});
    }
}

// The limits hold in a reader as in every other read, and a limit that the head of an item exceeds gives its
// error before the bytes that the head announces arrive.
TEST_CASE("sequence_reader: the limits hold before the announced bytes arrive")
{
    SUBCASE("string_length")
    {
        test::limits_guard const g{{.string_length = 3}};
        CHECK_EQ(test::reader_outcome_byte_by_byte("\x63"
                                                   "abc"s)
                     .items.size(),
                 1u);
        CHECK_EQ(test::reader_outcome_byte_by_byte("\x44"s).last, error::string_length_exceeded);
    }
    SUBCASE("container_elements")
    {
        test::limits_guard const g{{.container_elements = 2}};
        CHECK_EQ(test::reader_outcome_byte_by_byte("\x82\x01\x02"s).items.size(), 1u);
        CHECK_EQ(test::reader_outcome_byte_by_byte("\x83"s).last, error::container_elements_exceeded);
        CHECK_EQ(test::reader_outcome_byte_by_byte("\xa3"s).last, error::container_elements_exceeded);
    }
    SUBCASE("input_bytes")
    {
        test::limits_guard const g{{.input_bytes = 4}};
        CHECK_EQ(test::reader_outcome_byte_by_byte("\x63"
                                                   "abc\x63"
                                                   "abc"s)
                     .items.size(),
                 2u);
        CHECK_EQ(test::reader_outcome_byte_by_byte("\x64"s).last, error::input_bytes_exceeded);
        CHECK_EQ(test::reader_outcome_byte_by_byte("\x84\x01\x02\x03\x04"s).last,
                 error::input_bytes_exceeded);
    }
    SUBCASE("nesting_depth")
    {
        test::limits_guard const g{{.nesting_depth = 3}};
        CHECK_EQ(test::reader_outcome_byte_by_byte(nested_arrays(3)).items.size(), 1u);
        CHECK_EQ(test::reader_outcome_byte_by_byte(nested_arrays(4)).last, error::nesting_depth_exceeded);
        CHECK_EQ(test::reader_outcome_byte_by_byte("\xc1\xc1\xc1\x00"s).items.size(), 1u);
        CHECK_EQ(test::reader_outcome_byte_by_byte("\xc1\xc1\xc1\xc1\x00"s).last,
                 error::nesting_depth_exceeded);
    }
}

// The limits are read when an item starts and hold for the whole item, so an item that is cut is read to its
// end under the limits that it started with. The next item reads the limits again.
TEST_CASE("sequence_reader: the limits of an item are those at its start")
{
    cbor::sequence_reader r;
    r.feed("\x82\x01"sv);
    CHECK_EQ(r.next().error(), error::too_little_data);
    test::limits_guard const g{{.container_elements = 1}};
    r.feed("\x02\x82\x01\x02"sv);
    CHECK(r.next().has_value());
    CHECK_EQ(r.next().error(), error::container_elements_exceeded);
}

// The reader keeps one count per open container in a stack of fixed size. The hard bound of the nesting depth
// is the size of that stack, so an item at the bound is read and one level more is refused.
TEST_CASE("sequence_reader: the stack holds the hard bound of the nesting depth")
{
    test::limits_guard const g{{.nesting_depth = cbor::validity::nesting_depth_limit}};
    auto const at_bound =
        test::reader_outcome_split_at(nested_arrays(cbor::validity::nesting_depth_limit), 0);
    CHECK_EQ(at_bound.items.size(), 1u);
    CHECK_EQ(test::reader_outcome_split_at(nested_arrays(cbor::validity::nesting_depth_limit + 1), 0).last,
             error::nesting_depth_exceeded);
}

// RFC 8949 3 and the decision of the owner of 2026-10-12: an array or a map that announces more items than
// bytes follow gives too_little_data before any of its elements is read, in every reader. Each item takes one
// byte at least, so such a container cannot be complete. With the bytes that complete the count, the element
// is read and gives its own error.
TEST_CASE("sequence_reader: a count larger than the bytes left waits before its elements")
{
    for (std::size_t at = 0; at <= 2; ++at) {
        CAPTURE(at);
        CHECK_EQ(test::reader_outcome_split_at("\x82\xff"s, at).last, error::too_little_data);
        CHECK_EQ(test::reader_outcome_split_at("\x82\xff"s, at), test::sequence_outcome_of("\x82\xff"sv));
    }
    for (std::size_t at = 0; at <= 3; ++at) {
        CAPTURE(at);
        CHECK_EQ(test::reader_outcome_split_at("\x82\xff\x00"s, at).last, error::syntax_error);
    }
}

// empty() says whether the reader holds a byte that no item has taken yet, as std::ranges::empty says whether
// a range has an element. At the end of a stream, a reader that is not empty holds a cut item.
TEST_CASE("sequence_reader: empty() is true when no unread byte is held")
{
    cbor::sequence_reader r;
    CHECK(r.empty());
    r.feed("\x01\x82"sv);
    CHECK_FALSE(r.empty());
    CHECK(r.next().has_value());
    CHECK_EQ(r.next().error(), error::too_little_data);
    CHECK_FALSE(r.empty());
    r.feed("\x01\x02"sv);
    CHECK(r.next().has_value());
    CHECK(r.empty());
}

// A reader holds bytes that other items view, so a copy is refused at compile time. A reader that was moved
// from holds no buffer; a call on it is a wrong use and throws.
TEST_CASE("sequence_reader: copy does not compile, a moved-from reader throws")
{
    static_assert(!std::is_copy_constructible_v<cbor::sequence_reader>);
    static_assert(!std::is_copy_assignable_v<cbor::sequence_reader>);
    cbor::sequence_reader r;
    r.feed("\x01"sv);
    cbor::sequence_reader moved = std::move(r);
    CHECK(moved.next().has_value());
    CHECK_THROWS_AS(r.feed("\x01"sv), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(r.next()), std::logic_error);
}
