#include "reference.hpp"
#include "schema_golden.hpp"


#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#if __has_include(<stdfloat>)
#include <stdfloat>
#endif
#include <stdexcept>
#include <tuple>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

template <class X>
using form = cbor::schema<X>;

} // namespace

namespace
{

enum class color : std::uint16_t { black, white };

struct wheel {
    std::uint16_t diameter;
    float airPressure;
    bool snowTires;
};

} // namespace

template <>
struct cbor::describe_members<wheel> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1500};
    static constexpr std::tuple members{
        CBOR_MEMBER(wheel, diameter),
        CBOR_MEMBER(wheel, airPressure),
        CBOR_MEMBER(wheel, snowTires),
    };
};

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
#ifdef __SIZEOF_INT128__
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

template <>
struct cbor::describe_members<engine> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1501};
    static constexpr std::tuple members{
        CBOR_MEMBER(engine, horsepower),
        CBOR_MEMBER(engine, cc),
    };
};

template <>
struct cbor::describe_members<car> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1502};
    static constexpr std::tuple members{
        CBOR_MEMBER(car, seats),
        CBOR_MEMBER(car, motor),
        CBOR_MEMBER(car, hasNavSystem),
    };
};

template <>
struct cbor::describe_members<empty> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1503};
    static constexpr std::tuple<> members{};
};

template <>
struct cbor::describe_members<thirty> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1504};
    static constexpr std::tuple members{
        CBOR_MEMBER(thirty, m00),
        CBOR_MEMBER(thirty, m01),
        CBOR_MEMBER(thirty, m02),
        CBOR_MEMBER(thirty, m03),
        CBOR_MEMBER(thirty, m04),
        CBOR_MEMBER(thirty, m05),
        CBOR_MEMBER(thirty, m06),
        CBOR_MEMBER(thirty, m07),
        CBOR_MEMBER(thirty, m08),
        CBOR_MEMBER(thirty, m09),
        CBOR_MEMBER(thirty, m10),
        CBOR_MEMBER(thirty, m11),
        CBOR_MEMBER(thirty, m12),
        CBOR_MEMBER(thirty, m13),
        CBOR_MEMBER(thirty, m14),
        CBOR_MEMBER(thirty, m15),
        CBOR_MEMBER(thirty, m16),
        CBOR_MEMBER(thirty, m17),
        CBOR_MEMBER(thirty, m18),
        CBOR_MEMBER(thirty, m19),
        CBOR_MEMBER(thirty, m20),
        CBOR_MEMBER(thirty, m21),
        CBOR_MEMBER(thirty, m22),
        CBOR_MEMBER(thirty, m23),
        CBOR_MEMBER(thirty, m24),
        CBOR_MEMBER(thirty, m25),
        CBOR_MEMBER(thirty, m26),
        CBOR_MEMBER(thirty, m27),
        CBOR_MEMBER(thirty, m28),
        CBOR_MEMBER(thirty, m29),
    };
};

template <>
struct cbor::describe_members<long_name> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1505};
    static constexpr std::tuple members{
        CBOR_MEMBER(long_name, a_member_name_of_twenty_nine_b),
        CBOR_MEMBER(long_name, b),
    };
};

template <>
struct cbor::describe_members<größe> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1506};
    static constexpr std::tuple members{
        CBOR_MEMBER(größe, höhe),
        CBOR_MEMBER(größe, b),
    };
};

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
    CHECK_EQ(form<größe>::member_offset<&größe::b>(), 10u);
    CHECK_EQ(form<größe>::member_offset<&größe::höhe>(), 12u);
}



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

struct pmr_named {
    std::pmr::string n;
};

struct pmr_people {
    std::pmr::vector<pmr_named> people;
};

} // namespace

template <>
struct cbor::describe_members<login> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1507};
    static constexpr std::tuple members{
        CBOR_MEMBER(login, id),
        CBOR_MEMBER(login, ok),
        CBOR_MEMBER(login, name),
    };
};

template <>
struct cbor::describe_members<measures> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1508};
    static constexpr std::tuple members{
        CBOR_MEMBER(measures, t),
        CBOR_MEMBER(measures, f),
        CBOR_MEMBER(measures, v),
        CBOR_MEMBER(measures, o),
    };
};

template <>
struct cbor::describe_members<named> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1509};
    static constexpr std::tuple members{
        CBOR_MEMBER(named, n),
    };
};

template <>
struct cbor::describe_members<people> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1510};
    static constexpr std::tuple members{
        CBOR_MEMBER(people, people),
    };
};

template <>
struct cbor::describe_members<pmr_named> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1533};
    static constexpr std::tuple members{
        CBOR_MEMBER(pmr_named, n),
    };
};

template <>
struct cbor::describe_members<pmr_people> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1534};
    static constexpr std::tuple members{
        CBOR_MEMBER(pmr_people, people),
    };
};

namespace
{

class counting_resource : public std::pmr::memory_resource
{
    void *do_allocate(std::size_t const bytes, std::size_t const alignment) override
    {
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void *const p, std::size_t const bytes, std::size_t const alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(std::pmr::memory_resource const &other) const noexcept override
    {
        return this == &other;
    }

public:
    std::size_t allocations = 0;
};

std::string schema_bytes(auto const &value)
{
    string_writer w;
    REQUIRE(form<std::remove_cvref_t<decltype(value)>>::encode(value, w).has_value());
    return w.encoded;
}

// The tests of one path open the message with the copying form of path and read one leaf. A text is copied into a
// std::string, because the copy that path made ends with this function.
template <class T, cbor::fixed_string Path, class... Index>
auto at_path(std::string_view const bytes, Index const... indexes)
{
    using X = std::remove_cvref_t<decltype(*std::declval<typename form<T>::template accessor<> const &>().template at_path<Path>(indexes...))>;
    using Y = std::conditional_t<std::same_as<X, std::string_view>, std::string,
                                 std::conditional_t<std::same_as<X, std::optional<std::string_view>>, std::optional<std::string>, X>>;
    auto const root = form<T>::path(std::string(bytes));
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

// The view form: the caller keeps the owner alive, so a text stays a view into the message.
template <class T, cbor::fixed_string Path, class... Index>
auto at_path(std::shared_ptr<std::string const> const &owner, Index const... indexes)
    -> decltype(std::declval<typename form<T>::template accessor<> const &>().template at_path<Path>(indexes...))
{
    auto const root = form<T>::path(owner);
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

struct unsorted {
    std::int8_t zz;
    std::int8_t b;
    std::int8_t aa;
};

template <>
struct cbor::describe_members<unsorted> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1620};
    static constexpr std::tuple members{
        CBOR_MEMBER(unsorted, zz),
        CBOR_MEMBER(unsorted, b),
        CBOR_MEMBER(unsorted, aa),
    };
};


struct unsorted_other_order {
    std::int8_t aa;
    std::int8_t zz;
    std::int8_t b;
};

template <>
struct cbor::describe_members<unsorted_other_order> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1620};
    static constexpr std::tuple members{
        CBOR_MEMBER(unsorted_other_order, aa),
        CBOR_MEMBER(unsorted_other_order, zz),
        CBOR_MEMBER(unsorted_other_order, b),
    };
};


struct renamed {
    std::int8_t last;
    std::int8_t first;
};

template <>
struct cbor::describe_members<renamed> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1621};
    static constexpr std::tuple members{
        cbor::member{"a", &renamed::last},
        CBOR_MEMBER(renamed, first),
    };
};


// packed is deterministic: RFC 8949 4.2.1 sorts the keys of a map "in the bytewise lexicographic order of their
// deterministic encodings", so a shorter key comes first. The encodings are 61 62 ("b"), 62 61 61 ("aa") and
// 62 7a 7a ("zz"); the key list of tag 114 and the values of the record follow that order, whatever the order of
// declaration. The bytes are written from the RFC by hand.
TEST_CASE("encode: members stand in the key order of RFC 8949 4.2.1, not in the order of declaration")
{
    std::string const bytes = schema_bytes(unsorted{3, 1, 2});
    CHECK_NE(bytes.find("\xd8\x72\x83\x61" "b\x62" "aa\x62zz"s), std::string::npos);
    CHECK_NE(bytes.find("\xd9\x06\x54\xd8\x80\x9a\x00\x00\x00\x03\x18\x01\x18\x02\x18\x03"s), std::string::npos);
    CHECK_EQ(at_path<unsorted, "$.zz">(bytes), 3);
    CHECK_EQ(at_path<unsorted, "$.b">(bytes), 1);
    CHECK_EQ(at_path<unsorted, "$.aa">(bytes), 2);
}

struct listed_other_order {
    std::int8_t zz;
    std::int8_t b;
    std::int8_t aa;
};

template <>
struct cbor::describe_members<listed_other_order> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1620};
    static constexpr std::tuple members{
        CBOR_MEMBER(listed_other_order, aa),
        CBOR_MEMBER(listed_other_order, zz),
        CBOR_MEMBER(listed_other_order, b),
    };
};

// The description lists aa, zz, b: neither the order of declaration (zz, b, aa) nor the key order (b, aa, zz).
// The test exists because the key order alone decides the bytes, so the order of the list changes nothing.
TEST_CASE("encode: the order of the description does not change the bytes")
{
    CHECK_EQ(schema_bytes(listed_other_order{3, 1, 2}), schema_bytes(unsorted{3, 1, 2}));
    CHECK_EQ(at_path<listed_other_order, "$.zz">(schema_bytes(listed_other_order{3, 1, 2})), 3);
}

// The same members declared in another order give the same bytes, so a peer gets one encoding for one value.
TEST_CASE("encode: the order of declaration does not change the bytes")
{
    CHECK_EQ(schema_bytes(unsorted{3, 1, 2}), schema_bytes(unsorted_other_order{2, 3, 1}));
}

// A key that the description gives takes part in the order: "a" (61 61) comes before "first" (65 ...).
TEST_CASE("encode: a key given in cbor::describe_members is sorted by that key")
{
    std::string const bytes = schema_bytes(renamed{7, 9});
    CHECK_NE(bytes.find("\xd8\x72\x82\x61" "a\x65" "first"s), std::string::npos);
    CHECK_EQ(at_path<renamed, "$.a">(bytes), 7);
    CHECK_EQ(at_path<renamed, "$.first">(bytes), 9);
    auto const back = form<renamed>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->last, 7);
    CHECK_EQ((*back)->first, 9);
}

struct sorted_inner {
    std::int8_t y;
    std::int8_t x;
};

template <>
struct cbor::describe_members<sorted_inner> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1622};
    static constexpr std::tuple members{
        CBOR_MEMBER(sorted_inner, y),
        CBOR_MEMBER(sorted_inner, x),
    };
};


struct sorted_outer {
    std::int8_t z;
    sorted_inner a;
    std::int8_t B;
};

template <>
struct cbor::describe_members<sorted_outer> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1623};
    static constexpr std::tuple members{
        CBOR_MEMBER(sorted_outer, z),
        CBOR_MEMBER(sorted_outer, a),
        CBOR_MEMBER(sorted_outer, B),
    };
};


// The order holds at every depth: the outer keys are "B" (61 42), "a" (61 61) and "z"
// (61 7a), keys of one length ordered by their bytes; the inner record holds x before y. A decode gives every value
// back to its member, so the order of the bytes never mixes up members.
TEST_CASE("encode and decode: the key order holds in an inner struct")
{
    sorted_outer const value{5, {2, 1}, 4};
    std::string const bytes = schema_bytes(value);
    check_one_item(bytes);
    CHECK_NE(bytes.find("\xd8\x72\x83\x61" "B\x61" "a\x61z"s), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x72\x82\x61x\x61y"s), std::string::npos);
    CHECK_EQ(at_path<sorted_outer, "$.a.y">(bytes), 2);
    CHECK_EQ(at_path<sorted_outer, "$.a.x">(bytes), 1);
    CHECK_EQ(at_path<sorted_outer, "$.B">(bytes), 4);
    auto const back = form<sorted_outer>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->z, 5);
    CHECK_EQ((*back)->a.y, 2);
    CHECK_EQ((*back)->a.x, 1);
    CHECK_EQ((*back)->B, 4);
    auto const unsorted_back = form<unsorted>::decode(schema_bytes(unsorted{3, 1, 2}));
    REQUIRE(unsorted_back.has_value());
    CHECK_EQ((*unsorted_back)->zz, 3);
    CHECK_EQ((*unsorted_back)->b, 1);
    CHECK_EQ((*unsorted_back)->aa, 2);
}

// A negative int16 is major type 1 with -1 - n in two bytes (RFC 8949 3.1). A float stays binary32 (fa). The values
// stand in the key order of RFC 8949 4.2.1: f, o, t, v. The empty optional is shared item 16, an empty array. The
// list of uint16 is shared item 17, a typed array of RFC 8746: tag 69 (uint16, little endian) = d8 45 over a byte
// string of four bytes. Item 17 is 6(-1) = c6 3a 00 00 00 00, because 2.2 maps a negative N to index 16 - 2N - 1.
// The items start at 46 (2e) and 51 (33). The rump starts with the class tag 1508 = d9 05 e4.
TEST_CASE("encode: signed numbers, floats, a list and an empty optional")
{
    std::string const bytes = schema_bytes(measures{-5, 1.5f, {7, 8}, std::nullopt});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x12\xd8\x72\x84\x61\x66\x61o\x61t\x61v"
                    "\x5a\x00\x00\x00\x08\x00\x00\x00\x2e\x00\x00\x00\x33"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x9a\x00\x00\x00\x00"
                    "\xd8\x45\x5a\x00\x00\x00\x04\x07\x00\x08\x00"
                    "\xd9\x05\xe4\xd8\x80\x9a\x00\x00\x00\x04\xfa\x3f\xc0\x00\x00"
                    "\xc6\x1a\x00\x00\x00\x00\x39\x00\x04\xc6\x3a\x00\x00\x00\x00"s);
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



// path checks the least size of the message and the table of keys once, as decode does. The test exists so that a
// change to either side shows here.
TEST_CASE("path: a message shorter than the least size is too_little_data, a foreign key is incorrect_type")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK_EQ(at_path<login, "$.id">(bytes), 5u);
    CHECK_EQ(at_path<login, "$.id">(std::string_view(bytes).substr(0, 20)).error(), error::too_little_data);
    std::string other = bytes;
    other.replace(8, 2, "ID");
    CHECK_EQ(form<login>::decode(std::string(other)).error(), error::incorrect_type);
    CHECK_EQ(at_path<login, "$.id">(other).error(), error::incorrect_type);
}



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

} // namespace

template <>
struct cbor::describe_members<tire> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1511};
    static constexpr std::tuple members{
        CBOR_MEMBER(tire, diameter),
        CBOR_MEMBER(tire, airPressure),
    };
};

template <>
struct cbor::describe_members<vehicle> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1512};
    static constexpr std::tuple members{
        CBOR_MEMBER(vehicle, make),
        CBOR_MEMBER(vehicle, balance),
        CBOR_MEMBER(vehicle, spare),
        CBOR_MEMBER(vehicle, wheels),
        CBOR_MEMBER(vehicle, motor),
        CBOR_MEMBER(vehicle, code),
        CBOR_MEMBER(vehicle, owner),
        CBOR_MEMBER(vehicle, none),
    };
};

namespace
{

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
    CHECK_FALSE(form<vehicle>::decode(std::string(backward)).has_value());
    CHECK_FALSE(form<vehicle>::decode(std::string(past)).has_value());
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

template <>
struct cbor::describe_members<badge> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1513};
    static constexpr std::tuple members{
        CBOR_MEMBER(badge, mac),
        CBOR_MEMBER(badge, tone),
        CBOR_MEMBER(badge, label),
    };
};

// A fixed-length array of bytes is a byte string laid out inline (RFC 8949 3.1), the same as a fixed text.
// The reader gives the bytes themselves, not an offset and a length read out of them.
TEST_CASE("at_path: a fixed byte array is read inline")
{
    badge const b{{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}, shade::light, "x"};
    std::string const bytes = schema_bytes(b);
    CHECK_EQ(at_path<badge, "$.mac">(bytes), "\x01\x02\x03\x04"s);
    CHECK_EQ(at_path<badge, "$.tone">(bytes), shade::light);
    std::string zeros = bytes;
    std::size_t const mac = bytes.size() - form<badge>::fixed_size() + form<badge>::member_offset<&badge::mac>();
    zeros.replace(mac + 1, 4, "\x00\x00\x00\x00"s);
    CHECK_EQ(at_path<badge, "$.mac">(zeros), "\x00\x00\x00\x00"s);
}



