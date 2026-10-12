#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <initializer_list>
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

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace shared_test
{

// The decoded item tree of a top-level item, as lazy::decode gives it.
using tree = std::shared_ptr<cbor::item const>;

// An item of a decoded tree together with the tree, which cbor::shared_value needs to follow a tag 29.
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

// The item that a mark of tag 28 or a reference of tag 29 stands for. Two places that stand for the same item are
// one value with identity, as RFC 8949 3.4 and the value-sharing registration describe it.
place target(place n)
{
    for (int steps = 0; steps < 1000; ++steps) {
        if (n.i->type == cbor::type::tag && n.i->via.tag.number == 29)
            n.i = &cbor::shared_value(n.t, *n.i);
        else if (n.i->type == cbor::type::tag && n.i->via.tag.number == 28)
            n.i = n.i->via.tag.content;
        else
            return n;
    }
    FAIL("a chain of marks without an end");
    return n;
}

place target(tree const &t)
{
    return target(place{t, t.get()});
}

tree decoded_ref(std::string_view const wire)
{
    auto const l = cbor::lazy::copy(wire);
    REQUIRE(l.has_value());
    auto t = l->decode();
    REQUIRE(t.has_value());
    return *t;
}

error ref_decode_error(std::string_view const wire)
{
    auto const l = cbor::lazy::copy(wire);
    REQUIRE(l.has_value());
    auto const t = l->decode();
    REQUIRE_FALSE(t.has_value());
    return t.error();
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
        if (place const k = target(c[i]); k->type == cbor::type::text_string && k->via.str == key)
            return target(c[i + 1]);
    FAIL("no such key");
    return c.front();
}

// The content of a tag that is no mark, as the item of an application object holds it.
template <class P>
place content(P const &tagged)
{
    place const t = target(tagged);
    return target(place{t.t, t->via.tag.content});
}

template <class A, class B>
bool same(A const &a, B const &b)
{
    return target(a).i == target(b).i;
}

} // namespace shared_test

using namespace shared_test;

// Ported from test.rb: 'tag 28/29: scalar shareable (integer) roundtrip via wire'.
TEST_CASE("tag 28/29: scalar shareable")
{
    auto const r = decoded_ref("\x82\xd8\x1c\x18\x2a\xd8\x1d\x00"sv);
    CHECK(same(element(r, 0), element(r, 1)));
    CHECK_EQ(argument_of(*element(r, 0)), 42u);
}

// Ported from test.rb: 'tag 29: invalid index raises IndexError, non-uint payload raises TypeError'
// and 'tag 29: uint64-max index overflow handled cleanly'. Each case keeps its own error, as each
// raised its own class in mruby-cbor.
TEST_CASE("tag 29: an index that no tag 28 marked")
{
    CHECK_EQ(ref_decode_error("\xd8\x1d\x18\x63"sv), error::sharedref_index_not_marked);
    CHECK_EQ(ref_decode_error("\xd8\x1d\x1b\xff\xff\xff\xff\xff\xff\xff\xff"sv),
             sizeof(std::size_t) < sizeof(std::uint64_t) ? error::sharedref_index_out_of_range
                                                         : error::sharedref_index_not_marked);
}

TEST_CASE("tag 29: content that is not an unsigned integer")
{
    CHECK_EQ(ref_decode_error("\xd8\x1d\x61"
                              "a"sv),
             error::inadmissible_type_for_tag_content);
}

// Ported from test.rb: 'tag 28: decoder accepts Tag 28 in map-value position'.
TEST_CASE("tag 28: in map-value position")
{
    auto const r = decoded_ref("\xa2\x61"
                               "a\xd8\x1c\x83\x01\x02\x03\x61"
                               "b\xd8\x1d\x00"sv);
    CHECK(same(map_value(r, 0), map_value(r, 1)));
}

// Ported from test.rb: 'tag 28: self-referential Tag 28 inside Tag 28 is safe'.
// Two marks, index 0 and 1, both on the value 42.
TEST_CASE("tag 28: inside tag 28")
{
    auto const r = decoded_ref("\x83\xd8\x1c\xd8\x1c\x18\x2a\xd8\x1d\x00\xd8\x1d\x01"sv);
    CHECK(same(element(r, 1), element(r, 0)));
    CHECK(same(element(r, 2), element(r, 0)));
}

