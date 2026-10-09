#include "binding.hpp"

#ifdef __cpp_impl_reflection

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <stdfloat>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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
    return w.encoded;
}

// The tests of one path open the message with the copying form of path and read one leaf. A text is copied into a
// std::string, because the copy that path made ends with this function.
template <class T, cbor::fixed_string Path, class... Index>
auto at_path(std::string_view const bytes, Index const... indexes)
{
    using X = std::remove_cvref_t<decltype(*std::declval<typename cbor::schema<T>::template accessor<> const &>().template at_path<Path>(indexes...))>;
    using Y = std::conditional_t<std::same_as<X, std::string_view>, std::string,
                                 std::conditional_t<std::same_as<X, std::optional<std::string_view>>, std::optional<std::string>, X>>;
    auto const root = cbor::schema<T>::path(bytes);
    if (!root)
        return std::expected<Y, cbor::error>(std::unexpect, root.error());
    auto const x = root->template at_path<Path>(indexes...);
    if (!x)
        return std::expected<Y, cbor::error>(std::unexpect, x.error());
    if constexpr (std::same_as<X, std::optional<std::string_view>>)
        return x->has_value() ? std::expected<Y, cbor::error>(Y(std::in_place, **x)) : std::expected<Y, cbor::error>(Y{});
    else
        return std::expected<Y, cbor::error>(Y(*x));
}

// The owner form: the caller keeps the owner alive, so a text stays a view into the message.
template <class T, cbor::fixed_string Path, class... Index>
auto at_path(std::shared_ptr<void const> const &owner, std::string_view const bytes, Index const... indexes)
    -> decltype(std::declval<typename cbor::schema<T>::template accessor<> const &>().template at_path<Path>(indexes...))
{
    auto const root = cbor::schema<T>::path(owner, bytes);
    if (!root)
        return std::unexpected(root.error());
    return root->template at_path<Path>(indexes...);
}

// The schema encoding is one CBOR item, tag 113 over the table and the two parts. Every byte is well-formed, so
// the generic decoder finds its end at the end of the message.
void check_one_item(std::string const &bytes)
{
    auto const end = cbor::item_size(bytes);
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
// of uint16 is shared item 16, a typed array of RFC 8746: tag 69 (uint16, little endian) = d8 45 over a byte string
// of four bytes. The empty optional is shared item 17, an empty
// array. Item 17 is 6(-1) = c6 3a 00 00 00 00, because 2.2 maps a negative N to index 16 - 2N - 1. The items start
// at 46 (2e) and 57 (39). The rump starts with the class tag 1508 = d9 05 e4.
TEST_CASE("encode: signed numbers, floats, a list and an empty optional")
{
    std::string const bytes = schema_bytes(measures{-5, 1.5f, {7, 8}, std::nullopt});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x12\xd8\x72\x84\x61t\x61\x66\x61v\x61o"
                    "\x5a\x00\x00\x00\x08\x00\x00\x00\x2e\x00\x00\x00\x39"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xd8\x45\x5a\x00\x00\x00\x04\x07\x00\x08\x00"
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

// path checks the least size of the message and the table of keys once, as decode does. The test exists so that a
// change to either side shows here.
TEST_CASE("path: a message shorter than the least size is too_little_data, a foreign key is incorrect_type")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK_EQ(at_path<login, "$.id">(bytes), 5u);
    CHECK_EQ(at_path<login, "$.id">(std::string_view(bytes).substr(0, 20)).error(), error::too_little_data);
    std::string other = bytes;
    other.replace(8, 2, "ID");
    CHECK_EQ(cbor::schema<login>::decode(other).error(), error::incorrect_type);
    CHECK_EQ(at_path<login, "$.id">(other).error(), error::incorrect_type);
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

// A path over fixed fields becomes one offset at compile time. A text is copied into a std::string, so it cannot
// outlive the bytes it came from.
TEST_CASE("at_path: fixed fields give the value")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    CHECK_EQ(at_path<vehicle, "$.balance">(bytes), -7);
    CHECK_EQ(at_path<vehicle, "$.motor.cc">(bytes), 1800u);
    CHECK_EQ(at_path<vehicle, "$.motor.horsepower">(bytes), 300u);
    CHECK_EQ(at_path<vehicle, "$.spare[1].diameter">(bytes), 16u);
    CHECK_EQ(at_path<vehicle, "$.spare[0].airPressure">(bytes), 2.5f);
    CHECK_EQ(at_path<vehicle, "$.spare[].diameter">(bytes, 1uz), 16u);
    CHECK_EQ(at_path<vehicle, "$.spare[].diameter">(bytes, 2uz).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<vehicle, "$.code">(bytes), "ABCD"s);
}

// A step over a part of variable size reads an offset and a length from the wire. An empty pair of brackets takes
// its index from the arguments, in the order of the path.
TEST_CASE("at_path: parts of variable size")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    CHECK_EQ(at_path<vehicle, "$.make">(bytes), "Tesla"s);
    CHECK_EQ(at_path<vehicle, "$.wheels[2].diameter">(bytes), 19u);
    CHECK_EQ(at_path<vehicle, "$.wheels[1].airPressure">(bytes), 3.0f);
    CHECK_EQ(at_path<vehicle, "$.wheels[].airPressure">(bytes, 1uz), 3.0f);
    CHECK_EQ(at_path<vehicle, "$.wheels[3].diameter">(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<vehicle, "$.wheels[].diameter">(bytes, 3uz).error(), error::index_out_of_bounds);
    CHECK_EQ(*at_path<vehicle, "$.owner">(bytes), std::optional<std::uint8_t>{9});
    CHECK_FALSE(at_path<vehicle, "$.none">(bytes)->has_value());
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
    CHECK_FALSE(at_path<vehicle, "$.make">(backward).has_value());
    std::size_t const make = bytes.find("\x7a\x00\x00\x00\x05Tesla"s);
    REQUIRE_NE(make, std::string::npos);
    std::string past = bytes;
    past.replace(make + 1, 4, "\x00\x00\xff\xff"s);
    CHECK_FALSE(at_path<vehicle, "$.make">(past).has_value());
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
    CHECK_EQ(at_path<badge, "$.mac">(bytes), "\x01\x02\x03\x04"s);
    CHECK_EQ(at_path<badge, "$.tone">(bytes), shade::light);
    std::string zeros = bytes;
    std::size_t const mac = bytes.size() - cbor::schema<badge>::fixed_size() + cbor::schema<badge>::member_offset<^^badge::mac>();
    zeros.replace(mac + 1, 4, "\x00\x00\x00\x00"s);
    CHECK_EQ(at_path<badge, "$.mac">(zeros), "\x00\x00\x00\x00"s);
}

#endif

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1530)]] numbers {
    std::vector<std::uint16_t> u16;
    std::vector<std::uint32_t> u32;
    std::vector<std::uint64_t> u64;
    std::vector<std::int8_t> s8;
    std::vector<std::int16_t> s16;
    std::vector<std::int32_t> s32;
    std::vector<std::int64_t> s64;
#if defined(__STDCPP_FLOAT16_T__)
    std::vector<std::float16_t> f16;
#endif
    std::vector<float> f32;
    std::vector<double> f64;
};

numbers const sample_numbers{{0x0102},
                             {0x01020304},
                             {0x0102030405060708},
                             {-2},
                             {-2},
                             {-2},
                             {-2},
#if defined(__STDCPP_FLOAT16_T__)
                             {1.0f16},
#endif
                             {1.0f},
                             {1.0}};

struct [[=cbor::tag(1531)]] doubles {
    std::vector<double> v;
};

} // namespace

