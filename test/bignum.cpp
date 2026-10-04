#include "binding.hpp"

#include <cstdint>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

// The magnitude of 2^(8 * zero_bytes) plus low, most significant byte first.
std::string power_of_256(std::size_t const zero_bytes, std::uint8_t const low = 0)
{
    std::string m(1, '\x01');
    m.append(zero_bytes, '\0');
    m.back() = static_cast<char>(m.back() | low);
    return m;
}

value big(bool const negative, std::string magnitude)
{
    return {bignum{negative, std::move(magnitude)}};
}

void check_round_trip(value const &v)
{
    auto const r = decoded(encoded(v));
    REQUIRE(r.has_value());
    CHECK(*r == v);
}

} // namespace

// Ported from test.rb: 'bignum tag 2: positive bignum boundaries (uint64, +1, deep)'. 2^64 - 1 fits
// major type 0, so it comes back as an integer, not as a bignum.
TEST_CASE("bignum tag 2: positive boundaries")
{
    check_round_trip(big(false, power_of_256(8)));
    check_round_trip(big(false, power_of_256(8, 1)));
    check_round_trip(big(false, power_of_256(25, 0x39)));
    check_round_trip(big(false, power_of_256(4096, 0x15)));
    auto const r = decoded(encoded(big(false, std::string(8, '\xff'))));
    REQUIRE(r.has_value());
    CHECK(*r == value{~std::uint64_t{0}});
}

// Ported from test.rb: 'bignum tag 3: negative bignum boundaries'. -(2^64) is -1 - (2^64 - 1), so it
// fits major type 1.
TEST_CASE("bignum tag 3: negative boundaries")
{
    check_round_trip(big(true, power_of_256(8, 1)));
    check_round_trip(big(true, power_of_256(25, 0x01)));
    auto const r = decoded(encoded(big(true, power_of_256(8))));
    REQUIRE(r.has_value());
    CHECK(*r == value{negative{~std::uint64_t{0}}});
}

// Ported from test.rb: 'bignum: the wire carries the magnitude most significant byte first'.
// The bytes on the wire are checked, not only the round trip, because a reversed order would
// round-trip through the same code.
TEST_CASE("bignum: the wire carries the magnitude most significant byte first")
{
    CHECK_EQ(encoded(big(false, power_of_256(8))), "\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv);
    CHECK_EQ(encoded(big(false, power_of_256(8, 2))), "\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x02"sv);
    CHECK_EQ(encoded(big(true, power_of_256(8, 1))), "\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv);
    CHECK(*decoded("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv) == big(false, power_of_256(8)));
    CHECK(*decoded("\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv) == big(true, power_of_256(8, 1)));
    CHECK(*decoded("\xc2\x4a\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv) == big(false, power_of_256(8)));
    CHECK_EQ(encoded(big(false, std::string(8, '\xff'))), "\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
    CHECK_EQ(encoded(big(true, power_of_256(8))), "\x3b\xff\xff\xff\xff\xff\xff\xff\xff"sv);
}

// Ported from test.rb: 'bignum §3.4.3: zero-length payload — tag(2,h'')=0, tag(3,h'')=-1'.
TEST_CASE("bignum: an empty magnitude is 0 or -1")
{
    CHECK(*decoded("\xc2\x40"sv) == value{std::uint64_t{0}});
    CHECK(*decoded("\xc3\x40"sv) == value{negative{0}});
}

// Ported from test.rb: 'bignum: non-byte-string payload raises TypeError'.
TEST_CASE("bignum: content that is not a byte string")
{
    CHECK_EQ(decode_error("\xc2\x05"sv), error::inadmissible_type_for_tag_content);
    CHECK_EQ(decode_error("\xc3\x05"sv), error::inadmissible_type_for_tag_content);
}

// Ported from test.rb: 'bignum: mixed with normal integers in array'.
TEST_CASE("bignum: among normal integers in an array")
{
    check_round_trip(A(1, 2, V(big(false, power_of_256(12, 0))), 4, 5));
}

// A negative bignum has a magnitude of at least one, because -1 - n needs n from zero. An empty magnitude
// is no value and is refused.
TEST_CASE("bignum tag 3: an empty magnitude is refused")
{
    test_binding binding;
    string_writer w;
    CHECK_EQ(cbor::encode<16>(binding, w, big(true, "")).error(), make_error_code(error::unsupported_value));
    CHECK_EQ(cbor::encode<16>(binding, w, big(true, std::string(2, '\0'))).error(),
             make_error_code(error::unsupported_value));
}

// Found by the fuzzer: the decoder counts the content of tag 2 or 3 one level deeper, as the content of every
// tag. The encoder wrote a bignum at the depth limit without that level, so its own output did not decode.
TEST_CASE("bignum: encode refuses a tagged bignum where decode would refuse it")
{
    value deep = V(value{bignum{false, std::string(9, '\x01')}});
    for (int i = 0; i < 16; ++i)
        deep = A(deep);
    test_binding binding;
    string_writer w;
    auto const r = cbor::encode<16>(binding, w, deep);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::make_error_code(error::nesting_depth_exceeded));
    value shallow = V(value{bignum{false, std::string(9, '\x01')}});
    for (int i = 0; i < 15; ++i)
        shallow = A(shallow);
    string_writer ok;
    REQUIRE(cbor::encode<16>(binding, ok, shallow).has_value());
    CHECK(decoded<16>(ok.bytes).has_value());
}
