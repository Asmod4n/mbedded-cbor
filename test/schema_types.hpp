#pragma once

#include "reference.hpp"

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

namespace schema_test
{

template <class X>
using form = cbor::schema<X>;

} // namespace schema_test

using namespace schema_test;

namespace schema_test
{

enum class color : std::uint16_t { black, white };

struct wheel {
    std::uint16_t diameter;
    float airPressure;
    bool snowTires;
};

} // namespace schema_test

template <>
struct cbor::describe_members<wheel> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1500};
    static constexpr std::tuple members{
        CBOR_MEMBER(wheel, diameter),
        CBOR_MEMBER(wheel, airPressure),
        CBOR_MEMBER(wheel, snowTires),
    };
};

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
{

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
inline void check_one_item(std::string const &bytes)
{
    auto const end = cbor::item_size(bytes);
    REQUIRE(end.has_value());
    CHECK_EQ(*end, bytes.size());
}

} // namespace schema_test

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

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
{

inline vehicle const sample_vehicle{"Tesla", -7, {{{15, 2.5f}, {16, 2.0f}}}, {{17, 1.5f}, {18, 3.0f}, {19, 0.5f}},
                             {300, 1800}, {'A', 'B', 'C', 'D'}, std::uint8_t{9}, std::nullopt};

} // namespace schema_test

namespace schema_test
{

enum class shade : std::uint8_t { dark, light };

struct badge {
    std::array<std::byte, 4> mac;
    shade tone;
    std::string label;
};

} // namespace schema_test

template <>
struct cbor::describe_members<badge> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1513};
    static constexpr std::tuple members{
        CBOR_MEMBER(badge, mac),
        CBOR_MEMBER(badge, tone),
        CBOR_MEMBER(badge, label),
    };
};

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
{

inline numbers const sample_numbers{{0x0102},
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

} // namespace schema_test

template <>
struct cbor::describe_members<doubles> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1531};
    static constexpr std::tuple members{
        CBOR_MEMBER(doubles, v),
    };
};

namespace schema_test
{

struct garage {
    std::vector<tire> tires;
    std::vector<std::string> names;
    std::vector<std::vector<std::uint16_t>> rows;
    std::map<std::uint16_t, std::string> owners;
};

} // namespace schema_test

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

namespace schema_test
{

inline garage const sample_garage{{{17, 1.5f}, {18, 3.0f}, {19, 0.5f}},
                           {"a", "bc"},
                           {{1, 2}, {}, {3}},
                           {{7, "seven"}, {9, "nine"}}};

} // namespace schema_test

namespace schema_test
{

template <class T, cbor::fixed_string Path>
concept path_reads = requires(typename form<T>::template accessor<> const &a) { a.template at_path<Path>(); };

} // namespace schema_test

namespace schema_test
{

struct keyed {
    std::uint8_t m0;
    bool m1;
};

} // namespace schema_test

template <>
struct cbor::describe_members<keyed> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{65536};
    static constexpr std::tuple members{
        cbor::member{"x-user-id", &keyed::m0},
        cbor::member{"EOF", &keyed::m1},
    };
};

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
{

struct point_a {
    std::uint8_t x;
};

struct point_b {
    std::uint8_t x;
};

} // namespace schema_test

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

namespace schema_test
{

struct ticket {
    std::string_view holder;
    std::vector<std::string_view> seats;
    std::uint16_t row;
};

} // namespace schema_test

template <>
struct cbor::describe_members<ticket> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1522};
    static constexpr std::tuple members{
        CBOR_MEMBER(ticket, holder),
        CBOR_MEMBER(ticket, seats),
        CBOR_MEMBER(ticket, row),
    };
};

namespace schema_test
{

inline ticket const sample_ticket{"a holder name longer than the small string buffer", {"seat one of the long list", "seat two"}, 12};

inline std::string ticket_bytes()
{
    return *form<ticket>::encode(sample_ticket);
}

} // namespace schema_test

namespace schema_test
{

// The tests below attack the path reader with forged and cut messages. Every message is copied into a
// std::string, because the view form takes one. The address sanitizer reports a read past the capacity of that
// string, not a read of the bytes between its size and its capacity. Every text of the view form must lie inside the
// message.

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

} // namespace schema_test

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

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
{

inline parking_lot sample_lot()
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

} // namespace schema_test

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
{

inline bool same(gauge const &a, gauge const &b)
{
    return a.on == b.on && a.shade == b.shade && a.at.x == b.at.x && a.at.y == b.at.y && a.range == b.range;
}

} // namespace schema_test

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
{

inline everything const sample_everything{true,
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

} // namespace schema_test

namespace schema_test
{

inline constexpr auto apart = cbor::description_place::apart;

template <class T>
std::string apart_bytes(T const &value)
{
    auto const bytes = form<T>::template encode<apart>(value);
    REQUIRE(bytes.has_value());
    return *bytes;
}

} // namespace schema_test

namespace schema_test
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

} // namespace schema_test

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

namespace schema_test
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

} // namespace schema_test

CBOR_DEFINE_TYPE(byte_fields, cbor::tag{1733}, CBOR_MEMBER(byte_fields, fixed), CBOR_MEMBER(byte_fields, plain),
                 CBOR_MEMBER(byte_fields, bytes), CBOR_MEMBER(byte_fields, list), CBOR_MEMBER(byte_fields, byte_list),
                 CBOR_MEMBER(byte_fields, none));

CBOR_DEFINE_TYPE(byte_record, cbor::tag{1734}, CBOR_MEMBER(byte_record, name), CBOR_MEMBER(byte_record, code));

CBOR_DEFINE_TYPE(byte_lists, cbor::tag{1735}, CBOR_MEMBER(byte_lists, records), CBOR_MEMBER(byte_lists, codes));

namespace schema_test
{

inline byte_fields byte_fields_value()
{
    return byte_fields{{1, 2, 3}, {4, 5}, {std::byte{6}, std::byte{7}}, {8, 9}, {std::byte{10}}, {}};
}

} // namespace schema_test

namespace schema_test
{

struct nan_fields {
    float single;
    double twice;
};

} // namespace schema_test

CBOR_DEFINE_TYPE(nan_fields, cbor::tag{1736}, CBOR_MEMBER(nan_fields, single), CBOR_MEMBER(nan_fields, twice));

#if defined(__STDCPP_BFLOAT16_T__)

namespace schema_test
{

struct brain_float {
    std::bfloat16_t value;
};

} // namespace schema_test

CBOR_DEFINE_TYPE(brain_float, cbor::tag{1737}, CBOR_MEMBER(brain_float, value));

#endif
