#include "binding.hpp"

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
