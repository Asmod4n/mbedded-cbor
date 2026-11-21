#include <algorithm>
#include "pieces.hpp"
#include "owned_copy.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <cbor/diagnostic_notation.hpp>
#include "item_head.hpp"
#include "small_stack.hpp"
#include "unfolding.hpp"
#include "reference.hpp"
#include <cbor/structs.hpp>

#include <functional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace shared_test
{

// The decoded item tree of a top-level item, as lazy::to_item gives it.
using tree = cbor::owning<cbor::item>;

// An item of a decoded tree together with the tree. A tree decode reads a tag 29 as a plain tag. So the tests of the
// encoder find the value that a tag 29 names in the tree itself.
struct place {
    tree t;
    cbor::item i;

    cbor::item operator*() const
    {
        return i;
    }

    cbor::item const *operator->() const
    {
        return &i;
    }
};

// The tags 28 of a tree in stream order. RFC 8949 3.4 and the value-sharing registration number the marks in this
// order, and a tag 29 holds the index of one of them.
std::vector<cbor::item> marks_of(tree const &t)
{
    std::vector<cbor::item> marks;
    std::vector<cbor::item> pending{*t};
    while (!pending.empty()) {
        cbor::item const i = pending.back();
        pending.pop_back();
        if (auto const tag = i.get<std::pair<std::uint64_t, cbor::item>>(); tag && i.type() == cbor::type::tag) {
            if (tag->first == 28)
                marks.push_back(i);
            pending.push_back(tag->second);
        }
        if (i.type() == cbor::type::array || i.type() == cbor::type::map) {
            auto const c = list_children_of_item(i);
            for (auto it = c.rbegin(); it != c.rend(); ++it)
                pending.push_back(*it);
        }
    }
    return marks;
}

// This is the item that a mark of tag 28 or a reference of tag 29 stands for. It is read from the tree by the index of
// the mark.
place target(place n)
{
    constexpr int most_steps = 1000;
    for (int steps = 0; steps < most_steps; ++steps) {
        auto const t = n.i.get<std::pair<std::uint64_t, cbor::item>>();
        if (t && t->first == 29) {
            auto const marks = marks_of(n.t);
            auto const index = t->second.get<std::uint64_t>();
            REQUIRE(index.has_value());
            REQUIRE(*index < marks.size());
            n.i = marks[static_cast<std::size_t>(*index)];
        } else if (t && t->first == 28)
            n.i = t->second;
        else
            return n;
    }
    FAIL("a chain of marks without an end");
    return n;
}

place target(tree const &t)
{
    return target(place{t, *t});
}

tree decoded_ref(std::string_view const wire)
{
    auto const l = test::make_lazy_from_copy(wire);
    REQUIRE(l.has_value());
    auto t = l->to_item();
    REQUIRE(t.has_value());
    return *t;
}

template <class P>
std::vector<place> children(P const &container)
{
    place const c = target(container);
    std::vector<place> out;
    for (cbor::item const child : list_children_of_item(*c))
        out.push_back(place{c.t, child});
    return out;
}

template <class P>
place element(P const &array, std::size_t const i)
{
    return target(children(array)[i]);
}

template <class P>
place map_value(P const &map, std::size_t const i)
{
    return target(children(map)[2 * i + 1]);
}

template <class P>
place at(P const &map, std::string_view const key)
{
    auto const c = children(map);
    for (std::size_t i = 0; i + 1 < c.size(); i += 2) {
        place const k = target(c[i]);
        if (auto const s = k->get<std::string_view>(); s && *s == key)
            return target(c[i + 1]);
    }
    FAIL("no such key");
    return c.front();
}

// The content of a tag that is no mark, as the item of an application object holds it.
template <class P>
place content(P const &tagged)
{
    place const t = target(tagged);
    return target(place{t.t, t->get<std::pair<std::uint64_t, cbor::item>>().value().second});
}

// A resolved tag 29 gives a tree of its own, so two places are compared by the encoded form of their targets.
std::string encoded_of(place const &p)
{
    std::string out;
    REQUIRE(cbor::encode(out, p.i).has_value());
    return out;
}

// A tag 29 stands where a value is not written again. A resolved tag 29 has no identity in this test yet. So a test that
// two values are distinct checks that the second one is written in full.
inline bool is_reference(place const &p)
{
    auto const t = p->get<std::pair<std::uint64_t, cbor::item>>();
    return t && t->first == 29;
}

template <class A, class B>
bool targets_encode_to_the_same_bytes(A const &a, B const &b)
{
    return encoded_of(target(a)) == encoded_of(target(b));
}

} // namespace shared_test

using namespace shared_test;

// The owner decided that tags 28 and 29 exist only to write cycles. A value that two places hold without a cycle is
// written in full at each place. It has no tag 28. It decodes to two equal values.
TEST_CASE("tag 28/29: a repeated value without a cycle is written twice")
{
    auto const r = decoded_ref(
        "\x82\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x1b\x00\x00\x00\x00\x00\x00\x00\x02"
        "\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x1b\x00\x00\x00\x00\x00\x00\x00\x02"sv);
    CHECK_FALSE(is_reference(children(r)[1]));
    CHECK_EQ(read_argument_of_item(*element(element(r, 1), 1)), 2u);
}

