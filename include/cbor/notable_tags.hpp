#pragma once

#include <cstdint>

namespace cbor
{

class notable_tags
{
    enum class tag_number : std::uint64_t { programming_aid_for_simple_values = 21334 };

    friend class reflection;

    friend class generic;
};

}
