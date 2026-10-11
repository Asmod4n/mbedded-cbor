#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "limits_guard.hpp"

#include <cstdint>
#include <string>
#include <string_view>

// Each test in this file is one example of README.md. The tests exist so that every example in the README
// compiles and gives the result that the README states.

// The section "Quick start" reads two values from bytes, once by a path and once by steps.
TEST_CASE("doc readme: quick start")
{
    using namespace std::string_literals;

    // {"user": {"id": 42, "name": "ann"}}
    std::string const bytes = "\xa1\x64user\xa2\x62id\x18\x2a\x64name\x63" "ann"s;

    auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
    CHECK_EQ(id, 42);

    auto const doc = cbor::lazy::from(bytes);
    auto const name = doc->at("user")
                          .and_then([](cbor::lazy const &u) { return u.at("name"); })
                          .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    REQUIRE(name.has_value());
    CHECK_EQ(**name, std::string_view{"ann"});

    auto const missing = cbor::at_path<"$.user.age", std::int64_t>(bytes);
    CHECK_EQ(missing.error(), cbor::error::key_not_found);
}

#ifdef __cpp_impl_reflection

namespace doc_readme
{

struct inner
{
    std::int64_t b;
};

struct outer
{
    inner a;
};

}

// The section "Write a struct and read one value by a path" writes a nested struct with structs and reads the inner
// member back by a path. The test exists because a user combines the two parts and no other example shows both.
TEST_CASE("doc readme: write a struct and read by a path")
{
    using namespace std::string_literals;

    auto const bytes = cbor::structs<doc_readme::outer>::encode(doc_readme::outer{{5}});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, "\xa1\x61"
                     "a\xa1\x61"
                     "b\x05"s);
    auto const b = cbor::at_path<"$.a.b", std::int64_t>(*bytes);
    CHECK_EQ(b, 5);
}

#endif

// The section "Set a limit" sets one field. The test exists so that the README states the error that the limit gives
// and that the other fields keep their values.
TEST_CASE("doc readme: set a limit")
{
    using namespace std::string_literals;

    test::limits_guard const g{{}};
    cbor::limits.container_elements = 100;
    CHECK_EQ(cbor::limits.container_elements, 100u);
    CHECK_EQ(cbor::limits.nesting_depth, 128u);

    std::string bytes = "\x98\x65"s;
    bytes.append(101, '\x01');
    CHECK_EQ(cbor::lazy::from(bytes)->size().error(), cbor::error::container_elements_exceeded);
    bytes = "\x98\x64"s;
    bytes.append(100, '\x01');
    CHECK_EQ(cbor::lazy::from(bytes)->size(), 100u);
}
