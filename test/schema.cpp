#include "binding.hpp"

#ifdef __cpp_impl_reflection

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdfloat>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

enum class color : std::uint16_t { black, white };

struct [[=cbor::tag(1500)]] wheel {
    std::uint16_t diameter;
    float airPressure;
    bool snowTires;
};

} // namespace

// Each kind of field has the size that RFC 8949 3 gives its head plus its argument, so a reader finds every
// field at an offset the compiler knows. The expectations are counted from the specification by hand.
TEST_CASE("fixed_size: numbers and simple values have the size of head and argument")
{
    // 0xf4 or 0xf5, one initial byte (RFC 8949 3.3).
    CHECK_EQ(cbor::schema<wheel>::fixed_size<bool>(), 1u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<bool const>(), 1u);
    // An integer of n bytes is 0x18 + log2(n) followed by n bytes (RFC 8949 3).
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::uint8_t>(), 2u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::int16_t>(), 3u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::uint32_t>(), 5u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::int64_t>(), 9u);
    // An enum has the size of its underlying type.
    CHECK_EQ(cbor::schema<wheel>::fixed_size<color>(), 3u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::byte>(), 2u);
#ifdef __SIZEOF_INT128__
    // A 128-bit integer is tag 2 or 3 (one byte) and a byte string of 16 bytes (RFC 8949 3.4.3).
    CHECK_EQ(cbor::schema<wheel>::fixed_size<cbor::int128>(), 18u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<cbor::uint128>(), 18u);
#endif
}

// RFC 8949 3.3 has binary16, binary32 and binary64. binary128 is a typed array of one element under tag 83
// (RFC 8746 2.1): two bytes of tag, one byte of byte string head, 16 bytes. The 80-bit extended precision of
// x87 is widened to binary128, and bfloat16 to binary32, without loss.
TEST_CASE("fixed_size: floats by their digits")
{
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::float16_t>(), 3u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::bfloat16_t>(), 5u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<float>(), 5u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<double>(), 9u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::float128_t>(), 19u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<long double>(), std::numeric_limits<long double>::digits == 53 ? 9u : 19u);
}

// A text or a byte string of fixed length has its head and its bytes. An array of fixed length has its head and
// its elements, each of fixed size.
TEST_CASE("fixed_size: text, bytes and arrays of fixed length")
{
    CHECK_EQ(cbor::schema<wheel>::fixed_size<char[32]>(), 34u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<char8_t[3]>(), 4u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::array<char, 24>>(), 26u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::byte[4]>(), 5u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<unsigned char[300]>(), 303u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::array<std::uint16_t, 4>>(), 13u);
    // std::uint8_t is unsigned char, so a span of it is a byte string.
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::span<std::uint8_t, 2>>(), 3u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::span<std::uint16_t, 2>>(), 7u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::uint32_t[2]>(), 11u);
}

// draft-ietf-cbor-packed-19 4.2: a struct is a record, its values behind a straight reference to the table entry
// that holds its keys. The tag of the class stands outside the reference; tag 1500 is d9 05 dc in preferred
// serialization (RFC 8949 3.4 and 4.1). The reference is d8 80 to d8 87, the value array has a head of 9a and 4
// bytes, so 10 bytes come before the values. wheel: 10, then 3 + 5 + 1.
TEST_CASE("fixed_size: a struct is a record of its values under the tag of its class")
{
    CHECK_EQ(cbor::schema<wheel>::fixed_size(), 19u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::array<wheel, 4>>(), 1u + 4u * 19u);
}

// A part of variable size is a shared item in the table of tag 113. The record holds the reference c6 1a <N> or
// c6 3a <N>: tag 6 with an argument of fixed width (draft-ietf-cbor-packed-19 2.2), so 6 bytes.
TEST_CASE("fixed_size: a part of variable size takes 6 bytes")
{
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::string>(), 6u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::vector<wheel>>(), 6u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::map<int, int>>(), 6u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::optional<int>>(), 6u);
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::span<std::uint8_t>>(), 6u);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1501)]] engine {
    std::uint16_t horsepower;
    std::uint32_t cc;
};

struct [[=cbor::tag(1502)]] car {
    std::uint8_t seats;
    engine motor;
    bool hasNavSystem;
};

struct [[=cbor::tag(1503)]] empty {
};

struct [[=cbor::tag(1504)]] thirty {
    std::uint8_t m00, m01, m02, m03, m04, m05, m06, m07, m08, m09, m10, m11, m12, m13, m14;
    std::uint8_t m15, m16, m17, m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29;
};

struct [[=cbor::tag(1505)]] long_name {
    std::uint8_t a_member_name_of_twenty_nine_b;
    std::uint8_t b;
};

struct [[=cbor::tag(1506)]] größe {
    std::uint8_t höhe;
    std::uint8_t b;
};

} // namespace

// The offset points at the head of a value. The keys are in the table, so the 3 bytes of the class tag and the 7
// bytes of the record head come first. wheel: diameter at 10, airPressure behind its 3 bytes at 13, snowTires
// behind its 5 bytes at 18.
TEST_CASE("member_offset: the head of each value, behind the class tag and the head of the record")
{
    CHECK_EQ(cbor::schema<wheel>::member_offset<^^wheel::diameter>(), 10u);
    CHECK_EQ(cbor::schema<wheel>::member_offset<^^wheel::airPressure>(), 13u);
    CHECK_EQ(cbor::schema<wheel>::member_offset<^^wheel::snowTires>(), 18u);
    CHECK_EQ(cbor::schema<wheel const>::member_offset<^^wheel::snowTires>(), 18u);
}

// A nested struct is a record at the offset of its member, so offsets add up. car: 10, seats 2, so the engine record
// with its own class tag is at 12; inside it horsepower at 10.
TEST_CASE("member_offset: offsets of nested structs add up")
{
    CHECK_EQ(cbor::schema<car>::member_offset<^^car::motor>(), 12u);
    CHECK_EQ(cbor::schema<engine>::member_offset<^^engine::horsepower>(), 10u);
    CHECK_EQ(cbor::schema<car>::member_offset<^^car::hasNavSystem>(), 12u + cbor::schema<engine>::fixed_size());
}