// The schema writes a list of a fixed-width number as a typed array of RFC 8746 in little endian. The tag numbers
// come from RFC 8746 figure 6: uint16le 69, uint32le 70, uint64le 71, sint8 72, sint16le 77, sint32le 78,
// sint64le 79, float16le 84, float32le 85, float64le 86. The byte string holds the elements with the lowest byte
// first. The test exists because the tag tells every other reader the type of the elements.
TEST_CASE("encode: a list of a fixed-width number is a typed array of RFC 8746")
{
    std::string const bytes = schema_bytes(sample_numbers);
    check_one_item(bytes);
    CHECK_NE(bytes.find("\xd8\x45\x5a\x00\x00\x00\x02\x02\x01"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x46\x5a\x00\x00\x00\x04\x04\x03\x02\x01"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x47\x5a\x00\x00\x00\x08\x08\x07\x06\x05\x04\x03\x02\x01"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x48\x5a\x00\x00\x00\x01\xfe"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x4d\x5a\x00\x00\x00\x02\xfe\xff"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x4e\x5a\x00\x00\x00\x04\xfe\xff\xff\xff"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x4f\x5a\x00\x00\x00\x08\xfe\xff\xff\xff\xff\xff\xff\xff"sv), std::string::npos);
#if defined(__STDCPP_FLOAT16_T__)
    CHECK_NE(bytes.find("\xd8\x54\x5a\x00\x00\x00\x02\x00\x3c"sv), std::string::npos);
#endif
    CHECK_NE(bytes.find("\xd8\x55\x5a\x00\x00\x00\x04\x00\x00\x80\x3f"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x56\x5a\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\xf0\x3f"sv), std::string::npos);
}

// decode and path read every element type back from its typed array. The floats are compared by their bits.
TEST_CASE("decode and at_path: every typed array gives its elements back")
{
    std::string const bytes = schema_bytes(sample_numbers);
    auto const back = cbor::schema<numbers>::decode(bytes);
    REQUIRE(back.has_value());
    numbers const &n = **back;
    CHECK_EQ(n.u16, sample_numbers.u16);
    CHECK_EQ(n.u32, sample_numbers.u32);
    CHECK_EQ(n.u64, sample_numbers.u64);
    CHECK_EQ(n.s8, sample_numbers.s8);
    CHECK_EQ(n.s16, sample_numbers.s16);
    CHECK_EQ(n.s32, sample_numbers.s32);
    CHECK_EQ(n.s64, sample_numbers.s64);
#if defined(__STDCPP_FLOAT16_T__)
    REQUIRE_EQ(n.f16.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint16_t>(n.f16[0]), 0x3c00u);
    CHECK_EQ(std::bit_cast<std::uint16_t>(*at_path<numbers, "$.f16[0]">(bytes)), 0x3c00u);
#endif
    REQUIRE_EQ(n.f32.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint32_t>(n.f32[0]), 0x3f800000u);
    REQUIRE_EQ(n.f64.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(n.f64[0]), 0x3ff0000000000000u);
    CHECK_EQ(at_path<numbers, "$.u16[0]">(bytes), 0x0102u);
    CHECK_EQ(at_path<numbers, "$.u32[]">(bytes, 0uz), 0x01020304u);
    CHECK_EQ(at_path<numbers, "$.u64[0]">(bytes), 0x0102030405060708u);
    CHECK_EQ(at_path<numbers, "$.s8[0]">(bytes), std::int8_t{-2});
    CHECK_EQ(at_path<numbers, "$.s16[0]">(bytes), std::int16_t{-2});
    CHECK_EQ(at_path<numbers, "$.s32[0]">(bytes), -2);
    CHECK_EQ(at_path<numbers, "$.s64[]">(bytes, 0uz), std::int64_t{-2});
    CHECK_EQ(std::bit_cast<std::uint32_t>(*at_path<numbers, "$.f32[0]">(bytes)), 0x3f800000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*at_path<numbers, "$.f64[0]">(bytes)), 0x3ff0000000000000u);
    CHECK_EQ(at_path<numbers, "$.f64[1]">(bytes).error(), error::index_out_of_bounds);
    auto const root = cbor::schema<numbers>::path(bytes);
    REQUIRE(root.has_value());
    auto const list = root->at_path<"$.u32">();
    REQUIRE(list.has_value());
    CHECK_EQ(list->size(), 1u);
    CHECK_EQ(list->at_path<"@[0]">(), 0x01020304u);
    CHECK_EQ(list->at_path<"@[]">(1uz).error(), error::index_out_of_bounds);
}

// A list of doubles that does not carry tag 86, or whose byte string is not a multiple of eight bytes, is refused
// as an error value by decode and by path. Tag 82 is binary64 in big endian and tag 85 is binary32 (RFC 8746).
TEST_CASE("decode and at_path: a typed array with a wrong tag or a wrong length is refused")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    std::size_t const at = bytes.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(at, std::string::npos);
    REQUIRE(cbor::schema<doubles>::decode(bytes).has_value());
    for (char const tag : {'\x52', '\x55'}) {
        std::string wrong = bytes;
        wrong[at + 1] = tag;
        CHECK_EQ(cbor::schema<doubles>::decode(wrong).error(), error::incorrect_type);
        CHECK_EQ(at_path<doubles, "$.v[0]">(wrong).error(), error::incorrect_type);
    }
    std::string odd = bytes;
    odd[at + 6] = '\x0f';
    CHECK_EQ(cbor::schema<doubles>::decode(odd).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(at_path<doubles, "$.v[0]">(odd).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[at + 6] = '\x18';
    CHECK_EQ(cbor::schema<doubles>::decode(longer).error(), error::too_little_data);
    CHECK_EQ(at_path<doubles, "$.v[0]">(longer).error(), error::too_little_data);
}

// schema::at reads one number from the message with no accessor and no owner, because the number is returned by
// value. It makes every check that path and accessor::at make: the table of keys, the undefined values after the
// directory, the tag of the root, the tag and the length of the typed array, and the index. The expected bits of 1.0
// and 2.0 are 3ff0000000000000 and 4000000000000000 in binary64 (IEEE 754).
TEST_CASE("schema::at: one number of a typed array, with every check of path")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    using S = cbor::schema<doubles>;
    CHECK_EQ(std::bit_cast<std::uint64_t>(*S::at_path<"$.v[0]">(bytes)), 0x3ff0000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*S::at_path<"$.v[]">(bytes, 1uz)), 0x4000000000000000u);
    CHECK_EQ(S::at_path<"$.v[2]">(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(S::at_path<"$.v[]">(bytes, std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
    CHECK_EQ(S::at_path<"$.v[0]">(std::string_view(bytes).substr(0, 20)).error(), error::too_little_data);
    std::size_t const at = bytes.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(at, std::string::npos);
    std::string wrong = bytes;
    wrong[at + 1] = '\x52';
    CHECK_EQ(S::at_path<"$.v[0]">(wrong).error(), error::incorrect_type);
    std::string odd = bytes;
    odd[at + 6] = '\x0f';
    CHECK_EQ(S::at_path<"$.v[0]">(odd).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[at + 6] = '\x18';
    CHECK_EQ(S::at_path<"$.v[0]">(longer).error(), error::too_little_data);
    std::size_t const filler = bytes.find('\xf7');
    REQUIRE_NE(filler, std::string::npos);
    for (std::size_t j = filler; j < bytes.size() && bytes[j] == '\xf7'; ++j) {
        std::string defined = bytes;
        defined[j] = '\xf6';
        CHECK_EQ(S::at_path<"$.v[0]">(defined).error(), error::incorrect_type);
        CHECK_EQ(at_path<doubles, "$.v[0]">(defined).error(), error::incorrect_type);
    }
    std::string root = bytes;
    root[root.size() - S::fixed_size() + 2] = '\xfc';
    CHECK_EQ(S::at_path<"$.v[0]">(root).error(), error::incorrect_type);
    CHECK_EQ(at_path<doubles, "$.v[0]">(root).error(), error::incorrect_type);
}

// schema::at gives only a value that holds no reference into the message, because no owner keeps the message alive.
// A path to a text, a list or a struct has no form of schema::at, and a wrong use does not compile.
TEST_CASE("schema::at: a path to a text or a list does not compile")
{
    auto const at_compiles = []<class T, cbor::fixed_string Path>() { return requires { cbor::schema<T>::template at_path<Path>(std::string_view{}); }; };
    CHECK(at_compiles.template operator()<doubles, "$.v[0]">());
    CHECK_FALSE(at_compiles.template operator()<doubles, "$.v">());
    CHECK_FALSE(at_compiles.template operator()<login, "$.name">());
    CHECK(at_compiles.template operator()<login, "$.id">());
    CHECK_FALSE(at_compiles.template operator()<people, "$.people[0]">());
    CHECK_FALSE(at_compiles.template operator()<people, "$.people[0].n">());
}

namespace
{

template <cbor::fixed_string Path>
auto view_of(std::string const &bytes)
{
    auto const root = cbor::schema<numbers>::path(bytes);
    REQUIRE(root.has_value());
    return root->view<Path>();
}

template <class E>
std::uint64_t bits_of(E const value)
{
    if constexpr (std::is_floating_point_v<E>)
        return std::bit_cast<std::conditional_t<sizeof(E) == 2, std::uint16_t, std::conditional_t<sizeof(E) == 4, std::uint32_t, std::uint64_t>>>(value);
    else
        return static_cast<std::uint64_t>(value);
}

} // namespace

// view checks the tag and the length of a typed array once and then reads an element with one bounds check. The test
// reads every element type of RFC 8746 that the schema writes. The expected bits come from RFC 8746 figure 6 and IEEE
// 754: 1.0 is 3c00 in binary16, 3f800000 in binary32 and 3ff0000000000000 in binary64; -2 is all ones but the lowest
// bit in two's complement. An index equal to the size is index_out_of_bounds, an error value and no read.
TEST_CASE("view: every typed array gives its elements back, and an index past the end is an error value")
{
    std::string const bytes = schema_bytes(sample_numbers);
    auto check = [&]<cbor::fixed_string Path>(std::uint64_t const expected) {
        auto const v = view_of<Path>(bytes);
        REQUIRE(v.has_value());
        REQUIRE_EQ(v->size(), 1u);
        auto const x = (*v)[0];
        REQUIRE(x.has_value());
        CHECK_EQ(bits_of(*x), expected);
        CHECK_EQ((*v)[1].error(), error::index_out_of_bounds);
        CHECK_EQ((*v)[std::numeric_limits<std::size_t>::max()].error(), error::index_out_of_bounds);
    };
    check.operator()<"$.u16">(0x0102);
    check.operator()<"$.u32">(0x01020304);
    check.operator()<"$.u64">(0x0102030405060708);
    check.operator()<"$.s8">(static_cast<std::uint64_t>(std::int64_t{-2}));
    check.operator()<"$.s16">(static_cast<std::uint64_t>(std::int64_t{-2}));
    check.operator()<"$.s32">(static_cast<std::uint64_t>(std::int64_t{-2}));
    check.operator()<"$.s64">(static_cast<std::uint64_t>(std::int64_t{-2}));
#if defined(__STDCPP_FLOAT16_T__)
    check.operator()<"$.f16">(0x3c00);
#endif
    check.operator()<"$.f32">(0x3f800000);
    check.operator()<"$.f64">(0x3ff0000000000000);
}

// A view of an empty typed array has size 0, and index 0 is already out of bounds.
TEST_CASE("view: an empty typed array has no element")
{
    auto const root = cbor::schema<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK_EQ(v->size(), 0u);
    CHECK_EQ((*v)[0].error(), error::index_out_of_bounds);
}

// view makes no view when the tag is not the one of the element type or the length is not a multiple of the element
// size. Tag 82 is binary64 in big endian and tag 85 is binary32 (RFC 8746). The errors are the same as at_path gives.
TEST_CASE("view: a typed array with a wrong tag or a wrong length gives no view")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    std::size_t const at = bytes.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(at, std::string::npos);
    auto refused = [](std::string const &wrong) {
        auto const root = cbor::schema<doubles>::path(wrong);
        REQUIRE(root.has_value());
        auto const v = root->view<"$.v">();
        REQUIRE_FALSE(v.has_value());
        return v.error();
    };
    for (char const tag : {'\x52', '\x55'}) {
        std::string wrong = bytes;
        wrong[at + 1] = tag;
        CHECK_EQ(refused(wrong), error::incorrect_type);
    }
    std::string odd = bytes;
    odd[at + 6] = '\x0f';
    CHECK_EQ(refused(odd), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[at + 6] = '\x18';
    CHECK_EQ(refused(longer), error::too_little_data);
}

// The view holds the owner of the bytes. The test reads it after the accessor and the string it came from are gone,
// so that the sanitizer sees a read of freed memory if the view did not keep the bytes alive.
TEST_CASE("view: the view keeps the bytes alive after the accessor is gone")
{
    std::optional<cbor::typed_array_view<double>> kept;
    {
        auto const root = cbor::schema<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        kept.emplace(std::move(*v));
    }
    REQUIRE_EQ(kept->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*(*kept)[1]), 0x4000000000000000u);
}

namespace
{

numbers const three_numbers{{1, 2, 0xfffe},
                            {1, 2, 0xfffffffe},
                            {1, 2, 0xfffffffffffffffe},
                            {-1, 2, -3},
                            {-1, 2, -3},
                            {-1, 2, -3},
                            {-1, 2, -3},
#if defined(__STDCPP_FLOAT16_T__)
                            {-1.0f16, 2.0f16, -4.0f16},
#endif
                            {-1.0f, 2.0f, -4.0f},
                            {-1.0, 2.0, -4.0}};

template <class E>
std::vector<std::uint64_t> bits_of_all(std::vector<E> const &values)
{
    std::vector<std::uint64_t> out;
    for (E const x : values)
        out.push_back(bits_of(x));
    return out;
}

} // namespace

// The iterator of a view yields each element by value. It is a random access iterator by the concepts of the
// standard library, so that std::ranges algorithms and range-for take the view as they take a std::span. The view is
// a sized random access range. It is not a borrowed range: its iterators read bytes that the view keeps alive.
TEST_CASE("view: the iterator and the view model the standard concepts")
{
    using V = cbor::typed_array_view<double>;
    using I = V::iterator;
    CHECK(std::random_access_iterator<I>);
    CHECK(std::sized_sentinel_for<I, I>);
    CHECK(std::same_as<std::iter_value_t<I>, double>);
    CHECK(std::same_as<std::iter_reference_t<I>, double>);
    CHECK(std::ranges::random_access_range<V const>);
    CHECK(std::ranges::sized_range<V const>);
    CHECK(std::ranges::common_range<V const>);
    CHECK_FALSE(std::ranges::borrowed_range<V>);
    CHECK_FALSE(std::ranges::contiguous_range<V>);
    CHECK_FALSE(std::output_iterator<I, double>);
}

// Each element type of RFC 8746 that the schema writes is read three ways: range-for, the iterator with index and
// arithmetic, and std::ranges algorithms. The expected bits are those of the values that were encoded. The order of
// the values makes the maximum the middle element and the minimum the last one for the signed types.
TEST_CASE("view: range-for, the iterator and std::ranges algorithms read every element type")
{
    std::string const bytes = schema_bytes(three_numbers);
    auto check = [&]<cbor::fixed_string Path, class E>(std::vector<E> const &source) {
        auto const v = view_of<Path>(bytes);
        REQUIRE(v.has_value());
        cbor::typed_array_view<E> const &view = *v;
        REQUIRE_EQ(view.size(), 3u);
        CHECK_FALSE(view.empty());
        std::vector<E> seen;
        for (E const x : view)
            seen.push_back(x);
        CHECK_EQ(bits_of_all(seen), bits_of_all(source));
        auto const b = view.begin();
        CHECK_EQ(view.end() - b, 3);
        CHECK_EQ(b - view.end(), -3);
        CHECK_EQ(bits_of(b[2]), bits_of(source[2]));
        CHECK_EQ(bits_of(*(b + 1)), bits_of(source[1]));
        CHECK_EQ(bits_of(*(1 + b)), bits_of(source[1]));
        CHECK_EQ(bits_of(*(view.end() - 1)), bits_of(source[2]));
        auto i = b;
        CHECK_EQ(bits_of(*i++), bits_of(source[0]));
        CHECK_EQ(bits_of(*i), bits_of(source[1]));
        CHECK_EQ(bits_of(*--i), bits_of(source[0]));
        i += 2;
        CHECK_EQ(bits_of(*i), bits_of(source[2]));
        i -= 1;
        CHECK_EQ(bits_of(*i--), bits_of(source[1]));
        CHECK(i == b);
        CHECK(b < view.end());
        CHECK(view.end() > b);
        CHECK(b <= b);
        CHECK_EQ(bits_of(std::ranges::fold_left(view, E{}, std::plus<>{})),
                 bits_of(std::ranges::fold_left(source, E{}, std::plus<>{})));
        CHECK_EQ(bits_of(std::ranges::max(view)), bits_of(std::ranges::max(source)));
        CHECK_EQ(std::ranges::find(view, source[1]) - view.begin(), 1);
        CHECK(std::ranges::find(view, E{0}) == view.end());
        CHECK_EQ(std::ranges::distance(view), 3);
    };
    check.operator()<"$.u16">(three_numbers.u16);
    check.operator()<"$.u32">(three_numbers.u32);
    check.operator()<"$.u64">(three_numbers.u64);
    check.operator()<"$.s8">(three_numbers.s8);
    check.operator()<"$.s16">(three_numbers.s16);
    check.operator()<"$.s32">(three_numbers.s32);
    check.operator()<"$.s64">(three_numbers.s64);
#if defined(__STDCPP_FLOAT16_T__)
    check.operator()<"$.f16">(three_numbers.f16);
#endif
    check.operator()<"$.f32">(three_numbers.f32);
    check.operator()<"$.f64">(three_numbers.f64);
}

// front and back read the first and the last element with one load each. first(n) and last(n) give a sub-view of n
// elements, as std::span does. n equal to the size gives the whole view; n one past it is index_out_of_bounds as an
// error value, where std::span would have undefined behaviour.
TEST_CASE("view: front, back, first and last")
{
    auto const root = cbor::schema<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK_EQ(std::bit_cast<std::uint64_t>(*v->front()), 0x3ff0000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*v->back()), 0x4010000000000000u);
    auto const head = v->first(2);
    REQUIRE(head.has_value());
    REQUIRE_EQ(head->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*head->back()), 0x4000000000000000u);
    CHECK_EQ((*head)[2].error(), error::index_out_of_bounds);
    auto const tail = v->last(2);
    REQUIRE(tail.has_value());
    REQUIRE_EQ(tail->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*tail->front()), 0x4000000000000000u);
    CHECK_EQ(v->first(3)->size(), 3u);
    CHECK_EQ(v->last(3)->size(), 3u);
    CHECK_EQ(v->first(0)->size(), 0u);
    CHECK_EQ(v->last(0)->size(), 0u);
    CHECK_EQ(v->first(4).error(), error::index_out_of_bounds);
    CHECK_EQ(v->last(4).error(), error::index_out_of_bounds);
    CHECK_EQ(v->first(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
    CHECK_EQ(v->last(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
}

// An empty view has no front and no back, which is an error value, and no element to iterate. first(0) and last(0)
// are empty views, and first(1) is out of bounds.
TEST_CASE("view: an empty view has no front, no back and no element")
{
    auto const root = cbor::schema<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK(v->empty());
    CHECK_EQ(v->front().error(), error::index_out_of_bounds);
    CHECK_EQ(v->back().error(), error::index_out_of_bounds);
    CHECK(v->begin() == v->end());
    CHECK_EQ(std::ranges::fold_left(*v, 0.0, std::plus<>{}), 0.0);
    CHECK(v->first(0)->empty());
    CHECK(v->last(0)->empty());
    CHECK_EQ(v->first(1).error(), error::index_out_of_bounds);
    CHECK_EQ(v->last(1).error(), error::index_out_of_bounds);
}

// A sub-view holds the owner as the view does. The test drops the string, the accessor and the parent view, then
// reads the sub-views, so that the sanitizer sees a read of freed memory if a sub-view borrowed from its parent.
// The sub-view of an rvalue view takes the owner over, and the test reads it after the same drops. A view made from
// an rvalue accessor takes the owner over too.
TEST_CASE("view: a view and a sub-view keep the bytes alive after the parent is gone")
{
    std::optional<cbor::typed_array_view<double>> copied;
    std::optional<cbor::typed_array_view<double>> moved;
    std::optional<cbor::typed_array_view<double>> taken;
    {
        auto const root = cbor::schema<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        auto c = v->last(2);
        REQUIRE(c.has_value());
        copied.emplace(std::move(*c));
        auto m = std::move(*v).first(1);
        REQUIRE(m.has_value());
        moved.emplace(std::move(*m));
        auto r = cbor::schema<doubles>::path(schema_bytes(doubles{{8.0}}));
        REQUIRE(r.has_value());
        auto t = std::move(*r).view<"$.v">();
        REQUIRE(t.has_value());
        taken.emplace(std::move(*t));
    }
    REQUIRE_EQ(copied->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*copied->back()), 0x4010000000000000u);
    CHECK_EQ(std::ranges::fold_left(*copied, 0.0, std::plus<>{}), 6.0);
    REQUIRE_EQ(moved->size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*moved->front()), 0x3ff0000000000000u);
    REQUIRE_EQ(taken->size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*taken->front()), 0x4020000000000000u);
}

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
// that size behind the data, and a path reads it without walking the ones before. A list of lists takes one
// index for each level.
TEST_CASE("at_path: a list of structs, strings and lists, by index")
{
    std::string const bytes = schema_bytes(sample_garage);
    std::uint32_t sum = 0;
    for (std::size_t i = 0;; ++i) {
        auto const d = at_path<garage, "$.tires[].diameter">(bytes, i);
        if (!d) {
            CHECK_EQ(d.error(), error::index_out_of_bounds);
            CHECK_EQ(i, 3u);
            break;
        }
        sum += *d;
    }
    CHECK_EQ(sum, 17u + 18u + 19u);
    CHECK_EQ(at_path<garage, "$.tires[1].airPressure">(bytes), 3.0f);
    CHECK_EQ(at_path<garage, "$.names[1]">(bytes), "bc"s);
    CHECK_EQ(at_path<garage, "$.rows[][]">(bytes, 2uz, 0uz), 3u);
    CHECK_EQ(at_path<garage, "$.rows[][]">(bytes, 0uz, 1uz), 2u);
    CHECK_EQ(at_path<garage, "$.rows[1][0]">(bytes).error(), error::index_out_of_bounds);
}

// RFC 8949 5.6 lets a decoder that is not in a deterministic profile keep one entry of a repeated key. The key
// 9 of the sample is written over with 7, the other key, and decode keeps the first entry with no error.
TEST_CASE("schema: decode keeps the first entry of a repeated key")
{
    std::string bytes = schema_bytes(sample_garage);
    std::string const nine = "\x19\x00\x09"s;
    auto const at = bytes.find(nine);
    REQUIRE(at != std::string::npos);
    REQUIRE(bytes.find(nine, at + 1) == std::string::npos);
    bytes.replace(at, nine.size(), "\x19\x00\x07"s);
    auto const read = cbor::schema<garage>::decode(bytes);
    REQUIRE(read.has_value());
    CHECK_EQ((*read)->owners, (std::map<std::uint16_t, std::string>{{7, "seven"}}));
    CHECK(cbor::schema<garage>::decode(schema_bytes(sample_garage)).has_value());
}

template <class T, cbor::fixed_string Path>
concept path_reads = requires(typename cbor::schema<T>::template accessor<> const &a) { a.template at_path<Path>(); };

template <class T, cbor::fixed_string Path, class I>
concept path_reads_at = requires(typename cbor::schema<T>::template accessor<> const &a, I const i) { a.template at_path<Path>(i); };

// A path that ends at a leaf gives the value. A path that ends at a struct, a list, a fixed array or a map gives an
// accessor. A name that is not a member, an index into a text or a map, an index past a fixed array, and a count of
// indexes that does not match the empty brackets do not compile. A path starts at the root of the top-level item with $
// (RFC 9535 2.2) or at the node of the accessor with @ (RFC 9535 2.3.5); on the root accessor both are the same
// node.
TEST_CASE("path: a path compiles where it names a part of the type")
{
    CHECK(path_reads<garage, "$.names[0]">);
    CHECK(path_reads<garage, "$.tires">);
    CHECK(path_reads<garage, "$.tires[0]">);
    CHECK(path_reads<garage, "$.owners">);
    CHECK(path_reads<garage, "$.rows[0]">);
    CHECK(path_reads<garage, "$">);
    CHECK(path_reads<vehicle, "$.motor">);
    CHECK(path_reads<vehicle, "$.spare">);
    CHECK(path_reads_at<garage, "$.tires[].diameter", std::size_t>);
    CHECK(path_reads_at<garage, "$.tires[]", std::size_t>);
    CHECK_FALSE(path_reads<garage, "$.owners[7]">);
    CHECK_FALSE(path_reads<garage, "$.names[0][0]">);
    CHECK_FALSE(path_reads<garage, "$.nothing">);
    CHECK_FALSE(path_reads<garage, "$.tires[1">);
    CHECK_FALSE(path_reads<garage, "$.tires[].diameter">);
    CHECK_FALSE(path_reads_at<garage, "$.tires[0].diameter", std::size_t>);
    CHECK_FALSE(path_reads<vehicle, "$.spare[2].diameter">);
    CHECK_FALSE(path_reads<vehicle, "$.code[0]">);
    CHECK_FALSE(path_reads<vehicle, "$.make[0]">);
    CHECK_FALSE(path_reads<garage, ".tires">);
    CHECK_FALSE(path_reads<garage, "tires">);
    CHECK(path_reads<garage, "@.tires">);
    using A = cbor::schema<garage>::accessor<>;
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.names[0]">()), std::expected<std::string_view, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.tires[]">(0uz)), std::expected<cbor::schema<garage>::accessor<tire>, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.tires">()),
                       std::expected<cbor::schema<garage>::accessor<std::vector<tire>>, cbor::error>>);
}

