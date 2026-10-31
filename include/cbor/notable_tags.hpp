// Gives the tag number 21334, a programming aid for simple values.
// cbor::schema and cbor::structs stop the build when a described type
// has this tag.
// https://datatracker.ietf.org/doc/html/draft-bormann-cbor-notable-tags-12#section-10.2
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