namespace
{

struct numbers {
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

} // namespace

template <>
struct cbor::describe_members<numbers> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1530};
    static constexpr std::tuple members{
        CBOR_MEMBER(numbers, u16),
        CBOR_MEMBER(numbers, u32),
        CBOR_MEMBER(numbers, u64),
        CBOR_MEMBER(numbers, s8),
        CBOR_MEMBER(numbers, s16),
        CBOR_MEMBER(numbers, s32),
        CBOR_MEMBER(numbers, s64),
#if defined(__STDCPP_FLOAT16_T__)
        CBOR_MEMBER(numbers, f16),
#endif
        CBOR_MEMBER(numbers, f32),
        CBOR_MEMBER(numbers, f64),
    };
};

namespace
{

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

struct doubles {
    std::vector<double> v;
};

} // namespace

template <>
struct cbor::describe_members<doubles> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1531};
    static constexpr std::tuple members{
        CBOR_MEMBER(doubles, v),
    };
};

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
    auto const back = form<numbers>::decode(std::string(bytes));
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
    auto const root = form<numbers>::path(std::string(bytes));
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
    REQUIRE(form<doubles>::decode(std::string(bytes)).has_value());
    for (char const tag : {'\x52', '\x55'}) {
        std::string wrong = bytes;
        wrong[at + 1] = tag;
        CHECK_EQ(form<doubles>::decode(std::string(wrong)).error(), error::incorrect_type);
        CHECK_EQ(at_path<doubles, "$.v[0]">(wrong).error(), error::incorrect_type);
    }
    std::string odd = bytes;
    odd[at + 6] = '\x0f';
    CHECK_EQ(form<doubles>::decode(std::string(odd)).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(at_path<doubles, "$.v[0]">(odd).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[at + 6] = '\x18';
    CHECK_EQ(form<doubles>::decode(std::string(longer)).error(), error::too_little_data);
    CHECK_EQ(at_path<doubles, "$.v[0]">(longer).error(), error::too_little_data);
}

// schema::at reads one number from the message with no accessor and no owner, because the number is returned by
// value. It makes every check that path and accessor::at make: the table of keys, the undefined values after the
// directory, the tag of the root, the tag and the length of the typed array, and the index. The expected bits of 1.0
// and 2.0 are 3ff0000000000000 and 4000000000000000 in binary64 (IEEE 754).
TEST_CASE("schema::at: one number of a typed array, with every check of path")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    using S = form<doubles>;
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
    auto const at_compiles = []<class T, cbor::fixed_string Path>() { return requires { form<T>::template at_path<Path>(std::string_view{}); }; };
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
    auto const root = form<numbers>::path(std::string(bytes));
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
// bit in two's complement. at(i) with an index equal to the size is index_out_of_bounds, an error value and no read,
// as std::vector::at gives std::out_of_range; operator[] with that index is a wrong use and throws std::logic_error.
TEST_CASE("view: every typed array gives its elements back, and an index past the end is an error value")
{
    std::string const bytes = schema_bytes(sample_numbers);
    auto check = [&]<cbor::fixed_string Path>(std::uint64_t const expected) {
        auto const v = view_of<Path>(bytes);
        REQUIRE(v.has_value());
        REQUIRE_EQ(v->size(), 1u);
        auto const x = v->at(0);
        REQUIRE(x.has_value());
        CHECK_EQ(bits_of(*x), expected);
        CHECK_EQ(bits_of((*v)[0]), expected);
        CHECK_EQ(v->at(1).error(), error::index_out_of_bounds);
        CHECK_EQ(v->at(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
        CHECK_THROWS_AS((void)(*v)[1], std::logic_error);
        CHECK_THROWS_AS((void)(*v)[std::numeric_limits<std::size_t>::max()], std::logic_error);
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
    auto const root = form<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK_EQ(v->size(), 0u);
    CHECK_EQ(v->at(0).error(), error::index_out_of_bounds);
    CHECK_THROWS_AS((void)(*v)[0], std::logic_error);
}

// view makes no view when the tag is not the one of the element type or the length is not a multiple of the element
// size. Tag 82 is binary64 in big endian and tag 85 is binary32 (RFC 8746). The errors are the same as at_path gives.
TEST_CASE("view: a typed array with a wrong tag or a wrong length gives no view")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    std::size_t const at = bytes.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(at, std::string::npos);
    auto refused = [](std::string const &wrong) {
        auto const root = form<doubles>::path(std::string(wrong));
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
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        kept.emplace(std::move(*v));
    }
    REQUIRE_EQ(kept->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>((*kept)[1]), 0x4000000000000000u);
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
    CHECK((std::random_access_iterator<I>));
    CHECK((std::sized_sentinel_for<I, I>));
    CHECK((std::same_as<std::iter_value_t<I>, double>));
    CHECK((std::same_as<std::iter_reference_t<I>, double>));
    CHECK((std::ranges::random_access_range<V const>));
    CHECK((std::ranges::sized_range<V const>));
    CHECK((std::ranges::common_range<V const>));
    CHECK((std::random_access_iterator<std::ranges::iterator_t<std::ranges::reverse_view<std::ranges::ref_view<V const>>>>));
    CHECK((std::same_as<decltype(std::declval<V const &>().rbegin()), std::reverse_iterator<I>>));
    CHECK((std::same_as<decltype(std::declval<V const &>().cbegin()), I>));
    CHECK((std::same_as<decltype(std::declval<V const &>()[0]), double>));
    CHECK((std::same_as<decltype(std::declval<V const &>().at(0)), std::expected<double, cbor::error>>));
    CHECK((std::same_as<decltype(std::declval<V const &>().front()), double>));
    CHECK((!std::ranges::borrowed_range<V>));
    CHECK((!std::ranges::contiguous_range<V>));
    CHECK((!std::output_iterator<I, double>));
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

// front and back read the first and the last element with one load each, and rbegin and rend walk the view
// backwards, as std::span does. first(n) and last(n) give a sub-view of n elements. n equal to the size gives the
// whole view; n one past it is index_out_of_bounds as an error value, where std::span would have undefined behaviour.
TEST_CASE("view: front, back, first and last")
{
    auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK_EQ(std::bit_cast<std::uint64_t>(v->front()), 0x3ff0000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(v->back()), 0x4010000000000000u);
    std::vector<std::uint64_t> backwards;
    for (auto i = v->rbegin(); i != v->rend(); ++i)
        backwards.push_back(std::bit_cast<std::uint64_t>(*i));
    CHECK(backwards == std::vector<std::uint64_t>{0x4010000000000000u, 0x4000000000000000u, 0x3ff0000000000000u});
    CHECK(v->crbegin() == v->rbegin());
    CHECK(v->cbegin() == v->begin());
    CHECK(v->cend() == v->end());
    auto const head = v->first(2);
    REQUIRE(head.has_value());
    REQUIRE_EQ(head->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(head->back()), 0x4000000000000000u);
    CHECK_EQ(head->at(2).error(), error::index_out_of_bounds);
    auto const tail = v->last(2);
    REQUIRE(tail.has_value());
    REQUIRE_EQ(tail->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(tail->front()), 0x4000000000000000u);
    CHECK_EQ(v->first(3)->size(), 3u);
    CHECK_EQ(v->last(3)->size(), 3u);
    CHECK_EQ(v->first(0)->size(), 0u);
    CHECK_EQ(v->last(0)->size(), 0u);
    CHECK_EQ(v->first(4).error(), error::index_out_of_bounds);
    CHECK_EQ(v->last(4).error(), error::index_out_of_bounds);
    CHECK_EQ(v->first(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
    CHECK_EQ(v->last(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
}

// The requirements of a reversible container in the C++ standard ([container.rev.reqmts]) say: a.rend() is
// reverse_iterator(a.begin()), a.crend() is const_cast<X const&>(a).rend(), and the distance from crbegin to crend is
// the size. The test checks each requirement on a view of three elements and on an empty view, and walks from crend
// back to crbegin, so that the last element read is the first element of the view.
TEST_CASE("view: crend meets the requirements of a reversible container")
{
    using V = cbor::typed_array_view<double>;
    CHECK((std::same_as<decltype(std::declval<V const &>().crend()), std::reverse_iterator<V::iterator>>));
    auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK(v->crend() == std::as_const(*v).rend());
    CHECK(v->crend() == std::reverse_iterator<V::iterator>(v->begin()));
    CHECK(v->crend().base() == v->begin());
    CHECK_EQ(std::distance(v->crbegin(), v->crend()), 3);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*std::ranges::prev(v->crend())), 0x3ff0000000000000u);
    std::vector<std::uint64_t> forwards;
    for (auto i = v->crend(); i != v->crbegin();)
        forwards.push_back(std::bit_cast<std::uint64_t>(*--i));
    CHECK(forwards == std::vector<std::uint64_t>{0x3ff0000000000000u, 0x4000000000000000u, 0x4010000000000000u});
    auto const empty_root = form<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(empty_root.has_value());
    auto const e = empty_root->view<"$.v">();
    REQUIRE(e.has_value());
    CHECK(e->crend() == e->crbegin());
    CHECK_EQ(std::distance(e->crbegin(), e->crend()), 0);
}

// An empty view has no front and no back: each is a wrong use and throws std::logic_error, as the precondition of
// std::span::front is. There is no element to iterate. first(0) and last(0)
// are empty views, and first(1) is out of bounds.
TEST_CASE("view: an empty view has no front, no back and no element")
{
    auto const root = form<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK(v->empty());
    CHECK_THROWS_AS((void)v->front(), std::logic_error);
    CHECK_THROWS_AS((void)v->back(), std::logic_error);
    CHECK(v->rbegin() == v->rend());
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
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        auto c = v->last(2);
        REQUIRE(c.has_value());
        copied.emplace(std::move(*c));
        auto m = std::move(*v).first(1);
        REQUIRE(m.has_value());
        moved.emplace(std::move(*m));
        auto r = form<doubles>::path(schema_bytes(doubles{{8.0}}));
        REQUIRE(r.has_value());
        auto t = std::move(*r).view<"$.v">();
        REQUIRE(t.has_value());
        taken.emplace(std::move(*t));
    }
    REQUIRE_EQ(copied->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(copied->back()), 0x4010000000000000u);
    CHECK_EQ(std::ranges::fold_left(*copied, 0.0, std::plus<>{}), 6.0);
    REQUIRE_EQ(moved->size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(moved->front()), 0x3ff0000000000000u);
    REQUIRE_EQ(taken->size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(taken->front()), 0x4020000000000000u);
}

// A move gives the owner of the bytes to the new object. The moved-from view or accessor must not read the bytes
// after the new owner is gone, because they may be freed. The test drops every other owner and then uses each
// moved-from object, so that the sanitizer sees a read of freed memory if a moved-from object kept its view. A
// moved-from view is empty: operator[], front and back are a wrong use, at is index_out_of_bounds. A use of a
// moved-from accessor is a wrong use. Each case is std::logic_error, as for any other wrong use of the library.
TEST_CASE("view: a moved-from view and a moved-from accessor read no bytes")
{
    using V = cbor::typed_array_view<double>;
    std::optional<V> view;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        view.emplace(std::move(*v));
    }
    {
        V const taken = std::move(*view);
        CHECK_EQ(taken.size(), 2u);
    }
    CHECK(view->empty());
    CHECK_EQ(view->size(), 0u);
    CHECK(view->begin() == view->end());
    CHECK_THROWS_AS((void)(*view)[0], std::logic_error);
    CHECK_THROWS_AS((void)view->front(), std::logic_error);
    CHECK_EQ(view->at(0).error(), error::index_out_of_bounds);

    std::optional<V> sliced;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        sliced.emplace(std::move(*v));
    }
    {
        auto const head = std::move(*sliced).first(1);
        REQUIRE(head.has_value());
        CHECK_EQ(head->size(), 1u);
    }
    CHECK(sliced->empty());
    CHECK_THROWS_AS((void)(*sliced)[0], std::logic_error);

    V assigned = [] {
        auto const root = form<doubles>::path(schema_bytes(doubles{{4.0}}));
        return *root->view<"$.v">();
    }();
    {
        V target = [] {
            auto const root = form<doubles>::path(schema_bytes(doubles{{8.0}}));
            return *root->view<"$.v">();
        }();
        target = std::move(assigned);
        CHECK_EQ(std::bit_cast<std::uint64_t>(target.front()), 0x4010000000000000u);
    }
    CHECK(assigned.empty());
    CHECK_THROWS_AS((void)assigned.back(), std::logic_error);

    auto root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
    REQUIRE(root.has_value());
    {
        auto const taken = std::move(*root);
        CHECK_EQ(std::bit_cast<std::uint64_t>(*taken.at_path<"$.v[0]">()), 0x3ff0000000000000u);
    }
    CHECK_THROWS_AS((void)root->at_path<"$.v[0]">(), std::logic_error);
    CHECK_THROWS_AS((void)root->view<"$.v">(), std::logic_error);

    auto list_root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
    REQUIRE(list_root.has_value());
    auto list = list_root->at_path<"$.v">();
    REQUIRE(list.has_value());
    list_root = std::unexpected(error::incorrect_type);
    {
        auto const taken = std::move(*list);
        CHECK_EQ(std::bit_cast<std::uint64_t>(*taken.at_path<"@[1]">()), 0x4000000000000000u);
    }
    CHECK_THROWS_AS((void)list->at_path<"@[1]">(), std::logic_error);
}

namespace
{

struct garage {
    std::vector<tire> tires;
    std::vector<std::string> names;
    std::vector<std::vector<std::uint16_t>> rows;
    std::map<std::uint16_t, std::string> owners;
};

} // namespace

template <>
struct cbor::describe_members<garage> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1514};
    static constexpr std::tuple members{
        CBOR_MEMBER(garage, tires),
        CBOR_MEMBER(garage, names),
        CBOR_MEMBER(garage, rows),
        CBOR_MEMBER(garage, owners),
    };
};

namespace
{

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
    auto const read = form<garage>::decode(std::string(bytes));
    REQUIRE(read.has_value());
    CHECK_EQ((*read)->owners, (std::map<std::uint16_t, std::string>{{7, "seven"}}));
    CHECK(form<garage>::decode(schema_bytes(sample_garage)).has_value());
}

namespace
{

template <class T, cbor::fixed_string Path>
concept path_reads = requires(typename form<T>::template accessor<> const &a) { a.template at_path<Path>(); };

template <class T, cbor::fixed_string Path, class I>
concept path_reads_at = requires(typename form<T>::template accessor<> const &a, I const i) { a.template at_path<Path>(i); };

} // namespace

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
    using A = form<garage>::accessor<>;
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.names[0]">()), std::expected<std::string_view, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.tires[]">(0uz)), std::expected<form<garage>::accessor<cbor::fixed_element<tire>>, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.tires">()),
                       std::expected<form<garage>::accessor<std::vector<tire>>, cbor::error>>);
}

