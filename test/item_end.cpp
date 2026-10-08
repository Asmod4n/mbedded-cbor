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
TEST_CASE("item_end: the end of the first top-level item")
{
    std::string const first = encoded(V(42));
    std::string const second = encoded(V("hello"s));
    CHECK_EQ(cbor::item_end<16>(first + second).value(), first.size());
    CHECK_EQ(cbor::item_end<16>(""sv).error(), error::too_little_data);
    CHECK_EQ(cbor::item_end<16>("\x1b"sv).error(), error::too_little_data);
    std::string const skip = encoded(V("skip"s));
    std::string const both = skip + encoded(V(99));
    CHECK_EQ(cbor::item_end<16>(std::string_view(both).substr(skip.size())).value(),
             both.size() - skip.size());
}

// A top-level item of every kind ends where its last byte is, and one byte less is not complete.
TEST_CASE("item_end: every kind, complete and one byte short")
{
    for (std::string_view const doc :
         {"\x00"sv, "\x38\x18"sv, "\x43\x01\x02\x03"sv, "\x62hi"sv, "\x82\x01\x82\x02\x03"sv,
          "\xa1\x61k\x01"sv, "\xc1\x1a\x51\x4b\x67\xb0"sv, "\xf9\x3c\x00"sv, "\xf8\x20"sv}) {
        CHECK_EQ(cbor::item_end<16>(doc).value(), doc.size());
        CHECK_EQ(cbor::item_end<16>(doc.substr(0, doc.size() - 1)).error(), error::too_little_data);
    }
}

// The same errors as decode, so a stream stops at the same place.
TEST_CASE("item_end: malformed and too deep")
{
    CHECK_EQ(cbor::item_end<16>("\x9f\x01\xff"sv).error(), error::indefinite_length);
    CHECK_EQ(cbor::item_end<16>("\x1c"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::item_end<16>("\xf8\x1f"sv).error(), error::syntax_error);
    CHECK_EQ(cbor::item_end<16>(std::string(17, '\x81') + '\x00').error(), error::nesting_depth_exceeded);
}

namespace
{

template <std::size_t DepthMax>
concept item_end_compiles = requires(std::string_view const s) { cbor::item_end<DepthMax>(s); };

} // namespace

// DepthMax sizes the stack array of the skip, so a large DepthMax would overflow the stack. The bound is 1024: an
// item nested 1024 deep is read with DepthMax 1024, and DepthMax 1025 does not compile.
TEST_CASE("item_end: DepthMax is at most 1024")
{
    CHECK(item_end_compiles<1024>);
    CHECK_FALSE(item_end_compiles<1025>);
    CHECK_EQ(cbor::item_end<1024>(std::string(1024, '\x81') + '\x00').value(), 1025u);
    CHECK_EQ(cbor::item_end<1024>(std::string(1025, '\x81') + '\x00').error(), error::nesting_depth_exceeded);
}
