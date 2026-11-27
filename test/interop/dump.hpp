// The loop that every C++ decoder program shares. It reads the lines of write.cpp from stdin. With no
// argument it decodes the written and the expected bytes with one decoder, and prints "name TAB
// result-of-written TAB result-of-expected". With the argument "reencode" it decodes the input of the case
// (the expected bytes, or the written bytes where no expected bytes exist) with the decoder, writes the value
// again with the encoder of the same library, and prints "name TAB input TAB bytes-written TAB
// result-of-bytes-written". A result is the canonical dump of the decoded value, or "!" and the message of
// the decoder. Bytes that the encoder refuses to write are "!" and its message. The dump is the same text in
// every program, so check.py can compare it. The decoder gives the dump through dump_of, and the encoder
// gives its bytes through reencode_of; each program defines both.
#pragma once

#include "../hex.hpp"

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

// The value of a float, independent of the width on the wire: the bits of the binary64 that holds it. All
// NaNs are one, because RFC 8949 4.2.2 lets a decoder keep or drop the payload.
inline std::string dump_float(double const d)
{
    if (std::isnan(d))
        return "nan";
    char buf[32];
    std::snprintf(buf, sizeof buf, "f%016llx", static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(d)));
    return buf;
}

inline std::string dump_unsigned_integer(unsigned __int128 v)
{
    std::string s;
    do {
        s.insert(s.begin(), static_cast<char>('0' + static_cast<int>(v % 10)));
        v /= 10;
    } while (v != 0);
    return s;
}

// -1 - n of RFC 8949 3.1, major type 1.
inline std::string dump_negative_integer(std::uint64_t const n)
{
    return "-" + dump_unsigned_integer(static_cast<unsigned __int128>(n) + 1);
}

// RFC 8949 5.6: the order of the pairs of a map carries no meaning, so the dump sorts them.
inline std::string dump_sorted_map(std::vector<std::string> pairs)
{
    std::ranges::sort(pairs);
    std::string out = "{";
    for (std::size_t i = 0; i < pairs.size(); ++i)
        out += (i ? "," : "") + pairs[i];
    return out + "}";
}

inline std::string dump_array(std::vector<std::string> const &elements)
{
    std::string out = "[";
    for (std::size_t i = 0; i < elements.size(); ++i)
        out += (i ? "," : "") + elements[i];
    return out + "]";
}

// A tag number of RFC 8949 3.4 and the dump of its content.
inline std::string dump_tag(unsigned __int128 const number, std::string const &content)
{
    return dump_unsigned_integer(number) + "(" + content + ")";
}

inline constexpr std::size_t case_fields = 4;

// A message goes into one field of a line, so a tab or a line break in it becomes a space.
inline std::string one_line(std::string message)
{
    std::ranges::replace(message, '\t', ' ');
    std::ranges::replace(message, '\n', ' ');
    return message;
}

inline std::vector<std::string_view> fields_of(std::string_view rest)
{
    std::vector<std::string_view> fields;
    for (std::size_t tab; (tab = rest.find('\t')) != std::string_view::npos; rest.remove_prefix(tab + 1))
        fields.push_back(rest.substr(0, tab));
    fields.push_back(rest);
    return fields;
}

template <class Dump, class Reencode>
int run(int const argc, char const *const *const argv, Dump const dump_of, Reencode const reencode_of)
{
    bool const reencode = argc == 2 && std::string_view(argv[1]) == "reencode";
    if (argc > 2 || (argc == 2 && !reencode)) {
        std::cerr << "usage: " << argv[0] << " [reencode] <cases.tsv\n";
        return 2;
    }
    auto const result = [&](std::string const &bytes) -> std::string {
        std::expected<std::string, std::string> const r = dump_of(bytes);
        return r ? *r : "!" + one_line(r.error());
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string_view> const fields = fields_of(line);
        if (fields.size() != case_fields || fields[2].starts_with('!'))
            continue;
        if (!reencode) {
            auto const of_hex = [&](std::string_view const hex) {
                return hex == "-" ? std::string("-") : result(test::unhexlify(hex));
            };
            std::cout << fields[0] << '\t' << of_hex(fields[2]) << '\t' << of_hex(fields[3]) << '\n';
            continue;
        }
        std::string_view const input = fields[3] == "-" ? fields[2] : fields[3];
        std::string const bytes = test::unhexlify(input);
        if (!dump_of(bytes))
            continue;
        std::expected<std::string, std::string> const written = reencode_of(bytes);
        std::cout << fields[0] << '\t' << input << '\t';
        if (written)
            std::cout << test::hexlify(*written) << '\t' << result(*written) << '\n';
        else
            std::cout << '!' << one_line(written.error()) << "\t-\n";
    }
    return 0;
}
}