// An accessor of a list gives its size and its elements; an accessor of a struct gives its fields. A leaf through
// several steps gives the same value as the same steps through named accessors.
TEST_CASE("path: an accessor reads relative to the part it names")
{
    std::string const bytes = schema_bytes(sample_garage);
    auto const opened = form<garage>::path(std::string(bytes));
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



// The simple form: encode gives the bytes, decode gives the whole struct back. Every field takes the value it
// went in with, the lists, the map and the optional included.
TEST_CASE("encode and decode: a struct goes in and comes back whole")
{
    std::string const bytes = *form<garage>::encode(sample_garage);
    auto const decoded_back = *form<garage>::decode(std::string(bytes));
    garage const &back = *decoded_back;
    REQUIRE_EQ(back.tires.size(), 3u);
    CHECK_EQ(back.tires.at(2).diameter, 19u);
    CHECK_EQ(back.tires.at(1).airPressure, 3.0f);
    CHECK_EQ(back.names, sample_garage.names);
    CHECK_EQ(back.rows, sample_garage.rows);
    CHECK_EQ(back.owners, sample_garage.owners);

    std::string const encoded = *form<vehicle>::encode(sample_vehicle);
    auto const decoded_v = *form<vehicle>::decode(std::string(encoded));
    vehicle const &v = *decoded_v;
    CHECK_EQ(v.make, "Tesla");
    CHECK_EQ(v.balance, -7);
    CHECK_EQ(v.spare.at(1).diameter, 16u);
    CHECK_EQ(v.motor.cc, 1800u);
    CHECK_EQ(std::string_view(v.code, 4), "ABCD"sv);
    CHECK_EQ(v.owner, std::optional<std::uint8_t>{9});
    CHECK_FALSE(v.none.has_value());
}

// A plain struct has no allocator, so a std::pmr::vector cannot give its resource to it, but it can give it to each
// member that takes an allocator, as uses-allocator construction ([allocator.uses.construction]) does for one
// object. The test puts a counting resource as the default resource, so that a member that took the default
// resource shows as a count above zero. Each name is longer than any short string buffer, so each one allocates.
TEST_CASE("decode: a plain struct in a std::pmr::vector takes the resource of the vector for its members")
{
    std::string const long_name(40, 'n');
    std::string const bytes = schema_bytes(pmr_people{{{std::pmr::string(long_name)}, {std::pmr::string(long_name)}}});
    counting_resource fallback;
    counting_resource target;
    auto const owner = std::make_shared<std::string const>(bytes);
    std::pmr::memory_resource *const was = std::pmr::set_default_resource(&fallback);
    auto const decoded = form<pmr_people>::decode(owner, pmr_people{std::pmr::vector<pmr_named>(&target)});
    std::pmr::set_default_resource(was);
    REQUIRE(decoded.has_value());
    REQUIRE_EQ((**decoded).people.size(), 2u);
    for (pmr_named const &p : (**decoded).people) {
        CHECK_EQ(p.n, long_name);
        CHECK_EQ(p.n.get_allocator().resource(), &target);
    }
    CHECK_EQ(fallback.allocations, 0u);
    CHECK_GT(target.allocations, 0u);
}

// Broken bytes give the error as a value, as with std::expected.
TEST_CASE("decode: an error is read as a value")
{
    std::string const bytes = *form<vehicle>::encode(sample_vehicle);
    std::string_view const cut = std::string_view(bytes).substr(0, 3);
    auto const r = form<vehicle>::decode(std::string(cut));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::too_little_data);
}

namespace
{

struct keyed {
    std::uint8_t m0;
    bool m1;
};

} // namespace

template <>
struct cbor::describe_members<keyed> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{65536};
    static constexpr std::tuple members{
        cbor::member{"x-user-id", &keyed::m0},
        cbor::member{"EOF", &keyed::m1},
    };
};

// The key that the description gives takes the place of the member name in the table. The expectation is
// counted from the draft and RFC 8949 3 by hand: the keys with the text heads 69 and 63, an empty directory,
// f7 up to index 15, then the class tag 65536, which needs the 4-byte argument da 00 01 00 00 (RFC 8949 3 and
// 4.1), the record head of 7 bytes, the integer with its fixed width of 18 and 1 byte, and the simple value.
// The test exists because a tag number above 65535 makes the head of the class tag 5 bytes long, and every
// offset behind it moves by that.
TEST_CASE("schema: the description gives the key of a member")
{
    CHECK_EQ(form<keyed>::fixed_size(), 15u);
    CHECK_EQ(form<keyed>::member_offset<&keyed::m1>(), 12u);
    CHECK_EQ(form<keyed>::member_offset<&keyed::m0>(), 13u);
    std::string const bytes = *form<keyed>::encode(keyed{7, true});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x10\xd8\x72\x82\x63"
                    "EOF\x69x-user-id\x5a\x00\x00\x00\x00"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xda\x00\x01\x00\x00\xd8\x80\x9a\x00\x00\x00\x02\xf5\x18\x07"s);
    auto const decoded_back = *form<keyed>::decode(std::string(bytes));
    keyed const &back = *decoded_back;
    CHECK_EQ(back.m0, 7u);
    CHECK(back.m1);
    CHECK_EQ(at_path<keyed, "$.EOF">(bytes), true);
}



// encode writes into a target of the caller: a growing container gets the message appended, a fixed span takes
// it if it fits, and any other target gives an object with append and done through allocate. done carries the
// final length of the message. A span that holds the message but not the padding of a head still takes it.
TEST_CASE("encode: into a string, a vector, a span and a writer of the caller")
{
    std::string const expected = *form<garage>::encode(sample_garage);
    std::string text = "x";
    CHECK_EQ(*form<garage>::encode(sample_garage, text), expected.size());
    CHECK_EQ(text, "x" + expected);
    std::vector<std::byte> bytes;
    REQUIRE(form<garage>::encode(sample_garage, bytes).has_value());
    CHECK_EQ(bytes.size(), expected.size());
    std::array<char, 4096> buffer{};
    auto const fits = form<garage>::encode(sample_garage, std::span(buffer));
    REQUIRE(fits.has_value());
    CHECK_EQ(std::string_view(buffer.data(), *fits), expected);
    std::vector<char> chars{'x'};
    CHECK_EQ(*form<garage>::encode(sample_garage, chars), expected.size());
    CHECK_EQ(std::string_view(chars.data(), chars.size()), "x" + expected);
    std::vector<char> exact(expected.size());
    auto const fits_exactly = form<garage>::encode(sample_garage, std::span(exact));
    REQUIRE(fits_exactly.has_value());
    CHECK_EQ(std::string_view(exact.data(), *fits_exactly), expected);
    std::array<char, 8> small{};
    auto const too_small = form<garage>::encode(sample_garage, std::span(small));
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
    REQUIRE(form<garage>::encode(sample_garage, writer).has_value());
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

template <>
struct cbor::describe_members<passkey_user> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1516};
    static constexpr std::tuple members{
        CBOR_MEMBER(passkey_user, id),
        CBOR_MEMBER(passkey_user, name),
    };
};

template <>
struct cbor::describe_members<passkey_login> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1517};
    static constexpr std::tuple members{
        CBOR_MEMBER(passkey_login, signature),
        CBOR_MEMBER(passkey_login, user),
        CBOR_MEMBER(passkey_login, note),
    };
};

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
        std::string const expected = *form<std::remove_cvref_t<decltype(value)>>::encode(value);
        reserving writer;
        writer.store.fill('#');
        REQUIRE_EQ(*form<std::remove_cvref_t<decltype(value)>>::encode(value, writer), expected.size());
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
    std::string const bytes = *form<passkey_login>::encode(full);
    CHECK_EQ(at_path<passkey_login, "$.note">(bytes).value(), std::optional<std::string>{"hello"});
    auto const decoded_back = *form<passkey_login>::decode(std::string(bytes));
    passkey_login const &back = *decoded_back;
    REQUIRE(back.user.has_value());
    CHECK_EQ(back.user->name, "alice");
    CHECK_EQ(back.note, std::optional<std::string>{"hello"});

    passkey_login const empty{{std::byte{1}}, std::nullopt, std::nullopt};
    std::string const none = *form<passkey_login>::encode(empty);
    auto const absent = at_path<passkey_login, "$.note">(none);
    REQUIRE(absent.has_value());
    CHECK_FALSE(absent->has_value());
    for (std::string_view message : {std::string_view(bytes), std::string_view(none)}) {
        auto const end = cbor::item_size(message);
        REQUIRE(end.has_value());
        CHECK_EQ(*end, message.size());
    }
    CHECK_EQ(form<wheel>::fixed_size<std::optional<passkey_user>>(), 2 + form<passkey_user>::fixed_size());
    auto const decoded_back2 = *form<passkey_login>::decode(std::string(none));
    passkey_login const &back2 = *decoded_back2;
    CHECK_FALSE(back2.user.has_value());
    CHECK_FALSE(back2.note.has_value());
}



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

} // namespace

template <>
struct cbor::describe_members<chain19> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1619};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain19, v),
        CBOR_MEMBER(chain19, text),
        CBOR_MEMBER(chain19, words),
    };
};

template <>
struct cbor::describe_members<chain18> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1618};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain18, v),
        CBOR_MEMBER(chain18, next),
    };
};

template <>
struct cbor::describe_members<chain17> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1617};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain17, v),
        CBOR_MEMBER(chain17, next),
    };
};

template <>
struct cbor::describe_members<chain16> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1616};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain16, v),
        CBOR_MEMBER(chain16, next),
    };
};

template <>
struct cbor::describe_members<chain15> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1615};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain15, v),
        CBOR_MEMBER(chain15, next),
    };
};

template <>
struct cbor::describe_members<chain14> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1614};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain14, v),
        CBOR_MEMBER(chain14, next),
    };
};

template <>
struct cbor::describe_members<chain13> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1613};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain13, v),
        CBOR_MEMBER(chain13, next),
    };
};

template <>
struct cbor::describe_members<chain12> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1612};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain12, v),
        CBOR_MEMBER(chain12, next),
    };
};

template <>
struct cbor::describe_members<chain11> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1611};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain11, v),
        CBOR_MEMBER(chain11, next),
    };
};

template <>
struct cbor::describe_members<chain10> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1610};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain10, v),
        CBOR_MEMBER(chain10, next),
    };
};

template <>
struct cbor::describe_members<chain9> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1609};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain9, v),
        CBOR_MEMBER(chain9, next),
    };
};

template <>
struct cbor::describe_members<chain8> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1608};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain8, v),
        CBOR_MEMBER(chain8, next),
    };
};

template <>
struct cbor::describe_members<chain7> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1607};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain7, v),
        CBOR_MEMBER(chain7, next),
    };
};

template <>
struct cbor::describe_members<chain6> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1606};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain6, v),
        CBOR_MEMBER(chain6, next),
    };
};

template <>
struct cbor::describe_members<chain5> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1605};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain5, v),
        CBOR_MEMBER(chain5, next),
    };
};

template <>
struct cbor::describe_members<chain4> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1604};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain4, v),
        CBOR_MEMBER(chain4, next),
    };
};

template <>
struct cbor::describe_members<chain3> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1603};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain3, v),
        CBOR_MEMBER(chain3, next),
    };
};

template <>
struct cbor::describe_members<chain2> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1602};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain2, v),
        CBOR_MEMBER(chain2, next),
    };
};

template <>
struct cbor::describe_members<chain1> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1601};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain1, v),
        CBOR_MEMBER(chain1, next),
    };
};

template <>
struct cbor::describe_members<chain0> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1600};
    static constexpr std::tuple members{
        CBOR_MEMBER(chain0, v),
        CBOR_MEMBER(chain0, next),
    };
};

namespace
{

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
    CHECK_EQ(form<chain0>::fixed_size<chain19>(), 3u + 3u + 5u + 2u + 6u + 6u);
    CHECK_EQ(form<chain19>::fixed_size(), 3u + 2u + 5u + 2u + 6u + 6u);
    CHECK_EQ(form<chain0>::fixed_size(), 8u * 12u + 11u * 13u + 25u);
    CHECK_EQ((form<chain0>::member_offset<&chain8::next, chain8>()), 13u);
    CHECK_EQ(form<chain8>::member_offset<&chain8::next>(), 12u);

    chain0 const value = chain_of<chain0>(0);
    std::string const bytes = *form<chain0>::encode(value);
    CHECK_EQ(cbor::item_size(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x19"s);
    CHECK_EQ(bytes.substr(bytes.size() - 25),
             "\xd9\x06\x53\xc6\x82\x0b\x9a\x00\x00\x00\x03\x18\x13\xc6\x3a\x00\x00\x00\x02\xc6\x1a\x00\x00\x00\x03"s);
    CHECK_EQ(bytes.substr(bytes.size() - form<chain0>::fixed_size() + 8u * 12u, 6), "\xd9\x06\x48\xc6\x82\x00"s);

    auto const decoded_back = *form<chain0>::decode(std::string(bytes));
    chain0 const &back = *decoded_back;
    CHECK_EQ(*form<chain0>::encode(back), bytes);
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
    std::string const bytes = *form<chain3>::encode(value);
    CHECK_EQ(cbor::item_size(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x16"s);
    CHECK_EQ(bytes.substr(bytes.size() - 25),
             "\xd9\x06\x53\xc6\x82\x08\x9a\x00\x00\x00\x03\x18\x13\xc6\x1a\x00\x00\x00\x01\xc6\x3a\x00\x00\x00\x01"s);

    auto const decoded_back = *form<chain3>::decode(std::string(bytes));
    chain3 const &back = *decoded_back;
    CHECK_EQ(*form<chain3>::encode(back), bytes);
    CHECK_EQ(at_path<chain3, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(bytes), "ab"s);

    std::string broken = bytes;
    broken.at(broken.size() - 7) = '\x00';
    CHECK_EQ(form<chain3>::decode(std::string(broken)).error(), error::unpopulated_table_index);
    CHECK_EQ(at_path<chain3, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(broken).error(),
             error::unpopulated_table_index);
}



namespace
{

struct point_a {
    std::uint8_t x;
};

struct point_b {
    std::uint8_t x;
};

} // namespace

template <>
struct cbor::describe_members<point_a> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1520};
    static constexpr std::tuple members{
        CBOR_MEMBER(point_a, x),
    };
};

template <>
struct cbor::describe_members<point_b> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1521};
    static constexpr std::tuple members{
        CBOR_MEMBER(point_b, x),
    };
};

// The tag of the class names the class on the wire. point_a and point_b have the same keys and the same layout,
// so only the class tag tells them apart: 1520 = d9 05 f0 against 1521 = d9 05 f1 (RFC 8949 3.4). The test exists
// because a reader that ignored the class tag would take a message of one class for the other.
TEST_CASE("schema: a root of another class tag is incorrect_type")
{
    std::string const bytes = *form<point_a>::encode(point_a{7});
    CHECK_EQ(bytes.substr(bytes.size() - form<point_a>::fixed_size(), 3), "\xd9\x05\xf0"s);
    auto const point = *form<point_a>::decode(std::string(bytes));
    CHECK_EQ(point->x, 7u);
    CHECK_EQ(form<point_b>::decode(std::string(bytes)).error(), error::incorrect_type);
    CHECK_EQ(at_path<point_b, "$.x">(bytes).error(), error::incorrect_type);
    CHECK_EQ(at_path<point_a, "$.x">(bytes), 7u);
}

// The class tag of a nested record is a fixed head like any other. car holds the engine record at offset 12, and
// the low byte of its tag 1501 = d9 05 dd is changed to de. The test exists because a forged nested tag must stop
// decode, and a path through that record must stop too, while the root and the other fields still read.
TEST_CASE("schema: a forged class tag of a nested record is incorrect_type")
{
    std::string const bytes = *form<car>::encode(car{4, {300, 1800}, true});
    std::size_t const motor = bytes.size() - form<car>::fixed_size() + form<car>::member_offset<&car::motor>();
    CHECK_EQ(bytes.substr(motor, 3), "\xd9\x05\xdd"s);
    std::string forged = bytes;
    forged.at(motor + 2) = '\xde';
    CHECK_EQ(form<car>::decode(std::string(forged)).error(), error::incorrect_type);
    CHECK_EQ(at_path<car, "$.seats">(forged), 4u);
    CHECK_EQ(at_path<car, "$.motor.cc">(forged).error(), error::incorrect_type);
    CHECK_EQ(at_path<car, "$.motor.cc">(bytes), 1800u);
}

namespace
{

struct ticket {
    std::string_view holder;
    std::vector<std::string_view> seats;
    std::uint16_t row;
};

} // namespace

template <>
struct cbor::describe_members<ticket> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1522};
    static constexpr std::tuple members{
        CBOR_MEMBER(ticket, holder),
        CBOR_MEMBER(ticket, seats),
        CBOR_MEMBER(ticket, row),
    };
};

