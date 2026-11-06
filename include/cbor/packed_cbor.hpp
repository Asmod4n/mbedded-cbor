// Gives the tag numbers of Packed CBOR that cbor::schema writes and reads:
// the reference tag 6, the Basic Packed CBOR tag 113, the record function
// tag 114 and the straight argument references 128 to 135.
// https://datatracker.ietf.org/doc/html/draft-ietf-cbor-packed-19
#pragma once

#include <cstdint>

namespace cbor
{

class packed_cbor
{
    enum class tag_number : std::uint64_t {
        reference = 6,
        basic_packed_cbor = 113,
        record_function = 114,
        straight_argument_first = 128,
        straight_argument_last = 135
    };

    friend class reflection;
};

}