// The count of values has a head of 9a and 4 bytes whatever the count, so a member added later moves no offset, and
// a long name or a name in UTF-8 takes no place in the record.
TEST_CASE("member_offset: the record head has one width for any count and any name")
{
    CHECK_EQ(cbor::schema<thirty>::member_offset<^^thirty::m00>(), 10u);
    CHECK_EQ(cbor::schema<thirty>::member_offset<^^thirty::m29>(), 10u + 29u * 2u);
    CHECK_EQ(cbor::schema<thirty>::fixed_size(), 10u + 30u * 2u);
    CHECK_EQ(cbor::schema<long_name>::member_offset<^^long_name::a_member_name_of_twenty_nine_b>(), 10u);
    CHECK_EQ(cbor::schema<empty>::fixed_size(), 10u);
    CHECK_EQ(cbor::schema<größe>::member_offset<^^größe::b>(), 12u);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1507)]] login {
    std::uint16_t id;
    bool ok;
    std::string name;
};

struct [[=cbor::tag(1508)]] measures {
    std::int16_t t;
    float f;
    std::vector<std::uint16_t> v;
    std::optional<std::uint8_t> o;
};

struct [[=cbor::tag(1509)]] named {
    std::string n;
};

struct [[=cbor::tag(1510)]] people {
    std::vector<named> people;
};

std::string schema_bytes(auto const &value)
{
    string_writer w;
    REQUIRE(cbor::schema<std::remove_cvref_t<decltype(value)>>::encode(value, w).has_value());
    return w.bytes;
}

// The schema encoding is one CBOR item, tag 113 over the table and the two parts. Every byte is well-formed, so
// the generic decoder finds its end at the end of the message.
void check_one_item(std::string const &bytes)
{
    auto const end = cbor::doc_end<16>(bytes);
    REQUIRE(end.has_value());
    CHECK_EQ(*end, bytes.size());
}

} // namespace

// draft-ietf-cbor-packed-19 3.1 and 4.2: tag 113 holds an array of the table and the rump. The table holds the
// record function 114 over the keys at index 0, the directory at index 1 (a byte string of the u32 offsets of the
// shared items), f7 up to index 15, and the shared items from index 16 on, each with a head of fixed width. The rump
// is the class tag 1507 = d9 05 e3 (RFC 8949 3.4), then the record d8 80 with the count 9a 00 00 00 03, every number
// in the width of its type, and the string as 6(0) = c6 1a 00 00 00 00, which 2.2 resolves to index 16. The table
// head counts 17 entries. The string starts at byte 45 (2d). The bytes are written from the draft and RFC 8949 3 by
// hand.
TEST_CASE("encode: a struct with a number, a bool and a string")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x11\xd8\x72\x83\x62id\x62ok\x64name"
                    "\x5a\x00\x00\x00\x04\x00\x00\x00\x2d"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x7a\x00\x00\x00\x02"
                    "ab"
                    "\xd9\x05\xe3\xd8\x80\x9a\x00\x00\x00\x03\x19\x00\x05\xf5\xc6\x1a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// A negative int16 is major type 1 with -1 - n in two bytes (RFC 8949 3.1). A float stays binary32 (fa). The list
// of uint16 is shared item 16 and keeps the width of its elements. The empty optional is shared item 17, an empty
// array. Item 17 is 6(-1) = c6 3a 00 00 00 00, because 2.2 maps a negative N to index 16 - 2N - 1. The items start
// at 46 (2e) and 57 (39). The rump starts with the class tag 1508 = d9 05 e4.
TEST_CASE("encode: signed numbers, floats, a list and an empty optional")
{
    std::string const bytes = schema_bytes(measures{-5, 1.5f, {7, 8}, std::nullopt});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x12\xd8\x72\x84\x61t\x61\x66\x61v\x61o"
                    "\x5a\x00\x00\x00\x08\x00\x00\x00\x2e\x00\x00\x00\x39"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x9a\x00\x00\x00\x02\x19\x00\x07\x19\x00\x08"
                    "\x9a\x00\x00\x00\x00"
                    "\xd9\x05\xe4\xd8\x80\x9a\x00\x00\x00\x04\x39\x00\x04\xfa\x3f\xc0\x00\x00"
                    "\xc6\x1a\x00\x00\x00\x00\xc6\x3a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// Two record functions take index 0 and 1, the directory index 2. The list is item 16; its elements are records
// of fixed size, each the class tag 1509 = d9 05 e5 over d8 81 for the second type. The strings of the elements
// follow the list as items 17 and 18, in the order the encoder meets them: 6(-1) and 6(1). The items start at
// 53 (35), 90 (5a) and 96 (60). The root is the class tag 1510 = d9 05 e6 over d8 80.
TEST_CASE("encode: a list of structs that hold strings")
{
    std::string const bytes = schema_bytes(people{{{"x"}, {"yz"}}});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x13\xd8\x72\x81\x66people\xd8\x72\x81\x61n"
                    "\x5a\x00\x00\x00\x0c\x00\x00\x00\x35\x00\x00\x00\x5a\x00\x00\x00\x60"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x9a\x00\x00\x00\x02"
                    "\xd9\x05\xe5\xd8\x81\x9a\x00\x00\x00\x01\xc6\x3a\x00\x00\x00\x00"
                    "\xd9\x05\xe5\xd8\x81\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x01"
                    "\x7a\x00\x00\x00\x01x\x7a\x00\x00\x00\x02yz"
                    "\xd9\x05\xe6\xd8\x80\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

#endif

#ifdef __cpp_impl_reflection

// view checks that the keys are the keys of the type, byte for byte, and that the message is at least as long as
// the schema needs. A table of other keys is another type.
TEST_CASE("view: a struct checks its keys and the least size of the message")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK(cbor::schema<login>::view(bytes).has_value());
    CHECK_FALSE(cbor::schema<login>::view(std::string_view(bytes).substr(0, 20)).has_value());
    std::string other = bytes;
    other.replace(8, 2, "ID");
    CHECK_EQ(cbor::schema<login>::view(other).error(), error::incorrect_type);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1511)]] tire {
    std::uint16_t diameter;
    float airPressure;
};