// Ported from test.rb: 'tag 28/29: repeated value → identity preserved (eager)'. The input comes from a writer that
// marks every repeated value; the decoder keeps its identity.
TEST_CASE("tag 28/29: a repeated value that another writer marked keeps its identity")
{
    auto const r = decoded_ref(
        "\x82\xd8\x1c\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x1b\x00\x00\x00\x00\x00\x00\x00\x02"
        "\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(element(r, 0), element(r, 1)));
}

// Ported from test.rb: 'tag 28/29: three-way sharing preserves identity (eager)'.
TEST_CASE("tag 28/29: three-way sharing")
{
    auto const r = decoded_ref(
        "\xa3\x61" "a\xd8\x1c\x83\x1b\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x02\x1b\x00\x00\x00\x00\x00\x00\x00\x03"
        "\x61" "b\xd8\x1d\x18\x00\x61" "c\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(at(r, "a"), at(r, "b")));
    CHECK(targets_encode_to_the_same_bytes(at(r, "b"), at(r, "c")));
}

// Ported from test.rb: 'tag 28/29: shared hash / shared string roundtrip'.
TEST_CASE("tag 28/29: shared map and shared string")
{
    auto const rh = decoded_ref(
        "\x83\xd8\x1c\xa2\x61k\x61v\x61n\x1b\x00\x00\x00\x00\x00\x00\x00\x2a"
        "\xd8\x1d\x18\x00\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(element(rh, 0), element(rh, 1)));
    CHECK(targets_encode_to_the_same_bytes(element(rh, 1), element(rh, 2)));
    auto const rs = decoded_ref("\x83\xd8\x1c\x6dshared_string\xd8\x1d\x18\x00\xa1\x63key\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(element(rs, 0), element(rs, 1)));
    CHECK(targets_encode_to_the_same_bytes(element(rs, 1), at(element(rs, 2), "key")));
}

// Ported from test.rb: 'tag 28/29: cyclic array and cyclic hash (eager)'.
TEST_CASE("tag 28/29: a cyclic array and a cyclic map")
{
    auto const ra = decoded_ref("\xd8\x1c\x81\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(ra, element(ra, 0)));
    auto const rh = decoded_ref("\xd8\x1c\xa1\x64self\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(rh, at(rh, "self")));
}

// A cycle can come from the wire. encode of the tree writes the reference as it stands. It does not follow it, so a
// cycle ends.
TEST_CASE("tag 28/29: encode of a tree with a cycle ends")
{
    tree const a = decoded_ref("\xd8\x1c\x81\xd8\x1d\x00"sv);
    std::string out;
    REQUIRE(cbor::encode(out, *a).has_value());
    CHECK_EQ(out, "\xd8\x1c\x81\xd8\x1d\x00"sv);
}

// Ported from test.rb: 'tag 28/29: mutual recursion — hash↔array cycle'.
TEST_CASE("tag 28/29: mutual recursion between a map and an array")
{
    auto const r = decoded_ref("\xd8\x1c\xa1\x64list\x82\xd8\x1d\x18\x00\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(r, element(at(r, "list"), 0)));
    CHECK(targets_encode_to_the_same_bytes(r, element(at(r, "list"), 1)));
}

// Ported from test.rb: 'tag 28/29: cyclic map round-trip — full cycle navigable'.
TEST_CASE("tag 28/29: a full cycle through two maps")
{
    auto const r = decoded_ref(
        "\xd8\x1c\xa1\x65" "child\xa2\x64name\x65" "cycle\x66parent"
        "\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(r, at(at(r, "child"), "parent")));
    CHECK(targets_encode_to_the_same_bytes(at(r, "child"), at(at(at(r, "child"), "parent"), "child")));
}

// Ported from test.rb: 'tag 28/29: shared leaf reached through deep paths'.
TEST_CASE("tag 28/29: a shared leaf through a deep path")
{
    auto const r = decoded_ref(
        "\xa2\x61" "a\xa1\x61" "b\xa1\x61" "c\xa1\x61" "d\xa1\x61" "e"
        "\xd8\x1c\x83\x1b\x00\x00\x00\x00\x00\x00\x00\x0a\x1b\x00\x00\x00\x00\x00\x00\x00\x14"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x1e\x61x\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(at(at(at(at(at(r, "a"), "b"), "c"), "d"), "e"), at(r, "x")));
}

// Ported from test.rb: 'tag 28/29: diamond pattern preserves identity across all paths'.
TEST_CASE("tag 28/29: a diamond")
{
    auto const r = decoded_ref(
        "\xa3\x64left\xa1\x65" "child\xd8\x1c\xa1\x65value\x1b\x00\x00\x00\x00\x00\x00\x00\x63"
        "\x65right\xa1\x65" "child\xd8\x1d\x18\x00\x66" "direct\xd8\x1d"
        "\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(at(at(r, "left"), "child"), at(at(r, "right"), "child")));
    CHECK(targets_encode_to_the_same_bytes(at(at(r, "left"), "child"), at(r, "direct")));
}

// Ported from test.rb: 'tag 28/29: nested sharing — outer and inner both share'.
TEST_CASE("tag 28/29: outer and inner both share")
{
    auto const r = decoded_ref(
        "\x82\xd8\x1c\x82\xd8\x1c\x83\x1b\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x02\x1b\x00\x00\x00\x00\x00\x00\x00\x03"
        "\xd8\x1d\x18\x01\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(element(r, 0), element(r, 1)));
    CHECK(targets_encode_to_the_same_bytes(element(element(r, 0), 0), element(element(r, 0), 1)));
    CHECK(targets_encode_to_the_same_bytes(element(element(r, 0), 0), element(element(r, 1), 0)));
}

// Ported from test.rb: 'tag 28/29: distinct shared groups do not conflate'.
TEST_CASE("tag 28/29: distinct groups do not conflate")
{
    auto const r = decoded_ref(
        "\xa7\x62" "a1\xd8\x1c\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x02\x62" "a2\xd8\x1d\x18\x00"
        "\x62" "b1\xd8\x1c\xa1\x61k\x61v\x62" "b2\xd8\x1d\x18\x01"
        "\x62" "c1\xd8\x1c\x63str\x62" "c2\xd8\x1d\x18\x02\x66nested"
        "\xa3\x61" "a\xd8\x1d\x18\x00\x61" "b\xd8\x1d\x18\x01\x61" "c"
        "\xd8\x1d\x18\x02"sv);
    CHECK(targets_encode_to_the_same_bytes(at(r, "a1"), at(at(r, "nested"), "a")));
    CHECK(targets_encode_to_the_same_bytes(at(r, "b1"), at(at(r, "nested"), "b")));
    CHECK(targets_encode_to_the_same_bytes(at(r, "c1"), at(at(r, "nested"), "c")));
    CHECK_FALSE(targets_encode_to_the_same_bytes(at(r, "a1"), at(r, "b1")));
    CHECK_FALSE(targets_encode_to_the_same_bytes(at(r, "b1"), at(r, "c1")));
}

// Replaces test.rb: 'tag 28/29: same object as value AND key — only values share'. mruby keeps an Array key as the same
// object. It was measured with mruby 14fc2ef. So an array key with identity shares.
TEST_CASE("tag 28/29: an array key with identity shares with the values")
{
    auto const r = decoded_ref(
        "\xa3\x62v1\xd8\x1c\x83\x1b\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x02\x1b\x00\x00\x00\x00\x00\x00\x00\x03"
        "\x62v2\xd8\x1d\x18\x00\x6a" "as_key_map\xa1\xd8\x1d\x18\x00"
        "\x67payload"sv);
    CHECK(targets_encode_to_the_same_bytes(at(r, "v1"), at(r, "v2")));
    auto const key = target(children(at(r, "as_key_map"))[0]);
    CHECK(targets_encode_to_the_same_bytes(key, at(r, "v1")));
}

// Ported from test.rb: 'no sharedref flag: values do not share, cycles hit depth limit'. An application that
// writes no marks writes a shared value each time, and the decoded values are distinct.
TEST_CASE("without marks: values are written each time")
{
    auto const r = decoded_ref(
        "\xa2\x61" "a\x83\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x1b\x00\x00\x00\x00\x00\x00\x00\x02"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x03\x61" "b\x83\x1b\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x02\x1b\x00\x00\x00\x00\x00\x00\x00\x03"sv);
    CHECK_FALSE(is_reference(children(r)[3]));
}

// Ported from test.rb: 'registered tag + sharedref: instance with self-referential field'. The tag item of the
// object exists before its content, so a reference inside the content names the object.
TEST_CASE("application tag: a reference inside the content names the object")
{
    auto const r = decoded_ref("\xd8\x1c\xd9\x13\x88\x81\xd8\x1d\x00"sv);
    CHECK_EQ(read_argument_of_item(*target(r)), 5000u);
    CHECK(targets_encode_to_the_same_bytes(element(content(r), 0), r));
}

// A reference after the object names the tag item of the object, the same item as the place of the mark.
TEST_CASE("application tag: a reference after the object names the object")
{
    auto const r = decoded_ref("\x82\xd8\x1c\xd9\x13\x88\x81\x01\xd8\x1d\x00"sv);
    CHECK_EQ(read_argument_of_item(*element(r, 0)), 5000u);
    CHECK(targets_encode_to_the_same_bytes(element(r, 0), element(r, 1)));
}

// Ported from test.rb: 'registered tag + sharedref: mutual recursion between two instances'.
TEST_CASE("application tag: two objects that name each other")
{
    // 28 5000([28 5000([29 0])])
    auto const r = decoded_ref("\xd8\x1c\xd9\x13\x88\x81\xd8\x1c\xd9\x13\x88\x81\xd8\x1d\x00"sv);
    place const peer = element(content(r), 0);
    CHECK_EQ(read_argument_of_item(*peer), 5000u);
    CHECK(targets_encode_to_the_same_bytes(element(content(peer), 0), r));
}

// A tag that no application knows is a tag item over its content.
TEST_CASE("application tag: a tag without meaning is a tag item")
{
    auto const r = decoded_ref("\xc1\x01"sv);
    CHECK_EQ(read_argument_of_item(*target(r)), 1u);
    CHECK_EQ(read_argument_of_item(*content(r)), 1u);
}

// Ported from test.rb: 'tag 28/29: shared value preserved via lazy.value'.
TEST_CASE("lazy: a shared value keeps its identity")
{
    std::string const doc(
        "\x82\xd8\x1c\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01\x1b\x00\x00\x00\x00\x00\x00\x00\x02"
        "\xd8\x1d\x18\x00"sv);
    auto const r = test::make_lazy_from_copy(doc)->to_item();
    REQUIRE(r.has_value());
    CHECK(targets_encode_to_the_same_bytes(element(*r, 0), element(*r, 1)));
}

// Ported from test.rb: 'tag 28/29: cyclic array materializes via lazy'.
TEST_CASE("lazy: a cyclic array")
{
    std::string const doc = "\xd8\x1c\x81\xd8\x1d\x00"s;
    auto const r = test::make_lazy_from_copy(doc)->to_item();
    REQUIRE(r.has_value());
    CHECK(targets_encode_to_the_same_bytes(*r, element(*r, 0)));
}

// Ported from test.rb: 'tag 28/29: shared value preserved via lazy.value'. The map half: the first
// test above covers only the array half.
TEST_CASE("lazy: a value shared by two map values keeps its identity")
{
    std::string const doc(
        "\xa2\x61" "a\xd8\x1c\x83\x1b\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x02\x1b\x00\x00\x00\x00\x00\x00\x00\x03"
        "\x61" "b\xd8\x1d\x18\x00"sv);
    auto const r = test::make_lazy_from_copy(doc)->to_item();
    REQUIRE(r.has_value());
    CHECK(targets_encode_to_the_same_bytes(at(*r, "a"), at(*r, "b")));
}

// Ported from test.rb: 'tag 28: self-referential Tag 28 inside Tag 28 is safe'. The exact bytes of
// mruby-cbor: two marks on one value and no reference, which must decode without a fault.
TEST_CASE("tag 28: inside tag 28 without a reference")
{
    auto const r = decoded_ref("\xd8\x1c\xd8\x1c\x18\x2a"sv);
    CHECK_EQ(read_argument_of_item(*target(r)), 42u);
}

// Ported from test.rb: 'registered tag + sharedref: same instance in array → identity preserved'. The decoder gives
// one object three times.
TEST_CASE("application tag: the same object three times keeps its identity")
{
    auto const r = decoded_ref(
        "\x83\xd8\x1c\xd9\x13\x88\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x03"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x07\xd8\x1d\x18\x00\xd8\x1d"
        "\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(element(r, 0), element(r, 1)));
    CHECK(targets_encode_to_the_same_bytes(element(r, 1), element(r, 2)));
}

// Ported from test.rb: 'registered tag + sharedref: distinct instances with equal fields do NOT share'. The decoder
// gives two objects.
TEST_CASE("application tag: distinct objects with equal content decode to two objects")
{
    auto const r = decoded_ref(
        "\x82\xd9\x13\x88\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x02\xd9\x13\x88\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x01"
        "\x1b\x00\x00\x00\x00\x00\x00\x00\x02"sv);
    CHECK_FALSE(is_reference(children(r)[1]));
}

// Ported from test.rb: 'registered tag + sharedref: non-immediate ivar (String) shares correctly'. The
// string inside the content gets its own mark, so the mark of the object must be placed before it.
TEST_CASE("application tag: an object whose content holds a string keeps its identity")
{
    auto const r = decoded_ref(
        "\xa3\x61p\xd8\x1c\xd9\x13\x89\x82\x1b\x00\x00\x00\x00\x00\x00\x00\x1e"
        "\x67" "default\x61" "b\xd8\x1d\x18\x00\x61" "f\xd8\x1d\x18\x00"sv);
    CHECK(targets_encode_to_the_same_bytes(at(r, "p"), at(r, "b")));
    CHECK(targets_encode_to_the_same_bytes(at(r, "b"), at(r, "f")));
}

// Ported from test.rb: 'registered tag + sharedref: identity preserved through lazy.value'.
TEST_CASE("lazy: a registered object keeps its identity")
{
    std::string const doc(
        "\xa2\x61" "a\xd8\x1c\xd9\x13\x8b\x81\x1b\x00\x00\x00\x00\x00\x00\x00\x63"
        "\x61" "b\xd8\x1d\x18\x00"sv);
    auto const r = test::make_lazy_from_copy(doc)->to_item();
    REQUIRE(r.has_value());
    CHECK(targets_encode_to_the_same_bytes(at(*r, "a"), at(*r, "b")));
}

namespace shared_test
{

// The head of a tag or an unsigned integer with its argument in 1, 2, 4 and 8 bytes (RFC 8949 3).
std::string head_of(std::uint8_t const major, std::uint64_t const argument, std::size_t const bytes)
{
    std::string h(1, static_cast<char>(major << 5 | (bytes == 1 ? 24 : bytes == 2 ? 25 : bytes == 4 ? 26 : 27)));
    for (std::size_t i = bytes; i-- > 0;)
        h += static_cast<char>(argument >> (8 * i));
    return h;
}

} // namespace shared_test

// The owner decided on 2026-10-13 that every reader but an eager struct decode reads tags 28 and 29 as plain tags. Tag
// 28 stays over its content. Tag 29 stays over its content. There is no index check and no error of its own. RFC 8949 3
// lets the argument of a head take 1, 2, 4 or 8 bytes, and each form is read the same.
TEST_CASE("decode_cycles: lazy, at_path, decode and diagnostic notation read tags 28 and 29 as tags")
{
    for (std::size_t const bytes : {1uz, 2uz, 4uz, 8uz}) {
        CAPTURE(bytes);
        std::string const doc = "\x83"s + head_of(6, 28, bytes) + "\x05"s + head_of(6, 29, bytes) +
                                head_of(0, 0, bytes) + head_of(6, 29, bytes) + head_of(0, 7, bytes);
        auto const l = *test::make_lazy_from_copy(doc);
        CHECK_EQ(l.at(0)->type(), cbor::type::tag);
        CHECK_EQ(l.at(1)->type(), cbor::type::tag);
        CHECK_EQ(l.at(0)->get<std::uint64_t>(), 5u);
        CHECK_EQ(l.at(1)->get<std::uint64_t>(), 0u);
        CHECK_EQ(l.at(2)->get<std::uint64_t>(), 7u);
        CHECK_EQ(l.at_path<"$[1]", std::uint64_t>(), 0u);
        CHECK_EQ(cbor::at_path<"$[2]", std::uint64_t>(doc), 7u);
        std::vector<cbor::type> types;
        auto const range = *l.elements();
        for (auto const e : range)
            types.push_back(*e->type());
        CHECK(types == std::vector<cbor::type>(3, cbor::type::tag));
        for (auto const &tree : {l.to_item(), cbor::decode(doc)}) {
            REQUIRE(tree.has_value());
            auto const elements = list_children_of_item(**tree);
            REQUIRE_EQ(elements.size(), 3u);
            for (auto const &[element, number, index] : {std::tuple{elements[0], 28u, 5u},
                                                        std::tuple{elements[1], 29u, 0u},
                                                        std::tuple{elements[2], 29u, 7u}}) {
                std::pair<std::uint64_t, cbor::item> const t =
                    element.get<std::pair<std::uint64_t, cbor::item>>().value();
                CHECK_EQ(t.first, number);
                CHECK_EQ(t.second.get<std::uint64_t>(), index);
            }
        }
        auto const text = cbor::diagnostic_notation(doc);
        REQUIRE(text.has_value());
        CHECK_EQ(text->contains("28_"), bytes > 1);
        CHECK_EQ(text->contains("29_"), bytes > 1);
    }
    CHECK_EQ(cbor::diagnostic_notation("\x83\xd8\x1c\x05\xd8\x1d\x00\xd8\x1d\x07"sv), "[28(5), 29(0), 29(7)]");
}

// The owner decided on 2026-11-16 that a struct decode reads no shared value: a tag 28 or a tag 29 where it
// reads a value is invalid_tag, for each width of the head and of the index. A vector of std::optional is a
// plain array; a vector of numbers is a typed array, which holds no tag.
TEST_CASE("decode_cycles: a struct decode refuses every form of a tag 28 and a tag 29")
{
    using marked_numbers = std::vector<std::optional<std::uint64_t>>;
    for (std::string const width : {"_0", "_1", "_2", "_3"}) {
        CAPTURE(width);
        std::string const marked = cbor::diag2cbor("[28" + width + "(5)]").value();
        std::string const named =
            cbor::diag2cbor("[28" + width + "(5), 29" + width + "(0" + width + ")]").value();
        CHECK_EQ(cbor::structs<marked_numbers>::decode(std::string(marked)).error(), error::invalid_tag);
        CHECK_EQ(cbor::structs<marked_numbers>::decode(std::string(named)).error(), error::invalid_tag);
    }
}

namespace shared_test
{

// The node of the target of a shared_reference node.
cbor::item target_of(cbor::item const reference)
{
    auto const r = reference.get<std::pair<std::uint64_t, cbor::item>>();
    REQUIRE(r.has_value());
    return r->second;
}

std::vector<cbor::item> elements_of(cbor::item const array)
{
    auto const e = array.elements();
    REQUIRE(e.has_value());
    return {e->begin(), e->end()};
}

} // namespace shared_test

// These are the cases of the model (theories/ValueSharing.v, the Examples). cbor::decode_cycles reads them and
// resolves the tags (owner decision). A tag 28 makes no node and marks the node of its content. A tag 29 is a shared_reference node
// whose target is that node. The specification (refs/value-sharing.html) gives the first two cases; the others are the
// traps that the model names. The model gives the expected node identities and errors.
TEST_CASE("decode_cycles: cbor::decode_cycles reads each case of the model")
{
    using shared_test::elements_of;
    using shared_test::target_of;
    // [28([]), 29(0), []]: the reference names an array whose decode has ended, so it closes no cycle (owner decision).
    CHECK_EQ(cbor::decode_cycles("\x83\xd8\x1c\x80\xd8\x1d\x00\x80"sv).error(), error::sharedref_not_cyclic);
    CHECK_EQ(cbor::decode_cycles("\x82\xd8\x1c\x01\xd8\x1d\x00"sv).error(), error::sharedref_not_cyclic);
    // 28([29(0)]): an array that holds itself.
    auto const cyclic = cbor::decode_cycles("\xd8\x1c\x81\xd8\x1d\x00"sv);
    REQUIRE(cyclic.has_value());
    CHECK(target_of(elements_of(**cyclic)[0]) == **cyclic);
    // 28(1234([29(0)])): a cycle through a tag that is not 28.
    auto const through_tag = cbor::decode_cycles("\xd8\x1c\xd9\x04\xd2\x81\xd8\x1d\x00"sv);
    REQUIRE(through_tag.has_value());
    auto const content = (*through_tag)->get<std::pair<std::uint64_t, cbor::item>>().value().second;
    CHECK(target_of(elements_of(content)[0]) == **through_tag);
    // A mark whose content is only marks and a reference to one of them names no value.
    for (std::string_view const own : {"\xd8\x1c\xd8\x1d\x00"sv, "\xd8\x1c\xd8\x1c\xd8\x1d\x00"sv,
                                       "\xd8\x1c\xd8\x1c\xd8\x1d\x01"sv}) {
        CAPTURE(own.size());
        CHECK_EQ(cbor::decode_cycles(own).error(), error::sharedref_not_defined);
    }
    // 28([28(29(0)), 29(1)]): a mark of a reference marks the value that the reference names. That value is the open
    // root, so the second reference closes a cycle too, though the mark that it names was set after the root.
    auto const mark_of_reference = cbor::decode_cycles("\xd8\x1c\x82\xd8\x1c\xd8\x1d\x00\xd8\x1d\x01"sv);
    REQUIRE(mark_of_reference.has_value());
    auto const m = elements_of(**mark_of_reference);
    CHECK(target_of(m[0]) == **mark_of_reference);
    CHECK(target_of(m[1]) == **mark_of_reference);
    // [28(1), 28(29(0)), 29(1)]: the mark of the reference names the integer, whose decode has ended.
    CHECK_EQ(cbor::decode_cycles("\x83\xd8\x1c\x01\xd8\x1c\xd8\x1d\x00\xd8\x1d\x01"sv).error(),
             error::sharedref_not_cyclic);
    // 28(28([29(0), 29(1)])): two marks of one value.
    auto const two_marks = cbor::decode_cycles("\xd8\x1c\xd8\x1c\x82\xd8\x1d\x00\xd8\x1d\x01"sv);
    REQUIRE(two_marks.has_value());
    auto const t = elements_of(**two_marks);
    CHECK(target_of(t[0]) == **two_marks);
    CHECK(target_of(t[1]) == **two_marks);
    // A reference before any mark, and a reference to a mark that comes later.
    CHECK_EQ(cbor::decode_cycles("\x81\xd8\x1d\x00"sv).error(), error::sharedref_index_not_marked);
    CHECK_EQ(cbor::decode_cycles("\x82\xd8\x1d\x00\xd8\x1c\x01"sv).error(), error::sharedref_index_not_marked);
    // The registration: the content of tag 29 is an unsigned integer. A text and a bignum are not.
    CHECK_EQ(cbor::decode_cycles("\xd8\x1d\x61x"sv).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(cbor::decode_cycles("\x82\xd8\x1c\x01\xd8\x1d\xc2\x41\x00"sv).error(), error::inadmissible_type_for_tag_content);
    // {28("a"): 29(0)}: a value refers to its own key, whose decode has ended.
    CHECK_EQ(cbor::decode_cycles("\xa1\xd8\x1c\x61\x61\xd8\x1d\x00"sv).error(), error::sharedref_not_cyclic);
    // 28({29(0): 1}): a map that holds itself as its key.
    auto const own_key = cbor::decode_cycles("\xd8\x1c\xa1\xd8\x1d\x00\x01"sv);
    REQUIRE(own_key.has_value());
    CHECK(target_of(std::get<0>((*own_key)->pairs().value().front())) == **own_key);
}

// A tag 29 may name any marked value whose read has not ended: the value itself or one above it. A tag 28 over a tag
// 29 marks the value that the reference names, so its mark stays open as long as that value is open, also after the
// item around the tag 28 ends. Each case comes with the open values at its tag 29.
TEST_CASE("decode_cycles: cbor::decode_cycles takes a tag 29 to each open value and to no other")
{
    for (std::string_view const edn :
         {"28([28([28(29(0))]), 29(2)])", "28([28([28(29(0)), 29(2)]), 29(2)])", "28([28(29(0)), 28([29(2), 29(1)])])",
          "28(28([29(1), 29(0)]))", "[28([29(0)]), 28([29(1)])]", "28([28(29(0)), [29(1)]])",
          "28({\"kids\": [28({\"parent\": 29(0), \"self\": 29(1)})]})"}) {
        CAPTURE(edn);
        CHECK(cbor::decode_cycles(cbor::diag2cbor(edn).value()).has_value());
    }
    // 29(2) names the mark of 28(29(1)), whose value is the inner array; that array has ended. 29(1) names the inner
    // array after its end. 29(0) names the first array after its end.
    for (std::string_view const edn : {"28([28([28(29(1))]), 29(2)])", "28([28([1]), 29(1)])", "[28([29(0)]), 29(0)]"}) {
        CAPTURE(edn);
        CHECK_EQ(cbor::decode_cycles(cbor::diag2cbor(edn).value()).error(), error::sharedref_not_cyclic);
    }
}

// RFC 8949 3: an argument can stand in each width. The tag 29 and its index are read in each width. The largest index
// has no mark, on every build (the index is compared as a 64-bit number).
TEST_CASE("decode_cycles: cbor::decode_cycles reads a tag 29 and its index in each width")
{
    for (std::size_t const bytes : {1uz, 2uz, 4uz, 8uz}) {
        CAPTURE(bytes);
        std::string const wire = "\xd8\x1c\x81"s + head_of(6, 29, bytes) + head_of(0, 0, bytes);
        auto const tree = cbor::decode_cycles(wire);
        REQUIRE(tree.has_value());
        CHECK(shared_test::target_of(shared_test::elements_of(**tree)[0]) == **tree);
    }
    CHECK_EQ(cbor::decode_cycles("\xd8\x1c\x81\xd8\x1d\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv).error(),
             error::sharedref_index_not_marked);
}

// A long chain of tags 28 is read in a loop: 50000 tags 28 before 1 give the integer and no deep stack. The content of a
// tag 24 is decoded with a table of its own. RFC 8949 3.4.5.1 says that an embedded item is a data item of its own. So a
// tag 29 in it does not see the marks of the enclosing item.
TEST_CASE("decode_cycles: cbor::decode_cycles reads a long chain of marks and a tag 24 with its own table")
{
    constexpr std::size_t chain_length = 50000;
    std::string chain;
    for (std::size_t i = 0; i < chain_length; ++i)
        chain += "\xd8\x1c"s;
    chain += '\x01';
    auto const one = cbor::decode_cycles(chain);
    REQUIRE(one.has_value());
    CHECK_EQ((*one)->get<std::uint64_t>().value(), 1u);
    CHECK_EQ(cbor::decode_cycles("\x82\xd8\x1c\x01\xd8\x18\x43\xd8\x1d\x00"sv).error(), error::sharedref_index_not_marked);
}

// lazy::to_item, paths, cbor::decode and diagnostic_notation pass tags 28 and 29 as plain tags (owner decision); only
// cbor::decode_cycles resolves them.
TEST_CASE("decode_cycles: lazy::to_item keeps tags 28 and 29")
{
    auto const top_level = test::make_lazy_from_copy("\xd8\x1c\x81\xd8\x1d\x00"s);
    REQUIRE(top_level.has_value());
    auto const tree = top_level->to_item();
    REQUIRE(tree.has_value());
    CHECK_EQ((*tree)->get<std::pair<std::uint64_t, cbor::item>>().value().first, 28u);
}

// The marks of the model reach every kind of node: a bignum that decode reads as an integer, a tag 24 and a typed array.
// None of them can hold a reference to itself, so a reference to one of them closes no cycle; an unmarked node would
// give sharedref_index_not_marked instead. The tags of an embedded item are resolved with its own table (RFC 8949
// 3.4.5.1). The model gives
// sharedref_index_not_marked for an index past the marks before the chain. An item that ends in its tag 28 or tag 29 is
// too little data.
TEST_CASE("decode_cycles: cbor::decode marks every kind of node and resolves inside a tag 24")
{
    using shared_test::elements_of;
    using shared_test::target_of;
    for (std::string_view const wire : {"\x82\xd8\x1c\xc2\x41\x01\xd8\x1d\x00"sv, "\x82\xd8\x1c\xd8\x18\x41\x01\xd8\x1d\x00"sv,
                                        "\x82\xd8\x1c\xd8\x40\x42\x01\x02\xd8\x1d\x00"sv}) {
        CAPTURE(wire.size());
        CHECK_EQ(cbor::decode_cycles(wire).error(), error::sharedref_not_cyclic);
    }
    auto const outer = cbor::decode_cycles("\xd8\x18\x46\xd8\x1c\x81\xd8\x1d\x00"sv);
    REQUIRE(outer.has_value());
    auto const embedded = cbor::encoded_cbor_data_item(*outer, **outer);
    REQUIRE(embedded.has_value());
    CHECK(target_of(elements_of(*embedded)[0]) == *embedded);
    CHECK_EQ(cbor::decode_cycles("\xd8\x1c\xd8\x1c\xd8\x1d\x02"sv).error(), error::sharedref_index_not_marked);
    CHECK_EQ(cbor::decode_cycles("\xd8\x1d"sv).error(), error::too_little_data);
    CHECK_EQ(cbor::decode_cycles("\xd8\x1c"sv).error(), error::too_little_data);
}

// clone of a lazy inside the top-level item keeps tags 28 and 29 and a tag 55799 as tag nodes. Only cbor::decode
// resolves tags 28 and 29. A tag 55799 inside an item stays a tag (owner decisions).
TEST_CASE("decode_cycles: clone at an inner offset keeps tags 28, 29 and 55799")
{
    auto const top_level = test::make_lazy_from_copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s);
    REQUIRE(top_level.has_value());
    auto const reference = top_level->at(1).and_then([](cbor::lazy const &l) { return l.to_item(); });
    REQUIRE(reference.has_value());
    CHECK_EQ((*reference)->get<std::pair<std::uint64_t, cbor::item>>().value().first, 29u);
    auto const marked = test::make_lazy_from_copy("\x81\xd9\xd9\xf7\x01"s);
    REQUIRE(marked.has_value());
    auto const inner = marked->at(0).and_then([](cbor::lazy const &l) { return l.to_item(); });
    REQUIRE(inner.has_value());
    CHECK_EQ((*inner)->get<std::pair<std::uint64_t, cbor::item>>().value().first, 55799u);
}

namespace shared_test
{

// The bytes that cbor::encode gives for the tree that cbor::decode gives for the input.
std::string encoded_again(std::string_view const input)
{
    auto const decoded = cbor::decode_cycles(input);
    REQUIRE(decoded.has_value());
    std::string out;
    REQUIRE(cbor::encode(out, **decoded).has_value());
    return out;
}

// The shortest head of an unsigned integer (RFC 8949 4.2.1).
std::string shortest_head_of(std::uint8_t const major, std::uint64_t const argument)
{
    if (argument < 24)
        return std::string(1, static_cast<char>(std::uint64_t{major} << 5 | argument));
    return head_of(major, argument, argument <= 0xff ? 1uz : argument <= 0xffff ? 2uz : argument <= 0xffffffff ? 4uz : 8uz);
}

} // namespace shared_test

namespace shared_test
{

// The bytes of cbor::encode for the node. encode_iov gives the same bytes, so both choose the walk from the same node.
std::string written_by_encode(cbor::item const node)
{
    std::string out;
    REQUIRE(cbor::encode(out, node).has_value());
#if __has_include(<sys/uio.h>)
    auto const pieces = cbor::encode_iov(node);
    REQUIRE(pieces.has_value());
    CHECK_EQ(test::join_pieces_into_bytes(**pieces).value(), out);
#endif
    return out;
}

// The bytes of cbor::encode for the tree that cbor::decode gives for the EDN text.
std::string written_by_encode(std::string_view const edn)
{
    auto const decoded = cbor::decode_cycles(cbor::diag2cbor(edn).value());
    REQUIRE(decoded.has_value());
    return written_by_encode(**decoded);
}

// The elements of the array at the root of the tree.
std::vector<cbor::item> elements_of(tree const &array)
{
    auto const elements = array->elements();
    REQUIRE(elements.has_value());
    return {elements->begin(), elements->end()};
}

} // namespace shared_test

// The owner decided that encode writes the cycles of a tree. encode sees a shared_reference below a node only through a
// mark that the decoder sets on each node above it. A reference that stands only as a map key, or only as the content
// of a tag, must reach that mark too. Else encode writes the tree in one walk and meets the reference.
TEST_CASE("decode_cycles: cbor::encode writes a reference under a map key or a tag")
{
    using shared_test::written_by_encode;
    for (std::string_view const edn : {"28([{29(0): 1}])", "28([1234(29(0))])", "28([2([29(0)])])", "28([3([29(0)])])"}) {
        CAPTURE(edn);
        CHECK_EQ(written_by_encode(edn), cbor::diag2cbor(edn).value());
    }
}

// encode takes any node of a tree, not only its root. A node that is itself a shared_reference stands for its target.
// A root that a tag 28 marks keeps its mark only when a reference names it in a cycle.
TEST_CASE("decode_cycles: cbor::encode of a shared_reference node and of a marked root")
{
    using shared_test::written_by_encode;
    auto const tree = cbor::decode_cycles(cbor::diag2cbor("28([1, 29(0)])").value());
    REQUIRE(tree.has_value());
    auto const elements = shared_test::elements_of(*tree);
    CHECK_EQ(elements[1].type(), cbor::type::shared_reference);
    CHECK_EQ(written_by_encode(elements[1]), cbor::diag2cbor("28([1, 29(0)])").value());
    CHECK_EQ(written_by_encode(**tree), cbor::diag2cbor("28([1, 29(0)])").value());
    CHECK_EQ(written_by_encode("28([29(0)])"), cbor::diag2cbor("28([29(0)])").value());
    CHECK_EQ(written_by_encode("28([1])"), cbor::diag2cbor("[1]").value());
}

// The content of a tag 24 is decoded with marks of its own. encode writes the tag 24 as its byte string. The root of
// the content, from cbor::encoded_cbor_data_item, is written with its cycle.
TEST_CASE("decode_cycles: cbor::encode of the content of a tag 24 writes its cycle")
{
    std::string const inner = cbor::diag2cbor("28([29(0)])").value();
    std::string hex;
    for (char const c : inner)
        hex += std::format("{:02x}", static_cast<unsigned char>(c));
    std::string const outer = cbor::diag2cbor("24(h'" + hex + "')").value();
    auto const tree = cbor::decode_cycles(outer);
    REQUIRE(tree.has_value());
    CHECK_EQ(shared_test::written_by_encode(**tree), outer);
    auto const content = cbor::encoded_cbor_data_item(*tree, **tree);
    REQUIRE(content.has_value());
    CHECK_EQ(shared_test::written_by_encode(*content), inner);
}

// The mark of a reference must reach every node above it, through siblings that hold none. A sibling without a
// reference is written in one walk, with the same bytes.
TEST_CASE("decode_cycles: cbor::encode of a deep reference and of a sibling without one")
{
    auto const tree = cbor::decode_cycles(cbor::diag2cbor("[[1, [2]], {\"a\": 28([3, [[29(0)]]])}]").value());
    REQUIRE(tree.has_value());
    CHECK_EQ(shared_test::written_by_encode(**tree), cbor::diag2cbor("[[1, [2]], {\"a\": 28([3, [[29(0)]]])}]").value());
    CHECK_EQ(shared_test::written_by_encode(shared_test::elements_of(*tree)[0]), cbor::diag2cbor("[1, [2]]").value());
}

// theories/Writer.v write: encode puts a tag 28 on a node that a back edge of the walk reaches. A
// tag 29 with the index of that mark on each back edge. Each expectation is the stream that write gives for the graph that cbor::decode_cycles reads.
TEST_CASE("decode_cycles: cbor::encode writes the marks of each cycle as Writer.v writes them")
{
    using shared_test::encoded_again;
    // An array that holds itself.
    CHECK_EQ(encoded_again("\xd8\x1c\x81\xd8\x1d\x00"sv), "\xd8\x1c\x81\xd8\x1d\x00"s);
    // A cycle through a tag that is not an array or map.
    CHECK_EQ(encoded_again("\xd8\x1c\xd9\x04\xd2\x81\xd8\x1d\x00"sv), "\xd8\x1c\xd9\x04\xd2\x81\xd8\x1d\x00"s);
    // A map that is its own key.
    CHECK_EQ(encoded_again("\xd8\x1c\xa1\xd8\x1d\x00\x01"sv), "\xd8\x1c\xa1\xd8\x1d\x00\x01"s);
    // A mark that no reference names is not written.
    CHECK_EQ(encoded_again("\x82\xd8\x1c\x01\x02"sv), "\x82\x01\x02"s);
    // A mark of a reference: both references name the root, which gets one mark.
    CHECK_EQ(encoded_again("\xd8\x1c\x82\xd8\x1c\xd8\x1d\x00\xd8\x1d\x01"sv), "\xd8\x1c\x82\xd8\x1d\x00\xd8\x1d\x00"s);
}

// theories/Writer.v write_read_back: what the encoder writes reads back to the same graph, compared by its unfolding.
// encode_iov writes the same bytes as encode.
TEST_CASE("decode_cycles: cbor::decode_cycles reads back what cbor::encode writes")
{
    for (std::string_view const input :
         {"\xd8\x1c\x81\xd8\x1d\x00"sv, "\xd8\x1c\xd9\x04\xd2\x81\xd8\x1d\x00"sv, "\xd8\x1c\xa1\xd8\x1d\x00\x01"sv,
          "\xd8\x1c\x82\xd8\x1c\xd8\x1d\x00\xd8\x1d\x01"sv, "\xd8\x1c\xa1\x61\x61\xd8\x1c\x82\xd8\x1d\x00\xd8\x1d\x01"sv,
          "\xd8\x1c\x82\xd8\x1c\x81\xd8\x1d\x01\xd8\x1d\x00"sv}) {
        CAPTURE(input);
        auto const tree = cbor::decode_cycles(input);
        REQUIRE(tree.has_value());
        std::string const out = shared_test::encoded_again(input);
        auto const back = cbor::decode_cycles(out);
        REQUIRE(back.has_value());
        CHECK(test::unfoldings_equal(**tree, **back));
#if __has_include(<sys/uio.h>)
        auto const pieces = cbor::encode_iov(**tree);
        REQUIRE(pieces.has_value());
        std::string joined;
        for (iovec const &piece : **pieces)
            joined.append(static_cast<char const *>(piece.iov_base), piece.iov_len);
        CHECK_EQ(joined, out);
#endif
    }
}

// Each mark gets the index of its place in the output, in the shortest head (RFC 8949 4.2.1). Take n values that each
// hold themselves come back as they were written. The indices have one, two, three and five bytes.
TEST_CASE("decode_cycles: cbor::encode writes each index in its shortest head")
{
    for (std::size_t const n : {24uz, 25uz, 257uz, 65537uz}) {
        CAPTURE(n);
        std::string input = shared_test::shortest_head_of(4, n);
        for (std::size_t i = 0; i < n; ++i)
            input += "\xd8\x1c\x81\xd8\x1d"s + shared_test::shortest_head_of(0, i);
        CHECK_EQ(shared_test::encoded_again(input), input);
    }
}

// A shared reference names one of the values that were marked before it. The decoder and the lazy reader read the index
// through this one check. So an index that no mark has reached gives the same error on both paths. The index is
// converted to std::size_t only after it is known to fit.
TEST_CASE("value_sharing: check_sharedref_index for up to three marks")
{
    std::array<std::uint64_t, 6> const arguments{0, 1, 2, 3, 4, std::numeric_limits<std::uint64_t>::max()};
    for (std::size_t marked = 0; marked <= 3; ++marked)
        for (std::uint64_t const argument : arguments) {
            auto const r = cbor::detail::value_sharing::check_sharedref_index(argument, marked);
            if (argument >= marked)
                CHECK_EQ(r.error(), error::sharedref_index_not_marked);
            else
                CHECK_EQ(std::to_underlying(*r), argument);
        }
}

template <class Argument>
constexpr bool sharedref_index_accepts =
    requires(Argument const argument, void (*const take)(cbor::detail::value_sharing::index)) { take(argument); };

// An index into the marks of tags 28 and 29 is a type of its own. A plain integer that stands where a shared reference
// index is expected does not compile. So a count or a position cannot take its place silently.
TEST_CASE("value_sharing: index takes no plain integer")
{
    CHECK(sharedref_index_accepts<cbor::detail::value_sharing::index>);
    CHECK_FALSE(sharedref_index_accepts<std::size_t>);
    CHECK_FALSE(sharedref_index_accepts<std::uint64_t>);
}
