// Gives the tag numbers for an extended time (1001) and a duration (1002),
// the keys of their map that the library reads, the timescale values and
// the offset of GPS time to TAI.
// https://www.rfc-editor.org/rfc/rfc9581#section-3
#pragma once

#include <cstdint>

namespace cbor
{

namespace detail
{

class generic;
class reflection;
class validity;

}

class rfc9581
{
    enum class tag_number : std::uint64_t { extended_time = 1001, duration = 1002 };

    enum class map_key : std::int64_t {
        attoseconds = -18,
        femtoseconds = -15,
        timescale_elective = -13,
        picoseconds = -12,
        nanoseconds = -9,
        time_zone_hint = -10,
        microseconds = -6,
        milliseconds = -3,
        timescale_elective_first = -1,
        base_time = 1,
        decimal_fraction_base_time = 4,
        bigfloat_base_time = 5,
        time_zone_hint_critical = 10,
        timescale_critical = 13
    };

    enum class timescale : std::uint64_t { utc = 0, tai = 1 };

    static constexpr std::int64_t tai_minus_gps_seconds = 315964819;

    friend class detail::reflection;

    friend class detail::generic;

    friend class detail::validity;
};

}
