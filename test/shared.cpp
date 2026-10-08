#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "binding.hpp"
#include "ref_binding.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace shared_test
{

template <std::size_t DepthMax = 16>
handle decoded_ref(std::string_view wire)
{
    ref_binding binding;
    auto v = cbor::lazy_decode<DepthMax>(binding, *cbor::decode<DepthMax>(wire));
    REQUIRE(v.has_value());
    return *v;
}

template <std::size_t DepthMax = 16>
error ref_decode_error(std::string_view wire)
{
    ref_binding binding;
    auto const v = cbor::lazy_decode<DepthMax>(binding, *cbor::decode<DepthMax>(wire));
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

// Only an array or a map exists before its content; any other value does not. Tag 6 takes every content, so
// the check of the tag content (RFC 8949 3.4.1, 3.4.2) does not answer first.
TEST_CASE("tag 29: a reference to a mark that is not complete")
{
    CHECK_EQ(ref_decode_error("\xd8\x1c\xc6\xd8\x1d\x00"sv), error::sharedref_not_complete);
}

// value-sharing: a cyclic data structure needs a reference to a value before it is completely decoded. A binding
// that does not answer cyclic_data_structures holds values, not references, so a cycle would come back as a cut
// copy; the decoder refuses it instead. Sharing without a cycle stays: the binding gets a copy.
TEST_CASE("tag 28/29: a binding without cyclic data structures refuses a cycle")
{
    CHECK_EQ(decode_error("\xd8\x1c\x81\xd8\x1d\x00"sv), error::sharedref_not_complete);
    CHECK_EQ(decode_error("\xd8\x1c\xa1\x61\x61\xd8\x1d\x00"sv), error::sharedref_not_complete);
    CHECK_EQ(decode_error("\x81\xd8\x1c\x81\xd8\x1d\x00"sv), error::sharedref_not_complete);
    auto const v = decoded("\x82\xd8\x1c\x80\xd8\x1d\x00"sv);
    REQUIRE(v.has_value());
    CHECK(*v == A(A(), A()));
}

// Found by the fuzzer: a decode that fails inside a cycle returns no value, so nobody could reach the cycle
// to end it, and its shared pointers held each other forever. The binding ends every node that only other
// nodes hold. The map is complete and its keys are distinct, so the binding makes the map and the cycle
// before the content of tag 2, an integer, fails (RFC 8949 3.4.3). The fuzzer found a truncated map, which
// decode now refuses before the binding makes anything.
TEST_CASE("tag 28/29: a cycle that a failed decode leaves behind is freed with the binding")
{
    std::vector<std::weak_ptr<node>> const made = [] {
        ref_binding binding;
        CHECK_FALSE(cbor::lazy_decode<16>(
                        binding, *cbor::decode<16>("\xd8\x1c\xa2\x61\x61\xd8\x1d\x00\x61\x62\xc2\x00"sv))
                        .has_value());
        return binding.made;
    }();
    REQUIRE_FALSE(made.empty());
    for (auto const &w : made)
        CHECK(w.expired());
}

// A value the caller still holds keeps its whole graph when the binding ends, cycle included.
TEST_CASE("tag 28/29: a held cycle outlives its binding")
{
    handle const a = decoded_ref("\xd8\x1c\x81\xd8\x1d\x00"sv);
    CHECK(same(a, element(a, 0)));
    std::get<std::vector<handle>>(a->kind).clear();
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
    ref_binding binding;
    string_writer w;
    REQUIRE(cbor::encode<16, cbor::sharedrefs::on>(binding, w, v).has_value());
    return w.encoded;
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

// A cycle can come from the wire, through a binding that answers cyclic_data_structures. Encode without
// sharing then gives an error as a value and throws nothing, so bytes of an attacker cannot raise an
// exception. A value that is only deep stays nesting_depth_exceeded.
TEST_CASE("tag 28/29: encode without sharing refuses a cycle as a value")
{
    auto const a = decoded_ref("\xd8\x1c\x81\xd8\x1d\x00"sv);
    ref_binding binding;
    string_writer w;
    auto const r = cbor::encode<16>(binding, w, a);
    REQUIRE_FALSE(r.has_value());
    CHECK((r.error() == cbor::error{error::cyclic_data_structure}));
    std::get<std::vector<handle>>(a->kind).clear();

    handle deep = u(1);
    for (int i = 0; i < 20; ++i)
        deep = arr({deep});
    string_writer d;
    auto const too_deep = cbor::encode<16>(binding, d, deep);
    REQUIRE_FALSE(too_deep.has_value());
    CHECK((too_deep.error() == cbor::error{error::nesting_depth_exceeded}));
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
// key_identity; this binding gives a string key no identity, as mruby copies an unfrozen String key.
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

// Ported from test.rb: 'no sharedref flag: values do not share, cycles hit depth limit'. mruby-cbor raises
// there; here the cycle is an error value, because a cycle can come from bytes on the wire.
TEST_CASE("sharedrefs::off: values are written each time, a cycle is an error")
{
    auto const shared = arr({u(1), u(2), u(3)});
    ref_binding binding;
    string_writer w;
    REQUIRE(cbor::encode<16>(binding, w, obj({{s("a"), shared}, {s("b"), shared}})).has_value());
    auto const r = decoded_ref(w.encoded);
    CHECK_FALSE(same(at(r, "a"), at(r, "b")));
    auto const a = arr({});
    std::get<std::vector<handle>>(a->kind).push_back(a);
    string_writer w2;
    auto const e = cbor::encode<16>(binding, w2, a);
    REQUIRE_FALSE(e.has_value());
    CHECK((e.error() == cbor::error{error::cyclic_data_structure}));
    std::get<std::vector<handle>>(a->kind).clear();
}

// Ported from test.rb: 'registered tag + sharedref: same instance in array → identity preserved' and
// the cache of walk_count: the hook of a registered object runs once, though the encoder makes two
// passes and meets the object three times.
TEST_CASE("registered tag: before_encode runs once per object with two passes")
{
    auto const point = std::make_shared<node>(node{object{5000, arr({u(3), u(7)})}});
    ref_binding binding;
    string_writer w;
    REQUIRE(cbor::encode<16, cbor::sharedrefs::on>(binding, w, arr({point, point, point})).has_value());
    CHECK_EQ(binding.before_encode_calls, 1);
    CHECK_EQ(w.encoded, "\x83\xd8\x1c\xd9\x13\x88\x82\x03\x07\xd8\x1d\x00\xd8\x1d\x00"sv);
}

// Ported from test.rb: 'registered tag + sharedref: distinct instances with equal fields do NOT share'.
TEST_CASE("registered tag: distinct objects with equal content do not share")
{
    auto const p1 = std::make_shared<node>(node{object{5000, arr({u(1), u(2)})}});
    auto const p2 = std::make_shared<node>(node{object{5000, arr({u(1), u(2)})}});
    ref_binding binding;
    string_writer w;
    REQUIRE(cbor::encode<16, cbor::sharedrefs::on>(binding, w, arr({p1, p2})).has_value());
    CHECK_EQ(binding.before_encode_calls, 2);
    CHECK_EQ(w.encoded, "\x82\xd9\x13\x88\x82\x01\x02\xd9\x13\x88\x82\x01\x02"sv);
}

// Ported from test.rb: 'registered tag + sharedref: instance with self-referential field'. The object
// exists before its content, as decode_registered_tag allocates it before the payload.
TEST_CASE("registered tag: a reference inside the content names the object")
{
    ref_binding binding;
    auto const r = cbor::lazy_decode<16>(binding, *cbor::decode<16>("\xd8\x1c\xd9\x13\x88\x81\xd8\x1d\x00"sv));
    REQUIRE(r.has_value());
    CHECK(same(element(std::get<object>((*r)->kind).content, 0), *r));
    CHECK_EQ(binding.after_decode_calls, 1);
    std::get<object>((*r)->kind).content = nullptr;
}

// As decode_tag_sharedrefs in mruby-cbor sets the place to the value after _after_decode: a reference
// after the object names the replacement.
TEST_CASE("registered tag: after_decode sets the place of the mark")
{
    ref_binding binding;
    binding.replacement = u(99);
    auto const r = cbor::lazy_decode<16>(binding, *cbor::decode<16>("\x82\xd8\x1c\xd9\x13\x88\x81\x01\xd8\x1d\x00"sv));
    REQUIRE(r.has_value());
    CHECK(same(element(*r, 0), binding.replacement));
    CHECK(same(element(*r, 1), binding.replacement));
}

// Ported from test.rb: 'registered tag + sharedref: mutual recursion between two instances'.
TEST_CASE("registered tag: two objects that name each other")
{
    ref_binding binding;
    // 28 5000([28 5000([29 0])])
    auto const r = cbor::lazy_decode<16>(binding, *cbor::decode<16>("\xd8\x1c\xd9\x13\x88\x81\xd8\x1c\xd9\x13\x88\x81\xd8\x1d\x00"sv));
    REQUIRE(r.has_value());
    auto const peer = element(std::get<object>((*r)->kind).content, 0);
    CHECK(same(element(std::get<object>(peer->kind).content, 0), *r));
    std::get<object>(peer->kind).content = nullptr;
}

// A tag without registration takes the plain path: no object first and no hook.
TEST_CASE("registered tag: no hook for a tag without registration")
{
    ref_binding binding;
    REQUIRE(cbor::lazy_decode<16>(binding, *cbor::decode<16>("\xc1\x01"sv)).has_value());
    CHECK_EQ(binding.after_decode_calls, 0);
}

// Ported from test.rb: 'tag 28/29: shared value preserved via lazy.value'.
TEST_CASE("lazy: a shared value keeps its identity")
{
    auto const a = arr({u(1), u(2)});
    std::string const doc = encoded_shared(arr({a, a}));
    ref_binding binding;
    auto const r = cbor::lazy_decode<16>(binding, *cbor::decode<16>(doc));
    REQUIRE(r.has_value());
    CHECK(same(element(*r, 0), element(*r, 1)));
}

// Ported from test.rb: 'tag 28: lazy path through Tag 28 without prior registration'. The reference
// names a mark before the target, so the core finds it and decodes it on demand.
TEST_CASE("lazy: a reference to a mark before the target")
{
    std::string const doc = "\xa2\x65outer\xd8\x1c\x82\x01\x02\x63ref\xd8\x1d\x00"s;
    auto const ref = (*cbor::decode<16>(doc)).at<16>("ref");
    REQUIRE(ref.has_value());
    ref_binding binding;
    auto const r = cbor::lazy_decode<16>(binding, *ref);
    REQUIRE(r.has_value());
    CHECK_EQ(std::get<std::uint64_t>(element(*r, 1)->kind), 2);
    auto const inside = ref->at<16>(1);
    REQUIRE(inside.has_value());
    auto const two = cbor::lazy_decode<16>(binding, *inside);
    REQUIRE(two.has_value());
    CHECK_EQ(std::get<std::uint64_t>((*two)->kind), 2);
}

// Ported from test.rb: 'tag 28/29: cyclic array materializes via lazy'.
TEST_CASE("lazy: a cyclic array")
{
    std::string const doc = "\xd8\x1c\x81\xd8\x1d\x00"s;
    ref_binding binding;
    auto const r = cbor::lazy_decode<16>(binding, *cbor::decode<16>(doc));
    REQUIRE(r.has_value());
    CHECK(same(*r, element(*r, 0)));
    std::get<std::vector<handle>>((*r)->kind).clear();
}

// Ported from test.rb: 'path + sharedref: wildcard iterates over Tag 29 target' and 'path + sharedref:
// wildcard on shared leaf + two wildcards over shared'.
TEST_CASE("path: a wildcard over a shared array")
{
    auto const users = arr({obj({{s("name"), s("alice")}}), obj({{s("name"), s("bob")}})});
    std::string const doc = encoded_shared(
        obj({{s("primary"), obj({{s("users"), users}})}, {s("backup"), obj({{s("users"), users}})}}));
    ref_binding binding;
    auto const r = cbor::at_path<16>(binding, "$.backup.users[*].name", *cbor::decode<16>(doc));
    REQUIRE(r.has_value());
    CHECK_EQ(std::get<std::string>(element(*r, 0)->kind), "alice");
    CHECK_EQ(std::get<std::string>(element(*r, 1)->kind), "bob");
    auto const shared_leaf = arr({u(1), u(2), u(3)});
    std::string const leaf = encoded_shared(obj({{s("a"), shared_leaf}, {s("b"), shared_leaf}}));
    for (std::string_view const p : {"$.a[*]"sv, "$.b[*]"sv}) {
        auto const v = cbor::at_path<16>(binding, p, *cbor::decode<16>(leaf));
        REQUIRE(v.has_value());
        CHECK_EQ(std::get<std::uint64_t>(element(*v, 2)->kind), 3);
    }
}

// A binding as in mruby gives the content of a registered object by value, as a temporary that only the encoder
// holds. The encoder of one pass writes it before the temporary ends.
TEST_CASE("registered tag: before_encode that returns a new value by value")
{
    struct fresh_binding : ref_binding {
        handle before_encode(handle const &)
        {
            ++before_encode_calls;
            return arr({u(1), arr({u(2), u(3)})});
        }
    };
    auto const point = std::make_shared<node>(node{object{5000, nullptr}});
    fresh_binding binding;
    string_writer w;
    REQUIRE(cbor::encode<16>(binding, w, arr({point, point})).has_value());
    CHECK_EQ(binding.before_encode_calls, 2);
    CHECK_EQ(w.encoded, "\x82\xd9\x13\x88\x82\x01\x82\x02\x03\xd9\x13\x88\x82\x01\x82\x02\x03"sv);
}

// Ported from test.rb: 'tag 28/29: shared value preserved via lazy.value'. The map half: the first
// test above covers only the array half.
TEST_CASE("lazy: a value shared by two map values keeps its identity")
{
    auto const v = arr({u(1), u(2), u(3)});
    std::string const doc = encoded_shared(obj({{s("a"), v}, {s("b"), v}}));
    ref_binding binding;
    auto const r = cbor::lazy_decode<16>(binding, *cbor::decode<16>(doc));
    REQUIRE(r.has_value());
    CHECK(same(at(*r, "a"), at(*r, "b")));
}

// Ported from test.rb: 'tag 28: self-referential Tag 28 inside Tag 28 is safe'. The exact bytes of
// mruby-cbor: two marks on one value and no reference, which must decode without a fault.
TEST_CASE("tag 28: inside tag 28 without a reference")
{
    auto const r = decoded_ref("\xd8\x1c\xd8\x1c\x18\x2a"sv);
    CHECK_EQ(std::get<std::uint64_t>(r->kind), 42);
}

// Ported from test.rb: 'registered tag + sharedref: same instance in array → identity preserved'. The
// test above checks the encoder only; this one checks that the decoder gives one object three times.
TEST_CASE("registered tag: the same object three times keeps its identity")
{
    auto const point = std::make_shared<node>(node{object{5000, arr({u(3), u(7)})}});
    auto const r = round_trip(arr({point, point, point}));
    CHECK(same(element(r, 0), element(r, 1)));
    CHECK(same(element(r, 1), element(r, 2)));
}

// Ported from test.rb: 'registered tag + sharedref: distinct instances with equal fields do NOT share'.
// The test above checks the encoder only; this one checks that the decoder gives two objects.
TEST_CASE("registered tag: distinct objects with equal content decode to two objects")
{
    auto const p1 = std::make_shared<node>(node{object{5000, arr({u(1), u(2)})}});
    auto const p2 = std::make_shared<node>(node{object{5000, arr({u(1), u(2)})}});
    auto const r = round_trip(arr({p1, p2}));
    CHECK_FALSE(same(element(r, 0), element(r, 1)));
}

// Ported from test.rb: 'registered tag + sharedref: non-immediate ivar (String) shares correctly'. The
// string inside the content gets its own mark, so the mark of the object must be placed before it.
TEST_CASE("registered tag: an object whose content holds a string keeps its identity")
{
    auto const cfg = std::make_shared<node>(node{object{5001, arr({u(30), s("default")})}});
    auto const r = round_trip(obj({{s("p"), cfg}, {s("b"), cfg}, {s("f"), cfg}}));
    CHECK(same(at(r, "p"), at(r, "b")));
    CHECK(same(at(r, "b"), at(r, "f")));
}

// Ported from test.rb: 'registered tag + sharedref: identity preserved through lazy.value'.
TEST_CASE("lazy: a registered object keeps its identity")
{
    auto const l = std::make_shared<node>(node{object{5003, arr({u(99)})}});
    std::string const doc = encoded_shared(obj({{s("a"), l}, {s("b"), l}}));
    ref_binding binding;
    auto const r = cbor::lazy_decode<16>(binding, *cbor::decode<16>(doc));
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
    ref_binding binding;
    auto const r = cbor::at_path<16>(binding, "$.b.teams[*].members[*].n", *cbor::decode<16>(doc));
    REQUIRE(r.has_value());
    CHECK_EQ(std::get<std::vector<handle>>((*r)->kind).size(), 3);
    CHECK_EQ(std::get<std::string>(element(*r, 0)->kind), "a");
    CHECK_EQ(std::get<std::string>(element(*r, 1)->kind), "b");
    CHECK_EQ(std::get<std::string>(element(*r, 2)->kind), "c");
}

// Ported from what.rb: 'regression #3'. A lazy read of the first path must not change what a later
// decode of the second path gives: the plain array, with its values.
TEST_CASE("lazy: a reference decodes to the plain value after a read of its mark")
{
    auto const shared = arr({u(10), u(20), u(30)});
    std::string const doc = encoded_shared(
        obj({{s("path_a"), obj({{s("ref"), shared}})}, {s("path_b"), obj({{s("ref"), shared}})}}));
    auto const root = *cbor::decode<16>(doc);
    REQUIRE(root.at<16>("path_a").and_then([](cbor::lazy const &a) { return a.at<16>("ref"); }).has_value());
    auto const ref = root.at<16>("path_b").and_then([](cbor::lazy const &b) { return b.at<16>("ref"); });
    REQUIRE(ref.has_value());
    ref_binding binding;
    auto const r = cbor::lazy_decode<16>(binding, *ref);
    REQUIRE(r.has_value());
    REQUIRE(std::holds_alternative<std::vector<handle>>((*r)->kind));
    CHECK_EQ(std::get<std::vector<handle>>((*r)->kind).size(), 3);
    CHECK_EQ(std::get<std::uint64_t>(element(*r, 0)->kind), 10);
    CHECK_EQ(std::get<std::uint64_t>(element(*r, 1)->kind), 20);
    CHECK_EQ(std::get<std::uint64_t>(element(*r, 2)->kind), 30);
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

struct nesting_binding : ref_binding {
    std::size_t open = 0;
    std::size_t open_max = 0;

    handle array_decode(std::uint64_t const size)
    {
        open_max = std::max(open_max, ++open);
        return ref_binding::array_decode(size);
    }

    handle array_append(handle a, handle e)
    {
        --open;
        return ref_binding::array_append(std::move(a), std::move(e));
    }
};

} // namespace shared_test

// The fuzz target path found this: [28([[...[0]...]]), [[...[29(0)]...]]] with h arrays in the mark and r
// arrays around the reference. The decode of the whole item needs a depth of h + 2 and r + 2 only. A lazy
// that starts at the second element meets the reference before the mark is built. The mark is then built
// at the depth of its own position, as the decode of the whole item builds it, and not below the
// reference, so at_path and lazy_decode accept every item that the decode of the whole item accepts.
TEST_CASE("tag 29: a mark before the start of a lazy is built at the depth of its own position")
{
    for (std::size_t h = 0; h <= 15; ++h) {
        for (std::size_t r = 0; r <= 15; ++r) {
            CAPTURE(h);
            CAPTURE(r);
            std::string const doc = "\x82\xd8\x1c"s + repeat("\x81"sv, h) + '\x00' + repeat("\x81"sv, r) + "\xd8\x1d\x00"s;
            std::string const target = repeat("\x81"sv, h) + '\x00';
            test_binding whole_binding;
            auto const whole = cbor::lazy_decode<16>(whole_binding, *cbor::decode<16>(doc));
            test_binding path_binding;
            auto const found = cbor::at_path<16>(path_binding, "$[1]", *cbor::decode<16>(doc));
            CHECK_EQ(found.has_value(), whole.has_value());
            if (!found)
                continue;
            auto const second = cbor::decode<16>(doc)->at<16>(1);
            REQUIRE(second.has_value());
            test_binding lazy_binding;
            auto const lazy = cbor::lazy_decode<16>(lazy_binding, *second);
            REQUIRE(lazy.has_value());
            test_binding expected_binding;
            auto const expected = cbor::lazy_decode<32>(expected_binding, *cbor::decode<32>(repeat("\x81"sv, r) + target));
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
    auto const second = cbor::decode<16>(doc)->at<16>(1);
    REQUIRE(second.has_value());
    ref_binding binding;
    auto const v = cbor::lazy_decode<16>(binding, *second);
    REQUIRE(v.has_value());
    CHECK(same(element(*v, 0), *v));
    test_binding plain;
    CHECK_EQ(cbor::lazy_decode<16>(plain, *second).error(), error::sharedref_not_complete);
}

// [28([0]), 28([29(0)]), ..., 28([29(n - 2)]), 29(n - 1)]: each mark names the one before it. A lazy at the
// last element builds every mark that is not built and stands before the target, in the order of the
// marks. Each mark is built at the depth of its own position, and each reference inside it finds its mark
// built. So no mark is built below another mark, and the stack holds the frames down to the reference
// plus the frames of one mark: at most 2 * (DepthMax + 1) calls of value_decode, whatever n is. The
// binding counts the arrays that are open at the same time; a chain that is built mark below mark opens
// n of them.
TEST_CASE("tag 29: a chain of marks before the start of a lazy keeps the stack bounded")
{
    for (std::size_t const n : {1uz, 2uz, 17uz, 300uz}) {
        CAPTURE(n);
        std::string doc = unsigned_head(4, n + 1) + "\xd8\x1c\x81\x00"s;
        for (std::size_t i = 1; i < n; ++i)
            doc += "\xd8\x1c\x81\xd8\x1d"s + unsigned_head(0, i - 1);
        doc += "\xd8\x1d"s + unsigned_head(0, n - 1);
        auto const last = cbor::decode<16>(doc)->at<16>(static_cast<std::int64_t>(n));
        REQUIRE(last.has_value());
        nesting_binding binding;
        auto v = cbor::lazy_decode<16>(binding, *last);
        CAPTURE(v.has_value() ? error{} : v.error());
        REQUIRE(v.has_value());
        CHECK_LE(binding.open_max, 2);
        for (std::size_t i = 1; i < n; ++i)
            v = element(*v, 0);
        CHECK_EQ(std::get<std::uint64_t>(element(*v, 0)->kind), 0);
        test_binding path_binding;
        CHECK(cbor::at_path<16>(path_binding, "$[" + std::to_string(n) + "]", *cbor::decode<16>(doc)).has_value());
    }
}
