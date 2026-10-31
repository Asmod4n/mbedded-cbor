// Gives the tag number 258, which marks an array as a mathematical
// finite set.
// https://github.com/input-output-hk/cbor-sets-spec/blob/master/CBOR_SETS.md
#pragma once

#include <cstdint>

namespace cbor
{

class cbor_sets
{
    enum class tag_number : std::uint64_t { set = 258 };

    friend class reflection;

    friend class generic;
};

}