// An accessor of a list gives its size and its elements; an accessor of a struct gives its fields. A leaf through
// several steps gives the same value as the same steps through named accessors.
TEST_CASE("path: an accessor reads relative to the part it names")
{
    std::string const bytes = schema_bytes(sample_garage);
    auto const opened = cbor::schema<garage>::path(bytes);
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    auto const tires = root.at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 3u);
    auto const second = tires->at_path<"@[]">(1uz);
    REQUIRE(second.has_value());
    CHECK_EQ(second->at_path<"@.diameter">(), 18u);
    CHECK_EQ(second->at_path<"@.airPressure">(), root.at_path<"$.tires[].airPressure">(1uz));
    CHECK_EQ(tires->at_path<"@[]">(3uz).error(), error::index_out_of_bounds);
    auto const rows = root.at_path<"$.rows">();
    REQUIRE(rows.has_value());
    CHECK_EQ(rows->size(), 3u);
    auto const first = rows->at_path<"@[0]">();
    REQUIRE(first.has_value());
    CHECK_EQ(first->size(), 2u);
    CHECK_EQ(first->at_path<"@[1]">(), 2u);
    auto const owners = root.at_path<"$.owners">();
    REQUIRE(owners.has_value());
    CHECK_EQ(owners->size(), 2u);
    auto const names = root.at_path<"$.names">();
    REQUIRE(names.has_value());
    CHECK_EQ(names->at_path<"@[]">(1uz), "bc"sv);
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
    CHECK_EQ(at_path<keyed, "$.EOF">(bytes), true);
}

