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
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

enum class color : std::uint16_t { black, white };

struct wheel {
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
    CHECK_EQ(cbor::fixed_size<bool>(), 1u);
    CHECK_EQ(cbor::fixed_size<bool const>(), 1u);
    // An integer of n bytes is 0x18 + log2(n) followed by n bytes (RFC 8949 3).
    CHECK_EQ(cbor::fixed_size<std::uint8_t>(), 2u);
    CHECK_EQ(cbor::fixed_size<std::int16_t>(), 3u);
    CHECK_EQ(cbor::fixed_size<std::uint32_t>(), 5u);
    CHECK_EQ(cbor::fixed_size<std::int64_t>(), 9u);
    // An enum has the size of its underlying type.
    CHECK_EQ(cbor::fixed_size<color>(), 3u);
    CHECK_EQ(cbor::fixed_size<std::byte>(), 2u);
#ifdef __SIZEOF_INT128__
    // A 128-bit integer is tag 2 or 3 (one byte) and a byte string of 16 bytes (RFC 8949 3.4.3).
    CHECK_EQ(cbor::fixed_size<cbor::int128>(), 18u);
    CHECK_EQ(cbor::fixed_size<cbor::uint128>(), 18u);
#endif
}

// RFC 8949 3.3 has binary16, binary32 and binary64. binary128 is a typed array of one element under tag 83
// (RFC 8746 2.1): two bytes of tag, one byte of byte string head, 16 bytes. The 80-bit extended precision of
// x87 is widened to binary128, and bfloat16 to binary32, without loss.
TEST_CASE("fixed_size: floats by their digits")
{
    CHECK_EQ(cbor::fixed_size<std::float16_t>(), 3u);
    CHECK_EQ(cbor::fixed_size<std::bfloat16_t>(), 5u);
    CHECK_EQ(cbor::fixed_size<float>(), 5u);
    CHECK_EQ(cbor::fixed_size<double>(), 9u);
    CHECK_EQ(cbor::fixed_size<std::float128_t>(), 19u);
    CHECK_EQ(cbor::fixed_size<long double>(), std::numeric_limits<long double>::digits == 53 ? 9u : 19u);
}

// A text or a byte string of fixed length has its head and its bytes. An array of fixed length has its head and
// its elements, each of fixed size.
TEST_CASE("fixed_size: text, bytes and arrays of fixed length")
{
    CHECK_EQ(cbor::fixed_size<char[32]>(), 34u);
    CHECK_EQ(cbor::fixed_size<char8_t[3]>(), 4u);
    CHECK_EQ(cbor::fixed_size<std::array<char, 24>>(), 26u);
    CHECK_EQ(cbor::fixed_size<std::byte[4]>(), 5u);
    CHECK_EQ(cbor::fixed_size<unsigned char[300]>(), 303u);
    CHECK_EQ(cbor::fixed_size<std::array<std::uint16_t, 4>>(), 13u);
    // std::uint8_t is unsigned char, so a span of it is a byte string.
    CHECK_EQ(cbor::fixed_size<std::span<std::uint8_t, 2>>(), 3u);
    CHECK_EQ(cbor::fixed_size<std::span<std::uint16_t, 2>>(), 7u);
    CHECK_EQ(cbor::fixed_size<std::uint32_t[2]>(), 11u);
}

// draft-ietf-cbor-packed-19 4.2: a struct is a record, its values behind a straight reference to the table entry
// that holds its keys. The reference is d8 80 to d8 87, the value array has a head of 9a and 4 bytes, so 7 bytes
// come before the values. wheel: 7, then 3 + 5 + 1.
TEST_CASE("fixed_size: a struct is a record of its values")
{
    CHECK_EQ(cbor::fixed_size<wheel>(), 16u);
    CHECK_EQ(cbor::fixed_size<std::array<wheel, 4>>(), 1u + 4u * 16u);
}

