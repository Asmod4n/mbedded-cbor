#pragma once

#include "rfc8746.hpp"
#include "rfc9535.hpp"

namespace cbor
{

enum class kind {
    unsigned_integer,
    negative_integer,
    unsigned_bignum,
    negative_bignum,
    byte_string,
    text_string,
    floating_point,
    simple_value,
    array,
    map,
    typed_array,
    registered,
    unsupported
};

template <class B>
concept binding = requires { typename B::value; };

}
