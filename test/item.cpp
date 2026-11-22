#include "rfc8949.hpp"
#include "small_stack.hpp"
#include <cbor/cbor.hpp>
#include "pieces.hpp"
#include "owned_copy.hpp"
#include <doctest/doctest.h>

#include "item_head.hpp"
#include "reference.hpp"
#include <cbor/structs.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace item_test
{

// RFC 8949 section 3: every data item starts with a head of major type,
// additional information and argument. An integer is its type, its
// additional information and its argument, so get gives an item of
// major type 1 as -1 minus the argument.
TEST_CASE("an integer is the head fields and nothing else")
{
    auto const tree = cbor::decode("\x39\x01\xf3"sv);
    REQUIRE(tree.has_value());
    cbor::item const minus_500 = **tree;
    CHECK(minus_500.type() == cbor::type::negative_integer);
    CHECK(minus_500.additional_information() == 25);
    auto const number = minus_500.get<std::int64_t>();
    CHECK(number.value() == -500);
}

// RFC 8949 3.4: a tag is major type 6 with the tag number as argument.
// The content points to the item of the tag content, so a reader goes
// from the tag to its content by the address.
TEST_CASE("a tag holds its number and points to its content")
{
    auto const tree = cbor::decode("\xc1\x1a\x51\x4b\x67\xb0"sv);
    REQUIRE(tree.has_value());
    cbor::item const time = **tree;
    auto const time_tagged = time.get<std::pair<std::uint64_t, cbor::item>>();
    CHECK(time_tagged.value().first == 1);
    cbor::item const epoch = time_tagged.value().second;
    CHECK(epoch.additional_information() == 26);
    auto const epoch_number = epoch.get<std::uint64_t>();
    CHECK(epoch_number.value() == 1363896240u);
}

// RFC 8949 3.3 Table 3: major type 7 says by its additional information what the item is. A float keeps the width it had
// on the wire in the additional information and in its type. Its value widened to a double, which holds every value of
// the narrower widths.
TEST_CASE("a float keeps its width in the additional information and its value as a double")
{
    auto const tree = cbor::decode("\x83\xf9\x3e\x00\xfa\x47\xc3\x50\x00\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"sv);
    REQUIRE(tree.has_value());
    auto const tree_elements = (*tree)->elements();
    auto const floats = tree_elements.value();
    auto at = floats.begin();
    cbor::item const f16 = *at;
    cbor::item const f32 = *++at;
    cbor::item const f64 = *++at;
    CHECK(f16.additional_information() == 25);
    auto const f16_real = f16.get<double>();
    CHECK_EQ(std::bit_cast<std::uint64_t>(f16_real.value()), 0x3ff8000000000000u);
    CHECK(f32.additional_information() == 26);
    auto const f32_real = f32.get<double>();
    CHECK_EQ(std::bit_cast<std::uint64_t>(f32_real.value()), 0x40f86a0000000000u);
    CHECK(f64.additional_information() == 27);
    auto const real = f64.get<double>();
    CHECK_EQ(std::bit_cast<std::uint64_t>(real.value()), 0x3ff199999999999au);
}

// undefined is a value of its own, apart from null, so that a message
// from JavaScript passes through unchanged.
TEST_CASE("null and undefined are two items")
{
    auto const tree = cbor::decode("\x82\xf6\xf7"sv);
    REQUIRE(tree.has_value());
    auto const tree_elements = (*tree)->elements();
    auto const items = tree_elements.value();
    auto at = items.begin();
    cbor::item const null = *at;
    cbor::item const undefined = *++at;
    CHECK(null.type() != undefined.type());
    CHECK(null.additional_information() != undefined.additional_information());
}

// An item owns no memory: it is a view of one node, as a dom::element of simdjson is, and holds its tree and the index
// of its node. So a copy of an item is a view of the same node, and an item is passed by value in 16 bytes. It is no
// aggregate, because only a decode builds one.
TEST_CASE("an item owns no memory, is a view of its node and takes 16 bytes")
{
    CHECK(std::is_trivially_destructible_v<cbor::item>);
    CHECK(std::is_trivially_copyable_v<cbor::item>);
    CHECK_FALSE(std::is_aggregate_v<cbor::item>);
    CHECK_EQ(sizeof(cbor::item), 16u);
    auto const tree = cbor::decode(cbor::diag2cbor("[1, 2]").value());
    REQUIRE(tree.has_value());
    cbor::item const root = **tree;
    cbor::item const copy = root;
    CHECK(copy == root);
    auto const copy_elements = copy.elements();
    CHECK_EQ(copy_elements->size(), 2u);
    auto const root_elements = root.elements();
    CHECK(copy != root_elements->front());
}

// RFC 8949 3.1: an array holds its elements in order, and its argument is their count. The range of an array gives
// each element once, in the order of the message, and its size is the count of the head.
TEST_CASE("item elements: the elements of an array in order, and their count")
{
    auto const tree = cbor::parse_edn("[7, 8, 9]");
    REQUIRE(tree.has_value());
    auto const elements = (*tree)->elements();
    REQUIRE(elements.has_value());
    CHECK_EQ(elements->size(), 3u);
    std::vector<int> read;
    for (cbor::item const element : *elements) {
        auto const number = element.get<int>();
        read.push_back(number.value());
    }
    CHECK_EQ(read, std::vector<int>{7, 8, 9});
    auto const tree_pairs = (*tree)->pairs();
    CHECK_EQ(tree_pairs.error(), cbor::error::incorrect_type);
}

// RFC 8949 3.1: a map holds pairs of a key and a value, and its argument is the count of pairs. The range of a map
// gives each key with its value, in the order of the message.
TEST_CASE("item pairs: each key with its value in order, and the count of pairs")
{
    auto const tree = cbor::parse_edn(R"({"a": 1, "b": [2, 3]})");
    REQUIRE(tree.has_value());
    auto const entries = (*tree)->pairs();
    REQUIRE(entries.has_value());
    CHECK_EQ(entries->size(), 2u);
    std::vector<std::string_view> keys;
    for (auto const [key, value] : *entries) {
        auto const text = key.get<std::string_view>();
        keys.push_back(text.value());
    }
    CHECK_EQ(keys, std::vector<std::string_view>{"a", "b"});
    auto const second = std::ranges::next(entries->begin());
    auto const second_elements = (*second).second.elements();
    CHECK_EQ(second_elements->size(), 2u);
    auto const tree_elements = (*tree)->elements();
    CHECK_EQ(tree_elements.error(), cbor::error::incorrect_type);
}

// RFC 8949 3.1: a key and a value are each one data item, and an array, a map or a tag holds further items.
// The range must step over all of them, so a value after a wide key, and a key after a wide value, are the
// right nodes. A map with no pairs gives an empty range. A forward range can be passed more than once: two
// copies of the first iterator give the same pairs.
TEST_CASE("item pairs: the range steps over keys and values that hold further items")
{
    auto const tree =
        cbor::parse_edn(R"([{[1, 2]: {"x": 1}, "k": [3], 1([4, 5]): 100(h'01')}, {}, {"b": 2}])");
    REQUIRE(tree.has_value());
    auto const maps = (*tree)->elements();
    REQUIRE(maps.has_value());
    REQUIRE_EQ(maps->size(), 3u);
    auto const wide = maps->front().pairs();
    REQUIRE(wide.has_value());
    CHECK_EQ(wide->size(), 3u);
    std::vector<std::string> pairs;
    for (auto const [key, value] : *wide)
        pairs.push_back(cbor::to_edn(key).value() + ": " + cbor::to_edn(value).value());
    CHECK_EQ(pairs, std::vector<std::string>{"[1, 2]: {\"x\": 1}", "\"k\": [3]", "1([4, 5]): 100(h'01')"});

    auto first = wide->begin();
    auto copy = first;
    auto const before = first++;
    CHECK(before == wide->begin());
    CHECK_EQ(cbor::to_edn((*first).first).value(), "\"k\"");
    ++copy;
    CHECK(copy == first);
    CHECK_EQ(cbor::to_edn((*copy).second).value(), "[3]");

    auto const empty = (*std::ranges::next(maps->begin())).pairs();
    REQUIRE(empty.has_value());
    CHECK_EQ(empty->size(), 0u);
    CHECK(empty->begin() == empty->end());

    auto const last = (*std::ranges::next(maps->begin(), 2)).pairs();
    REQUIRE(last.has_value());
    REQUIRE_EQ(last->size(), 1u);
    auto const text = last->front().first.get<std::string_view>();
    CHECK_EQ(text.value(), "b");
    auto const number = last->front().second.get<int>();
    CHECK_EQ(number.value(), 2);
}

// RFC 8949 3: the additional information is the low 5 bits of the initial byte. The item keeps it, so that a head in
// a wider form than the shortest one keeps its width.
TEST_CASE("cbor::item::additional_information gives the low 5 bits of the initial byte")
{
    auto const bytes = cbor::diag2cbor("1_1");
    REQUIRE(bytes.has_value());
    auto const tree = cbor::decode(*bytes);
    REQUIRE(tree.has_value());
    CHECK_EQ((*tree)->additional_information(), 25);
}

cbor::owning<cbor::item> item_of(std::expected<cbor::owning<cbor::item>, cbor::error> const &r)
{
    REQUIRE(r.has_value());
    return *r;
}

// Each decode returns items that the caller owns, and no lazy keeps a
// cache that a second call or a second thread would write. Two decodes
// of one node give two items with the same content. A decode copies
// each text string into its own tree (owner decision: eager copies), so
// the two texts lie in two places.
TEST_CASE("decode: a node decoded twice gives two items that the caller owns")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\x01\x62hi"sv));
    REQUIRE(top_level.has_value());
    auto const first = top_level->to_item();
    auto const second = top_level->to_item();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK_NE(&**first, &**second);
    CHECK_EQ(read_argument_of_item(*(*first)), read_argument_of_item(*(*second)));
    auto const a = list_children_of_item(**first);
    auto const b = list_children_of_item(**second);
    REQUIRE_EQ(a.size(), 2u);
    REQUIRE_EQ(b.size(), 2u);
    CHECK(a[1] != b[1]);
    auto const a_text = a[1].get<std::string_view>();
    auto const b_text = b[1].get<std::string_view>();
    CHECK_EQ(a_text.value(), b_text.value());
    CHECK_NE(a_text.value().data(), b_text.value().data());
}