struct [[=cbor::tag(1512)]] vehicle {
    std::string make;
    std::int32_t balance;
    std::array<tire, 2> spare;
    std::vector<tire> wheels;
    engine motor;
    char code[4];
    std::optional<std::uint8_t> owner;
    std::optional<std::uint8_t> none;
};

vehicle const sample_vehicle{"Tesla", -7, {{{15, 2.5f}, {16, 2.0f}}}, {{17, 1.5f}, {18, 3.0f}, {19, 0.5f}},
                             {300, 1800}, {'A', 'B', 'C', 'D'}, std::uint8_t{9}, std::nullopt};

} // namespace

// A path over fixed fields becomes one offset at compile time, so the result is the value itself.
TEST_CASE("at_path: fixed fields give the value")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    auto const doc = cbor::schema<vehicle>::view(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(cbor::schema<vehicle>::at_path<".balance">(*doc), -7);
    CHECK_EQ(cbor::schema<vehicle>::at_path<".motor.cc">(*doc), 1800u);
    CHECK_EQ(cbor::schema<vehicle>::at_path<".spare[1].diameter">(*doc), 16u);
    CHECK_EQ(cbor::schema<vehicle>::at_path<".spare[0].airPressure">(*doc), 2.5f);
    auto const code = cbor::schema<vehicle>::at_path<".code">(*doc);
    CHECK_EQ(code, "ABCD"sv);
    auto const motor = cbor::schema<vehicle>::at_path<".motor">(*doc);
    CHECK_EQ(cbor::schema<vehicle>::at_path<".horsepower">(motor), 300u);
}

// A step over a part of variable size reads an offset and a length from the wire, so the result is an expected.
TEST_CASE("at_path: parts of variable size give an expected")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    auto const doc = cbor::schema<vehicle>::view(bytes);
    REQUIRE(doc.has_value());
    auto const make = cbor::schema<vehicle>::at_path<".make">(*doc);
    REQUIRE(make.has_value());
    CHECK_EQ(*make, "Tesla"sv);
    CHECK_EQ(*cbor::schema<vehicle>::at_path<".wheels[2].diameter">(*doc), 19u);
    CHECK_EQ(*cbor::schema<vehicle>::at_path<".wheels[1].airPressure">(*doc), 3.0f);
    CHECK_EQ(cbor::schema<vehicle>::at_path<".wheels[3].diameter">(*doc).error(), error::index_out_of_bounds);
    CHECK_EQ(**cbor::schema<vehicle>::at_path<".owner">(*doc), 9u);
    CHECK_FALSE(cbor::schema<vehicle>::at_path<".none">(*doc)->has_value());
}

// A directory offset from the wire that points at bytes of another kind, or a length past the start of the next
// item, is refused and reads nothing. The directory of vehicle starts behind the prefix and its head 5a.
TEST_CASE("at_path: a broken offset is an error")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    std::size_t const head = bytes.find("\x5a"s);
    REQUIRE_NE(head, std::string::npos);
    std::string backward = bytes;
    backward.replace(head + 5, 4, "\x00\x00\x00\x01"s);
    auto const a = cbor::schema<vehicle>::view(backward);
    CHECK((!a || !cbor::schema<vehicle>::at_path<".make">(*a)));
    std::size_t const make = bytes.find("\x7a\x00\x00\x00\x05Tesla"s);
    REQUIRE_NE(make, std::string::npos);
    std::string past = bytes;
    past.replace(make + 1, 4, "\x00\x00\xff\xff"s);
    auto const p = cbor::schema<vehicle>::view(past);
    CHECK((!p || !cbor::schema<vehicle>::at_path<".make">(*p)));
    CHECK_FALSE(cbor::schema<vehicle>::decode(backward).has_value());
    CHECK_FALSE(cbor::schema<vehicle>::decode(past).has_value());
}

namespace
{

enum class shade : std::uint8_t { dark, light };

struct [[=cbor::tag(1513)]] badge {
    std::array<std::byte, 4> mac;
    shade tone;
    std::string label;
};

} // namespace

// A fixed-length array of bytes is a byte string laid out inline (RFC 8949 3.1), the same as a fixed text.
// The reader gives the bytes themselves, not an offset and a length read out of them.
TEST_CASE("at_path: a fixed byte array is read inline")
{
    badge const b{{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}, shade::light, "x"};
    std::string const bytes = schema_bytes(b);
    auto const doc = cbor::schema<badge>::view(bytes);
    REQUIRE(doc.has_value());
    auto const mac_read = cbor::schema<badge>::at_path<".mac">(*doc);
    CHECK_EQ(mac_read, "\x01\x02\x03\x04"sv);
    CHECK_EQ(cbor::schema<badge>::at_path<".tone">(*doc), shade::light);
    std::string zeros = bytes;
    std::size_t const mac =
        static_cast<std::size_t>(doc->field.data() - doc->bytes.data()) + cbor::schema<badge>::member_offset<^^badge::mac>();
    zeros.replace(mac + 1, 4, "\x00\x00\x00\x00"s);
    auto const zero_doc = cbor::schema<badge>::view(zeros);
    REQUIRE(zero_doc.has_value());
    auto const zero_read = cbor::schema<badge>::at_path<".mac">(*zero_doc);
    CHECK_EQ(zero_read, "\x00\x00\x00\x00"sv);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1514)]] garage {
    std::vector<tire> tires;
    std::vector<std::string> names;
    std::vector<std::vector<std::uint16_t>> rows;
    std::map<std::uint16_t, std::string> owners;
};

garage const sample_garage{{{17, 1.5f}, {18, 3.0f}, {19, 0.5f}},
                           {"a", "bc"},
                           {{1, 2}, {}, {3}},
                           {{7, "seven"}, {9, "nine"}}};

} // namespace