// A struct that holds a list of itself is read by recursion, and the bytes decide how deep. Without a limit, a
// chain of 100000 nodes in 2.2 MB overflowed a stack of 8 MiB. Each reference that is followed counts one
// level, as each nested item counts one in the other decoders.
TEST_CASE("decode: a struct that holds itself stops at the nesting depth in force")
{
    REQUIRE_EQ(cbor::schema<node>::fixed_size(), 16u);

    auto const at_limit = cbor::schema<node>::decode(node_chain(128));
    REQUIRE(at_limit.has_value());
    CHECK_EQ(depth_of(**at_limit), 128u);

    auto const over = cbor::schema<node>::decode(node_chain(129));
    REQUIRE_FALSE(over.has_value());
    CHECK_EQ(over.error(), error::nesting_depth_exceeded);

    {
        test::nesting_depth_max_guard const depth{3};
        auto const small = cbor::schema<node>::decode(node_chain(3));
        REQUIRE(small.has_value());
        CHECK_EQ(depth_of(**small), 3u);
        CHECK_EQ(cbor::schema<node>::decode(node_chain(4)).error(), error::nesting_depth_exceeded);
    }

    auto const deep = cbor::schema<node>::decode(node_chain(100000));
    REQUIRE_FALSE(deep.has_value());
    CHECK_EQ(deep.error(), error::nesting_depth_exceeded);
}

