#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "doc_types.hpp"
#include "reference.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace std::string_literals;

// Each test in this file is one example of a reference page of the structs group. The comments of structs.hpp and
// describe.hpp take the examples through the snippet markers. The tests exist so that every example compiles and gives
// the result that the page states. The input of each example is built with encode_in_preferred_form() from the text in the comment of
// the page.

using reference_structs::device;
using reference_structs::account;
using reference_structs::header;
using reference_structs::line;
using reference_structs::node;
using reference_structs::person;
using reference_structs::reading;
using reference_structs::senml;
using reference_structs::trailer;

// The snippet of the group structs, between the markers "structs group". The example on the page of the group structs
// writes a struct and reads it back. So a reader sees one round trip. The expected bytes are the ones that RFC 8949
// gives for the map, a text and an integer, and not the output of the library. The test checks each value that the
// comments of the example state.
TEST_CASE("doc reference structs: the example of the group structs")
{
    // [structs group]
    // person: struct { std::string name; int age; }, described by CBOR_DEFINE_TYPE_MEMBERS
    // encoded: 16 bytes, {"age": 30, "name": "ann"}
    auto const encoded = cbor::structs<person>::encode(person{"ann", 30});
    // back: a ref to person{"ann", 30}
    auto const back = cbor::structs<person>::decode(std::string(*encoded));
    auto const age = (*back)->age;    // 30
    auto const name = (*back)->name;  // "ann"
    // [structs group]
    REQUIRE(encoded.has_value());
    CHECK_EQ(*encoded, "\xa2\x63" "age" "\x18\x1e\x64" "name" "\x63" "ann"s);
    REQUIRE(back.has_value());
    CHECK_EQ(age, 30);
    CHECK_EQ(name, "ann");
}

// The snippet of the group encode, between the markers "encode group". The example on the page of the group encode
// writes a described struct, a container and the text of a value. So a reader sees that one call takes any value. The
// expected bytes and text are written from the values with the encoder of the tests, and not from the output under test.
TEST_CASE("doc reference structs: the example of the group encode")
{
    // [encode group]
    // person: struct { std::string name; int age; }, described by CBOR_DEFINE_TYPE_MEMBERS
    auto const bytes = cbor::encode<person>(person{"ann", 30});  // 16 bytes, {"age": 30, "name": "ann"}
    std::string out;
    auto const n = cbor::encode(out, std::map<std::string, int>{{"a", 1}});  // 4: the bytes of {"a": 1} appended
    auto const text = cbor::to_edn<person>(person{"ann", 30});   // "{\"age\": 30, \"name\": \"ann\"}"
    // [encode group]
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::encode_in_preferred_form(test::M("age"s, 30, "name"s, "ann"s)));
    REQUIRE(n.has_value());
    CHECK_EQ(*n, 4u);
    CHECK_EQ(out, test::encode_in_preferred_form(test::M("a"s, 1)));
    REQUIRE(text.has_value());
    CHECK_EQ(*text, "{\"age\": 30, \"name\": \"ann\"}");
}

// structs.hpp, snippet encode: encode(value) gives the bytes of a map with the member names as keys, sorted by
// RFC 8949 4.2.1.
TEST_CASE("doc reference structs: encode a value")
{
    // [encode]
    // person: struct { std::string name; int age; }, described by CBOR_DEFINE_TYPE_MEMBERS
    auto const bytes = cbor::encode<person>(person{"ann", 30});  // {"age": 30, "name": "ann"}
    // [encode]
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::encode_in_preferred_form(test::M("age"s, 30, "name"s, "ann"s)));
}

// structs.hpp, the comment of encode: encode(out, value) appends to out and returns the count of bytes
// that it appended.
TEST_CASE("doc reference structs: encode appends to a target")
{
    std::string out = "\x01"s;
    auto const n = cbor::encode<std::uint8_t>(out, 2);
    REQUIRE(n.has_value());
    CHECK_EQ(*n, 1u);
    CHECK_EQ(out, "\x01\x02"s);
}

// structs.hpp, the comment of encode: a span that is too small for the bytes gives no_buffer_space.
TEST_CASE("doc reference structs: encode into a span that is too small")
{
    std::array<char, 2> small{};
    auto const n = cbor::encode<person>(std::span<char>(small), person{"ann", 30});
    REQUIRE_FALSE(n.has_value());
    CHECK_EQ(n.error(), cbor::error::no_buffer_space);
}