// Ported from test.rb: 'tag 28/29: cyclic array and cyclic hash (eager)'.
// value-sharing: "it has to record the reference before decoding the value".
TEST_CASE("tag 28/29: cyclic array and cyclic map")
{
    auto const a = decoded_ref("\xd8\x1c\x81\xd8\x1d\x00"sv);
    CHECK(same(a, element(a, 0)));
    auto const m = decoded_ref("\xd8\x1c\xa1\x64self\xd8\x1d\x00"sv);
    CHECK(same(m, map_value(m, 0)));
}

// Ported from test.rb: 'tag 28/29: mutual recursion — hash↔array cycle'.
TEST_CASE("tag 28/29: mutual recursion between a map and an array")
{
    auto const r = decoded_ref("\xd8\x1c\xa1\x64list\x82\xd8\x1d\x00\xd8\x1d\x00"sv);
    CHECK(same(r, element(map_value(r, 0), 0)));
    CHECK(same(r, element(map_value(r, 0), 1)));
}

// Ported from test.rb: 'tag 28/29: distinct shared groups do not conflate'.
// The index is the order of the marks on the wire, as the encoder wrote them.
TEST_CASE("tag 28/29: two groups keep their own index")
{
    auto const r = decoded_ref("\x84\xd8\x1c\x81\x01\xd8\x1c\x81\x02\xd8\x1d\x01\xd8\x1d\x00"sv);
    CHECK(same(element(r, 2), element(r, 1)));
    CHECK(same(element(r, 3), element(r, 0)));
    CHECK_FALSE(same(element(r, 0), element(r, 1)));
}

// Every item of the tree exists before its content, a tag too. Tag 6 takes every content, so the check of the tag
// content (RFC 8949 3.4.1, 3.4.2) does not answer first, and 28(6(29(0))) is a tag that holds itself. The reference
// model, which has no identity, refuses it.
TEST_CASE("tag 29: a reference to a tag that holds itself")
{
    auto const r = decoded_ref("\xd8\x1c\xc6\xd8\x1d\x00"sv);
    CHECK_EQ(argument_of(*target(r)), 6u);
    CHECK(same(content(r), r));
    CHECK_EQ(decode_error("\xd8\x1c\xc6\xd8\x1d\x00"sv), error::sharedref_not_complete);
}

// Found by the fuzzer: a mark whose content is a chain of marks and a reference back to the first mark has no
// end. The check of the content of tag 0 followed the chain without a stop.
TEST_CASE("tag 29: a chain of marks that refers to its own start ends with an error")
{
    CHECK_EQ(decode_error("\xc0\xd8\x1c\xd8\x1c\xd8\x1d\x00"sv), error::sharedref_not_complete);
    CHECK_EQ(ref_decode_error("\xc0\xd8\x1c\xd8\x1c\xd8\x1d\x00"sv), error::sharedref_not_complete);
    CHECK_EQ(decode_error("\xc0\xd8\x1c\xd8\x1d\x00"sv), error::sharedref_not_complete);
    CHECK_EQ(decode_error("\xd8\x24\xd8\x1c\xd8\x1c\xd8\x1c\xd8\x1d\x01"sv), error::sharedref_not_complete);
    CHECK_EQ(ref_decode_error("\xd8\x24\xd8\x1c\xd8\x1c\xd8\x1c\xd8\x1d\x01"sv), error::sharedref_not_complete);
}

// value-sharing: a cyclic data structure needs a reference to a value before it is completely decoded. The
// reference model holds values, not references, so a cycle would come back as a cut copy; it refuses the cycle
// instead. Sharing without a cycle stays: the model gets a copy.
TEST_CASE("tag 28/29: a model without identity refuses a cycle")
{
    CHECK_EQ(decode_error("\xd8\x1c\x81\xd8\x1d\x00"sv), error::sharedref_not_complete);
    CHECK_EQ(decode_error("\xd8\x1c\xa1\x61\x61\xd8\x1d\x00"sv), error::sharedref_not_complete);
    CHECK_EQ(decode_error("\x81\xd8\x1c\x81\xd8\x1d\x00"sv), error::sharedref_not_complete);
    auto const v = decoded("\x82\xd8\x1c\x80\xd8\x1d\x00"sv);
    REQUIRE(v.has_value());
    CHECK(*v == A(A(), A()));
}

