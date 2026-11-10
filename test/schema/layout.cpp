#include "types.hpp"

// Each kind of field has the size that RFC 8949 3 gives its head plus its argument, so a reader finds every
// field at an offset the compiler knows. The expectations are counted from the specification by hand.
TEST_CASE("fixed_size: numbers and simple values have the size of head and argument")
{
    // 0xf4 or 0xf5, one initial byte (RFC 8949 3.3).
    CHECK_EQ(form<wheel>::fixed_size<bool>(), 1u);
    CHECK_EQ(form<wheel>::fixed_size<bool const>(), 1u);
    // An integer of n bytes is 0x18 + log2(n) followed by n bytes (RFC 8949 3).
    CHECK_EQ(form<wheel>::fixed_size<std::uint8_t>(), 2u);
    CHECK_EQ(form<wheel>::fixed_size<std::int16_t>(), 3u);
    CHECK_EQ(form<wheel>::fixed_size<std::uint32_t>(), 5u);
    CHECK_EQ(form<wheel>::fixed_size<std::int64_t>(), 9u);
    // An enum has the size of its underlying type.
    CHECK_EQ(form<wheel>::fixed_size<color>(), 3u);
    CHECK_EQ(form<wheel>::fixed_size<std::byte>(), 2u);
#ifdef CBOR_HAS_INT128
    // A 128-bit integer is tag 2 or 3 (one byte) and a byte string of 16 bytes (RFC 8949 3.4.3).
    CHECK_EQ(form<wheel>::fixed_size<cbor::int128>(), 18u);
    CHECK_EQ(form<wheel>::fixed_size<cbor::uint128>(), 18u);
#endif
}

// RFC 8949 3.3 has binary16, binary32 and binary64. binary128 is a typed array of one element under tag 83
// (RFC 8746 2.1): two bytes of tag, one byte of byte string head, 16 bytes. The 80-bit extended precision of
// x87 is widened to binary128, and bfloat16 to binary32, without loss.
TEST_CASE("fixed_size: floats by their digits")
{
#if defined(__STDCPP_FLOAT16_T__)
    CHECK_EQ(form<wheel>::fixed_size<std::float16_t>(), 3u);
#endif
#if defined(__STDCPP_BFLOAT16_T__)
    CHECK_EQ(form<wheel>::fixed_size<std::bfloat16_t>(), 5u);
#endif
    CHECK_EQ(form<wheel>::fixed_size<float>(), 5u);
    CHECK_EQ(form<wheel>::fixed_size<double>(), 9u);
#if defined(__STDCPP_FLOAT128_T__)
    CHECK_EQ(form<wheel>::fixed_size<std::float128_t>(), 19u);
#endif
#if defined(__STDCPP_FLOAT128_T__)
    CHECK_EQ(form<wheel>::fixed_size<long double>(), std::numeric_limits<long double>::digits == 53 ? 9u : 19u);
#endif
}

// A text or a byte string of fixed length has its head and its bytes. An array of fixed length has its head and
// its elements, each of fixed size.
TEST_CASE("fixed_size: text, bytes and arrays of fixed length")
{
    CHECK_EQ(form<wheel>::fixed_size<char[32]>(), 34u);
    CHECK_EQ(form<wheel>::fixed_size<char8_t[3]>(), 4u);
    CHECK_EQ(form<wheel>::fixed_size<std::array<char, 24>>(), 26u);
    CHECK_EQ(form<wheel>::fixed_size<std::byte[4]>(), 5u);
    CHECK_EQ(form<wheel>::fixed_size<unsigned char[300]>(), 303u);
    CHECK_EQ(form<wheel>::fixed_size<std::array<std::uint16_t, 4>>(), 13u);
    // std::uint8_t is unsigned char, so a span of it is a byte string.
    CHECK_EQ(form<wheel>::fixed_size<std::span<std::uint8_t, 2>>(), 3u);
    CHECK_EQ(form<wheel>::fixed_size<std::span<std::uint16_t, 2>>(), 7u);
    CHECK_EQ(form<wheel>::fixed_size<std::uint32_t[2]>(), 11u);
}

