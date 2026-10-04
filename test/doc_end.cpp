#include "binding.hpp"

#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Ported from test.rb: 'doc_end: basic offset / empty / truncated / with-offset parameter'. The offset
// of mruby-cbor is a substr of the view here. A document that is not complete yet answers
// too_little_data, as RFC 8949 Appendix F allows an application to wait for more bytes.
TEST_CASE("doc_end: the end of the first document")
{
    std::string const first = encoded(V(42));
    std::string const second = encoded(V("hello"s));
    CHECK_EQ(cbor::doc_end<16>(first + second).value(), first.size());
    CHECK_EQ(cbor::doc_end<16>(""sv).error(), error::too_little_data);
    CHECK_EQ(cbor::doc_end<16>("\x1b"sv).error(), error::too_little_data);
    std::string const skip = encoded(V("skip"s));
    std::string const both = skip + encoded(V(99));
    CHECK_EQ(cbor::doc_end<16>(std::string_view(both).substr(skip.size())).value(),
             both.size() - skip.size());
}

// A document of every kind ends where its last byte is, and one byte less is not complete.
TEST_CASE("doc_end: every kind, complete and one byte short")
{
    for (std::string_view const doc :
         {"\x00"sv, "\x38\x18"sv, "\x43\x01\x02\x03"sv, "\x62hi"sv, "\x82\x01\x82\x02\x03"sv,
          "\xa1\x61k\x01"sv, "\xc1\x1a\x51\x4b\x67\xb0"sv, "\xf9\x3c\x00"sv, "\xf8\x20"sv}) {
        CHECK_EQ(cbor::doc_end<16>(doc).value(), doc.size());
        CHECK_EQ(cbor::doc_end<16>(doc.substr(0, doc.size() - 1)).error(), error::too_little_data);
    }
}

// The same errors as decode, so a stream stops at the same place.
TEST_CASE("doc_end: malformed and too deep")
{
    CHECK_EQ(cbor::doc_end<16>("\x9f\x01\xff"sv).error(), error::indefinite_length);
    CHECK_EQ(cbor::doc_end<16>("\x1c"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::doc_end<16>("\xf8\x1f"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::doc_end<16>(std::string(17, '\x81') + '\x00').error(), error::nesting_depth_exceeded);
}
