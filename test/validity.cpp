#include "binding.hpp"

#include <cstddef>

using cbor::error;
using cbor::validity;

// The owner set the bound of DepthMax to 1024 and its default to 128. Every form that takes DepthMax reads both
// from these two constants, so this test is the one place that holds the two numbers.
TEST_CASE("validity: the limit and the default of the nesting depth")
{
    CHECK_EQ(validity::nesting_depth_limit, 1024u);
    CHECK_EQ(validity::nesting_depth_default, 128u);
}

// One function decides whether a depth is allowed, at compile time for DepthMax and at run time for the depth of an
// item. A depth up to the maximum is allowed and every depth above it is refused, for every depth up to one past
// the limit.
TEST_CASE("validity: check_nesting_depth for every depth up to one past the limit")
{
    for (std::size_t depth = 0; depth <= 1025; ++depth) {
        auto const r = validity::check_nesting_depth(depth, 1024);
        if (depth <= 1024)
            CHECK(r.has_value());
        else
            CHECK_EQ(r.error(), error::nesting_depth_exceeded);
    }
    constexpr bool limit_allowed = validity::check_nesting_depth(1024, validity::nesting_depth_limit).has_value();
    constexpr bool past_limit_allowed = validity::check_nesting_depth(1025, validity::nesting_depth_limit).has_value();
    CHECK(limit_allowed);
    CHECK_FALSE(past_limit_allowed);
}
