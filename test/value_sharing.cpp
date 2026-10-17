#include <algorithm>
#include "owned_copy.hpp"
#include <array>
#include <cstdint>
#include <expected>
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

#include "item_head.hpp"
#include "reference.hpp"

#include <span>
#include <unordered_map>
#include <unordered_set>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace shared_test
{

// The decoded item tree of a top-level item, as lazy::decode gives it.
using tree = cbor::owning<cbor::item>;

// An item of a decoded tree together with the tree. A tree decode reads a tag 29 as a plain tag, so the tests of
// the encoder find the value that a tag 29 names in the tree itself.
struct place {
    tree t;
    cbor::item const *i;

    cbor::item const &operator*() const
    {
        return *i;
    }

    cbor::item const *operator->() const
    {
        return i;
    }
};

// The tags 28 of a tree in stream order. RFC 8949 3.4 and the value-sharing registration number the marks in this
// order, and a tag 29 holds the index of one of them.
std::vector<cbor::item const *> marks_of(tree const &t)
{
    std::vector<cbor::item const *> marks;
    std::vector<cbor::item const *> pending{&*t};
    while (!pending.empty()) {
        cbor::item const *const i = pending.back();
        pending.pop_back();
        if (i->type == cbor::type::tag) {
            if (i->tag_number().value() == 28)
                marks.push_back(i);
            pending.push_back(i->tag_content().value());
        }
        if (i->type == cbor::type::array || i->type == cbor::type::map) {
            auto const c = children_of(*i);
            for (auto it = c.rbegin(); it != c.rend(); ++it)
                pending.push_back(*it);
        }
    }
    return marks;
}

// The item that a mark of tag 28 or a reference of tag 29 stands for, read from the tree by the index of the mark.
place target(place n)
{
    for (int steps = 0; steps < 1000; ++steps) {
        if (n.i->type == cbor::type::tag && n.i->tag_number().value() == 29) {
            auto const marks = marks_of(n.t);
            REQUIRE(n.i->tag_content().value()->type == cbor::type::unsigned_integer);
            REQUIRE(n.i->tag_content().value()->argument().value() < marks.size());
            n.i = marks[n.i->tag_content().value()->argument().value()];
        } else if (n.i->type == cbor::type::tag && n.i->tag_number().value() == 28)
            n.i = n.i->tag_content().value();
        else
            return n;
    }
    FAIL("a chain of marks without an end");
    return n;
}

place target(tree const &t)
{
    return target(place{t, &*t});
}

tree decoded_ref(std::string_view const wire)
{
    auto const l = test::lazy_of_copy(wire);
    REQUIRE(l.has_value());
    auto t = l->decode();
    REQUIRE(t.has_value());
    return *t;
}

template <class P>
std::vector<place> children(P const &container)
{
    place const c = target(container);
    std::vector<place> out;
    for (cbor::item const *const child : children_of(*c))
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
    for (std::size_t i = 0; i + 1 < c.size(); i += 2)
        if (place const k = target(c[i]); k->type == cbor::type::text_string && k->text_string().value() == key)
            return target(c[i + 1]);
    FAIL("no such key");
    return c.front();
}

// The content of a tag that is no mark, as the item of an application object holds it.
template <class P>
place content(P const &tagged)
{
    place const t = target(tagged);
    return target(place{t.t, t->tag_content().value()});
}

// A resolved tag 29 gives a tree of its own, so two places are compared by the encoded form of their targets.
std::string encoded_of(place const &p)
{
    std::string out;
    REQUIRE(cbor::encode(*p.i, out).has_value());
    return out;
}

// A tag 29 stands where a value is not written again. A resolved tag 29 has no identity in this test yet, so a test
// that two values are distinct checks that the second one is written in full.
inline bool is_reference(place const &p)
{
    return p->type == cbor::type::tag && p->tag_number().value() == 29;
}

template <class A, class B>
bool same(A const &a, B const &b)
{
    return encoded_of(target(a)) == encoded_of(target(b));
}

} // namespace shared_test

using namespace shared_test;

