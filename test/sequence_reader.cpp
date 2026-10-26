#include "sequence_reader.hpp"
#include "reference.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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

std::string nested_tags(std::size_t const depth)
{
    return std::string(depth, '\xc1') + "\x00"s;
}

template <class Piece>
concept feedable = requires(cbor::sequence_reader r, Piece p) { r.feed(std::forward<Piece>(p)); };

bool view_of(cbor::lazy const &item, std::string const &piece)
{
    std::less_equal<char const *> const not_after;
    return not_after(piece.data(), item.top_level.encoded.data()) &&
           not_after(item.top_level.encoded.data() + item.top_level.encoded.size(), piece.data() + piece.size());
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

// A new user feeds what a socket gives first: a literal through std::string_view, a std::string by name, a
// std::string that is moved in, and a std::shared_ptr. Each form gives the same items.
TEST_CASE("sequence_reader: each form of bytes that a user tries first")
{
    cbor::sequence_reader r;
    r.feed("\x01"sv);
    std::string const named = "\x02"s;
    r.feed(named);
    std::string moved = "\x03"s;
    r.feed(std::move(moved));
    r.feed(std::make_shared<std::string const>("\x04"s));
    std::vector<std::uint64_t> got;
    while (auto item = r.next())
        got.push_back(item->get<std::uint64_t>().value());
    CHECK_EQ(got, std::vector<std::uint64_t>{1, 2, 3, 4});
    CHECK(r.empty());
}

// A literal holds a NUL after its last byte, which a range over the array would feed. A std::shared_ptr to bytes
// that are not const can change under an item that views them. Both do not compile, as for lazy::take_ownership
// and lazy::view. A null std::shared_ptr is a wrong use that the compiler cannot see.
TEST_CASE("sequence_reader: a char array and a shared_ptr to bytes that are not const are refused")
{
    static_assert(!feedable<char const (&)[3]>);
    static_assert(!feedable<char (&)[3]>);
    static_assert(!feedable<std::shared_ptr<std::string>>);
    static_assert(feedable<std::string_view>);
    static_assert(feedable<std::string &>);
    static_assert(feedable<std::string const &>);
    static_assert(feedable<std::string>);
    static_assert(feedable<std::vector<char>>);
    static_assert(feedable<std::shared_ptr<std::string const>>);
    cbor::sequence_reader r;
    CHECK_THROWS_AS(r.feed(std::shared_ptr<std::string const>{}), std::logic_error);
}

// RFC 8742 2: a sequence with no item is valid. A reader that got no byte, or only empty pieces, holds nothing
// and waits.
TEST_CASE("sequence_reader: an empty sequence and empty pieces")
{
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        cbor::sequence_reader r;
        CHECK(r.empty());
        CHECK_EQ(r.next().error(), error::too_little_data);
        test::piece_feed(r, ""sv, form, 0);
        test::piece_feed(r, ""sv, form, 1);
        test::piece_feed(r, ""sv, form, 2);
        CHECK(r.empty());
        CHECK_EQ(r.next().error(), error::too_little_data);
        test::piece_feed(r, "\x82"sv, form, 0);
        test::piece_feed(r, ""sv, form, 1);
        test::piece_feed(r, "\x01\x02"sv, form, 2);
        auto const item = r.next();
        REQUIRE(item.has_value());
        CHECK_EQ(item->top_level.encoded, "\x82\x01\x02"sv);
        CHECK(r.empty());
    }
}

// The bytes of several pieces wait until next() is called. The items come in the order of the bytes, also
// where the forms of the pieces differ.
TEST_CASE("sequence_reader: several pieces before one next()")
{
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        cbor::sequence_reader r;
        test::piece_feed(r, "\x01\x02"sv, form, 0);
        test::piece_feed(r, "\x83\x01"sv, form, 1);
        test::piece_feed(r, "\x02\x03\x04"sv, form, 2);
        test::piece_feed(r, "\x05"sv, form, 3);
        std::vector<std::string> got;
        while (auto item = r.next())
            got.emplace_back(item->top_level.encoded);
        CHECK_EQ(got, std::vector<std::string>{"\x01"s, "\x02"s, "\x83\x01\x02\x03"s, "\x04"s, "\x05"s});
    }
}

// Every split of a sequence into three pieces gives the items of cbor::sequence over the whole buffer, in
// each form of feed. The sequence holds a string, a map, a tag, a float and an array, so that a cut falls into
// a head, an argument, the content of a string and the elements of a container.
TEST_CASE("sequence_reader: every two cuts in every form give the same items")
{
    std::string const all = "\x01\x82\x01\x02\x63"
                            "abc\xa1\x61k\x19\x01\x00\xc1\x1a\x00\x00\x00\x01\xfb\x3f\xf0\x00\x00\x00\x00\x00"
                            "\x00\x80\x84\x80\x41z\xa0\xf6"s;
    test::sequence_outcome const whole = test::sequence_outcome_of(all);
    REQUIRE_EQ(whole.items.size(), 8u);
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        for (std::size_t a = 0; a <= all.size(); ++a)
            for (std::size_t b = a; b <= all.size(); ++b) {
                CAPTURE(a);
                CAPTURE(b);
                std::size_t const cuts[] = {a, b};
                CHECK_EQ(test::reader_outcome_of(all, cuts, form), whole);
            }
        CHECK_EQ(test::reader_outcome_byte_by_byte(all, form), whole);
    }
}

