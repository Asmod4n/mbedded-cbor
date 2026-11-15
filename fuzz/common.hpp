// Gives the checks, the comparison of values and the reader of the input that every fuzz target uses.
#pragma once

#include "../test/reference.hpp"

#include <cbor/error.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fuzz
{

// A finding of the fuzzer is a wrong answer as much as a crash, so every check that fails ends the run.
inline void require(bool const holds, std::source_location const where = std::source_location::current())
{
    if (!holds) {
        std::fprintf(stderr, "fuzz: check failed at line %u\n", static_cast<unsigned>(where.line()));
        std::abort();
    }
}

// An error that a call gives converts to a std::error_code of the library category with a message, and its default
// condition is the std::errc of the four codes that error.hpp maps, else the code itself in the library category.
inline void require_error_value(cbor::error const e)
{
    std::error_code const code = cbor::make_error_code(e);
    require(&code.category() == &cbor::category());
    require(!code.message().empty() && code.message() != "unknown cbor error");
    std::error_condition const condition = code.default_error_condition();
    switch (e) {
    case cbor::error::no_buffer_space:
        require(condition == std::errc::no_buffer_space);
        break;
    case cbor::error::value_too_large:
        require(condition == std::errc::value_too_large);
        break;
    case cbor::error::not_enough_memory:
        require(condition == std::errc::not_enough_memory);
        break;
    case cbor::error::io_error:
        require(condition == std::errc::io_error);
        break;
    default:
        require(condition == std::error_condition(static_cast<int>(e), cbor::category()));
    }
}

// A float compares by its bits, so -0.0 and 0.0 differ, and a NaN keeps its sign and payload: a shorter NaN is
// held as the double with zeros on the right of its significand (RFC 8949 4.1, 4.2.1).
// The double that holds a float with every bit: a static_cast sets the quiet bit of a signaling NaN, so a NaN takes
// its sign, an exponent of all ones and its significand with 29 zeros on the right (RFC 8949 4.1).
inline double widened(float const f)
{
    std::uint32_t const bits = std::bit_cast<std::uint32_t>(f);
    if (!std::isnan(f))
        return static_cast<double>(f);
    return std::bit_cast<double>(std::uint64_t{bits >> 31} << 63 | std::uint64_t{0x7ff} << 52 |
                                 std::uint64_t{bits & 0x7fffff} << 29);
}

inline bool values_are_the_same(test::value const &a, test::value const &b)
{
    if (a.kind.index() != b.kind.index())
        return false;
    if (auto const *x = std::get_if<double>(&a.kind))
        return std::bit_cast<std::uint64_t>(*x) == std::bit_cast<std::uint64_t>(std::get<double>(b.kind));
    if (auto const *x = std::get_if<test::array *>(&a.kind)) {
        auto const &y = *std::get<test::array *>(b.kind);
        if ((*x)->size() != y.size())
            return false;
        for (std::size_t i = 0; i < y.size(); ++i)
            if (!values_are_the_same((*x)->at(i), y.at(i)))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<test::map *>(&a.kind)) {
        auto const &y = *std::get<test::map *>(b.kind);
        if ((*x)->size() != y.size())
            return false;
        for (std::size_t i = 0; i < y.size(); ++i)
            if (!values_are_the_same((*x)->at(i).key, y.at(i).key) || !values_are_the_same((*x)->at(i).val, y.at(i).val))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<test::tagged *>(&a.kind)) {
        auto const &y = *std::get<test::tagged *>(b.kind);
        return (*x)->tag == y.tag && values_are_the_same(test::value{(*x)->content}, test::value{y.content});
    }
    return a == b;
}

// The bytes of the input, taken from the front. When they run out every read gives zero.
struct source {
    std::string_view bytes;

    std::uint8_t byte()
    {
        if (bytes.empty())
            return 0;
        auto const b = static_cast<std::uint8_t>(bytes.front());
        bytes.remove_prefix(1);
        return b;
    }

    std::uint64_t number()
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v = v << 8 | byte();
        return v;
    }

    std::string string()
    {
        std::size_t const n = byte() % 40;
        std::string s;
        for (std::size_t i = 0; i < n; ++i)
            s.push_back(static_cast<char>(byte()));
        return s;
    }

    std::string text()
    {
        std::string s = string();
        for (char &c : s)
            c = static_cast<char>(c & 0x7f);
        return s;
    }
};

} // namespace fuzz
