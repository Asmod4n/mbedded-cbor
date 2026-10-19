// The loop that every C++ decoder program shares. It reads the lines of write.cpp from stdin, decodes the
// written and the expected bytes with one decoder, and prints "name TAB result-of-written TAB
// result-of-expected". A result is the canonical dump of the decoded value, or "!" and the message of the
// decoder. The dump is the same text in every program, so check.py can compare it. The decoder gives the dump
// through dump_of, which each program defines.
#pragma once

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace interop
{

inline std::string hex_of(std::string_view const bytes)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (char const c : bytes) {
        auto const b = static_cast<unsigned char>(c);
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xf]);
    }
    return out;
}

inline std::string bytes_of_hex(std::string_view const hex)
{
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned value = 0;
        std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
        bytes.push_back(static_cast<char>(value));
    }
    return bytes;
}

// The value of a float, independent of the width on the wire: the bits of the binary64 that holds it. All
// NaNs are one, because RFC 8949 4.2.2 lets a decoder keep or drop the payload.
inline std::string float_dump(double const d)
{
    if (std::isnan(d))
        return "nan";
    char buf[32];
    std::snprintf(buf, sizeof buf, "f%016llx", static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(d)));
    return buf;
}

inline std::string unsigned_dump(unsigned __int128 v)
{
    std::string s;
    do {
        s.insert(s.begin(), static_cast<char>('0' + static_cast<int>(v % 10)));
        v /= 10;
    } while (v != 0);
    return s;
}

// -1 - n of RFC 8949 3.1, major type 1.
inline std::string negative_dump(std::uint64_t const n)
{
    return "-" + unsigned_dump(static_cast<unsigned __int128>(n) + 1);
}

// RFC 8949 5.6: the order of the pairs of a map carries no meaning, so the dump sorts them.
inline std::string map_dump(std::vector<std::string> pairs)
{
    std::ranges::sort(pairs);
    std::string out = "{";
    for (std::size_t i = 0; i < pairs.size(); ++i)
        out += (i ? "," : "") + pairs[i];
    return out + "}";
}

inline std::string array_dump(std::vector<std::string> const &elements)
{
    std::string out = "[";
    for (std::size_t i = 0; i < elements.size(); ++i)
        out += (i ? "," : "") + elements[i];
    return out + "]";
}

template <class Dump>
int run(Dump const dump_of)
{
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string_view> fields;
        std::string_view rest = line;
        for (std::size_t tab; (tab = rest.find('\t')) != std::string_view::npos; rest.remove_prefix(tab + 1))
            fields.push_back(rest.substr(0, tab));
        fields.push_back(rest);
        if (fields.size() != 4 || fields[2].starts_with('!'))
            continue;
        auto const result = [&](std::string_view const hex) -> std::string {
            if (hex == "-")
                return "-";
            std::expected<std::string, std::string> const r = dump_of(bytes_of_hex(hex));
            return r ? *r : "!" + r.error();
        };
        std::cout << fields[0] << '\t' << result(fields[2]) << '\t' << result(fields[3]) << '\n';
    }
    return 0;
}

}