namespace shared_test
{

// The values of an application with identity, as mrb_value is: two handles to one node are the same object.
struct node;
using handle = std::shared_ptr<node>;

// An object of the application that it writes as its own tag over a content.
struct object {
    std::uint64_t tag;
    handle content;
};

struct node {
    std::variant<std::uint64_t, std::string, std::vector<handle>, std::vector<std::pair<handle, handle>>, object> kind;
};

handle u(std::uint64_t const n)
{
    return std::make_shared<node>(node{n});
}

handle s(std::string text)
{
    return std::make_shared<node>(node{std::move(text)});
}

handle arr(std::vector<handle> elements)
{
    return std::make_shared<node>(node{std::move(elements)});
}

handle obj(std::vector<std::pair<handle, handle>> pairs)
{
    return std::make_shared<node>(node{std::move(pairs)});
}

handle object_of(std::uint64_t const tag, handle content)
{
    return std::make_shared<node>(node{object{tag, std::move(content)}});
}

// The writer of the application. It decides what has identity: an integer has none, and a string key has none, as
// mruby copies an unfrozen String key. The encoder writes tags 28 and 29 only for a cycle: a first pass walks the
// value as the write walks it and finds each node that a cycle reaches again. The second pass writes tag 28 before
// such a node and tag 29 with its index where the cycle reaches the node again, through the public encoder. A node
// that two places hold without a cycle is written in full at each place. The mode repeats_marked writes the input
// of another writer, which marks every node it meets twice; the decoder reads that input too.
enum class marks { cycles, repeats_marked, none };

// The public encoder writes an unsigned integer in the width of its type, so the writer writes the index of a tag 29
// in one byte. The tests hold fewer than 256 marks.
std::uint8_t index_of(std::size_t const i)
{
    REQUIRE(i <= std::numeric_limits<std::uint8_t>::max());
    return static_cast<std::uint8_t>(i);
}

struct shared_writer {
    marks mode = marks::cycles;
    std::unordered_map<node const *, std::size_t> seen;
    std::unordered_set<node const *> cyclic;
    std::unordered_map<node const *, std::vector<std::uint64_t>> open;
    std::unordered_map<node const *, std::uint64_t> numbers;
    std::uint64_t next = 0;
    int content_reads = 0;

    static node const *identity(handle const &v, bool const key)
    {
        if (std::holds_alternative<std::uint64_t>(v->kind) || (key && std::holds_alternative<std::string>(v->kind)))
            return nullptr;
        return v.get();
    }

    template <class F>
    static void children_visit(handle const &v, F const &f)
    {
        if (auto const *a = std::get_if<std::vector<handle>>(&v->kind)) {
            for (handle const &e : *a)
                f(e, false);
        } else if (auto const *m = std::get_if<std::vector<std::pair<handle, handle>>>(&v->kind)) {
            for (auto const &[k, x] : *m) {
                f(k, true);
                f(x, false);
            }
        } else if (auto const *o = std::get_if<object>(&v->kind)) {
            f(o->content, false);
        }
    }

    void count(handle const &v, bool const key)
    {
        node const *const id = identity(v, key);
        if (mode == marks::repeats_marked) {
            if (id != nullptr && ++seen[id] > 1)
                return;
        } else if (id != nullptr) {
            if (open.contains(id)) {
                cyclic.insert(id);
                return;
            }
            open[id];
        }
        if (std::holds_alternative<object>(v->kind))
            ++content_reads;
        children_visit(v, [this](handle const &c, bool const k) { count(c, k); });
        if (mode != marks::repeats_marked && id != nullptr)
            open.erase(id);
    }

    template <class Target>
    std::expected<void, error> write(cbor::encoder<Target> &e, handle const &v, bool const key)
    {
        node const *const id = identity(v, key);
        bool opened = false;
        if (id != nullptr && mode == marks::repeats_marked) {
            if (auto const n = numbers.find(id); n != numbers.end())
                return e.tag(29).and_then([&] { return e.unsigned_integer(index_of(n->second)); });
            if (seen[id] > 1) {
                if (auto const r = e.tag(28); !r)
                    return r;
                numbers.emplace(id, next++);
            }
        } else if (id != nullptr) {
            if (auto const n = open.find(id); n != open.end() && !n->second.empty()) {
                if (mode == marks::none)
                    return std::unexpected(error::cyclic_data_structure);
                return e.tag(29).and_then([&] { return e.unsigned_integer(index_of(n->second.back())); });
            }
            if (cyclic.contains(id)) {
                if (auto const r = e.tag(28); !r)
                    return r;
                open[id].push_back(next++);
            } else {
                open[id].push_back(0);
            }
            opened = true;
        }
        auto const r = content_write(e, v);
        if (opened)
            open[id].pop_back();
        return r;
    }