// draft-ietf-cbor-packed-19 4.2: a struct is a record, its values behind a straight reference to the table entry
// that holds its keys. The tag of the class stands outside the reference; tag 1500 is d9 05 dc in preferred
// serialization (RFC 8949 3.4 and 4.1). The reference is d8 80 to d8 87, the value array has a head of 9a and 4
// bytes, so 10 bytes come before the values. wheel: 10, then 3 + 5 + 1.
TEST_CASE("fixed_size: a struct is a record of its values under the tag of its class")
{
    CHECK_EQ(form<wheel>::fixed_size(), 19u);
    CHECK_EQ(form<wheel>::fixed_size<std::array<wheel, 4>>(), 1u + 4u * 19u);
}

// A part of variable size is a shared item in the table of tag 113. The record holds the reference c6 1a <N> or
// c6 3a <N>: tag 6 with an argument of fixed width (draft-ietf-cbor-packed-19 2.2), so 6 bytes.
TEST_CASE("fixed_size: a part of variable size takes 6 bytes")
{
    CHECK_EQ(form<wheel>::fixed_size<std::string>(), 6u);
    CHECK_EQ(form<wheel>::fixed_size<std::vector<wheel>>(), 6u);
    CHECK_EQ(form<wheel>::fixed_size<std::map<int, int>>(), 6u);
    CHECK_EQ(form<wheel>::fixed_size<std::optional<int>>(), 6u);
    CHECK_EQ(form<wheel>::fixed_size<std::span<std::uint8_t>>(), 6u);
}

// The offset points at the head of a value. The keys are in the table, so the 3 bytes of the class tag and the 7
// bytes of the record head come first. The values follow the key order of RFC 8949 4.2.1, a shorter key first:
// diameter (68 ...) at 10, snowTires (69 ...) behind its 3 bytes at 13, airPressure (6b ...) behind 1 byte at 14.
TEST_CASE("member_offset: the head of each value, behind the class tag and the head of the record")
{
    CHECK_EQ(form<wheel>::member_offset<&wheel::diameter>(), 10u);
    CHECK_EQ(form<wheel>::member_offset<&wheel::snowTires>(), 13u);
    CHECK_EQ(form<wheel>::member_offset<&wheel::airPressure>(), 14u);
    CHECK_EQ(form<wheel const>::member_offset<&wheel::airPressure>(), 14u);
}

// A nested struct is a record at the offset of its member, so offsets add up. car in key order: motor (65 6d ...)
// at 10, seats (65 73 ...) behind the engine record, hasNavSystem (6c ...) behind the 2 bytes of seats. engine in key
// order: cc (62 ...) at 10, horsepower (6a ...) behind its 5 bytes at 15.
TEST_CASE("member_offset: offsets of nested structs add up")
{
    CHECK_EQ(form<car>::member_offset<&car::motor>(), 10u);
    CHECK_EQ(form<engine>::member_offset<&engine::cc>(), 10u);
    CHECK_EQ(form<engine>::member_offset<&engine::horsepower>(), 15u);
    CHECK_EQ(form<car>::member_offset<&car::seats>(), 10u + form<engine>::fixed_size());
    CHECK_EQ(form<car>::member_offset<&car::hasNavSystem>(), 12u + form<engine>::fixed_size());
}

// The count of values has a head of 9a and 4 bytes whatever the count, so a member added later moves no offset, and
// a long name or a name in UTF-8 takes no place in the record.
TEST_CASE("member_offset: the record head has one width for any count and any name")
{
    CHECK_EQ(form<thirty>::member_offset<&thirty::m00>(), 10u);
    CHECK_EQ(form<thirty>::member_offset<&thirty::m29>(), 10u + 29u * 2u);
    CHECK_EQ(form<thirty>::fixed_size(), 10u + 30u * 2u);
    CHECK_EQ(form<long_name>::member_offset<&long_name::b>(), 10u);
    CHECK_EQ(form<long_name>::member_offset<&long_name::a_member_name_of_twenty_nine_b>(), 12u);
    CHECK_EQ(form<empty>::fixed_size(), 10u);
    CHECK_EQ(form<utf8_key>::member_offset<&utf8_key::b>(), 10u);
    CHECK_EQ(form<utf8_key>::member_offset<&utf8_key::height>(), 12u);
}

// A variant member is a reference field like any part of variable size: tag 6 and an argument of 4 bytes.
TEST_CASE("fixed_size: a variant member takes 6 bytes")
{
    CHECK_EQ(form<home>::fixed_size<std::variant<dog, cat>>(), 6u);
    CHECK_EQ(form<home>::member_offset<&home::street>() - form<home>::member_offset<&home::pet>(), 6u);
}
