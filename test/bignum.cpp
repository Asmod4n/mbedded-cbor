#include "reference.hpp"
#include "item_head.hpp"

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
    string_writer w;
    CHECK_EQ(test::encode(w, big(true, "")).error(), cbor::error{error::unsupported_value});
    string_writer w2;
    CHECK_EQ(test::encode(w2, big(true, std::string(2, '\0'))).error(),
             cbor::error{error::unsupported_value});
}

// Found by the fuzzer: the decoder counts the content of tag 2 or 3 one level deeper, as the content of every
// tag. encode of a tree counts the same levels, so a tree that decode refuses at a depth is refused by encode at
// that depth too.
TEST_CASE("bignum: encode refuses a tagged bignum where decode would refuse it")
{
    std::string const bignum = "\xc2\x49"s + std::string(9, '\x01');
    std::string const deep = std::string(16, '\x81') + bignum;
    std::string const shallow = std::string(15, '\x81') + bignum;
    auto const tree = cbor::lazy::copy(deep)->decode();
    REQUIRE(tree.has_value());
    test::limits_guard const depth{{.nesting_depth = 16}};
    CHECK_EQ(cbor::lazy::copy(deep)->decode().error(), error::nesting_depth_exceeded);
    std::string out;
    CHECK_EQ(cbor::encode(**tree, out).error(), error::nesting_depth_exceeded);
    auto const ok = cbor::lazy::copy(shallow)->decode();
    REQUIRE(ok.has_value());
    std::string again;
    REQUIRE(cbor::encode(**ok, again).has_value());
    CHECK_EQ(again, shallow);
}

// RFC 8949 3.4.3: a bignum is equivalent to the integer of the same numeric value, and leading zeros are allowed.
// The tree holds values (RFC 8949 2), so a tag 2 or 3 whose magnitude fits in 64 bits is that integer, and lazy
// type() says the same. The rows cover the empty magnitude (0 and -1), leading zeros past eight bytes, both ends of
// 64 bits and the first magnitude that does not fit.
TEST_CASE("bignum: a magnitude that fits 64 bits is an integer in decode and in type()")
{
    struct row {
        std::string wire;
        cbor::type type;
        std::uint64_t argument;
    };
    std::string const max = std::string(8, '\xff');
    std::string const two_64 = "\x01"s + std::string(8, '\0');
    row const rows[] = {
        {"\xc2\x40"s, cbor::type::unsigned_integer, 0},
        {"\xc3\x40"s, cbor::type::negative_integer, 0},
        {"\xc2\x4a"s + std::string(9, '\0') + "\x01"s, cbor::type::unsigned_integer, 1},
        {"\xc3\x4c"s + std::string(11, '\0') + "\x07"s, cbor::type::negative_integer, 7},
        {"\xc2\x48"s + max, cbor::type::unsigned_integer, ~std::uint64_t{0}},
        {"\xc3\x48"s + max, cbor::type::negative_integer, ~std::uint64_t{0}},
        {"\xc2\x49"s + two_64, cbor::type::unsigned_bignum, 2},
        {"\xc3\x49"s + two_64, cbor::type::negative_bignum, 3},
    };
    for (row const &r : rows) {
        CAPTURE(r.wire);
        auto const l = cbor::lazy::copy(r.wire);
        REQUIRE(l.has_value());
        auto const t = l->type();
        REQUIRE(t.has_value());
        CHECK_EQ(*t, r.type);
        auto const tree = l->decode();
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type, r.type);
        CHECK_EQ(argument_of(**tree), r.argument);
    }
    CHECK_EQ(cbor::lazy::copy("\xc3\x48"s + max)->get<std::int64_t>().error(), error::number_out_of_range);
    CHECK_EQ(*cbor::lazy::copy("\xc2\x40"s)->get<std::int64_t>(), 0);
    CHECK_EQ(*cbor::lazy::copy("\xc2\x41\x05"s)->get<double>(), 5.0);
    CHECK_EQ(*cbor::lazy::copy("\xc3\x41\x05"s)->get<double>(), -6.0);
    CHECK_EQ(cbor::lazy::copy("\xc2\x41\x05"s)->get<std::span<std::byte const>>().error(), error::incorrect_type);
    CHECK_EQ(*cbor::lazy::copy("\xc3\x40"s)->get<std::int64_t>(), -1);
}

// RFC 8949 3.4.3 admits only a byte string as the content of tag 2 or 3. The fold to an integer reads the
// content first, so a wrong content keeps its error in decode and in type(), and an indefinite length stays
// refused. A content that ends early gives too_little_data and reads no byte past the input.
TEST_CASE("bignum: a wrong content keeps its error in decode and in type()")
{
    struct row {
        std::string wire;
        error expected;
    };
    row const rows[] = {
        {"\xc2\x61\x61"s, error::inadmissible_type_for_tag_content},
        {"\xc2\x80"s, error::inadmissible_type_for_tag_content},
        {"\xc2\x01"s, error::inadmissible_type_for_tag_content},
        {"\xc3\x61\x61"s, error::inadmissible_type_for_tag_content},
        {"\xc2\x5f\x41\x01\xff"s, error::indefinite_length},
        {"\xc2\x5f"s, error::indefinite_length},
        {"\xc2"s, error::too_little_data},
        {"\xc2\x41"s, error::too_little_data},
        {"\xc2\x49\x01"s, error::too_little_data},
    };
    for (row const &r : rows) {
        CAPTURE(r.wire);
        auto const l = cbor::lazy::copy(r.wire);
        REQUIRE(l.has_value());
        auto const t = l->type();
        REQUIRE_FALSE(t.has_value());
        CHECK_EQ(t.error(), r.expected);
        auto const tree = l->decode();
        REQUIRE_FALSE(tree.has_value());
        CHECK_EQ(tree.error(), r.expected);
    }
}