// structs.hpp, snippet decode: the example moves the string in, and the result holds the value.
TEST_CASE("doc reference structs: decode a moved string")
{
    std::string bytes = test::encode_in_preferred_form(test::M("name"s, "ann"s, "age"s, 30));
    int age = 0;
    // [decode]
    // bytes: a std::string with {"name": "ann", "age": 30}
    auto const p = cbor::structs<person>::decode(std::move(bytes));
    if (p)
        age = (*p)->age;  // 30
    // [decode]
    CHECK_EQ(age, 30);
    REQUIRE(p.has_value());
    CHECK_EQ((*p)->age, 30);
    CHECK_EQ((*p)->name, "ann");
}

// structs.hpp, the comment of decode: the forms with an owner read the bytes in place; a view in the result points
// into the owner.
TEST_CASE("doc reference structs: decode with an owner")
{
    auto const owner = std::make_shared<std::string const>(test::encode_in_preferred_form(test::V("abc"s)));
    auto const shared = cbor::structs<std::string_view>::decode(owner);
    REQUIRE(shared.has_value());
    CHECK_EQ(**shared, "abc");
    CHECK_EQ((**shared).data(), std::span(*owner).subspan(1).data());

    std::string_view const part(*owner);
    auto const viewed = cbor::structs<std::string_view>::decode(std::shared_ptr<void const>(owner), part);
    REQUIRE(viewed.has_value());
    CHECK_EQ(**viewed, "abc");

    auto const into = cbor::structs<std::vector<std::string>>::decode(
        std::make_shared<std::string const>(test::encode_in_preferred_form(test::A("a"s, "b"s))), std::vector<std::string>{"z"});
    REQUIRE(into.has_value());
    CHECK_EQ(**into, std::vector<std::string>{"a", "b"});

    CHECK_EQ(cbor::structs<std::vector<int>>::decode(test::encode_in_preferred_form(test::A(1, 2))),
             std::unexpected(cbor::error::incorrect_type));
}

// structs.hpp, the comment of decode: a missing key, bytes after the item, and an input that ends early.
TEST_CASE("doc reference structs: decode errors")
{
    CHECK_EQ(cbor::structs<person>::decode(test::encode_in_preferred_form(test::M("name"s, "ann"s))),
             std::unexpected(cbor::error::key_not_found));
    auto const first = cbor::structs<int>::decode(test::encode_in_preferred_form(test::V(1)) + "\x01"s);
    REQUIRE(first.has_value());
    CHECK_EQ(**first, 1);
    CHECK_EQ(cbor::structs<std::string>::decode("\x63" "ab"s), std::unexpected(cbor::error::too_little_data));
    CHECK_EQ(cbor::structs<int>::decode(test::encode_in_preferred_form(test::V("x"s))), std::unexpected(cbor::error::incorrect_type));
    CHECK_EQ(cbor::structs<std::uint8_t>::decode(test::encode_in_preferred_form(test::V(256))),
             std::unexpected(cbor::error::number_out_of_range));
    auto const edn_bytes = cbor::diag2cbor(R"({"age": 1, "age": 2, "name": "a"})");
    std::string repeated = edn_bytes.value();
    CHECK_EQ(cbor::structs<person>::decode(std::move(repeated)), std::unexpected(cbor::error::duplicate_key));
}

// structs.hpp, the comment of decode: an empty owner is a wrong use and throws std::logic_error.
TEST_CASE("doc reference structs: decode with an empty owner throws")
{
    CHECK_THROWS_AS(std::ignore = cbor::structs<int>::decode(std::shared_ptr<std::string const>()), std::logic_error);
    CHECK_THROWS_AS(std::ignore = cbor::structs<int>::decode(std::shared_ptr<void const>(), std::string_view("\x01")),
                    std::logic_error);
}

// describe.hpp, the comment of describe_members: a description gives the tag and the text key of each member.
TEST_CASE("doc reference structs: cbor::describe_members")
{
    // [describe_members use]
    auto const bytes = cbor::encode<header>(header{"text/plain"});  // 160060({"content-type": "text/plain"})
    // [describe_members use]
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::encode_in_preferred_form(test::value{test::tagged{160060, test::M("content-type"s, "text/plain"s)}}));
    CHECK_EQ(cbor::structs<header>::decode(test::encode_in_preferred_form(test::M("content-type"s, "x"s))),
             std::unexpected(cbor::error::incorrect_type));
}

// describe.hpp, the comment of member: cbor::member takes an integer key; cbor::untagged gives a map with no tag.
TEST_CASE("doc reference structs: cbor::member with integer keys")
{
    // [member use]
    auto const bytes = cbor::encode<senml>(senml{"t", 1.5});  // {0: "t", 2: 1.5}
    // [member use]
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::encode_in_preferred_form(test::M(0, "t"s, 2, 1.5)));
}

// describe.hpp, the comment of CBOR_MEMBER: the macro takes the name of the member as its key.
TEST_CASE("doc reference structs: CBOR_MEMBER")
{
    // [CBOR_MEMBER use]
    auto const bytes = cbor::encode<account>(account{"ann", 2});  // {"user": "ann", "level": 2}
    // [CBOR_MEMBER use]
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::encode_in_preferred_form(test::M("user"s, "ann"s, "level"s, 2)));
}