// schema::value_read reads a chain as deep as validity::nesting_depth_limit allows, so the stack of the
// thread holds the deepest chain that the library accepts.
TEST_CASE("nesting depth limit: schema")
{
    test::nesting_depth_max_guard const depth{cbor::validity::nesting_depth_limit};
    auto const deepest = cbor::schema<node>::decode(node_chain(cbor::validity::nesting_depth_limit));
    REQUIRE(deepest.has_value());
    CHECK_EQ(depth_of(**deepest), cbor::validity::nesting_depth_limit);
    CHECK_EQ(cbor::schema<node>::decode(node_chain(cbor::validity::nesting_depth_limit + 1)).error(),
             error::nesting_depth_exceeded);
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
    CHECK_EQ(too_small.error(), cbor::error::no_buffer_space);

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
// fixed item as an array of a presence flag and its fields. decode reads it; a path does not step into it. Both
// forms stay two well-formed CBOR items.
TEST_CASE("schema: an optional struct and an optional string, present and absent")
{
    passkey_login const full{{std::byte{1}, std::byte{2}}, passkey_user{{std::byte{9}}, "alice"}, "hello"};
    std::string const bytes = *cbor::schema<passkey_login>::encode(full);
    CHECK_EQ(at_path<passkey_login, "$.note">(bytes).value(), std::optional<std::string>{"hello"});
    auto const decoded_back = *cbor::schema<passkey_login>::decode(bytes);
    passkey_login const &back = *decoded_back;
    REQUIRE(back.user.has_value());
    CHECK_EQ(back.user->name, "alice");
    CHECK_EQ(back.note, std::optional<std::string>{"hello"});

    passkey_login const empty{{std::byte{1}}, std::nullopt, std::nullopt};
    std::string const none = *cbor::schema<passkey_login>::encode(empty);
    auto const absent = at_path<passkey_login, "$.note">(none);
    REQUIRE(absent.has_value());
    CHECK_FALSE(absent->has_value());
    for (std::string_view message : {std::string_view(bytes), std::string_view(none)}) {
        auto const end = cbor::item_size(message);
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
TEST_CASE("schema: a root with 20 struct types reaches indexes 8 and more with tag 6")
{
    CHECK_EQ(cbor::schema<chain0>::fixed_size<chain19>(), 3u + 3u + 5u + 2u + 6u + 6u);
    CHECK_EQ(cbor::schema<chain19>::fixed_size(), 3u + 2u + 5u + 2u + 6u + 6u);
    CHECK_EQ(cbor::schema<chain0>::fixed_size(), 8u * 12u + 11u * 13u + 25u);
    CHECK_EQ((cbor::schema<chain0>::member_offset<^^chain8::next, chain8>()), 13u);
    CHECK_EQ(cbor::schema<chain8>::member_offset<^^chain8::next>(), 12u);

    chain0 const value = chain_of<chain0>(0);
    std::string const bytes = *cbor::schema<chain0>::encode(value);
    CHECK_EQ(cbor::item_size(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x19"s);
    CHECK_EQ(bytes.substr(bytes.size() - 25),
             "\xd9\x06\x53\xc6\x82\x0b\x9a\x00\x00\x00\x03\x18\x13\xc6\x3a\x00\x00\x00\x02\xc6\x1a\x00\x00\x00\x03"s);
    CHECK_EQ(bytes.substr(bytes.size() - cbor::schema<chain0>::fixed_size() + 8u * 12u, 6), "\xd9\x06\x48\xc6\x82\x00"s);

    auto const decoded_back = *cbor::schema<chain0>::decode(bytes);
    chain0 const &back = *decoded_back;
    CHECK_EQ(*cbor::schema<chain0>::encode(back), bytes);
    CHECK_EQ(back.next.next.next.next.next.next.next.next.next.v, 9u);

    CHECK_EQ(at_path<chain0, "$.next.next.next.next.next.next.next.next.next.v">(bytes), 9u);
    CHECK_EQ(at_path<chain0, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.v">(bytes),
             19u);
    CHECK_EQ(at_path<chain0, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(
                 bytes),
             "ab"s);
    CHECK_EQ(at_path<chain0, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.words[1]">(
                 bytes),
             "yz"s);
}

// draft-ietf-cbor-packed-19 2.2 Table 1: with 17 struct types the directory is index 17 and the first shared item
// is index 18, which is 6(1) = c6 1a 00 00 00 01. The test exists because a reference below index 18 then names
// no shared item: 6(0) is index 16, a record function, and the reader refuses it.
TEST_CASE("schema: a root with 17 struct types starts its shared items at index 18")
{
    chain3 const value = chain_of<chain3>(3);
    std::string const bytes = *cbor::schema<chain3>::encode(value);
    CHECK_EQ(cbor::item_size(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x16"s);
    CHECK_EQ(bytes.substr(bytes.size() - 25),
             "\xd9\x06\x53\xc6\x82\x08\x9a\x00\x00\x00\x03\x18\x13\xc6\x1a\x00\x00\x00\x01\xc6\x3a\x00\x00\x00\x01"s);

    auto const decoded_back = *cbor::schema<chain3>::decode(bytes);
    chain3 const &back = *decoded_back;
    CHECK_EQ(*cbor::schema<chain3>::encode(back), bytes);
    CHECK_EQ(at_path<chain3, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(bytes), "ab"s);

    std::string broken = bytes;
    broken.at(broken.size() - 7) = '\x00';
    CHECK_EQ(cbor::schema<chain3>::decode(broken).error(), error::unpopulated_table_index);
    CHECK_EQ(at_path<chain3, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(broken).error(),
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
    CHECK_EQ(at_path<point_b, "$.x">(bytes).error(), error::incorrect_type);
    CHECK_EQ(at_path<point_a, "$.x">(bytes), 7u);
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
    CHECK_EQ(at_path<car, "$.seats">(forged), 4u);
    CHECK_EQ(at_path<car, "$.motor.cc">(forged).error(), error::incorrect_type);
    CHECK_EQ(at_path<car, "$.motor.cc">(bytes), 1800u);
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
    std::expected<cbor::owning_ref<ticket>, cbor::error> const t = cbor::schema<ticket>::decode(ticket_bytes());
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

// The copying form of path holds its own copy of the message. The message here is a temporary that is destroyed at
// the end of the full expression. The test exists because a view into those bytes would read freed memory, and the
// address sanitizer reports that read.
TEST_CASE("path: the copying form outlives the message of the caller")
{
    auto const opened = cbor::schema<ticket>::path(ticket_bytes());
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    CHECK_EQ(root.at_path<"$.holder">(), sample_ticket.holder);
    CHECK_EQ(root.at_path<"$.seats[]">(1uz), sample_ticket.seats.at(1));
}

// The owner form keeps the owner in the root accessor. The caller releases its own pointer to the owner before it
// reads. The test exists because the root alone must keep the bytes alive.
TEST_CASE("path: the root keeps the owner after the caller releases it")
{
    auto owner = std::make_shared<std::string const>(ticket_bytes());
    std::string_view const bytes = *owner;
    auto const opened = cbor::schema<ticket>::path(std::move(owner), bytes);
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    auto const holder = root.at_path<"$.holder">();
    auto const seat = root.at_path<"$.seats[1]">();
    REQUIRE(holder.has_value());
    REQUIRE(seat.has_value());
    CHECK_EQ(*holder, sample_ticket.holder);
    CHECK_EQ(*seat, sample_ticket.seats.at(1));
}

#endif

#ifdef __cpp_impl_reflection

// The tests below attack the path reader with forged and cut messages. Every message is copied into a heap block of
// its exact size, so the address sanitizer reports a read of one byte past its end. Every text of the owner form must
// lie inside the message.

namespace
{

struct [[=cbor::tag(1523)]] probe {
    bool flag;
    std::int16_t i16;
    std::uint64_t u64;
    double f64;
    float f32;
    cbor::int128 i128;
    char code[3];
    std::string text;
    std::vector<tire> tires;
    std::optional<std::int32_t> maybe;
    engine motor;
    std::array<std::uint16_t, 2> pair;
};

probe const sample_probe{true, -300, 1ull << 40, 2.5, -1.5f, cbor::int128{-5}, {'x', 'y', 'z'}, "probe text",
                         {{17, 1.5f}, {18, 2.0f}}, -9, {300, 1800}, {{7, 8}}};

std::size_t probe_offset(std::size_t const size, std::size_t const member)
{
    return size - cbor::schema<probe>::fixed_size() + member;
}

bool lies_inside(std::string_view const part, std::string_view const message)
{
    std::less<char const *> const before;
    return part.empty() || (!before(part.data(), message.data()) &&
                            !before(message.data() + message.size(), part.data() + part.size()));
}

// Reads every leaf path of probe, each through a new root and through a root with an owner, from a heap block of the exact size of the message. Gives the count of
// paths that gave a value.
std::size_t probe_paths_read(std::string_view const message)
{
    std::shared_ptr<char const[]> const block = [message] {
        auto b = std::make_unique_for_overwrite<char[]>(message.size());
        std::ranges::copy(message, b.get());
        return b;
    }();
    std::string_view const bytes(block.get(), message.size());
    std::size_t n = 0;
    auto const count = [&n](auto const &r) {
        if (r)
            ++n;
    };
    auto const owned = [&n, bytes](auto const &r) {
        if (!r)
            return;
        ++n;
        CHECK(lies_inside(*r, bytes));
    };
    count(at_path<probe, "$.flag">(bytes));
    count(at_path<probe, "$.i16">(bytes));
    count(at_path<probe, "$.u64">(bytes));
    count(at_path<probe, "$.f64">(bytes));
    count(at_path<probe, "$.f32">(bytes));
    count(at_path<probe, "$.i128">(bytes));
    count(at_path<probe, "$.code">(bytes));
    owned(at_path<probe, "$.code">(block, bytes));
    count(at_path<probe, "$.text">(bytes));
    owned(at_path<probe, "$.text">(block, bytes));
    for (std::size_t i = 0; i < 3; ++i) {
        count(at_path<probe, "$.tires[].diameter">(bytes, i));
        count(at_path<probe, "$.tires[].airPressure">(bytes, i));
        count(at_path<probe, "$.pair[]">(bytes, i));
    }
    count(at_path<probe, "$.maybe">(bytes));
    count(at_path<probe, "$.motor.horsepower">(bytes));
    count(at_path<probe, "$.motor.cc">(bytes));
    return n;
}

constexpr std::size_t probe_paths = 6 + 2 + 2 + 3 * 2 + 3;

// Reads every leaf path of probe through one root accessor, first in order and then in the reverse order, and the
// tires through an accessor of each tire. Gives the count of paths that gave a value in the first pass. Each value of
// a later pass is the value of the first pass.
std::size_t probe_lot_read(std::string_view const message)
{
    std::shared_ptr<char const[]> const block = [message] {
        auto b = std::make_unique_for_overwrite<char[]>(message.size());
        std::ranges::copy(message, b.get());
        return b;
    }();
    std::string_view const bytes(block.get(), message.size());
    auto opened = cbor::schema<probe>::path(block, bytes);
    if (!opened)
        return 0;
    auto &lot = *opened;
    std::vector<std::optional<double>> first;
    auto const number = [](auto const &r) -> std::optional<double> {
        if (!r)
            return std::nullopt;
        if constexpr (requires { r->has_value(); })
            return r->has_value() ? std::optional<double>(static_cast<double>(**r)) : std::optional<double>(-1);
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(*r)>, cbor::int128>)
            return static_cast<double>(static_cast<std::int64_t>(*r));
        else
            return static_cast<double>(*r);
    };
    auto const text = [bytes](auto const &r) -> std::optional<double> {
        if (!r)
            return std::nullopt;
        CHECK(lies_inside(*r, bytes));
        return static_cast<double>(r->size());
    };
    auto const all = [&] {
        std::vector<std::optional<double>> v;
        v.push_back(number(lot.at_path<"$.flag">()));
        v.push_back(number(lot.at_path<"$.i16">()));
        v.push_back(number(lot.at_path<"$.u64">()));
        v.push_back(number(lot.at_path<"$.f64">()));
        v.push_back(number(lot.at_path<"$.f32">()));
        v.push_back(number(lot.at_path<"$.i128">()));
        v.push_back(text(lot.at_path<"$.code">()));
        v.push_back(text(lot.at_path<"$.text">()));
        for (std::size_t i = 0; i < 3; ++i) {
            v.push_back(number(lot.at_path<"$.tires[].diameter">(i)));
            v.push_back(number(lot.at_path<"$.tires[].airPressure">(i)));
            v.push_back(number(lot.at_path<"$.pair[]">(i)));
        }
        v.push_back(number(lot.at_path<"$.maybe">()));
        v.push_back(number(lot.at_path<"$.motor.horsepower">()));
        v.push_back(number(lot.at_path<"$.motor.cc">()));
        return v;
    };
    std::vector<std::optional<double>> const forward = all();
    std::vector<std::optional<double>> backward;
    for (std::size_t i = 3; i-- > 0;) {
        backward.push_back(number(lot.at_path<"$.tires[].airPressure">(i)));
        backward.push_back(number(lot.at_path<"$.tires[].diameter">(i)));
    }
    CHECK_EQ(backward[0], forward[8 + 3 * 2 + 1]);
    CHECK_EQ(backward[1], forward[8 + 3 * 2]);
    CHECK_EQ(backward[4], forward[8 + 1]);
    CHECK_EQ(backward[5], forward[8]);
    for (std::size_t i = 0; i < 3; ++i) {
        auto const tire = lot.at_path<"$.tires[]">(i);
        std::optional<double> const diameter = tire ? number(tire->at_path<"@.diameter">()) : std::nullopt;
        std::optional<double> const pressure = tire ? number(tire->at_path<"@.airPressure">()) : std::nullopt;
        CHECK_EQ(diameter, forward[8 + 3 * i]);
        CHECK_EQ(pressure, forward[8 + 3 * i + 1]);
    }
    CHECK_EQ(all(), forward);
    return static_cast<std::size_t>(std::ranges::count_if(forward, [](auto const &x) { return x.has_value(); }));
}

constexpr std::size_t probe_lot_paths = 8 + 3 * 2 + 3;

template <class A, cbor::fixed_string Path>
concept temporary_reads = requires(A &&a) { std::move(a).template at_path<Path>(); };

template <class A>
concept chain_reads = requires(A const &a) { (*a.template at_path<"$.tires[]">(0uz)).template at_path<"@.diameter">(); };

template <class R>
concept dereferenced_as_rvalue = requires(R &&r) { *std::move(r); } || requires(R &&r) { std::move(r).operator->(); };

} // namespace

// The whole message reads every path, and each value is the one that went in.
TEST_CASE("attack: the sample message reads every path")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    CHECK_EQ(probe_paths_read(bytes), probe_paths);
    CHECK_EQ(probe_lot_read(bytes), probe_lot_paths);
    CHECK_EQ(at_path<probe, "$.i16">(bytes), -300);
    CHECK((*at_path<probe, "$.i128">(bytes) == cbor::int128{-5}));
    CHECK_EQ(at_path<probe, "$.f64">(bytes), 2.5);
    CHECK_EQ(at_path<probe, "$.pair[]">(bytes, 1uz), 8u);
    CHECK_EQ(at_path<probe, "$.maybe">(bytes).value(), std::optional<std::int32_t>{-9});
}

// A text is a view into the bytes that the root holds. An accessor is read through an lvalue only, so each accessor
// on the way to a value has a name. An owning_ref cannot be read through a temporary. An owner that is empty is a
// wrong use and throws. The test exists because each of these would otherwise let a view outlive its bytes.
TEST_CASE("attack: no text result outlives its bytes")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    using A = cbor::schema<probe>::accessor<>;
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.text">()), std::expected<std::string_view, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.code">()), std::expected<std::string_view, cbor::error>>);
    CHECK(path_reads<probe, "$.text">);
    CHECK_FALSE(temporary_reads<A, "$.text">);
    CHECK_FALSE(temporary_reads<cbor::schema<probe>::accessor<tire>, "@.diameter">);
    CHECK_FALSE(chain_reads<A>);
    CHECK_FALSE(dereferenced_as_rvalue<cbor::owning_ref<probe>>);
    CHECK_THROWS_AS((void)cbor::schema<probe>::path(std::shared_ptr<void const>{}, bytes), std::logic_error);
    CHECK_THROWS_AS((void)cbor::schema<probe>::decode(std::shared_ptr<void const>{}, bytes), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. A temporary or a moved std::string beside an
// owner does not compile, because the owner does not hold it. The test exists because each of these let a view
// outlive its bytes, and a check of the stored pointer refused an owner that holds the bytes.
TEST_CASE("attack: path and decode check that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>(*cbor::schema<probe>::encode(sample_probe));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::schema<probe>::path(o, std::string(*o)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o, std::string s) { cbor::schema<probe>::path(o, std::move(s)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::schema<probe>::decode(o, std::string(*o)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o, std::string s) { cbor::schema<probe>::decode(o, std::move(s)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::schema<probe>::path(o, *o); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::schema<probe>::decode(o, *o); }; }(bytes)));
    std::shared_ptr<void const> const holds_nothing(std::shared_ptr<void const>{}, bytes->data());
    CHECK_THROWS_AS((void)cbor::schema<probe>::path(holds_nothing, *bytes), std::logic_error);
    CHECK_THROWS_AS((void)cbor::schema<probe>::decode(holds_nothing, *bytes), std::logic_error);
    std::shared_ptr<void const> const holds_bytes(bytes, nullptr);
    auto const root = cbor::schema<probe>::path(holds_bytes, *bytes);
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.i16">(), -300);
    auto const decoded = cbor::schema<probe>::decode(holds_bytes, *bytes);
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->i16, -300);
}

// Each prefix of the message is a cut message. The test exists because a reader that trusted an offset from the
// wire would read past the end of the cut. No prefix reads a value: the root lies at the end of the message.
TEST_CASE("attack: every prefix of a message is an error for every path")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    std::size_t values = 0;
    for (std::size_t size = 0; size < bytes.size(); ++size)
        values += probe_paths_read(std::string_view(bytes).substr(0, size)) + probe_lot_read(std::string_view(bytes).substr(0, size));
    CHECK_EQ(values, 0u);
}

// A byte in front of the message or behind it moves the root away from its place at the end. The test exists because
// a reader that found the root by an offset from the start would read a record that is not there.
TEST_CASE("attack: a shifted root is incorrect_type")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    CHECK_EQ(at_path<probe, "$.i16">("\x00"s + bytes).error(), error::incorrect_type);
    CHECK_EQ(at_path<probe, "$.i16">(bytes + "\x00"s).error(), error::incorrect_type);
    CHECK_EQ(probe_paths_read("\x00"s + bytes), 0u);
    CHECK_EQ(probe_paths_read(bytes + "\x00"s), 0u);
    CHECK_EQ(probe_lot_read("\x00"s + bytes), 0u);
    CHECK_EQ(probe_lot_read(bytes + "\x00"s), 0u);
    CHECK_EQ(cbor::schema<probe>::path(bytes + "\x00"s).error(), error::incorrect_type);
}

// RFC 8949 3: each fixed field has one head. The heads that are valid are written here from RFC 8949 3.3 and 3.4
// and the fixed widths of the schema: a bool is f4 or f5, an int16 is 19 or 39, a uint64 is 1b, a double fb, a
// float fa, an int128 the tag c2 or c3, a char[3] the text head 63. Every other head is incorrect_type. The test
// exists because a reader that ignored the head would read the argument of another type.
TEST_CASE("attack: every forged head of a fixed field is incorrect_type")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    auto const forged = [&bytes](std::size_t const member, int const head) {
        std::string f = bytes;
        f.at(probe_offset(f.size(), member)) = static_cast<char>(head);
        return f;
    };
    using S = cbor::schema<probe>;
    for (int head = 0; head < 256; ++head) {
        CAPTURE(head);
        CHECK_EQ(at_path<probe, "$.flag">(forged(S::member_offset<^^probe::flag>(), head)).has_value(), head == 0xf4 || head == 0xf5);
        CHECK_EQ(at_path<probe, "$.i16">(forged(S::member_offset<^^probe::i16>(), head)).has_value(), head == 0x19 || head == 0x39);
        CHECK_EQ(at_path<probe, "$.u64">(forged(S::member_offset<^^probe::u64>(), head)).has_value(), head == 0x1b);
        CHECK_EQ(at_path<probe, "$.f64">(forged(S::member_offset<^^probe::f64>(), head)).has_value(), head == 0xfb);
        CHECK_EQ(at_path<probe, "$.f32">(forged(S::member_offset<^^probe::f32>(), head)).has_value(), head == 0xfa);
        CHECK_EQ(at_path<probe, "$.i128">(forged(S::member_offset<^^probe::i128>(), head)).has_value(), head == 0xc2 || head == 0xc3);
        CHECK_EQ(at_path<probe, "$.code">(forged(S::member_offset<^^probe::code>(), head)).has_value(), head == 0x63);
        CHECK_EQ(at_path<probe, "$.pair[1]">(forged(S::member_offset<^^probe::pair>(), head)).has_value(), head == 0x82);
    }
}

// Every byte of the root record is forged in turn, with values that flip a head, a length or a reference. The test
// exists because no forged byte may make a path read outside the message; the sanitizer and the check of each text
// see that.
TEST_CASE("attack: a forged byte anywhere in the root reads nothing outside the message")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    std::size_t const root = bytes.size() - cbor::schema<probe>::fixed_size();
    for (std::size_t at = root; at < bytes.size(); ++at) {
        for (int const flip : {0x01, 0x20, 0x80, 0xff}) {
            std::string f = bytes;
            f.at(at) = static_cast<char>(f.at(at) ^ flip);
            probe_paths_read(f);
            probe_lot_read(f);
        }
    }
}