// Found by the fuzzer: a decode that fails inside a cycle returned no value, and the cycle that it had built held
// itself. The tree owns every item it builds, so a failed decode leaves nothing; LeakSanitizer finds a leak
// otherwise.
TEST_CASE("tag 28/29: a cycle that a failed decode leaves behind is freed")
{
    CHECK_EQ(ref_decode_error("\xd8\x1c\xa5\x61\x61\xd8\x1d\x00"sv), error::too_little_data);
}

// A tree that holds a cycle keeps it while the caller holds the tree, after the lazy is gone.
TEST_CASE("tag 28/29: a held cycle outlives its lazy")
{
    tree const a = decoded_ref("\xd8\x1c\x81\xd8\x1d\x00"sv);
    CHECK(same(a, element(a, 0)));
}

// A chain of marks counts as nesting, as a chain of other tags does.
TEST_CASE("tag 28: a chain past the limit")
{
    CHECK_EQ(ref_decode_error(repeat("\xd8\x1c"sv, 129) + '\x00'), error::nesting_depth_exceeded);
    test::limits_guard const depth{{.nesting_depth = 16}};
    CHECK_EQ(ref_decode_error(repeat("\xd8\x1c"sv, 17) + '\x00'), error::nesting_depth_exceeded);
    CHECK(decoded_ref(repeat("\xd8\x1c"sv, 16) + '\x00') != nullptr);
}

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
// mruby copies an unfrozen String key. A first pass counts each node; the second pass writes tag 28 before the
// first place of a node met more than once and tag 29 with its index at every later place, through the public
// encoder. A cycle is met twice, so it gets a mark.
struct shared_writer {
    bool sharing = true;
    std::unordered_map<node const *, std::size_t> seen;
    std::unordered_map<node const *, std::uint64_t> numbers;
    std::uint64_t next = 0;
    int content_reads = 0;

    static node const *identity(handle const &v, bool const key)
    {
        if (std::holds_alternative<std::uint64_t>(v->kind) || (key && std::holds_alternative<std::string>(v->kind)))
            return nullptr;
        return v.get();
    }

    void count(handle const &v, bool const key)
    {
        if (node const *const id = identity(v, key); id != nullptr && ++seen[id] > 1)
            return;
        if (auto const *a = std::get_if<std::vector<handle>>(&v->kind)) {
            for (handle const &e : *a)
                count(e, false);
        } else if (auto const *m = std::get_if<std::vector<std::pair<handle, handle>>>(&v->kind)) {
            for (auto const &[k, x] : *m) {
                count(k, true);
                count(x, false);
            }
        } else if (auto const *o = std::get_if<object>(&v->kind)) {
            ++content_reads;
            count(o->content, false);
        }
    }

