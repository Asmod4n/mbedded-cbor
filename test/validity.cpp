#include "binding.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>

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

namespace
{

struct shared_ptr_case {
    std::shared_ptr<int const> pointer;
    bool empty;
    bool null;
};

std::shared_ptr<int const> const holder = std::make_shared<int const>(7);
int const elsewhere = 8;

// The four states of a std::shared_ptr after [util.smartptr.shared]: it owns an object or not (empty means it owns
// none), and it stores a pointer or not (null). Each state is built with a constructor of the standard.
shared_ptr_case const shared_ptr_cases[] = {
    {std::shared_ptr<int const>{}, true, true},
    {std::shared_ptr<int const>(std::shared_ptr<int const>{}, &elsewhere), true, false},
    {std::shared_ptr<int const>(holder, nullptr), false, true},
    {holder, false, false},
};

} // namespace

// An owner that owns no object keeps nothing alive, whatever pointer it stores. The check must throw for exactly the
// two empty states, so that a view beside such an owner is never handed out.
TEST_CASE("validity: throw_logic_error_if_empty for every state of a shared_ptr")
{
    for (shared_ptr_case const &c : shared_ptr_cases) {
        if (c.empty)
            CHECK_THROWS_AS(validity::throw_logic_error_if_empty(c.pointer, "empty"), std::logic_error);
        else
            CHECK_NOTHROW(validity::throw_logic_error_if_empty(c.pointer, "empty"));
    }
}

// A pointer that is read through must not be null, whether it owns an object or not. The check must throw for
// exactly the two null states.
TEST_CASE("validity: throw_logic_error_if_null for every state of a shared_ptr")
{
    for (shared_ptr_case const &c : shared_ptr_cases) {
        if (c.null)
            CHECK_THROWS_AS(validity::throw_logic_error_if_null(c.pointer, "null"), std::logic_error);
        else
            CHECK_NOTHROW(validity::throw_logic_error_if_null(c.pointer, "null"));
    }
}
