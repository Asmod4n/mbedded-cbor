#include "binding.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

cbor::lazy lazy_of(std::string const &document)
{
    auto const l = cbor::decode<16>(document);
    REQUIRE(l.has_value());
    return *l;
}

value value_at(cbor::lazy const &l)
{
    test_binding binding;
    auto const v = cbor::lazy_decode<16>(binding, l);
    REQUIRE(v.has_value());
    return *v;
}

cbor::lazy at(cbor::lazy const &l, std::string_view const key)
{
    auto const r = l.at<16>(key);
    REQUIRE(r.has_value());
    return *r;
}

cbor::lazy at(cbor::lazy const &l, std::int64_t const index)
{
    auto const r = l.at<16>(index);
    REQUIRE(r.has_value());
    return *r;
}

} // namespace

// Ported from test.rb: 'lazy: basic key access (string, integer, mixed)'.
TEST_CASE("lazy: key access by string and by integer")
{
    std::string const a = encoded(M("a"s, 1, "b"s, 2));
    CHECK(value_at(at(lazy_of(a), "a")) == V(1));
    std::string const b = encoded(M(1, "one"s, 2, "two"s));
    CHECK(value_at(at(lazy_of(b), 1)) == V("one"s));
    CHECK(value_at(at(lazy_of(b), 2)) == V("two"s));
    std::string const c = encoded(M(1, "int"s, "str"s, "s"s, 100, "c"s));
    CHECK(value_at(at(lazy_of(c), 1)) == V("int"s));
    CHECK(value_at(at(lazy_of(c), "str")) == V("s"s));
    CHECK(value_at(at(lazy_of(c), 100)) == V("c"s));
}

// Ported from test.rb: 'lazy: access errors — empty, missing, out-of-bounds'. Each Ruby error class
// has its own error here: IndexError index_out_of_bounds, KeyError key_not_found, TypeError
// not_indexable.
TEST_CASE("lazy: access errors")
{
    std::string const empty_array = encoded(A());
    std::string const empty_map = encoded(M());
    std::string const one = encoded(M("a"s, 1));
    std::string const three = encoded(A(1, 2, 3));
    std::string const scalar = encoded(V(42));
    CHECK_EQ(lazy_of(empty_array).at<16>(0).error(), error::index_out_of_bounds);
    CHECK_EQ(lazy_of(empty_map).at<16>("x").error(), error::key_not_found);
    CHECK_EQ(lazy_of(one).at<16>("missing").error(), error::key_not_found);
    CHECK_EQ(lazy_of(three).at<16>(99).error(), error::index_out_of_bounds);
    CHECK_EQ(lazy_of(three).at<16>("invalid").error(), error::not_indexable);
    CHECK_EQ(lazy_of(scalar).at<16>("key").error(), error::not_indexable);
}

// Ported from test.rb: 'lazy: deep nesting + wide maps'.
TEST_CASE("lazy: deep nesting and a wide map")
{
    std::string const deep = encoded(A(A(A(A(A(42))))));
    CHECK(value_at(at(at(at(at(at(lazy_of(deep), 0), 0), 0), 0), 0)) == V(42));
    map wide;
    for (int i = 0; i < 100; ++i)
        wide.push_back(entry{V("key_" + std::to_string(i)), V(i)});
    std::string const w = encoded(value{wide});
    CHECK(value_at(at(lazy_of(w), "key_0")) == V(0));
    CHECK(value_at(at(lazy_of(w), "key_50")) == V(50));
    CHECK(value_at(at(lazy_of(w), "key_99")) == V(99));
}

// Ported from test.rb: 'lazy: dig — missing keys return nil, negative array indices work'. dig maps
// to a chain of at; a miss is an error value, and the binding makes nil of it.
TEST_CASE("lazy: negative indices and misses")
{
    std::string const h = encoded(M("a"s, 1, "b"s, M("c"s, 42)));
    CHECK_EQ(lazy_of(h).at<16>("missing").error(), error::key_not_found);
    CHECK(value_at(at(at(lazy_of(h), "b"), "c")) == V(42));
    std::string const a = encoded(A(10, 20, 30, 40, 50));
    CHECK(value_at(at(lazy_of(a), -1)) == V(50));
    CHECK(value_at(at(lazy_of(a), -5)) == V(10));
    CHECK_EQ(lazy_of(a).at<16>(-99).error(), error::index_out_of_bounds);
}