// RFC 8949 3.1 major type 4: a list is an array. Each element has a fixed size, so element i lies at i times
// that size behind the data, and at(i) reads it without walking the ones before. A list is read once, in order.
TEST_CASE("cbor::array: a list of structs, strings and lists, by index and in order")
{
    std::string const bytes = schema_bytes(sample_garage);
    auto const doc = cbor::schema<garage>::view(bytes);
    REQUIRE(doc.has_value());
    auto const tires = cbor::schema<garage>::at_path<".tires">(*doc);
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 3u);
    std::uint32_t sum = 0;
    for (auto const t : *tires)
        sum += *cbor::schema<garage>::at_path<".diameter">(*t);
    CHECK_EQ(sum, 17u + 18u + 19u);
    auto const second = tires->at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(cbor::schema<garage>::at_path<".airPressure">(*second), 3.0f);
    CHECK_EQ(tires->at(3).error(), error::index_out_of_bounds);

    auto const names = cbor::schema<garage>::at_path<".names">(*doc);
    REQUIRE(names.has_value());
    auto const name = names->at(1);
    REQUIRE(name.has_value());
    CHECK_EQ(*name, "bc"sv);

    auto const rows = cbor::schema<garage>::at_path<".rows">(*doc);
    REQUIRE(rows.has_value());
    auto const third = rows->at(2);
    REQUIRE(third.has_value());
    CHECK_EQ(third->size(), 1u);
    CHECK_EQ(*third->at(0), 3u);
    CHECK_EQ(rows->at(1)->size(), 0u);
}

// RFC 8949 3.1 major type 5: a std::map is a map. Each pair has the fixed size of its key and its value, so the
// pairs are read in the order the sender wrote them.
TEST_CASE("cbor::map: the pairs of a std::map in order")
{
    std::string const bytes = schema_bytes(sample_garage);
    auto const doc = cbor::schema<garage>::view(bytes);
    REQUIRE(doc.has_value());
    auto const owners = cbor::schema<garage>::at_path<".owners">(*doc);
    REQUIRE(owners.has_value());
    CHECK_EQ(owners->size(), 2u);
    std::string seen;
    for (auto const [key, value] : *owners) {
        REQUIRE(key.has_value());
        REQUIRE(value.has_value());
        seen += std::to_string(*key) + "=" + std::string(*value) + ";";
    }
    CHECK_EQ(seen, "7=seven;9=nine;");
    CHECK_EQ(owners->value_at(2).error(), error::index_out_of_bounds);
}

#endif

#ifdef __cpp_impl_reflection

// The simple form: encode gives the bytes, decode gives the whole struct back. Every field takes the value it
// went in with, the lists, the map and the optional included.
TEST_CASE("encode and decode: a struct goes in and comes back whole")
{
    std::string const bytes = *cbor::schema<garage>::encode(sample_garage);
    auto const decoded_back = *cbor::schema<garage>::decode(bytes);
    garage const &back = *decoded_back;
    REQUIRE_EQ(back.tires.size(), 3u);
    CHECK_EQ(back.tires.at(2).diameter, 19u);
    CHECK_EQ(back.tires.at(1).airPressure, 3.0f);
    CHECK_EQ(back.names, sample_garage.names);
    CHECK_EQ(back.rows, sample_garage.rows);
    CHECK_EQ(back.owners, sample_garage.owners);

    std::string const encoded = *cbor::schema<vehicle>::encode(sample_vehicle);
    auto const decoded_v = *cbor::schema<vehicle>::decode(encoded);
    vehicle const &v = *decoded_v;
    CHECK_EQ(v.make, "Tesla");
    CHECK_EQ(v.balance, -7);
    CHECK_EQ(v.spare.at(1).diameter, 16u);
    CHECK_EQ(v.motor.cc, 1800u);
    CHECK_EQ(std::string_view(v.code, 4), "ABCD"sv);
    CHECK_EQ(v.owner, std::optional<std::uint8_t>{9});
    CHECK_FALSE(v.none.has_value());
}

// Broken bytes give the error as a value, as with std::expected.
TEST_CASE("decode: an error is read as a value")
{
    std::string const bytes = *cbor::schema<vehicle>::encode(sample_vehicle);
    std::string_view const cut = std::string_view(bytes).substr(0, 3);
    auto const r = cbor::schema<vehicle>::decode(cut);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::too_little_data);
}

namespace
{

struct [[=cbor::tag(1515)]] node {
    std::vector<node> children;
};

void u32_append(std::string &out, std::uint32_t const value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out += static_cast<char>(value >> shift);
}

// draft-ietf-cbor-packed-19 2.2: item j sits at table index 16 + j. An even j is 6(j / 2), an odd j is
// 6(-(j + 1) / 2), so its argument is (j - 1) / 2 under major type 1.
void reference_append(std::string &out, std::uint32_t const item)
{
    out += '\xc6';
    out += item % 2 == 0 ? '\x1a' : '\x3a';
    u32_append(out, item / 2);
}

// A chain of nodes, each with one child, as an attacker writes it by hand. Item k is the list of children of the
// node at depth k: one node that refers to item k + 1, and the last item is empty. Each record carries the class tag
// 1515 = d9 05 eb of node. The encoder of this library cannot write it, because gcc does not inline a recursive
// encoder.
std::string node_chain(std::size_t const levels)
{
    auto const items = static_cast<std::uint32_t>(levels + 1);
    std::string out = "\xd8\x71\x82\x9a"s;
    u32_append(out, 16 + items);
    out += "\xd8\x72\x81\x68" "children" "\x5a"s;
    u32_append(out, 4 * items);
    std::size_t const directory = out.size();
    out.append(4 * items, '\0');
    out.append(14, '\xf7');
    for (std::uint32_t k = 0; k < items; ++k) {
        std::string offset;
        u32_append(offset, static_cast<std::uint32_t>(out.size()));
        out.replace(directory + 4 * k, 4, offset);
        bool const last = k + 1 == items;
        out += "\x9a"s;
        u32_append(out, last ? 0 : 1);
        if (!last) {
            out += "\xd9\x05\xeb\xd8\x80\x9a\x00\x00\x00\x01"s;
            reference_append(out, k + 1);
        }
    }
    out += "\xd9\x05\xeb\xd8\x80\x9a\x00\x00\x00\x01"s;
    reference_append(out, 0);
    return out;
}

std::size_t depth_of(node const &root)
{
    std::size_t depth = 0;
    for (node const *at = &root; !at->children.empty(); at = &at->children.front())
        ++depth;
    return depth;
}

struct [[=cbor::tag(65536)]] keyed {
    [[=cbor::key("x-user-id")]] std::uint8_t m0;
    [[=cbor::key("EOF")]] bool m1;
};

struct [[=cbor::tag(1518)]] guarded {
    std::uint8_t id;
    [[=cbor::skip{}]] std::uint8_t secret;
};

} // namespace