namespace
{

ticket const sample_ticket{"a holder name longer than the small string buffer", {"seat one of the long list", "seat two"}, 12};

std::string ticket_bytes()
{
    return *form<ticket>::encode(sample_ticket);
}

} // namespace

// decode(bytes) copies the message once, and every view in the result points into that copy. The message here is a
// temporary that is destroyed at the end of the full expression. The test exists because a view into the bytes of
// the caller would read freed memory, and the address sanitizer reports that read.
TEST_CASE("decode: the views of the result outlive the bytes of the caller")
{
    std::expected<cbor::ref<ticket>, cbor::error> const t = form<ticket>::decode(ticket_bytes());
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->holder, sample_ticket.holder);
    REQUIRE_EQ((*t)->seats.size(), 2u);
    CHECK_EQ((*t)->seats.at(0), sample_ticket.seats.at(0));
    CHECK_EQ((*t)->seats.at(1), sample_ticket.seats.at(1));
    CHECK_EQ((*t)->row, 12u);
}

// decode(owner) copies nothing and keeps the owner. The caller releases its own pointer to the owner before
// it reads. The test exists because the result alone must keep the bytes alive.
TEST_CASE("decode: the result keeps the owner after the caller releases it")
{
    auto owner = std::make_shared<std::string const>(ticket_bytes());
    auto const t = form<ticket>::decode(std::move(owner));
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
    auto const opened = form<ticket>::path(ticket_bytes());
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
    auto const opened = form<ticket>::path(std::move(owner));
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    auto const holder = root.at_path<"$.holder">();
    auto const seat = root.at_path<"$.seats[1]">();
    REQUIRE(holder.has_value());
    REQUIRE(seat.has_value());
    CHECK_EQ(*holder, sample_ticket.holder);
    CHECK_EQ(*seat, sample_ticket.seats.at(1));
}



// The tests below attack the path reader with forged and cut messages. Every message is copied into a
// std::string, because the view form takes one. The address sanitizer reports a read past the capacity of that
// string, not a read of the bytes between its size and its capacity. Every text of the view form must lie inside the
// message.

namespace
{

struct probe {
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

} // namespace

template <>
struct cbor::describe_members<probe> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1523};
    static constexpr std::tuple members{
        CBOR_MEMBER(probe, flag),
        CBOR_MEMBER(probe, i16),
        CBOR_MEMBER(probe, u64),
        CBOR_MEMBER(probe, f64),
        CBOR_MEMBER(probe, f32),
        CBOR_MEMBER(probe, i128),
        CBOR_MEMBER(probe, code),
        CBOR_MEMBER(probe, text),
        CBOR_MEMBER(probe, tires),
        CBOR_MEMBER(probe, maybe),
        CBOR_MEMBER(probe, motor),
        CBOR_MEMBER(probe, pair),
    };
};

namespace
{

probe const sample_probe{true, -300, 1ull << 40, 2.5, -1.5f, cbor::int128{-5}, {'x', 'y', 'z'}, "probe text",
                         {{17, 1.5f}, {18, 2.0f}}, -9, {300, 1800}, {{7, 8}}};

std::size_t probe_offset(std::size_t const size, std::size_t const member)
{
    return size - form<probe>::fixed_size() + member;
}

bool lies_inside(std::string_view const part, std::string_view const message)
{
    std::less<char const *> const before;
    return part.empty() || (!before(part.data(), message.data()) &&
                            !before(message.data() + message.size(), part.data() + part.size()));
}

// Reads every leaf path of probe, each through a new root and through a root with an owner, from a copy of the message. Gives the count of
// paths that gave a value.
std::size_t probe_paths_read(std::string_view const message)
{
    auto const block = std::make_shared<std::string const>(message);
    std::string_view const bytes = *block;
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
    owned(at_path<probe, "$.code">(block));
    count(at_path<probe, "$.text">(bytes));
    owned(at_path<probe, "$.text">(block));
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
    auto const block = std::make_shared<std::string const>(message);
    std::string_view const bytes = *block;
    auto opened = form<probe>::path(block);
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
    std::string const bytes = *form<probe>::encode(sample_probe);
    CHECK_EQ(probe_paths_read(bytes), probe_paths);
    CHECK_EQ(probe_lot_read(bytes), probe_lot_paths);
    CHECK_EQ(at_path<probe, "$.i16">(bytes), -300);
    CHECK((*at_path<probe, "$.i128">(bytes) == cbor::int128{-5}));
    CHECK_EQ(at_path<probe, "$.f64">(bytes), 2.5);
    CHECK_EQ(at_path<probe, "$.pair[]">(bytes, 1uz), 8u);
    CHECK_EQ(at_path<probe, "$.maybe">(bytes).value(), std::optional<std::int32_t>{-9});
}

// A text is a view into the bytes that the root holds. An accessor is read through an lvalue only, so each accessor
// on the way to a value has a name. A ref cannot be read through a temporary. An owner that is empty is a
// wrong use and throws. The test exists because each of these would otherwise let a view outlive its bytes.
TEST_CASE("attack: no text result outlives its bytes")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    using A = form<probe>::accessor<>;
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.text">()), std::expected<std::string_view, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.code">()), std::expected<std::string_view, cbor::error>>);
    CHECK(path_reads<probe, "$.text">);
    CHECK_FALSE(temporary_reads<A, "$.text">);
    CHECK_FALSE(temporary_reads<form<probe>::accessor<tire>, "@.diameter">);
    CHECK_FALSE(chain_reads<A>);
    CHECK_FALSE(dereferenced_as_rvalue<cbor::ref<probe>>);
    CHECK_THROWS_AS((void)form<probe>::path(std::shared_ptr<std::string const>{}), std::logic_error);
    CHECK_THROWS_AS((void)form<probe>::decode(std::shared_ptr<std::string const>{}), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. An owner beside borrowed bytes compiles
// (owner decision 2026-10-17: an LMDB read transaction keeps the bytes of its value alive); an owner beside a string
// does not, because the string may die first. The test exists because each of these let a view outlive its bytes,
// and a check of the stored pointer refused an owner that holds the bytes.
TEST_CASE("attack: path and decode check that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>(*form<probe>::encode(sample_probe));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { form<probe>::path(o, std::string(*o)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { form<probe>::path(o, std::string_view(*o)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { form<probe>::decode(o, std::string_view(*o)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { form<probe>::decode(o, *o); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { form<probe>::path(o); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { form<probe>::decode(o); }; }(bytes)));
    std::shared_ptr<std::string const> const holds_nothing(std::shared_ptr<std::string const>{}, bytes.get());
    CHECK_THROWS_AS((void)form<probe>::path(holds_nothing), std::logic_error);
    CHECK_THROWS_AS((void)form<probe>::decode(holds_nothing), std::logic_error);
    std::shared_ptr<std::string const> const holds_bytes(std::shared_ptr<void const>(bytes), bytes.get());
    auto const root = form<probe>::path(holds_bytes);
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.i16">(), -300);
    auto const decoded = form<probe>::decode(holds_bytes);
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->i16, -300);
}

// Each prefix of the message is a cut message. The test exists because a reader that trusted an offset from the
// wire would read past the end of the cut. No prefix reads a value: the root lies at the end of the message.
TEST_CASE("attack: every prefix of a message is an error for every path")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t values = 0;
    for (std::size_t size = 0; size < bytes.size(); ++size)
        values += probe_paths_read(std::string_view(bytes).substr(0, size)) + probe_lot_read(std::string_view(bytes).substr(0, size));
    CHECK_EQ(values, 0u);
}

// A byte in front of the message or behind it moves the root away from its place at the end. The test exists because
// a reader that found the root by an offset from the start would read a record that is not there.
TEST_CASE("attack: a shifted root is incorrect_type")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    CHECK_EQ(at_path<probe, "$.i16">("\x00"s + bytes).error(), error::incorrect_type);
    CHECK_EQ(at_path<probe, "$.i16">(bytes + "\x00"s).error(), error::incorrect_type);
    CHECK_EQ(probe_paths_read("\x00"s + bytes), 0u);
    CHECK_EQ(probe_paths_read(bytes + "\x00"s), 0u);
    CHECK_EQ(probe_lot_read("\x00"s + bytes), 0u);
    CHECK_EQ(probe_lot_read(bytes + "\x00"s), 0u);
    CHECK_EQ(form<probe>::path(bytes + "\x00"s).error(), error::incorrect_type);
}

// RFC 8949 3: each fixed field has one head. The heads that are valid are written here from RFC 8949 3.3 and 3.4
// and the fixed widths of the schema: a bool is f4 or f5, an int16 is 19 or 39, a uint64 is 1b, a double fb, a
// float fa, an int128 the tag c2 or c3, a char[3] the text head 63. Every other head is incorrect_type. The test
// exists because a reader that ignored the head would read the argument of another type.
TEST_CASE("attack: every forged head of a fixed field is incorrect_type")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    auto const forged = [&bytes](std::size_t const member, int const head) {
        std::string f = bytes;
        f.at(probe_offset(f.size(), member)) = static_cast<char>(head);
        return f;
    };
    using S = form<probe>;
    for (int head = 0; head < 256; ++head) {
        CAPTURE(head);
        CHECK_EQ(at_path<probe, "$.flag">(forged(S::member_offset<&probe::flag>(), head)).has_value(), head == 0xf4 || head == 0xf5);
        CHECK_EQ(at_path<probe, "$.i16">(forged(S::member_offset<&probe::i16>(), head)).has_value(), head == 0x19 || head == 0x39);
        CHECK_EQ(at_path<probe, "$.u64">(forged(S::member_offset<&probe::u64>(), head)).has_value(), head == 0x1b);
        CHECK_EQ(at_path<probe, "$.f64">(forged(S::member_offset<&probe::f64>(), head)).has_value(), head == 0xfb);
        CHECK_EQ(at_path<probe, "$.f32">(forged(S::member_offset<&probe::f32>(), head)).has_value(), head == 0xfa);
        CHECK_EQ(at_path<probe, "$.i128">(forged(S::member_offset<&probe::i128>(), head)).has_value(), head == 0xc2 || head == 0xc3);
        CHECK_EQ(at_path<probe, "$.code">(forged(S::member_offset<&probe::code>(), head)).has_value(), head == 0x63);
        CHECK_EQ(at_path<probe, "$.pair[1]">(forged(S::member_offset<&probe::pair>(), head)).has_value(), head == 0x82);
    }
}

// Every byte of the root record is forged in turn, with values that flip a head, a length or a reference. The test
// exists because no forged byte may make a path read outside the message; the sanitizer and the check of each text
// see that.
TEST_CASE("attack: a forged byte anywhere in the root reads nothing outside the message")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t const root = bytes.size() - form<probe>::fixed_size();
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
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t const head = bytes.find("\x5a\x00\x00\x00\x0c"s);
    REQUIRE_NE(head, std::string::npos);
    std::size_t const root = bytes.size() - form<probe>::fixed_size();
    for (std::size_t j = 0; j < 3; ++j) {
        for (std::uint32_t const to : {0u, static_cast<std::uint32_t>(head), static_cast<std::uint32_t>(root),
                                       static_cast<std::uint32_t>(bytes.size()), 0x7fffffffu, 0xffffffffu}) {
            std::string f = bytes;
            f.replace(head + 5 + 4 * j, 4, std::string{static_cast<char>(to >> 24), static_cast<char>(to >> 16),
                                                       static_cast<char>(to >> 8), static_cast<char>(to)});
            CAPTURE(j);
            CAPTURE(to);
            CHECK_FALSE(form<probe>::decode(std::string(f)).has_value());
            CHECK_LT(probe_paths_read(f), probe_paths);
            CHECK_LT(probe_lot_read(f), probe_lot_paths);
        }
    }
}

// Two directory entries are forged to the same offset, so two items would share their bytes. In key order the items
// of probe are text, maybe and tires; each pair of neighbours is forged once. The text item of login
// is the last item; its length is forged to reach into the root record behind it. The test exists because a text
// that reached into the root would give the bytes of the root's fields as text.
TEST_CASE("attack: overlapping items are an error")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t const head = bytes.find("\x5a\x00\x00\x00\x0c"s);
    REQUIRE_NE(head, std::string::npos);
    std::string same = bytes;
    same.replace(head + 9, 4, bytes.substr(head + 5, 4));
    CHECK_FALSE(at_path<probe, "$.text">(same).has_value());
    CHECK_FALSE(at_path<probe, "$.maybe">(same).has_value());
    std::string next = bytes;
    next.replace(head + 13, 4, bytes.substr(head + 9, 4));
    CHECK_FALSE(at_path<probe, "$.maybe">(next).has_value());
    CHECK_FALSE(at_path<probe, "$.tires[0].diameter">(next).has_value());

    std::string const one = schema_bytes(login{5, true, "ab"});
    std::size_t const text = one.find("\x7a\x00\x00\x00\x02" "ab"s);
    REQUIRE_NE(text, std::string::npos);
    std::string into_root = one;
    into_root.replace(text + 1, 4, std::string{'\x00', '\x00', '\x00', static_cast<char>(2 + form<login>::fixed_size())});
    CHECK_EQ(at_path<login, "$.name">(into_root).error(), error::too_little_data);
    CHECK_EQ(at_path<login, "$.id">(into_root), 5u);
}

// Each item of a packed message starts with an RFC 8949 head: a typed array with the tag head d8 56 and the head
// 5a of its byte string, a text with the head 7a. A four-byte argument is read big endian (RFC 8949 Section 3).
// A directory entry is forged so that the head of the last item no longer fits before the root record. The test
// exists because the read of that head must stop at the bytes that are left and give too_little_data.
TEST_CASE("attack: an item head that does not fit before the root is too_little_data")
{
    auto const big_endian = [](std::size_t const x) {
        return std::string{static_cast<char>(x >> 24), static_cast<char>(x >> 16), static_cast<char>(x >> 8),
                           static_cast<char>(x)};
    };
    std::string const array = schema_bytes(doubles{{1.0, 2.0}});
    std::size_t const head = array.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(head, std::string::npos);
    std::size_t const entry = array.find(big_endian(head));
    REQUIRE_LT(entry, head);
    std::string cut_array = array;
    cut_array.replace(entry, 4, big_endian(array.size() - form<doubles>::fixed_size() - 3));
    CHECK_EQ(at_path<doubles, "$.v[0]">(cut_array).error(), error::too_little_data);

    std::string const text = schema_bytes(login{5, true, "ab"});
    std::size_t const name = text.find("\x7a\x00\x00\x00\x02" "ab"sv);
    REQUIRE_NE(name, std::string::npos);
    std::size_t const name_entry = text.find(big_endian(name));
    REQUIRE_LT(name_entry, name);
    std::string cut_text = text;
    cut_text.replace(name_entry, 4, big_endian(text.size() - form<login>::fixed_size() - 3));
    CHECK_EQ(at_path<login, "$.name">(cut_text).error(), error::too_little_data);
}

// The directory is a byte string with a four-byte argument (RFC 8949 Section 3.1, major type 2). Its length is
// forged far past the end of the message, and the count in the prefix is forged to match it. The test exists
// because the read of the directory must stop at the bytes that are left and give too_little_data.
TEST_CASE("attack: a directory longer than the message is too_little_data")
{
    auto const big_endian = [](std::size_t const x) {
        return std::string{static_cast<char>(x >> 24), static_cast<char>(x >> 16), static_cast<char>(x >> 8),
                           static_cast<char>(x)};
    };
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    std::size_t const directory = bytes.find("\x5a\x00\x00\x00\x04"sv, 8);
    REQUIRE_NE(directory, std::string::npos);
    std::size_t const forged_length = 0x00fffff0;
    std::size_t const first =
        (std::size_t{static_cast<std::uint8_t>(bytes[4])} << 24 | std::size_t{static_cast<std::uint8_t>(bytes[5])} << 16 |
         std::size_t{static_cast<std::uint8_t>(bytes[6])} << 8 | std::size_t{static_cast<std::uint8_t>(bytes[7])}) -
        1;
    std::string forged = bytes;
    forged.replace(4, 4, big_endian(first + forged_length / 4));
    forged.replace(directory + 1, 4, big_endian(forged_length));
    CHECK_EQ(form<login>::path(std::string(forged)).error(), error::too_little_data);
    CHECK_EQ(form<login>::decode(std::string(forged)).error(), error::too_little_data);
}