// Ported from test.rb: 'lazy: can still navigate child lazies after calling .value on parent'.
TEST_CASE("lazy: a value of the parent does not change the children")
{
    std::string const doc = encoded(M("a"s, M("b"s, 42)));
    CHECK(value_at(lazy_of(doc)) == M("a"s, M("b"s, 42)));
    CHECK(value_at(at(at(lazy_of(doc), "a"), "b")) == V(42));
}

// Ported from test.rb: 'lazy: random-access stress'. A fixed seed, so a failure repeats.
TEST_CASE("lazy: random access")
{
    array statuses;
    for (int i = 1; i <= 50; ++i)
        statuses.push_back(M("id"s, i, "txt"s, "msg" + std::to_string(i)));
    std::string const doc = encoded(M("statuses"s, value{statuses}));
    std::mt19937 rng(1);
    for (int n = 0; n < 200; ++n) {
        int const i = static_cast<int>(rng() % 50);
        CHECK(value_at(at(at(at(lazy_of(doc), "statuses"), i), "txt")) == V("msg" + std::to_string(i + 1)));
    }
}

// Ported from test.rb: 'lazy: skip_cbor truncation raises RangeError'.
TEST_CASE("lazy: a truncated item before the target")
{
    std::string const doc = "\x82\x4a\x01\x02\x03\x18\x2a"s;
    CHECK_EQ(lazy_of(doc).at<16>(1).error(), error::too_little_data);
}

// Ported from test.rb: 'lazy: huge aref index handled cleanly'.
TEST_CASE("lazy: a huge index")
{
    std::string const doc = encoded(A(1, 2, 3));
    CHECK_EQ(lazy_of(doc).at<16>(0x7fffffff).error(), error::index_out_of_bounds);
}

// Found by the fuzz corpus: a reference inside the mark it names, d8 1c d8 1d 00, led the navigation
// back to itself without end. A reference must name a mark that ends before it.
TEST_CASE("lazy: a reference to its own enclosing mark ends")
{
    std::string const doc = "\xd8\x1c\xd8\x1d\x00"s;
    CHECK_EQ(lazy_of(doc).at<16>(0).error(), error::sharedref_not_complete);
    test_binding binding;
    CHECK_FALSE(cbor::lazy_decode<16>(binding, lazy_of(doc)).has_value());
}

// Found by the fuzz corpus: a map that claims about 7.7 * 10^18 pairs. Before the fix the scan for
// marks before the value of its first key went on through the claimed pairs after it reached the
// target; now it stops there.
TEST_CASE("lazy: a value inside a huge claimed map")
{
    std::string const doc = "\xbb\x6a\xc9\xfb\x32\xf6\xd8\xd8\x27\x61\x61\x19\x00\x00"s;
    auto const a = lazy_of(doc).at<16>("a");
    REQUIRE(a.has_value());
    CHECK(value_at(*a) == V(0));
}

// Found by the fuzzer: [28(28(29(0))), ...] leads from the reference through two marks back to the
// same reference. A reference that the walk meets a second time ends it.
TEST_CASE("lazy: a chain of marks back to the same reference ends")
{
    std::string const doc = "\x92\xd8\x1c\xd8\x1c\xd8\x1d\x00"s;
    auto const element = lazy_of(doc).at<16>(0);
    REQUIRE(element.has_value());
    CHECK_EQ(element->at<16>(0).error(), error::sharedref_not_complete);
}

namespace
{

std::vector<value> elements_of(std::string const &document)
{
    auto const elements = lazy_of(document).elements<16>();
    REQUIRE(elements.has_value());
    std::vector<value> values;
    for (auto const element : *elements) {
        REQUIRE(element.has_value());
        values.push_back(value_at(*element));
    }
    return values;
}

} // namespace

// The elements of an array come in wire order, each as a view of its own.
TEST_CASE("lazy: the elements of an array")
{
    CHECK(elements_of(encoded(A(1, "a"s, A(2)))) == std::vector<value>{V(1), V("a"s), A(2)});
    CHECK(elements_of(encoded(A())).empty());
}

// A mark around the array is passed, as at passes it.
TEST_CASE("lazy: the elements of a marked array")
{
    CHECK(elements_of("\xd8\x1c\x82\x01\x02"s) == std::vector<value>{V(1), V(2)});
}

