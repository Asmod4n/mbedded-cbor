#pragma once

#include <cstddef>
#include <cstdint>

namespace cbor
{

enum class pass;

class rfc8746
{
    enum class tag_number : std::uint64_t {
        typed_array_first = 64,
        typed_array_reserved = 76,
        float128_big_endian = 83,
        typed_array_last = 87
    };

    friend class validity;

    friend class heads;

    friend class decoding;

    friend class encoding;

    friend class well_formedness;

    friend class value_sharing;

    friend class diagnostic_notation;

    friend class jsonpath;

    template <class Writer>
    friend struct encoder;

    template <std::size_t, class, class, pass>
    friend class walker;

    friend struct lazy;

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

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
