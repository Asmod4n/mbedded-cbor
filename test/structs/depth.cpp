#include "../owned_copy.hpp"
#include "../small_stack.hpp"
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

// A struct that holds itself nests as deep as its value. 64 steps are a depth that every stack holds: encode writes it
// and decode reads what encode wrote. A value of as many steps as the small stack has frames of the smallest size is
// deeper than that stack: encode and decode stop with nesting_depth_exceeded where the stack of the thread ends. The
// value is made and ended on the main thread, because its destructor recurses through its steps.
constexpr std::size_t steps_held = 64;
constexpr std::size_t steps_beyond = test::small_stack_bytes / test::smallest_frame_bytes;

TEST_CASE("structs depth: a struct that holds a vector of itself")
{
    auto const held = cbor::structs<node>::encode(nested(steps_held));
    REQUIRE(held.has_value());
    CHECK_EQ(*held, nested_bytes(steps_held));
    CHECK(cbor::structs<node>::decode(nested_bytes(steps_held)).has_value());
    node const deep = nested(steps_beyond);
    std::string const deep_bytes = nested_bytes(steps_beyond);
    CHECK_EQ(test::error_on_small_stack([&deep] { return cbor::structs<node>::encode(deep); }),
             error::nesting_depth_exceeded);
    CHECK_EQ(test::error_on_small_stack([&deep_bytes] { return cbor::structs<node>::decode(std::string(deep_bytes)); }),
             error::nesting_depth_exceeded);
}

TEST_CASE("structs depth: a struct with an optional vector of itself")
{
    auto const held = cbor::structs<optional_node>::encode(nested_optional(steps_held));
    REQUIRE(held.has_value());
    CHECK_EQ(*held, nested_optional_bytes(steps_held));
    CHECK(cbor::structs<optional_node>::decode(nested_optional_bytes(steps_held)).has_value());
    optional_node const deep = nested_optional(steps_beyond);
    std::string const deep_bytes = nested_optional_bytes(steps_beyond);
    CHECK_EQ(test::error_on_small_stack([&deep] { return cbor::structs<optional_node>::encode(deep); }),
             error::nesting_depth_exceeded);
    CHECK_EQ(test::error_on_small_stack(
                 [&deep_bytes] { return cbor::structs<optional_node>::decode(std::string(deep_bytes)); }),
             error::nesting_depth_exceeded);
}
