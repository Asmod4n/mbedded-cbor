// Gives the tag numbers for a calendar date: 100 for a count of days
// since 1970-01-01, and 1004 for a full-date text string.
// https://www.rfc-editor.org/rfc/rfc8943#section-2.1
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