    template <class Target>
    std::expected<void, error> write(cbor::encoder<Target> &e, handle const &v, bool const key)
    {
        node const *const id = sharing ? identity(v, key) : nullptr;
        if (id != nullptr) {
            if (auto const n = numbers.find(id); n != numbers.end()) {
                return e.sharedref(n->second);
            }
            if (seen[id] > 1) {
                if (auto const r = e.shareable(); !r)
                    return r;
                numbers.emplace(id, next++);
            }
        }
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

std::string encoded_shared(handle const &v, bool const sharing = true)
{
    shared_writer w;
    w.sharing = sharing;
    w.count(v, false);
    std::string out;
    cbor::encoder e(out);
    REQUIRE(w.write(e, v, false).has_value());
    REQUIRE(e.finish().has_value());
    return out;
}

tree round_trip(handle const &v)
{
    return decoded_ref(encoded_shared(v));
}

} // namespace shared_test

// Ported from test.rb: 'tag 28/29: repeated value → identity preserved (eager)'.
TEST_CASE("tag 28/29: a repeated value keeps its identity")
{
    auto const a = arr({u(1), u(2)});
    CHECK_EQ(encoded_shared(arr({a, a})), "\x82\xd8\x1c\x82"s + test::native_unsigned(1) + test::native_unsigned(2) + "\xd8\x1d\x00"s);
    auto const r = round_trip(arr({a, a}));
    CHECK(same(element(r, 0), element(r, 1)));
}

// Ported from test.rb: 'tag 28/29: three-way sharing preserves identity (eager)'.
TEST_CASE("tag 28/29: three-way sharing")
{
    auto const v = arr({u(1), u(2), u(3)});
    auto const r = round_trip(obj({{s("a"), v}, {s("b"), v}, {s("c"), v}}));
    CHECK(same(at(r, "a"), at(r, "b")));
    CHECK(same(at(r, "b"), at(r, "c")));
}

// Ported from test.rb: 'tag 28/29: shared hash / shared string roundtrip'.
TEST_CASE("tag 28/29: shared map and shared string")
{
    auto const h = obj({{s("k"), s("v")}, {s("n"), u(42)}});
    auto const rh = round_trip(arr({h, h, h}));
    CHECK(same(element(rh, 0), element(rh, 1)));
    CHECK(same(element(rh, 1), element(rh, 2)));
    auto const str = s("shared_string");
    auto const rs = round_trip(arr({str, str, obj({{s("key"), str}})}));
    CHECK(same(element(rs, 0), element(rs, 1)));
    CHECK(same(element(rs, 1), at(element(rs, 2), "key")));
}

// Ported from test.rb: 'tag 28/29: cyclic array and cyclic hash (eager)'.
TEST_CASE("tag 28/29: a cyclic array and a cyclic map")
{
    auto const a = arr({});
    std::get<std::vector<handle>>(a->kind).push_back(a);
    CHECK_EQ(encoded_shared(a), "\xd8\x1c\x81\xd8\x1d\x00"sv);
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
    auto const r = round_trip(root);
    CHECK(same(at(at(at(at(at(r, "a"), "b"), "c"), "d"), "e"), at(r, "x")));
}

// Ported from test.rb: 'tag 28/29: diamond pattern preserves identity across all paths'.
TEST_CASE("tag 28/29: a diamond")
{
    auto const bottom = obj({{s("value"), u(99)}});
    auto const root = obj({{s("left"), obj({{s("child"), bottom}})},
                           {s("right"), obj({{s("child"), bottom}})},
                           {s("direct"), bottom}});
    auto const r = round_trip(root);
    CHECK(same(at(at(r, "left"), "child"), at(at(r, "right"), "child")));
    CHECK(same(at(at(r, "left"), "child"), at(r, "direct")));
}

// Ported from test.rb: 'tag 28/29: nested sharing — outer and inner both share'.
TEST_CASE("tag 28/29: outer and inner both share")
{
    auto const inner = arr({u(1), u(2), u(3)});
    auto const outer = arr({inner, inner});
    auto const r = round_trip(arr({outer, outer}));
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
                                   {s("nested"), obj({{s("a"), a}, {s("b"), b}, {s("c"), c}})}}));
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
    auto const r = round_trip(obj({{s("v1"), a}, {s("v2"), a}, {s("as_key_map"), obj({{a, s("payload")}})}}));
    CHECK(same(at(r, "v1"), at(r, "v2")));
    auto const key = target(children(at(r, "as_key_map"))[0]);
    CHECK(same(key, at(r, "v1")));
}

// Ported from test.rb: 'no sharedref flag: values do not share, cycles hit depth limit'. An application that
// writes no marks writes a shared value each time, and the decoded values are distinct.
TEST_CASE("without marks: values are written each time")
{
    auto const shared = arr({u(1), u(2), u(3)});
    auto const r = decoded_ref(encoded_shared(obj({{s("a"), shared}, {s("b"), shared}}), false));
    CHECK_FALSE(same(at(r, "a"), at(r, "b")));
}

// Ported from test.rb: 'registered tag + sharedref: same instance in array → identity preserved'. The object is
// its own tag over its content; the writer reads the content once, though it meets the object three times.
TEST_CASE("application tag: one object three times is one mark and two references")
{
    auto const p = object_of(5000, arr({u(3), u(7)}));
    shared_writer w;
    w.count(arr({p, p, p}), false);
    CHECK_EQ(w.content_reads, 1);
    CHECK_EQ(encoded_shared(arr({p, p, p})), "\x83\xd8\x1c\xd9\x13\x88\x82"s + test::native_unsigned(3) + test::native_unsigned(7) + "\xd8\x1d\x00\xd8\x1d\x00"s);
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
    std::string const doc = encoded_shared(arr({a, a}));
    auto const r = cbor::lazy::copy(doc)->decode();
    REQUIRE(r.has_value());
    CHECK(same(element(*r, 0), element(*r, 1)));
}

