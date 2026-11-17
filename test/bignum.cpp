#include "small_stack.hpp"
#include "reference.hpp"
#include "owned_copy.hpp"
#include "item_head.hpp"
#include <cbor/decode.hpp>
#include <cbor/diagnostic_notation.hpp>
#include <cbor/query.hpp>

#include <cstdint>
#include <expected>
#include <limits>
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

} // namespace

// Ported from test.rb: 'bignum tag 2: positive bignum boundaries (uint64, +1, deep)'. 2^64 - 1 fits major type 0. So it
// comes back as an integer, not as a bignum.
TEST_CASE("bignum tag 2: positive boundaries")
{
    check_both("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv, big(false, power_of_256(8)));
    check_both("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x01"sv, big(false, power_of_256(8, 1)));
    check_both("\xc2\x58\x1a\x01"s + std::string(24, '\0') + "\x39"s, big(false, power_of_256(25, 0x39)));
    check_both("\xc2\x59\x10\x01\x01"s + std::string(4095, '\0') + "\x15"s, big(false, power_of_256(4096, 0x15)));
    auto const r = decode_wire_into_value("\x1b\xff\xff\xff\xff\xff\xff\xff\xff"s);
    REQUIRE(r.has_value());
    CHECK(*r == value{~std::uint64_t{0}});
}

// Ported from test.rb: 'bignum tag 3: negative bignum boundaries'. -(2^64) is -1 - (2^64 - 1), so it
// fits major type 1.
TEST_CASE("bignum tag 3: negative boundaries")
{
    check_both("\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv, big(true, power_of_256(8, 1)));
    check_both("\xc3\x58\x1a\x01"s + std::string(25, '\0'), big(true, power_of_256(25, 0x01)));
    auto const r = decode_wire_into_value("\x3b\xff\xff\xff\xff\xff\xff\xff\xff"s);
    REQUIRE(r.has_value());
    CHECK(*r == value{negative{~std::uint64_t{0}}});
}

// Ported from test.rb: 'bignum: the wire carries the magnitude most significant byte first'.
// The wires are literals in the order of RFC 8949 3.4.3, so a reversed order in the decode does not pass.
TEST_CASE("bignum: the wire carries the magnitude most significant byte first")
{
    CHECK(*decode_wire_into_value("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv) == big(false, power_of_256(8)));
    CHECK(*decode_wire_into_value("\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv) == big(true, power_of_256(8, 1)));
    CHECK(*decode_wire_into_value("\xc2\x4a\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv) == big(false, power_of_256(8)));
}

// Ported from test.rb: 'bignum §3.4.3: zero-length payload — tag(2,h'')=0, tag(3,h'')=-1'.
TEST_CASE("bignum: an empty magnitude is 0 or -1")
{
    CHECK(*decode_wire_into_value("\xc2\x40"sv) == value{std::uint64_t{0}});
    CHECK(*decode_wire_into_value("\xc3\x40"sv) == value{negative{0}});
}

// Ported from test.rb: 'bignum: non-byte-string payload raises TypeError'. The owner decided that a tree decode
// interprets a tag 2 or 3 only over a byte string. Any other content leaves a tag over that content.
TEST_CASE("bignum: content that is not a byte string")
{
    CHECK(*decode_wire_into_value("\xc2\x05"sv) == value{tagged{2, value{std::uint64_t{5}}}});
    CHECK(*decode_wire_into_value("\xc3\x05"sv) == value{tagged{3, value{std::uint64_t{5}}}});
    CHECK_EQ(test::make_lazy_from_copy("\xc2\x05"s)->get<std::uint64_t>().error(), error::inadmissible_type_for_tag_content);
}

// Ported from test.rb: 'bignum: mixed with normal integers in array'.
TEST_CASE("bignum: among normal integers in an array")
{
    check_both("\x85\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x1b\x00\x00\x00\x00\x00\x00\x00\x02"
               "\xc2\x4d\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
               "\x1b\x00\x00\x00\x00\x00\x00\x00\x04\x1b\x00\x00\x00\x00\x00\x00\x00\x05"sv,
               A(1, 2, V(big(false, power_of_256(12, 0))), 4, 5));
}

// The fuzzer found this. The decoder reads the content of tag 2 or 3 one level deeper, as it does for the content of
// every tag. The encode of a tree recurses through the same levels. So a deep tagged bignum that decode gives encodes.
// An input deeper than a small stack holds is refused by decode on a thread with that stack.
TEST_CASE("bignum: encode takes a deep tagged bignum that decode gives")
{
    std::string const bignum = "\xc2\x49"s + std::string(9, '\x01');
    std::string const deep = std::string(test::levels_beyond_small_stack, '\x81') + bignum;
    std::string const shallow = std::string(test::levels_every_stack_holds, '\x81') + bignum;
    CHECK_EQ(test::error_on_small_stack([&deep] { return test::make_lazy_from_copy(deep)->to_item(); }),
             error::nesting_depth_exceeded);
    auto const ok = test::make_lazy_from_copy(shallow)->to_item();
    REQUIRE(ok.has_value());
    std::string again;
    REQUIRE(cbor::encode(again, **ok).has_value());
    CHECK_EQ(again, shallow);
}

// RFC 8949 3.4.3: a bignum is equivalent to the integer of the same numeric value, and leading zeros are allowed. The
// tree holds values (RFC 8949 2). So a tag 2 or 3 whose magnitude fits in 64 bits is that integer, and lazy type() says
// the same. The rows cover the empty magnitude (0 and -1) and leading zeros past eight bytes. They cover both ends of 64
// bits. They also cover the first magnitude that does not fit.
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
        auto const l = test::make_lazy_from_copy(r.wire);
        REQUIRE(l.has_value());
        auto const t = l->type();
        REQUIRE(t.has_value());
        CHECK_EQ(*t, r.type);
        auto const tree = l->to_item();
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type(), r.type);
        CHECK_EQ(read_argument_of_item(**tree), r.argument);
    }
    CHECK_EQ(test::make_lazy_from_copy("\xc3\x48"s + max)->get<std::int64_t>().error(), error::number_out_of_range);
    CHECK_EQ(*test::make_lazy_from_copy("\xc2\x40"s)->get<std::int64_t>(), 0);
    CHECK_EQ(*test::make_lazy_from_copy("\xc2\x41\x05"s)->get<double>(), 5.0);
    CHECK_EQ(*test::make_lazy_from_copy("\xc3\x41\x05"s)->get<double>(), -6.0);
    CHECK_EQ(test::make_lazy_from_copy("\xc2\x41\x05"s)->get<std::span<std::byte const>>().error(), error::incorrect_type);
    CHECK_EQ(*test::make_lazy_from_copy("\xc3\x40"s)->get<std::int64_t>(), -1);
}

// A path read as double gives a bignum as the number of its magnitude (RFC 8949 3.4.3). The errors are those of
// lazy::get<double>. This holds in each form of at_path: over bytes, over a lazy with a compiled query, and in the
// compile mode. The form over bytes once took a second walk for a bignum; the value and the errors must not depend on
// the form.
TEST_CASE("bignum: at_path as double reads a bignum in every form")
{
    auto const query = cbor::path::compile<double>("$.a");
    REQUIRE(query.has_value());
    auto const read = [&](std::string const &encoded) {
        auto const bytes = cbor::at_path(std::string_view(encoded), *query);
        auto const doc = test::make_lazy_from_copy(encoded);
        REQUIRE(doc.has_value());
        auto const run_time = doc->at_path(*query);
        auto const compiled = doc->at_path<"$.a", double>();
        REQUIRE_EQ(bytes.has_value(), run_time.has_value());
        REQUIRE_EQ(bytes.has_value(), compiled.has_value());
        if (!bytes) {
            CHECK_EQ(bytes.error(), run_time.error());
            CHECK_EQ(bytes.error(), compiled.error());
        } else {
            CHECK_EQ(*bytes, *run_time);
            CHECK_EQ(*bytes, *compiled);
        }
        return bytes;
    };
    CHECK_EQ(*read("\xa1\x61\x61\xc3\x41\x05"s), -6.0);
    CHECK_EQ(*read("\xa1\x61\x61\xc2\x41\x05"s), 5.0);
    CHECK_EQ(read("\xa1\x61\x61\xc3\x49"s + std::string(9, '\x01')).error(), error::number_out_of_range);
    CHECK_EQ(read("\xa1\x61\x61\xc3\x61x"s).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(read("\xa1\x61\x61\xc3\x42\x05"s).error(), error::too_little_data);
}

// RFC 8949 3.4.3 admits only a byte string as the content of tag 2 or 3. The owner decided that decode and type()
// interpret only a definite byte string, and leave a tag over any other content (Q4 a). A content that ends early
// gives too_little_data and reads no byte past the input.
TEST_CASE("bignum: a wrong content is a tag in decode and in type()")
{
    struct row {
        std::string_view wire;
        cbor::type type;
    };
    for (row const r : {row{"\xc2\x61\x61"sv, cbor::type::unsigned_bignum}, row{"\xc2\x80"sv, cbor::type::unsigned_bignum},
                        row{"\xc2\x01"sv, cbor::type::unsigned_bignum}, row{"\xc3\x61\x61"sv, cbor::type::negative_bignum}}) {
        CAPTURE(r.wire);
        auto const l = test::make_lazy_from_copy(std::string(r.wire));
        REQUIRE(l.has_value());
        CHECK_EQ(l->type(), r.type);
        auto const tree = l->to_item();
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type(), r.type);
        CHECK_EQ(read_argument_of_item(**tree), r.wire[0] == '\xc2' ? 2u : 3u);
    }
    struct failure {
        std::string_view wire;
        error expected;
    };
    for (failure const f : {failure{"\xc2\x5f\x41\x01\xff"sv, error::indefinite_length},
                            failure{"\xc2\x5f"sv, error::indefinite_length}, failure{"\xc2"sv, error::too_little_data},
                            failure{"\xc2\x41"sv, error::too_little_data}, failure{"\xc2\x49\x01"sv, error::too_little_data}}) {
        CAPTURE(f.wire);
        auto const l = test::make_lazy_from_copy(std::string(f.wire));
        REQUIRE(l.has_value());
        CHECK_EQ(l->to_item().error(), f.expected);
    }
}

// RFC 8949 3.4.3: the value of a bignum that fits is the integer. A tag 28 around it is a plain tag over that
// integer, and a tag 29 is a plain tag over its index.
TEST_CASE("bignum: tag 28 around a bignum that fits is a tag over the integer")
{
    std::string const wire = "\x82\xd8\x1c\xc2\x41\x01\xd8\x1d\x00"s;
    auto const l = test::make_lazy_from_copy(wire);
    REQUIRE(l.has_value());
    auto const tree = l->to_item();
    REQUIRE(tree.has_value());
    auto const elements = list_children_of_item(**tree);
    REQUIRE_EQ(elements.size(), 2u);
    auto const mark = elements[0]->get<std::pair<std::uint64_t, cbor::item const *>>();
    REQUIRE(mark.has_value());
    CHECK_EQ(mark->second->get<std::uint64_t>(), 1u);
    CHECK_EQ(elements[1]->get<std::pair<std::uint64_t, cbor::item const *>>().value().first, 29u);
    CHECK_EQ(*l->at(1)->type(), cbor::type::tag);
    CHECK_EQ(*l->at(1)->get<std::int64_t>(), 0);
    std::string again;
    REQUIRE(cbor::encode(again, **tree).has_value());
    CHECK_EQ(again, "\x82\xd8\x1c\x01\xd8\x1d\x00"s);
}

// RFC 8949 5.6.1 compares keys in the data model, and RFC 8949 3.4.3 makes 2(h'01') the integer 1. So a key
// written as a bignum that fits is the same key as the integer, and a lookup by an integer finds it. A lookup
// refuses a tag 2 whose content is not a byte string.
TEST_CASE("bignum: a key that fits 64 bits is the same key as the integer")
{
    CHECK_EQ(test::make_lazy_from_copy("\xa2\xc2\x41\x01\x00\x01\x01"s)->at_key(1)->get<int>(), 0);
    CHECK_EQ(test::make_lazy_from_copy("\xa2\xc3\x41\x00\x00\x20\x01"s)->at_key(-1)->get<int>(), 0);
    CHECK_EQ(test::make_lazy_from_copy("\xa1\xc2\x41\x01\x61\x61"s)->at_key(1)->get<std::string_view>().has_value(), true);
    CHECK_EQ(test::make_lazy_from_copy("\xa1\xc2\x41\x01\x61\x61"s)->at_key(-2).error(), error::key_not_found);
    CHECK_EQ(test::make_lazy_from_copy("\xa1\xc2\x61\x61\x61\x61"s)->at_key(1).error(), error::inadmissible_type_for_tag_content);
    CHECK(test::query("$[1]", *test::make_lazy_from_copy("\xa1\xc2\x41\x01\x61\x61"s)) ==
          test::query("$[1]", *test::make_lazy_from_copy("\xa1\x01\x61\x61"s)));
    CHECK(test::query("$[-1]", *test::make_lazy_from_copy("\xa1\xc3\x40\x61\x61"s)) ==
          test::query("$[-1]", *test::make_lazy_from_copy("\xa1\x20\x61\x61"s)));
}

namespace
{

std::string encode_edn(std::string const &edn)
{
    auto bytes = cbor::diag2cbor(edn);
    REQUIRE(bytes.has_value());
    return std::move(*bytes);
}

template <class T>
void check_read(std::expected<T, error> const &got, std::expected<T, error> const &expected)
{
    REQUIRE_EQ(got.has_value(), expected.has_value());
    if (expected)
        CHECK_EQ(*got, *expected);
    else
        CHECK_EQ(got.error(), expected.error());
}

bool holds_text_v(std::expected<cbor::lazy, error> const &node)
{
    if (!node)
        return false;
    auto const text = node->get<std::string_view>();
    return text.has_value() && *text.value() == "v";
}

std::size_t count_nodes(std::string const &path, cbor::lazy const &doc)
{
    auto const query = cbor::path::compile<cbor::nodelist>(path);
    REQUIRE(query.has_value());
    auto const nodes = doc.at_path(*query);
    REQUIRE(nodes.has_value());
    return nodes->size();
}

} // namespace

// RFC 8949 3.4.3: the value of a bignum is n for tag 2 and -1 - n for tag 3, and leading zeros of n do not count.
// Appendix A gives the ends of 64 bits. Each reader of a bignum computes that value in its own place, so the test asks
// every reader for each value. A double holds a bignum only where binary64 holds it exactly (lazy::get): 53
// significant bits. 2^64 - 1 and 2^53 + 1 need more; -2^63 and -2^64 do not. A content that is not a byte string is
// invalid (RFC 8949 3.4.3). decode and type() keep such a tag, and get refuses it (the bignum content decision of
// test/bignum.cpp:186). An index in a path is an I-JSON integer (RFC 9535 2.1), so the row of -2^63 and the row of
// 2^53 + 1 have no index form.
TEST_CASE("bignum: every reader gives the value of a bignum")
{
    struct row {
        std::string edn;
        std::expected<std::int64_t, error> as_int64;
        std::expected<std::uint64_t, error> as_uint64;
        std::expected<double, error> as_double;
        cbor::type type;
        std::uint64_t argument;
        std::string number;
    };
    auto const out_of_range = std::unexpected(error::number_out_of_range);
    auto const not_bytes = std::unexpected(error::inadmissible_type_for_tag_content);
    constexpr std::int64_t largest_index = (std::int64_t{1} << 53) - 1;
    row const rows[] = {
        {"2(h'')", 0, 0u, 0.0, cbor::type::unsigned_integer, 0, "0"},
        {"3(h'')", -1, out_of_range, -1.0, cbor::type::negative_integer, 0, "-1"},
        {"2(h'000000000000000001')", 1, 1u, 1.0, cbor::type::unsigned_integer, 1, "1"},
        {"2(h'ffffffffffffffff')", out_of_range, std::numeric_limits<std::uint64_t>::max(), out_of_range,
         cbor::type::unsigned_integer, std::numeric_limits<std::uint64_t>::max(), "18446744073709551615"},
        {"3(h'7fffffffffffffff')", std::numeric_limits<std::int64_t>::min(), out_of_range, -9223372036854775808.0,
         cbor::type::negative_integer, 0x7fffffffffffffffu, "-9223372036854775808"},
        {"3(h'ffffffffffffffff')", out_of_range, out_of_range, -18446744073709551616.0, cbor::type::negative_integer,
         std::numeric_limits<std::uint64_t>::max(), "-18446744073709551616"},
        {"2(h'010000000000000000')", out_of_range, out_of_range, out_of_range, cbor::type::unsigned_bignum, 2, ""},
        {"2(h'20000000000001')", 9007199254740993, 9007199254740993u, out_of_range, cbor::type::unsigned_integer,
         9007199254740993u, "9007199254740993"},
        {"2(1)", not_bytes, not_bytes, not_bytes, cbor::type::unsigned_bignum, 2, ""},
        {"2(\"a\")", not_bytes, not_bytes, not_bytes, cbor::type::unsigned_bignum, 2, ""},
    };
    for (row const &r : rows) {
        CAPTURE(r.edn);
        std::string const encoded = encode_edn(r.edn);
        auto const doc = test::make_lazy_from_copy(encoded);
        REQUIRE(doc.has_value());
        check_read(doc->get<std::int64_t>(), r.as_int64);
        check_read(doc->get<std::uint64_t>(), r.as_uint64);
        check_read(doc->get<double>(), r.as_double);
        CHECK_EQ(doc->type().value(), r.type);
        auto const tree = cbor::decode(encoded);
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type(), r.type);
        CHECK_EQ(read_argument_of_item(**tree), r.argument);
        if (r.number.empty())
            continue;
        if (r.as_int64) {
            auto const keyed = test::make_lazy_from_copy(encode_edn("{" + r.edn + ": \"v\"}"));
            REQUIRE(keyed.has_value());
            CHECK(holds_text_v(keyed->at_key(*r.as_int64)));
            if (*r.as_int64 >= -largest_index && *r.as_int64 <= largest_index) {
                auto const index = cbor::path::compile<cbor::lazy>("$[" + r.number + "]");
                REQUIRE(index.has_value());
                CHECK(holds_text_v(keyed->at_path(*index)));
            }
        }
        auto const list = test::make_lazy_from_copy(encode_edn("[" + r.edn + "]"));
        REQUIRE(list.has_value());
        CHECK_EQ(count_nodes("$[?@ == " + r.number + "]", *list), 1u);
        CHECK_EQ(count_nodes("$[?@ >= " + r.number + "]", *list), 1u);
        auto const maps = test::make_lazy_from_copy(encode_edn("[{" + r.edn + ": 0}]"));
        REQUIRE(maps.has_value());
        CHECK_EQ(count_nodes("$[?@ == {" + r.number + ": 0}]", *maps), 1u);
        auto const plain = test::make_lazy_from_copy(encode_edn("{" + r.number + ": \"v\"}"));
        REQUIRE(plain.has_value());
        auto const key = cbor::path::compile<cbor::lazy>("$[" + r.edn + "]");
        REQUIRE(key.has_value());
        CHECK(holds_text_v(plain->at_path(*key)));
    }
}

// lazy::type() reads the head of the content of a tag 2 or 3 to decide between an integer and a tag. A content
// whose head is cut is an error of the input, and type() gives it.
TEST_CASE("bignum: type() gives the error of a cut content head")
{
    std::string const encoded = encode_edn("2(h'01')");
    CHECK_EQ(test::make_lazy_from_copy(encoded.substr(0, 1))->type().error(), error::too_little_data);
}

// RFC 8949 3.4.3: encode writes a bignum that fits 64 bits as the integer, the preferred form. A tree has such a
// bignum over a byte string only where a tag 28 stood between the tag 2 or 3 and the bytes, because decode reads a
// bignum over a byte string as the integer. Decode drops the tag 28 today. A magnitude that does not fit stays a
// bignum.
TEST_CASE("bignum: encode writes a bignum that fits 64 bits as the integer")
{
    struct row {
        std::string_view edn;
        std::string_view written;
    };
    for (row const r : {row{"2(28(h'05'))", "5"}, row{"3(28(h'00'))", "-1"},
                        row{"2(28(h'010000000000000000'))", "2(h'010000000000000000')"}}) {
        CAPTURE(r.edn);
        auto const tree = cbor::decode(encode_edn(std::string(r.edn)));
        REQUIRE(tree.has_value());
        std::string again;
        REQUIRE(cbor::encode(again, **tree).has_value());
        CHECK_EQ(again, encode_edn(std::string(r.written)));
    }
}
