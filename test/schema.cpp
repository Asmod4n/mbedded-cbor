#include "host.hpp"

#if __cpp_impl_reflection

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
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
    // A 128-bit integer is tag 2 or 3 (one byte) and a byte string of 16 bytes (RFC 8949 3.4.3).
    CHECK_EQ(cbor::fixed_size<__int128>(), 18u);
    CHECK_EQ(cbor::fixed_size<unsigned __int128>(), 18u);
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

// A struct is a map: its head, then for each member a text key and the fixed value. wheel: a3 head, "diameter"
// 9 + 3, "airPressure" 12 + 5, "snowTires" 10 + 1.
TEST_CASE("fixed_size: a struct is a map of its members")
{
    CHECK_EQ(cbor::fixed_size<wheel>(), 41u);
    CHECK_EQ(cbor::fixed_size<std::array<wheel, 4>>(), 1u + 4u * 41u);
}

// A part of variable size lives in the second item. The first item holds 82 1a <offset> 1a <length>.
TEST_CASE("fixed_size: a part of variable size takes 11 bytes")
{
    CHECK_EQ(cbor::fixed_size<std::string>(), 11u);
    CHECK_EQ(cbor::fixed_size<std::vector<wheel>>(), 11u);
    CHECK_EQ(cbor::fixed_size<std::map<int, int>>(), 11u);
    CHECK_EQ(cbor::fixed_size<std::optional<int>>(), 11u);
    CHECK_EQ(cbor::fixed_size<std::span<std::uint8_t>>(), 11u);
}

#endif

#if __cpp_impl_reflection

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

// The offset points at the head of the value, behind the key. wheel: a3, "diameter" 9 bytes, so 10; then 3 for
// its value and "airPressure" 12 bytes, so 25; then 5 and "snowTires" 10 bytes, so 40.
TEST_CASE("member_offset: the head of each value, behind its key")
{
    CHECK_EQ(cbor::member_offset<wheel, ^^wheel::diameter>(), 10u);
    CHECK_EQ(cbor::member_offset<wheel, ^^wheel::airPressure>(), 25u);
    CHECK_EQ(cbor::member_offset<wheel, ^^wheel::snowTires>(), 40u);
    CHECK_EQ(cbor::member_offset<wheel const, ^^wheel::snowTires>(), 40u);
}

// A nested struct is a map at the offset of its member, so offsets add up. car: a3 1, "seats" 6, its value 2,
// "motor" 6, so the engine map is at 15; inside it a2, "horsepower" 11, so 12.
TEST_CASE("member_offset: offsets of nested structs add up")
{
    CHECK_EQ(cbor::member_offset<car, ^^car::motor>(), 15u);
    CHECK_EQ(cbor::member_offset<engine, ^^engine::horsepower>(), 12u);
    CHECK_EQ(cbor::member_offset<car, ^^car::hasNavSystem>(), 15u + cbor::fixed_size<engine>() + 13u);
}

// RFC 8949 3: a map of 24 or more pairs and a text of 24 or more bytes need a head of two bytes.
TEST_CASE("member_offset: a long map head and a long key head")
{
    // b8 1e, then "m00" 4 bytes, so 6; each member takes 4 + 2.
    CHECK_EQ(cbor::member_offset<thirty, ^^thirty::m00>(), 6u);
    CHECK_EQ(cbor::member_offset<thirty, ^^thirty::m29>(), 2u + 29u * 6u + 4u);
    CHECK_EQ(cbor::fixed_size<thirty>(), 2u + 30u * 6u);
    // a2, then 78 1e and the 30 bytes of the name.
    CHECK_EQ(cbor::member_offset<long_name, ^^long_name::a_member_name_of_twenty_nine_b>(), 1u + 2u + 30u);
    CHECK_EQ(cbor::fixed_size<empty>(), 1u);
}

// A key is CBOR text, so it is UTF-8 (RFC 8949 3.1): "höhe" has 5 bytes, not 4.
TEST_CASE("member_offset: a key is counted in UTF-8 bytes")
{
    CHECK_EQ(cbor::member_offset<größe, ^^größe::höhe>(), 1u + 1u + 5u);
    CHECK_EQ(cbor::member_offset<größe, ^^größe::b>(), 7u + 2u + 2u);
}

#endif

#if __cpp_impl_reflection

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
    REQUIRE(cbor::encode(w, value).has_value());
    return w.bytes;
}

// The schema encoding is two CBOR items. Every byte is well-formed, so the generic decoder finds the end of each.
void check_two_items(std::string const &bytes)
{
    auto const first = cbor::doc_end<16>(bytes);
    REQUIRE(first.has_value());
    auto const second = cbor::doc_end<16>(std::string_view(bytes).substr(*first));
    REQUIRE(second.has_value());
    CHECK_EQ(*first + *second, bytes.size());
}

} // namespace

// Item 1 is a map with every number in the width of its type; a string is 82 1a <offset> 1a <length> into item 2.
// Item 2 is an array of one text; the offset counts from the start of the message (27 bytes of item 1, then 81 and
// the head 62) and points at the data. The bytes are written from RFC 8949 3 by hand.
TEST_CASE("encode: a struct with a number, a bool and a string")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK_EQ(bytes, "\xa3\x62id\x19\x00\x05\x62ok\xf5\x64name\x82\x1a\x00\x00\x00\x1d\x1a\x00\x00\x00\x02"
                    "\x81\x62"
                    "ab"s);
    check_two_items(bytes);
}