// A part of variable size is a shared item in the table of tag 113. The record holds the reference c6 1a <N> or
// c6 3a <N>: tag 6 with an argument of fixed width (draft-ietf-cbor-packed-19 2.2), so 6 bytes.
TEST_CASE("fixed_size: a part of variable size takes 6 bytes")
{
    CHECK_EQ(cbor::fixed_size<std::string>(), 6u);
    CHECK_EQ(cbor::fixed_size<std::vector<wheel>>(), 6u);
    CHECK_EQ(cbor::fixed_size<std::map<int, int>>(), 6u);
    CHECK_EQ(cbor::fixed_size<std::optional<int>>(), 6u);
    CHECK_EQ(cbor::fixed_size<std::span<std::uint8_t>>(), 6u);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct engine {
    std::uint16_t horsepower;
    std::uint32_t cc;
};

struct car {
    std::uint8_t seats;
    engine motor;
    bool hasNavSystem;
};

struct empty {
};

struct thirty {
    std::uint8_t m00, m01, m02, m03, m04, m05, m06, m07, m08, m09, m10, m11, m12, m13, m14;
    std::uint8_t m15, m16, m17, m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29;
};

struct long_name {
    std::uint8_t a_member_name_of_twenty_nine_b;
    std::uint8_t b;
};

struct größe {
    std::uint8_t höhe;
    std::uint8_t b;
};

} // namespace

// The offset points at the head of a value. The keys are in the table, so 7 bytes of the record come first. wheel:
// diameter at 7, airPressure behind its 3 bytes at 10, snowTires behind its 5 bytes at 15.
TEST_CASE("member_offset: the head of each value, behind the head of the record")
{
    CHECK_EQ(cbor::member_offset<wheel, ^^wheel::diameter>(), 7u);
    CHECK_EQ(cbor::member_offset<wheel, ^^wheel::airPressure>(), 10u);
    CHECK_EQ(cbor::member_offset<wheel, ^^wheel::snowTires>(), 15u);
    CHECK_EQ(cbor::member_offset<wheel const, ^^wheel::snowTires>(), 15u);
}

// A nested struct is a record at the offset of its member, so offsets add up. car: 7, seats 2, so the engine record
// is at 9; inside it horsepower at 7.
TEST_CASE("member_offset: offsets of nested structs add up")
{
    CHECK_EQ(cbor::member_offset<car, ^^car::motor>(), 9u);
    CHECK_EQ(cbor::member_offset<engine, ^^engine::horsepower>(), 7u);
    CHECK_EQ(cbor::member_offset<car, ^^car::hasNavSystem>(), 9u + cbor::fixed_size<engine>());
}

