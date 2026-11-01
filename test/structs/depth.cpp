#include "../owned_copy.hpp"
#include <cbor/error.hpp>
#include <cbor/structs.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace std::string_literals;
using cbor::error;

namespace structs_depth
{
namespace
{

struct node {
    std::vector<node> c;
};

struct optional_node {
    std::optional<std::vector<optional_node>> c;
};

// n steps of node: n maps, each holding the array of the next; the root map is at depth 0 (RFC 8949: each array or
// map is one level), the map of step i at 2i, and the innermost empty array at 2n - 1.
node nested(std::size_t const steps)
{
    node root;
    node *at = &root;
    for (std::size_t i = 1; i < steps; ++i)
        at = &at->c.emplace_back();
    return root;
}

std::string nested_bytes(std::size_t const steps)
{
    std::string bytes;
    for (std::size_t i = 1; i < steps; ++i)
        bytes += "\xa1\x61" "c\x81"s;
    return bytes + "\xa1\x61" "c\x80"s;
}

// k steps of optional_node: the innermost map has no member, so it is an empty map at depth 2k.
optional_node nested_optional(std::size_t const steps)
{
    optional_node root;
    optional_node *at = &root;
    for (std::size_t i = 0; i < steps; ++i)
        at = &at->c.emplace().emplace_back();
    return root;
}

std::string nested_optional_bytes(std::size_t const steps)
{
    std::string bytes;
    for (std::size_t i = 0; i < steps; ++i)
        bytes += "\xa1\x61" "c\x81"s;
    return bytes + "\xa0"s;
}

} // namespace
} // namespace structs_depth

using namespace structs_depth;

// The default nesting depth is 128: an item at depth 128 is read, one at 129 is nesting_depth_exceeded, for the
// encoder as for the decoder, so that what structs::encode writes structs::decode reads.
TEST_CASE("structs depth: a struct that holds a vector of itself, innermost item at depth 127 and 129")
{
    auto const at_127 = cbor::structs<node>::encode(nested(64));
    REQUIRE(at_127.has_value());
    CHECK_EQ(*at_127, nested_bytes(64));
    CHECK(cbor::structs<node>::decode(nested_bytes(64)).has_value());
    CHECK_EQ(cbor::structs<node>::encode(nested(65)).error(), error::nesting_depth_exceeded);
    CHECK_EQ(cbor::structs<node>::decode(nested_bytes(65)).error(), error::nesting_depth_exceeded);
}

TEST_CASE("structs depth: a struct with an optional vector of itself, innermost item at depth 128 and 130")
{
    auto const at_128 = cbor::structs<optional_node>::encode(nested_optional(64));
    REQUIRE(at_128.has_value());
    CHECK_EQ(*at_128, nested_optional_bytes(64));
    CHECK(cbor::structs<optional_node>::decode(nested_optional_bytes(64)).has_value());
    CHECK_EQ(cbor::structs<optional_node>::encode(nested_optional(65)).error(), error::nesting_depth_exceeded);
    CHECK_EQ(cbor::structs<optional_node>::decode(nested_optional_bytes(65)).error(), error::nesting_depth_exceeded);
}