// A negative int16 is major type 1 with -1 - n in two bytes (RFC 8949 3.1). A float stays binary32 (fa). A list of
// uint16 is an array in item 2 whose elements keep their width. An empty optional has length 0 and puts no item
// into item 2; its offset is the end of item 2, so it still points forward.
TEST_CASE("encode: signed numbers, floats, a list and an empty optional")
{
    std::string const bytes = schema_bytes(measures{-5, 1.5f, {7, 8}, std::nullopt});
    CHECK_EQ(bytes, "\xa4\x61t\x39\x00\x04\x61\x66\xfa\x3f\xc0\x00\x00"
                    "\x61v\x82\x1a\x00\x00\x00\x29\x1a\x00\x00\x00\x02"
                    "\x61o\x82\x1a\x00\x00\x00\x2f\x1a\x00\x00\x00\x00"
                    "\x81\x82\x19\x00\x07\x19\x00\x08"s);
    check_two_items(bytes);
}

// The elements of a list are maps of fixed size, so element i is at i times that size. A string inside an element
// goes behind the whole block of elements, in the order the encoder meets it.
TEST_CASE("encode: a list of structs that hold strings")
{
    std::string const bytes = schema_bytes(people{{{"x"}, {"yz"}}});
    CHECK_EQ(bytes, "\xa1\x66people\x82\x1a\x00\x00\x00\x15\x1a\x00\x00\x00\x02"
                    "\x83\x82"
                    "\xa1\x61n\x82\x1a\x00\x00\x00\x32\x1a\x00\x00\x00\x01"
                    "\xa1\x61n\x82\x1a\x00\x00\x00\x34\x1a\x00\x00\x00\x02"
                    "\x61x\x62yz"s);
    check_two_items(bytes);
}

#endif

#if __cpp_impl_reflection

// decode checks one thing: the first item has the size that the schema gives it. A shorter message cannot hold
// the fixed fields, and a longer one is a newer sender or a second item.
TEST_CASE("decode: a struct checks only the size of the first item")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK(cbor::decode<login>(bytes).has_value());
    CHECK(cbor::decode<login>(std::string_view(bytes).substr(0, cbor::fixed_size<login>())).has_value());
    CHECK_EQ(cbor::decode<login>(std::string_view(bytes).substr(0, cbor::fixed_size<login>() - 1)).error(),
             error::too_little_data);
}

#endif

#if __cpp_impl_reflection

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
    auto const doc = cbor::decode<vehicle>(bytes);
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
    auto const doc = cbor::decode<vehicle>(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(*cbor::at_path_compiled<vehicle, ".make">(*doc), "Tesla"sv);
    CHECK_EQ(*cbor::at_path_compiled<vehicle, ".wheels[2].diameter">(*doc), 19u);
    CHECK_EQ(*cbor::at_path_compiled<vehicle, ".wheels[1].airPressure">(*doc), 3.0f);
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".wheels[3].diameter">(*doc).error(), error::index_out_of_bounds);
    CHECK_EQ(**cbor::at_path_compiled<vehicle, ".owner">(*doc), 9u);
    CHECK_FALSE(cbor::at_path_compiled<vehicle, ".none">(*doc)->has_value());
}

// An offset from the wire that points backward, or a length past the end, is refused and reads nothing.
TEST_CASE("at_path_compiled: a broken offset is an error")
{
    std::string bytes = schema_bytes(sample_vehicle);
    std::size_t const make = cbor::member_offset<vehicle, ^^vehicle::make>();
    std::string backward = bytes;
    backward.replace(make + 2, 4, "\x00\x00\x00\x01"s);
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".make">(*cbor::decode<vehicle>(backward)).error(), error::too_little_data);
    std::string past = bytes;
    past.replace(make + 7, 4, "\x00\x00\xff\xff"s);
    CHECK_EQ(cbor::at_path_compiled<vehicle, ".make">(*cbor::decode<vehicle>(past)).error(), error::too_little_data);
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
    auto const doc = cbor::decode<badge>(bytes);
    REQUIRE(doc.has_value());
    CHECK_EQ(cbor::at_path_compiled<badge, ".mac">(*doc), "\x01\x02\x03\x04"sv);
    CHECK_EQ(cbor::at_path_compiled<badge, ".tone">(*doc), shade::light);
    std::string zeros = bytes;
    std::size_t const mac = cbor::member_offset<badge, ^^badge::mac>();
    zeros.replace(mac + 1, 4, "\x00\x00\x00\x00"s);
    CHECK_EQ(cbor::at_path_compiled<badge, ".mac">(*cbor::decode<badge>(zeros)), "\x00\x00\x00\x00"sv);
}

#endif

#if __cpp_impl_reflection

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
    auto const doc = cbor::decode<garage>(bytes);
    REQUIRE(doc.has_value());
    auto const tires = cbor::at_path_compiled<garage, ".tires">(*doc);
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 3u);
    std::uint32_t sum = 0;
    for (auto const t : *tires)
        sum += cbor::at_path_compiled<tire, ".diameter">(*t);
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
    auto const doc = cbor::decode<garage>(bytes);
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
