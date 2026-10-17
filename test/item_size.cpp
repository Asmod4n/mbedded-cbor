#include "reference.hpp"
#include "owned_copy.hpp"

#include <cstddef>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Ported from test.rb: 'doc_end: basic offset / empty / truncated / with-offset parameter'. The offset
// of mruby-cbor is a substr of the view here. A top-level item that is not complete yet answers
// too_little_data, as RFC 8949 Appendix F allows an application to wait for more bytes.
TEST_CASE("item_size: the end of the first top-level item")
{
    std::string const first = encoded(V(42));
    std::string const second = encoded(V("hello"s));
    CHECK_EQ(cbor::item_size(first + second).value(), first.size());
    CHECK_EQ(cbor::item_size(""sv).error(), error::too_little_data);
    CHECK_EQ(cbor::item_size("\x1b"sv).error(), error::too_little_data);
    std::string const skip = encoded(V("skip"s));
    std::string const both = skip + encoded(V(99));
    CHECK_EQ(cbor::item_size(std::string_view(both).substr(skip.size())).value(),
             both.size() - skip.size());
}

// A top-level item of every kind ends where its last byte is, and one byte less is not complete.
TEST_CASE("item_size: every kind, complete and one byte short")
{
    for (std::string_view const doc :
         {"\x00"sv, "\x38\x18"sv, "\x43\x01\x02\x03"sv, "\x62hi"sv, "\x82\x01\x82\x02\x03"sv,
          "\xa1\x61k\x01"sv, "\xc1\x1a\x51\x4b\x67\xb0"sv, "\xf9\x3c\x00"sv, "\xf8\x20"sv}) {
        CHECK_EQ(cbor::item_size(doc).value(), doc.size());
        CHECK_EQ(cbor::item_size(doc.substr(0, doc.size() - 1)).error(), error::too_little_data);
    }
}

// The same errors as decode, so a stream stops at the same place.
TEST_CASE("item_size: malformed, and deep without an error")
{
    CHECK_EQ(cbor::item_size("\x9f\x01\xff"sv).error(), error::indefinite_length);
    CHECK_EQ(cbor::item_size("\x1c"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::item_size("\xf8\x1f"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::item_size(std::string(17, '\x81') + '\x00').value(), 18u);
}

// The skip keeps one count of the items still to read and no stack, so item_size reads any nesting depth. Each item
// takes at least one byte, so a count larger than the bytes left is too_little_data at once.
TEST_CASE("item_size: the nesting depth is not bounded, and the count of items is bounded by the bytes")
{
    CHECK_EQ(cbor::item_size(std::string(1025, '\x81') + '\x00').value(), 1026u);
    CHECK_EQ(cbor::item_size(std::string(100000, '\x81') + '\x00').value(), 100001u);
    CHECK_EQ(cbor::item_size(std::string(100000, '\xc1') + '\x00').value(), 100001u);
    CHECK_EQ(cbor::item_size("\x83\x00\x00"sv).error(), error::too_little_data);
    CHECK_EQ(cbor::item_size("\x9b\xff\xff\xff\xff\xff\xff\xff\xff\x00"sv).error(), error::too_little_data);
    CHECK_EQ(cbor::item_size("\xbb\x80\x00\x00\x00\x00\x00\x00\x00\x00"sv).error(), error::too_little_data);
    CHECK_EQ(cbor::item_size("\xbb\x7f\xff\xff\xff\xff\xff\xff\xff\x00"sv).error(), error::too_little_data);
    CHECK_EQ(cbor::item_size("\x82\x00\x00"sv).value(), 3u);
    CHECK_EQ(cbor::item_size("\xa1\x00\x00"sv).value(), 3u);
}

// RFC 8949 3: each data item has at least its initial byte. item_size refuses a count larger than the bytes left with
// too_little_data before it skips an element, by owner decision.
TEST_CASE("item_size: a count larger than the bytes left is too_little_data before the elements")
{
    for (std::string_view const wire : {"\x8c\xff"sv, "\x95\xf8\x04"sv, "\x95\xbf"sv, "\x88\x00\x7f"sv,
                                        "\x87\xd8\x1c\x1f"sv, "\x85\xd8\x1c\xd8\x1c"sv, "\xa2\xff\x00\x00"sv}) {
        CAPTURE(wire);
        CHECK_EQ(cbor::item_size(wire).error(), error::too_little_data);
    }
}

// Found by the fuzzer: 28(24(h'83')) holds an embedded array that claims three items and has none. RFC 8949 3.4.5.1 and
// 5.3.2: the outer item is well-formed, and the embedded data item is a top-level item of its own, by owner decision.
// item_size frames the outer item; decode reads the embedded item and gives too_little_data for it.
TEST_CASE("item_size: a cut item inside tag 24 is framed, and decode refuses it")
{
    std::string_view const wire = "\xd8\x1c\xd8\x18\x41\x83"sv;
    CHECK_EQ(cbor::item_size(wire).value(), wire.size());
    auto const top_level = test::lazy_of_copy(wire);
    REQUIRE(top_level.has_value());
    CHECK_EQ(top_level->decode().error(), error::too_little_data);
    CHECK_EQ(cbor::item_size("\xd8\x1c\xd8\x18\x41"sv).error(), error::too_little_data);
}