    template <class Target>
    std::expected<void, error> content_write(cbor::encoder<Target> &e, handle const &v)
    {
        if (auto const *n = std::get_if<std::uint64_t>(&v->kind))
            return e.unsigned_integer(*n);
        if (auto const *t = std::get_if<std::string>(&v->kind))
            return e.text_string(std::string_view(*t));
        if (auto const *a = std::get_if<std::vector<handle>>(&v->kind)) {
            if (auto const r = e.array(a->size()); !r)
                return r;
            for (handle const &x : *a)
                if (auto const r = write(e, x, false); !r)
                    return r;
            return {};
        }
        if (auto const *m = std::get_if<std::vector<std::pair<handle, handle>>>(&v->kind)) {
            if (auto const r = e.map(m->size()); !r)
                return r;
            for (auto const &[k, x] : *m) {
                if (auto const r = write(e, k, true); !r)
                    return r;
                if (auto const r = write(e, x, false); !r)
                    return r;
            }
            return {};
        }
        object const &o = std::get<object>(v->kind);
        if (auto const r = e.tag(o.tag); !r)
            return r;
        return write(e, o.content, false);
    }
};

std::string encoded_shared(handle const &v, marks const mode = marks::cycles)
{
    shared_writer w;
    w.mode = mode;
    w.count(v, false);
    std::string out;
    cbor::encoder e(out);
    REQUIRE(w.write(e, v, false).has_value());
    REQUIRE(e.finish().has_value());
    return out;
}

tree round_trip(handle const &v, marks const mode = marks::cycles)
{
    return decoded_ref(encoded_shared(v, mode));
}

} // namespace shared_test

// The owner decided that tags 28 and 29 exist only to write cycles. A value that two places hold without a cycle is
// written in full at each place, with no tag 28, and decodes to two equal values.
TEST_CASE("tag 28/29: a repeated value without a cycle is written twice")
{
    auto const a = arr({u(1), u(2)});
    std::string const one = "\x82"s + test::native_unsigned(1) + test::native_unsigned(2);
    CHECK_EQ(encoded_shared(arr({a, a})), "\x82"s + one + one);
    auto const r = round_trip(arr({a, a}));
    CHECK_FALSE(is_reference(children(r)[1]));
    CHECK_EQ(argument_of(*element(element(r, 1), 1)), 2u);
}

// Ported from test.rb: 'tag 28/29: repeated value → identity preserved (eager)'. The input comes from a writer that
// marks every repeated value; the decoder keeps its identity.
TEST_CASE("tag 28/29: a repeated value that another writer marked keeps its identity")
{
    auto const a = arr({u(1), u(2)});
    auto const r = round_trip(arr({a, a}), marks::repeats_marked);
    CHECK(same(element(r, 0), element(r, 1)));
}

// Ported from test.rb: 'tag 28/29: three-way sharing preserves identity (eager)'.
TEST_CASE("tag 28/29: three-way sharing")
{
    auto const v = arr({u(1), u(2), u(3)});
    auto const r = round_trip(obj({{s("a"), v}, {s("b"), v}, {s("c"), v}}), marks::repeats_marked);
    CHECK(same(at(r, "a"), at(r, "b")));
    CHECK(same(at(r, "b"), at(r, "c")));
}

// Ported from test.rb: 'tag 28/29: shared hash / shared string roundtrip'.
TEST_CASE("tag 28/29: shared map and shared string")
{
    auto const h = obj({{s("k"), s("v")}, {s("n"), u(42)}});
    auto const rh = round_trip(arr({h, h, h}), marks::repeats_marked);
    CHECK(same(element(rh, 0), element(rh, 1)));
    CHECK(same(element(rh, 1), element(rh, 2)));
    auto const str = s("shared_string");
    auto const rs = round_trip(arr({str, str, obj({{s("key"), str}})}), marks::repeats_marked);
    CHECK(same(element(rs, 0), element(rs, 1)));
    CHECK(same(element(rs, 1), at(element(rs, 2), "key")));
}

