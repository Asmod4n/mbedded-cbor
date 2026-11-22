#include "rfc8949.hpp"
#include "small_stack.hpp"
#include "reference.hpp"
#include "owned_copy.hpp"

#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Ported from test.rb: 'major 4: empty, basic, nested, mixed roundtrip'.
// The wire bytes are the preferred serialization of RFC 8949 4.1.
TEST_CASE("major 4: empty, basic, nested, mixed")
{
    check_both("\x80"sv, A());
    check_both("\x83\x01\x02\x03"sv, A(1, 2, 3));
    check_both("\x82\x82\x01\x02\x82\x03\x82\x04\x05"sv, A(A(1, 2), A(3, A(4, 5))));
    check_both("\x85\x01\x63two\xf5\xf6\xf9\x44\x00"sv,
               A(1, "two"s, simple{std::to_underlying(cbor::simple_value::true_value)},
                 simple{std::to_underlying(cbor::simple_value::null)}, 4.0));
}

// Ported from test.rb: 'major 4: huge length claim (uint64) raises RangeError'.
// The decode keeps room only for the items it has read, so the claim reserves nothing.
TEST_CASE("major 4: a huge length claim is too_little_data")
{
    CHECK_EQ(read_decode_error_of_wire("\x9b\xff\xff\xff\xff\xff\xff\xff\xff"sv), error::too_little_data);
}

// Ported from test.rb: 'major 5: empty, string, integer, nested keys roundtrip'.
TEST_CASE("major 5: empty, string, integer, nested keys")
{
    check_both("\xa0"sv, M());
    check_both("\xa2\x61"
               "a\x01\x61"
               "b\x02"sv,
               M("a"s, 1, "b"s, 2));
    check_both("\xa2\x01\x63one\x02\x63two"sv, M(1, "one"s, 2, "two"s));
    check_both("\xa1\x65outer\xa1\x65inner\x18\x2a"sv, M("outer"s, M("inner"s, 42)));
}

// Ported from test.rb: 'depth: deeply-nested arrays / maps / encoding all raise RuntimeError'. The end of the stack of
// the thread bounds the depth: a depth that every stack holds passes. An input deeper than a small stack holds fails on
// a thread with that stack.
TEST_CASE("depth: nested arrays and maps past the end of the stack")
{
    constexpr std::size_t held = test::levels_every_stack_holds;
    constexpr std::size_t beyond = test::levels_beyond_small_stack;
    CHECK(decode_wire_into_value(std::string(held, '\x81') + '\x00').has_value());
    std::string const arrays = std::string(beyond, '\x81') + '\x00';
    test::run_on_small_stack([&arrays] { CHECK_EQ(read_decode_error_of_wire(arrays), error::nesting_depth_exceeded); });
    CHECK(decode_wire_into_value(repeat("\xa1\x61"
                         "a"sv,
                         held) +
                  '\x00')
              .has_value());
    std::string const maps = repeat("\xa1\x61"
                                    "a"sv,
                                    beyond) +
                             '\x00';
    test::run_on_small_stack([&maps] { CHECK_EQ(read_decode_error_of_wire(maps), error::nesting_depth_exceeded); });
}

// Ported from test.rb: 'safety: all known malformed-input shapes raise cleanly'.
// assert_safe accepts any error; here every shape names its error. The zero-length
// bignum comes with the tags.
TEST_CASE("safety: malformed shapes")
{
    CHECK_EQ(read_decode_error_of_wire("\xa2\x61"
                          "a\x01\x61"
                          "b"sv),
             error::too_little_data);
    CHECK_EQ(read_decode_error_of_wire("\x9f\x01\x02\xff"sv), error::indefinite_length);
    CHECK_EQ(read_decode_error_of_wire("\xbf\x61"
                          "a\x01\xff"sv),
             error::indefinite_length);
    CHECK_EQ(read_decode_error_of_wire("\x7f\x61"
                          "a\x61"
                          "b\xff"sv),
             error::indefinite_length);
    for (auto const w : {"\xfc"sv, "\xfd"sv, "\xfe"sv})
        CHECK_EQ(read_decode_error_of_wire(w), error::syntax_error);
}

// RFC 8949 Appendix F: major 7 with additional information 24 and a value below 32.
TEST_CASE("major 7: a one-byte simple value below 32 is syntax_error")
{
    CHECK_EQ(read_decode_error_of_wire("\xf8\x1f"sv), error::syntax_error);
    check_both("\xf8\x20"sv, V(simple{32}));
}

// Every value of a tag keeps its number in the reference model; RFC 8949 Appendix A, tag 1.
TEST_CASE("major 6: a tag keeps its number and its content")
{
    tagged t{1, V(1363896240)};
    check_both("\xc1\x1a\x51\x4b\x67\xb0"sv, value{t});
}

// Ported from test.rb: 'depth: deeply-nested arrays / maps / encoding all raise RuntimeError', the encode part. encode
// of a tree recurses as decode does. So a deep tree that decode gives encodes.
TEST_CASE("depth: encode of a deep tree")
{
    value deep = V(0);
    for (std::size_t i = 0; i < test::levels_every_stack_holds; ++i)
        deep = A(deep);
    auto const fits = test::make_lazy_from_copy(encode_in_preferred_form(deep))->to_item();
    REQUIRE(fits.has_value());
    std::string w;
    CHECK(cbor::encode(w, **fits).has_value());
    CHECK_EQ(w, encode_in_preferred_form(deep));
}

// A span target is written in place. The last items of a top-level item lie closer to the end than the widest head. So
// each kind is checked as the last item of a span of exact size and of one byte less. It is checked with the shortest
// heads and float widths and with every number at eight bytes. cbor::encode keeps the width of each head, so the bytes
// are those of the wire.
TEST_CASE("encode: a span of exact size holds the top-level item, one byte less is no_buffer_space")
{
    for (std::string_view const wire :
         {"\x82\x01\x18\x18"sv, "\x82\x01\xf9\x3e\x00"sv, "\x82\x01\xfa\x47\xc3\x50\x20"sv,
          "\x82\x01\xfb\x3f\xb9\x99\x99\x99\x99\x99\x9a"sv, "\x82\x01\x63" "abc"sv, "\x82\x01\xf6"sv,
          "\x82\x01\x1a\xff\xff\xff\xff"sv,
          "\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x1b\x00\x00\x00\x00\x00\x00\x00\x18"sv,
          "\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\xfb\x3f\xf8\x00\x00\x00\x00\x00\x00"sv,
          "\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\xfb\x40\xf8\x6a\x04\x00\x00\x00\x00"sv,
          "\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\xfb\x3f\xb9\x99\x99\x99\x99\x99\x9a"sv,
          "\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x63" "abc"sv,
          "\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\xf6"sv,
          "\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x1b\x00\x00\x00\x01\x00\x00\x00\x00"sv}) {
        CAPTURE(wire);
        auto const tree = cbor::decode(wire);
        REQUIRE(tree.has_value());
        std::vector<char> exact(wire.size());
        CHECK(cbor::encode(std::span<char>(exact), **tree).has_value());
        CHECK_EQ(std::string_view(exact.data(), exact.size()), wire);
        std::vector<char> short_by_one(wire.size() - 1);
        CHECK_EQ(cbor::encode(std::span<char>(short_by_one), **tree), std::unexpected(cbor::error{cbor::error::no_buffer_space}));
    }
}

