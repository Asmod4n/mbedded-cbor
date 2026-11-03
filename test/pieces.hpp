// Joins the pieces of an iov_writer into one string, each byte read as a value.
#ifndef CBOR_TEST_PIECES_HPP
#define CBOR_TEST_PIECES_HPP

#include <cbor/encode.hpp>

#include <cstddef>
#include <span>
#include <string>

namespace test {

inline std::string joined(std::span<std::span<std::byte const> const> const pieces)
{
    std::string out;
    for (std::span<std::byte const> const piece : pieces)
        for (std::byte const b : piece)
            out.push_back(std::to_integer<char>(b));
    return out;
}

}

#endif
