#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "host.hpp"

using namespace std::string_view_literals;
using cbor::error;

namespace shared_test
{

// A value is a handle, as mrb_value is: two handles to one node are the same object.
struct node;
using handle = std::shared_ptr<node>;

// A registered object of the host: its tag number and the value that before_encode gives for it.
struct object {
    std::uint64_t tag;
    handle content;
};

struct node {
    std::variant<std::uint64_t, std::string, std::vector<handle>, std::vector<std::pair<handle, handle>>,
                 object>
        kind;
};

struct ref_host {
    using value = handle;
    using identity = node const *;
    int before_encode_calls = 0;
    int after_decode_calls = 0;
    handle replacement;
};

inline handle tag_invoke(cbor::unsigned_integer_decode_t, ref_host &, std::uint64_t a)
{
    return std::make_shared<node>(node{a});
}

inline handle tag_invoke(cbor::negative_integer_decode_t, ref_host &, std::uint64_t a)
{
    return std::make_shared<node>(node{a});
}

inline handle tag_invoke(cbor::unsigned_bignum_decode_t, ref_host &, std::string_view m)
{
    return std::make_shared<node>(node{std::string(m)});
}

inline handle tag_invoke(cbor::negative_bignum_decode_t, ref_host &, std::string_view m)
{
    return std::make_shared<node>(node{std::string(m)});
}

inline handle tag_invoke(cbor::byte_string_decode_t, ref_host &, std::string_view b)
{
    return std::make_shared<node>(node{std::string(b)});
}

inline handle tag_invoke(cbor::text_string_decode_t, ref_host &, std::string_view t)
{
    return std::make_shared<node>(node{std::string(t)});
}

inline handle tag_invoke(cbor::float_decode_t, ref_host &, double)
{
    return std::make_shared<node>(node{std::uint64_t{0}});
}

inline handle tag_invoke(cbor::simple_value_decode_t, ref_host &, std::uint8_t s)
{
    return std::make_shared<node>(node{std::uint64_t{s}});
}

inline handle tag_invoke(cbor::array_decode_t, ref_host &)
{
    return std::make_shared<node>(node{std::vector<handle>{}});
}

// The node changes and the same handle comes back, so a reference into a container under
// construction stays valid.
inline handle tag_invoke(cbor::array_append_t, ref_host &, handle a, handle e)
{
    std::get<std::vector<handle>>(a->kind).push_back(std::move(e));
    return a;
}

inline handle tag_invoke(cbor::map_decode_t, ref_host &)
{
    return std::make_shared<node>(node{std::vector<std::pair<handle, handle>>{}});
}

inline handle tag_invoke(cbor::map_insert_t, ref_host &, handle m, handle k, handle v)
{
    std::get<std::vector<std::pair<handle, handle>>>(m->kind).emplace_back(std::move(k), std::move(v));
    return m;
}

// Tag 5000 is registered in this host: tag_begin makes the empty object before its content.
inline std::optional<handle> tag_invoke(cbor::tag_begin_t, ref_host &, std::uint64_t tag)
{
    if (tag != 5000)
        return std::nullopt;
    return std::make_shared<node>(node{object{5000, nullptr}});
}

inline handle tag_invoke(cbor::registered_decode_t, ref_host &, handle o, handle content)
{
    std::get<object>(o->kind).content = std::move(content);
    return o;
}

// The hook returns the replacement when the test set one, else the object itself.
inline handle tag_invoke(cbor::after_decode_t, ref_host &host, handle o)
{
    ++host.after_decode_calls;
    return host.replacement ? host.replacement : o;
}

inline handle tag_invoke(cbor::tag_decode_t, ref_host &, std::uint64_t, handle content)
{
    return content;
}

// The answers of this host to the questions of the encoder. A string key has no identity, as mruby
// copies an unfrozen String key; every other node is its own identity, an integer has none.
inline cbor::kind tag_invoke(cbor::kind_of_t, ref_host &, handle const &v)
{
    if (std::holds_alternative<std::uint64_t>(v->kind))
        return cbor::kind::unsigned_integer;
    if (std::holds_alternative<std::string>(v->kind))
        return cbor::kind::text_string;
    if (std::holds_alternative<std::vector<handle>>(v->kind))
        return cbor::kind::array;
    if (std::holds_alternative<object>(v->kind))
        return cbor::kind::registered;
    return cbor::kind::map;
}

inline std::uint64_t tag_invoke(cbor::registered_tag_t, ref_host &, handle const &v)
{
    return std::get<object>(v->kind).tag;
}

inline handle tag_invoke(cbor::before_encode_t, ref_host &host, handle const &v)
{
    ++host.before_encode_calls;
    return std::get<object>(v->kind).content;
}

inline std::uint64_t tag_invoke(cbor::unsigned_of_t, ref_host &, handle const &v)
{
    return std::get<std::uint64_t>(v->kind);
}

inline std::string_view tag_invoke(cbor::text_of_t, ref_host &, handle const &v)
{
    return std::get<std::string>(v->kind);
}

inline std::uint64_t tag_invoke(cbor::array_size_t, ref_host &, handle const &v)
{
    return std::get<std::vector<handle>>(v->kind).size();
}

inline handle const &tag_invoke(cbor::array_at_t, ref_host &, handle const &v, std::uint64_t i)
{
    return std::get<std::vector<handle>>(v->kind).at(i);
}

inline std::uint64_t tag_invoke(cbor::map_size_t, ref_host &, handle const &v)
{
    return std::get<std::vector<std::pair<handle, handle>>>(v->kind).size();
}

template <class F>
inline void tag_invoke(cbor::map_for_each_t, ref_host &, handle const &v, F const &f)
{
    for (auto const &[key, val] : std::get<std::vector<std::pair<handle, handle>>>(v->kind))
        f(key, val);
}

inline std::optional<node const *> tag_invoke(cbor::value_identity_t, ref_host &, handle const &v)
{
    if (std::holds_alternative<std::uint64_t>(v->kind))
        return std::nullopt;
    return v.get();
}

inline std::optional<node const *> tag_invoke(cbor::key_identity_t, ref_host &h, handle const &v)
{
    if (std::holds_alternative<std::string>(v->kind))
        return std::nullopt;
    return tag_invoke(cbor::value_identity, h, v);
}

template <std::size_t DepthMax = 16>
handle decoded_ref(std::string_view wire)
{
    ref_host host;
    auto v = cbor::decode<DepthMax>(host, wire);
    REQUIRE(v.has_value());
    return *v;
}

template <std::size_t DepthMax = 16>
error ref_decode_error(std::string_view wire)
{
    ref_host host;
    auto const v = cbor::decode<DepthMax>(host, wire);
    REQUIRE_FALSE(v.has_value());
    return v.error();
}

handle element(handle const &array, std::size_t const i)
{
    return std::get<std::vector<handle>>(array->kind).at(i);
}

handle map_value(handle const &map, std::size_t const i)
{
    return std::get<std::vector<std::pair<handle, handle>>>(map->kind).at(i).second;
}

bool same(handle const &a, handle const &b)
{
    return a.get() == b.get();
}

} // namespace shared_test

