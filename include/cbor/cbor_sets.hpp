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
