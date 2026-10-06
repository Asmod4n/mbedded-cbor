#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

#include "binding.hpp"
#include "error.hpp"

namespace cbor
{

enum class pass;

struct lazy;

class validity
{
    static constexpr std::expected<void, error> check_nesting_depth(std::size_t const depth, std::size_t const depth_max)
    {
        if (depth > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        return {};
    }

    friend class decoding;

    friend class well_formedness;

    friend class diagnostic_notation;

    friend class jsonpath;

    template <std::size_t, class, class, pass>
    friend class walker;

#ifdef __cpp_impl_reflection
    friend class packed;

    friend class generic;

    template <class>
    friend class schema;

    template <class>
    friend class databind;
#endif
};

}