// cbor::skip holds in the schema form too: the member has no key in the table and takes no place in the record.
// The record is the class tag 1518 = d9 05 ee, 7 bytes of record head and the integer.
TEST_CASE("schema: cbor::skip leaves a public member out")
{
    CHECK_EQ(cbor::schema<guarded>::fixed_size(), 12u);
    std::string const bytes = *cbor::schema<guarded>::encode(guarded{7, 42});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x10\xd8\x72\x81\x62id\x5a\x00\x00\x00\x00"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xd9\x05\xee\xd8\x80\x9a\x00\x00\x00\x01\x18\x07"s);
    auto const decoded_back = *cbor::schema<guarded>::decode(bytes);
    guarded const &back = *decoded_back;
    CHECK_EQ(back.id, 7u);
    CHECK_EQ(back.secret, 0u);
}

// The key of an annotation takes the place of the member name in the table. The expectation is counted from the
// draft and RFC 8949 3 by hand: the keys with the text heads 69 and 63, an empty directory, f7 up to index 15,
// then the class tag 65536, which needs the 4-byte argument da 00 01 00 00 (RFC 8949 3 and 4.1), the record head of
// 7 bytes, the integer with its fixed width of 18 and 1 byte, and the simple value. The test exists because a tag
// number above 65535 makes the head of the class tag 5 bytes long, and every offset behind it moves by that.
TEST_CASE("schema: an annotation gives the key of a member")
{
    CHECK_EQ(cbor::schema<keyed>::fixed_size(), 15u);
    CHECK_EQ(cbor::schema<keyed>::member_offset<^^keyed::m0>(), 12u);
    CHECK_EQ(cbor::schema<keyed>::member_offset<^^keyed::m1>(), 14u);
    std::string const bytes = *cbor::schema<keyed>::encode(keyed{7, true});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x10\xd8\x72\x82\x69x-user-id\x63"
                    "EOF\x5a\x00\x00\x00\x00"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xda\x00\x01\x00\x00\xd8\x80\x9a\x00\x00\x00\x02\x18\x07\xf5"s);
    auto const decoded_back = *cbor::schema<keyed>::decode(bytes);
    keyed const &back = *decoded_back;
    CHECK_EQ(back.m0, 7u);
    CHECK(back.m1);
    auto const doc = cbor::schema<keyed>::view(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(cbor::schema<keyed>::at_path<".EOF">(*doc), true);
}

// A struct that holds a list of itself is read by recursion, and the bytes decide how deep. Without a limit, a
// chain of 100000 nodes in 2.2 MB overflowed a stack of 8 MiB. Each reference that is followed counts one
// level, as each nested item counts one in the other decoders.
TEST_CASE("decode: a struct that holds itself stops at DepthMax")
{
    REQUIRE_EQ(cbor::schema<node>::fixed_size(), 16u);

    auto const at_limit = cbor::schema<node>::decode(node_chain(64));
    REQUIRE(at_limit.has_value());
    CHECK_EQ(depth_of(**at_limit), 64u);

    auto const over = cbor::schema<node>::decode(node_chain(65));
    REQUIRE_FALSE(over.has_value());
    CHECK_EQ(over.error(), error::nesting_depth_exceeded);

    auto const small = cbor::schema<node>::decode<3>(node_chain(3));
    REQUIRE(small.has_value());
    CHECK_EQ(depth_of(**small), 3u);
    CHECK_EQ(cbor::schema<node>::decode<3>(node_chain(4)).error(), error::nesting_depth_exceeded);

    auto const deep = cbor::schema<node>::decode(node_chain(100000));
    REQUIRE_FALSE(deep.has_value());
    CHECK_EQ(deep.error(), error::nesting_depth_exceeded);
}

#endif

#ifdef __cpp_impl_reflection

// encode writes into a target of the caller: a growing container gets the message appended, a fixed span takes
// it if it fits, and any other target gives an object with append and done through allocate. done carries the
// final length of the message. A span that holds the message but not the padding of a head still takes it.
TEST_CASE("encode: into a string, a vector, a span and a writer of the caller")
{
    std::string const expected = *cbor::schema<garage>::encode(sample_garage);
    std::string text = "x";
    CHECK_EQ(*cbor::schema<garage>::encode(sample_garage, text), expected.size());
    CHECK_EQ(text, "x" + expected);
    std::vector<std::byte> bytes;
    REQUIRE(cbor::schema<garage>::encode(sample_garage, bytes).has_value());
    CHECK_EQ(bytes.size(), expected.size());
    std::array<char, 4096> buffer{};
    auto const fits = cbor::schema<garage>::encode(sample_garage, std::span(buffer));
    REQUIRE(fits.has_value());
    CHECK_EQ(std::string_view(buffer.data(), *fits), expected);
    std::vector<char> chars{'x'};
    CHECK_EQ(*cbor::schema<garage>::encode(sample_garage, chars), expected.size());
    CHECK_EQ(std::string_view(chars.data(), chars.size()), "x" + expected);
    std::vector<char> exact(expected.size());
    auto const fits_exactly = cbor::schema<garage>::encode(sample_garage, std::span(exact));
    REQUIRE(fits_exactly.has_value());
    CHECK_EQ(std::string_view(exact.data(), *fits_exactly), expected);
    std::array<char, 8> small{};
    auto const too_small = cbor::schema<garage>::encode(sample_garage, std::span(small));
    REQUIRE_FALSE(too_small.has_value());
    CHECK_EQ(too_small.error(), std::errc::no_buffer_space);

    struct counting {
        std::string sent;
        std::size_t finished = 0;
        std::size_t hint = 0;

        struct message {
            counting &to;
            std::expected<void, std::errc> append(std::string_view const part)
            {
                to.sent.append(part);
                return {};
            }
            std::expected<void, std::errc> done(std::size_t const size)
            {
                to.finished = size;
                return {};
            }
        };

        message allocate(std::size_t const n)
        {
            hint = n;
            return message{*this};
        }
    } writer;
    REQUIRE(cbor::schema<garage>::encode(sample_garage, writer).has_value());
    CHECK_EQ(writer.sent, expected);
    CHECK_EQ(writer.finished, expected.size());
    CHECK_EQ(writer.hint, expected.size());
}

namespace
{

struct [[=cbor::tag(1516)]] passkey_user {
    std::vector<std::byte> id;
    std::string name;
};

struct [[=cbor::tag(1517)]] passkey_login {
    std::vector<std::byte> signature;
    std::optional<passkey_user> user;
    std::optional<std::string> note;
};

} // namespace

// A store such as LMDB hands out exactly the bytes it was asked for, and the next record may sit right
// behind them. A writer whose allocate returns a span gets the message written straight into it, and no
// byte behind the span changes, also when the last item is a head with no content after it.
TEST_CASE("encode: into the exact span that a writer allocates")
{
    struct reserving {
        std::array<char, 4096> store{};
        std::size_t asked = 0;
        std::size_t finished = 0;

        struct message {
            reserving &to;
            explicit operator std::span<char>() const
            {
                return std::span(to.store).first(to.asked);
            }
            std::expected<void, std::errc> done(std::size_t const size)
            {
                to.finished = size;
                return {};
            }
        };

        message allocate(std::size_t const n)
        {
            asked = n;
            return message{*this};
        }
    };

    auto const check = [](auto const &value) {
        std::string const expected = *cbor::schema<std::remove_cvref_t<decltype(value)>>::encode(value);
        reserving writer;
        writer.store.fill('#');
        REQUIRE_EQ(*cbor::schema<std::remove_cvref_t<decltype(value)>>::encode(value, writer), expected.size());
        CHECK_EQ(writer.asked, expected.size());
        CHECK_EQ(writer.finished, expected.size());
        CHECK_EQ(std::string_view(writer.store.data(), expected.size()), expected);
        auto const behind = std::span(writer.store).subspan(expected.size());
        CHECK(std::ranges::all_of(behind, [](char const c) { return c == '#'; }));
    };
    check(sample_garage);
    check(passkey_login{{std::byte{1}, std::byte{2}}, std::nullopt, std::string{}});
    check(passkey_login{{}, passkey_user{{}, ""}, std::nullopt});
}

// CTAP 2.1 6.2.2 lets the user of a getAssertion response be absent. An optional struct lies in the
// fixed item as an array of a presence flag and its fields, so a path to it reads no reference. Both forms stay
// two well-formed CBOR items.
TEST_CASE("schema: an optional struct and an optional string, present and absent")
{
    passkey_login const full{{std::byte{1}, std::byte{2}}, passkey_user{{std::byte{9}}, "alice"}, "hello"};
    std::string const bytes = *cbor::schema<passkey_login>::encode(full);
    auto const doc = cbor::schema<passkey_login>::view(bytes);
    REQUIRE(doc.has_value());
    auto const user = cbor::schema<passkey_login>::at_path<".user">(*doc);
    REQUIRE(user.has_value());
    REQUIRE(user->has_value());
    auto const name = cbor::schema<passkey_login>::at_path<".name">(**user);
    REQUIRE(name.has_value());
    CHECK_EQ(*name, "alice"sv);
    auto const id = cbor::schema<passkey_login>::at_path<".id">(**user);
    REQUIRE(id.has_value());
    CHECK_EQ(id->size(), 1u);
    auto const note = cbor::schema<passkey_login>::at_path<".note">(*doc);
    REQUIRE(note.has_value());
    CHECK_EQ(note->value(), "hello"sv);
    auto const decoded_back = *cbor::schema<passkey_login>::decode(bytes);
    passkey_login const &back = *decoded_back;
    REQUIRE(back.user.has_value());
    CHECK_EQ(back.user->name, "alice");
    CHECK_EQ(back.note, std::optional<std::string>{"hello"});

    passkey_login const empty{{std::byte{1}}, std::nullopt, std::nullopt};
    std::string const none = *cbor::schema<passkey_login>::encode(empty);
    auto const doc2 = cbor::schema<passkey_login>::view(none);
    REQUIRE(doc2.has_value());
    auto const absent = cbor::schema<passkey_login>::at_path<".user">(*doc2);
    REQUIRE(absent.has_value());
    CHECK_FALSE(absent->has_value());
    for (std::string_view message : {std::string_view(bytes), std::string_view(none)}) {
        auto const end = cbor::doc_end<64>(message);
        REQUIRE(end.has_value());
        CHECK_EQ(*end, message.size());
    }
    CHECK_EQ(cbor::schema<wheel>::fixed_size<std::optional<passkey_user>>(), 2 + cbor::schema<passkey_user>::fixed_size());
    auto const decoded_back2 = *cbor::schema<passkey_login>::decode(none);
    passkey_login const &back2 = *decoded_back2;
    CHECK_FALSE(back2.user.has_value());
    CHECK_FALSE(back2.note.has_value());
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1619)]] chain19 {
    std::uint8_t v;
    std::string text;
    std::vector<std::string> words;
};

