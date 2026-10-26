#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

// Each test in this file is one example of a page in doc/reference/structs/. The tests exist so that every
// example compiles and gives the result that the page states. The input of each example is built with
// preferred() from the text in the comment of the page.

#ifdef __cpp_impl_reflection

namespace reference_structs
{

struct person {
    std::string name;
    int age;
};

struct header {
    std::string content_type;
};

struct senml {
    std::string n;
    double v;
};

struct account {
    std::string user;
    std::uint8_t level;
};

struct reading {
    std::uint8_t id;
    std::optional<std::uint8_t> unit;
};

struct renamed {
    std::uint8_t number;
};

struct [[=cbor::tag(1802)]] guarded {
    std::uint8_t id;
    [[=cbor::skip{}]] std::uint8_t secret;
};

struct [[=cbor::tag(1803)]] [[=cbor::allowlist{}]] login {
    [[=cbor::allow{}]] std::uint8_t id;
    std::uint8_t secret;
};

} // namespace reference_structs

template <>
struct cbor::describe_members<reference_structs::renamed> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1801};
    static constexpr std::tuple members{
        cbor::member{"id", &reference_structs::renamed::number},
    };
};

template <>
struct cbor::describe_members<reference_structs::header> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160060};
    static constexpr std::tuple members{
        cbor::member{"content-type", &reference_structs::header::content_type},
    };
};

template <>
struct cbor::describe_members<reference_structs::senml> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{0, &reference_structs::senml::n},
        cbor::member{2, &reference_structs::senml::v},
    };
};

template <>
struct cbor::describe_members<reference_structs::account> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        CBOR_MEMBER(reference_structs::account, user),
        CBOR_MEMBER(reference_structs::account, level),
    };
};

using reference_structs::account;
using reference_structs::guarded;
using reference_structs::header;
using reference_structs::login;
using reference_structs::person;
using reference_structs::reading;
using reference_structs::renamed;
using reference_structs::senml;

// encode.md: encode(value) gives the bytes of a map with the member names as keys, sorted by RFC 8949 4.2.1.
TEST_CASE("doc reference structs: encode a value")
{
    auto const bytes = cbor::structs<person>::encode(person{"ann", 30});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::preferred(test::M("age"s, 30, "name"s, "ann"s)));
}

// encode.md: encode(value, target) appends to the target and returns the count of bytes that it appended.
TEST_CASE("doc reference structs: encode appends to a target")
{
    std::string out = "\x01"s;
    auto const n = cbor::structs<std::uint8_t>::encode(2, out);
    REQUIRE(n.has_value());
    CHECK_EQ(*n, 1u);
    CHECK_EQ(out, "\x01\x02"s);
}

// encode.md, section Errors: a span that is too small for the bytes gives no_buffer_space.
TEST_CASE("doc reference structs: encode into a span that is too small")
{
    std::array<char, 2> small{};
    auto const n = cbor::structs<std::string>::encode("abcdef"s, std::span<char>(small));
    REQUIRE_FALSE(n.has_value());
    CHECK_EQ(n.error(), cbor::error::no_buffer_space);
}

// decode.md: the example moves the string in, and the result holds the value.
TEST_CASE("doc reference structs: decode a moved string")
{
    std::string bytes = test::preferred(test::M("name"s, "ann"s, "age"s, 30));
    auto const p = cbor::structs<person>::decode(std::move(bytes));
    REQUIRE(p.has_value());
    CHECK_EQ((*p)->age, 30);
    CHECK_EQ((*p)->name, "ann");
}

// decode.md: the forms with an owner read the bytes in place; a view in the result points into the owner.
TEST_CASE("doc reference structs: decode with an owner")
{
    auto const owner = std::make_shared<std::string const>(test::preferred(test::V("abc"s)));
    auto const shared = cbor::structs<std::string_view>::decode(owner);
    REQUIRE(shared.has_value());
    CHECK_EQ(**shared, "abc");
    CHECK_EQ((**shared).data(), owner->data() + 1);

    std::string_view const part(*owner);
    auto const viewed = cbor::structs<std::string_view>::decode(std::shared_ptr<void const>(owner), part);
    REQUIRE(viewed.has_value());
    CHECK_EQ(**viewed, "abc");

    auto const into = cbor::structs<std::vector<std::string>>::decode(
        std::make_shared<std::string const>(test::preferred(test::A("a"s, "b"s))), std::vector<std::string>{"z"});
    REQUIRE(into.has_value());
    CHECK_EQ(**into, std::vector<std::string>{"a", "b"});

    CHECK_EQ(cbor::structs<std::vector<int>>::decode(test::preferred(test::A(1, 2))).error(),
             cbor::error::incorrect_type);
}

