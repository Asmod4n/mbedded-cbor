#pragma once

#include <cstdint>

namespace cbor
{

class rfc8943
{
    enum class tag_number : std::uint64_t { full_date_string = 1004, days_since_epoch_date = 100 };

    friend class reflection;

    friend class generic;
};

}
