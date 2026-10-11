#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

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