// decode.md, section Errors: a missing key, bytes after the item, and an input that ends early.
TEST_CASE("doc reference structs: decode errors")
{
    CHECK_EQ(cbor::structs<person>::decode(test::preferred(test::M("name"s, "ann"s))).error(),
             cbor::error::key_not_found);
    auto const first = cbor::structs<int>::decode(test::preferred(test::V(1)) + "\x01"s);
    REQUIRE(first.has_value());
    CHECK_EQ(**first, 1);
    CHECK_EQ(cbor::structs<std::string>::decode("\x63" "ab"s).error(), cbor::error::too_little_data);
    CHECK_EQ(cbor::structs<int>::decode(test::preferred(test::V("x"s))).error(), cbor::error::incorrect_type);
    CHECK_EQ(cbor::structs<std::uint8_t>::decode(test::preferred(test::V(256))).error(),
             cbor::error::number_out_of_range);
    CHECK_EQ(cbor::structs<person>::decode(test::preferred(test::M("age"s, 1, "age"s, 2, "name"s, "a"s))).error(),
             cbor::error::duplicate_key);
}

// decode.md, section Errors: an empty owner is a wrong use and throws std::logic_error.
TEST_CASE("doc reference structs: decode with an empty owner throws")
{
    CHECK_THROWS_AS(std::ignore = cbor::structs<int>::decode(std::shared_ptr<std::string const>()), std::logic_error);
    CHECK_THROWS_AS(std::ignore = cbor::structs<int>::decode(std::shared_ptr<void const>(), std::string_view("\x01")),
                    std::logic_error);
}

// key.md: cbor::key gives a member of a schema struct another key; the lazy form takes an integer key.
TEST_CASE("doc reference structs: cbor::key")
{
    auto bytes = cbor::schema<renamed>::encode(renamed{7});
    REQUIRE(bytes.has_value());
    CHECK_NE(bytes->find("\x62id"s), std::string::npos);
    CHECK_EQ(bytes->find("number"s), std::string::npos);
    auto const back = cbor::schema<renamed>::decode(std::move(*bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->number, 7u);

    auto const doc = cbor::lazy::take_ownership(test::preferred(test::M(7, "seven"s))).value();
    auto const seven = doc.at(cbor::key{7})->get<std::string_view>();
    REQUIRE(seven.has_value());
    CHECK_EQ(**seven, "seven");
}

// skip.md: a skipped member is not written, and decode leaves its default value.
TEST_CASE("doc reference structs: cbor::skip")
{
    auto bytes = cbor::schema<guarded>::encode(guarded{7, 42});
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->find("secret"s), std::string::npos);
    auto const back = cbor::schema<guarded>::decode(std::move(*bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->id, 7u);
    CHECK_EQ((*back)->secret, 0u);
}

// allowlist.md and allow.md: under cbor::allowlist only the members with cbor::allow are written.
TEST_CASE("doc reference structs: cbor::allowlist and cbor::allow")
{
    auto bytes = cbor::schema<login>::encode(login{7, 42});
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->find("secret"s), std::string::npos);
    auto const back = cbor::schema<login>::decode(std::move(*bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->id, 7u);
    CHECK_EQ((*back)->secret, 0u);
}

// describe_members.md: a description gives the tag and the text key of each member.
TEST_CASE("doc reference structs: cbor::describe_members")
{
    auto const bytes = cbor::structs<header>::encode(header{"text/plain"});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::preferred(test::value{test::tagged{160060, test::M("content-type"s, "text/plain"s)}}));
    CHECK_EQ(cbor::structs<header>::decode(test::preferred(test::M("content-type"s, "x"s))).error(),
             cbor::error::incorrect_type);
}

// member.md: cbor::member takes an integer key; cbor::untagged gives a map with no tag.
TEST_CASE("doc reference structs: cbor::member with integer keys")
{
    auto const bytes = cbor::structs<senml>::encode(senml{"t", 1.5});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::preferred(test::M(0, "t"s, 2, 1.5)));
}

// CBOR_MEMBER.md: the macro takes the name of the member as its key.
TEST_CASE("doc reference structs: CBOR_MEMBER")
{
    auto const bytes = cbor::structs<account>::encode(account{"ann", 2});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, test::preferred(test::M("user"s, "ann"s, "level"s, 2)));
}

// types.md: one row of each kind that the table shows with a tag, and an empty optional member.
TEST_CASE("doc reference structs: member types")
{
    CHECK_EQ(*cbor::structs<std::set<std::uint8_t>>::encode({3, 1}),
             test::preferred(test::value{test::tagged{258, test::A(1, 3)}}));
    CHECK_EQ(*cbor::structs<std::array<std::byte, 2>>::encode({std::byte{1}, std::byte{2}}),
             test::preferred(test::value{test::tagged{64, test::value{test::bytes{"\x01\x02"s}}}}));
    CHECK_EQ(*cbor::structs<std::chrono::sys_seconds>::encode(std::chrono::sys_seconds{std::chrono::seconds{1700000000}}),
             test::preferred(test::value{test::tagged{1, test::value{std::uint64_t{1700000000}}}}));
    CHECK_EQ(*cbor::structs<reading>::encode(reading{4, std::nullopt}), test::preferred(test::M("id"s, 4)));
    auto const back = cbor::structs<reading>::decode(test::preferred(test::M("id"s, 4)));
    REQUIRE(back.has_value());
    CHECK_FALSE((*back)->unit.has_value());
}

#endif
