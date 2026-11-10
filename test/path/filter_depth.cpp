#include "../owned_copy.hpp"
#include "../reference/path.hpp"
#include "../reference/value.hpp"
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>

#include <cstddef>
#include <string>

using namespace std::string_literals;

// RFC 9535 2.3.5.2.2: !(x) is the negation of x, so 100 nested negations of @ == 1 select what @ == 1 selects. The
// document holds 100 nested arrays, the innermost [1, 2] (RFC 8949: depth 99, its elements at 100). RFC 9535 2.5.2:
// ..[?...] applies the filter to the children of every node, so the nodelist holds the one node 1. No fixed depth
// bounds the filter or the data; the stack of the main thread holds both, and they do not add up.
TEST_CASE("path filter depth: 100 nested negations over 100 nested arrays")
{
    constexpr std::size_t depth = 100;
    std::string filter = "@ == 1";
    for (std::size_t i = 0; i < depth; ++i)
        filter = "!(" + filter + ")";
    std::string const document = std::string(depth - 1, '\x81') + "\x82\x01\x02"s;
    auto const l = test::lazy_of_copy(document);
    REQUIRE(l.has_value());
    auto const nodes = test::query("$..[?" + filter + "]", *l);
    REQUIRE(nodes.has_value());
    CHECK(*nodes == A(1));
}