// The count of values has a head of 9a and 4 bytes whatever the count, so a member added later moves no offset, and
// a long name or a name in UTF-8 takes no place in the record.
TEST_CASE("member_offset: the record head has one width for any count and any name")
{
    CHECK_EQ(cbor::member_offset<thirty, ^^thirty::m00>(), 7u);
    CHECK_EQ(cbor::member_offset<thirty, ^^thirty::m29>(), 7u + 29u * 2u);
    CHECK_EQ(cbor::fixed_size<thirty>(), 7u + 30u * 2u);
    CHECK_EQ(cbor::member_offset<long_name, ^^long_name::a_member_name_of_twenty_nine_b>(), 7u);
    CHECK_EQ(cbor::fixed_size<empty>(), 7u);
    CHECK_EQ(cbor::member_offset<größe, ^^größe::b>(), 9u);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct login {
    std::uint16_t id;
    bool ok;
    std::string name;
};

struct measures {
    std::int16_t t;
    float f;
    std::vector<std::uint16_t> v;
    std::optional<std::uint8_t> o;
};

struct named {
    std::string n;
};

struct people {
    std::vector<named> people;
};

std::string schema_bytes(auto const &value)
{
    string_writer w;
    REQUIRE(cbor::encode(value, w).has_value());
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
// is the record d8 80 with the count 9a 00 00 00 03, every number in the width of its type, and the string as
// 6(0) = c6 1a 00 00 00 00, which 2.2 resolves to index 16. The table head counts 17 entries. The string starts at
// byte 45 (2d). The bytes are written from the draft and RFC 8949 3 by hand.
TEST_CASE("encode: a struct with a number, a bool and a string")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x11\xd8\x72\x83\x62id\x62ok\x64name"
                    "\x5a\x00\x00\x00\x04\x00\x00\x00\x2d"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x7a\x00\x00\x00\x02"
                    "ab"
                    "\xd8\x80\x9a\x00\x00\x00\x03\x19\x00\x05\xf5\xc6\x1a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// A negative int16 is major type 1 with -1 - n in two bytes (RFC 8949 3.1). A float stays binary32 (fa). The list
// of uint16 is shared item 16 and keeps the width of its elements. The empty optional is shared item 17, an empty
// array. Item 17 is 6(-1) = c6 3a 00 00 00 00, because 2.2 maps a negative N to index 16 - 2N - 1. The items start
// at 46 (2e) and 57 (39).
TEST_CASE("encode: signed numbers, floats, a list and an empty optional")
{
    std::string const bytes = schema_bytes(measures{-5, 1.5f, {7, 8}, std::nullopt});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x12\xd8\x72\x84\x61t\x61\x66\x61v\x61o"
                    "\x5a\x00\x00\x00\x08\x00\x00\x00\x2e\x00\x00\x00\x39"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x9a\x00\x00\x00\x02\x19\x00\x07\x19\x00\x08"
                    "\x9a\x00\x00\x00\x00"
                    "\xd8\x80\x9a\x00\x00\x00\x04\x39\x00\x04\xfa\x3f\xc0\x00\x00"
                    "\xc6\x1a\x00\x00\x00\x00\xc6\x3a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// Two record functions take index 0 and 1, the directory index 2. The list is item 16; its elements are records
// of fixed size with d8 81 for the second type. The strings of the elements follow the list as items 17 and 18, in
// the order the encoder meets them: 6(-1) and 6(1). The items start at 53 (35), 84 (54) and 90 (5a).
TEST_CASE("encode: a list of structs that hold strings")
{
    std::string const bytes = schema_bytes(people{{{"x"}, {"yz"}}});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x13\xd8\x72\x81\x66people\xd8\x72\x81\x61n"
                    "\x5a\x00\x00\x00\x0c\x00\x00\x00\x35\x00\x00\x00\x54\x00\x00\x00\x5a"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x9a\x00\x00\x00\x02"
                    "\xd8\x81\x9a\x00\x00\x00\x01\xc6\x3a\x00\x00\x00\x00"
                    "\xd8\x81\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x01"
                    "\x7a\x00\x00\x00\x01x\x7a\x00\x00\x00\x02yz"
                    "\xd8\x80\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

#endif

#ifdef __cpp_impl_reflection

// view checks that the keys are the keys of the type, byte for byte, and that the message is at least as long as
// the schema needs. A table of other keys is another type.
TEST_CASE("view: a struct checks its keys and the least size of the message")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK(cbor::view<login>(bytes).has_value());
    CHECK_FALSE(cbor::view<login>(std::string_view(bytes).substr(0, 20)).has_value());
    std::string other = bytes;
    other.replace(8, 2, "ID");
    CHECK_EQ(cbor::view<login>(other).error(), error::incorrect_type);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct tire {
    std::uint16_t diameter;
    float airPressure;
};

struct vehicle {
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
TEST_CASE("at_path_compiled: fixed fields give the value")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    auto const doc = cbor::view<vehicle>(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".balance">(*doc), -7);
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".motor.cc">(*doc), 1800u);
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".spare[1].diameter">(*doc), 16u);
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".spare[0].airPressure">(*doc), 2.5f);
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".code">(*doc), "ABCD"sv);
    auto const motor = cbor::at_path_compiled<vehicle, ".motor">(*doc);
    CHECK_EQ(cbor::at_path_compiled<engine, ".horsepower">(motor), 300u);
}

// A step over a part of variable size reads an offset and a length from the wire, so the result is an expected.
TEST_CASE("at_path_compiled: parts of variable size give an expected")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    auto const doc = cbor::view<vehicle>(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(*cbor::at_path_compiled<vehicle, ".make">(*doc), "Tesla"sv);
    CHECK_EQ(*cbor::at_path_compiled<vehicle, ".wheels[2].diameter">(*doc), 19u);
    CHECK_EQ(*cbor::at_path_compiled<vehicle, ".wheels[1].airPressure">(*doc), 3.0f);
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".wheels[3].diameter">(*doc).error(), error::index_out_of_bounds);
    CHECK_EQ(**cbor::at_path_compiled<vehicle, ".owner">(*doc), 9u);
    CHECK_FALSE(cbor::at_path_compiled<vehicle, ".none">(*doc)->has_value());
}