// A directory entry of the wire is forged to the edges of the message: before the shared items, inside the root,
// past the end, and to the largest u32. The test exists because an item that begins in the directory or ends in the
// root would overlap bytes that another path reads as another type.
TEST_CASE("attack: a directory entry out of range is an error")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    std::size_t const head = bytes.find("\x5a\x00\x00\x00\x0c"s);
    REQUIRE_NE(head, std::string::npos);
    std::size_t const root = bytes.size() - cbor::schema<probe>::fixed_size();
    for (std::size_t j = 0; j < 3; ++j) {
        for (std::uint32_t const to : {0u, static_cast<std::uint32_t>(head), static_cast<std::uint32_t>(root),
                                       static_cast<std::uint32_t>(bytes.size()), 0x7fffffffu, 0xffffffffu}) {
            std::string f = bytes;
            f.replace(head + 5 + 4 * j, 4, std::string{static_cast<char>(to >> 24), static_cast<char>(to >> 16),
                                                       static_cast<char>(to >> 8), static_cast<char>(to)});
            CAPTURE(j);
            CAPTURE(to);
            CHECK_FALSE(cbor::schema<probe>::decode(f).has_value());
            CHECK_LT(probe_paths_read(f), probe_paths);
            CHECK_LT(probe_lot_read(f), probe_lot_paths);
        }
    }
}