// A held piece gives its items as views into it, with no copy. Only an item cut at the end of a piece is
// copied, and the next item whole in a piece is a view again.
TEST_CASE("sequence_reader: an item whole in a shared piece is a view, a cut item is a copy")
{
    auto const one = std::make_shared<std::string const>("\x01\x82\x01"s);
    auto const two = std::make_shared<std::string const>("\x02\x03\x83\x04"s);
    auto const three = std::make_shared<std::string const>("\x05\x06\x07"s);
    cbor::sequence_reader r;
    r.feed(one);
    r.feed(two);
    r.feed(three);
    auto const a = r.next();
    auto const b = r.next();
    auto const c = r.next();
    auto const d = r.next();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    REQUIRE(d.has_value());
    CHECK(view_of(*a, *one));
    CHECK_EQ(b->top_level.encoded, "\x82\x01\x02"sv);
    CHECK_FALSE(view_of(*b, *one));
    CHECK_FALSE(view_of(*b, *two));
    CHECK(view_of(*c, *two));
    CHECK_EQ(d->top_level.encoded, "\x83\x04\x05\x06"sv);
    CHECK_FALSE(view_of(*d, *three));
    auto const e = r.next();
    REQUIRE(e.has_value());
    CHECK(view_of(*e, *three));
    CHECK(r.empty());
}

// One item can start in bytes that the reader copied and end in a piece that it holds, and it can run over
// more than two held pieces. The reader copies only the bytes of the cut item, so the item after it is a view.
TEST_CASE("sequence_reader: an item over a copied piece and held pieces")
{
    cbor::sequence_reader r;
    r.feed("\x84\x01"sv);
    r.feed(std::make_shared<std::string const>("\x02"s));
    r.feed(std::make_shared<std::string const>("\x03"s));
    auto const last = std::make_shared<std::string const>("\x04\x05"s);
    r.feed(last);
    auto const item = r.next();
    REQUIRE(item.has_value());
    CHECK_EQ(item->top_level.encoded, "\x84\x01\x02\x03\x04"sv);
    auto const after = r.next();
    REQUIRE(after.has_value());
    CHECK(view_of(*after, *last));
    for (std::size_t n : {3u, 4u, 9u}) {
        CAPTURE(n);
        cbor::sequence_reader s;
        s.feed(std::make_shared<std::string const>("\x7a\x00\x00\x00"s + static_cast<char>(n)));
        for (std::size_t i = 0; i < n; ++i)
            s.feed(std::make_shared<std::string const>("x"s));
        auto const string = s.next();
        REQUIRE(string.has_value());
        CHECK_EQ(string->get<std::string_view>().value(), std::string(n, 'x'));
    }
}

// RFC 8949 3.4.6: tag 55799 marks the start of a CBOR item. Each item of a sequence may carry it, and the
// reader strips it as lazy::view does.
TEST_CASE("sequence_reader: a leading tag 55799 on each item")
{
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        CHECK_EQ(test::reader_outcome_byte_by_byte("\xd9\xd9\xf7\x01\xd9\xd9\xf7\x82\x02\x03"sv, form),
                 test::sequence_outcome{{"\x01"s, "\x82\x02\x03"s}, error::too_little_data});
    }
}

// A lazy that the reader gave owns its bytes. Later feeds move the unread bytes into a new buffer or compact
// the buffer; neither may change or free the bytes of an item that is still held. The sanitizer build reads
// freed memory as an error.
TEST_CASE("sequence_reader: an item stays valid after later feeds and after the reader is gone")
{
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        std::vector<cbor::lazy> held;
        {
            cbor::sequence_reader r;
            for (std::size_t i = 0; i < 2000; ++i) {
                test::piece_feed(r, "\x63"sv, form, 3 * i);
                test::piece_feed(r, "ab"sv, form, 3 * i + 1);
                test::piece_feed(r, "c"sv, form, 3 * i + 2);
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
}

// The reader and each item keep a shared piece alive, so the caller may drop its own pointer at once.
TEST_CASE("sequence_reader: an item stays valid after the caller releases the piece")
{
    cbor::sequence_reader r;
    auto piece = std::make_shared<std::string const>("\x63"
                                                     "abc\x01"s);
    r.feed(piece);
    piece.reset();
    auto const item = r.next();
    REQUIRE(item.has_value());
    CHECK_EQ(item->get<std::string_view>().value(), "abc"sv);
    CHECK_EQ(r.next()->get<std::uint64_t>().value(), 1u);
}

// RFC 8949 3: the initial byte alone decides that additional information 28 to 30 is not well-formed, and
// that 31 is an indefinite length (a break for major type 7). Appendix F names these errors. The reader gives
// the error as soon as the byte is there, at every split and in every form, and never too_little_data.
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
        for (test::feed_form const form : test::feed_forms) {
            CAPTURE(static_cast<int>(form));
            for (std::size_t at = 0; at <= m.bytes.size(); ++at) {
                CAPTURE(at);
                CHECK_EQ(test::reader_outcome_split_at(m.bytes, at, form).last, m.expected);
            }
            CHECK_EQ(test::reader_outcome_byte_by_byte(m.bytes, form).last, m.expected);
        }
    }
}