struct [[=cbor::tag(1618)]] chain18 {
    std::uint8_t v;
    chain19 next;
};

struct [[=cbor::tag(1617)]] chain17 {
    std::uint8_t v;
    chain18 next;
};

struct [[=cbor::tag(1616)]] chain16 {
    std::uint8_t v;
    chain17 next;
};

struct [[=cbor::tag(1615)]] chain15 {
    std::uint8_t v;
    chain16 next;
};

struct [[=cbor::tag(1614)]] chain14 {
    std::uint8_t v;
    chain15 next;
};

struct [[=cbor::tag(1613)]] chain13 {
    std::uint8_t v;
    chain14 next;
};

struct [[=cbor::tag(1612)]] chain12 {
    std::uint8_t v;
    chain13 next;
};

struct [[=cbor::tag(1611)]] chain11 {
    std::uint8_t v;
    chain12 next;
};

struct [[=cbor::tag(1610)]] chain10 {
    std::uint8_t v;
    chain11 next;
};

struct [[=cbor::tag(1609)]] chain9 {
    std::uint8_t v;
    chain10 next;
};

struct [[=cbor::tag(1608)]] chain8 {
    std::uint8_t v;
    chain9 next;
};

struct [[=cbor::tag(1607)]] chain7 {
    std::uint8_t v;
    chain8 next;
};

