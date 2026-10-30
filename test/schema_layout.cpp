#include "reference.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

using namespace std::string_literals;

namespace
{

struct spot {
    float x;
    float y;
    std::uint32_t id;
};

struct flipped {
    float y;
    float x;
};

struct nest {
    flipped f;
    std::uint16_t n;
};

struct gap {
    std::uint8_t a;
    std::uint32_t b;
};

struct preset {
    std::uint32_t a = 7;
    float b = -0.0f;
};

struct mixed {
    std::uint32_t a;
    float b;
    std::array<std::int16_t, 2> c;
    bool d;
    std::uint8_t e[3];
};

template <class E>
struct in_vector {
    std::vector<E> v;
};

template <class E>
struct in_deque {
    std::deque<E> v;
};

} // namespace

CBOR_DEFINE_TYPE(spot, cbor::tag{1901}, CBOR_MEMBER(spot, x), CBOR_MEMBER(spot, y), CBOR_MEMBER(spot, id));
CBOR_DEFINE_TYPE(flipped, cbor::tag{1902}, CBOR_MEMBER(flipped, y), CBOR_MEMBER(flipped, x));
CBOR_DEFINE_TYPE(nest, cbor::tag{1903}, CBOR_MEMBER(nest, f), CBOR_MEMBER(nest, n));
CBOR_DEFINE_TYPE(gap, cbor::tag{1904}, CBOR_MEMBER(gap, a), CBOR_MEMBER(gap, b));
CBOR_DEFINE_TYPE(preset, cbor::tag{1905}, CBOR_MEMBER(preset, a), CBOR_MEMBER(preset, b));
CBOR_DEFINE_TYPE(mixed, cbor::tag{1906}, CBOR_MEMBER(mixed, a), CBOR_MEMBER(mixed, b), CBOR_MEMBER(mixed, c),
                 CBOR_MEMBER(mixed, d), CBOR_MEMBER(mixed, e));

#define CBOR_LAYOUT_LISTS(E, VECTOR_TAG, DEQUE_TAG)                                                                      \
    CBOR_DEFINE_TYPE(in_vector<E>, cbor::tag{VECTOR_TAG}, CBOR_MEMBER(in_vector<E>, v));                               \
    CBOR_DEFINE_TYPE(in_deque<E>, cbor::tag{DEQUE_TAG}, CBOR_MEMBER(in_deque<E>, v))

CBOR_LAYOUT_LISTS(spot, 1911, 1912);
CBOR_LAYOUT_LISTS(nest, 1913, 1914);
CBOR_LAYOUT_LISTS(gap, 1915, 1916);
CBOR_LAYOUT_LISTS(preset, 1917, 1918);
CBOR_LAYOUT_LISTS(mixed, 1919, 1920);

namespace
{

// The root of each message is its class tag, a head of two bytes after d9: tag 1911 is 07 77, 1912 is 07 78.
std::string root_tag(std::uint16_t const tag)
{
    return "\xd9"s + static_cast<char>(tag >> 8) + static_cast<char>(tag & 0xff);
}

// A std::vector of a fixed-width struct whose layout in memory is the layout on the wire is written with one copy; a
// std::deque is not contiguous and is written element by element. Both give the same bytes but for the class tag of
// the root, and both read back the same values.
template <class E>
void check_both_writes_agree(std::vector<E> const &values, std::uint16_t const vector_tag, std::uint16_t const deque_tag)
{
    auto const copied = cbor::schema<in_vector<E>>::encode(in_vector<E>{values});
    auto const each = cbor::schema<in_deque<E>>::encode(in_deque<E>{std::deque<E>(values.begin(), values.end())});
    REQUIRE(copied.has_value());
    REQUIRE(each.has_value());
    std::string expected = *copied;
    std::size_t const at = expected.rfind(root_tag(vector_tag));
    REQUIRE_NE(at, std::string::npos);
    expected.replace(at, 3, root_tag(deque_tag));
    CHECK_EQ(*each, expected);
    auto const back = cbor::schema<in_vector<E>>::decode(std::string(*copied));
    REQUIRE(back.has_value());
    CHECK_EQ(cbor::schema<in_vector<E>>::encode(**back), *copied);
}

} // namespace

// The check of the layout sets the bytes of one member and reads the member back. It must give false for a struct
// with padding, for an inner struct in another key order and for members in another order than the keys, and true
// with nonzero default member initializers; a false result gives the element-by-element write, a wrong true result
// gives other bytes than the deque.
TEST_CASE("schema layout: a list written with one copy gives the bytes of the element-by-element write")
{
    check_both_writes_agree<spot>({{1.0f, 0.5f, 7}, {2.0f, -1.0f, 8}}, 1911, 1912);
    check_both_writes_agree<nest>({{{1.0f, 2.0f}, 3}, {{4.0f, 5.0f}, 6}}, 1913, 1914);
    check_both_writes_agree<gap>({{1, 2}, {3, 4}}, 1915, 1916);
    check_both_writes_agree<preset>({{1, 2.0f}, {}}, 1917, 1918);
    check_both_writes_agree<mixed>({{1, 2.0f, {3, 4}, true, {5, 6, 7}}, {8, 9.0f, {10, 11}, false, {12, 13, 14}}}, 1919, 1920);
}

#ifdef __cpp_impl_reflection

// member_offset takes a reflection of a member or a member pointer; both name the same offset.
TEST_CASE("schema layout: a reflection and a member pointer give the same offset")
{
    CHECK_EQ(cbor::schema<spot>::member_offset<^^spot::id>(), cbor::schema<spot>::member_offset<&spot::id>());
    CHECK_EQ(cbor::schema<nest>::member_offset<^^nest::n>(), cbor::schema<nest>::member_offset<&nest::n>());
    CHECK_EQ((cbor::schema<nest>::member_offset<^^flipped::x, flipped>()),
             (cbor::schema<nest>::member_offset<&flipped::x, flipped>()));
}

#endif