// A directory offset from the wire that points at bytes of another kind, or a length past the start of the next
// item, is refused and reads nothing. The directory of vehicle starts behind the prefix and its head 5a.
TEST_CASE("at_path_compiled: a broken offset is an error")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    std::size_t const head = bytes.find("\x5a"s);
    REQUIRE_NE(head, std::string::npos);
    std::string backward = bytes;
    backward.replace(head + 5, 4, "\x00\x00\x00\x01"s);
    auto const a = cbor::view<vehicle>(backward);
    CHECK((!a || !cbor::at_path_compiled<vehicle, ".make">(*a)));
    std::size_t const make = bytes.find("\x7a\x00\x00\x00\x05Tesla"s);
    REQUIRE_NE(make, std::string::npos);
    std::string past = bytes;
    past.replace(make + 1, 4, "\x00\x00\xff\xff"s);
    auto const p = cbor::view<vehicle>(past);
    CHECK((!p || !cbor::at_path_compiled<vehicle, ".make">(*p)));
    CHECK_FALSE(cbor::decode<vehicle>(backward).has_value());
    CHECK_FALSE(cbor::decode<vehicle>(past).has_value());
}

namespace
{

enum class shade : std::uint8_t { dark, light };

struct badge {
    std::array<std::byte, 4> mac;
    shade tone;
    std::string label;
};

} // namespace

// A fixed-length array of bytes is a byte string laid out inline (RFC 8949 3.1), the same as a fixed text.
// The reader gives the bytes themselves, not an offset and a length read out of them.
TEST_CASE("at_path_compiled: a fixed byte array is read inline")
{
    badge const b{{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}, shade::light, "x"};
    std::string const bytes = schema_bytes(b);
    auto const doc = cbor::view<badge>(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(cbor::at_path_compiled<badge, ".mac">(*doc), "\x01\x02\x03\x04"sv);
    CHECK_EQ(cbor::at_path_compiled<badge, ".tone">(*doc), shade::light);
    std::string zeros = bytes;
    std::size_t const mac =
        static_cast<std::size_t>(doc->field.data() - bytes.data()) + cbor::member_offset<badge, ^^badge::mac>();
    zeros.replace(mac + 1, 4, "\x00\x00\x00\x00"s);
    CHECK_EQ(cbor::at_path_compiled<badge, ".mac">(*cbor::view<badge>(zeros)), "\x00\x00\x00\x00"sv);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct garage {
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
    auto const doc = cbor::view<garage>(bytes);
    REQUIRE(doc.has_value());
    auto const tires = cbor::at_path_compiled<garage, ".tires">(*doc);
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 3u);
    std::uint32_t sum = 0;
    for (auto const t : *tires)
        sum += *cbor::at_path_compiled<tire, ".diameter">(*t);
    CHECK_EQ(sum, 17u + 18u + 19u);
    CHECK_EQ(cbor::at_path_compiled<tire, ".airPressure">(*tires->at(1)), 3.0f);
    CHECK_EQ(tires->at(3).error(), error::index_out_of_bounds);

    auto const names = cbor::at_path_compiled<garage, ".names">(*doc);
    REQUIRE(names.has_value());
    CHECK_EQ(*names->at(1), "bc"sv);

    auto const rows = cbor::at_path_compiled<garage, ".rows">(*doc);
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
    auto const doc = cbor::view<garage>(bytes);
    REQUIRE(doc.has_value());
    auto const owners = cbor::at_path_compiled<garage, ".owners">(*doc);
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
    std::string const bytes = *cbor::encode(sample_garage);
    garage const back = *cbor::decode<garage>(bytes);
    REQUIRE_EQ(back.tires.size(), 3u);
    CHECK_EQ(back.tires.at(2).diameter, 19u);
    CHECK_EQ(back.tires.at(1).airPressure, 3.0f);
    CHECK_EQ(back.names, sample_garage.names);
    CHECK_EQ(back.rows, sample_garage.rows);
    CHECK_EQ(back.owners, sample_garage.owners);

    std::string const encoded = *cbor::encode(sample_vehicle);
    vehicle const v = *cbor::decode<vehicle>(encoded);
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
    std::string const bytes = *cbor::encode(sample_vehicle);
    std::string_view const cut = std::string_view(bytes).substr(0, 3);
    auto const r = cbor::decode<vehicle>(cut);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::too_little_data);
}