struct [[=cbor::tag(1606)]] chain6 {
    std::uint8_t v;
    chain7 next;
};

struct [[=cbor::tag(1605)]] chain5 {
    std::uint8_t v;
    chain6 next;
};

struct [[=cbor::tag(1604)]] chain4 {
    std::uint8_t v;
    chain5 next;
};

struct [[=cbor::tag(1603)]] chain3 {
    std::uint8_t v;
    chain4 next;
};

struct [[=cbor::tag(1602)]] chain2 {
    std::uint8_t v;
    chain3 next;
};

struct [[=cbor::tag(1601)]] chain1 {
    std::uint8_t v;
    chain2 next;
};

struct [[=cbor::tag(1600)]] chain0 {
    std::uint8_t v;
    chain1 next;
};

template <class C>
C chain_of(std::uint8_t const v)
{
    if constexpr (requires { C::next; })
        return C{v, chain_of<decltype(C::next)>(static_cast<std::uint8_t>(v + 1))};
    else
        return C{v, "ab", {"x", "yz"}};
}

} // namespace

// draft-ietf-cbor-packed-19 2.3 Table 2 gives tags 128..135 to the first 8 table indexes only. The test exists
// because a root with 20 struct types needs record functions at indexes 8 to 19, which a record reaches with the
// straight reference 6([N, rump]) at index 8 + N. chain19 is index 19, so N = 11 (0b): c6 82 0b, then the rump
// 9a 00 00 00 03. The directory takes index 20, so the shared items start at 21 and no f7 is left. Table 1 maps
// index 21 to 6(-3) = c6 3a 00 00 00 02 and index 22 to 6(3) = c6 1a 00 00 00 03. The table head counts 21 + 4
// entries. A record of index 8 or more is one byte longer, so its size depends on the root. The class tag of chainK
// is 1600 + K, 3 bytes in front of each record, and stands outside the reference: 1619 = d9 06 53 before c6 82 0b.
// A generic reader counts the class tag, the reference and the value array as three levels of nesting, so 20 nested
// records need more than a DepthMax of 64.
TEST_CASE("schema: a root with 20 struct types reaches indexes 8 and more with tag 6")
{
    CHECK_EQ(cbor::schema<chain0>::fixed_size<chain19>(), 3u + 3u + 5u + 2u + 6u + 6u);
    CHECK_EQ(cbor::schema<chain19>::fixed_size(), 3u + 2u + 5u + 2u + 6u + 6u);
    CHECK_EQ(cbor::schema<chain0>::fixed_size(), 8u * 12u + 11u * 13u + 25u);
    CHECK_EQ((cbor::schema<chain0>::member_offset<^^chain8::next, chain8>()), 13u);
    CHECK_EQ(cbor::schema<chain8>::member_offset<^^chain8::next>(), 12u);

    chain0 const value = chain_of<chain0>(0);
    std::string const bytes = *cbor::schema<chain0>::encode(value);
    CHECK_EQ(cbor::doc_end<64>(bytes).error(), error::nesting_depth_exceeded);
    CHECK_EQ(cbor::doc_end<128>(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x19"s);
    CHECK_EQ(bytes.substr(bytes.size() - 25),
             "\xd9\x06\x53\xc6\x82\x0b\x9a\x00\x00\x00\x03\x18\x13\xc6\x3a\x00\x00\x00\x02\xc6\x1a\x00\x00\x00\x03"s);
    CHECK_EQ(bytes.substr(bytes.size() - cbor::schema<chain0>::fixed_size() + 8u * 12u, 6), "\xd9\x06\x48\xc6\x82\x00"s);

    auto const decoded_back = *cbor::schema<chain0>::decode(bytes);
    chain0 const &back = *decoded_back;
    CHECK_EQ(*cbor::schema<chain0>::encode(back), bytes);
    CHECK_EQ(back.next.next.next.next.next.next.next.next.next.v, 9u);

    auto const doc = cbor::schema<chain0>::view(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(cbor::schema<chain0>::at_path<".next.next.next.next.next.next.next.next.next.v">(*doc), 9u);
    auto const inner = cbor::schema<chain0>::at_path<".next.next.next.next.next.next.next.next.next.next">(*doc);
    CHECK_EQ(cbor::schema<chain0>::at_path<".next.next.next.next.next.next.next.next.next.v">(inner), 19u);
    auto const text = cbor::schema<chain0>::at_path<".next.next.next.next.next.next.next.next.next.text">(inner);
    REQUIRE(text.has_value());
    CHECK_EQ(*text, "ab"sv);
    auto const words = cbor::schema<chain0>::at_path<".next.next.next.next.next.next.next.next.next.words">(inner);
    REQUIRE(words.has_value());
    auto const word = words->at(1);
    REQUIRE(word.has_value());
    CHECK_EQ(*word, "yz"sv);
}

// draft-ietf-cbor-packed-19 2.2 Table 1: with 17 struct types the directory is index 17 and the first shared item
// is index 18, which is 6(1) = c6 1a 00 00 00 01. The test exists because a reference below index 18 then names
// no shared item: 6(0) is index 16, a record function, and the reader refuses it.
TEST_CASE("schema: a root with 17 struct types starts its shared items at index 18")
{
    chain3 const value = chain_of<chain3>(3);
    std::string const bytes = *cbor::schema<chain3>::encode(value);
    CHECK_EQ(cbor::doc_end<64>(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x16"s);
    CHECK_EQ(bytes.substr(bytes.size() - 25),
             "\xd9\x06\x53\xc6\x82\x08\x9a\x00\x00\x00\x03\x18\x13\xc6\x1a\x00\x00\x00\x01\xc6\x3a\x00\x00\x00\x01"s);

    auto const decoded_back = *cbor::schema<chain3>::decode(bytes);
    chain3 const &back = *decoded_back;
    CHECK_EQ(*cbor::schema<chain3>::encode(back), bytes);
    auto const doc = cbor::schema<chain3>::view(bytes);
    REQUIRE(doc.has_value());
    auto const text = cbor::schema<chain3>::at_path<".next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(*doc);
    REQUIRE(text.has_value());
    CHECK_EQ(*text, "ab"sv);

    std::string broken = bytes;
    broken.at(broken.size() - 7) = '\x00';
    CHECK_EQ(cbor::schema<chain3>::decode(broken).error(), error::unpopulated_table_index);
    auto const view = cbor::schema<chain3>::view(broken);
    REQUIRE(view.has_value());
    CHECK_EQ(cbor::schema<chain3>::at_path<".next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(*view)
                 .error(),
             error::unpopulated_table_index);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1520)]] point_a {
    std::uint8_t x;
};

struct [[=cbor::tag(1521)]] point_b {
    std::uint8_t x;
};

} // namespace

// The tag of the class names the class on the wire. point_a and point_b have the same keys and the same layout,
// so only the class tag tells them apart: 1520 = d9 05 f0 against 1521 = d9 05 f1 (RFC 8949 3.4). The test exists
// because a reader that ignored the class tag would take a message of one class for the other.
TEST_CASE("schema: a root of another class tag is incorrect_type")
{
    std::string const bytes = *cbor::schema<point_a>::encode(point_a{7});
    CHECK_EQ(bytes.substr(bytes.size() - cbor::schema<point_a>::fixed_size(), 3), "\xd9\x05\xf0"s);
    auto const point = *cbor::schema<point_a>::decode(bytes);
    CHECK_EQ(point->x, 7u);
    CHECK_EQ(cbor::schema<point_b>::decode(bytes).error(), error::incorrect_type);
    CHECK_EQ(cbor::schema<point_b>::view(bytes).error(), error::incorrect_type);
}

// The class tag of a nested record is a fixed head like any other. car holds the engine record at offset 12, and
// the low byte of its tag 1501 = d9 05 dd is changed to de. The test exists because a forged nested tag must stop
// decode, and a path through that record must stop too, while the root and the other fields still read.
TEST_CASE("schema: a forged class tag of a nested record is incorrect_type")
{
    std::string const bytes = *cbor::schema<car>::encode(car{4, {300, 1800}, true});
    std::size_t const motor = bytes.size() - cbor::schema<car>::fixed_size() + cbor::schema<car>::member_offset<^^car::motor>();
    CHECK_EQ(bytes.substr(motor, 3), "\xd9\x05\xdd"s);
    std::string forged = bytes;
    forged.at(motor + 2) = '\xde';
    CHECK_EQ(cbor::schema<car>::decode(forged).error(), error::incorrect_type);
    auto const doc = cbor::schema<car>::view(forged);
    REQUIRE(doc.has_value());
    CHECK_EQ(*cbor::schema<car>::at_path<".seats">(*doc), 4u);
    CHECK_EQ(cbor::schema<car>::at_path<".motor.cc">(*doc).error(), error::incorrect_type);
    auto const good = cbor::schema<car>::view(bytes);
    REQUIRE(good.has_value());
    CHECK_EQ(*cbor::schema<car>::at_path<".motor.cc">(*good), 1800u);
}

namespace
{

struct [[=cbor::tag(1522)]] ticket {
    std::string_view holder;
    std::vector<std::string_view> seats;
    std::uint16_t row;
};

ticket const sample_ticket{"a holder name longer than the small string buffer", {"seat one of the long list", "seat two"}, 12};

std::string ticket_bytes()
{
    return *cbor::schema<ticket>::encode(sample_ticket);
}

} // namespace

// decode(bytes) copies the message once, and every view in the result points into that copy. The message here is a
// temporary that is destroyed at the end of the full expression. The test exists because a view into the bytes of
// the caller would read freed memory, and the address sanitizer reports that read.
TEST_CASE("decode: the views of the result outlive the bytes of the caller")
{
    cbor::result<cbor::oref<ticket>> const t = cbor::schema<ticket>::decode(ticket_bytes());
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->holder, sample_ticket.holder);
    REQUIRE_EQ((*t)->seats.size(), 2u);
    CHECK_EQ((*t)->seats.at(0), sample_ticket.seats.at(0));
    CHECK_EQ((*t)->seats.at(1), sample_ticket.seats.at(1));
    CHECK_EQ((*t)->row, 12u);
}

// decode(owner, bytes) copies nothing and keeps the owner. The caller releases its own pointer to the owner before
// it reads. The test exists because the result alone must keep the bytes alive.
TEST_CASE("decode: the result keeps the owner after the caller releases it")
{
    auto owner = std::make_shared<std::string const>(ticket_bytes());
    std::string_view const bytes = *owner;
    auto const t = cbor::schema<ticket>::decode(std::move(owner), bytes);
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->holder, sample_ticket.holder);
    REQUIRE_EQ((*t)->seats.size(), 2u);
    CHECK_EQ((*t)->seats.at(0), sample_ticket.seats.at(0));
    CHECK_EQ((*t)->seats.at(1), sample_ticket.seats.at(1));
}

// view(bytes) copies the message once, and the document holds that copy. A text from a path is a view onto it,
// with no allocation of its own. The test exists because the document alone must keep every text it gives valid,
// also after the caller's string is gone. A path on a temporary document does not compile.
TEST_CASE("at_path: a text result lives as long as its document")
{
    std::optional<cbor::document<ticket>> doc;
    {
        auto d = cbor::schema<ticket>::view(ticket_bytes());
        REQUIRE(d.has_value());
        doc = std::move(*d);
    }
    auto const holder = *cbor::schema<ticket>::at_path<".holder">(*doc);
    auto const seats = *cbor::schema<ticket>::at_path<".seats">(*doc);
    CHECK_EQ(holder, sample_ticket.holder);
    CHECK_EQ(*seats.at(1), sample_ticket.seats.at(1));
}

#endif
