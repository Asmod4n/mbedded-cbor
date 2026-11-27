#include "../reference.hpp"
#include <cbor/diagnostic_notation.hpp>
#include <cbor/error.hpp>
#include <cbor/structs.hpp>

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using cbor::error;

namespace structs_pointers
{
namespace
{

struct leaf {
    int v;
};

struct empty_pointers {
    leaf *r;
    std::unique_ptr<leaf> u;
    std::shared_ptr<leaf> s;
};

struct raw_triple {
    leaf *a;
    leaf *b;
    leaf *c;
};

struct numbers {
    int a;
    int b;
    int *p;
};

struct shared_pair {
    std::shared_ptr<leaf> a;
    std::shared_ptr<leaf const> b;
};

struct node {
    std::string name;
    node *parent;
    std::vector<std::unique_ptr<node>> children;
};

struct tree {
    int v;
    std::vector<std::unique_ptr<tree>> kids;
};

struct pointer_first {
    leaf *first;
    std::vector<leaf> nodes;
};

struct value_first {
    std::vector<leaf> items;
    leaf *focus_pointer;
};

struct plain {
    int a;
    std::vector<std::string> b;
    std::optional<int> c;
};

struct array_first {
    std::array<int, 4> a;
    std::array<int, 4> *p;
    int *i;
};

struct array_after_pointer {
    std::array<int, 4> array;
    std::array<int, 4> *p;
    int *i;
};

struct starts_with_array {
    std::array<int, 4> a;
    int x;
};

struct holds_struct {
    starts_with_array o;
    starts_with_array *p;
    int *i;
};

struct vector_and_int {
    std::vector<int> v;
    int a;
    int *p;
};

struct text_and_char {
    std::string t;
    char *p;
};

} // namespace

// The types below stand only in checks at compile time. Outside the unnamed namespace their descriptions are not
// unused declarations of internal linkage.
struct view_of_reached_char {
    std::string_view s;
    char *c;
};

struct view_of_other_type {
    std::string_view s;
    int *p;
};

struct span_of_reached_type {
    std::vector<leaf> v;
    std::span<leaf const> s;
    leaf *p;
};

struct value_with_pointer {
    int x;
};

struct value_of_int {
    int x;
};

struct reached_by_value {
    int *p;
    value_of_int d;
};

struct not_reached_by_value {
    leaf *p;
    value_of_int d;
};

namespace
{

std::string bytes_of(std::string_view const edn)
{
    auto const bytes = cbor::diag2cbor(edn);
    REQUIRE(bytes.has_value());
    return *bytes;
}

std::string edn_of_array(std::array<int, 4> const &array)
{
    auto const bytes = cbor::encode<std::array<int, 4>>(array);
    REQUIRE(bytes.has_value());
    auto const text = cbor::diagnostic_notation(*bytes);
    REQUIRE(text.has_value());
    return *text;
}

// Each entry of the encode writes the same bytes: cbor::encode into a new string, cbor::encode into a span,
// cbor::structs<T>::encode, and cbor::to_edn, whose text equals cbor::diagnostic_notation of these bytes.
template <class T>
void check_written(T const &value, std::string_view const edn)
{
    std::string const expected = bytes_of(edn);
    auto const encoded = cbor::encode<T>(value);
    REQUIRE(encoded.has_value());
    CHECK_EQ(*encoded, expected);
    std::array<char, 256> buffer{};
    auto const written = cbor::encode<T>(std::span<char>(buffer), value);
    REQUIRE(written.has_value());
    CHECK_EQ(std::string_view(buffer.data(), *written), expected);
    auto const by_structs = cbor::structs<T>::encode(value);
    REQUIRE(by_structs.has_value());
    CHECK_EQ(*by_structs, expected);
    auto const text = cbor::to_edn<T>(value);
    auto const notation = cbor::diagnostic_notation(expected);
    REQUIRE(text.has_value());
    REQUIRE(notation.has_value());
    CHECK_EQ(*text, *notation);
}

template <class T>
void check_refused(T const &value, error const expected)
{
    auto const encoded = cbor::encode<T>(value);
    REQUIRE_FALSE(encoded.has_value());
    CHECK_EQ(encoded.error(), expected);
    std::array<char, 256> buffer{};
    auto const written = cbor::encode<T>(std::span<char>(buffer), value);
    REQUIRE_FALSE(written.has_value());
    CHECK_EQ(written.error(), expected);
    auto const by_structs = cbor::structs<T>::encode(value);
    REQUIRE_FALSE(by_structs.has_value());
    CHECK_EQ(by_structs.error(), expected);
    auto const text = cbor::to_edn<T>(value);
    REQUIRE_FALSE(text.has_value());
    CHECK_EQ(text.error(), expected);
}

template <class X>
constexpr bool encodes = requires(X const &x, std::span<char> const out) {
    cbor::encode<X>(x);
    cbor::encode<X>(out, x);
    cbor::to_edn<X>(x);
};

template <class X>
constexpr bool structs_encodes = requires(X const &x, std::span<char> const out) {
    cbor::structs<X>::encode(x);
    cbor::structs<X>::encode(out, x);
};

} // namespace
} // namespace structs_pointers

CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::leaf, cbor::untagged, v);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::empty_pointers, cbor::untagged, r, u, s);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::raw_triple, cbor::untagged, a, b, c);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::numbers, cbor::untagged, a, b, p);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::shared_pair, cbor::untagged, a, b);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::node, cbor::untagged, name, parent, children);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::tree, cbor::untagged, v, kids);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::pointer_first, cbor::untagged, first, nodes);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::value_first, cbor::untagged, items, focus_pointer);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::plain, cbor::untagged, a, b, c);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::array_first, cbor::untagged, a, p, i);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::array_after_pointer, cbor::untagged, array, p, i);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::starts_with_array, cbor::untagged, a, x);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::holds_struct, cbor::untagged, o, p, i);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::vector_and_int, cbor::untagged, v, a, p);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::text_and_char, cbor::untagged, t, p);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::view_of_reached_char, cbor::untagged, s, c);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::view_of_other_type, cbor::untagged, s, p);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::span_of_reached_type, cbor::untagged, v, s, p);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::reached_by_value, cbor::untagged, p, d);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::not_reached_by_value, cbor::untagged, p, d);

template <>
struct cbor::describe_members<structs_pointers::value_with_pointer> {
    static int const *value(structs_pointers::value_with_pointer const &object)
    {
        return &object.x;
    }
};

template <>
struct cbor::describe_members<structs_pointers::value_of_int> {
    static int value(structs_pointers::value_of_int const &object)
    {
        return object.x;
    }
};