// Owner decision: cbor::decode builds a tree that points into nothing
// outside itself. The input is freed before the tree is read; the
// address sanitizer build finds any read of the freed input.
TEST_CASE("decode: the tree stays valid after the input is gone")
{
    auto input = std::make_unique<std::string>("\x83\x62hi\x42\x01\x02\xd8\x18\x42\x61\x62"s);
    auto const tree = cbor::decode(*input);
    input.reset();
    REQUIRE(tree.has_value());
    auto const children = list_children_of_item(**tree);
    REQUIRE_EQ(children.size(), 3u);
    auto const children_text = children[0].get<std::string_view>();
    CHECK_EQ(children_text.value(), "hi"sv);
    auto const children_bytes = children[1].get<std::span<std::byte const>>();
    CHECK_EQ(children_bytes.value().size(), 2u);
    CHECK_EQ(children_bytes.value()[1], std::byte{2});
    auto const embedded = cbor::encoded_cbor_data_item(*tree, children[2]);
    REQUIRE(embedded.has_value());
    auto const text = embedded->get<std::string_view>();
    CHECK_EQ(text.value(), "b"sv);
}

// RFC 8949 3.1: the argument of a string head is its length in bytes. A length that the bytes left cannot hold is
// too_little_data. This holds before any memory is taken for the copy (rule: a count in the input reserves nothing).
TEST_CASE("decode: a string length larger than the input is too_little_data")
{
    for (std::string const &wire : {"\x7b\x80\x00\x00\x00\x00\x00\x00\x00"s,
                                   "\x5b\x7f\xff\xff\xff\xff\xff\xff\xff\x00"s, "\x65"s + "abc"}) {
        CAPTURE(wire);
        auto const tree = cbor::decode(wire);
        CHECK_EQ(tree.error(), cbor::error::too_little_data);
    }
}

// RFC 8949 3: a data item has at least one byte, its initial byte. Empty input holds no data item. The decode of
// empty input must give an error value and must not stop the process.
TEST_CASE("decode: empty input is too_little_data")
{
    auto const decoded = cbor::decode(std::string_view{});
    CHECK_EQ(decoded.error(), cbor::error::too_little_data);
    auto const tree = cbor::decode(""s);
    CHECK_EQ(tree.error(), cbor::error::too_little_data);
}

// Owner decision 2026-10-20: cbor::decode reads the first data item and no byte after it, as a lazy does. So a CBOR
// sequence (RFC 8742) can be decoded item by item. The bytes 01 02 are the sequence 1, 2.
TEST_CASE("decode: bytes after the data item are not read")
{
    auto const first = cbor::decode("\x01\x02"s);
    REQUIRE(first.has_value());
    CHECK_EQ((*first)->get<std::uint64_t>(), 1u);
}

// The fault: decode on the lazy inside a temporary expected returned a reference into the top-level item. The temporary
// freed that item at the end of the statement. The result now owns its items and the top-level item, so it stays valid
// after the lazy is gone.
TEST_CASE("decode: the item outlives the lazy that decoded it")
{
    std::string const s("\x83\x01\x02\x03"sv);
    auto const r = test::make_lazy_from_copy(s)->to_item();
    REQUIRE(r.has_value());
    CHECK_EQ((*r)->type(), cbor::type::array);
    CHECK_EQ(read_argument_of_item(*(*r)), 3u);
    auto const elements = list_children_of_item(**r);
    REQUIRE_EQ(elements.size(), 3u);
    CHECK_EQ(read_argument_of_item(elements[2]), 3u);
}

// A child reached through two paths decodes to the same content.
TEST_CASE("decode: a child decoded through two paths has one content")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\xa1\x61k\x82\x01\x02"sv));
    REQUIRE(top_level.has_value());
    auto const map = item_of(top_level->to_item());
    CHECK_EQ(read_argument_of_item(*map), 1u);
    auto const entries = top_level->entries();
    REQUIRE(entries.has_value());
    auto const entry = *entries->begin();
    REQUIRE(entry.has_value());
    auto const value = top_level->at("k");
    REQUIRE(value.has_value());
    auto const by_key = item_of(value->to_item());
    auto const by_entry = item_of(entry->second.to_item());
    CHECK_EQ(read_argument_of_item(*by_key), read_argument_of_item(*by_entry));
    REQUIRE_EQ(list_children_of_item(*by_key).size(), 2u);
    REQUIRE_EQ(list_children_of_item(*by_entry).size(), 2u);
    CHECK_EQ(read_argument_of_item(list_children_of_item(*by_key)[1]), read_argument_of_item(list_children_of_item(*by_entry)[1]));
}

// RFC 8949 3.1: an array is its count and its data items. A map holds each pair as key and value, one after the other.
// So a map of n pairs shows 2n items in the order of the wire.
TEST_CASE("decode: an array and a map hold their data items in the order of the wire")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\x01\xa2\x61k\x62hi\x02\x80"sv));
    REQUIRE(top_level.has_value());
    auto const array = item_of(top_level->to_item());
    auto const elements = list_children_of_item(*array);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(read_argument_of_item(elements[0]), 1u);
    CHECK_EQ(elements[1].type(), cbor::type::map);
    auto const pairs = list_children_of_item(elements[1]);
    REQUIRE_EQ(pairs.size(), 4u);
    auto const pairs_text = pairs[0].get<std::string_view>();
    CHECK_EQ(pairs_text.value(), "k");
    auto const text = pairs[1].get<std::string_view>();
    CHECK_EQ(text.value(), "hi");
    CHECK_EQ(read_argument_of_item(pairs[2]), 2u);
    CHECK_EQ(pairs[3].type(), cbor::type::array);
    CHECK(list_children_of_item(pairs[3]).empty());
}

// The children of every container stand in one vector, and each container shows its own part. Nested
// containers and siblings each see only their own children, in the order of the wire.
TEST_CASE("decode: nested arrays and maps each show their own children")
{
    // [[1, [2, 3]], {4: [5]}, [], 6]
    auto const top_level = test::make_lazy_from_copy(std::string("\x84\x82\x01\x82\x02\x03\xa1\x04\x81\x05\x80\x06"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const outer = list_children_of_item(*root);
    REQUIRE_EQ(outer.size(), 4u);
    auto const first = list_children_of_item(outer[0]);
    REQUIRE_EQ(first.size(), 2u);
    CHECK_EQ(read_argument_of_item(first[0]), 1u);
    auto const inner = list_children_of_item(first[1]);
    REQUIRE_EQ(inner.size(), 2u);
    CHECK_EQ(read_argument_of_item(inner[0]), 2u);
    CHECK_EQ(read_argument_of_item(inner[1]), 3u);
    auto const pairs = list_children_of_item(outer[1]);
    REQUIRE_EQ(pairs.size(), 2u);
    CHECK_EQ(read_argument_of_item(pairs[0]), 4u);
    auto const value = list_children_of_item(pairs[1]);
    REQUIRE_EQ(value.size(), 1u);
    CHECK_EQ(read_argument_of_item(value[0]), 5u);
    CHECK(list_children_of_item(outer[2]).empty());
    CHECK_EQ(read_argument_of_item(outer[3]), 6u);
}

// A count in the input reserves nothing. An array that claims 2^63 elements has one byte after its head. It is refused
// before any element is read. The decode allocates in proportion to the input only.
TEST_CASE("decode: an array that claims more elements than bytes is refused")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x9b\x80\x00\x00\x00\x00\x00\x00\x00\x01"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->to_item();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::too_little_data);
    auto const map = test::make_lazy_from_copy(std::string("\xbb\x40\x00\x00\x00\x00\x00\x00\x00\x01"sv));
    REQUIRE(map.has_value());
    auto const m = map->to_item();
    REQUIRE_FALSE(m.has_value());
    CHECK_EQ(m.error(), cbor::error::too_little_data);
}

// The check of the count admits a count equal to the bytes left. Five empty arrays in five bytes are five children. Each
// child has no children of its own.
TEST_CASE("decode: a count equal to the bytes left of empty children is read")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x85\x80\x80\x80\x80\x80"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const elements = list_children_of_item(*root);
    REQUIRE_EQ(elements.size(), 5u);
    for (cbor::item const e : elements)
        CHECK(list_children_of_item(e).empty());
}

// Many sibling containers make the one vector of children grow many times while earlier containers already
// show their part. A container shows its part through an index into the vector, so the growth leaves every
// earlier view valid; AddressSanitizer reports a read of freed memory otherwise.
TEST_CASE("decode: many sibling containers keep their children through the growth of the vector")
{
    constexpr std::size_t siblings = 4096;
    std::string wire = "\x99\x10\x00"s;
    for (std::size_t i = 0; i < siblings; ++i)
        wire += "\x82\x01\x02"sv;
    auto const top_level = cbor::lazy::adopt(std::move(wire));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const elements = list_children_of_item(*root);
    REQUIRE_EQ(elements.size(), siblings);
    std::size_t sum = 0;
    for (cbor::item const e : elements)
        for (cbor::item const c : list_children_of_item(e))
            sum += read_argument_of_item(c);
    CHECK_EQ(sum, siblings * 3);
}

// RFC 8949 3.4.3 and 3.4.4: a bignum and a typed array are tags over a byte string. The tree keeps each as a
// tag item whose content is the byte string item.
TEST_CASE("decode: a bignum and a typed array are tags over a byte string")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00\xd8\x40\x42\x01\x02"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const elements = list_children_of_item(*root);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(elements[0].type(), cbor::type::unsigned_bignum);
    CHECK_EQ(read_argument_of_item(elements[0]), 2u);
    auto const tagged = elements[0].get<std::pair<std::uint64_t, cbor::item>>();
    auto const magnitude = tagged.value().second;
    auto const magnitude_bytes = magnitude.get<std::span<std::byte const>>();
    CHECK_EQ(magnitude_bytes.value().size(), 9u);
    CHECK_EQ(read_argument_of_item(elements[1]), 64u);
    auto const elements_typed = elements[1].get<cbor::typed_array>();
    CHECK_EQ(elements_typed.value().bytes.size(), 2u);
}

// RFC 8949 5.6 lets a decoder keep one entry of a repeated key. This holds for a decoder that is not in a deterministic
// profile. decode builds the item of such a map with no error. This holds at the top and inside an array.
TEST_CASE("decode: a map with a repeated key is read with no error")
{
    auto const twice = test::make_lazy_from_copy(std::string("\xa2\x61k\x01\x61k\x02"sv));
    REQUIRE(twice.has_value());
    CHECK(twice->to_item().has_value());
    auto const nested = test::make_lazy_from_copy(std::string("\x81\xa2\x01\x00\x18\x01\x00"sv));
    REQUIRE(nested.has_value());
    REQUIRE(nested->to_item().has_value());
    auto const inner = nested->at(0);
    REQUIRE(inner.has_value());
    CHECK(inner->to_item().has_value());
}

