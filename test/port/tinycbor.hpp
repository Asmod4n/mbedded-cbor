// Helpers of the tests in test/port/tinycbor_*.cpp, which port the tests of TinyCBOR.
//
// Upstream: https://github.com/intel/tinycbor, commit 0cb8ba93637a18e4497e85a3f5c681ee7dba38f4.
// The tests and data tables of upstream carry this license:
//
// Copyright (C) 2021 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once

#include <cbor/cbor.hpp>

#include <charconv>
#include <cmath>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace tinycbor_port
{

// The value 0 is no enumerator of cbor::error: a table row with it expects a result and no error.
inline constexpr cbor::error no_error{};

inline std::string error_text(cbor::error const e)
{
    if (e == no_error)
        return "no error";
    return std::error_code{e}.message();
}

template <class T>
std::string outcome(std::expected<T, cbor::error> const &r)
{
    return error_text(r ? no_error : r.error());
}

// The diagnostic notation of the bytes, or the text of the error. A failed check then shows the error.
inline std::string printed(std::string_view const bytes)
{
    auto const r = cbor::diagnostic_notation(bytes);
    if (!r)
        return "error: " + error_text(r.error());
    return *r;
}

// RFC 8949 section 8 writes a float as a number with a fraction or an exponent, as JSON does, and the EDN draft
// (refs/draft-ietf-cbor-edn-literals-28.txt, 3.1) appends _1, _2 or _3 when the float is not in its preferred
// serialization. Neither fixes when the text uses an exponent, so a float is checked by the value of its text and by
// its encoding indicator.
inline bool float_text_holds(std::string_view const text, double const value, std::string_view const indicator)
{
    if (!text.ends_with(indicator))
        return false;
    std::string_view const number = text.substr(0, text.size() - indicator.size());
    if (number.find('_') != std::string_view::npos)
        return false;
    if (std::isnan(value))
        return number == "NaN";
    if (std::isinf(value))
        return number == (value < 0 ? "-Infinity" : "Infinity");
    if (number.find_first_of(".eE") == std::string_view::npos)
        return false;
    double parsed = 0;
    auto const [end, ec] = std::from_chars(number.data(), std::to_address(number.end()), parsed);
    return ec == std::errc{} && end == std::to_address(number.end()) && parsed == value &&
           std::signbit(parsed) == std::signbit(value);
}

}