// Ported from test.rb: 'tag 28/29: cyclic array and cyclic hash (eager)'.
TEST_CASE("tag 28/29: a cyclic array and a cyclic map")
{
    auto const a = arr({});
    std::get<std::vector<handle>>(a->kind).push_back(a);
    CHECK_EQ(encoded_shared(a), "\xd8\x1c\x81\xd8\x1d\x18\x00"sv);
    auto const ra = round_trip(a);
    CHECK(same(ra, element(ra, 0)));
    auto const h = obj({});
    std::get<std::vector<std::pair<handle, handle>>>(h->kind).emplace_back(s("self"), h);
    auto const rh = round_trip(h);
    CHECK(same(rh, at(rh, "self")));
    // The cycles of the application hold their nodes; the test ends them, so the sanitizers see no leak.
    std::get<std::vector<handle>>(a->kind).clear();
    std::get<std::vector<std::pair<handle, handle>>>(h->kind).clear();
}

// A cycle can come from the wire. encode of the tree writes the reference as it stands and does not follow it, so a
// cycle ends. A tree that is only deep is nesting_depth_exceeded under a lower limit.
TEST_CASE("tag 28/29: encode of a tree with a cycle ends")
{
    tree const a = decoded_ref("\xd8\x1c\x81\xd8\x1d\x00"sv);
    std::string out;
    REQUIRE(cbor::encode(*a, out).has_value());
    CHECK_EQ(out, "\xd8\x1c\x81\xd8\x1d\x00"sv);

    tree const deep = decoded_ref(repeat("\x81"sv, 20) + '\x01');
    test::limits_guard const depth{{.nesting_depth = 16}};
    std::string d;
    auto const too_deep = cbor::encode(*deep, d);
    REQUIRE_FALSE(too_deep.has_value());
    CHECK_EQ(too_deep.error(), error::nesting_depth_exceeded);
}

// Ported from test.rb: 'tag 28/29: mutual recursion — hash↔array cycle'.
TEST_CASE("tag 28/29: mutual recursion between a map and an array")
{
    auto const list = arr({});
    auto const h = obj({{s("list"), list}});
    std::get<std::vector<handle>>(list->kind) = {h, h};
    auto const r = round_trip(h);
    CHECK(same(r, element(at(r, "list"), 0)));
    CHECK(same(r, element(at(r, "list"), 1)));
    std::get<std::vector<handle>>(list->kind).clear();
}

// An application that writes no marks cannot write a cycle, and its writer gives cyclic_data_structure.
TEST_CASE("without marks: a cycle is cyclic_data_structure")
{
    auto const a = arr({});
    std::get<std::vector<handle>>(a->kind).push_back(a);
    shared_writer w;
    w.mode = marks::none;
    std::string out;
    cbor::encoder e(out);
    auto const r = w.write(e, a, false);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::cyclic_data_structure);
    std::get<std::vector<handle>>(a->kind).clear();
}

// Ported from test.rb: 'tag 28/29: cyclic map round-trip — full cycle navigable'.
TEST_CASE("tag 28/29: a full cycle through two maps")
{
    auto const inner = obj({{s("name"), s("cycle")}});
    auto const outer = obj({{s("child"), inner}});
    std::get<std::vector<std::pair<handle, handle>>>(inner->kind).emplace_back(s("parent"), outer);
    auto const r = round_trip(outer);
    CHECK(same(r, at(at(r, "child"), "parent")));
    CHECK(same(at(r, "child"), at(at(at(r, "child"), "parent"), "child")));
    std::get<std::vector<std::pair<handle, handle>>>(inner->kind).clear();
}

// Ported from test.rb: 'tag 28/29: shared leaf reached through deep paths'.
TEST_CASE("tag 28/29: a shared leaf through a deep path")
{
    auto const leaf = arr({u(10), u(20), u(30)});
    auto const deep = obj({{s("e"), leaf}});
    auto const root =
        obj({{s("a"), obj({{s("b"), obj({{s("c"), obj({{s("d"), deep}})}})}})}, {s("x"), leaf}});
    auto const r = round_trip(root, marks::repeats_marked);
    CHECK(same(at(at(at(at(at(r, "a"), "b"), "c"), "d"), "e"), at(r, "x")));
}