// Two directory entries are forged to the same offset, so two items would share their bytes. The text item of login
// is the last item; its length is forged to reach into the root record behind it. The test exists because a text
// that reached into the root would give the bytes of the root's fields as text.
TEST_CASE("attack: overlapping items are an error")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    std::size_t const head = bytes.find("\x5a\x00\x00\x00\x0c"s);
    REQUIRE_NE(head, std::string::npos);
    std::string same = bytes;
    same.replace(head + 9, 4, bytes.substr(head + 5, 4));
    CHECK_FALSE(at_path<probe, "$.text">(same).has_value());
    CHECK_FALSE(at_path<probe, "$.tires[0].diameter">(same).has_value());

    std::string const one = schema_bytes(login{5, true, "ab"});
    std::size_t const text = one.find("\x7a\x00\x00\x00\x02" "ab"s);
    REQUIRE_NE(text, std::string::npos);
    std::string into_root = one;
    into_root.replace(text + 1, 4, std::string{'\x00', '\x00', '\x00', static_cast<char>(2 + cbor::schema<login>::fixed_size())});
    CHECK_EQ(at_path<login, "$.name">(into_root).error(), error::too_little_data);
    CHECK_EQ(at_path<login, "$.id">(into_root), 5u);
}

// A record inside a list carries its class tag like the root. The tag of the second tire is forged. The test exists
// because a path into a list must check the tag of the record it reaches, not only the tag of the root.
TEST_CASE("attack: a forged class tag of a record in a list is incorrect_type")
{
    std::string const bytes = *cbor::schema<probe>::encode(sample_probe);
    std::size_t const second = bytes.find("\xd9\x05\xe7"s, bytes.find("\xd9\x05\xe7"s) + 1);
    REQUIRE_NE(second, std::string::npos);
    std::string forged = bytes;
    forged.at(second + 2) = '\xe8';
    CHECK_EQ(at_path<probe, "$.tires[0].diameter">(forged), 17u);
    CHECK_EQ(at_path<probe, "$.tires[1].diameter">(forged).error(), error::incorrect_type);
    auto lot = *cbor::schema<probe>::path(forged);
    for (int pass = 0; pass < 2; ++pass) {
        CHECK_EQ(lot.at_path<"$.tires[].diameter">(0uz), 17u);
        CHECK_EQ(lot.at_path<"$.tires[].diameter">(1uz).error(), error::incorrect_type);
        CHECK_EQ(lot.at_path<"$.tires[].airPressure">(1uz).error(), error::incorrect_type);
    }
}

namespace
{

struct [[=cbor::tag(1524)]] lot_wheel {
    std::uint16_t diameter;
    float airPressure;
    bool snowTires;
};

struct [[=cbor::tag(1525)]] lot_engine {
    std::uint16_t horsepower;
    std::uint32_t cc;
};

struct [[=cbor::tag(1526)]] lot_car {
    std::string make;
    std::string model;
    std::uint8_t seats;
    std::vector<lot_wheel> wheels;
    lot_engine engine;
    std::optional<std::string> note;
    char code[2];
};

struct [[=cbor::tag(1527)]] parking_lot {
    std::vector<lot_car> cars;
    std::vector<std::string> notes;
    std::vector<std::vector<std::uint16_t>> rows;
};

parking_lot sample_lot()
{
    parking_lot l;
    for (std::uint16_t i = 0; i < 5; ++i) {
        lot_car c{"make" + std::to_string(i), std::string(i, 'm'), static_cast<std::uint8_t>(2 + i), {}, {static_cast<std::uint16_t>(100 + i), 1000u * i},
                  i % 2 == 0 ? std::optional<std::string>("n" + std::to_string(i)) : std::nullopt, {static_cast<char>('a' + i), 'z'}};
        for (std::uint16_t k = 0; k < i % 4; ++k)
            c.wheels.push_back({static_cast<std::uint16_t>(15 + k + i), 1.5f * k, k % 2 == 1});
        l.cars.push_back(c);
    }
    l.notes = {"first", "", "third"};
    l.rows = {{1, 2}, {}, {3}};
    return l;
}

std::string text_of(std::expected<std::string_view, cbor::error> const &r)
{
    return r ? std::string(*r) : "(error)"s;
}

// One read of every field of car i, through leaf paths from the root and through an accessor of the car and of each
// wheel, compared with the decoded car.
void lot_car_matches(cbor::schema<parking_lot>::accessor<> const &lot, parking_lot const &back, std::size_t const i)
{
    CAPTURE(i);
    lot_car const &c = back.cars.at(i);
    CHECK_EQ(text_of(lot.at_path<"$.cars[].make">(i)), c.make);
    CHECK_EQ(text_of(lot.at_path<"$.cars[].model">(i)), c.model);
    CHECK_EQ(lot.at_path<"$.cars[].seats">(i), c.seats);
    CHECK_EQ(lot.at_path<"$.cars[].engine.cc">(i), c.engine.cc);
    CHECK_EQ(lot.at_path<"$.cars[].engine.horsepower">(i), c.engine.horsepower);
    auto const note = *lot.at_path<"$.cars[].note">(i);
    CHECK_EQ(note.has_value(), c.note.has_value());
    if (note && c.note)
        CHECK_EQ(*note, *c.note);
    auto const opened = lot.at_path<"$.cars[]">(i);
    REQUIRE(opened.has_value());
    auto const &car = *opened;
    CHECK_EQ(text_of(car.at_path<"@.make">()), c.make);
    CHECK_EQ(car.at_path<"@.engine.cc">(), c.engine.cc);
    CHECK_EQ(car.at_path<"@.wheels">()->size(), c.wheels.size());
    CHECK_EQ(text_of(lot.at_path<"$.cars[].code">(i)), std::string_view(c.code, 2));
    for (std::size_t k = c.wheels.size() + 1; k-- > 0;) {
        CAPTURE(k);
        if (k == c.wheels.size()) {
            CHECK_EQ(lot.at_path<"$.cars[].wheels[].diameter">(i, k).error(), error::index_out_of_bounds);
            continue;
        }
        CHECK_EQ(lot.at_path<"$.cars[].wheels[].diameter">(i, k), c.wheels[k].diameter);
        CHECK_EQ(lot.at_path<"$.cars[].wheels[].airPressure">(i, k), c.wheels[k].airPressure);
        CHECK_EQ(lot.at_path<"$.cars[].wheels[].snowTires">(i, k), c.wheels[k].snowTires);
        auto const wheel = car.at_path<"@.wheels[]">(k);
        REQUIRE(wheel.has_value());
        CHECK_EQ(wheel->at_path<"@.airPressure">(), c.wheels[k].airPressure);
        CHECK_EQ(car.at_path<"@.wheels[].diameter">(k), c.wheels[k].diameter);
    }
}

template <class D, cbor::fixed_string Path>
concept lot_reads = requires(D &d) { d.template at_path<Path>(0uz); };

template <class D, cbor::fixed_string Path>
concept temporary_lot_reads = requires(D &&d) { std::move(d).template at_path<Path>(0uz); };

template <class D>
concept temporary_car_reads = requires(D const &d) { (*d.template at_path<"$.cars[]">(0uz)).template at_path<"@.make">(); };

} // namespace