// A vector of fixed-size structs refers to the description of its element in the header. The reference of the
// tires is forged to name another table entry. The test exists because a path into the vector must check that
// reference, while a path that does not touch the vector stays readable.
TEST_CASE("attack: a forged reference of a fixed-size vector is incorrect_type")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t const head = bytes.find("\xd9\x80\x69\x82"s);
    REQUIRE_NE(head, std::string::npos);
    std::string forged = bytes;
    forged.at(head + 4) = static_cast<char>(forged.at(head + 4) + 1);
    CHECK_EQ(at_path<probe, "$.tires[1].diameter">(forged).error(), error::incorrect_type);
    CHECK_EQ(at_path<probe, "$.text">(forged), at_path<probe, "$.text">(bytes));
    auto lot = *form<probe>::path(std::string(forged));
    for (int pass = 0; pass < 2; ++pass) {
        CHECK_EQ(lot.at_path<"$.tires[].diameter">(0uz).error(), error::incorrect_type);
        CHECK_EQ(lot.at_path<"$.tires[].airPressure">(1uz).error(), error::incorrect_type);
    }
    CHECK_EQ(form<probe>::decode(std::string(forged)).error(), error::incorrect_type);
}

namespace
{

struct lot_wheel {
    std::uint16_t diameter;
    float airPressure;
    bool snowTires;
};

struct lot_engine {
    std::uint16_t horsepower;
    std::uint32_t cc;
};

struct lot_car {
    std::string make;
    std::string model;
    std::uint8_t seats;
    std::vector<lot_wheel> wheels;
    lot_engine engine;
    std::optional<std::string> note;
    char code[2];
};

struct parking_lot {
    std::vector<lot_car> cars;
    std::vector<std::string> notes;
    std::vector<std::vector<std::uint16_t>> rows;
};

} // namespace

template <>
struct cbor::describe_members<lot_wheel> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1524};
    static constexpr std::tuple members{
        CBOR_MEMBER(lot_wheel, diameter),
        CBOR_MEMBER(lot_wheel, airPressure),
        CBOR_MEMBER(lot_wheel, snowTires),
    };
};

template <>
struct cbor::describe_members<lot_engine> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1525};
    static constexpr std::tuple members{
        CBOR_MEMBER(lot_engine, horsepower),
        CBOR_MEMBER(lot_engine, cc),
    };
};

template <>
struct cbor::describe_members<lot_car> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1526};
    static constexpr std::tuple members{
        CBOR_MEMBER(lot_car, make),
        CBOR_MEMBER(lot_car, model),
        CBOR_MEMBER(lot_car, seats),
        CBOR_MEMBER(lot_car, wheels),
        CBOR_MEMBER(lot_car, engine),
        CBOR_MEMBER(lot_car, note),
        CBOR_MEMBER(lot_car, code),
    };
};

template <>
struct cbor::describe_members<parking_lot> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1527};
    static constexpr std::tuple members{
        CBOR_MEMBER(parking_lot, cars),
        CBOR_MEMBER(parking_lot, notes),
        CBOR_MEMBER(parking_lot, rows),
    };
};

namespace
{

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
void lot_car_matches(form<parking_lot>::accessor<> const &lot, parking_lot const &back, std::size_t const i)
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
    auto const decoded = *form<parking_lot>::decode(std::string(bytes));
    parking_lot const &back = *decoded;
    auto lot = *form<parking_lot>::path(std::string(bytes));
    std::vector<std::size_t> order(back.cars.size());
    std::iota(order.begin(), order.end(), 0uz);
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
    using D = form<parking_lot>::accessor<>;
    CHECK(lot_reads<D, "$.cars[].seats">);
    CHECK(lot_reads<D const, "$.cars[].seats">);
    CHECK(lot_reads<D const, "$.cars[]">);
    CHECK_FALSE(temporary_lot_reads<D, "$.cars[].seats">);
    CHECK_FALSE(temporary_lot_reads<D, "$.cars[]">);
    CHECK_FALSE(temporary_car_reads<D>);
    CHECK_THROWS_AS((void)form<parking_lot>::path(std::shared_ptr<std::string const>{}), std::logic_error);
}

// The copying form keeps its copy: a text and a car accessor stay valid after the caller frees the message, while the
// root lives.
TEST_CASE("path: the copying form keeps its copy after the caller frees the message")
{
    auto bytes = std::make_unique<std::string>(schema_bytes(sample_lot()));
    auto lot = *form<parking_lot>::path(std::string(*bytes));
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
    auto const read = [&differ, other](std::string_view const m) {
        auto opened = form<parking_lot>::path(std::make_shared<std::string const>(m));
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
    auto const moved = form<login>::path(std::move(*buffer));
    REQUIRE(moved.has_value());
    auto const moved_name = moved->at_path<"$.name">();
    REQUIRE(moved_name.has_value());
    CHECK(range(*moved_name, data, message.size()));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(*moved_name, name);

    std::string lvalue = message;
    auto const copied = form<login>::path(std::string(lvalue));
    REQUIRE(copied.has_value());
    auto const copied_name = copied->at_path<"$.name">();
    REQUIRE(copied_name.has_value());
    CHECK_FALSE(range(*copied_name, lvalue.data(), lvalue.size()));
    CHECK_EQ(lvalue, message);
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(*copied_name, name);

    std::string const constant = message;
    // Owner decision 2026-10-17: no form copies the input. A const string, a named string, a view and a literal
    // would need a copy, so none of them compiles.
    auto const takes = []<class S>(S &&) { return requires(S &&s) { form<login>::decode(std::forward<S>(s)); }; };
    auto const opens = []<class S>(S &&) { return requires(S &&s) { form<login>::path(std::forward<S>(s)); }; };
    CHECK_FALSE(opens(std::move(constant)));
    CHECK_FALSE(takes(std::move(constant)));
    CHECK_FALSE(takes(lvalue));
    CHECK_FALSE(takes(std::string_view(message)));
    CHECK_FALSE(takes(""));
    CHECK_FALSE(opens(""));

    auto decode_buffer = std::make_unique<std::string>(message);
    auto const decoded = form<login>::decode(std::move(*decode_buffer));
    decode_buffer.reset();
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->name, name);
    CHECK_EQ(form<login>::decode(std::string()).error(), error::too_little_data);
    CHECK_EQ(form<login>::path(std::string()).error(), error::too_little_data);
}


namespace
{

struct point {
    float x;
    float y;
};

struct cloud {
    std::vector<point> points;
};

struct gauge {
    bool on;
    color shade;
    sorted_inner at;
    std::array<std::uint16_t, 2> range;
};

struct gauges {
    std::vector<gauge> g;
};

template <int K>
struct filler_type {
    std::int8_t v;
};

struct fifteen_entries {
    filler_type<0> d0;
    filler_type<1> d1;
    filler_type<2> d2;
    filler_type<3> d3;
    filler_type<4> d4;
    filler_type<5> d5;
    filler_type<6> d6;
    filler_type<7> d7;
    filler_type<8> d8;
    filler_type<9> d9;
    filler_type<10> e0;
    filler_type<11> e1;
    std::vector<point> p;
};

struct eighteen_entries {
    filler_type<0> d0;
    filler_type<1> d1;
    filler_type<2> d2;
    filler_type<3> d3;
    filler_type<4> d4;
    filler_type<5> d5;
    filler_type<6> d6;
    filler_type<7> d7;
    filler_type<8> d8;
    filler_type<9> d9;
    filler_type<10> e0;
    filler_type<11> e1;
    filler_type<12> e2;
    filler_type<13> e3;
    filler_type<14> e4;
    std::vector<point> p;
};

} // namespace

template <>
struct cbor::describe_members<point> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1630};
    static constexpr std::tuple members{
        CBOR_MEMBER(point, x),
        CBOR_MEMBER(point, y),
    };
};

template <>
struct cbor::describe_members<cloud> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1631};
    static constexpr std::tuple members{
        CBOR_MEMBER(cloud, points),
    };
};

template <>
struct cbor::describe_members<gauge> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1638};
    static constexpr std::tuple members{
        CBOR_MEMBER(gauge, on),
        CBOR_MEMBER(gauge, shade),
        CBOR_MEMBER(gauge, at),
        CBOR_MEMBER(gauge, range),
    };
};

template <>
struct cbor::describe_members<gauges> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1639};
    static constexpr std::tuple members{
        CBOR_MEMBER(gauges, g),
    };
};

template <int K>
struct cbor::describe_members<filler_type<K>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1700 + K};
    static constexpr std::tuple members{
        CBOR_MEMBER(filler_type<K>, v),
    };
};

template <>
struct cbor::describe_members<fifteen_entries> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1690};
    static constexpr std::tuple members{
        CBOR_MEMBER(fifteen_entries, d0),
        CBOR_MEMBER(fifteen_entries, d1),
        CBOR_MEMBER(fifteen_entries, d2),
        CBOR_MEMBER(fifteen_entries, d3),
        CBOR_MEMBER(fifteen_entries, d4),
        CBOR_MEMBER(fifteen_entries, d5),
        CBOR_MEMBER(fifteen_entries, d6),
        CBOR_MEMBER(fifteen_entries, d7),
        CBOR_MEMBER(fifteen_entries, d8),
        CBOR_MEMBER(fifteen_entries, d9),
        CBOR_MEMBER(fifteen_entries, e0),
        CBOR_MEMBER(fifteen_entries, e1),
        CBOR_MEMBER(fifteen_entries, p),
    };
};

template <>
struct cbor::describe_members<eighteen_entries> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1691};
    static constexpr std::tuple members{
        CBOR_MEMBER(eighteen_entries, d0),
        CBOR_MEMBER(eighteen_entries, d1),
        CBOR_MEMBER(eighteen_entries, d2),
        CBOR_MEMBER(eighteen_entries, d3),
        CBOR_MEMBER(eighteen_entries, d4),
        CBOR_MEMBER(eighteen_entries, d5),
        CBOR_MEMBER(eighteen_entries, d6),
        CBOR_MEMBER(eighteen_entries, d7),
        CBOR_MEMBER(eighteen_entries, d8),
        CBOR_MEMBER(eighteen_entries, d9),
        CBOR_MEMBER(eighteen_entries, e0),
        CBOR_MEMBER(eighteen_entries, e1),
        CBOR_MEMBER(eighteen_entries, e2),
        CBOR_MEMBER(eighteen_entries, e3),
        CBOR_MEMBER(eighteen_entries, e4),
        CBOR_MEMBER(eighteen_entries, p),
    };
};

namespace
{

bool same(gauge const &a, gauge const &b)
{
    return a.on == b.on && a.shade == b.shade && a.at.x == b.at.x && a.at.y == b.at.y && a.range == b.range;
}

} // namespace

// Owner decision 2026-10-19: a vector of structs whose members all have a fixed width is one byte string of its
// elements, little endian and without padding, under tag 32873 (the first unassigned number of the first-come
// range of the IANA registry, provisional). The header, the table of tag 113 (draft-ietf-cbor-packed-19 3.1),
// describes the element once: [class tag, [[key, RFC 8746 tag], ...]]. The description of point stands at table
// index 2, after the two record entries: 82 19 06 5e 82 82 61 78 18 55 82 61 79 18 55 (tag 85 is float32 little
// endian, RFC 8746 2.1). The vector refers to it with simple(2) = e2 (draft Table 1), so a reader that unpacks
// finds the description in its place. The directory takes index 3, the fillers 4 to 15. The vector is item 16 at
// 61 (3d): d9 80 69 82 e2 and a byte string of 16 bytes. 1.0f is 3f800000, 2.0f is 40000000, 0.5f is 3f000000,
// -1.0f is bf800000 (IEEE 754), low byte first. The bytes are written from the draft and the RFCs by hand.
TEST_CASE("encode: a vector of fixed-size structs is one byte string under tag 32873")
{
    std::string const bytes = schema_bytes(cloud{{{1.0f, 0.5f}, {2.0f, -1.0f}}});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x11\xd8\x72\x81\x66points\xd8\x72\x82\x61x\x61y"
                    "\x82\x19\x06\x5e\x82\x82\x61x\x18\x55\x82\x61y\x18\x55"
                    "\x5a\x00\x00\x00\x04\x00\x00\x00\x3d"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xd9\x80\x69\x82\xe2\x5a\x00\x00\x00\x10"
                    "\x00\x00\x80\x3f\x00\x00\x00\x3f\x00\x00\x00\x40\x00\x00\x80\xbf"
                    "\xd9\x06\x5f\xd8\x80\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// The description names every leaf in the key order of RFC 8949 4.2.1: "at" (62 61 74) before "on" (62 6f 6e),
// "range" before "shade". An inner struct is described in place with its own class tag 1622 = 19 06 56; a
// std::array takes [key, tag, count]. bool takes uint8 (tag 64), the enum color the tag of its underlying uint16
// (69, little endian), int8 the tag 72. An element is at.x, at.y, on, range[0], range[1], shade: 9 bytes. The table
// holds gauges, gauge and sorted_inner, so the description is index 3 and the reference simple(3) = e3.
TEST_CASE("encode: a fixed-size struct with bool, enum, an inner struct and an array")
{
    std::string const bytes = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    CHECK_NE(bytes.find("\x82\x19\x06\x66\x84"
                        "\x82\x62" "at\x82\x19\x06\x56\x82\x82\x61x\x18\x48\x82\x61y\x18\x48"
                        "\x82\x62" "on\x18\x40"
                        "\x83\x65" "range\x18\x45\x02"
                        "\x82\x65" "shade\x18\x45"s),
             std::string::npos);
    CHECK_NE(bytes.find("\xd9\x80\x69\x82\xe3\x5a\x00\x00\x00\x09\x03\xfe\x01\x01\x00\x03\x02\x01\x00"s), std::string::npos);
    check_one_item(bytes);
}

// A path reads one leaf of one element at an offset the compiler knows, after the vector was checked once.
TEST_CASE("at_path: a leaf of one element of a fixed-size vector")
{
    std::string const bytes = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}}});
    CHECK_EQ(at_path<gauges, "$.g[0].on">(bytes), true);
    CHECK_EQ(at_path<gauges, "$.g[1].on">(bytes), false);
    CHECK_EQ(at_path<gauges, "$.g[0].shade">(bytes), color::white);
    CHECK_EQ(at_path<gauges, "$.g[0].at.x">(bytes), 3);
    CHECK_EQ(at_path<gauges, "$.g[1].at.y">(bytes), 5);
    CHECK_EQ(at_path<gauges, "$.g[0].range[1]">(bytes), 0x0203u);
    CHECK_EQ(at_path<gauges, "$.g[].range[]">(bytes, 1uz, 0uz), 7u);
    CHECK_EQ(at_path<gauges, "$.g[2].on">(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<gauges, "$.g[].range[]">(bytes, 0uz, 2uz).error(), error::index_out_of_bounds);
    std::string const points = schema_bytes(cloud{{{1.0f, 0.5f}, {2.0f, -1.0f}}});
    CHECK_EQ(at_path<cloud, "$.points[1].y">(points), -1.0f);
    CHECK_EQ(at_path<cloud, "$.points[0].x">(points), 1.0f);
    CHECK_EQ(at_path<cloud, "$.points[0].x">(schema_bytes(cloud{})).error(), error::index_out_of_bounds);
}

// An accessor of the vector gives its size; an accessor of one element or of its inner struct reads the leaves
// relative to it.
TEST_CASE("at_path: an accessor of a fixed-size vector and of one element")
{
    auto const owner = std::make_shared<std::string const>(
        schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}}}));
    auto const root = form<gauges>::path(owner);
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    CHECK_EQ(g->size(), 2u);
    CHECK_EQ(g->at_path<"@[1].at.x">(), -6);
    CHECK_EQ(g->at_path<"@[]">(2uz).error(), error::index_out_of_bounds);
    auto const second = g->at_path<"@[]">(1uz);
    REQUIRE(second.has_value());
    CHECK(std::same_as<std::remove_cvref_t<decltype(*second)>, form<gauges>::accessor<cbor::fixed_element<gauge>>>);
    CHECK_EQ(second->at_path<"@.range[0]">(), 7u);
    CHECK_EQ(second->at_path<"@.shade">(), color::black);
    auto const inner = root->at_path<"$.g[0].at">();
    REQUIRE(inner.has_value());
    CHECK_EQ(inner->at_path<"@.y">(), -2);
}