// Ported from test.rb: 'tag 28: lazy path through Tag 28 without prior registration'. The reference
// names a mark before the target, so the core finds it and decodes it on demand.
TEST_CASE("lazy: a reference to a mark before the target")
{
    std::string const doc = "\xa2\x65outer\xd8\x1c\x82\x01\x02\x63ref\xd8\x1d\x00"s;
    auto const ref = (*cbor::lazy::copy(doc)).at("ref");
    REQUIRE(ref.has_value());
    auto const r = ref->decode();
    REQUIRE(r.has_value());
    CHECK_EQ(argument_of(*element(*r, 1)), 2u);
    auto const inside = ref->at(1);
    REQUIRE(inside.has_value());
    auto const two = inside->decode();
    REQUIRE(two.has_value());
    CHECK_EQ(argument_of(*target(*two)), 2u);
}

// Ported from test.rb: 'tag 28/29: cyclic array materializes via lazy'.
TEST_CASE("lazy: a cyclic array")
{
    std::string const doc = "\xd8\x1c\x81\xd8\x1d\x00"s;
    auto const r = cbor::lazy::copy(doc)->decode();
    REQUIRE(r.has_value());
    CHECK(same(*r, element(*r, 0)));
}

// Ported from test.rb: 'path + sharedref: wildcard iterates over Tag 29 target' and 'path + sharedref:
// wildcard on shared leaf + two wildcards over shared'.
TEST_CASE("path: a wildcard over a shared array")
{
    auto const users = arr({obj({{s("name"), s("alice")}}), obj({{s("name"), s("bob")}})});
    std::string const doc = encoded_shared(
        obj({{s("primary"), obj({{s("users"), users}})}, {s("backup"), obj({{s("users"), users}})}}));
    auto const r = test::query("$.backup.users[*].name", *cbor::lazy::copy(doc));
    REQUIRE(r.has_value());
    CHECK(*r == A("alice"s, "bob"s));
    auto const shared_leaf = arr({u(1), u(2), u(3)});
    std::string const leaf = encoded_shared(obj({{s("a"), shared_leaf}, {s("b"), shared_leaf}}));
    for (std::string_view const p : {"$.a[*]"sv, "$.b[*]"sv}) {
        auto const v = test::query(p, *cbor::lazy::copy(leaf));
        REQUIRE(v.has_value());
        CHECK(*v == A(1, 2, 3));
    }
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
    std::string const doc = encoded_shared(obj({{s("a"), v}, {s("b"), v}}));
    auto const r = cbor::lazy::copy(doc)->decode();
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
    auto const r = round_trip(arr({p, p, p}));
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
    CHECK_FALSE(same(element(r, 0), element(r, 1)));
}

// Ported from test.rb: 'registered tag + sharedref: non-immediate ivar (String) shares correctly'. The
// string inside the content gets its own mark, so the mark of the object must be placed before it.
TEST_CASE("application tag: an object whose content holds a string keeps its identity")
{
    auto const cfg = object_of(5001, arr({u(30), s("default")}));
    auto const r = round_trip(obj({{s("p"), cfg}, {s("b"), cfg}, {s("f"), cfg}}));
    CHECK(same(at(r, "p"), at(r, "b")));
    CHECK(same(at(r, "b"), at(r, "f")));
}

// Ported from test.rb: 'registered tag + sharedref: identity preserved through lazy.value'.
TEST_CASE("lazy: a registered object keeps its identity")
{
    auto const l = object_of(5003, arr({u(99)}));
    std::string const doc = encoded_shared(obj({{s("a"), l}, {s("b"), l}}));
    auto const r = cbor::lazy::copy(doc)->decode();
    REQUIRE(r.has_value());
    CHECK(same(at(*r, "a"), at(*r, "b")));
}

// Ported from test.rb: 'path + sharedref: wildcard on shared leaf + two wildcards over shared'. The
// second half: two wildcards, and the outer one goes through a reference. RFC 9535 gives one flat
// nodelist, as path.cpp expects, where mruby-cbor gave one array for each team.
TEST_CASE("path: two wildcards over a shared array")
{
    auto const teams = arr({obj({{s("members"), arr({obj({{s("n"), s("a")}}), obj({{s("n"), s("b")}})})}}),
                            obj({{s("members"), arr({obj({{s("n"), s("c")}})})}})});
    std::string const doc =
        encoded_shared(obj({{s("p"), obj({{s("teams"), teams}})}, {s("b"), obj({{s("teams"), teams}})}}));
    auto const r = test::query("$.b.teams[*].members[*].n", *cbor::lazy::copy(doc));
    REQUIRE(r.has_value());
    CHECK(*r == A("a"s, "b"s, "c"s));
}

