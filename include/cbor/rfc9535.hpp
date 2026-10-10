#pragma once

#include <cstdint>
#include <limits>

namespace cbor
{

class rfc9535
{
    static constexpr std::int64_t exact_integer_max =
        (std::int64_t{1} << std::numeric_limits<double>::digits) - 1;

    friend class jsonpath;
};

}