// A decode gives every leaf of every element back, also for no element and one.
TEST_CASE("encode and decode: a fixed-size vector round trip")
{
    for (std::size_t const n : {0uz, 1uz, 3uz}) {
        gauges value;
        for (std::size_t i = 0; i < n; ++i)
            value.g.push_back({i % 2 == 0, i % 2 == 0 ? color::black : color::white,
                               {static_cast<std::int8_t>(-static_cast<int>(i)), static_cast<std::int8_t>(i)},
                               {static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(i * 300)}});
        std::string const bytes = schema_bytes(value);
        check_one_item(bytes);
        auto const back = form<gauges>::decode(std::string(bytes));
        REQUIRE(back.has_value());
        REQUIRE_EQ((*back)->g.size(), n);
        for (std::size_t i = 0; i < n; ++i)
            CHECK(same((*back)->g[i], value.g[i]));
    }
}

// The table index of the description depends on how many record entries come before it. With 14 record entries the
// description is index 14, simple(14) = ee, and no filler is left. With 17 record entries the description is index
// 17, beyond the 16 one-byte references, so the reference is 6(-1) = c6 20 (draft Table 1: 16 - 2N - 1 = 17).
TEST_CASE("encode and decode: the reference to a description at every table index")
{
    fifteen_entries fifteen{};
    fifteen.p = {{1.0f, 2.0f}};
    fifteen.e1.v = 9;
    std::string const a = schema_bytes(fifteen);
    check_one_item(a);
    CHECK_NE(a.find("\xd9\x80\x69\x82\xee\x5a"s), std::string::npos);
    CHECK_EQ(a.find('\xf7'), std::string::npos);
    auto const a_back = form<fifteen_entries>::decode(std::string(a));
    REQUIRE(a_back.has_value());
    REQUIRE_EQ((*a_back)->p.size(), 1u);
    CHECK_EQ((*a_back)->p[0].y, 2.0f);
    CHECK_EQ((*a_back)->e1.v, 9);
    CHECK_EQ(at_path<fifteen_entries, "$.p[0].x">(a), 1.0f);
    eighteen_entries eighteen{};
    eighteen.p = {{3.0f, 4.0f}};
    eighteen.e4.v = -3;
    std::string const b = schema_bytes(eighteen);
    check_one_item(b);
    CHECK_NE(b.find("\xd9\x80\x69\x82\xc6\x20\x5a"s), std::string::npos);
    auto const b_back = form<eighteen_entries>::decode(std::string(b));
    REQUIRE(b_back.has_value());
    CHECK_EQ((*b_back)->p[0].y, 4.0f);
    CHECK_EQ((*b_back)->e4.v, -3);
    CHECK_EQ(at_path<eighteen_entries, "$.p[0].y">(b), 4.0f);
}

// The bytes of the vector come from the wire. The head of the vector, the description in the header, the length
// of the byte string and every bool are checked before a value is read.
TEST_CASE("at_path and decode: a broken fixed-size vector is an error")
{
    std::string const bytes = schema_bytes(cloud{{{1.0f, 0.5f}, {2.0f, -1.0f}}});
    std::size_t const head = bytes.find("\xd9\x80\x69\x82\xe2\x5a"s);
    REQUIRE_NE(head, std::string::npos);
    std::string reference = bytes;
    reference[head + 4] = '\xe3';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(reference).error(), error::incorrect_type);
    CHECK_EQ(form<cloud>::decode(std::string(reference)).error(), error::incorrect_type);
    std::string description = bytes;
    description[description.find("\x18\x55"s) + 1] = '\x56';
    CHECK_EQ(form<cloud>::path(std::string(description)).error(), error::incorrect_type);
    CHECK_EQ(form<cloud>::decode(std::string(description)).error(), error::incorrect_type);
    std::string partial = bytes;
    partial[head + 9] = '\x0f';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(partial).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(form<cloud>::decode(std::string(partial)).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[head + 9] = '\x18';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(longer).error(), error::too_little_data);
    CHECK_EQ(form<cloud>::decode(std::string(longer)).error(), error::too_little_data);
    std::string const flags = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    std::size_t const element = flags.find("\x03\xfe\x01\x01\x00"s);
    REQUIRE_NE(element, std::string::npos);
    std::string two = flags;
    two[element + 2] = '\x02';
    CHECK_EQ(at_path<gauges, "$.g[0].on">(two).error(), error::incorrect_type);
    CHECK_EQ(at_path<gauges, "$.g[0].shade">(two), color::white);
    CHECK_EQ(form<gauges>::decode(std::string(two)).error(), error::incorrect_type);
}

// The accessor of a fixed-size vector is a random access range of element accessors, so a loop, a page through
// std::views::drop and std::views::take, and a count work as with any std range. A page reads only its elements.
TEST_CASE("at_path: a fixed-size vector is a range of its elements")
{
    auto const owner = std::make_shared<std::string const>(schema_bytes(
        gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}, {true, color::black, {0, 9}, {0, 0}}}}));
    auto const root = form<gauges>::path(owner);
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    CHECK(std::ranges::random_access_range<decltype(*g)>);
    CHECK(std::ranges::sized_range<decltype(*g)>);
    std::vector<int> xs;
    for (auto const e : *g)
        xs.push_back(*e.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6, 9}));
    CHECK_EQ(std::ranges::size(*g), 3u);
    auto page = *g | std::views::drop(1) | std::views::take(1);
    CHECK_EQ(std::ranges::distance(page), 1);
    auto const first = *std::ranges::begin(page);
    CHECK_EQ(first.at_path<"@.range[1]">(), 8u);
    auto const third = g->begin()[2];
    CHECK_EQ(third.at_path<"@.on">(), true);
    std::string const empty = schema_bytes(gauges{});
    auto const none = form<gauges>::path(std::string(empty));
    REQUIRE(none.has_value());
    auto const nothing = none->at_path<"$.g">();
    REQUIRE(nothing.has_value());
    CHECK(std::ranges::empty(*nothing));
}


namespace
{

struct everything {
    bool flag;
    cbor::int128 big;
    char code[3];
    std::array<engine, 2> engines;
    std::optional<sorted_inner> inner;
    std::optional<std::int32_t> maybe;
    std::string text;
    std::vector<double> track;
    std::vector<std::string> names;
    std::map<std::uint16_t, std::string> owners;
    std::vector<gauge> gauges;
    std::vector<tire> tires;
};

} // namespace

template <>
struct cbor::describe_members<everything> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1660};
    static constexpr std::tuple members{
        CBOR_MEMBER(everything, flag),
        CBOR_MEMBER(everything, big),
        CBOR_MEMBER(everything, code),
        CBOR_MEMBER(everything, engines),
        CBOR_MEMBER(everything, inner),
        CBOR_MEMBER(everything, maybe),
        CBOR_MEMBER(everything, text),
        CBOR_MEMBER(everything, track),
        CBOR_MEMBER(everything, names),
        CBOR_MEMBER(everything, owners),
        CBOR_MEMBER(everything, gauges),
        CBOR_MEMBER(everything, tires),
    };
};

namespace
{

everything const sample_everything{true,
                                   cbor::int128{-5},
                                   {'x', 'y', 'z'},
                                   {{{300, 1800}, {90, 1200}}},
                                   sorted_inner{2, 1},
                                   -9,
                                   "text",
                                   {1.0, 2.0},
                                   {"a", "bc"},
                                   {{7, "seven"}, {9, "nine"}},
                                   {{true, color::white, {-2, 3}, {1, 2}}, {false, color::black, {5, -6}, {7, 8}}},
                                   {{17, 1.5f}, {18, 3.0f}}};

std::string reencoded(auto const &decoded)
{
    REQUIRE(decoded.has_value());
    return schema_bytes(**decoded);
}

template <class T>
using unverified_root = std::remove_cvref_t<decltype(*form<T>::unverified_path(std::string()))>;

// Reads every kind of part through the unverified forms and returns nothing: the sweep below only needs each call
// to return.
void unverified_reads(std::string const &bytes)
{
    auto const owner = std::make_shared<std::string const>(bytes);
    (void)form<everything>::unverified_at_path<"$.big">(bytes);
    (void)form<everything>::unverified_at_path<"$.engines[1].cc">(bytes);
    (void)form<everything>::unverified_at_path<"$.maybe">(bytes);
    (void)form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz);
    (void)form<everything>::unverified_at_path<"$.tires[].airPressure">(bytes, 1uz);
    (void)form<everything>::unverified_decode(owner);
    auto const root = form<everything>::unverified_path(owner);
    if (!root)
        return;
    (void)root->at_path<"$.text">();
    if (auto const inner = root->at_path<"$.inner">(); inner && *inner)
        (void)(*inner)->at_path<"@.x">();
    (void)root->at_path<"$.names[1]">();
    if (auto const track = root->view<"$.track">())
        for (double const d : *track)
            (void)d;
    if (auto const owners = root->at_path<"$.owners">())
        (void)owners->size();
    if (auto const gauges = root->at_path<"$.gauges">())
        for (auto const g : *gauges) {
            (void)g.at_path<"@.on">();
            (void)g.at_path<"@.range[1]">();
        }
    if (auto const tires = root->at_path<"$.tires">())
        for (std::size_t i = 0; i < tires->size() + 1; ++i)
            (void)tires->at_path<"@[].diameter">(i);
}

} // namespace

// The verified decode checks the head of a fixed string and of a std::array as the verified path does. Before,
// only the path checked them, so a forged head gave incorrect_type to a path and a value to decode.
TEST_CASE("decode: the head of a fixed string and of a std::array is checked")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    std::size_t const code = bytes.find("\x64" "ABCD"s);
    REQUIRE_NE(code, std::string::npos);
    std::string text_head = bytes;
    text_head[code] = '\x44';
    CHECK_EQ(at_path<vehicle, "$.code">(text_head).error(), error::incorrect_type);
    CHECK_EQ(form<vehicle>::decode(std::string(text_head)).error(), error::incorrect_type);
    std::string const spare = schema_bytes(sample_everything);
    std::size_t const engines = spare.find("\x82\xd9\x05\xdd"s);
    REQUIRE_NE(engines, std::string::npos);
    std::string array_head = spare;
    array_head[engines] = '\x83';
    CHECK_EQ(at_path<everything, "$.engines[1].cc">(array_head).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::decode(std::string(array_head)).error(), error::incorrect_type);
}

// Owner decision 2026-10-18: the unverified_ forms read a message from a trusted peer without the checks of its
// data, as FlatBuffers does without its Verifier. On a message that the encoder wrote they give what the verified
// forms give.
TEST_CASE("unverified: the same results as the verified forms on a message that the encoder wrote")
{
    std::string const bytes = schema_bytes(sample_everything);
    CHECK_EQ(form<everything>::unverified_at_path<"$.big">(bytes), at_path<everything, "$.big">(bytes));
    CHECK_EQ(form<everything>::unverified_at_path<"$.engines[1].cc">(bytes), 1200u);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz), -6);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[].airPressure">(bytes, 1uz), 3.0f);
    CHECK_EQ(form<everything>::unverified_at_path<"$.maybe">(bytes).value(), std::optional<std::int32_t>{-9});
    auto const root = form<everything>::unverified_path(std::string(bytes));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.text">(), "text"sv);
    CHECK_EQ(root->at_path<"$.names[1]">(), "bc"sv);
    auto const inner = root->at_path<"$.inner">();
    REQUIRE(inner.has_value());
    REQUIRE(inner->has_value());
    CHECK_EQ((*inner)->at_path<"@.y">(), 2);
    auto const track = root->view<"$.track">();
    REQUIRE(track.has_value());
    CHECK_EQ(track->at(1), 2.0);
    auto const gauges = root->at_path<"$.gauges">();
    REQUIRE(gauges.has_value());
    std::vector<int> xs;
    for (auto const g : *gauges)
        xs.push_back(*g.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6}));
    CHECK_EQ(reencoded(form<everything>::unverified_decode(std::string(bytes))), bytes);
    auto const owner = std::make_shared<std::string const>(schema_bytes(sample_vehicle));
    CHECK_EQ(reencoded(form<vehicle>::unverified_decode(owner)), *owner);
}

// The unverified forms skip the checks of the data: a forged class tag, a forged head of a number, a forged tag of
// a typed array and a forged head of a fixed-size vector give a value where the verified forms give incorrect_type.
TEST_CASE("unverified: a forged type byte gives a value, not an error")
{
    std::string const bytes = schema_bytes(sample_everything);
    auto const forged = [&bytes](std::string const &find, std::size_t const at, char const to) {
        std::size_t const where = bytes.rfind(find);
        REQUIRE_NE(where, std::string::npos);
        std::string out = bytes;
        out[where + at] = to;
        return out;
    };
    std::string const root_tag = forged("\xd9\x06\x7c"s, 2, '\x7d');
    CHECK_EQ(at_path<everything, "$.flag">(root_tag).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::unverified_at_path<"$.flag">(root_tag), true);
    CHECK(form<everything>::unverified_decode(std::string(root_tag)).has_value());
    std::string const track_tag = forged("\xd8\x56\x5a"s, 1, '\x55');
    auto const verified = form<everything>::path(std::string(track_tag));
    REQUIRE(verified.has_value());
    CHECK_EQ(verified->view<"$.track">().error(), error::incorrect_type);
    auto const unverified = form<everything>::unverified_path(std::string(track_tag));
    REQUIRE(unverified.has_value());
    CHECK_EQ(unverified->view<"$.track">()->at(0), 1.0);
    std::string const vector_head = forged("\xd9\x80\x69"s, 1, '\x81');
    CHECK_EQ(at_path<everything, "$.gauges[1].on">(vector_head).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[1].on">(vector_head), false);
}

// An accessor inside an optional shares the owner of the message like every other accessor, so a read through it
// does not see a moved-from accessor.
TEST_CASE("schema: an accessor inside an optional reads its fields")
{
    auto const root = form<everything>::path(schema_bytes(sample_everything));
    REQUIRE(root.has_value());
    auto const inner = root->at_path<"$.inner">();
    REQUIRE(inner.has_value());
    REQUIRE(inner->has_value());
    CHECK_EQ((*inner)->at_path<"@.x">(), 1);
    CHECK_EQ((*inner)->at_path<"@.y">(), 2);
}

// The check for a moved-from accessor compares the control block of the owner, not its count. An owner that shares a
// control block and holds a null pointer is still an owner; an owner with no control block is empty.
TEST_CASE("schema: an owner is empty only when it has no control block")
{
    std::string const bytes = schema_bytes(sample_everything);
    auto const held = std::make_shared<std::string const>(bytes);
    auto const alias = std::shared_ptr<void const>(held, nullptr);
    auto root = form<everything>::path(alias, std::string_view(*held));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.text">(), "text"sv);

    auto const moved = std::move(*root);
    CHECK_EQ(moved.at_path<"$.text">(), "text"sv);
    CHECK_THROWS_AS((void)root->at_path<"$.text">(), std::logic_error);
    CHECK_THROWS_AS(std::ignore = form<everything>::path(std::shared_ptr<void const>(), std::string_view(bytes)), std::logic_error);
    auto unverified = form<everything>::unverified_path(held);
    REQUIRE(unverified.has_value());
    auto const taken = std::move(*unverified);
    CHECK_THROWS_AS((void)unverified->at_path<"$.text">(), std::logic_error);
}

// The unverified forms keep every check of a bound: a cut message, an index past the end and a length past the
// end give the same error in both forms.
TEST_CASE("unverified: a bound is checked as in the verified forms")
{
    std::string const bytes = schema_bytes(sample_everything);
    CHECK_EQ(form<everything>::unverified_path(bytes.substr(0, 20)).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_decode(bytes.substr(0, 20)).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 2uz).error(),
             error::index_out_of_bounds);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[].diameter">(bytes, 2uz).error(),
             error::index_out_of_bounds);
    std::string longer = bytes;
    std::size_t vectors = 0;
    for (std::size_t head = longer.find("\xd9\x80\x69"s); head != std::string::npos;
         head = longer.find("\xd9\x80\x69"s, head + 1), ++vectors)
        longer.replace(longer.find('\x5a', head) + 1, 4, "\xff\xff\xff\xff"s);
    CHECK_EQ(vectors, 2);
    CHECK_EQ(form<everything>::at_path<"$.gauges[0].on">(longer).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[0].on">(longer).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[0].diameter">(longer).error(), error::too_little_data);
}