// Ported from what.rb: 'regression #3'. A lazy read of the first path must not change what a later
// decode of the second path gives: the plain array, with its values.
TEST_CASE("lazy: a reference decodes to the plain value after a read of its mark")
{
    auto const shared = arr({u(10), u(20), u(30)});
    std::string const doc = encoded_shared(
        obj({{s("path_a"), obj({{s("ref"), shared}})}, {s("path_b"), obj({{s("ref"), shared}})}}));
    auto const root = *cbor::lazy::copy(doc);
    REQUIRE(root.at("path_a").and_then([](cbor::lazy const &a) { return a.at("ref"); }).has_value());
    auto const ref = root.at("path_b").and_then([](cbor::lazy const &b) { return b.at("ref"); });
    REQUIRE(ref.has_value());
    auto const r = ref->decode();
    REQUIRE(r.has_value());
    REQUIRE_EQ(target(*r)->type, cbor::type::array);
    CHECK_EQ(children(*r).size(), 3u);
    CHECK_EQ(argument_of(*element(*r, 0)), 10u);
    CHECK_EQ(argument_of(*element(*r, 1)), 20u);
    CHECK_EQ(argument_of(*element(*r, 2)), 30u);
}


namespace shared_test
{

std::string unsigned_head(std::uint64_t const major, std::uint64_t const n)
{
    std::string h;
    if (n < 24) {
        h += static_cast<char>(major << 5 | n);
    } else if (n < 256) {
        h += static_cast<char>(major << 5 | 24);
        h += static_cast<char>(n);
    } else {
        h += static_cast<char>(major << 5 | 25);
        h += static_cast<char>(n >> 8);
        h += static_cast<char>(n & 0xff);
    }
    return h;
}

} // namespace shared_test

// The fuzz target path found this: [28([[...[0]...]]), [[...[29(0)]...]]] with h arrays in the mark and r
// arrays around the reference. The decode of the whole item needs a depth of h + 2 and r + 2 only. A lazy
// that starts at the second element meets the reference before the mark is built. The mark is then built
// at the depth of its own position, as the decode of the whole item builds it, and not below the
// reference, so at_path and decode accept every item that the decode of the whole item accepts.
TEST_CASE("tag 29: a mark before the start of a lazy is built at the depth of its own position")
{
    test::limits_guard const depth{{.nesting_depth = 16}};
    for (std::size_t h = 0; h <= 15; ++h) {
        for (std::size_t r = 0; r <= 15; ++r) {
            CAPTURE(h);
            CAPTURE(r);
            std::string const doc = "\x82\xd8\x1c"s + repeat("\x81"sv, h) + '\x00' + repeat("\x81"sv, r) + "\xd8\x1d\x00"s;
            std::string const target = repeat("\x81"sv, h) + '\x00';
            auto const whole = test::value_of(*cbor::lazy::copy(doc));
            auto const found = test::at_path("$[1]", *cbor::lazy::copy(doc));
            CHECK_EQ(found.has_value(), whole.has_value());
            if (!found)
                continue;
            auto const second = cbor::lazy::copy(doc)->at(1);
            REQUIRE(second.has_value());
            auto const lazy = test::value_of(*second);
            REQUIRE(lazy.has_value());
            auto const expected = [&] {
                test::limits_guard const wide{{.nesting_depth = 32}};
                return test::value_of(*cbor::lazy::copy(repeat("\x81"sv, r) + target));
            }();
            REQUIRE(expected.has_value());
            CHECK_EQ(*found, *expected);
            CHECK_EQ(*lazy, *expected);
        }
    }
}

// [28([29(0)]), 29(0)]: the array holds itself. A lazy at the second element builds the mark at its own
// depth. The reference inside the mark meets the mark while it is built and gives the same array.
TEST_CASE("tag 29: a self-referencing array before the start of a lazy")
{
    std::string const doc = "\x82\xd8\x1c\x81\xd8\x1d\x00\xd8\x1d\x00"s;
    auto const second = cbor::lazy::copy(doc)->at(1);
    REQUIRE(second.has_value());
    auto const v = second->decode();
    REQUIRE(v.has_value());
    CHECK(same(element(*v, 0), *v));
    CHECK_EQ(test::value_of(*second).error(), error::sharedref_not_complete);
}