// An accessor reads each field of a list of several cars as decode reads it, in order, in the reverse order and in
// a shuffled order. The test exists because a read that goes back must not take a value of another car.
TEST_CASE("path: an accessor gives the values of decode in every order")
{
    std::string const bytes = schema_bytes(sample_lot());
    auto const decoded = *cbor::schema<parking_lot>::decode(bytes);
    parking_lot const &back = *decoded;
    auto lot = *cbor::schema<parking_lot>::path(bytes);
    std::vector<std::size_t> order(back.cars.size());
    std::ranges::iota(order, 0uz);
    for (std::size_t const i : order)
        lot_car_matches(lot, back, i);
    for (std::size_t const i : order | std::views::reverse)
        lot_car_matches(lot, back, i);
    std::ranges::shuffle(order, std::mt19937(5));
    for (std::size_t const i : order)
        lot_car_matches(lot, back, i);
    CHECK_EQ(lot.at_path<"$.cars[].seats">(back.cars.size()).error(), error::index_out_of_bounds);
    CHECK_EQ(text_of(lot.at_path<"$.notes[]">(2uz)), "third"sv);
    CHECK_EQ(text_of(lot.at_path<"$.notes[]">(0uz)), "first"sv);
    CHECK_EQ(lot.at_path<"$.rows[][]">(2uz, 0uz), 3u);
    CHECK_EQ(lot.at_path<"$.rows[][]">(0uz, 1uz), 2u);
    CHECK_EQ(lot.at_path<"$.rows[][]">(1uz, 0uz).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<parking_lot, "$.cars[].wheels[].diameter">(bytes, 3uz, 0uz), back.cars[3].wheels[0].diameter);
    CHECK_EQ(at_path<parking_lot, "$.cars[].model">(bytes, 3uz), "mmm"s);
}

// An accessor is read through an lvalue. The test exists because a text and a nested accessor borrow the bytes of
// the root, and a read through a temporary root would be the only use of a root that ends at once.
TEST_CASE("path: an accessor is not read through a temporary")
{
    using D = cbor::schema<parking_lot>::accessor<>;
    CHECK(lot_reads<D, "$.cars[].seats">);
    CHECK(lot_reads<D const, "$.cars[].seats">);
    CHECK(lot_reads<D const, "$.cars[]">);
    CHECK_FALSE(temporary_lot_reads<D, "$.cars[].seats">);
    CHECK_FALSE(temporary_lot_reads<D, "$.cars[]">);
    CHECK_FALSE(temporary_car_reads<D>);
    CHECK_THROWS_AS((void)cbor::schema<parking_lot>::path(std::shared_ptr<void const>{}, ""sv), std::logic_error);
}

// The copying form keeps its copy: a text and a car accessor stay valid after the caller frees the message, while the
// root lives.
TEST_CASE("path: the copying form keeps its copy after the caller frees the message")
{
    auto bytes = std::make_unique<std::string>(schema_bytes(sample_lot()));
    auto lot = *cbor::schema<parking_lot>::path(*bytes);
    auto const make = *lot.at_path<"$.cars[].make">(3uz);
    auto const car = *lot.at_path<"$.cars[]">(4uz);
    bytes.reset();
    CHECK_EQ(make, "make3"sv);
    CHECK_EQ(car.at_path<"@.make">(), "make4"sv);
    CHECK_EQ(car.at_path<"$.cars[].make">(4uz), "make4"sv);
}

// Each prefix and each forged byte of a message of several cars is read through a new root and an accessor of each
// car. The test exists because an accessor that trusted a list it read once would read a later item past the end.
TEST_CASE("attack: an accessor reads nothing outside a cut or forged message of several cars")
{
    std::string const bytes = schema_bytes(sample_lot());
    std::size_t differ = 0;
    auto const other = [](auto const &a, auto const &b) { return a.has_value() != b.has_value() || (a && *a != *b); };
    auto const keep = std::make_shared<int const>(0);
    auto const read = [&differ, other, &keep](std::string_view const m) {
        auto opened = cbor::schema<parking_lot>::path(keep, m);
        std::size_t n = 0;
        if (!opened)
            return n;
        auto &lot = *opened;
        for (std::size_t i = 6; i-- > 0;) {
            auto const make = lot.at_path<"$.cars[].make">(i);
            auto const seats = lot.at_path<"$.cars[].seats">(i);
            auto const pressure = lot.at_path<"$.cars[].wheels[].airPressure">(i, 1uz);
            n += std::size_t{make.has_value()} + std::size_t{seats.has_value()} + std::size_t{lot.at_path<"$.cars[].note">(i).has_value()} + std::size_t{pressure.has_value()};
            auto const car = lot.at_path<"$.cars[]">(i);
            if (!car)
                continue;
            differ += std::size_t{other(car->at_path<"@.make">(), make)} + std::size_t{other(car->at_path<"@.seats">(), seats)};
            auto const wheel = car->at_path<"@.wheels[]">(1uz);
            if (wheel)
                differ += other(wheel->at_path<"@.airPressure">(), pressure);
        }
        n += lot.at_path<"$.notes[]">(1uz).has_value();
        n += lot.at_path<"$.rows[][]">(2uz, 0uz).has_value();
        return n;
    };
    CHECK_EQ(read(bytes), 5u * 3 + 2 + 2);
    std::size_t values = 0;
    for (std::size_t size = 0; size < bytes.size(); ++size)
        values += read(std::string_view(bytes).substr(0, size));
    CHECK_EQ(values, 0u);
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        std::string f = bytes;
        f.at(at) = static_cast<char>(f.at(at) ^ 0x41);
        read(f);
    }
    CHECK_EQ(differ, 0u);
}

// The value category of the bytes says what decode and path do. A moved std::string becomes the owner and is not
// copied; an lvalue and a const rvalue are copied once. The result stays valid after the caller reuses or destroys
// its buffer.
TEST_CASE("decode and path: an rvalue string is moved, everything else is copied")
{
    std::string const name(40, 'n');
    std::string const message = schema_bytes(login{7, true, name});
    auto range = [](std::string_view const inner, char const *const begin, std::size_t const size) {
        return std::less_equal<>{}(begin, inner.data()) && std::less_equal<>{}(inner.data() + inner.size(), begin + size);
    };

    auto buffer = std::make_unique<std::string>(message);
    char const *data = buffer->data();
    auto const moved = cbor::schema<login>::path(std::move(*buffer));
    REQUIRE(moved.has_value());
    auto const moved_name = moved->at_path<"$.name">();
    REQUIRE(moved_name.has_value());
    CHECK(range(*moved_name, data, message.size()));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(*moved_name, name);

    std::string lvalue = message;
    auto const copied = cbor::schema<login>::path(lvalue);
    REQUIRE(copied.has_value());
    auto const copied_name = copied->at_path<"$.name">();
    REQUIRE(copied_name.has_value());
    CHECK_FALSE(range(*copied_name, lvalue.data(), lvalue.size()));
    CHECK_EQ(lvalue, message);
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(*copied_name, name);

    std::string const constant = message;
    auto const from_const = cbor::schema<login>::path(std::move(constant));
    REQUIRE(from_const.has_value());
    CHECK_EQ(constant, message);

    auto decode_buffer = std::make_unique<std::string>(message);
    auto const decoded = cbor::schema<login>::decode(std::move(*decode_buffer));
    decode_buffer.reset();
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->name, name);
    auto const from_lvalue = cbor::schema<login>::decode(lvalue = message);
    REQUIRE(from_lvalue.has_value());
    CHECK_EQ((*from_lvalue)->id, 7u);
    auto const decoded_const = cbor::schema<login>::decode(std::move(constant));
    REQUIRE(decoded_const.has_value());
    CHECK_EQ((*decoded_const)->id, 7u);
    auto const from_view = cbor::schema<login>::decode(std::string_view(message));
    REQUIRE(from_view.has_value());
    CHECK_EQ((*from_view)->id, 7u);
    char const *const empty = "";
    CHECK_EQ(cbor::schema<login>::decode(empty).error(), error::too_little_data);
    CHECK_EQ(cbor::schema<login>::path("").error(), error::too_little_data);
}

#endif