namespace
{

struct node {
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
// node at depth k: one node that refers to item k + 1, and the last item is empty. The encoder of this library
// cannot write it, because gcc does not inline a recursive encoder.
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
            out += "\xd8\x80\x9a\x00\x00\x00\x01"s;
            reference_append(out, k + 1);
        }
    }
    out += "\xd8\x80\x9a\x00\x00\x00\x01"s;
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

struct keyed {
    [[=cbor::key("x-user-id")]] std::uint8_t m0;
    [[=cbor::key("EOF")]] bool m1;
};

struct guarded {
    std::uint8_t id;
    [[=cbor::skip{}]] std::uint8_t secret;
};

} // namespace

// cbor::skip holds in the schema form too: the member has no key in the table and takes no place in the record.
TEST_CASE("schema: cbor::skip leaves a public member out")
{
    CHECK_EQ(cbor::fixed_size<guarded>(), 9u);
    std::string const bytes = *cbor::encode(guarded{7, 42});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x10\xd8\x72\x81\x62id\x5a\x00\x00\x00\x00"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xd8\x80\x9a\x00\x00\x00\x01\x18\x07"s);
    guarded const back = *cbor::decode<guarded>(bytes);
    CHECK_EQ(back.id, 7u);
    CHECK_EQ(back.secret, 0u);
}

