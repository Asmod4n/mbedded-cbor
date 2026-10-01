#include "host.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

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
// No size hint reaches the host, so nothing is allocated for the claim.
TEST_CASE("major 4: a huge length claim is too_little_data")
{
    CHECK_EQ(decode_error("\x9b\xff\xff\xff\xff\xff\xff\xff\xff"sv), error::too_little_data);
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

// Ported from test.rb: 'depth: deeply-nested arrays / maps / encoding all raise RuntimeError'.
// The limit is 16 here; the limit itself must pass and one level more must fail.
// The encode depth belongs to the binding, which walks its own values.
TEST_CASE("depth: nested arrays and maps past the limit")
{
    CHECK(decoded(std::string(16, '\x81') + '\x00').has_value());
    CHECK_EQ(decode_error(std::string(17, '\x81') + '\x00'), error::nesting_depth_exceeded);
    CHECK(decoded(repeat("\xa1\x61"
                         "a"sv,
                         16) +
                  '\x00')
              .has_value());
    CHECK_EQ(decode_error(repeat("\xa1\x61"
                                 "a"sv,
                                 17) +
                          '\x00'),
             error::nesting_depth_exceeded);
}

// Ported from test.rb: 'safety: all known malformed-input shapes raise cleanly'.
// assert_safe accepts any error; here every shape names its error. Invalid UTF-8 is in
// test/string.cpp, the zero-length bignum comes with the tags.
TEST_CASE("safety: malformed shapes")
{
    CHECK_EQ(decode_error("\xa2\x61"
                          "a\x01\x61"
                          "b"sv),
             error::too_little_data);
    CHECK_EQ(decode_error("\x9f\x01\x02\xff"sv), error::indefinite_length);
    CHECK_EQ(decode_error("\xbf\x61"
                          "a\x01\xff"sv),
             error::indefinite_length);
    CHECK_EQ(decode_error("\x7f\x61"
                          "a\x61"
                          "b\xff"sv),
             error::indefinite_length);
    for (auto const w : {"\xfc"sv, "\xfd"sv, "\xfe"sv})
        CHECK_EQ(decode_error(w), error::syntax_error);
}

// RFC 8949 Appendix F: major 7 with additional information 24 and a value below 32.
TEST_CASE("major 7: a one-byte simple value below 32 is syntax_error")
{
    CHECK_EQ(decode_error("\xf8\x1f"sv), error::syntax_error);
    check_both("\xf8\x20"sv, V(simple{32}));
}

// Every value of a tag reaches the host with its number; RFC 8949 Appendix A, tag 1.
TEST_CASE("major 6: a tag reaches the host with its content")
{
    tagged t{1, array{V(1363896240)}};
    check_both("\xc1\x1a\x51\x4b\x67\xb0"sv, value{t});
}