// Ported from test.rb: 'tag 28/29: diamond pattern preserves identity across all paths'.
TEST_CASE("tag 28/29: a diamond")
{
    auto const bottom = obj({{s("value"), u(99)}});
    auto const root = obj({{s("left"), obj({{s("child"), bottom}})},
                           {s("right"), obj({{s("child"), bottom}})},
                           {s("direct"), bottom}});
    auto const r = round_trip(root, marks::repeats_marked);
    CHECK(same(at(at(r, "left"), "child"), at(at(r, "right"), "child")));
    CHECK(same(at(at(r, "left"), "child"), at(r, "direct")));
}

// Ported from test.rb: 'tag 28/29: nested sharing — outer and inner both share'.
TEST_CASE("tag 28/29: outer and inner both share")
{
    auto const inner = arr({u(1), u(2), u(3)});
    auto const outer = arr({inner, inner});
    auto const r = round_trip(arr({outer, outer}), marks::repeats_marked);
    CHECK(same(element(r, 0), element(r, 1)));
    CHECK(same(element(element(r, 0), 0), element(element(r, 0), 1)));
    CHECK(same(element(element(r, 0), 0), element(element(r, 1), 0)));
}

// Ported from test.rb: 'tag 28/29: distinct shared groups do not conflate'.
TEST_CASE("tag 28/29: distinct groups do not conflate")
{
    auto const a = arr({u(1), u(2)});
    auto const b = obj({{s("k"), s("v")}});
    auto const c = s("str");
    auto const r = round_trip(obj({{s("a1"), a},
                                   {s("a2"), a},
                                   {s("b1"), b},
                                   {s("b2"), b},
                                   {s("c1"), c},
                                   {s("c2"), c},
                                   {s("nested"), obj({{s("a"), a}, {s("b"), b}, {s("c"), c}})}}), marks::repeats_marked);
    CHECK(same(at(r, "a1"), at(at(r, "nested"), "a")));
    CHECK(same(at(r, "b1"), at(at(r, "nested"), "b")));
    CHECK(same(at(r, "c1"), at(at(r, "nested"), "c")));
    CHECK_FALSE(same(at(r, "a1"), at(r, "b1")));
    CHECK_FALSE(same(at(r, "b1"), at(r, "c1")));
}

// Ported from test.rb: 'tag 28/29: hash keys do not participate in sharing'. The application decides what has
// identity; this one gives a string key no identity, as mruby copies an unfrozen String key.
TEST_CASE("tag 28/29: a string key without identity is written each time")
{
    auto const k = s("repeated_key");
    CHECK_EQ(encoded_shared(arr({obj({{k, u(1)}}), obj({{k, u(2)}})})),
             "\x82\xa1\x6crepeated_key"s + test::native_unsigned(1) + "\xa1\x6crepeated_key"s + test::native_unsigned(2));
}

// Replaces test.rb: 'tag 28/29: same object as value AND key — only values share'. mruby keeps an
// Array key as the same object (measured with mruby 14fc2ef), so an array key with identity shares.
TEST_CASE("tag 28/29: an array key with identity shares with the values")
{
    auto const a = arr({u(1), u(2), u(3)});
    auto const r = round_trip(obj({{s("v1"), a}, {s("v2"), a}, {s("as_key_map"), obj({{a, s("payload")}})}}), marks::repeats_marked);
    CHECK(same(at(r, "v1"), at(r, "v2")));
    auto const key = target(children(at(r, "as_key_map"))[0]);
    CHECK(same(key, at(r, "v1")));
}

// Ported from test.rb: 'no sharedref flag: values do not share, cycles hit depth limit'. An application that
// writes no marks writes a shared value each time, and the decoded values are distinct.
TEST_CASE("without marks: values are written each time")
{
    auto const shared = arr({u(1), u(2), u(3)});
    auto const r = decoded_ref(encoded_shared(obj({{s("a"), shared}, {s("b"), shared}}), marks::none));
    CHECK_FALSE(is_reference(children(r)[3]));
}

