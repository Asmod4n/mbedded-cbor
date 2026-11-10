#include "../owned_copy.hpp"
#include "../reference/value.hpp"
#include <cbor/decode.hpp>
#include <cbor/error.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace std::string_literals;
using cbor::error;

namespace lazy_typed_array_length
{
namespace
{

// A typed array whose byte string head claims 2^32 + 4 bytes and holds 4: on a target with a 32-bit size_t the
// length does not fit a size_t, and its value modulo 2^32 is the 4 bytes that follow.
std::string typed_array_claiming_more(char const tag)
{
    return "\xd8"s + tag + "\x5b\x00\x00\x00\x01\x00\x00\x00\x04\x01\x02\x03\x04"s;
}

void refuses(char const tag, error const expected)
{
    auto const l = test::lazy_of_copy(typed_array_claiming_more(tag));
    REQUIRE(l.has_value());
    CHECK_EQ(l->elements().error(), expected);
    CHECK_EQ(l->at(std::size_t{0}).error(), expected);
    auto const q = cbor::path::compile<cbor::lazy>("$[0]");
    REQUIRE(q.has_value());
    CHECK_EQ(l->at_path(*q).error(), expected);
}

// RFC 8746 2.1, Tables 1 and 2: the number of bytes of one element of each typed array tag from 64 to 87. Tag
// 76 is reserved and has no element size; 0 stands for it.
constexpr std::array<std::size_t, 24> element_bytes{1, 2, 4, 8, 1, 2, 4, 8,  1, 2, 4, 8,
                                                    0, 2, 4, 8, 2, 4, 8, 16, 2, 4, 8, 16};

constexpr std::uint64_t typed_array_first = 64;
constexpr std::uint64_t typed_array_last = 87;
constexpr std::uint64_t typed_array_reserved = 76;

// A tag head with a one-byte argument and a byte string of length bytes, each byte 1 (RFC 8949 3).
std::string tagged_byte_string(std::uint64_t const tag, std::size_t const length)
{
    constexpr std::size_t one_byte_argument_min = 24;
    constexpr std::size_t byte_string_initial_byte = 0x40;
    std::string bytes = "\xd8"s + static_cast<char>(tag);
    if (length < one_byte_argument_min)
        bytes += static_cast<char>(byte_string_initial_byte | length);
    else
        bytes += static_cast<char>(byte_string_initial_byte | one_byte_argument_min) +
                 std::string(1, static_cast<char>(length));
    return bytes + std::string(length, '\x01');
}

} // namespace
} // namespace lazy_typed_array_length

using namespace lazy_typed_array_length;

// A regression check for 32-bit targets. RFC 8949 3: the argument of a byte string is its length, here more than the
// input holds. RFC 8746 2: the length is a whole number of elements. The check of the elements takes the full
// 64-bit length, so every target gives the error of the full length: for tag 64 (uint8, one byte) the length is a
// whole number of elements and the bytes are too few; for tag 86 (float64 little endian, eight bytes) 2^32 + 4 is
// no whole number of elements.
TEST_CASE("lazy typed array length: a length past the input that a 32-bit size_t does not hold")
{
    refuses('\x40', error::too_little_data);
    refuses('\x56', error::inadmissible_type_for_tag_content);
}

// RFC 8746 2: the byte string of a typed array holds a whole number of elements, and the element size comes
// from the tag. For each typed array tag and every length up to two elements of the widest type, so that
// every remainder of every element size is met, lazy gives the elements and cbor::decode reads the item, or
// both refuse the length. A tag around the range and the reserved tag 76 are tags that the library does not
// interpret: a lazy has no elements in them, and cbor::decode reads them as tags.
TEST_CASE("lazy typed array length: the element size of each tag and every length up to 32 bytes")
{
    constexpr std::uint64_t tags_before = 4;
    constexpr std::uint64_t tags_after = 3;
    constexpr std::size_t longest = 32;
    for (std::uint64_t tag = typed_array_first - tags_before; tag <= typed_array_last + tags_after; ++tag)
        for (std::size_t length = 0; length <= longest; ++length) {
            CAPTURE(tag);
            CAPTURE(length);
            std::string const bytes = tagged_byte_string(tag, length);
            auto const l = test::lazy_of_copy(bytes);
            REQUIRE(l.has_value());
            auto const elements = l->elements();
            auto const tree = cbor::decode(bytes);
            if (tag < typed_array_first || tag > typed_array_last || tag == typed_array_reserved) {
                CHECK_EQ(elements.error(), error::not_indexable);
                CHECK(tree.has_value());
                continue;
            }
            std::size_t const size = element_bytes[static_cast<std::size_t>(tag - typed_array_first)];
            if (length % size != 0) {
                CHECK_EQ(elements.error(), error::inadmissible_type_for_tag_content);
                CHECK_EQ(tree.error(), error::inadmissible_type_for_tag_content);
            } else {
                CHECK(elements->size() == length / size);
                CHECK(tree.has_value());
            }
        }
}
