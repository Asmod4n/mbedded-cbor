#pragma once

#include <cstdint>

namespace cbor
{

class rfc9535
{
    static constexpr std::int64_t exact_integer_max = (std::int64_t{1} << 53) - 1;

    friend class jsonpath;
};

}
