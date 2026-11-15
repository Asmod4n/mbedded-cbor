// A writer target of the public encodes that appends each part to a std::string. A test passes it where it checks the
// writer form of the target: allocate, append and done.
#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace test
{

struct string_writer {
    std::string encoded;

    string_writer &allocate(std::size_t)
    {
        return *this;
    }

    std::expected<void, std::errc> append(std::string_view const part)
    {
        encoded.append(part);
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

} // namespace test