// Every accessor that an unverified accessor gives is unverified, and every accessor that a verified accessor
// gives is verified, so a read never changes its form in the middle of a path.
TEST_CASE("unverified: the form of an accessor passes to its parts")
{
    using unverified = unverified_root<everything>;
    using verified = form<everything>::accessor<>;
    CHECK(std::same_as<unverified, form<everything>::accessor<everything, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.gauges">())>,
                       form<everything>::accessor<std::vector<gauge>, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.gauges[]">(0uz))>,
                       form<everything>::accessor<cbor::fixed_element<gauge>, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.engines[0]">())>,
                       form<everything>::accessor<engine, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<verified const &>().at_path<"$.gauges[]">(0uz))>,
                       form<everything>::accessor<cbor::fixed_element<gauge>>>);
}

// The unverified forms read where the data says, so the test changes every byte of a message that holds every kind
// of part, and cuts it at every length. Each call must return; in the build with AddressSanitizer and
// UndefinedBehaviorSanitizer no read leaves the input.
TEST_CASE("unverified: every changed byte and every cut stays inside the input")
{
    std::string const bytes = schema_bytes(sample_everything);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        for (char const to : {'\x00', '\xff', static_cast<char>(~bytes[i]), '\x7f', '\x80'}) {
            std::string changed = bytes;
            changed[i] = to;
            unverified_reads(changed);
        }
    for (std::size_t n = 0; n < bytes.size(); ++n)
        unverified_reads(bytes.substr(0, n));
    std::size_t const directory = bytes.find('\x5a');
    REQUIRE_NE(directory, std::string::npos);
    for (std::uint32_t const entry : {0u, 1u, 0x7fffffffu, 0xffffffffu, static_cast<std::uint32_t>(bytes.size())})
        for (std::size_t at = directory + 5; at + 4 <= bytes.size() && at < directory + 5 + 4 * 8; at += 4) {
            std::string changed = bytes;
            for (std::size_t k = 0; k < 4; ++k)
                changed[at + k] = static_cast<char>(entry >> (24 - 8 * k));
            unverified_reads(changed);
        }
    for (std::uint32_t const length : {0u, 3u, 4u, 0xffffffffu}) {
        std::string changed = bytes;
        for (std::size_t k = 0; k < 4; ++k)
            changed[directory + 1 + k] = static_cast<char>(length >> (24 - 8 * k));
        unverified_reads(changed);
    }
}

// A bool in a fixed-size struct is one byte. The verified form refuses a byte other than 0 and 1; the unverified
// form reads any byte other than 0 as true.
TEST_CASE("unverified: a bool byte 2 reads true")
{
    std::string const flags = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    std::size_t const element = flags.find("\x03\xfe\x01\x01\x00"s);
    REQUIRE_NE(element, std::string::npos);
    std::string two = flags;
    two[element + 2] = '\x02';
    CHECK_EQ(at_path<gauges, "$.g[0].on">(two).error(), error::incorrect_type);
    CHECK_EQ(form<gauges>::unverified_at_path<"$.g[0].on">(two), true);
    auto const back = form<gauges>::unverified_decode(std::string(two));
    REQUIRE(back.has_value());
    CHECK((*back)->g[0].on);
}


namespace
{

constexpr auto apart = cbor::description_place::apart;

template <class T>
std::string apart_bytes(T const &value)
{
    auto const bytes = form<T>::template encode<apart>(value);
    REQUIRE(bytes.has_value());
    return *bytes;
}

template <class T>
void check_description_apart(T const &value)
{
    std::string const inside = schema_bytes(value);
    std::string const data = apart_bytes(value);
    auto const description = form<T>::description();
    CHECK_EQ(data.size() + description.size(), inside.size());
    CHECK(std::ranges::equal(description, std::span(inside).subspan(8, description.size())));
    check_one_item(data);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(std::string(data))), inside);
    CHECK_EQ(reencoded(form<T>::template unverified_decode<apart>(std::string(data))), inside);
    auto const owner = std::make_shared<std::string const>(data);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(owner)), inside);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(std::shared_ptr<void const>(owner), std::string_view(*owner))), inside);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(owner, T{})), inside);
    CHECK(form<T>::template path<apart>(std::string(data)).has_value());
    CHECK(form<T>::template path<apart>(owner).has_value());
    CHECK(form<T>::template unverified_path<apart>(owner).has_value());
}

} // namespace

// Owner decision 2026-10-20: the description of a struct is stored once, apart from the data of each key, so the
// data does not repeat it. The data form is the message without the table entries of the description; this
// library reads it with the type, which knows the description at compile time.
TEST_CASE("description apart: the data and the description together hold what the message holds")
{
    check_description_apart(sample_everything);
    check_description_apart(sample_vehicle);
    check_description_apart(empty{});
    check_description_apart(gauges{});
    check_description_apart(fifteen_entries{});
    eighteen_entries points{};
    points.p = {{1, 2}, {3, 4}};
    check_description_apart(points);
}

// Every read form gives on the data form what it gives on the message.
TEST_CASE("description apart: each read form reads the data form")
{
    std::string const data = apart_bytes(sample_everything);
    CHECK_EQ((form<everything>::at_path<"$.big", apart>(data)), -5);
    CHECK_EQ((form<everything>::at_path<"$.gauges[].at.x", apart>(data, 1uz)), -6);
    CHECK_EQ((form<everything>::unverified_at_path<"$.tires[].airPressure", apart>(data, 1uz)), 3.0f);
    auto const root = form<everything>::path<apart>(std::string(data));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.text">(), "text"sv);
    CHECK_EQ(root->at_path<"$.names[1]">(), "bc"sv);
    CHECK_EQ(root->view<"$.track">()->at(1), 2.0);
    CHECK_EQ(root->at_path<"$.owners">()->size(), 2u);
    auto const inner = root->at_path<"$.inner">();
    REQUIRE(inner.has_value());
    REQUIRE(inner->has_value());
    CHECK_EQ((*inner)->at_path<"@.y">(), 2);
    std::vector<int> xs;
    for (auto const g : *root->at_path<"$.gauges">())
        xs.push_back(*g.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6}));
    CHECK_EQ(root->at_path<"$.tires[1].diameter">(), 18u);
    auto const unverified = form<everything>::unverified_path<apart>(std::string(data));
    REQUIRE(unverified.has_value());
    CHECK_EQ(unverified->at_path<"$.names[0]">(), "a"sv);
}

// Each target of encode writes the same data form.
TEST_CASE("description apart: every target of encode writes the same bytes")
{
    std::string const data = apart_bytes(sample_everything);
    std::string appended = "ab";
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, appended), data.size());
    CHECK_EQ(appended, "ab" + data);
    std::vector<char> chars;
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, chars), data.size());
    CHECK(std::ranges::equal(chars, data));
    std::vector<char> room(data.size() + 64);
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, std::span<char>(room)), data.size());
    CHECK(std::ranges::equal(std::span(room).first(data.size()), data));
    std::vector<char> exact(data.size());
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, std::span<char>(exact)), data.size());
    CHECK(std::ranges::equal(exact, data));
    string_writer w;
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, w), data.size());
    CHECK_EQ(w.encoded, data);
}

// A message read as the data form and the data form read as a message are refused: the byte after the head of
// the table is the head of the directory in one form and the first entry of the description in the other. A short
// input ends as too_little_data before that byte is read.
TEST_CASE("description apart: one form read as the other is refused")
{
    for (std::string const &inside : {schema_bytes(sample_everything), schema_bytes(empty{}), schema_bytes(eighteen_entries{})}) {
        auto const as_data = form<everything>::path<apart>(std::string(inside));
        REQUIRE_FALSE(as_data.has_value());
    }
    std::string const inside = schema_bytes(sample_everything);
    std::string const data = apart_bytes(sample_everything);
    CHECK_EQ(form<everything>::path<apart>(std::string(inside)).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::decode<apart>(std::string(inside)).error(), error::incorrect_type);
    CHECK_EQ((form<everything>::at_path<"$.big", apart>(inside)).error(), error::incorrect_type);
    for (auto const e : {form<everything>::path(std::string(data)).error(),
                         form<everything>::decode(std::string(data)).error()})
        CHECK((e == error::incorrect_type || e == error::too_little_data));
    std::string const small = apart_bytes(empty{});
    CHECK_EQ(form<empty>::path(std::string(small)).error(), error::too_little_data);
}

// The data form keeps every check of the message: a cut gives too_little_data, a forged directory entry and a
// forged filler give an error in the verified forms.
TEST_CASE("description apart: the checks of the message hold for the data form")
{
    std::string const data = apart_bytes(sample_everything);
    for (std::size_t n = 0; n < data.size(); ++n) {
        CHECK_FALSE(form<everything>::decode<apart>(data.substr(0, n)).has_value());
        CHECK_FALSE(form<everything>::path<apart>(data.substr(0, n)).has_value());
    }
    std::size_t const directory = data.find('\x5a');
    REQUIRE_EQ(directory, 8u);
    std::string entry = data;
    for (std::size_t k = 0; k < 4; ++k)
        entry[directory + 5 + k] = '\xff';
    CHECK_FALSE(form<everything>::decode<apart>(std::string(entry)).has_value());
    std::uint32_t const length = (std::uint32_t{static_cast<unsigned char>(data[directory + 3])} << 8) |
                                 static_cast<unsigned char>(data[directory + 4]);
    std::string filler = data;
    REQUIRE_EQ(filler[directory + 5 + length], '\xf7');
    filler[directory + 5 + length] = '\xf6';
    CHECK_EQ(form<everything>::path<apart>(std::string(filler)).error(), error::incorrect_type);
}

// The unverified forms read the data form inside its bounds: every changed byte and every cut, and a message read
// as the data form, stay inside the input under AddressSanitizer.
TEST_CASE("description apart: the unverified forms stay inside the input")
{
    std::string const data = apart_bytes(sample_everything);
    auto const reads = [](std::string const &bytes) {
        (void)form<everything>::unverified_at_path<"$.gauges[].at.x", apart>(bytes, 1uz);
        (void)form<everything>::unverified_decode<apart>(std::string(bytes));
        if (auto const root = form<everything>::unverified_path<apart>(std::string(bytes))) {
            (void)root->at_path<"$.names[1]">();
            (void)root->at_path<"$.tires[1].diameter">();
            if (auto const track = root->view<"$.track">())
                for (double const d : *track)
                    (void)d;
        }
    };
    for (std::size_t i = 0; i < data.size(); ++i)
        for (char const to : {'\x00', '\xff', static_cast<char>(~data[i])}) {
            std::string changed = data;
            changed[i] = to;
            reads(changed);
        }
    for (std::size_t n = 0; n < data.size(); ++n)
        reads(data.substr(0, n));
    reads(schema_bytes(sample_everything));
}


namespace
{

class reading
{
public:
    reading(std::uint16_t const v, std::string u) : value(v), unit(std::move(u))
    {
    }

    std::uint16_t value;
    std::string unit;
};

struct reading_plain {
    std::uint16_t value;
    std::string unit;
};

struct station {
    std::vector<reading> readings;
    std::optional<reading> first;
    std::map<std::uint16_t, reading> by_hour;
};

class pmr_reading
{
public:
    explicit pmr_reading(std::pmr::string u) : unit(std::move(u))
    {
    }

    std::pmr::string unit;
};

struct pmr_station {
    std::pmr::vector<pmr_reading> readings;
};

} // namespace

template <>
struct cbor::describe_members<reading> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1670};
    static constexpr std::tuple members{
        CBOR_MEMBER(reading, value),
        CBOR_MEMBER(reading, unit),
    };
};

template <>
struct cbor::describe_members<reading_plain> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1670};
    static constexpr std::tuple members{
        CBOR_MEMBER(reading_plain, value),
        CBOR_MEMBER(reading_plain, unit),
    };
};

template <>
struct cbor::describe_members<station> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1671};
    static constexpr std::tuple members{
        CBOR_MEMBER(station, readings),
        CBOR_MEMBER(station, first),
        CBOR_MEMBER(station, by_hour),
    };
};

template <>
struct cbor::describe_members<pmr_reading> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1672};
    static constexpr std::tuple members{
        CBOR_MEMBER(pmr_reading, unit),
    };
};

template <>
struct cbor::describe_members<pmr_station> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1673};
    static constexpr std::tuple members{
        CBOR_MEMBER(pmr_station, readings),
    };
};

// A class with no default constructor is built by its constructor, which takes the described members in the order
// of the description, as mrb_cpp_new of mruby-c-ext-helpers forwards its arguments to new T(...). The wire does not
// show how a class is built, so the same description gives the bytes of an aggregate. The key order (unit before
// value) differs from the order of the description, so a mix-up of the two orders shows here.
TEST_CASE("decode: a class with a constructor takes its described members in the order of the description")
{
    std::string const bytes = schema_bytes(reading{7, "kg"});
    CHECK_EQ(bytes, schema_bytes(reading_plain{7, "kg"}));
    auto const back = form<reading>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((**back).value, 7u);
    CHECK_EQ((**back).unit, "kg");
    auto const unverified = form<reading>::unverified_decode(std::make_shared<std::string const>(bytes));
    REQUIRE(unverified.has_value());
    CHECK_EQ((**unverified).unit, "kg");
    CHECK_EQ(at_path<reading, "$.unit">(bytes), "kg");
}

// Each place that makes an element builds a class with a constructor the same way: an element of a vector, the
// value of an optional and the value of a map.
TEST_CASE("decode: a class with a constructor in a vector, an optional and a map")
{
    station const full{{reading{1, "a"}, reading{2, "bb"}}, reading{3, "ccc"}, {{9, reading{4, "dddd"}}}};
    std::string const bytes = schema_bytes(full);
    auto const back = form<station>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    REQUIRE_EQ((**back).readings.size(), 2u);
    CHECK_EQ((**back).readings[1].value, 2u);
    CHECK_EQ((**back).readings[1].unit, "bb");
    REQUIRE((**back).first.has_value());
    CHECK_EQ((**back).first->unit, "ccc");
    REQUIRE_EQ((**back).by_hour.size(), 1u);
    CHECK_EQ((**back).by_hour.at(9).unit, "dddd");
    CHECK_EQ(at_path<station, "$.readings[1].unit">(bytes), "bb");
    CHECK_EQ(reencoded(form<station>::decode(std::string(bytes))), bytes);
    auto const empty = form<station>::decode(schema_bytes(station{{}, std::nullopt, {}}));
    REQUIRE(empty.has_value());
    CHECK((**empty).readings.empty());
    CHECK_FALSE((**empty).first.has_value());
}

// The arguments of the constructor are made with the allocator of the vector (std::tuple with std::allocator_arg,
// [tuple.cnstr]), so a std::pmr member keeps the resource of the vector through the move into the class.
TEST_CASE("decode: a class with a constructor in a std::pmr::vector takes the resource of the vector")
{
    std::string const long_unit(40, 'u');
    pmr_station value;
    value.readings.emplace_back(std::pmr::string(long_unit));
    value.readings.emplace_back(std::pmr::string(long_unit));
    std::string const bytes = schema_bytes(value);
    counting_resource fallback;
    counting_resource target;
    auto const owner = std::make_shared<std::string const>(bytes);
    std::pmr::memory_resource *const was = std::pmr::set_default_resource(&fallback);
    auto const decoded = form<pmr_station>::decode(owner, pmr_station{std::pmr::vector<pmr_reading>(&target)});
    std::pmr::set_default_resource(was);
    REQUIRE(decoded.has_value());
    REQUIRE_EQ((**decoded).readings.size(), 2u);
    for (pmr_reading const &r : (**decoded).readings) {
        CHECK_EQ(r.unit, long_unit);
        CHECK_EQ(r.unit.get_allocator().resource(), &target);
    }
    CHECK_EQ(fallback.allocations, 0u);
}