// The key of an annotation takes the place of the member name in the table. The expectation is counted from the
// draft and RFC 8949 3 by hand: the keys with the text heads 69 and 63, an empty directory, f7 up to index 15,
// then the record: 7 bytes of head, the integer with its fixed width of 18 and 1 byte, and the simple value.
TEST_CASE("schema: an annotation gives the key of a member")
{
    CHECK_EQ(cbor::fixed_size<keyed>(), 10u);
    CHECK_EQ(cbor::member_offset<keyed, ^^keyed::m0>(), 7u);
    CHECK_EQ(cbor::member_offset<keyed, ^^keyed::m1>(), 9u);
    std::string const bytes = *cbor::encode(keyed{7, true});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x10\xd8\x72\x82\x69x-user-id\x63"
                    "EOF\x5a\x00\x00\x00\x00"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xd8\x80\x9a\x00\x00\x00\x02\x18\x07\xf5"s);
    keyed const back = *cbor::decode<keyed>(bytes);
    CHECK_EQ(back.m0, 7u);
    CHECK(back.m1);
    auto const doc = cbor::view<keyed>(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(cbor::at_path_compiled<keyed, ".EOF">(*doc), true);
}

// A struct that holds a list of itself is read by recursion, and the bytes decide how deep. Without a limit, a
// chain of 100000 nodes in 2.2 MB overflowed a stack of 8 MiB. Each reference that is followed counts one
// level, as each nested item counts one in the other decoders.
TEST_CASE("decode: a struct that holds itself stops at DepthMax")
{
    REQUIRE_EQ(cbor::fixed_size<node>(), 13u);

    auto const at_limit = cbor::decode<node>(node_chain(64));
    REQUIRE(at_limit.has_value());
    CHECK_EQ(depth_of(*at_limit), 64u);

    auto const over = cbor::decode<node>(node_chain(65));
    REQUIRE_FALSE(over.has_value());
    CHECK_EQ(over.error(), error::nesting_depth_exceeded);

    auto const small = cbor::decode<node, 3>(node_chain(3));
    REQUIRE(small.has_value());
    CHECK_EQ(depth_of(*small), 3u);
    CHECK_EQ(cbor::decode<node, 3>(node_chain(4)).error(), error::nesting_depth_exceeded);

    auto const deep = cbor::decode<node>(node_chain(100000));
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
    std::string const expected = *cbor::encode(sample_garage);
    std::string text = "x";
    CHECK_EQ(*cbor::encode(sample_garage, text), expected.size());
    CHECK_EQ(text, "x" + expected);
    std::vector<std::byte> bytes;
    REQUIRE(cbor::encode(sample_garage, bytes).has_value());
    CHECK_EQ(bytes.size(), expected.size());
    std::array<char, 4096> buffer{};
    auto const fits = cbor::encode(sample_garage, std::span(buffer));
    REQUIRE(fits.has_value());
    CHECK_EQ(std::string_view(buffer.data(), *fits), expected);
    std::vector<char> chars{'x'};
    CHECK_EQ(*cbor::encode(sample_garage, chars), expected.size());
    CHECK_EQ(std::string_view(chars.data(), chars.size()), "x" + expected);
    std::vector<char> exact(expected.size());
    auto const fits_exactly = cbor::encode(sample_garage, std::span(exact));
    REQUIRE(fits_exactly.has_value());
    CHECK_EQ(std::string_view(exact.data(), *fits_exactly), expected);
    std::array<char, 8> small{};
    auto const too_small = cbor::encode(sample_garage, std::span(small));
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
    REQUIRE(cbor::encode(sample_garage, writer).has_value());
    CHECK_EQ(writer.sent, expected);
    CHECK_EQ(writer.finished, expected.size());
    CHECK_EQ(writer.hint, expected.size());
}

namespace
{

struct passkey_user {
    std::vector<std::byte> id;
    std::string name;
};

struct passkey_login {
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
        std::string const expected = *cbor::encode(value);
        reserving writer;
        writer.store.fill('#');
        REQUIRE_EQ(*cbor::encode(value, writer), expected.size());
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
    std::string const bytes = *cbor::encode(full);
    auto const doc = cbor::view<passkey_login>(bytes);
    REQUIRE(doc.has_value());
    auto const user = cbor::at_path_compiled<passkey_login, ".user">(*doc);
    REQUIRE(user.has_value());
    REQUIRE(user->has_value());
    CHECK_EQ(*cbor::at_path_compiled<passkey_user, ".name">(**user), "alice"sv);
    CHECK_EQ(cbor::at_path_compiled<passkey_user, ".id">(**user)->size(), 1u);
    auto const note = cbor::at_path_compiled<passkey_login, ".note">(*doc);
    REQUIRE(note.has_value());
    CHECK_EQ(note->value(), "hello"sv);
    passkey_login const back = *cbor::decode<passkey_login>(bytes);
    REQUIRE(back.user.has_value());
    CHECK_EQ(back.user->name, "alice");
    CHECK_EQ(back.note, std::optional<std::string>{"hello"});

    passkey_login const empty{{std::byte{1}}, std::nullopt, std::nullopt};
    std::string const none = *cbor::encode(empty);
    auto const doc2 = cbor::view<passkey_login>(none);
    REQUIRE(doc2.has_value());
    auto const absent = cbor::at_path_compiled<passkey_login, ".user">(*doc2);
    REQUIRE(absent.has_value());
    CHECK_FALSE(absent->has_value());
    for (std::string_view message : {std::string_view(bytes), std::string_view(none)}) {
        auto const end = cbor::doc_end<64>(message);
        REQUIRE(end.has_value());
        CHECK_EQ(*end, message.size());
    }
    CHECK_EQ(cbor::fixed_size<std::optional<passkey_user>>(), 2 + cbor::fixed_size<passkey_user>());
    passkey_login const back2 = *cbor::decode<passkey_login>(none);
    CHECK_FALSE(back2.user.has_value());
    CHECK_FALSE(back2.note.has_value());
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct chain19 {
    std::uint8_t v;
    std::string text;
    std::vector<std::string> words;
};

struct chain18 {
    std::uint8_t v;
    chain19 next;
};

struct chain17 {
    std::uint8_t v;
    chain18 next;
};

struct chain16 {
    std::uint8_t v;
    chain17 next;
};

struct chain15 {
    std::uint8_t v;
    chain16 next;
};

struct chain14 {
    std::uint8_t v;
    chain15 next;
};

struct chain13 {
    std::uint8_t v;
    chain14 next;
};

struct chain12 {
    std::uint8_t v;
    chain13 next;
};

struct chain11 {
    std::uint8_t v;
    chain12 next;
};

struct chain10 {
    std::uint8_t v;
    chain11 next;
};

struct chain9 {
    std::uint8_t v;
    chain10 next;
};

struct chain8 {
    std::uint8_t v;
    chain9 next;
};

struct chain7 {
    std::uint8_t v;
    chain8 next;
};

struct chain6 {
    std::uint8_t v;
    chain7 next;
};

struct chain5 {
    std::uint8_t v;
    chain6 next;
};

struct chain4 {
    std::uint8_t v;
    chain5 next;
};

struct chain3 {
    std::uint8_t v;
    chain4 next;
};

struct chain2 {
    std::uint8_t v;
    chain3 next;
};

struct chain1 {
    std::uint8_t v;
    chain2 next;
};

struct chain0 {
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
// entries. A record of index 8 or more is one byte longer, so its size depends on the root.
TEST_CASE("schema: a root with 20 struct types reaches indexes 8 and more with tag 6")
{
    CHECK_EQ(cbor::fixed_size<chain19, chain0>(), 3u + 5u + 2u + 6u + 6u);
    CHECK_EQ(cbor::fixed_size<chain19>(), 2u + 5u + 2u + 6u + 6u);
    CHECK_EQ(cbor::fixed_size<chain0>(), 8u * 9u + 11u * 10u + 22u);
    CHECK_EQ((cbor::member_offset<chain8, ^^chain8::next, chain0>()), 10u);
    CHECK_EQ(cbor::member_offset<chain8, ^^chain8::next>(), 9u);

    chain0 const value = chain_of<chain0>(0);
    std::string const bytes = *cbor::encode(value);
    CHECK_EQ(cbor::doc_end<64>(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x19"s);
    CHECK_EQ(bytes.substr(bytes.size() - 22),
             "\xc6\x82\x0b\x9a\x00\x00\x00\x03\x18\x13\xc6\x3a\x00\x00\x00\x02\xc6\x1a\x00\x00\x00\x03"s);
    CHECK_EQ(bytes.substr(bytes.size() - cbor::fixed_size<chain0>() + 8u * 9u, 3), "\xc6\x82\x00"s);

    chain0 const back = *cbor::decode<chain0>(bytes);
    CHECK_EQ(*cbor::encode(back), bytes);
    CHECK_EQ(back.next.next.next.next.next.next.next.next.next.v, 9u);

    auto const doc = cbor::view<chain0>(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(cbor::at_path_compiled<chain0, ".next.next.next.next.next.next.next.next.next.v">(*doc), 9u);
    auto const inner = cbor::at_path_compiled<chain0, ".next.next.next.next.next.next.next.next.next.next">(*doc);
    CHECK_EQ(cbor::at_path_compiled<chain10, ".next.next.next.next.next.next.next.next.next.v">(inner), 19u);
    CHECK_EQ(*cbor::at_path_compiled<chain10, ".next.next.next.next.next.next.next.next.next.text">(inner), "ab"sv);
    auto const words = cbor::at_path_compiled<chain10, ".next.next.next.next.next.next.next.next.next.words">(inner);
    REQUIRE(words.has_value());
    CHECK_EQ(*words->at(1), "yz"sv);
}

// draft-ietf-cbor-packed-19 2.2 Table 1: with 17 struct types the directory is index 17 and the first shared item
// is index 18, which is 6(1) = c6 1a 00 00 00 01. The test exists because a reference below index 18 then names
// no shared item: 6(0) is index 16, a record function, and the reader refuses it.
TEST_CASE("schema: a root with 17 struct types starts its shared items at index 18")
{
    chain3 const value = chain_of<chain3>(3);
    std::string const bytes = *cbor::encode(value);
    CHECK_EQ(cbor::doc_end<64>(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x16"s);
    CHECK_EQ(bytes.substr(bytes.size() - 22),
             "\xc6\x82\x08\x9a\x00\x00\x00\x03\x18\x13\xc6\x1a\x00\x00\x00\x01\xc6\x3a\x00\x00\x00\x01"s);

    chain3 const back = *cbor::decode<chain3>(bytes);
    CHECK_EQ(*cbor::encode(back), bytes);
    auto const doc = cbor::view<chain3>(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(*cbor::at_path_compiled<chain3, ".next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(*doc),
             "ab"sv);

    std::string broken = bytes;
    broken.at(broken.size() - 7) = '\x00';
    CHECK_EQ(cbor::decode<chain3>(broken).error(), error::unpopulated_table_index);
    auto const view = cbor::view<chain3>(broken);
    REQUIRE(view.has_value());
    CHECK_EQ(cbor::at_path_compiled<chain3, ".next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(*view)
                 .error(),
             error::unpopulated_table_index);
}

#endif