// Ported from test.rb: 'registered tag + sharedref: same instance in array → identity preserved'. The owner decided
// that only a cycle is shared, so one object three times is written three times in full, with no tag 28.
TEST_CASE("application tag: one object three times is written three times")
{
    auto const p = object_of(5000, arr({u(3), u(7)}));
    shared_writer w;
    w.count(arr({p, p, p}), false);
    CHECK_EQ(w.content_reads, 3);
    std::string const one = "\xd9\x13\x88\x82"s + test::native_unsigned(3) + test::native_unsigned(7);
    CHECK_EQ(encoded_shared(arr({p, p, p})), "\x83"s + one + one + one);
}

// The iov target of the encoder gives the bytes of the string target, the marks and a long string included.
TEST_CASE("application tag: the iov target with shared references gives the bytes of the string target")
{
    auto const p = object_of(5000, arr({u(3), u(7)}));
    handle const v = arr({p, p, p, s(std::string(40, 'x'))});
    shared_writer w;
    w.count(v, false);
    cbor::iov_writer iov;
    cbor::encoder e(iov);
    REQUIRE(w.write(e, v, false).has_value());
    auto const pieces = e.finish();
    REQUIRE(pieces.has_value());
    std::string joined;
    for (std::string_view const piece : **pieces)
        joined.append(piece);
    CHECK_EQ(joined, encoded_shared(v));
}

// Ported from test.rb: 'registered tag + sharedref: distinct instances with equal fields do NOT share'.
TEST_CASE("application tag: distinct objects with equal content do not share")
{
    auto const p1 = object_of(5000, arr({u(1), u(2)}));
    auto const p2 = object_of(5000, arr({u(1), u(2)}));
    shared_writer w;
    w.count(arr({p1, p2}), false);
    CHECK_EQ(w.content_reads, 2);
    CHECK_EQ(encoded_shared(arr({p1, p2})), "\x82\xd9\x13\x88\x82"s + test::native_unsigned(1) + test::native_unsigned(2) + "\xd9\x13\x88\x82"s + test::native_unsigned(1) + test::native_unsigned(2));
}

// Ported from test.rb: 'registered tag + sharedref: instance with self-referential field'. The tag item of the
// object exists before its content, so a reference inside the content names the object.
TEST_CASE("application tag: a reference inside the content names the object")
{
    auto const r = decoded_ref("\xd8\x1c\xd9\x13\x88\x81\xd8\x1d\x00"sv);
    CHECK_EQ(argument_of(*target(r)), 5000u);
    CHECK(same(element(content(r), 0), r));
}

// A reference after the object names the tag item of the object, the same item as the place of the mark.
TEST_CASE("application tag: a reference after the object names the object")
{
    auto const r = decoded_ref("\x82\xd8\x1c\xd9\x13\x88\x81\x01\xd8\x1d\x00"sv);
    CHECK_EQ(argument_of(*element(r, 0)), 5000u);
    CHECK(same(element(r, 0), element(r, 1)));
}

// Ported from test.rb: 'registered tag + sharedref: mutual recursion between two instances'.
TEST_CASE("application tag: two objects that name each other")
{
    // 28 5000([28 5000([29 0])])
    auto const r = decoded_ref("\xd8\x1c\xd9\x13\x88\x81\xd8\x1c\xd9\x13\x88\x81\xd8\x1d\x00"sv);
    place const peer = element(content(r), 0);
    CHECK_EQ(argument_of(*peer), 5000u);
    CHECK(same(element(content(peer), 0), r));
}

// A tag that no application knows is a tag item over its content.
TEST_CASE("application tag: a tag without meaning is a tag item")
{
    auto const r = decoded_ref("\xc1\x01"sv);
    CHECK_EQ(argument_of(*target(r)), 1u);
    CHECK_EQ(argument_of(*content(r)), 1u);
}

// Ported from test.rb: 'tag 28/29: shared value preserved via lazy.value'.
TEST_CASE("lazy: a shared value keeps its identity")
{
    auto const a = arr({u(1), u(2)});
    std::string const doc = encoded_shared(arr({a, a}), marks::repeats_marked);
    auto const r = test::lazy_of_copy(doc)->decode();
    REQUIRE(r.has_value());
    CHECK(same(element(*r, 0), element(*r, 1)));
}

// Ported from test.rb: 'tag 28/29: cyclic array materializes via lazy'.
TEST_CASE("lazy: a cyclic array")
{
    std::string const doc = "\xd8\x1c\x81\xd8\x1d\x00"s;
    auto const r = test::lazy_of_copy(doc)->decode();
    REQUIRE(r.has_value());
    CHECK(same(*r, element(*r, 0)));
}

