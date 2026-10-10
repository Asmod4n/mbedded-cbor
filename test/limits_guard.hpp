#pragma once

#include <cbor/cbor.hpp>

namespace test
{

// A test that needs other limits sets them here, and the destructor gives the limits before back, so no later test
// sees them.
struct limits_guard {
    cbor::limit_values before;

    explicit limits_guard(cbor::limit_values const v)
        : before{cbor::limits.nesting_depth, cbor::limits.decoded_bytes, cbor::limits.string_length,
                 cbor::limits.container_elements, cbor::limits.input_bytes}
    {
        cbor::limits = v;
    }

    limits_guard(limits_guard const &) = delete;
    limits_guard &operator=(limits_guard const &) = delete;

    ~limits_guard()
    {
        cbor::limits = before;
    }
};

}