// decode compares no keys, so a map of 65536 text keys reads in one walk, with its last key equal to its first
// or not.
TEST_CASE("decode: a map of 65536 keys is read with a repeated key")
{
    constexpr std::size_t count = 65536;
    std::string map = "\xba\x00\x01\x00\x00"s;
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t const n = i + 1 == count ? 0 : i;
        map += '\x64';
        for (int shift = 24; shift >= 0; shift -= 8)
            map += static_cast<char>(n >> shift & 0xff);
        map += '\x00';
    }
    std::string distinct = map;
    distinct[distinct.size() - 2] = '\x01';
    distinct[distinct.size() - 3] = '\xff';
    distinct[distinct.size() - 4] = '\xff';
    auto const unique = cbor::lazy::adopt(std::move(distinct));
    REQUIRE(unique.has_value());
    CHECK(unique->to_item().has_value());
    auto const twice = cbor::lazy::adopt(std::move(map));
    REQUIRE(twice.has_value());
    CHECK(twice->to_item().has_value());
}

// A fuzzer held one core for more than 10 s with one map of 65535 integer keys. The keys were compared pair by pair.
// decode compares no keys. So the map reads in one walk with a repeated key or not.
TEST_CASE("decode: a map of 65535 integer keys is read with a repeated key")
{
    constexpr std::size_t count = 65535;
    std::string map = "\xb9\xff\xff"s;
    for (std::size_t i = 0; i < count; ++i) {
        map += '\x19';
        map += static_cast<char>(i >> 8 & 0xff);
        map += static_cast<char>(i & 0xff);
        map += '\x00';
    }
    std::string negative = map;
    negative[3] = '\x39';
    auto const unique = cbor::lazy::adopt(std::move(negative));
    REQUIRE(unique.has_value());
    CHECK(unique->to_item().has_value());
    std::string longer = map;
    longer.replace(map.size() - 4, 4, "\x1a\x00\x00\x00\x00\x00"sv);
    auto const twice = cbor::lazy::adopt(std::move(longer));
    REQUIRE(twice.has_value());
    CHECK(twice->to_item().has_value());
}

// The result owns what it shows, so decode on a temporary lazy is safe
// and compiles.
TEST_CASE("decode: a temporary lazy gives an item")
{
    auto const r = std::move(*test::make_lazy_from_copy(std::string("\x01"sv))).to_item();
    REQUIRE(r.has_value());
    CHECK_EQ((*r)->type(), cbor::type::unsigned_integer);
    CHECK_EQ(read_argument_of_item(*(*r)), 1u);
}

// The order does not matter: a child decoded first and decoded again
// after its parent has one content.
TEST_CASE("decode: a child decoded before its parent has the same content after it")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\x01\x82\x02\x03"sv));
    REQUIRE(top_level.has_value());
    auto const child = top_level->at(1);
    REQUIRE(child.has_value());
    auto const inner = item_of(child->to_item());
    REQUIRE(top_level->to_item().has_value());
    auto const again = top_level->at(1);
    REQUIRE(again.has_value());
    auto const later = item_of(again->to_item());
    CHECK_EQ(read_argument_of_item(*later), read_argument_of_item(*inner));
    CHECK_EQ(read_argument_of_item(list_children_of_item(*later)[1]), read_argument_of_item(list_children_of_item(*inner)[1]));
}

// The owner decided that a tree decode reads tags 28 and 29 as plain tags: 28 over the shared value and 29 over
// its index. Nothing is copied and nothing is followed.
TEST_CASE("decode: a shared reference points to the item of the shared value")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\xd8\x1c\x62hi\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const elements = list_children_of_item(*root);
    REQUIRE_EQ(elements.size(), 2u);
    CHECK_EQ(elements[0].type(), cbor::type::tag);
    CHECK_EQ(read_argument_of_item(elements[0]), 28u);
    auto const first_tag = elements[0].get<std::pair<std::uint64_t, cbor::item>>();
    auto const value = first_tag.value().second;
    auto const text = value.get<std::string_view>();
    CHECK_EQ(text.value(), "hi");
    CHECK_EQ(read_argument_of_item(elements[1]), 29u);
    auto const elements_tagged = elements[1].get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(elements_tagged.value().first, 29u);
    CHECK_EQ(elements_tagged.value().second.get<std::uint64_t>(), 0u);
}

// A tag 29 inside a lazy is a tag over its index, also when its mark lies before the decoded node.
TEST_CASE("decode: a tag 29 after its mark is a tag over its index")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\xd8\x1c\x62hi\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const element = top_level->at(1);
    REQUIRE(element.has_value());
    auto const inner = item_of(element->to_item());
    cbor::item const reference = list_children_of_item(*inner)[0];
    auto const reference_tagged = reference.get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(reference_tagged.value().first, 29u);
    auto const number = reference_tagged.value().second.get<std::uint64_t>();
    CHECK_EQ(number.value(), 0u);
}
// A cycle comes from the wire: 28([29(0)]) is an array that holds
// itself. The element points to that array, and the decode ends.
TEST_CASE("decode: a cyclic array is its own element")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\xd8\x1c\x81\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const root_tagged = root->get<std::pair<std::uint64_t, cbor::item>>();
    auto const array = root_tagged.value().second;
    CHECK_EQ(array.type(), cbor::type::array);
    auto const elements = list_children_of_item(array);
    REQUIRE_EQ(elements.size(), 1u);
    auto const tagged = elements[0].get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(tagged.value().first, 29u);
}

// A tag 29 before its mark is a plain tag: the tree decode checks no index.
TEST_CASE("decode: a forward tag 29 is a plain tag")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\xd8\x1d\x00\xd8\x1c\x00"sv));
    REQUIRE(top_level.has_value());
    auto const whole = item_of(top_level->to_item());
    auto const whole_elements = whole->elements();
    auto const whole_elements_tagged = whole_elements.value().front().get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(whole_elements_tagged.value().first, 29u);
    auto const element = top_level->at(0);
    REQUIRE(element.has_value());
    auto const inner = item_of(element->to_item());
    auto const tagged = inner->get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(tagged.value().first, 29u);
}
// Fault 21: in [28(0), 28(29(0)), 28(29(1)), ...] each element refers to the one before it. The decode of the last
// element walked the chain once for each step and recursed once for each step at the same depth. So a long chain took
// time in the square of its length and overflowed the stack. The decode now only stores the tag 29. The mark lies before
// the decoded element, so shared_value_resolve gives it as a lazy. The lazy follows the chain in one loop to the integer
// at its start.
TEST_CASE("decode: a long chain of shared references is read without recursion")
{
    constexpr std::uint16_t chain_length = 50000;
    constexpr char array_head_uint16 = '\x99';
    constexpr char uint16_head = '\x19';
    constexpr std::string_view shareable = "\xd8\x1c";
    constexpr std::string_view sharedref = "\xd8\x1d";
    auto const uint16_of = [](std::uint16_t const n) {
        return std::string{static_cast<char>(n >> CHAR_BIT),
                           static_cast<char>(n & std::numeric_limits<unsigned char>::max())};
    };
    std::string document = array_head_uint16 + uint16_of(chain_length);
    document += shareable;
    document += '\x00';
    for (std::uint16_t i = 1; i < chain_length; ++i) {
        document += shareable;
        document += sharedref;
        document += uint16_head + uint16_of(static_cast<std::uint16_t>(i - 1));
    }
    auto const top_level = cbor::lazy::adopt(std::move(document));
    REQUIRE(top_level.has_value());
    auto const last = top_level->at(chain_length - 1);
    REQUIRE(last.has_value());
    auto const decoded = item_of(last->to_item());
    auto const tagged = decoded->get<std::pair<std::uint64_t, cbor::item>>();
    auto const reference = tagged.value().second.get<std::pair<std::uint64_t, cbor::item>>();
    REQUIRE(reference.has_value());
    CHECK_EQ(reference->first, 29u);
    auto const number = reference->second.get<std::uint64_t>();
    CHECK_EQ(number.value(), chain_length - 2u);
}

// The stack is checked where the decode recurses. n tags 100(...) around 0 hold an integer at depth n. So do n arrays
// around 0. A depth that every stack holds is read, and an input deeper than a small stack holds is refused on a thread
// with that stack.
TEST_CASE("decode: the depth is bounded by the stack of the thread")
{
    for (std::string_view const level : {"\xd8\x64"sv, "\x81"sv}) {
        auto const read = test::make_lazy_from_copy(test::repeat(level, test::levels_every_stack_holds) + '\x00');
        REQUIRE(read.has_value());
        CHECK(read->to_item().has_value());
        auto const deep = test::make_lazy_from_copy(test::repeat(level, test::levels_beyond_small_stack) + '\x00');
        REQUIRE(deep.has_value());
        CHECK_EQ(test::error_on_small_stack([&deep] { return deep->to_item(); }), cbor::error::nesting_depth_exceeded);
    }
}

// Indefinite length is never read; a streaming reader is the place for it.
TEST_CASE("decode: indefinite length is refused")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\x01\x9f\xff"sv));
    REQUIRE(top_level.has_value());
    auto const r = top_level->to_item();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::indefinite_length);
}

// RFC 8949 3.3: a float16 keeps its width in the additional information,
// so it is written back in the width it came in.
TEST_CASE("decode: a float16 keeps additional information 25")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\xf9\x3e\x00"sv));
    REQUIRE(top_level.has_value());
    auto const f = item_of(top_level->to_item());
    CHECK_EQ(f->type(), cbor::type::half_float);
    CHECK_EQ(f->additional_information(), 25);
    auto const real = f->get<double>();
    CHECK_EQ(std::bit_cast<std::uint64_t>(real.value()), 0x3ff8000000000000u);
    std::string again;
    REQUIRE(cbor::encode(again, *f).has_value());
    CHECK_EQ(again, "\xf9\x3e\x00"sv);
}

}