// RFC 8742 2: a decoder cannot find the start of the next item after an item that is not well-formed. After
// the error every later call gives the same error and does nothing else: feed drops the bytes, so the reader
// holds nothing.
TEST_CASE("sequence_reader: after an error every call gives the error and feed keeps no byte")
{
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        cbor::sequence_reader r;
        test::piece_feed(r, "\x01\x1c\x01"sv, form, 0);
        CHECK(r.next().has_value());
        CHECK_EQ(r.next().error(), error::syntax_error);
        CHECK(r.empty());
        test::piece_feed(r, "\x02"sv, form, 1);
        test::piece_feed(r, "\x03"sv, form, 2);
        CHECK(r.empty());
        CHECK_EQ(r.next().error(), error::syntax_error);
        CHECK_EQ(r.next().error(), error::syntax_error);
    }
}

// A count in the input reserves nothing (rule): an array that claims 2^64 - 1 elements, and a string that
// claims 2^64 - 1 bytes, wait for bytes like any other item.
TEST_CASE("sequence_reader: a large count waits for bytes")
{
    for (std::string const &bytes :
         {"\x9b\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00"s, "\xbb\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00"s,
          "\x5b\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00"s}) {
        CAPTURE(bytes);
        for (test::feed_form const form : test::feed_forms) {
            CAPTURE(static_cast<int>(form));
            CHECK_EQ(test::reader_outcome_byte_by_byte(bytes, form),
                     test::sequence_outcome{{}, error::too_little_data});
        }
    }
}

// The reader keeps one count per open container in a stack of fixed size, bounded by the nesting depth of the
// build. An array with an element and a tag each open one level; an item at the depth is read and one level
// more is refused, at every split.
TEST_CASE("sequence_reader: the nesting depth of the build and one level more")
{
    std::size_t const depth = cbor::limits::nesting_depth;
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        CHECK_EQ(test::reader_outcome_byte_by_byte(nested_arrays(depth), form).items.size(), 1u);
        CHECK_EQ(test::reader_outcome_byte_by_byte(nested_arrays(depth + 1), form).last,
                 error::nesting_depth_exceeded);
        CHECK_EQ(test::reader_outcome_byte_by_byte(nested_tags(depth), form).items.size(), 1u);
        CHECK_EQ(test::reader_outcome_byte_by_byte(nested_tags(depth + 1), form).last,
                 error::nesting_depth_exceeded);
    }
    CHECK_EQ(test::reader_outcome_split_at(std::string(depth, '\x81') + "\x80"s, 0).items.size(), 1u);
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
    // An empty array inside a container counts too: after 80 the outer array still waits for two items and
    // one byte follows. Found by the fuzz target.
    std::string const empty_inside = "\x84\x1a\x00\x00\x00\x00\x80\xfe"s;
    CHECK_EQ(test::reader_outcome_split_at(empty_inside, 0), test::sequence_outcome_of(empty_inside));
    CHECK_EQ(test::reader_outcome_split_at(empty_inside, 0).last, error::too_little_data);
}

// empty() says whether the reader holds a byte that no item has taken yet, as std::ranges::empty says whether
// a range has an element. At the end of a stream, a reader that is not empty holds a cut item.
TEST_CASE("sequence_reader: empty() is true when no unread byte is held")
{
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        cbor::sequence_reader r;
        CHECK(r.empty());
        test::piece_feed(r, "\x01\x82"sv, form, 0);
        CHECK_FALSE(r.empty());
        CHECK(r.next().has_value());
        CHECK_EQ(r.next().error(), error::too_little_data);
        CHECK_FALSE(r.empty());
        test::piece_feed(r, "\x01\x02"sv, form, 1);
        CHECK(r.next().has_value());
        CHECK(r.empty());
    }
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
    CHECK_THROWS_AS(r.feed("\x01"s), std::logic_error);
    CHECK_THROWS_AS(r.feed(std::make_shared<std::string const>("\x01"s)), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(r.next()), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(r.empty()), std::logic_error);
}
