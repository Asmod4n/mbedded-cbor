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

class padded_string
{
public:
    static constexpr std::size_t padding = 4;

    class in_place_string
    {
        std::span<char> bytes;
        std::size_t first;
        std::size_t last;

    public:
        in_place_string(std::span<char> const padded, std::size_t const at)
            : bytes(padded), first(at), last(at)
        {
        }

        void assign(std::string_view const content)
        {
            last = first + content.size();
        }

        void push_back(char const c)
        {
            bytes[last] = c;
            ++last;
        }

        void append(std::string_view const run)
        {
            std::ranges::copy(run, bytes.subspan(last).begin());
            last += run.size();
        }

        operator std::string_view() const
        {
            std::span<char const> const content = bytes.subspan(first, last - first);
            return {content.begin(), content.end()};
        }
    };

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

    std::span<char const> padded() const
    {
        return {bytes.get(), length + padding};
    }

public:
    explicit padded_string(std::string_view const text)
        : bytes(std::make_unique_for_overwrite<char[]>(padded_size(text.size()))), length(text.size())
    {
        std::span<char> const all(bytes.get(), length + padding);
        std::ranges::fill(std::ranges::copy(text, all.begin()).out, all.end(), '\0');
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

    in_place_string in_place(std::size_t const first)
    {
        return {std::span<char>(bytes.get(), length + padding), first};
    }
};

}

}
