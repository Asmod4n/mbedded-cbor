// Gives a copy of a text with zero bytes after its end, so that a reader of the copy reads up to the size of
// the padding past the end with no check per byte. A string of the copy can be unescaped in place.
// https://github.com/simdjson/simdjson/blob/master/include/simdjson/padded_string.h
// https://github.com/ibireme/yyjson
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "validity.hpp"

namespace cbor
{

namespace detail
{

class padded_string
{
public:
    static constexpr std::size_t padding = 4;

private:
    std::vector<char> bytes;
    std::size_t length;

    static std::size_t padded_size(std::size_t const size)
    {
        auto const sum = validity::checked_add(size, padding);
        validity::throw_logic_error_if(!sum,
                                       "cbor::detail::padded_string: the text is longer than memory holds");
        return *sum;
    }

public:
    explicit padded_string(std::string_view const text) : length(text.size())
    {
        bytes.reserve(padded_size(text.size()));
        bytes.assign(text.begin(), text.end());
        bytes.resize(padded_size(text.size()));
    }

    std::size_t size() const
    {
        return length;
    }

    operator std::string_view() const
    {
        return std::string_view(padded().first(length));
    }

    unsigned char operator[](std::size_t const at) const
    {
        return static_cast<unsigned char>(bytes[at]);
    }

    std::span<char const> padded() const
    {
        return bytes;
    }

    std::span<char> padded()
    {
        return bytes;
    }
};

}

}