namespace item_test
{

// Every value of cbor::type, each from the shortest item that has it. The expectation is written from RFC 8949 and RFC
// 8746. It uses 3.1 and 3.3 (major types, simple values 20 to 23, floats of additional information 25 to 27). It uses
// 3.4.3 (tags 2 and 3). It uses RFC 8746 2 (tags 64 to 87 without the reserved tag 76). lazy and item give the same
// answer.
TEST_CASE("type: every type of an item, through lazy and through the decoded item")
{
    struct row {
        std::string_view wire;
        cbor::type expected;
    };
    std::array const rows{
        row{"\x00"sv, cbor::type::unsigned_integer},
        row{"\x20"sv, cbor::type::negative_integer},
        row{"\x40"sv, cbor::type::byte_string},
        row{"\x60"sv, cbor::type::text_string},
        row{"\x80"sv, cbor::type::array},
        row{"\xa0"sv, cbor::type::map},
        row{"\xc1\x00"sv, cbor::type::tag},
        row{"\xd8\x4c\x40"sv, cbor::type::tag},
        row{"\xf4"sv, cbor::type::false_value},
        row{"\xf5"sv, cbor::type::true_value},
        row{"\xf6"sv, cbor::type::null},
        row{"\xf7"sv, cbor::type::undefined},
        row{"\xf0"sv, cbor::type::simple_value},
        row{"\xf8\x20"sv, cbor::type::simple_value},
        row{"\xf9\x00\x00"sv, cbor::type::half_float},
        row{"\xfa\x00\x00\x00\x00"sv, cbor::type::single_float},
        row{"\xfb\x00\x00\x00\x00\x00\x00\x00\x00"sv, cbor::type::double_float},
        row{"\xc2\x40"sv, cbor::type::unsigned_integer},
        row{"\xc3\x40"sv, cbor::type::negative_integer},
        row{"\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv, cbor::type::unsigned_bignum},
        row{"\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv, cbor::type::negative_bignum},
        row{"\xd8\x40\x40"sv, cbor::type::typed_array},
        row{"\xd8\x57\x40"sv, cbor::type::typed_array},
    };
    for (row const &r : rows) {
        auto const l = test::make_lazy_from_copy(r.wire);
        REQUIRE(l.has_value());
        auto const by_lazy = l->type();
        REQUIRE(by_lazy.has_value());
        CHECK_EQ(*by_lazy, r.expected);
        auto const tree = item_of(l->to_item());
        CHECK_EQ(tree->type(), r.expected);
    }
}

// A lazy and the decoded item both read tags 28 and 29 as tags.
TEST_CASE("type: lazy and the item keep tags 28 and 29")
{
    auto const root = test::make_lazy_from_copy(std::string("\x82\xd8\x1c\x01\xd8\x1d\x00"sv));
    REQUIRE(root.has_value());
    auto const at_0 = root->at(0);
    auto const kind = at_0->type();
    CHECK_EQ(*kind, cbor::type::tag);
    auto const at_1 = root->at(1);
    auto const kind_2 = at_1->type();
    CHECK_EQ(*kind_2, cbor::type::tag);
    auto const tree = item_of(root->to_item());
    auto const elements = list_children_of_item(*tree);
    CHECK_EQ(elements[0].type(), cbor::type::tag);
    CHECK_EQ(elements[1].type(), cbor::type::tag);
}

// A lazy that holds no top-level item is a wrong use, as for every other read of lazy.
TEST_CASE("type: an empty lazy throws")
{
    CHECK_THROWS_AS((void)cbor::lazy{}.type(), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.to_item(), std::logic_error);
}

}

namespace item_test
{

// The tree keeps the additional information of each head. So encode writes each length, tag number and integer at the
// width it had on the wire. A head that is not in preferred form comes back in the same form. 28([29(0)]) is an array
// that holds itself. Encode writes the head of tag 29 and its index and does not follow the pointer. The index of tag 29
// is an integer like any other and keeps its width.
TEST_CASE("encode: a tree is written with the head widths of its input, also with a cycle through tag 29")
{
    struct row {
        std::string_view wire;
        std::string_view own;
    };
    for (row const r : {row{"\xd8\x1c\x81\xd8\x1d\x00"sv, "\xd8\x1c\x81\xd8\x1d\x00"sv},
                        row{"\x82\xd8\x1c\x19\x00\x01\xd8\x1d\x18\x00"sv, "\x82\xd8\x1c\x19\x00\x01\xd8\x1d\x18\x00"sv},
                        row{"\x9a\x00\x00\x00\x01\x7a\x00\x00\x00\x02hi"sv, "\x9a\x00\x00\x00\x01\x7a\x00\x00\x00\x02hi"sv}}) {
        CAPTURE(r.wire);
        auto const top_level = test::make_lazy_from_copy(std::string(r.wire));
        REQUIRE(top_level.has_value());
        auto const root = item_of(top_level->to_item());
        std::string out;
        auto const size = cbor::encode(out, *root);
        REQUIRE(size.has_value());
        CHECK_EQ(*size, r.own.size());
        CHECK_EQ(out, r.own);
        CHECK(same_value(out, r.wire));
// The iov forms exist where the platform has <sys/uio.h>, as in the library (encode.hpp); Windows has none.
#if __has_include(<sys/uio.h>)
        auto const pieces = cbor::encode_iov(*root);
        REQUIRE(pieces.has_value());
        auto const read = test::join_pieces_into_bytes(**pieces);
        REQUIRE(read.has_value());
        std::string const joined = *read;
        CHECK_EQ(joined, r.own);
#endif
    }
}

// encode recurses through the levels of a tree as decode does, so a deep tree that decode gives is encoded back to
// its bytes.
TEST_CASE("encode: a deep tree that decode gives is encoded")
{
    std::string const deep = test::repeat("\x81"sv, test::levels_every_stack_holds) + '\x00';
    auto const top_level = test::make_lazy_from_copy(deep);
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    std::string out;
    REQUIRE(cbor::encode(out, *root).has_value());
    CHECK_EQ(out, deep);
}

// encode into a span that is too short gives no_buffer_space.
TEST_CASE("encode: a span that is too short")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x63" "abc"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    std::array<char, 3> buffer{};
    auto const r = cbor::encode(std::span<char>(buffer), *root);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::no_buffer_space);
}

// A tag 29 that names a mark which comes later on the wire is a plain tag in a tree decode.
TEST_CASE("decode: a reference before its mark is a plain tag")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\xd8\x1d\x00\xd8\x1c\x01"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const root_elements = root->elements();
    auto const tagged = root_elements.value().front().get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(tagged.value().first, 29u);
}
// RFC 8949 3.4.5.1: tag 24 holds a byte string with one encoded CBOR data item. The decode gives both the byte string as
// written and the embedded item as a tree. So a reader takes the item without a second decode, and encode writes the
// byte string again bit for bit.
TEST_CASE("decode: tag 24 gives the byte string and the embedded item")
{
    std::string const wire = "\xd8\x18\x45\x64IETF"s;
    auto const top_level = test::make_lazy_from_copy(wire);
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    REQUIRE_EQ(root->type(), cbor::type::tag);
    auto const root_tagged = root->get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(root_tagged.value().first, 24u);
    auto const bytes = root_tagged.value().second.get<std::span<std::byte const>>();
    CHECK_EQ(bytes.value().size(), 5u);
    auto const inner = cbor::encoded_cbor_data_item(root, *root);
    auto const text = inner->get<std::string_view>();
    CHECK_EQ(text.value(), "IETF");
    std::string again;
    REQUIRE(cbor::encode(again, *root).has_value());
    CHECK_EQ(again, wire);
}

// RFC 8949 3.4.5.1: the embedded item is a data item like any other, so it can itself be a tag 24. The decode
// enters each level, and encode writes the outer bytes again unchanged.
TEST_CASE("decode: a tag 24 inside a tag 24 is decoded at each level")
{
    std::string const wire = "\xd8\x18\x44\xd8\x18\x41\x01"s;
    auto const top_level = test::make_lazy_from_copy(wire);
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const outer_found = cbor::encoded_cbor_data_item(root, *root);
    REQUIRE(outer_found.has_value());
    cbor::item const outer = outer_found.value();
    REQUIRE_EQ(outer.type(), cbor::type::tag);
    auto const inner_found = cbor::encoded_cbor_data_item(root, outer);
    REQUIRE(inner_found.has_value());
    cbor::item const inner = inner_found.value();
    CHECK_EQ(inner.type(), cbor::type::unsigned_integer);
    auto const number = inner.get<std::uint64_t>();
    CHECK_EQ(number.value(), 1u);
    std::string again;
    REQUIRE(cbor::encode(again, *root).has_value());
    CHECK_EQ(again, wire);
}

// The tree of a tag 24 lives in the memory of the decode result. The result must keep it, its bytes and their owner
// alive after the lazy and the caller's string are gone. So the address sanitizer sees every read here.
TEST_CASE("decode: the embedded item of a tag 24 lives as long as the result")
{
    std::optional<cbor::owning<cbor::item>> root;
    {
        std::string const wire = "\xd8\x18\x47\xd8\x18\x44\x82\x01\x61x"s;
        root.emplace(item_of(test::make_lazy_from_copy(std::string(wire))->to_item()));
    }
    auto const outer_found = cbor::encoded_cbor_data_item(*root, **root);
    REQUIRE(outer_found.has_value());
    cbor::item const outer = outer_found.value();
    auto const inner_found = cbor::encoded_cbor_data_item(*root, outer);
    REQUIRE(inner_found.has_value());
    cbor::item const inner = inner_found.value();
    auto const children = list_children_of_item(inner);
    REQUIRE_EQ(children.size(), 2u);
    auto const number = children[0].get<std::uint64_t>();
    CHECK_EQ(number.value(), 1u);
    auto const text = children[1].get<std::string_view>();
    CHECK_EQ(text.value(), "x");
}

// lazy::to_item keeps a tag 28 over its value and a tag 29 over its index (owner decision: only cbor::decode
// resolves them).
TEST_CASE("clone: a tag 28 and a tag 29 are tag nodes")
{
    std::string const wire = "\x82\xd8\x1c\x82\x01\x02\xd8\x1d\x00"s;
    auto const root = item_of(test::make_lazy_from_copy(std::string(wire))->to_item());
    auto const children = list_children_of_item(*root);
    auto const first_tag = children[0].get<std::pair<std::uint64_t, cbor::item>>();
    CHECK(first_tag.value().second.elements().has_value());
    auto const children_tagged = children[1].get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(children_tagged.value().first, 29u);
    auto const number = children_tagged.value().second.get<std::uint64_t>();
    CHECK_EQ(number.value(), 0u);
}

// Owner decision: the embedded item is a top-level item of its own. A tag 29 inside it refers to a tag 28 inside
// the same embedded item, and the marks outside do not count there.
TEST_CASE("decode: a shared reference inside tag 24 refers to a mark inside the same embedded item")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\xd8\x18\x48\x82\xd8\x1c\x61\x61\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const inner = cbor::encoded_cbor_data_item(root, *root);
    auto const elements = list_children_of_item(inner.value());
    REQUIRE_EQ(elements.size(), 2u);
    auto const tagged = elements[1].get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(tagged.value().first, 29u);
}

// The other direction of the same decision: a mark outside is not visible inside. So 29(0) inside the embedded item has
// no mark to refer to.
TEST_CASE("decode: a shared reference inside tag 24 does not see a mark outside")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\xd8\x1c\x61\x61\xd8\x18\x43\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const root_elements = root->elements();
    auto const embedded = cbor::encoded_cbor_data_item(root, root_elements.value().next().front());
    REQUIRE(embedded.has_value());
    auto const tagged = embedded->get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(tagged.value().first, 29u);
}

// And a mark inside the embedded item does not count outside: the 29(0) after the tag 24 has no mark.
TEST_CASE("decode: a mark inside tag 24 does not count outside")
{
    auto const top_level = test::make_lazy_from_copy(std::string("\x82\xd8\x18\x43\xd8\x1c\x01\xd8\x1d\x00"sv));
    REQUIRE(top_level.has_value());
    auto const root = item_of(top_level->to_item());
    auto const root_elements = root->elements();
    auto const tagged = root_elements.value().next().front().get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(tagged.value().first, 29u);
}

