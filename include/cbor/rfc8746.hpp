#pragma once

#include <cstdint>

namespace cbor
{

class rfc8746
{
    enum class tag_number : std::uint64_t {
        typed_array_first = 64,
        typed_array_reserved = 76,
        float128_big_endian = 83,
        typed_array_last = 87
    };

    friend class validity;

#ifdef __cpp_impl_reflection
    friend class packed;
#endif
};

}