// An application as mruby gives the content of an object by value, as a temporary that only its writer holds. The
// encoder copies the content before the temporary ends.
TEST_CASE("application tag: a content made at the time of the write")
{
    std::string out;
    cbor::encoder e(out);
    for (int i = 0; i < 2; ++i) {
        REQUIRE(e.tag(5000).has_value());
        shared_writer w;
        handle const fresh = arr({u(1), arr({u(2), u(3)})});
        REQUIRE(w.write(e, fresh, false).has_value());
    }
    REQUIRE(e.finish().has_value());
    std::string const one = "\xd9\x13\x88\x82"s + test::native_unsigned(1) + "\x82"s + test::native_unsigned(2) + test::native_unsigned(3);
    CHECK_EQ(out, one + one);
}

// Ported from test.rb: 'tag 28/29: shared value preserved via lazy.value'. The map half: the first
// test above covers only the array half.
TEST_CASE("lazy: a value shared by two map values keeps its identity")
{
    auto const v = arr({u(1), u(2), u(3)});
    std::string const doc = encoded_shared(obj({{s("a"), v}, {s("b"), v}}), marks::repeats_marked);
    auto const r = test::lazy_of_copy(doc)->decode();
    REQUIRE(r.has_value());
    CHECK(same(at(*r, "a"), at(*r, "b")));
}

// Ported from test.rb: 'tag 28: self-referential Tag 28 inside Tag 28 is safe'. The exact bytes of
// mruby-cbor: two marks on one value and no reference, which must decode without a fault.
TEST_CASE("tag 28: inside tag 28 without a reference")
{
    auto const r = decoded_ref("\xd8\x1c\xd8\x1c\x18\x2a"sv);
    CHECK_EQ(argument_of(*target(r)), 42u);
}

// Ported from test.rb: 'registered tag + sharedref: same instance in array → identity preserved'. The
// test above checks the encoder only; this one checks that the decoder gives one object three times.
TEST_CASE("application tag: the same object three times keeps its identity")
{
    auto const p = object_of(5000, arr({u(3), u(7)}));
    auto const r = round_trip(arr({p, p, p}), marks::repeats_marked);
    CHECK(same(element(r, 0), element(r, 1)));
    CHECK(same(element(r, 1), element(r, 2)));
}

// Ported from test.rb: 'registered tag + sharedref: distinct instances with equal fields do NOT share'.
// The test above checks the encoder only; this one checks that the decoder gives two objects.
TEST_CASE("application tag: distinct objects with equal content decode to two objects")
{
    auto const p1 = object_of(5000, arr({u(1), u(2)}));
    auto const p2 = object_of(5000, arr({u(1), u(2)}));
    auto const r = round_trip(arr({p1, p2}));
    CHECK_FALSE(is_reference(children(r)[1]));
}

// Ported from test.rb: 'registered tag + sharedref: non-immediate ivar (String) shares correctly'. The
// string inside the content gets its own mark, so the mark of the object must be placed before it.
TEST_CASE("application tag: an object whose content holds a string keeps its identity")
{
    auto const cfg = object_of(5001, arr({u(30), s("default")}));
    auto const r = round_trip(obj({{s("p"), cfg}, {s("b"), cfg}, {s("f"), cfg}}), marks::repeats_marked);
    CHECK(same(at(r, "p"), at(r, "b")));
    CHECK(same(at(r, "b"), at(r, "f")));
}

// Ported from test.rb: 'registered tag + sharedref: identity preserved through lazy.value'.
TEST_CASE("lazy: a registered object keeps its identity")
{
    auto const l = object_of(5003, arr({u(99)}));
    std::string const doc = encoded_shared(obj({{s("a"), l}, {s("b"), l}}), marks::repeats_marked);
    auto const r = test::lazy_of_copy(doc)->decode();
    REQUIRE(r.has_value());
    CHECK(same(at(*r, "a"), at(*r, "b")));
}

// The owner decided that a user of the public encoder never writes tags 28 and 29. The encoder writes them
// only for a cycle that it finds itself.
template <class E>
concept writes_shareable = requires(E e) { e.shareable(); };

template <class E>
concept writes_sharedref = requires(E e) { e.sharedref(std::uint64_t{0}); };