namespace
{

template <class T>
void golden_fill(T &value, unsigned &seed)
{
    seed += 1;
    if constexpr (cbor::has_describe_members<T>) {
        std::apply([&](auto const &...m) { (golden_fill(value.*m.pointer, seed), ...); }, cbor::describe_members<T>::members);
    } else if constexpr (std::same_as<T, bool>) {
        value = seed % 2 == 1;
    } else if constexpr (std::is_enum_v<T>) {
        value = static_cast<T>(seed % 2);
    } else if constexpr (std::same_as<T, cbor::int128>) {
        value = -static_cast<cbor::int128>(seed) * 1000003;
    } else if constexpr (std::is_arithmetic_v<T>) {
        value = static_cast<T>(seed * 37u);
#if defined(__STDCPP_FLOAT16_T__)
    } else if constexpr (std::same_as<T, std::float16_t>) {
        value = static_cast<std::float16_t>(seed) + std::float16_t{0.5f};
#endif
    } else if constexpr (std::same_as<T, std::byte>) {
        value = static_cast<std::byte>(seed);
    } else if constexpr (std::same_as<T, std::string_view>) {
        value = std::string_view("golden text").substr(seed % 7);
    } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::pmr::string>) {
        value.assign(seed % 5 + 1, static_cast<char>('a' + seed % 26));
    } else if constexpr (std::is_array_v<T> || requires { std::tuple_size<T>::value; }) {
        for (auto &e : value)
            golden_fill(e, seed);
    } else if constexpr (requires { value.has_value(); }) {
        golden_fill(value.emplace(), seed);
    } else if constexpr (requires { typename T::mapped_type; }) {
        for (unsigned i = 0; i < 2; ++i) {
            typename T::key_type k{};
            typename T::mapped_type v{};
            golden_fill(k, seed);
            golden_fill(v, seed);
            value.try_emplace(std::move(k), std::move(v));
        }
    } else {
        for (unsigned i = 0; i < 2; ++i)
            golden_fill(value.emplace_back(), seed);
    }
}

std::string golden_hex(std::string_view const bytes)
{
    std::string out;
    for (char const c : bytes) {
        constexpr std::string_view digits = "0123456789abcdef";
        auto const byte = static_cast<std::size_t>(static_cast<unsigned char>(c));
        out += digits[byte >> 4];
        out += digits[byte & 0xf];
    }
    return out;
}

template <class T>
std::string golden_line(std::string_view const name)
{
    T value{};
    unsigned seed = 0;
    golden_fill(value, seed);
    auto const inside = form<T>::encode(value);
    auto const data = form<T>::template encode<cbor::description_place::apart>(value);
    if (!inside || !data)
        return std::string(name) + " no encoding\n";
    auto const description = form<T>::description();
    return std::string(name) + " " + golden_hex(*inside) + " " + golden_hex(*data) + " " +
           golden_hex(std::string_view(description.data(), description.size())) + "\n";
}

std::string golden_report()
{
    std::string report;
    report += golden_line<wheel>("wheel");
    report += golden_line<engine>("engine");
    report += golden_line<car>("car");
    report += golden_line<empty>("empty");
    report += golden_line<thirty>("thirty");
    report += golden_line<long_name>("long_name");
    report += golden_line<größe>("größe");
    report += golden_line<login>("login");
    report += golden_line<measures>("measures");
    report += golden_line<named>("named");
    report += golden_line<people>("people");
    report += golden_line<pmr_named>("pmr_named");
    report += golden_line<pmr_people>("pmr_people");
    report += golden_line<unsorted>("unsorted");
    report += golden_line<unsorted_other_order>("unsorted_other_order");
    report += golden_line<renamed>("renamed");
    report += golden_line<listed_other_order>("listed_other_order");
    report += golden_line<sorted_inner>("sorted_inner");
    report += golden_line<sorted_outer>("sorted_outer");
    report += golden_line<tire>("tire");
    report += golden_line<vehicle>("vehicle");
    report += golden_line<badge>("badge");
#if defined(__STDCPP_FLOAT16_T__)
    report += golden_line<numbers>("numbers");
#endif
    report += golden_line<doubles>("doubles");
    report += golden_line<garage>("garage");
    report += golden_line<keyed>("keyed");
    report += golden_line<passkey_user>("passkey_user");
    report += golden_line<passkey_login>("passkey_login");
    report += golden_line<chain19>("chain19");
    report += golden_line<chain10>("chain10");
    report += golden_line<chain0>("chain0");
    report += golden_line<point_a>("point_a");
    report += golden_line<point_b>("point_b");
    report += golden_line<ticket>("ticket");
    report += golden_line<probe>("probe");
    report += golden_line<lot_wheel>("lot_wheel");
    report += golden_line<lot_engine>("lot_engine");
    report += golden_line<lot_car>("lot_car");
    report += golden_line<parking_lot>("parking_lot");
    report += golden_line<point>("point");
    report += golden_line<cloud>("cloud");
    report += golden_line<gauge>("gauge");
    report += golden_line<gauges>("gauges");
    report += golden_line<filler_type<3>>("filler_type<3>");
    report += golden_line<fifteen_entries>("fifteen_entries");
    report += golden_line<eighteen_entries>("eighteen_entries");
    report += golden_line<everything>("everything");
    return report;
}

} // namespace

// The bytes of every described type of this file, with values that differ in each leaf, as the commit before
// cbor::packed wrote them (dc787da): the message with the description inside, the data form apart, and the
// description. A change of the walk that changes one byte fails here.
TEST_CASE("golden bytes: every type of this file encodes as before")
{
    std::string expected(golden_expected);
#if !defined(__STDCPP_FLOAT16_T__)
    std::size_t const at = expected.find("\nnumbers ") + 1;
    expected.erase(at, expected.find('\n', at) + 1 - at);
#endif
    CHECK_EQ(golden_report(), expected);
}


namespace
{

struct byte_fields {
    std::array<std::uint8_t, 3> fixed;
    std::uint8_t plain[2];
    std::array<std::byte, 2> bytes;
    std::vector<std::uint8_t> list;
    std::vector<std::byte> byte_list;
    std::array<std::uint8_t, 0> none;
};

struct byte_record {
    std::string name;
    std::array<std::uint8_t, 4> code;
};

struct byte_lists {
    std::vector<byte_record> records;
    std::vector<std::array<std::uint8_t, 4>> codes;
};

} // namespace

CBOR_DEFINE_TYPE(byte_fields, cbor::tag{1733}, CBOR_MEMBER(byte_fields, fixed), CBOR_MEMBER(byte_fields, plain),
                 CBOR_MEMBER(byte_fields, bytes), CBOR_MEMBER(byte_fields, list), CBOR_MEMBER(byte_fields, byte_list),
                 CBOR_MEMBER(byte_fields, none));
CBOR_DEFINE_TYPE(byte_record, cbor::tag{1734}, CBOR_MEMBER(byte_record, name), CBOR_MEMBER(byte_record, code));
CBOR_DEFINE_TYPE(byte_lists, cbor::tag{1735}, CBOR_MEMBER(byte_lists, records), CBOR_MEMBER(byte_lists, codes));

namespace
{

template <class T, cbor::fixed_string Path, class... Index>
concept message_path_reads = requires(std::string_view const s, Index const... i) { form<T>::template at_path<Path>(s, i...); };

byte_fields byte_fields_value()
{
    return byte_fields{{1, 2, 3}, {4, 5}, {std::byte{6}, std::byte{7}}, {8, 9}, {std::byte{10}}, {}};
}

} // namespace

// A user reads one byte of an array of bytes with [n], as one number of a list. The array is one byte string in
// the message: inline in the root for a fixed size, a part for a vector. The byte is the end of the path.
TEST_CASE("at_path: [n] reads one byte of a byte string")
{
    std::string const bytes = *form<byte_fields>::encode(byte_fields_value());
    CHECK_EQ(form<byte_fields>::at_path<"$.fixed[2]">(bytes), std::uint8_t{3});
    CHECK_EQ(form<byte_fields>::at_path<"$.plain[1]">(bytes), std::uint8_t{5});
    CHECK_EQ(form<byte_fields>::at_path<"$.bytes[1]">(bytes), std::byte{7});
    CHECK_EQ(form<byte_fields>::at_path<"$.list[1]">(bytes), std::uint8_t{9});
    CHECK_EQ(form<byte_fields>::at_path<"$.byte_list[0]">(bytes), std::byte{10});
    CHECK_EQ(form<byte_fields>::at_path<"$.fixed[]">(bytes, 1uz), std::uint8_t{2});
    CHECK_EQ(form<byte_fields>::at_path<"$.list[]">(bytes, 0uz), std::uint8_t{8});
    CHECK_EQ(form<byte_fields>::at_path<"$.fixed[]">(bytes, 3uz).error(), error::index_out_of_bounds);
    CHECK_EQ(form<byte_fields>::at_path<"$.list[]">(bytes, 2uz).error(), error::index_out_of_bounds);
    CHECK_EQ(form<byte_fields>::at_path<"$.none[]">(bytes, 0uz).error(), error::index_out_of_bounds);
    CHECK_EQ(form<byte_fields>::unverified_at_path<"$.fixed[2]">(bytes), std::uint8_t{3});
    CHECK_EQ(form<byte_fields>::unverified_at_path<"$.list[1]">(bytes), std::uint8_t{9});
    auto const root = form<byte_fields>::path(std::string(bytes));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.fixed[0]">(), std::uint8_t{1});
    CHECK_EQ(root->at_path<"$.list[0]">(), std::uint8_t{8});
    std::string const lists = *form<byte_lists>::encode(byte_lists{{{"x", {1, 2, 3, 4}}, {"y", {5, 6, 7, 8}}}, {{9, 10, 11, 12}}});
    CHECK_EQ(form<byte_lists>::at_path<"$.records[1].code[2]">(lists), std::uint8_t{7});
    CHECK_EQ(form<byte_lists>::at_path<"$.codes[0][3]">(lists), std::uint8_t{12});
    auto const records = form<byte_lists>::path(std::string(lists));
    REQUIRE(records.has_value());
    auto const second = records->at_path<"$.records[1]">();
    REQUIRE(second.has_value());
    CHECK_EQ(second->at_path<"@.code[0]">(), std::uint8_t{5});
}

// A path ends at the byte, and a literal index lies inside a byte string of fixed size. A text stays a text.
TEST_CASE("at_path: [n] into a byte string does not compile past its end or into a text")
{
    CHECK(message_path_reads<byte_fields, "$.fixed[2]">);
    CHECK_FALSE(message_path_reads<byte_fields, "$.fixed[3]">);
    CHECK_FALSE(message_path_reads<byte_fields, "$.none[0]">);
    CHECK_FALSE(message_path_reads<byte_fields, "$.fixed[0].x">);
    CHECK_FALSE(message_path_reads<byte_fields, "$.list[0][0]">);
    CHECK_FALSE(message_path_reads<byte_record, "$.name[0]">);
}

// A verified read checks the head of a byte string before it reads a byte of it.
TEST_CASE("at_path: [n] into a byte string with a wrong head is incorrect_type")
{
    std::string bytes = *form<byte_fields>::encode(byte_fields_value());
    std::size_t const head = bytes.find("\x43\x01\x02\x03"sv);
    REQUIRE_NE(head, std::string::npos);
    bytes[head] = '\x63';
    CHECK_EQ(form<byte_fields>::at_path<"$.fixed[2]">(bytes).error(), error::incorrect_type);
}

// The root of a message stands at its end. A message with a missing end gave incorrect_type, because the last bytes
// are not the root. Every verified entry gives too_little_data for each length that cuts the message.
TEST_CASE("a message cut at any length is too_little_data")
{
    std::string const bytes = schema_bytes(reading{7, "kg"});
    std::string const apart_bytes = *form<byte_fields>::template encode<cbor::description_place::apart>(byte_fields_value());
    std::string const whole = *form<byte_fields>::encode(byte_fields_value());
    for (std::size_t n = 0; n < whole.size(); ++n) {
        CAPTURE(n);
        std::string const cut = whole.substr(0, n);
        CHECK_EQ(form<byte_fields>::decode(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::decode(std::make_shared<std::string const>(cut), byte_fields{}).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::path(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::at_path<"$.list[1]">(cut).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::at_path<"$.list[]">(cut, 1uz).error(), error::too_little_data);
    }
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CAPTURE(n);
        CHECK_EQ(form<reading>::decode(bytes.substr(0, n)).error(), error::too_little_data);
    }
    for (std::size_t n = 0; n < apart_bytes.size(); ++n) {
        CAPTURE(n);
        std::string const cut = apart_bytes.substr(0, n);
        CHECK_EQ(form<byte_fields>::template decode<cbor::description_place::apart>(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::template at_path<"$.list[1]", cbor::description_place::apart>(cut).error(), error::too_little_data);
    }
}

// Only a cut message of the type is too_little_data: a message of another type, a message with a byte after it and
// text that is not CBOR keep their error.
TEST_CASE("a whole message of another type keeps its error")
{
    std::string const whole = *form<byte_fields>::encode(byte_fields_value());
    CHECK_EQ(form<reading>::decode(std::string(whole)).error(), error::incorrect_type);
    CHECK_EQ(form<byte_fields>::decode(whole + "\x01").error(), error::incorrect_type);
    std::string const text(whole.size(), '{');
    CHECK_EQ(form<byte_fields>::decode(std::string(text)).error(), error::incorrect_type);
}

namespace
{

struct nan_fields {
    float single;
    double twice;
};

} // namespace

CBOR_DEFINE_TYPE(nan_fields, cbor::tag{1736}, CBOR_MEMBER(nan_fields, single), CBOR_MEMBER(nan_fields, twice));

// Owner decision 2026-10-12: a NaN keeps its bits. A signaling NaN of a float and a double, with its payload in the
// lowest bit, goes through encode, decode and at_path with every bit; a conversion of the processor would set the
// quiet bit.
TEST_CASE("schema: a NaN keeps its bits through encode, decode and at_path")
{
    nan_fields const value{std::bit_cast<float>(0x7f800001u), std::bit_cast<double>(0x7ff0000000000001ull)};
    std::string const bytes = *form<nan_fields>::encode(value);
    auto const back = form<nan_fields>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ(std::bit_cast<std::uint32_t>((*back)->single), 0x7f800001u);
    CHECK_EQ(std::bit_cast<std::uint64_t>((*back)->twice), 0x7ff0000000000001ull);
    CHECK_EQ(std::bit_cast<std::uint32_t>(*form<nan_fields>::at_path<"$.single">(bytes)), 0x7f800001u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*form<nan_fields>::at_path<"$.twice">(bytes)), 0x7ff0000000000001ull);
}

#if defined(__STDCPP_BFLOAT16_T__)

namespace
{

struct brain_float {
    std::bfloat16_t value;
};

} // namespace

CBOR_DEFINE_TYPE(brain_float, cbor::tag{1737}, CBOR_MEMBER(brain_float, value));

// A bfloat16 is the top 16 bits of a binary32, so the wire takes its bits shifted, not converted. A conversion through
// float set the quiet bit of the signaling NaN 7f81 (7fc10000), and a read through float lost its payload.
TEST_CASE("schema: a bfloat16 NaN keeps its bits")
{
    brain_float const value{std::bit_cast<std::bfloat16_t>(std::uint16_t{0x7f81})};
    std::string const bytes = *form<brain_float>::encode(value);
    CHECK_NE(bytes.find("\xfa\x7f\x81\x00\x00"s), std::string::npos);
    auto const back = form<brain_float>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ(std::bit_cast<std::uint16_t>((*back)->value), 0x7f81u);
    CHECK_EQ(std::bit_cast<std::uint16_t>(*form<brain_float>::at_path<"$.value">(bytes)), 0x7f81u);
}

#endif