using namespace shared_test;

// Ported from test.rb: 'tag 28/29: scalar shareable (integer) roundtrip via wire'.
TEST_CASE("tag 28/29: scalar shareable")
{
    auto const r = decoded_ref("\x82\xd8\x1c\x18\x2a\xd8\x1d\x00"sv);
    CHECK(same(element(r, 0), element(r, 1)));
    CHECK_EQ(std::get<std::uint64_t>(element(r, 0)->kind), 42);
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
    // The cycles hold their nodes; the tests end them, so the sanitizers see no leak.
    std::get<std::vector<handle>>(a->kind).clear();
    std::get<std::vector<std::pair<handle, handle>>>(m->kind).clear();
}

// Ported from test.rb: 'tag 28/29: mutual recursion — hash↔array cycle'.
TEST_CASE("tag 28/29: mutual recursion between a map and an array")
{
    auto const r = decoded_ref("\xd8\x1c\xa1\x64list\x82\xd8\x1d\x00\xd8\x1d\x00"sv);
    CHECK(same(r, element(map_value(r, 0), 0)));
    CHECK(same(r, element(map_value(r, 0), 1)));
    std::get<std::vector<std::pair<handle, handle>>>(r->kind).clear();
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

// Only an array or a map exists before its content; any other value does not.
TEST_CASE("tag 29: a reference to a mark that is not complete")
{
    CHECK_EQ(ref_decode_error("\xd8\x1c\xc1\xd8\x1d\x00"sv), error::sharedref_not_complete);
}

// A chain of marks counts as nesting, as a chain of other tags does.
TEST_CASE("tag 28: a chain past the limit")
{
    CHECK_EQ(ref_decode_error(repeat("\xd8\x1c"sv, 17) + '\x00'), error::nesting_depth_exceeded);
}

namespace shared_test
{

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

std::string encoded_shared(handle const &v)
{
    ref_host host;
    string_writer w;
    REQUIRE(cbor::encode<16, cbor::sharedrefs::on>(host, w, v).has_value());
    return w.bytes;
}

handle round_trip(handle const &v)
{
    return decoded_ref(encoded_shared(v));
}

handle at(handle const &map, std::string_view const key)
{
    for (auto const &[k, v] : std::get<std::vector<std::pair<handle, handle>>>(map->kind))
        if (std::holds_alternative<std::string>(k->kind) && std::get<std::string>(k->kind) == key)
            return v;
    FAIL("no such key");
    return nullptr;
}

} // namespace shared_test

// Ported from test.rb: 'tag 28/29: repeated value → identity preserved (eager)'.
TEST_CASE("tag 28/29: a repeated value keeps its identity")
{
    auto const a = arr({u(1), u(2)});
    CHECK_EQ(encoded_shared(arr({a, a})), "\x82\xd8\x1c\x82\x01\x02\xd8\x1d\x00"sv);
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
    for (handle const &x : {a, ra})
        std::get<std::vector<handle>>(x->kind).clear();
    for (handle const &x : {h, rh})
        std::get<std::vector<std::pair<handle, handle>>>(x->kind).clear();
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
    std::get<std::vector<handle>>(at(r, "list")->kind).clear();
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
    std::get<std::vector<std::pair<handle, handle>>>(at(r, "child")->kind).clear();
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

// Ported from test.rb: 'tag 28/29: hash keys do not participate in sharing'. The binding decides by
// key_identity; this host gives a string key no identity, as mruby copies an unfrozen String key.
TEST_CASE("tag 28/29: a string key without identity is written each time")
{
    auto const k = s("repeated_key");
    CHECK_EQ(encoded_shared(arr({obj({{k, u(1)}}), obj({{k, u(2)}})})),
             "\x82\xa1\x6crepeated_key\x01\xa1\x6crepeated_key\x02"sv);
}

// Replaces test.rb: 'tag 28/29: same object as value AND key — only values share'. mruby keeps an
// Array key as the same object (measured with mruby 14fc2ef), so an array key with identity shares.
TEST_CASE("tag 28/29: an array key with identity shares with the values")
{
    auto const a = arr({u(1), u(2), u(3)});
    auto const r = round_trip(obj({{s("v1"), a}, {s("v2"), a}, {s("as_key_map"), obj({{a, s("payload")}})}}));
    CHECK(same(at(r, "v1"), at(r, "v2")));
    auto const key = std::get<std::vector<std::pair<handle, handle>>>(at(r, "as_key_map")->kind).at(0).first;
    CHECK(same(key, at(r, "v1")));
}

// Ported from test.rb: 'no sharedref flag: values do not share, cycles hit depth limit'.
TEST_CASE("sharedrefs::off: values are written each time, a cycle hits the depth limit")
{
    auto const shared = arr({u(1), u(2), u(3)});
    ref_host host;
    string_writer w;
    REQUIRE(cbor::encode<16>(host, w, obj({{s("a"), shared}, {s("b"), shared}})).has_value());
    auto const r = decoded_ref(w.bytes);
    CHECK_FALSE(same(at(r, "a"), at(r, "b")));
    auto const a = arr({});
    std::get<std::vector<handle>>(a->kind).push_back(a);
    string_writer w2;
    auto const e = cbor::encode<16>(host, w2, a);
    REQUIRE_FALSE(e.has_value());
    CHECK((e.error() == error::nesting_depth_exceeded));
    std::get<std::vector<handle>>(a->kind).clear();
}

// Ported from test.rb: 'registered tag + sharedref: same instance in array → identity preserved' and
// the cache of walk_count: the hook of a registered object runs once, though the encoder makes two
// passes and meets the object three times.
TEST_CASE("registered tag: before_encode runs once per object with two passes")
{
    auto const point = std::make_shared<node>(node{object{5000, arr({u(3), u(7)})}});
    ref_host host;
    string_writer w;
    REQUIRE(cbor::encode<16, cbor::sharedrefs::on>(host, w, arr({point, point, point})).has_value());
    CHECK_EQ(host.before_encode_calls, 1);
    CHECK_EQ(w.bytes, "\x83\xd8\x1c\xd9\x13\x88\x82\x03\x07\xd8\x1d\x00\xd8\x1d\x00"sv);
}

// Ported from test.rb: 'registered tag + sharedref: distinct instances with equal fields do NOT share'.
TEST_CASE("registered tag: distinct objects with equal content do not share")
{
    auto const p1 = std::make_shared<node>(node{object{5000, arr({u(1), u(2)})}});
    auto const p2 = std::make_shared<node>(node{object{5000, arr({u(1), u(2)})}});
    ref_host host;
    string_writer w;
    REQUIRE(cbor::encode<16, cbor::sharedrefs::on>(host, w, arr({p1, p2})).has_value());
    CHECK_EQ(host.before_encode_calls, 2);
    CHECK_EQ(w.bytes, "\x82\xd9\x13\x88\x82\x01\x02\xd9\x13\x88\x82\x01\x02"sv);
}

// Ported from test.rb: 'registered tag + sharedref: instance with self-referential field'. The object
// exists before its content, as decode_registered_tag allocates it before the payload.
TEST_CASE("registered tag: a reference inside the content names the object")
{
    ref_host host;
    auto const r = cbor::decode<16>(host, "\xd8\x1c\xd9\x13\x88\x81\xd8\x1d\x00"sv);
    REQUIRE(r.has_value());
    CHECK(same(element(std::get<object>((*r)->kind).content, 0), *r));
    CHECK_EQ(host.after_decode_calls, 1);
    std::get<object>((*r)->kind).content = nullptr;
}

// As decode_tag_sharedrefs in mruby-cbor sets the place to the value after _after_decode: a reference
// after the object names the replacement.
TEST_CASE("registered tag: after_decode sets the place of the mark")
{
    ref_host host;
    host.replacement = u(99);
    auto const r = cbor::decode<16>(host, "\x82\xd8\x1c\xd9\x13\x88\x81\x01\xd8\x1d\x00"sv);
    REQUIRE(r.has_value());
    CHECK(same(element(*r, 0), host.replacement));
    CHECK(same(element(*r, 1), host.replacement));
}

// Ported from test.rb: 'registered tag + sharedref: mutual recursion between two instances'.
TEST_CASE("registered tag: two objects that name each other")
{
    ref_host host;
    // 28 5000([28 5000([29 0])])
    auto const r = cbor::decode<16>(host, "\xd8\x1c\xd9\x13\x88\x81\xd8\x1c\xd9\x13\x88\x81\xd8\x1d\x00"sv);
    REQUIRE(r.has_value());
    auto const peer = element(std::get<object>((*r)->kind).content, 0);
    CHECK(same(element(std::get<object>(peer->kind).content, 0), *r));
    std::get<object>(peer->kind).content = nullptr;
}

// A tag without registration takes the plain path: no object first and no hook.
TEST_CASE("registered tag: no hook for a tag without registration")
{
    ref_host host;
    REQUIRE(cbor::decode<16>(host, "\xc1\x01"sv).has_value());
    CHECK_EQ(host.after_decode_calls, 0);
}