// The entries of a map come in wire order, each as a pair of key and value.
TEST_CASE("lazy: the entries of a map")
{
    auto const entries = lazy_of(encoded(M("a"s, 1, 2, "b"s))).entries<16>();
    REQUIRE(entries.has_value());
    std::vector<value> keys;
    std::vector<value> values;
    for (auto const entry : *entries) {
        REQUIRE(entry.has_value());
        keys.push_back(value_at(entry->first));
        values.push_back(value_at(entry->second));
    }
    CHECK(keys == std::vector<value>{V("a"s), V(2)});
    CHECK(values == std::vector<value>{V(1), V("b"s)});
}

// Only an array has elements and only a map has entries.
TEST_CASE("lazy: elements and entries of the wrong kind")
{
    CHECK_EQ(lazy_of(encoded(M("a"s, 1))).elements<16>().error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(A(1))).entries<16>().error(), error::not_indexable);
    CHECK_EQ(lazy_of(encoded(V(1))).elements<16>().error(), error::not_indexable);
}

// decode reads nothing ahead, so a step finds a truncated element. The step gives the error once and
// the walk ends after it.
TEST_CASE("lazy: a truncated element ends the walk with its error")
{
    auto const elements = lazy_of("\x83\x01\x62\x61"s).elements<16>();
    REQUIRE(elements.has_value());
    std::vector<std::expected<cbor::lazy, error>> steps;
    for (auto const step : *elements)
        steps.push_back(step);
    REQUIRE(steps.size() == 3);
    CHECK(steps.at(0).has_value());
    CHECK(steps.at(1).has_value());
    CHECK_EQ(steps.at(2).error(), error::too_little_data);
}

namespace
{

template <class T>
auto get(std::string const &document)
{
    return lazy_of(document).get<T>();
}

} // namespace

// The integers of RFC 8949 Appendix A, read without a binding.
TEST_CASE("lazy: get reads an integer")
{
    CHECK_EQ(*get<std::uint64_t>("\x00"s), 0u);
    CHECK_EQ(*get<std::uint64_t>("\x1b\xff\xff\xff\xff\xff\xff\xff\xff"s), 18446744073709551615u);
    CHECK_EQ(*get<std::int64_t>("\x20"s), -1);
    CHECK_EQ(*get<std::int64_t>("\x39\x03\xe7"s), -1000);
    CHECK_EQ(*get<std::int64_t>("\x3b\x7f\xff\xff\xff\xff\xff\xff\xff"s), std::numeric_limits<std::int64_t>::min());
    CHECK_EQ(*get<std::uint64_t>("\xc2\x42\x01\x00"s), 256u);
    CHECK_EQ(*get<std::int64_t>("\xc3\x41\x01"s), -2);
    CHECK_EQ(*get<std::uint64_t>("\xd8\x1c\x05"s), 5u);
}

