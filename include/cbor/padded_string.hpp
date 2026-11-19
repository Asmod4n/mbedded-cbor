// Gives a copy of a text with zero bytes after its end, so that a reader of the copy reads up to the size of
// the padding past the end with no check per byte. A string of the copy can be unescaped in place.
// https://github.com/simdjson/simdjson/blob/master/include/simdjson/padded_string.h
// https://github.com/ibireme/yyjson
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "validity.hpp"

namespace cbor
{

namespace detail
{

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif

class padded_string
{
public:
    static constexpr std::size_t padding = 4;

private:
    std::unique_ptr<char[]> bytes;
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
        std::size_t const size = padded_size(length);
        bytes = std::make_unique<char[]>(size);
        std::ranges::copy(text, std::span<char>(bytes.get(), size).begin());
    }

    std::size_t size() const
    {
        return length;
    }

    operator std::string_view() const
    {
        return {bytes.get(), length};
    }

    unsigned char operator[](std::size_t const at) const
    {
        return static_cast<unsigned char>(padded()[at]);
    }

    std::span<char const> padded() const
    {
        return {bytes.get(), length + padding};
    }

    std::span<char> padded()
    {
        return {bytes.get(), length + padding};
    }
};

#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif

}

}
