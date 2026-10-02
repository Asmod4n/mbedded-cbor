#include "host.hpp"

#if __cpp_impl_reflection

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdfloat>
#include <string>
#include <vector>

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
    CHECK_EQ(cbor::fixed_size<std::span<std::uint8_t, 2>>(), 5u);
    CHECK_EQ(cbor::fixed_size<std::uint32_t[2]>(), 11u);
}

// A struct is a map: its head, then for each member a text key and the fixed value. wheel: a1 head, "diameter"
// 9 + 3, "airPressure" 12 + 5, "snowTires" 10 + 1.
TEST_CASE("fixed_size: a struct is a map of its members")
{
    CHECK_EQ(cbor::fixed_size<wheel>(), 41u);
    CHECK_EQ(cbor::fixed_size<std::array<wheel, 4>>(), 1u + 4u * 41u);
}

// A part of variable size lives in the second item. The first item holds 82 1a <offset> 1a <length>.
TEST_CASE("fixed_size: a part of variable size is a reference of 11 bytes")
{
    CHECK_EQ(cbor::fixed_size<std::string>(), 11u);
    CHECK_EQ(cbor::fixed_size<std::vector<wheel>>(), 11u);
    CHECK_EQ(cbor::fixed_size<std::map<int, int>>(), 11u);
    CHECK_EQ(cbor::fixed_size<std::optional<int>>(), 11u);
    CHECK_EQ(cbor::fixed_size<std::span<std::uint8_t>>(), 11u);
}

#endif