// The embedded bytes are read with the same checks as the outer bytes. So malformed embedded bytes give the error that
// the same bytes give at the top level.
TEST_CASE("decode: malformed bytes inside tag 24 give their own error")
{
    auto const short_head = test::make_lazy_from_copy(std::string("\xd8\x18\x41\x18"sv));
    REQUIRE(short_head.has_value());
    auto const r = short_head->to_item();
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), cbor::error::too_little_data);
    auto const reserved = test::make_lazy_from_copy(std::string("\xd8\x18\x41\x1c"sv));
    REQUIRE(reserved.has_value());
    auto const at_top = test::make_lazy_from_copy(std::string("\x1c"sv));
    REQUIRE(at_top.has_value());
    auto const inside = reserved->to_item();
    auto const outside = at_top->to_item();
    REQUIRE_FALSE(inside.has_value());
    REQUIRE_FALSE(outside.has_value());
    CHECK_EQ(inside.error(), outside.error());
}

// The recursion goes on across the boundary: the embedded root of 24(<<0>>) is read by the same recursion, below the tag
// and its byte string. So 24(<<0>>) is read inside a depth that every stack holds. It is refused inside an input deeper
// than a small stack holds, on a thread with that stack.
TEST_CASE("decode: the stack check goes on inside tag 24")
{
    auto const read = test::make_lazy_from_copy(test::repeat("\x81"sv, test::levels_every_stack_holds) + "\xd8\x18\x41\x00"s);
    REQUIRE(read.has_value());
    CHECK(read->to_item().has_value());
    auto const deep = test::make_lazy_from_copy(test::repeat("\x81"sv, test::levels_beyond_small_stack) + "\xd8\x18\x41\x00"s);
    REQUIRE(deep.has_value());
    CHECK_EQ(test::error_on_small_stack([&deep] { return deep->to_item(); }), cbor::error::nesting_depth_exceeded);
}

// RFC 8949 3.4.5.1: the byte string of tag 24 holds one encoded data item. decode reads everything and refuses bytes
// after that item with syntax_error. The owner decided on this error. A lazy step and a typed read read only up to their
// target, by owner decision, so they do not see those bytes.
TEST_CASE("tag 24 with bytes after its data item is refused by decode and not seen by a step")
{
    std::string const wire = "\xd8\x18\x42\x01\x02"s;
    auto const top_level = test::make_lazy_from_copy(wire);
    REQUIRE(top_level.has_value());
    auto const tree = top_level->to_item();
    REQUIRE_FALSE(tree.has_value());
    CHECK_EQ(tree.error(), cbor::error::syntax_error);
    auto const read = top_level->get<int>();
    CHECK_EQ(*read, 1);
    CHECK_EQ(*cbor::at_path<"$", int>(std::string_view(wire)), 1);
    std::string const in_array = "\xd8\x18\x43\x81\x01\x02"s;
    auto const at_0 = test::make_lazy_from_copy(in_array)->at(0);
    auto const read_2 = at_0->get<int>();
    CHECK_EQ(*read_2, 1);
    CHECK_EQ(*cbor::at_path<"$[0]", int>(std::string_view(in_array)), 1);
}

}

namespace item_test
{

// RFC 8949 3.3: a float of 16 or 32 bits is held as a double. Encode narrows it back to the width of its additional
// information. A conversion of the processor sets the quiet bit of a signalling NaN, so a NaN is widened and narrowed by
// its bits. Each width is checked with four values: a signalling NaN, a quiet NaN with a payload, negative zero and the
// smallest subnormal. All must come back byte for byte.
TEST_CASE("encode: a float of each width comes back byte for byte")
{
    struct row {
        std::string_view wire;
        cbor::type type;
    };
    row const rows[] = {
        {"\xf9\x7c\x01"sv, cbor::type::half_float},
        {"\xf9\x7e\x01"sv, cbor::type::half_float},
        {"\xf9\xfd\x55"sv, cbor::type::half_float},
        {"\xf9\x80\x00"sv, cbor::type::half_float},
        {"\xf9\x00\x01"sv, cbor::type::half_float},
        {"\xf9\x83\xff"sv, cbor::type::half_float},
        {"\xfa\x7f\x80\x00\x01"sv, cbor::type::single_float},
        {"\xfa\x7f\xc0\x00\x01"sv, cbor::type::single_float},
        {"\xfa\xff\xa0\x00\x00"sv, cbor::type::single_float},
        {"\xfa\x80\x00\x00\x00"sv, cbor::type::single_float},
        {"\xfa\x00\x00\x00\x01"sv, cbor::type::single_float},
        {"\xfb\x7f\xf0\x00\x00\x00\x00\x00\x01"sv, cbor::type::double_float},
        {"\xfb\x7f\xf8\x00\x00\x00\x00\x00\x01"sv, cbor::type::double_float},
        {"\xfb\x80\x00\x00\x00\x00\x00\x00\x00"sv, cbor::type::double_float},
        {"\xfb\x00\x00\x00\x00\x00\x00\x00\x01"sv, cbor::type::double_float},
    };
    for (row const &r : rows) {
        CAPTURE(r.wire);
        auto const root = item_of(test::make_lazy_from_copy(std::string(r.wire))->to_item());
        CHECK_EQ(root->type(), r.type);
        CHECK_EQ(root->additional_information(), static_cast<std::uint8_t>(r.wire.front()) & 0x1f);
        std::string again;
        REQUIRE(cbor::encode(again, *root).has_value());
        CHECK_EQ(again, r.wire);
    }
}

// RFC 8949 3.1 and 3.3 give the type of an item by its major type and its additional information. The input is a finite
// set, so every pair of major type 0 to 7 and additional information 0 to 27 is checked. A head that is not in preferred
// form is well-formed, so an argument of 1 stands in every width. A simple value in one extra byte needs 32 or more.
// Each tag takes a content that RFC 8949 3.4 admits; tag 29 needs a mark and has its own tests. The expected type is
// written from the tables of RFC 8949 3.1 and 3.3. A tag 2 or 3 over h'05' fits a basic integer. It is that integer (RFC
// 8949 3.4.3). encode writes each head at its input width. It writes a folded bignum in the shortest head. A float at
// its width, which also checks the major type that encode takes from the type.
TEST_CASE("decode: every major type and additional information gives the type of RFC 8949")
{
    for (unsigned major = 0; major < test::rfc8949::major_types; ++major) {
        for (unsigned info = 0; info < test::rfc8949::first_reserved_information; ++info) {
            CAPTURE(major);
            CAPTURE(info);
            std::string wire(1, static_cast<char>(major << 5 | info));
            std::size_t const width = info < 24 ? 0 : std::size_t{1} << (info - 24);
            std::uint64_t const argument = info < 24 ? info : major == 7 && info == 24 ? 32 : 1;
            for (std::size_t i = width; i > 0; --i)
                wire += static_cast<char>(argument >> (8 * (i - 1)) & 0xff);
            REQUIRE(std::in_range<std::size_t>(argument));
            std::size_t const length = static_cast<std::size_t>(argument);
            if (major == 2 || major == 3)
                wire.append(length, 'a');
            else if (major == 4)
                wire.append(length, '\x00');
            else if (major == 6)
                wire += argument == 0                     ? "\x60"s
                        : argument == 2 || argument == 3  ? "\x41\x05"s
                        : argument == 24                  ? "\x41\x00"s
                        : argument == 29                  ? ""s
                                                          : "\x00"s;
            if (major == 6 && argument == 29)
                continue;
            else if (major == 5)
                wire.append(2 * length, '\x00');
            cbor::type expected = cbor::type::simple_value;
            switch (major) {
            case 0:
                expected = cbor::type::unsigned_integer;
                break;
            case 1:
                expected = cbor::type::negative_integer;
                break;
            case 2:
                expected = cbor::type::byte_string;
                break;
            case 3:
                expected = cbor::type::text_string;
                break;
            case 4:
                expected = cbor::type::array;
                break;
            case 5:
                expected = cbor::type::map;
                break;
            case 6:
                expected = argument == 2   ? cbor::type::unsigned_integer
                           : argument == 3 ? cbor::type::negative_integer
                                           : cbor::type::tag;
                break;
            default:
                expected = info == 20   ? cbor::type::false_value
                           : info == 21 ? cbor::type::true_value
                           : info == 22 ? cbor::type::null
                           : info == 23 ? cbor::type::undefined
                           : info == 25 ? cbor::type::half_float
                           : info == 26 ? cbor::type::single_float
                           : info == 27 ? cbor::type::double_float
                                        : cbor::type::simple_value;
                break;
            }
            auto const top_level = test::make_lazy_from_copy(std::string(wire));
            REQUIRE(top_level.has_value());
            auto const decoded = top_level->to_item();
            REQUIRE(decoded.has_value());
            CHECK_EQ((*decoded)->type(), expected);
            bool const bignum = major == 6 && (argument == 2 || argument == 3);
            CHECK_EQ((*decoded)->additional_information(), bignum ? 5 : info);
            CHECK_EQ(find_major_type_of((*decoded)->type()), bignum ? static_cast<cbor::major_type>(argument - 2)
                                                        : static_cast<cbor::major_type>(major));
            std::string const own = bignum ? (argument == 2 ? "\x05"s : "\x25"s) : wire;
            std::string again;
            REQUIRE(cbor::encode(again, **decoded).has_value());
            CHECK_EQ(again, own);
            CHECK(same_value(again, wire));
        }
    }
}

// RFC 8949 3.4.3: a bignum that fits a basic integer is that integer, and preferred serialization never writes
// it as a bignum. A larger bignum and a typed array of RFC 8746 keep major type 6 when they are written back.
TEST_CASE("encode: a bignum that fits is an integer, a larger bignum and a typed array stay tags")
{
    struct row {
        std::string_view wire;
        std::string_view own;
        cbor::major_type major;
    };
    std::string const two_64 = "\xc2\x49\x01"s + std::string(8, '\0');
    std::string const minus_two_64_minus_1 = "\xc3\x49\x01"s + std::string(8, '\0');
    for (row const r : {row{"\xc2\x41\x05"sv, "\x05"sv, cbor::major_type::unsigned_integer},
                        row{"\xc3\x41\x05"sv, "\x25"sv, cbor::major_type::negative_integer},
                        row{two_64, two_64, cbor::major_type::tag},
                        row{minus_two_64_minus_1, minus_two_64_minus_1, cbor::major_type::tag},
                        row{"\xd8\x40\x40"sv, "\xd8\x40\x40"sv, cbor::major_type::tag}}) {
        CAPTURE(r.wire);
        auto const root = item_of(test::make_lazy_from_copy(std::string(r.wire))->to_item());
        CHECK_EQ(find_major_type_of(root->type()), r.major);
        std::string again;
        REQUIRE(cbor::encode(again, *root).has_value());
        CHECK_EQ(again, r.own);
        CHECK(same_value(again, r.wire));
    }
}

// encoded_cbor_data_item takes a tag 24 of the decoded tree that is passed. Any other item is a wrong use and
// throws, so a caller never reads an item that does not exist.
TEST_CASE("encoded_cbor_data_item: an item that is not a tag 24 of the tree throws")
{
    auto const root = item_of(test::make_lazy_from_copy(std::string("\x82\xd8\x1c\x01\xd8\x1d\x00"sv))->to_item());
    auto const root_elements = root->elements();
    CHECK_THROWS_AS((void)cbor::encoded_cbor_data_item(root, root_elements.value().next().front()), std::logic_error);
    // A tree that no decode made cannot be built: owning<item> has no public constructor.
    CHECK_FALSE((std::is_constructible_v<cbor::owning<cbor::item>, std::shared_ptr<cbor::item const>>));
}
// Owner decision 2026-10-17: an item is a result of a decode only. No caller builds one, so no item holds a wrong count
// or a type that its content does not match. Every read of the content checks the type.
TEST_CASE("item: no caller builds an item")
{
    CHECK_FALSE((std::is_constructible_v<cbor::item, cbor::type, std::uint8_t>));
    CHECK_FALSE(std::is_default_constructible_v<cbor::item>);
}

}