TEST_CASE("encoder: shareable and sharedref are not public")
{
    CHECK_FALSE(writes_shareable<cbor::encoder<std::string>>);
    CHECK_FALSE(writes_sharedref<cbor::encoder<std::string>>);
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

// The owner decided on 2026-10-13 that every reader but an eager struct decode reads tags 28 and 29 as plain tags:
// 28 over its content and 29 over its content, with no index check and no error of its own. RFC 8949 3 lets the
// argument of a head take 1, 2, 4 or 8 bytes, and each form is read the same.
TEST_CASE("value sharing: lazy, at_path, decode and diagnostic notation read tags 28 and 29 as tags")
{
    for (std::size_t const bytes : {1uz, 2uz, 4uz, 8uz}) {
        CAPTURE(bytes);
        std::string const doc = "\x83"s + head_of(6, 28, bytes) + "\x05"s + head_of(6, 29, bytes) +
                                head_of(0, 0, bytes) + head_of(6, 29, bytes) + head_of(0, 7, bytes);
        auto const l = *test::lazy_of_copy(doc);
        CHECK_EQ(l.at(0)->type(), cbor::type::tag);
        CHECK_EQ(l.at(1)->type(), cbor::type::tag);
        CHECK_EQ(l.at(0)->get<std::uint64_t>(), 5u);
        CHECK_EQ(l.at(1)->get<std::uint64_t>(), 0u);
        CHECK_EQ(l.at(2)->get<std::uint64_t>(), 7u);
        CHECK_EQ(l.at_path<"$[1]", std::uint64_t>(), 0u);
        CHECK_EQ(cbor::at_path<"$[2]", std::uint64_t>(doc), 7u);
        std::vector<cbor::type> types;
        for (auto const e : *l.elements())
            types.push_back(*e->type());
        CHECK(types == std::vector<cbor::type>(3, cbor::type::tag));
        auto const tree = l.decode();
        REQUIRE(tree.has_value());
        auto const elements = children_of(**tree);
        REQUIRE_EQ(elements.size(), 3u);
        CHECK_EQ(elements[0]->tag_number().value(), 28u);
        CHECK_EQ(elements[0]->tag_content().value()->argument().value(), 5u);
        CHECK_EQ(elements[1]->tag_number().value(), 29u);
        CHECK_EQ(elements[1]->tag_content().value()->argument().value(), 0u);
        CHECK_EQ(elements[2]->tag_number().value(), 29u);
        CHECK_EQ(elements[2]->tag_content().value()->argument().value(), 7u);
        auto const text = cbor::diagnostic_notation(doc);
        REQUIRE(text.has_value());
        CHECK_EQ(text->contains("28_"), bytes > 1);
        CHECK_EQ(text->contains("29_"), bytes > 1);
    }
    CHECK_EQ(cbor::diagnostic_notation("\x83\xd8\x1c\x05\xd8\x1d\x00\xd8\x1d\x07"sv), "[28(5), 29(0), 29(7)]");
}

#ifdef __cpp_impl_reflection
// The eager table of one struct decode, for each form of the head of tag 29 and of its index: a forward index
// is sharedref_index_not_marked, and a tag 29 into a plain member is incorrect_type. A vector of
// std::optional is a plain array; a vector of numbers is a typed array, which holds no tag.
TEST_CASE("value sharing: an eager decode reads every form of a tag 29")
{
    using marked_numbers = std::vector<std::optional<std::uint64_t>>;
    for (std::size_t const bytes : {1uz, 2uz, 4uz, 8uz}) {
        CAPTURE(bytes);
        std::string const reference = head_of(6, 29, bytes) + head_of(0, 0, bytes);
        CHECK_EQ(cbor::structs<marked_numbers>::decode("\x81"s + reference).error(),
                 error::sharedref_index_not_marked);
        CHECK_EQ(cbor::structs<marked_numbers>::decode("\x82"s + head_of(6, 28, bytes) + "\x05"s + reference)
                     .error(),
                 error::incorrect_type);
        auto const marked = cbor::structs<marked_numbers>::decode("\x81"s + head_of(6, 28, bytes) + "\x05"s);
        REQUIRE(marked.has_value());
        CHECK((**marked == marked_numbers{5}));
    }
}
#endif
