#include "../owned_copy.hpp"
#include "../reference/value.hpp"
#include <cbor/error.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>

#include <cstddef>
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