// RFC 8949 3: each data item has at least its initial byte. By owner decision, decode refuses a count larger than the
// bytes left. It gives too_little_data before it reads an element. The malformed element after the head is not reached.
TEST_CASE("decode: a count larger than the bytes left is too_little_data before the elements")
{
    for (std::string const &wire : {"\x8c\xff"s, "\x95\xf8\x04"s, "\x95\xbf"s, "\x88\x00\x7f"s, "\x87\xd8\x1c\x1f"s,
                                   "\x85\xd8\x1c\xd8\x1c"s, "\xa2\xff\x00\x00"s}) {
        CAPTURE(wire);
        auto const top_level = test::make_lazy_from_copy(wire);
        REQUIRE(top_level.has_value());
        auto const r = top_level->to_item();
        REQUIRE_FALSE(r.has_value());
        CHECK_EQ(r.error(), cbor::error::too_little_data);
    }
}

// A string of length 0 takes no bytes from the block, and its view must still read as an empty string.
TEST_CASE("decode: an empty text string and an empty byte string read as empty")
{
    auto const tree = cbor::decode("\x82\x60\x40"sv);
    REQUIRE(tree.has_value());
    auto const tree_elements = (*tree)->elements();
    auto const children = tree_elements.value();
    auto const children_text = children.front().get<std::string_view>();
    CHECK_EQ(children_text.value(), ""sv);
    auto const bytes = children.next().front().get<std::span<std::byte const>>();
    CHECK(bytes.value().empty());
}

// The strings of a decode are copied into blocks of CBOR_STRING_SAVER_CHUNK_SIZE bytes. A string of exactly that size
// fills a block, and a string one byte longer needs a block of its own. Both must keep every byte, and the tree must
// outlive the input.
TEST_CASE("decode: strings at the size of a block and one byte over keep every byte after the input is freed")
{
    constexpr std::size_t block = CBOR_STRING_SAVER_CHUNK_SIZE;
    std::string const a(block, 'a');
    std::string const b(block + 1, 'b');
    auto input = std::make_unique<std::string>("\x83\x61x\x79"s);
    input->push_back(static_cast<char>(block >> 8));
    input->push_back(static_cast<char>(block & 0xff));
    *input += a;
    *input += "\x79"s;
    input->push_back(static_cast<char>((block + 1) >> 8));
    input->push_back(static_cast<char>((block + 1) & 0xff));
    *input += b;
    auto const tree = cbor::decode(*input);
    input.reset();
    REQUIRE(tree.has_value());
    auto const tree_elements = (*tree)->elements();
    auto const children = tree_elements.value();
    CHECK_EQ(children.front().get<std::string_view>(), "x"sv);
    auto const children_text = children.next(1).front().get<std::string_view>();
    CHECK_EQ(children_text.value(), a);
    auto const text = children.next(2).front().get<std::string_view>();
    CHECK_EQ(text.value(), b);
}

// An error part way through gives the error and no tree; the strings copied before the error go with the zone.
TEST_CASE("decode: an error after some strings gives the error and no tree")
{
    auto const tree = cbor::decode("\x83\x61x\x61y\x7a\x00\x00\x10\x00"sv);
    REQUIRE_FALSE(tree.has_value());
    CHECK_EQ(tree.error(), error::too_little_data);
}

// The strings inside the tree of a tag 24 are copies in the zone too, so they stay valid after the input is freed.
TEST_CASE("decode: a string inside a tag 24 tree outlives the input")
{
    auto input = std::make_unique<std::string>("\xd8\x18\x45\x64IETF"s);
    auto const tree = cbor::decode(*input);
    input.reset();
    REQUIRE(tree.has_value());
    auto const embedded = cbor::encoded_cbor_data_item(*tree, **tree);
    auto const text = embedded->get<std::string_view>();
    CHECK_EQ(text.value(), "IETF");
}