namespace structs_pointers
{

// RFC 8949 3.3: null is the CBOR form of an empty pointer. A pointer member is no std::optional, so its key stands
// with null for each of the three pointer kinds.
TEST_CASE("structs pointers: an empty pointer of each kind writes null")
{
    check_written(empty_pointers{}, R"({"r": null, "s": null, "u": null})");
    check_written<leaf *>(nullptr, "null");
}

// The owner decided that a tag 28 stands only on an object that the value reaches twice or more at run time. The
// object reached once stays without a tag, and the index of tag 29 counts only the tags 28 written before it.
TEST_CASE("structs pointers: only an object reached twice or more carries tag 28")
{
    numbers n{1, 2, nullptr};
    n.p = &n.b;
    check_written(n, R"({"a": 1, "b": 28(2), "p": 29(0)})");
    leaf x{1};
    leaf y{2};
    check_written(raw_triple{&x, &y, &y}, R"({"a": {"v": 1}, "b": 28({"v": 2}), "c": 29(0)})");
    check_written(raw_triple{&x, &x, nullptr}, R"({"a": 28({"v": 1}), "b": 29(0), "c": null})");
    check_written<leaf *>(&x, R"({"v": 1})");
}

// A parent pointer leads back to the root, which the value reaches in place and again through the pointer. The
// root carries tag 28, the child that is reached once carries none, and the encode ends at tag 29.
TEST_CASE("structs pointers: a cycle through a parent T* to the root")
{
    node root{"r", nullptr, {}};
    root.children.push_back(std::make_unique<node>(node{"c", &root, {}}));
    check_written(root, R"(28({"name": "r", "parent": null,)"
                        R"( "children": [{"name": "c", "parent": 29(0), "children": []}]}))");
}

// A std::shared_ptr held twice is one object: tag 28 at the first member, tag 29 at the second. A pointer to const
// names the same object as a pointer to the type without const.
TEST_CASE("structs pointers: a std::shared_ptr shared by two members")
{
    auto const shared = std::make_shared<leaf>(leaf{3});
    check_written(shared_pair{shared, shared}, R"({"a": 28({"v": 3}), "b": 29(0)})");
}

// Each std::unique_ptr owns its own object, so the value reaches every object of the tree once and the bytes hold
// no tag.
TEST_CASE("structs pointers: a tree of std::unique_ptr")
{
    tree root{1, {}};
    root.kids.push_back(std::make_unique<tree>(tree{2, {}}));
    root.kids.push_back(std::make_unique<tree>(tree{3, {}}));
    root.kids.back()->kids.push_back(std::make_unique<tree>(tree{4, {}}));
    check_written(root, R"({"v": 1, "kids": [{"v": 2, "kids": []},)"
                        R"( {"v": 3, "kids": [{"v": 4, "kids": []}]}]})");
}

// A pointer that writes an element first leaves the element to be written in place again later, as a second copy
// of the object. Boost.Serialization refuses this with pointer_conflict, and so does the encode. The other order,
// the element first and the pointer after it, writes tag 29.
TEST_CASE("structs pointers: an object in place after a pointer wrote it is pointer_conflict")
{
    pointer_first g{nullptr, {leaf{1}, leaf{2}}};
    g.first = &g.nodes[1];
    check_refused(g, error::pointer_conflict);
    value_first h{{leaf{1}, leaf{2}}, nullptr};
    h.focus_pointer = &h.items[1];
    check_written(h, R"({"items": [{"v": 1}, 28({"v": 2})], "focus_pointer": 29(0)})");
}

// A typed array is written as one byte string, so no tag can stand on one of its elements. A pointer to the whole
// array is a pointer to another type: in key order after the array it writes tag 29, before the array it writes
// the array, which then stands in place a second time.
TEST_CASE("structs pointers: a pointer to a typed array beside it, in both key orders")
{
    array_first f{{1, 2, 3, 4}, nullptr, nullptr};
    f.p = &f.a;
    check_written(f, R"({"a": 28()" + edn_of_array(f.a) + R"(), "i": null, "p": 29(0)})");
    array_after_pointer g{{1, 2, 3, 4}, nullptr, nullptr};
    g.p = &g.array;
    check_refused(g, error::pointer_conflict);
}

// A struct and the typed array at its start share one address. The identity of an object holds its type, so a
// pointer to the struct does not point into the array of int.
TEST_CASE("structs pointers: a pointer to a struct that starts with a typed array")
{
    holds_struct h{{{1, 2, 3, 4}, 5}, nullptr, nullptr};
    h.p = &h.o;
    check_written(h, R"({"i": null, "o": 28({"a": )" + edn_of_array(h.o.a) + R"(, "x": 5}), "p": 29(0)})");
}

// An element of a typed array or a char of a string has no item of its own, so a pointer into one cannot name it.
// A pointer to an int beside the vector names an item of its own and writes tags 28 and 29.
TEST_CASE("structs pointers: a pointer into a typed array or a string is pointer_conflict")
{
    vector_and_int into{{1, 2, 3}, 4, nullptr};
    into.p = &into.v[1];
    check_refused(into, error::pointer_conflict);
    vector_and_int beside{{1, 2, 3}, 4, nullptr};
    beside.p = &beside.a;
    auto const vector_text = cbor::diagnostic_notation(*cbor::encode<std::vector<int>>(beside.v));
    REQUIRE(vector_text.has_value());
    check_written(beside, R"({"a": 28(4), "p": 29(0), "v": )" + *vector_text + "}");
    text_and_char text{"abc", nullptr};
    text.p = &text.t[1];
    check_refused(text, error::pointer_conflict);
}

// A view over the objects of a type that a pointer can reach would reach each of them a second time in place, and a
// value made by cbor::describe_members is a temporary whose address names no object of the value. Such a type does
// not compile. A view of a type that no pointer can reach compiles.
TEST_CASE("structs pointers: a view or a described value of a reached type does not compile")
{
    CHECK_FALSE(encodes<view_of_reached_char>);
    CHECK_FALSE(structs_encodes<view_of_reached_char>);
    CHECK_FALSE(encodes<span_of_reached_type>);
    CHECK_FALSE(structs_encodes<span_of_reached_type>);
    CHECK(encodes<view_of_other_type>);
    CHECK(structs_encodes<view_of_other_type>);
    CHECK_FALSE(encodes<value_with_pointer>);
    CHECK_FALSE(encodes<reached_by_value>);
    CHECK(encodes<not_reached_by_value>);
    CHECK(encodes<value_of_int>);
}

// A type with no pointer in it has no tracker: its bytes hold no tag 28.
TEST_CASE("structs pointers: a type without pointers writes the bytes it wrote before")
{
    check_written(plain{1, {"x"}, std::nullopt}, R"({"a": 1, "b": ["x"]})");
    check_written(leaf{5}, R"({"v": 5})");
}

} // namespace structs_pointers
