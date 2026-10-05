#include "binding.hpp"

#include <initializer_list>
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
// The size that reaches the binding is capped by the remaining bytes, so the claim reserves nothing.
TEST_CASE("major 4: a huge length claim is too_little_data")
{
    CHECK_EQ(decode_error("\x9b\xff\xff\xff\xff\xff\xff\xff\xff"sv), error::too_little_data);
}

namespace
{

struct size_binding : test_binding {
    std::vector<std::uint64_t> sizes;

    value array_decode(std::uint64_t const size)
    {
        sizes.push_back(size);
        return {test::array{}};
    }

    value map_decode(std::uint64_t const size)
    {
        sizes.push_back(size);
        return {test::map{}};
    }
};

std::vector<std::uint64_t> sizes_of(std::string_view const bytes)
{
    size_binding binding;
    (void)cbor::decode<16>(binding, bytes);
    return binding.sizes;
}

struct key_binding : test_binding {
    std::vector<std::string> keys;

    value map_key_decode(std::string_view const key)
    {
        keys.emplace_back(key);
        return {std::string(key)};
    }
};

} // namespace

// A binding reserves with the size. A head that declares more items than the remaining bytes can
// hold must not make it reserve more: each item takes at least one byte, each pair at least two.
TEST_CASE("major 4 and 5: the size a binding receives")
{
    CHECK(sizes_of("\x83\x01\x02\x03"sv) == std::vector<std::uint64_t>{3});
    CHECK(sizes_of("\xa2\x01\x02\x03\x04"sv) == std::vector<std::uint64_t>{2});
    CHECK(sizes_of("\x82\x80\xa0"sv) == std::vector<std::uint64_t>{2, 0, 0});
    CHECK(sizes_of("\x9b\xff\xff\xff\xff\xff\xff\xff\xff\x01\x02"sv) == std::vector<std::uint64_t>{2});
    CHECK(sizes_of("\xbb\xff\xff\xff\xff\xff\xff\xff\xff\x01\x02\x03"sv) == std::vector<std::uint64_t>{1});
    CHECK(sizes_of("\x99\x01\x00"sv) == std::vector<std::uint64_t>{0});
}

// A binding with map_key_decode makes one value per key (RFC 8949 3.1, major type 5). Only a
// text string in key position reaches it; a text string as a map value and a key of another type
// take the usual functions, and the decoded value does not change.
TEST_CASE("major 5: a text key goes to map_key_decode")
{
    std::string_view const wire = "\xa3\x61"
                                  "a\x01\x02\x61"
                                  "b\x61"
                                  "c\xa1\x61"
                                  "a\x03"sv;
    key_binding binding;
    auto const keyed = cbor::decode<16>(binding, wire);
    REQUIRE(keyed.has_value());
    CHECK(binding.keys == std::vector<std::string>{"a", "c", "a"});
    test_binding plain;
    auto const usual = cbor::decode<16>(plain, wire);
    REQUIRE(usual.has_value());
    CHECK(*keyed == *usual);
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

// Every value of a tag reaches the binding with its number; RFC 8949 Appendix A, tag 1.
TEST_CASE("major 6: a tag reaches the binding with its content")
{
    tagged t{1, V(1363896240)};
    check_both("\xc1\x1a\x51\x4b\x67\xb0"sv, value{t});
}

// Ported from test.rb: 'depth: deeply-nested arrays / maps / encoding all raise RuntimeError',
// the encode part. The core counts the depth on encode as on decode.
TEST_CASE("depth: encode past the limit")
{
    value deep = V(0);
    for (int i = 0; i < 16; ++i)
        deep = A(deep);
    test_binding binding;
    string_writer w;
    CHECK(cbor::encode<16>(binding, w, deep).has_value());
    string_writer w2;
    auto const r = cbor::encode<16>(binding, w2, A(deep));
    REQUIRE_FALSE(r.has_value());
    CHECK((r.error() == error::nesting_depth_exceeded));
}

// RFC 8949 Table 4: the simple values 24 to 31 are reserved; Appendix F: f8 with a value below 32 is
// not well-formed, so no encoder may write one.
TEST_CASE("encode: a reserved simple value is an error")
{
    for (std::uint8_t v = 24; v < 32; ++v) {
        test_binding binding;
        string_writer w;
        auto const r = cbor::encode<16>(binding, w, V(simple{v}));
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error() == error::reserved_simple_value));
    }
    CHECK_EQ(encoded(V(simple{23})), "\xf7"sv);
    CHECK_EQ(encoded(V(simple{32})), "\xf8\x20"sv);
}

namespace
{

// A language with text and nothing else, as bash or zsh: it answers only kind_of and text_of.
struct text_binding : cbor::binding<std::string> {
    cbor::kind kind_of(std::string const &v)
    {
        return v == "array" ? cbor::kind::array : cbor::kind::text_string;
    }

    std::string_view text_of(std::string const &v)
    {
        return v;
    }
};

} // namespace

// A binding answers only the questions its language has; a kind it cannot answer is an error, not a
// failure to compile.
TEST_CASE("encode: a kind the binding cannot describe is unsupported_value")
{
    text_binding binding;
    string_writer w;
    CHECK(cbor::encode<16>(binding, w, std::string("a")).has_value());
    CHECK_EQ(w.encoded, "\x61"
                      "a"sv);
    string_writer w2;
    auto const r = cbor::encode<16>(binding, w2, std::string("array"));
    REQUIRE_FALSE(r.has_value());
    CHECK((r.error() == error::unsupported_value));
}

// A span target is written in place. The last items of a document lie closer to the end than the
// widest head, so each kind is checked as the last item of a span of exact size and of one byte less.
TEST_CASE("encode: a span of exact size holds the document, one byte less is no_buffer_space")
{
    test_binding binding;
    for (value const &v : {A(1, 24), A(1, 1.5), A(1, 100000.25), A(1, 0.1), A(1, "abc"s),
                           A(1, simple{std::to_underlying(cbor::simple_value::null)}), A(1, value{std::uint64_t{4294967296}})}) {
        std::string const expected = encoded(v);
        std::vector<char> exact(expected.size());
        CHECK(cbor::encode<16>(binding, std::span<char>(exact), v).has_value());
        CHECK_EQ(std::string_view(exact.data(), exact.size()), expected);
        std::vector<char> short_by_one(expected.size() - 1);
        CHECK_EQ(cbor::encode<16>(binding, std::span<char>(short_by_one), v).error(),
                 std::make_error_code(std::errc::no_buffer_space));
    }
}