namespace
{

struct sample {
    cbor::type type;
    std::string_view encoded;
};

// One encoded item for each member of cbor::type, written from RFC 8949 Appendix A, RFC 8746 and the value-sharing
// registration. A tag 29 names a tag 28 whose value is still open, so it closes a cycle. So the shared_reference stands
// as the second element of 28([1, 29(0)]), its target is the root, and the test reads that element.
constexpr std::array<sample, 19> samples{{
    {cbor::type::unsigned_integer, "\x18\x64"sv},
    {cbor::type::negative_integer, "\x38\x63"sv},
    {cbor::type::byte_string, "\x44\x01\x02\x03\x04"sv},
    {cbor::type::text_string, "\x64IETF"sv},
    {cbor::type::array, "\x83\x01\x02\x03"sv},
    {cbor::type::map, "\xa1\x01\x02"sv},
    {cbor::type::tag, "\xc1\x1a\x51\x4b\x67\xb0"sv},
    {cbor::type::false_value, "\xf4"sv},
    {cbor::type::true_value, "\xf5"sv},
    {cbor::type::null, "\xf6"sv},
    {cbor::type::undefined, "\xf7"sv},
    {cbor::type::simple_value, "\xf0"sv},
    {cbor::type::half_float, "\xf9\x3c\x00"sv},
    {cbor::type::single_float, "\xfa\x47\xc3\x50\x00"sv},
    {cbor::type::double_float, "\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"sv},
    {cbor::type::unsigned_bignum, "\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv},
    {cbor::type::negative_bignum, "\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv},
    {cbor::type::typed_array, "\xd8\x40\x42\x01\x02"sv},
    {cbor::type::shared_reference, "\xd8\x1c\x82\x01\xd8\x1d\x00"sv},
}};

} // namespace

// RFC 8949 5.3.2 and 5.4: a tag passes its content as it is, unless the decoder turns it into a native form. The owner
// decided two things. A tree decode turns a tag 2 or 3 into an integer only over a byte string. It decodes the content
// of a tag 24 only when it is a byte string. Any other content stays under the tag. The type of a tag 2 or 3 item is
// the kind of that tag. So decode accepts each of these tags over an item of each major type (the finite set), and the
// documentation of decode says so.
TEST_CASE("decode: tags 0 to 5, 21 to 24 and 32 to 36 over every major type stay tags or give their integer")
{
    std::array<std::string, 8> const contents{"\x00"s, "\x20"s, "\x41\x01"s, "\x61\x61"s,
                                              "\x81\x00"s, "\xa1\x00\x00"s, "\xc6\x00"s, "\xf4"s};
    for (int const tag : {0, 1, 2, 3, 4, 5, 21, 22, 23, 24, 32, 33, 34, 35, 36}) {
        for (std::size_t major = 0; major < contents.size(); ++major) {
            CAPTURE(tag);
            CAPTURE(major);
            std::string const bytes = (tag < 24 ? std::string(1, static_cast<char>(0xc0 + tag))
                                                : "\xd8"s + static_cast<char>(tag)) +
                                      contents[major];
            auto const r = cbor::decode(bytes);
            REQUIRE(r.has_value());
            cbor::type const expected = tag == 2 && major == 2   ? cbor::type::unsigned_integer
                                        : tag == 3 && major == 2 ? cbor::type::negative_integer
                                        : tag == 2               ? cbor::type::unsigned_bignum
                                        : tag == 3               ? cbor::type::negative_bignum
                                                                 : cbor::type::tag;
            CHECK_EQ((*r)->type(), expected);
        }
    }
}

namespace item_get_test
{

using cbor::error;

// The integer types of the C++ standard, signed and unsigned, and the character types. Each is an integral type that
// is not bool, so get takes each of them, and the set is finite.
template <class... T>
struct types {
};

using integer_types = types<signed char, short, int, long, long long, unsigned char, unsigned short, unsigned,
                            unsigned long, unsigned long long, char, wchar_t, char8_t, char16_t, char32_t>;

template <class T>
bool holds(std::int64_t const value)
{
    if (value < 0)
        return std::numeric_limits<T>::is_signed && value >= static_cast<std::int64_t>(std::numeric_limits<T>::min());
    return static_cast<std::uint64_t>(value) <= static_cast<std::uint64_t>(std::numeric_limits<T>::max());
}

// The values of the samples, from RFC 8949 Appendix A: 100, -1 - 99, the tag 1 over 1363896240, a bignum of 2^64 and
// of -1 - 2^64. A bignum node holds more than 64 bits, so no integer type holds it.
std::expected<std::int64_t, error> integer_of(cbor::type const kind)
{
    switch (kind) {
    case cbor::type::unsigned_integer:
        return 100;
    case cbor::type::negative_integer:
        return -100;
    case cbor::type::tag:
        return 1363896240;
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
        return std::unexpected(error::number_out_of_range);
    default:
        return std::unexpected(error::incorrect_type);
    }
}

// A double holds each integer of the samples exactly. Each float of the samples widens to a double exactly (RFC 8949
// 3.3): 1.0, 100000.0 and 1.1.
std::expected<double, error> double_of(cbor::type const kind)
{
    switch (kind) {
    case cbor::type::unsigned_integer:
        return 100.0;
    case cbor::type::negative_integer:
        return -100.0;
    case cbor::type::tag:
        return 1363896240.0;
    case cbor::type::half_float:
        return 1.0;
    case cbor::type::single_float:
        return 100000.0;
    case cbor::type::double_float:
        return 1.1;
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
        return std::unexpected(error::number_out_of_range);
    default:
        return std::unexpected(error::incorrect_type);
    }
}

// RFC 8949 3.3 Table 4: false is 20, true 21, null 22 and undefined 23. The sample of a simple value is simple(16).
std::expected<cbor::simple_value, error> simple_of(cbor::type const kind)
{
    switch (kind) {
    case cbor::type::false_value:
        return cbor::simple_value{20};
    case cbor::type::true_value:
        return cbor::simple_value{21};
    case cbor::type::null:
        return cbor::simple_value{22};
    case cbor::type::undefined:
        return cbor::simple_value{23};
    case cbor::type::simple_value:
        return cbor::simple_value{16};
    default:
        return std::unexpected(error::incorrect_type);
    }
}

template <class T, class E>
void check_result(cbor::result<T> const &got, std::expected<E, error> const &expected)
{
    REQUIRE_EQ(got.has_value(), expected.has_value());
    if (!expected)
        CHECK_EQ(got.error(), expected.error());
}

using pair = std::pair<std::uint64_t, cbor::item>;
using pairs = decltype(std::declval<cbor::item>().pairs())::value_type;

template <class T>
void check_integer(cbor::type const kind, cbor::item const it)
{
    CAPTURE(sizeof(T));
    CAPTURE(std::numeric_limits<T>::is_signed);
    std::expected<std::int64_t, error> expected = integer_of(kind);
    if (expected && !holds<T>(*expected))
        expected = std::unexpected(error::number_out_of_range);
    auto const got = it.get<T>();
    check_result(got, expected);
    if (expected)
        CHECK_EQ(static_cast<std::int64_t>(*got), *expected);
}

template <class... T>
void check_integers(cbor::type const kind, cbor::item const it, types<T...>)
{
    (check_integer<T>(kind, it), ...);
}

void check_every_type(cbor::type const kind, cbor::item const it, std::optional<cbor::item> const first)
{
    check_integers(kind, it, integer_types{});

    std::expected<double, error> const d = double_of(kind);
    auto const got_double = it.get<double>();
    check_result(got_double, d);
    if (d)
        CHECK_EQ(*got_double, *d);

    auto const got_bool = it.get<bool>();
    check_result(got_bool, kind == cbor::type::false_value || kind == cbor::type::true_value
                               ? std::expected<bool, error>(kind == cbor::type::true_value)
                               : std::unexpected(error::incorrect_type));
    if (got_bool)
        CHECK_EQ(*got_bool, kind == cbor::type::true_value);

    auto const null = it.get<std::nullptr_t>();
    check_result(null, kind == cbor::type::null ? std::expected<std::nullptr_t, error>(nullptr)
                                                : std::unexpected(error::incorrect_type));

    std::expected<cbor::simple_value, error> const s = simple_of(kind);
    auto const got_simple = it.get<cbor::simple_value>();
    check_result(got_simple, s);
    if (s)
        CHECK_EQ(*got_simple, *s);

    auto const text = it.get<std::string_view>();
    check_result(text, kind == cbor::type::text_string ? std::expected<int, error>(0)
                                                       : std::unexpected(error::incorrect_type));
    if (text)
        CHECK_EQ(*text, "IETF"sv);

    auto const bytes = it.get<std::span<std::byte const>>();
    check_result(bytes, kind == cbor::type::byte_string ? std::expected<int, error>(0)
                                                        : std::unexpected(error::incorrect_type));
    if (bytes)
        CHECK(std::ranges::equal(*bytes, std::array{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}));

    // RFC 8746 2: the tag 64 is a typed array of uint8, and its content is the byte string of the elements.
    auto const typed = it.get<cbor::typed_array>();
    check_result(typed, kind == cbor::type::typed_array ? std::expected<int, error>(0)
                                                        : std::unexpected(error::incorrect_type));
    if (typed) {
        CHECK_EQ(typed->tag, 64u);
        CHECK(std::ranges::equal(typed->bytes, std::array{std::byte{1}, std::byte{2}}));
    }

    auto const elements = it.elements();
    check_result(elements, kind == cbor::type::array ? std::expected<int, error>(0)
                                                     : std::unexpected(error::incorrect_type));
    if (elements) {
        REQUIRE_EQ(elements->size(), 3u);
        auto const elements_number = elements->next(2).front().get<int>();
        CHECK_EQ(elements_number.value(), 3);
    }

    auto const map = it.pairs();
    check_result(map, kind == cbor::type::map ? std::expected<int, error>(0) : std::unexpected(error::incorrect_type));
    if (map) {
        REQUIRE_EQ(map->size(), 1u);
        auto const key = std::get<0>(map->front()).get<int>();
        CHECK_EQ(key.value(), 1);
        auto const value = std::get<1>(map->front()).get<int>();
        CHECK_EQ(value.value(), 2);
    }

    // RFC 8949 3.4: the tag 1 sample has the number 1; a bignum has the number 2 or 3 (3.4.3); the shared reference
    // 29(0) has the index 0, and its target is the first element, which the tag 28 marks.
    auto const tagged = it.get<pair>();
    bool const is_pair = kind == cbor::type::tag || kind == cbor::type::unsigned_bignum ||
                         kind == cbor::type::negative_bignum || kind == cbor::type::shared_reference;
    check_result(tagged, is_pair ? std::expected<int, error>(0) : std::unexpected(error::incorrect_type));
    if (tagged) {
        std::uint64_t const number = kind == cbor::type::tag               ? 1
                                     : kind == cbor::type::unsigned_bignum ? 2
                                     : kind == cbor::type::negative_bignum ? 3
                                                                           : 0;
        CHECK_EQ(tagged->first, number);
        if (kind == cbor::type::shared_reference) {
            CHECK(first == tagged->second);
        } else if (kind == cbor::type::tag) {
            auto const epoch = tagged->second.get<std::uint64_t>();
            CHECK_EQ(epoch.value(), 1363896240u);
        } else {
            auto const magnitude = tagged->second.get<std::span<std::byte const>>();
            CHECK_EQ(magnitude->size(), 9u);
        }
    }
}

// Only cbor::decode_cycles makes a shared_reference node; cbor::decode reads its sample as tags.
cbor::result<cbor::owning<cbor::item>> tree_of(sample const &s)
{
    if (s.type == cbor::type::shared_reference)
        return cbor::decode_cycles(s.encoded);
    return cbor::decode(s.encoded);
}

cbor::item node_of(sample const &s, cbor::owning<cbor::item> const &tree)
{
    if (s.type != cbor::type::shared_reference)
        return *tree;
    auto const tree_elements = tree->elements();
    return tree_elements.value().next().front();
}

// get<T> is defined for every pair of a kind of node and a type T. The kinds are the 19 members of cbor::type and the
// types are a finite set, so every pair is checked. The expectations come from RFC 8949 and RFC 8746, not from the code.
TEST_CASE("item get<T>: every kind of node read as every type")
{
    for (sample const &s : samples) {
        CAPTURE(std::to_underlying(s.type));
        auto const tree = tree_of(s);
        REQUIRE(tree.has_value());
        cbor::item const it = node_of(s, *tree);
        std::optional<cbor::item> const first =
            s.type == cbor::type::shared_reference ? std::optional<cbor::item>(**tree) : std::nullopt;
        check_every_type(s.type, it, first);
    }
}

std::string head_of(int const major, std::uint64_t const argument)
{
    std::string head(1, static_cast<char>(major << 5 | 27));
    for (int shift = 56; shift >= 0; shift -= 8)
        head.push_back(static_cast<char>((argument >> shift) & 0xff));
    return head;
}

constexpr int unsigned_major = 0;
constexpr int negative_major = 1;
constexpr std::uint64_t all_bits = std::numeric_limits<std::uint64_t>::max();

template <class T>
cbor::result<T> read_integer(int const major, std::uint64_t const argument)
{
    auto const tree = cbor::decode(head_of(major, argument));
    REQUIRE(tree.has_value());
    return (*tree)->get<T>();
}

template <class T>
void check_out_of_range(cbor::result<T> const &got)
{
    REQUIRE_FALSE(got.has_value());
    CHECK_EQ(got.error(), error::number_out_of_range);
}

template <class T>
void check_bounds()
{
    CAPTURE(sizeof(T));
    CAPTURE(std::numeric_limits<T>::is_signed);
    auto const max = static_cast<std::uint64_t>(std::numeric_limits<T>::max());
    auto const zero = read_integer<T>(unsigned_major, 0);
    CHECK(zero.value() == T{});
    auto const largest = read_integer<T>(unsigned_major, max);
    CHECK(largest.value() == std::numeric_limits<T>::max());
    if (max != all_bits)
        check_out_of_range(read_integer<T>(unsigned_major, max + 1));
    auto const all = read_integer<T>(unsigned_major, all_bits);
    if (max == all_bits)
        CHECK(all.value() == std::numeric_limits<T>::max());
    else
        check_out_of_range(all);
    // RFC 8949 3.1: the value of major type 1 is -1 minus the argument. So -1 is the argument 0, the minimum of a
    // signed type is the argument max, and the argument 2^64 - 1 is -2^64, which no type of 64 bits holds.
    if constexpr (std::numeric_limits<T>::is_signed) {
        auto const minus_one = read_integer<T>(negative_major, 0);
        CHECK(minus_one.value() == T{-1});
        auto const smallest = read_integer<T>(negative_major, max);
        CHECK(smallest.value() == std::numeric_limits<T>::min());
        check_out_of_range(read_integer<T>(negative_major, max + 1));
    } else {
        check_out_of_range(read_integer<T>(negative_major, 0));
    }
    check_out_of_range(read_integer<T>(negative_major, all_bits));
}

template <class... T>
void check_every_bound(types<T...>)
{
    (check_bounds<T>(), ...);
}

// Each bound of each integer type is a place where a range check can be off by one: 0, max, max + 1, -1, min, min - 1,
// 2^64 - 1 and -2^64.
TEST_CASE("item get<T>: the bounds of every integer type")
{
    check_every_bound(integer_types{});
}

// A double has 53 bits of significand (IEEE 754 binary64). An integer that it does not hold exactly is out of range,
// as for cbor::lazy::get<double>.
TEST_CASE("item get<double>: an integer that a double holds and one that it does not")
{
    constexpr std::uint64_t two_to_53 = std::uint64_t{1} << 53;
    auto const exact = read_integer<double>(unsigned_major, two_to_53);
    CHECK_EQ(exact.value(), 9007199254740992.0);
    check_out_of_range(read_integer<double>(unsigned_major, two_to_53 + 1));
    check_out_of_range(read_integer<double>(negative_major, two_to_53));
    auto const lowest = read_integer<double>(negative_major, all_bits);
    CHECK_EQ(lowest.value(), -18446744073709551616.0);
    auto const half = cbor::decode("\xf9\xc4\x00"sv);
    auto const half_real = (*half)->get<double>();
    CHECK_EQ(half_real.value(), -4.0);
    auto const single = cbor::decode("\xfa\x7f\x7f\xff\xff"sv);
    auto const single_real = (*single)->get<double>();
    CHECK_EQ(single_real.value(), 3.4028234663852886e+38);
    auto const wide = cbor::decode("\xfb\xc0\x10\x66\x66\x66\x66\x66\x66"sv);
    auto const real = (*wide)->get<double>();
    CHECK_EQ(real.value(), -4.1);
    auto const bignum = cbor::decode("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"sv);
    check_out_of_range((*bignum)->get<double>());
}

// RFC 8949 3.4: a decoder passes a tag that it does not interpret. get passes the tags that cbor::lazy::get passes.
// A tag 24 is not passed: its content is a byte string, and cbor::encoded_cbor_data_item gives the item in it. A tag
// 55799 inside an item is a tag of its own (RFC 8949 3.4.6) and is passed. The tag 1 in 28(1(29(0))) holds a shared
// reference to itself after cbor::decode_cycles; a shared reference is not passed, so the call ends.
TEST_CASE("item get<T>: the tags that get passes")
{
    auto const epoch = cbor::decode("\xc1\x05"sv);
    auto const epoch_number = (*epoch)->get<int>();
    CHECK_EQ(epoch_number.value(), 5);
    auto const embedded = cbor::decode("\xd8\x18\x41\x01"sv);
    auto const embedded_number = (*embedded)->get<int>();
    CHECK_EQ(embedded_number.error(), error::incorrect_type);
    auto const bytes = (*embedded)->get<std::span<std::byte const>>();
    CHECK_EQ(bytes.error(), error::incorrect_type);
    auto const embedded_tagged = (*embedded)->get<pair>();
    CHECK_EQ(embedded_tagged->first, 24u);
    auto const self_described = cbor::decode("\x81\xd9\xd9\xf7\x07"sv);
    auto const self_described_elements = (*self_described)->elements();
    auto const self_described_elements_number = self_described_elements.value().front().get<int>();
    CHECK_EQ(self_described_elements_number.value(), 7);
    auto const loop = cbor::decode_cycles("\xd8\x1c\xc1\xd8\x1d\x00"sv);
    REQUIRE(loop.has_value());
    auto const number = (*loop)->get<int>();
    CHECK_EQ(number.error(), error::incorrect_type);
    auto const tagged = (*loop)->get<pair>();
    CHECK_EQ(tagged->first, 1u);
}

// RFC 8949 3.4.3: the content of a bignum is a byte string. A tree keeps a tag 2 over another content as a bignum
// node, and get reports the content as cbor::lazy::get does.
TEST_CASE("item get<T>: a bignum over a text")
{
    auto const tree = cbor::decode("\xc2\x61\x61"sv);
    REQUIRE(tree.has_value());
    auto const number = (*tree)->get<int>();
    CHECK_EQ(number.error(), error::inadmissible_type_for_tag_content);
    auto const real = (*tree)->get<double>();
    CHECK_EQ(real.error(), error::inadmissible_type_for_tag_content);
}

template <class T>
void check_agreement(cbor::item const it, cbor::lazy const &doc)
{
    auto const tree = it.get<T>();
    auto const lazy = doc.get<T>();
    REQUIRE_EQ(tree.has_value(), lazy.has_value());
    if (!tree) {
        CHECK_EQ(tree.error(), lazy.error());
        return;
    }
    if constexpr (std::same_as<T, std::span<std::byte const>>)
        CHECK(std::ranges::equal(*tree, **lazy));
    else if constexpr (std::same_as<T, std::string_view>)
        CHECK_EQ(*tree, **lazy);
    else if constexpr (std::same_as<T, cbor::typed_array>) {
        CHECK_EQ(tree->tag, (*lazy)->tag);
        CHECK(std::ranges::equal(tree->bytes, (*lazy)->bytes));
    } else
        CHECK(*tree == *lazy);
}

template <class... T>
void check_agreements(cbor::item const it, cbor::lazy const &doc, types<T...>)
{
    (check_agreement<T>(it, doc), ...);
}

// A program reads a value either from a tree or from a lazy, and both must give the same value or the same error. The
// exceptions are a ref against a plain view, a tag 24, which a lazy passes into its content, and the tags 28 and 29,
// which cbor::decode_cycles resolves and a lazy passes.
TEST_CASE("item get<T> and lazy get<T> agree")
{
    for (sample const &s : samples) {
        if (s.type == cbor::type::shared_reference)
            continue;
        CAPTURE(std::to_underlying(s.type));
        auto const tree = cbor::decode(s.encoded);
        REQUIRE(tree.has_value());
        auto const doc = cbor::lazy::view(std::make_shared<std::string const>(s.encoded));
        REQUIRE(doc.has_value());
        check_agreements(**tree, *doc, integer_types{});
        check_agreements(**tree, *doc,
                         types<double, bool, std::nullptr_t, cbor::simple_value, std::string_view,
                               std::span<std::byte const>, cbor::typed_array>{});
    }
}

// cbor::encode counts the bytes that it wrote to memory. Such a count is a std::size_t, as
// std::string::size and std::span::size give it. The expectation comes from that decision, not from the header.
TEST_CASE("encode: the count of bytes written is a std::size_t")
{
    CHECK((std::is_same_v<decltype(cbor::encode(std::declval<std::string &>(), std::declval<cbor::item const >())),
                          std::expected<std::size_t, cbor::error>>));
}

// The declaration of elements() and pairs() is auto: the doc comment states the element types, and this test holds
// the declaration to that statement.
TEST_CASE("item elements and pairs: the element types of the ranges")
{
    CHECK((std::same_as<std::ranges::range_value_t<decltype(std::declval<cbor::item>().elements())::value_type>,
                        cbor::item>));
    CHECK((std::same_as<std::ranges::range_value_t<decltype(std::declval<cbor::item>().pairs())::value_type>,
                        std::pair<cbor::item, cbor::item>>));
}

// RFC 8949 3.1: an array of no elements and a map of no pairs are valid. Their ranges are empty, and not an error.
TEST_CASE("item elements and pairs: an empty array and an empty map")
{
    auto const tree = cbor::parse_edn("[[], {}]");
    REQUIRE(tree.has_value());
    auto const outer = (*tree)->elements();
    REQUIRE(outer.has_value());
    auto const outer_elements = outer->front().elements();
    CHECK(outer_elements->empty());
    auto const inner_pairs = std::ranges::next(outer->begin()).operator*().pairs();
    CHECK(inner_pairs->empty());
}

// get<T>() passes a tag that it does not interpret (item.hpp, the table of get). elements() and pairs() give the
// content of the array and the map under such a tag the same way, and a tag of an integer is still no array.
TEST_CASE("item elements and pairs: a tag that is passed")
{
    auto const tree = cbor::parse_edn(R"([1([1, 2]), 1({"a": 1}), 1(5)])");
    REQUIRE(tree.has_value());
    auto const outer = (*tree)->elements();
    REQUIRE(outer.has_value());
    auto it = outer->begin();
    auto const inner_elements = (*it).elements();
    CHECK_EQ(inner_elements->size(), 2u);
    ++it;
    auto const inner_pairs = (*it).pairs();
    CHECK_EQ(inner_pairs->size(), 1u);
    ++it;
    auto const it_elements = (*it).elements();
    CHECK_EQ(it_elements.error(), cbor::error::incorrect_type);
    auto const it_pairs = (*it).pairs();
    CHECK_EQ(it_pairs.error(), cbor::error::incorrect_type);
}

}

namespace
{

struct bytes_item {
    std::uint64_t a;
    std::vector<std::uint64_t> b;
    bool operator==(bytes_item const &) const = default;
};

std::vector<std::uint8_t> unsigned_bytes_of_bytes_item(std::string const &bytes)
{
    std::vector<std::uint8_t> out(bytes.size());
    std::ranges::transform(std::as_bytes(std::span(bytes)), out.begin(),
                           [](std::byte const b) { return std::to_integer<std::uint8_t>(b); });
    return out;
}

} // namespace

CBOR_DEFINE_TYPE_MEMBERS(bytes_item, cbor::untagged, a, b);

// A byte range reads as the std::string of the same bytes. The bytes come from the encoder of the library, and the
// test reads them as std::vector<std::uint8_t> and as std::span<std::byte const>, the forms that other CBOR libraries take.
TEST_CASE("decode, item_size, diagnostic_notation: a range of bytes reads as the std::string of the same bytes")
{
    auto const encoded = cbor::encode<bytes_item>(bytes_item{1, {2, 3}});
    REQUIRE(encoded.has_value());
    auto const u = unsigned_bytes_of_bytes_item(*encoded);
    auto const b = std::as_bytes(std::span(*encoded));
    auto const text = cbor::diagnostic_notation(*encoded);
    REQUIRE(text.has_value());
    CHECK_EQ(cbor::diagnostic_notation(u).value(), *text);
    CHECK_EQ(cbor::diagnostic_notation(b).value(), *text);
    CHECK_EQ(cbor::item_size(u), encoded->size());
    auto const length = cbor::item_size(b);
    CHECK_EQ(length.value(), encoded->size());
    auto const from_u = cbor::decode(u);
    auto const from_b = cbor::decode(b);
    REQUIRE(from_u.has_value());
    REQUIRE(from_b.has_value());
    std::string out_u;
    std::string out_b;
    REQUIRE(cbor::encode(out_u, **from_u).has_value());
    REQUIRE(cbor::encode(out_b, **from_b).has_value());
    CHECK_EQ(out_u, *encoded);
    CHECK_EQ(out_b, *encoded);
}

// cbor::encode appends to a std::string. The writer grows that string while it writes, so the bytes that the string
// held before must stay, a second encode must follow the first, and the string must hold no byte past the item.
// A long text makes the string grow in the middle of an item.
TEST_CASE("cbor::encode of a tree appends to a string that holds bytes, also twice")
{
    std::string const text = "[\"" + std::string(100000, 'x') + "\", 1, {\"a\": [2.5, -3]}]";
    auto const tree = cbor::parse_edn(text);
    REQUIRE(tree.has_value());
    std::string alone;
    REQUIRE(cbor::encode(alone, **tree).has_value());
    std::string room(alone.size(), '\0');
    REQUIRE(cbor::encode(std::span<char>(room), **tree).has_value());
    CHECK_EQ(room, alone);
    std::string out = "pre";
    auto const first = cbor::encode(out, **tree);
    auto const second = cbor::encode(out, **tree);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK_EQ(*first, alone.size());
    CHECK_EQ(*second, alone.size());
    CHECK_EQ(out, "pre" + alone + alone);
}
