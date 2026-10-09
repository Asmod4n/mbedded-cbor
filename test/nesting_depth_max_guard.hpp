#pragma once

#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstddef>

namespace test
{

// A test that needs another nesting depth sets it here, and the destructor gives the depth before back, so no later
// test sees it.
struct nesting_depth_max_guard {
    std::size_t before;

    explicit nesting_depth_max_guard(std::size_t const depth_max)
        : before(cbor::validity::nesting_depth_max_read())
    {
        [[maybe_unused]] auto const set = cbor::validity::nesting_depth_max_set(depth_max);
        REQUIRE(set.has_value());
    }

    nesting_depth_max_guard(nesting_depth_max_guard const &) = delete;
    nesting_depth_max_guard &operator=(nesting_depth_max_guard const &) = delete;

    ~nesting_depth_max_guard()
    {
        static_cast<void>(cbor::validity::nesting_depth_max_set(before));
    }
};

}