// A number that the type cannot hold is out of range; anything that is not a number has the wrong type.
TEST_CASE("lazy: get refuses an integer it cannot hold")
{
    CHECK_EQ(get<std::uint64_t>("\x20"s).error(), error::number_out_of_range);
    CHECK_EQ(get<std::int64_t>("\x1b\x80\x00\x00\x00\x00\x00\x00\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(get<std::int64_t>("\x3b\x80\x00\x00\x00\x00\x00\x00\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(get<std::uint64_t>("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s).error(),
             error::number_out_of_range);
    CHECK_EQ(get<std::uint64_t>("\x61\x61"s).error(), error::incorrect_type);
    CHECK_EQ(get<std::uint64_t>("\xf9\x3c\x00"s).error(), error::incorrect_type);
}

// Floats of the three widths of Appendix A. An integer is not a float: CBOR keeps the two apart.
TEST_CASE("lazy: get reads a float")
{
    CHECK_EQ(*get<double>("\xf9\x3c\x00"s), 1.0);
    CHECK_EQ(*get<double>("\xfa\x47\xc3\x50\x00"s), 100000.0);
    CHECK_EQ(*get<double>("\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"s), 1.1);
    CHECK_EQ(get<double>("\x01"s).error(), error::incorrect_type);
}

// The simple values false, true and null of Table 4.
TEST_CASE("lazy: get reads a simple value")
{
    CHECK_EQ(*get<bool>("\xf4"s), false);
    CHECK_EQ(*get<bool>("\xf5"s), true);
    CHECK_EQ(get<bool>("\xf6"s).error(), error::incorrect_type);
    CHECK(get<std::nullptr_t>("\xf6"s).has_value());
    CHECK_EQ(get<std::nullptr_t>("\xf7"s).error(), error::incorrect_type);
}

// A text string and a byte string come as views into the top-level item, each only as its own type. The view holds the
// top-level item, so it stays valid after the lazy that gave it ends.
TEST_CASE("lazy: get reads a string as a view")
{
    auto const text = lazy_of("\x64IETF"s).get<std::string_view>();
    REQUIRE(text.has_value());
    CHECK_EQ(**text, "IETF"sv);
    CHECK_EQ(get<std::string_view>("\x44\x01\x02\x03\x04"s).error(), error::incorrect_type);
    auto const four = lazy_of("\x44\x01\x02\x03\x04"s);
    auto const bytes = four.get<std::span<std::byte const>>();
    REQUIRE(bytes.has_value());
    CHECK(std::ranges::equal(**bytes, std::array{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}));
    CHECK_EQ(get<std::span<std::byte const>>("\x64IETF"s).error(), error::incorrect_type);
    CHECK_EQ(get<std::string_view>("\x62\x61"s).error(), error::too_little_data);
}

// RFC 8746 Table 3: tags 64 to 87 carry a typed array in a byte string; 76 is reserved. The length
// is a multiple of the element size 1 << (f + ll) of Table 2.
TEST_CASE("lazy: get reads a typed array")
{
    auto const u8 = get<cbor::typed_array>("\xd8\x40\x43\x01\x02\x03"s);
    REQUIRE(u8.has_value());
    CHECK_EQ((*u8)->tag, 64u);
    CHECK_EQ((*u8)->bytes.size(), 3u);
    auto const u16 = get<cbor::typed_array>("\xd8\x41\x44\x00\x01\x00\x02"s);
    REQUIRE(u16.has_value());
    CHECK_EQ((*u16)->tag, 65u);
    CHECK_EQ((*u16)->bytes.size(), 4u);
    auto const f64 = get<cbor::typed_array>("\xd8\x56\x48\x00\x00\x00\x00\x00\x00\xf0\x3f"s);
    REQUIRE(f64.has_value());
    CHECK_EQ((*f64)->tag, 86u);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x41\x43\x00\x01\x00"s).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x4c\x41\x00"s).error(), error::incorrect_type);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x3f\x41\x00"s).error(), error::incorrect_type);
    CHECK_EQ(get<cbor::typed_array>("\xd8\x40\x01"s).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(get<cbor::typed_array>("\x43\x01\x02\x03"s).error(), error::incorrect_type);
}

namespace
{

// A binding whose only value is a typed array, as a language with ArrayBuffers has.
struct typed_binding : cbor::binding<cbor::typed_array> {
    cbor::kind kind_of(cbor::typed_array const &)
    {
        return cbor::kind::typed_array;
    }

    cbor::typed_array typed_array_of(cbor::typed_array const &a)
    {
        return a;
    }
};

std::expected<std::string, cbor::error> typed_encoded(std::uint64_t const tag, std::string_view const bytes)
{
    typed_binding binding;
    test::string_writer w;
    auto const r = cbor::encode<16>(binding, w, cbor::typed_array{tag, std::as_bytes(std::span(bytes))});
    if (!r)
        return std::unexpected(r.error());
    return w.encoded;
}

} // namespace

// The encoder writes the tag and one byte string, and get reads both back.
TEST_CASE("encode: a typed array is a tag and a byte string")
{
    auto const wire = typed_encoded(65, "\x00\x01\x00\x02"sv);
    REQUIRE(wire.has_value());
    CHECK_EQ(*wire, "\xd8\x41\x44\x00\x01\x00\x02"s);
    auto const document = lazy_of(*wire);
    auto const back = document.get<cbor::typed_array>();
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->tag, 65u);
    CHECK(std::ranges::equal((*back)->bytes, std::as_bytes(std::span("\x00\x01\x00\x02"sv))));
}

// A tag outside 64 to 87, or the reserved 76, is no typed array; a length that is not a multiple of the
// element size is inadmissible content.
TEST_CASE("encode: a typed array the binding answers wrongly")
{
    CHECK((typed_encoded(76, "\x00"sv).error() == cbor::error{cbor::error::unsupported_value}));
    CHECK((typed_encoded(63, "\x00"sv).error() == cbor::error{cbor::error::unsupported_value}));
    CHECK((typed_encoded(66, "\x00\x01\x02"sv).error() == cbor::error{cbor::error::inadmissible_type_for_tag_content}));
}

namespace
{

// A binding that embeds every array as an encoded data item of its own.
struct embedding_binding : test_binding {
    bool embed_of(value const &v)
    {
        return std::holds_alternative<test::array *>(v.kind);
    }
};

} // namespace

// RFC 8949 3.4.5.1: tag 24 carries an encoded data item in a byte string. The encoder writes each embedded
// value as a top-level item of its own, and a view passes through the tag into it.
TEST_CASE("tag 24: an embedded value is written as a top-level item of its own and read through")
{
    embedding_binding binding;
    test::string_writer w;
    REQUIRE(cbor::encode<16>(binding, w, A(1, A(2, 3))).has_value());
    CHECK_EQ(w.encoded, "\xd8\x18\x48\x82\x01\xd8\x18\x43\x82\x02\x03"s);
    auto const inner = lazy_of(w.encoded).at<16>(1);
    REQUIRE(inner.has_value());
    auto const three = inner->at<16>(1);
    REQUIRE(three.has_value());
    CHECK_EQ(*three->get<std::uint64_t>(), 3u);
    CHECK_EQ(get<std::uint64_t>("\xd8\x18\x05"s).error(), error::inadmissible_type_for_tag_content);
}

// The marks of an embedded data item are its own: the 29(0) inside names the 28 inside, not the one
// outside before it.
TEST_CASE("tag 24: an embedded data item has marks of its own")
{
    std::string const doc = "\x82\xd8\x1c\x07\xd8\x18\x47\x82\xd8\x1c\x09\xd8\x1d\x00"s;
    auto const embedded = lazy_of(doc).at<16>(1);
    REQUIRE(embedded.has_value());
    auto const second = embedded->at<16>(1);
    REQUIRE(second.has_value());
    CHECK_EQ(*second->get<std::uint64_t>(), 9u);
}

// Every integer width reads its own edges. A CBOR integer has a 64-bit magnitude and a sign in the major type
// (RFC 8949 3.1), so a narrower type refuses the first value past its edge as out of range.
TEST_CASE("lazy: get reads every integer width up to its edge")
{
    CHECK_EQ(*get<std::uint8_t>("\x18\xff"s), 255u);
    CHECK_EQ(get<std::uint8_t>("\x19\x01\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::uint16_t>("\x19\xff\xff"s), 65535u);
    CHECK_EQ(get<std::uint16_t>("\x1a\x00\x01\x00\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::uint32_t>("\x1a\xff\xff\xff\xff"s), 4294967295u);
    CHECK_EQ(get<std::uint32_t>("\x1b\x00\x00\x00\x01\x00\x00\x00\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(get<std::uint8_t>("\x20"s).error(), error::number_out_of_range);

    CHECK_EQ(*get<std::int8_t>("\x18\x7f"s), 127);
    CHECK_EQ(get<std::int8_t>("\x18\x80"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::int8_t>("\x38\x7f"s), -128);
    CHECK_EQ(get<std::int8_t>("\x38\x80"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::int16_t>("\x19\x7f\xff"s), 32767);
    CHECK_EQ(*get<std::int16_t>("\x39\x7f\xff"s), -32768);
    CHECK_EQ(get<std::int16_t>("\x39\x80\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(*get<std::int32_t>("\x1a\x7f\xff\xff\xff"s), 2147483647);
    CHECK_EQ(*get<std::int32_t>("\x3a\x7f\xff\xff\xff"s), std::numeric_limits<std::int32_t>::min());
    CHECK_EQ(get<std::int32_t>("\x1a\x80\x00\x00\x00"s).error(), error::number_out_of_range);
}

// RFC 8949 5.6: a byte string key and a text string key with the same bytes are two different keys. A lookup
// by text matches the text key only.
TEST_CASE("lazy: a byte string key is not the text key with the same bytes")
{
    auto const root = lazy_of("\xa2\x41\x61\x01\x61\x61\x02"s);
    auto const a = root.at<16>("a");
    REQUIRE(a.has_value());
    CHECK_EQ(*a->get<std::uint64_t>(), 2u);
}

// A tag 29 reference names a mark that lies before it. A mark that navigation recorded later in the top-level item
// is no target, as in the full decoder.
TEST_CASE("lazy: a reference forward to a mark that navigation recorded is an error")
{
    auto const root = lazy_of("\x82\xd8\x1d\x00\xd8\x1c\x05"s);
    auto const second = root.at<16>(1);
    REQUIRE(second.has_value());
    CHECK_EQ(*second->get<std::uint64_t>(), 5u);
    auto const first = root.at<16>(0);
    REQUIRE(first.has_value());
    test_binding binding;
    CHECK_EQ(cbor::lazy_decode<16>(binding, *first).error(), error::sharedref_not_complete);
}

namespace
{

struct buffer_owner {
    std::string bytes;
    bool *released;

    ~buffer_owner()
    {
        *released = true;
    }
};

} // namespace

// lazy::from with an owner reads bytes that someone else holds, as the page of a read transaction of LMDB. Each
// lazy that a step gives holds the owner too, so the bytes live until the last lazy of the top-level item ends.
TEST_CASE("lazy: from an owner holds the owner until the last lazy ends")
{
    bool released = false;
    std::optional<cbor::lazy> name;
    {
        auto const owner = std::make_shared<buffer_owner>(encoded(M("user"s, M("name"s, "ann"s))), &released);
        auto const doc = cbor::lazy::from(owner, owner->bytes);
        REQUIRE(doc.has_value());
        name.emplace(*doc->at("user").and_then([](cbor::lazy const &u) { return u.at("name"); }));
    }
    CHECK_FALSE(released);
    {
        auto const text = name->get<std::string_view>();
        REQUIRE(text.has_value());
        CHECK_EQ(**text, "ann"sv);
    }
    name.reset();
    CHECK(released);
}

// lazy::from takes the bytes by move and copies nothing. Each step gives a std::expected, and_then gives it to the
// next step, so an error anywhere reaches the end of the chain.
TEST_CASE("lazy: from, at and get as a chain")
{
    std::string bytes = encoded(M("statuses"s, A(M("user"s, M("name"s, "ann"s)), M("user"s, M("name"s, "bob"s)))));
    char const *const data = bytes.data();
    cbor::lazy const doc = *cbor::lazy::from(std::move(bytes));
    auto const name = doc.at("statuses")
                          .and_then([](cbor::lazy const &s) { return s.at(1); })
                          .and_then([](cbor::lazy const &s) { return s.at("user"); })
                          .and_then([](cbor::lazy const &u) { return u.at("name"); })
                          .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "bob"sv);
    CHECK_EQ(static_cast<void const *>(doc.top_level->encoded.data()), static_cast<void const *>(data));
    auto const missing = doc.at("statuses")
                             .and_then([](cbor::lazy const &s) { return s.at(5); })
                             .and_then([](cbor::lazy const &s) { return s.at("user"); })
                             .and_then([](cbor::lazy const &u) { return u.get<std::string_view>(); });
    REQUIRE_FALSE(missing.has_value());
    CHECK_EQ(missing.error(), error::index_out_of_bounds);
    CHECK_FALSE(doc.at("nope").and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); }).has_value());
    std::size_t count = 0;
    for (auto const e : *doc.at("statuses").and_then([](cbor::lazy const &s) { return s.elements(); })) {
        REQUIRE(e.has_value());
        ++count;
    }
    CHECK_EQ(count, 2u);
}

// RFC 8949 3.4 and the registration of tags 28 and 29: a tag 28 marks a value as shared and leaves the value as
// it is, and a tag 29 stands for the marked value. A key under either tag is the key it marks or names, as the
// full decoder reads it. The path fuzzer found a key under tag 28 that lazy::at did not match.
TEST_CASE("lazy: a key under tag 28 or tag 29 is the key it marks or names")
{
    auto const marked = lazy_of("\xa1\xd8\x1c\x61\x61\x01"s).at<16>("a");
    REQUIRE(marked.has_value());
    CHECK_EQ(*marked->get<std::uint64_t>(), 1u);
    auto const number = lazy_of("\xa1\xd8\x1c\x07\x03"s).at<16>(7);
    REQUIRE(number.has_value());
    CHECK_EQ(*number->get<std::uint64_t>(), 3u);
    auto const root = lazy_of("\x82\xd8\x1c\x61\x61\xa1\xd8\x1d\x00\x02"s);
    REQUIRE(root.at<16>(0).has_value());
    auto const named = root.at<16>(1)->at<16>("a");
    REQUIRE(named.has_value());
    CHECK_EQ(*named->get<std::uint64_t>(), 2u);
}

namespace
{

template <class R>
concept read_from_temporary = requires(R &&r) { *std::move(*r); };

} // namespace

// A view that get gives holds the bytes of the top-level item: it outlives the lazy and the owner that made it, and it
// cannot be read through a temporary.
TEST_CASE("lazy: a view holds the top-level item")
{
    bool released = false;
    std::optional<cbor::owning_ref<std::string_view>> view;
    {
        auto const owner = std::make_shared<buffer_owner>(encoded(M("name"s, "ann"s)), &released);
        auto const name = cbor::lazy::from(owner, owner->bytes)
                              .and_then([](cbor::lazy const &d) { return d.at("name"); })
                              .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
        REQUIRE(name.has_value());
        view.emplace(*name);
    }
    CHECK_FALSE(released);
    CHECK_EQ(**view, "ann"sv);
    view.reset();
    CHECK(released);
    CHECK_FALSE(read_from_temporary<std::expected<cbor::owning_ref<std::string_view>, cbor::error>>);
    CHECK_THROWS_AS((void)cbor::lazy::from(std::shared_ptr<void const>{}, "\x00"sv), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. A temporary or a moved std::string beside an
// owner does not compile, because the owner does not hold it. The test exists because each of these let a view
// outlive its bytes, and a check of the stored pointer refused an owner that holds the bytes.
TEST_CASE("lazy: from checks that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>("\xa1\x61\x61\x63xyz"s);
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::lazy::from(o, std::string(*o)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o, std::string s) { cbor::lazy::from(o, std::move(s)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::lazy::from(o, *o); }; }(bytes)));
    std::shared_ptr<void const> const holds_nothing(std::shared_ptr<void const>{}, bytes->data());
    CHECK_THROWS_AS((void)cbor::lazy::from(holds_nothing, *bytes), std::logic_error);
    std::shared_ptr<void const> const holds_bytes(bytes, nullptr);
    auto const name = cbor::lazy::from(holds_bytes, *bytes)
                          .and_then([](cbor::lazy const &d) { return d.at("a"); })
                          .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "xyz"sv);
}

// The forms that take the bytes in a std::shared_ptr<std::string const> read through it. A null pointer was read
// before this test existed, and an empty pointer was kept as the owner of a view.
TEST_CASE("lazy: from and decode refuse a null or empty std::shared_ptr<std::string const>")
{
    std::shared_ptr<std::string const> const null_bytes;
    CHECK_THROWS_AS((void)cbor::lazy::from(null_bytes), std::logic_error);
    CHECK_THROWS_AS((void)cbor::decode<16>(null_bytes), std::logic_error);
    std::string const bytes = "\x00"s;
    std::shared_ptr<std::string const> const holds_nothing(std::shared_ptr<std::string const>{}, &bytes);
    CHECK_THROWS_AS((void)cbor::lazy::from(holds_nothing), std::logic_error);
    CHECK_THROWS_AS((void)cbor::decode<16>(holds_nothing), std::logic_error);
}

// cbor::lazy is an aggregate, so cbor::lazy{} holds no top-level item, and so does the lazy inside a cache
// entry that was not built. Each member read through the null pointer before this test existed; each one now
// refuses it as a wrong use.
TEST_CASE("lazy: every member refuses a lazy that holds no top-level item")
{
    cbor::lazy const none{};
    CHECK_THROWS_AS((void)none.at<16>("a"sv), std::logic_error);
    CHECK_THROWS_AS((void)none.at<16>(std::int64_t{0}), std::logic_error);
    CHECK_THROWS_AS((void)none.get<std::uint64_t>(), std::logic_error);
    CHECK_THROWS_AS((void)none.get<cbor::typed_array>(), std::logic_error);
    CHECK_THROWS_AS((void)none.elements<16>(), std::logic_error);
    CHECK_THROWS_AS((void)none.entries<16>(), std::logic_error);
    CHECK_THROWS_AS((void)none.decode<16>(), std::logic_error);
    test_binding binding;
    CHECK_THROWS_AS((void)cbor::lazy_decode<16>(binding, none), std::logic_error);
}

// A shared reference may stand for the content of a tag: the magnitude of a bignum, the bytes of a typed array.
TEST_CASE("lazy: get follows a shared reference in the content of a tag")
{
    auto const root = lazy_of("\x83\xd8\x1c\x41\x05\xc2\xd8\x1d\x00\xd8\x40\xd8\x1d\x00"s);
    CHECK_EQ(*root.at(1)->get<std::uint64_t>(), 5u);
    auto const typed = root.at(2)->get<cbor::typed_array>();
    REQUIRE(typed.has_value());
    CHECK_EQ((*typed)->bytes.size(), 1u);
}

// The value category of the bytes says what cbor::decode and lazy::from do. A moved std::string becomes the owner and
// is not copied; an lvalue, a const rvalue and a literal are copied once. The top-level item stays valid after the caller
// reuses or destroys its buffer.
TEST_CASE("lazy: decode and from move an rvalue string and copy everything else")
{
    auto text_of = [](cbor::lazy const &l) {
        auto const r = l.get<std::string_view>();
        REQUIRE(r.has_value());
        return std::string(**r);
    };
    std::string const text(40, 't');
    std::string const message = encoded(M("k"s, text));
    auto buffer = std::make_unique<std::string>(message);
    char const *const data = buffer->data();
    auto const moved = cbor::decode<16>(std::move(*buffer));
    REQUIRE(moved.has_value());
    CHECK_EQ(static_cast<void const *>(moved->top_level->encoded.data()), static_cast<void const *>(data));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(text_of(*moved->at("k")), text);

    std::string lvalue = message;
    auto const copied = cbor::decode<16>(lvalue);
    REQUIRE(copied.has_value());
    CHECK_NE(static_cast<void const *>(copied->top_level->encoded.data()), static_cast<void const *>(lvalue.data()));
    CHECK_EQ(lvalue, message);
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(text_of(*copied->at("k")), text);

    std::string const constant = message;
    auto const from_const = cbor::lazy::from(std::move(constant));
    REQUIRE(from_const.has_value());
    CHECK_NE(static_cast<void const *>(from_const->top_level->encoded.data()), static_cast<void const *>(constant.data()));
    CHECK_EQ(constant, message);

    std::string again = message;
    char const *const again_data = again.data();
    auto const from_moved = cbor::lazy::from(std::move(again));
    CHECK_EQ(static_cast<void const *>(from_moved->top_level->encoded.data()), static_cast<void const *>(again_data));
    auto const from_lvalue = cbor::lazy::from(message);
    CHECK_NE(static_cast<void const *>(from_lvalue->top_level->encoded.data()), static_cast<void const *>(message.data()));

    CHECK_EQ(text_of(*cbor::decode<16>("\x63" "abc")), "abc");
    char const *const pointer = "\x62" "ab";
    CHECK_EQ(text_of(*cbor::lazy::from(pointer)), "ab");
    CHECK_EQ(text_of(*cbor::decode<16>("\x61" "a"sv)), "a");
}

namespace
{

template <std::size_t DepthMax>
std::array<bool, 9> lazy_compiles()
{
    return {
        requires(std::string_view const s) { cbor::decode<DepthMax>(s); },
        requires(std::string &&s) { cbor::decode<DepthMax>(std::move(s)); },
        requires(std::shared_ptr<std::string const> const &s) { cbor::decode<DepthMax>(s); },
        requires(cbor::lazy const &l) { l.template at<DepthMax>(std::string_view{}); },
        requires(cbor::lazy const &l) { l.template at<DepthMax>(std::int64_t{0}); },
        requires(cbor::lazy const &l) { l.template elements<DepthMax>(); },
        requires(cbor::lazy const &l) { l.template entries<DepthMax>(); },
        requires(cbor::lazy const &l) { l.template decode<DepthMax>(); },
        requires(test_binding &b, cbor::lazy const &l) { cbor::lazy_decode<DepthMax>(b, l); },
    };
}

} // namespace

// DepthMax has an upper bound of 1024 on every form that takes it, so no form can be given a depth that overflows the
// stack. Each form is checked alone: each one compiles with 1024, and none compiles with 1025.
TEST_CASE("lazy: DepthMax is at most 1024")
{
    CHECK(std::ranges::all_of(lazy_compiles<1024>(), std::identity{}));
    CHECK(std::ranges::none_of(lazy_compiles<1025>(), std::identity{}));
}