// [28([0]), 28([29(0)]), ..., 28([29(n - 2)]), 29(n - 1)]: each mark names the one before it. A lazy at the
// last element builds every mark that is not built and stands before the target, in the order of the
// marks. Each mark is built at the depth of its own position, and each reference inside it finds its mark
// built. So no mark is built below another mark, and the stack holds the frames down to the reference
// plus the frames of one mark, whatever n is; a chain built mark below mark at n = 300 passes the nesting depth.
TEST_CASE("tag 29: a chain of marks before the start of a lazy keeps the stack bounded")
{
    for (std::size_t const n : {1uz, 2uz, 17uz, 300uz}) {
        CAPTURE(n);
        std::string doc = unsigned_head(4, n + 1) + "\xd8\x1c\x81\x00"s;
        for (std::size_t i = 1; i < n; ++i)
            doc += "\xd8\x1c\x81\xd8\x1d"s + unsigned_head(0, i - 1);
        doc += "\xd8\x1d"s + unsigned_head(0, n - 1);
        auto const last = cbor::lazy::copy(doc)->at(n);
        REQUIRE(last.has_value());
        auto const v = last->decode();
        CAPTURE(v.has_value() ? error{} : v.error());
        REQUIRE(v.has_value());
        place at_mark = target(*v);
        for (std::size_t i = 1; i < n; ++i)
            at_mark = element(at_mark, 0);
        CHECK_EQ(argument_of(*element(at_mark, 0)), 0u);
        CHECK(test::at_path("$[" + std::to_string(n) + "]", *cbor::lazy::copy(doc)).has_value());
    }
}

// The bytes come from the value-sharing specification: tag 28 is d8 1c before the value, and tag 29 is d8 1d before
// the index as an unsigned integer in its shortest head. An index of 24 or more takes a longer head.
TEST_CASE("encoder: shareable and sharedref write the bytes of the specification")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.array(4).has_value());
    REQUIRE(e.shareable().has_value());
    REQUIRE(e.text_string("a").has_value());
    REQUIRE(e.sharedref(0).has_value());
    REQUIRE(e.sharedref(23).has_value());
    REQUIRE(e.sharedref(24).has_value());
    REQUIRE(e.finish().has_value());
    CHECK_EQ(out, "\x84\xd8\x1c\x61" "a" "\xd8\x1d\x00\xd8\x1d\x17\xd8\x1d\x18\x18"s);
    std::string const larger = [] {
        std::string o;
        cbor::encoder f(o);
        REQUIRE(f.sharedref(256).has_value());
        REQUIRE(f.finish().has_value());
        return o;
    }();
    CHECK_EQ(larger, "\xd8\x1d\x19\x01\x00"s);
}

// The contract of every call: an error of the target is a value, and a call after it throws.
TEST_CASE("encoder: sharedref after an error throws")
{
    std::array<char, 3> fixed{};
    cbor::encoder e{std::span<char>(fixed)};
    REQUIRE(e.shareable().has_value());
    CHECK_EQ(e.sharedref(0).error(), error::no_buffer_space);
    CHECK_THROWS_AS((void)e.shareable(), std::logic_error);
}

// The encoder writes 28(["a", 29(0)]); decode gives the reference as the marked array itself.
TEST_CASE("encoder: shareable and sharedref keep the identity through decode")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.array(2).has_value());
    REQUIRE(e.shareable().has_value());
    REQUIRE(e.array(1).has_value());
    REQUIRE(e.text_string("a").has_value());
    REQUIRE(e.sharedref(0).has_value());
    REQUIRE(e.finish().has_value());
    auto const r = decoded_ref(out);
    CHECK(same(element(r, 0), element(r, 1)));
}

// The owner allows a cycle through a tag that is not an array or a map. The encoder writes 28(6(29(0))), and decode
// gives a tree where the content of tag 6 is the marked tag itself.
TEST_CASE("encoder: a cycle through tag 6 decodes into a tree with a cycle")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.shareable().has_value());
    REQUIRE(e.tag(6).has_value());
    REQUIRE(e.sharedref(0).has_value());
    REQUIRE(e.finish().has_value());
    CHECK_EQ(out, "\xd8\x1c\xc6\xd8\x1d\x00"s);
    auto const r = decoded_ref(out);
    CHECK_EQ(argument_of(*target(r)), 6u);
    CHECK(same(content(r), r));
}