// RFC 8949 3.4.3: the value of a bignum that fits is the integer. A tag 28 around it marks that integer, so the
// item that tag 29 names is the integer item, and lazy reads the integer through tag 29.
TEST_CASE("bignum: tag 28 around a bignum that fits marks the integer")
{
    std::string const wire = "\x82\xd8\x1c\xc2\x41\x01\xd8\x1d\x00"s;
    auto const l = cbor::lazy::copy(wire);
    REQUIRE(l.has_value());
    auto const tree = l->decode();
    REQUIRE(tree.has_value());
    auto const elements = children_of(**tree);
    REQUIRE_EQ(elements.size(), 2u);
    REQUIRE_EQ(elements[0]->type, cbor::type::tag);
    cbor::item const *const marked = elements[0]->via.tag.content;
    CHECK_EQ(marked->type, cbor::type::unsigned_integer);
    CHECK_EQ(marked->via.u64, 1u);
    CHECK_EQ(&cbor::shared_value(*tree, *elements[1]), marked);
    CHECK_EQ(*l->at(1)->type(), cbor::type::unsigned_integer);
    CHECK_EQ(*l->at(1)->get<std::int64_t>(), 1);
    std::string again;
    REQUIRE(cbor::encode(**tree, again).has_value());
    CHECK_EQ(again, "\x82\xd8\x1c\x01\xd8\x1d\x00"s);
}

// RFC 8949 5.6.1 compares keys in the data model, and RFC 8949 3.4.3 makes 2(h'01') the integer 1. So a key
// written as a bignum that fits is the same key as the integer: keys_equivalent says so, a lookup by an integer
// finds it, and a map with both holds the key twice.
TEST_CASE("bignum: a key that fits 64 bits is the same key as the integer")
{
    struct pair {
        std::string first;
        std::string second;
        bool equal;
    };
    pair const pairs[] = {
        {"\x01"s, "\xc2\x41\x01"s, true},
        {"\xc2\x41\x01"s, "\x01"s, true},
        {"\x20"s, "\xc3\x40"s, true},
        {"\x20"s, "\xc3\x41\x00"s, true},
        {"\x01"s, "\xc2\x43\x00\x00\x01"s, true},
        {"\xc2\x41\x01"s, "\xc2\x42\x00\x01"s, true},
        {"\x01"s, "\xc3\x41\x01"s, false},
        {"\x01"s, "\xc2\x41\x02"s, false},
        {"\xc2\x41\x01"s, "\xc3\x41\x01"s, false},
        {"\x01"s, "\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s, false},
        {"\xc1\x01"s, "\xc2\x41\x01"s, false},
        {"\x41\x01"s, "\xc2\x41\x01"s, false},
        {"\xa2\x01\x00\xc2\x41\x01\x00"s, "\xa2\x01\x00\x01\x00"s, true},
        {"\xa2\x01\x00\xc2\x41\x01\x00"s, "\xa2\x01\x00\x02\x00"s, false},
    };
    for (pair const &p : pairs) {
        CAPTURE(p.first);
        CAPTURE(p.second);
        std::string_view const first = p.first;
        std::string_view const second = p.second;
        auto const equal =
            cbor::validity::keys_equivalent(first, 0, second, 0, 0, 16, cbor::validity::limit_checks<false>{});
        REQUIRE(equal.has_value());
        CHECK_EQ(*equal, p.equal);
    }
    CHECK_EQ(*cbor::lazy::copy("\xa2\x01\x00\xc2\x41\x01\x00"s)->count(1), 2u);
    CHECK_EQ(*cbor::lazy::copy("\xa2\x20\x00\xc3\x41\x00\x00"s)->count(-1), 2u);
    CHECK(*cbor::lazy::copy("\xa1\xc2\x41\x01\x61\x61"s)->contains(1));
    CHECK_FALSE(*cbor::lazy::copy("\xa1\xc2\x41\x01\x61\x61"s)->contains(-2));
    CHECK_EQ(cbor::lazy::copy("\xa1\xc2\x41\x01\x61\x61"s)->count(1).has_value(), true);
    CHECK_EQ(cbor::lazy::copy("\xa1\xc2\x61\x61\x61\x61"s)->count(1).error(), error::inadmissible_type_for_tag_content);
    CHECK(test::query("$[1]", *cbor::lazy::copy("\xa1\xc2\x41\x01\x61\x61"s)) ==
          test::query("$[1]", *cbor::lazy::copy("\xa1\x01\x61\x61"s)));
    CHECK(test::query("$[-1]", *cbor::lazy::copy("\xa1\xc3\x40\x61\x61"s)) ==
          test::query("$[-1]", *cbor::lazy::copy("\xa1\x20\x61\x61"s)));
}
