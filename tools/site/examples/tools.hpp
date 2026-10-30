#pragma once

#include "lazy.hpp"

namespace site
{

inline result notation_made(call const &c)
{
    auto const bytes = text_argument(c, 0);
    if (!bytes || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("cbor::diagnostic_notation takes the bytes of one item");
    return value_of(cbor::diagnostic_notation(*bytes));
}

inline constexpr entry tools_functions[] = {
    {"cbor::diagnostic_notation()", notation_made},
};

}
