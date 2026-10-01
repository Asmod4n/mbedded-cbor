#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
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

struct node {
    std::variant<std::uint64_t, std::string, std::vector<handle>, std::vector<std::pair<handle, handle>>>
        kind;
};

struct ref_host {
    using value = handle;
};

inline handle tag_invoke(cbor::unsigned_integer_decode_t, ref_host &, std::uint64_t a)
{
    return std::make_shared<node>(node{a});
}

inline handle tag_invoke(cbor::negative_integer_decode_t, ref_host &, std::uint64_t a)
{
    return std::make_shared<node>(node{a});
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

inline handle tag_invoke(cbor::tag_decode_t, ref_host &, std::uint64_t, handle content)
{
    return content;
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