// describe.hpp, the comment of CBOR_DEFINE_TYPE_MEMBERS: the macro takes the names of the members, and each name is
// its key.
TEST_CASE("doc reference structs: CBOR_DEFINE_TYPE_MEMBERS")
{
    // [CBOR_DEFINE_TYPE_MEMBERS use]
    auto const bytes = cbor::encode<device>(device{"lamp", 3});  // {"name": "lamp", "port": 3}
    // [CBOR_DEFINE_TYPE_MEMBERS use]
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::encode_in_preferred_form(test::M("name"s, "lamp"s, "port"s, 3)));
}

// structs.hpp, the comment of structs: one row of each kind that the table shows with a tag, and an empty optional
// member.
TEST_CASE("doc reference structs: member types")
{
    // [structs]
    // reading: struct { std::uint8_t id; std::optional<std::uint8_t> unit; }
    auto const one = cbor::encode<reading>(reading{4, std::nullopt});    // {"id": 4}
    auto const set = cbor::encode<std::set<std::uint8_t>>({3, 1});        // 258([1, 3])
    // [structs]
    CHECK_EQ(*one, test::encode_in_preferred_form(test::M("id"s, 4)));
    CHECK_EQ(*set, test::encode_in_preferred_form(test::value{test::tagged{258, test::A(1, 3)}}));
    CHECK_EQ(cbor::encode<std::set<std::uint8_t>>({3, 1}),
             test::encode_in_preferred_form(test::value{test::tagged{258, test::A(1, 3)}}));
    CHECK_EQ(cbor::encode<std::array<std::byte, 2>>({std::byte{1}, std::byte{2}}),
             test::encode_in_preferred_form(test::value{test::tagged{64, test::value{test::bytes{"\x01\x02"s}}}}));
    CHECK_EQ(cbor::encode<std::chrono::sys_seconds>(std::chrono::sys_seconds{std::chrono::seconds{1700000000}}),
             test::encode_in_preferred_form(test::value{test::tagged{1, test::value{std::uint64_t{1700000000}}}}));
    CHECK_EQ(cbor::encode<reading>(reading{4, std::nullopt}), test::encode_in_preferred_form(test::M("id"s, 4)));
    auto const back = cbor::structs<reading>::decode(test::encode_in_preferred_form(test::M("id"s, 4)));
    REQUIRE(back.has_value());
    CHECK_FALSE((*back)->unit.has_value());
}

// structs.hpp, the table of member types: a struct in a struct is a map in a map, and each point keeps its own tag
// 1802. A struct that holds a std::vector of itself is a map whose arrays hold maps of the same form. Both read back.
TEST_CASE("doc reference structs: a struct in a struct and a struct that holds itself")
{
    auto const l = cbor::encode<line>(line{{1, 2}, {3, 4}});
    CHECK_EQ(*l, test::encode_in_preferred_form(test::M("to"s, test::value{test::tagged{1802, test::M("x"s, 3.0, "y"s, 4.0)}},
                                                        "from"s, test::value{test::tagged{1802, test::M("x"s, 1.0, "y"s, 2.0)}})));
    auto const line_back = cbor::structs<line>::decode(std::string(*l));
    REQUIRE(line_back.has_value());
    CHECK_EQ((*line_back)->to.y, 4.0f);
    auto const n = cbor::encode<node>(node{"root", {node{"a", {}}}});
    CHECK_EQ(*n, test::encode_in_preferred_form(test::M("name"s, "root"s, "children"s,
                                                        test::A(test::M("name"s, "a"s, "children"s, test::A())))));
    auto const node_back = cbor::structs<node>::decode(std::string(*n));
    REQUIRE(node_back.has_value());
    REQUIRE_EQ((*node_back)->children.size(), 1u);
    CHECK_EQ((*node_back)->children[0].name, "a");
}

// describe.hpp, the comment of describe_members: CBOR_DEFINE_TYPE writes the description; 160061 = 0x0002713d,
// "checksum" is 68 + 8 bytes.
TEST_CASE("doc reference structs: CBOR_DEFINE_TYPE")
{
    // [CBOR_DEFINE_TYPE use]
    // trailer: struct { std::uint8_t checksum; }
    auto const bytes = cbor::encode<trailer>(trailer{7});  // 160061({"checksum": 7})
    // [CBOR_DEFINE_TYPE use]
    REQUIRE(bytes.has_value());
    using namespace std::string_literals;
    CHECK_EQ(*bytes, "\xda\x00\x02\x71\x3d\xa1\x68"
                     "checksum\x07"s);
}

