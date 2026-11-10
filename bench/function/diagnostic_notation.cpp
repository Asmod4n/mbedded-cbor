// The bench of the block cbor::diagnostic_notation (diagnostic_notation.hpp).
// Each iteration writes the diagnostic notation of every item of a buffer of
// COLD_BYTES bytes. The items of diagnostic_notation and their texts come from RFC 8949 Appendix A.
// diagnostic_notation_byte_string: byte strings of 1, 4, 16 and 64 bytes, written as h'' with lower-case
// digits. diagnostic_notation_control_text: texts with control characters, written with the escapes of
// draft-ietf-cbor-edn-literals-28 2.3.1; 0x7f stands as it is.
#include <benchmark/benchmark.h>
#include <cbor/diagnostic_notation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::string_literals;
using namespace std::string_view_literals;

constexpr std::array appendix_a{
    std::pair{"\x83\x01\x82\x02\x03\x82\x04\x05"sv, "[1, [2, 3], [4, 5]]"sv},
    std::pair{"\xa2\x61\x61\x01\x61\x62\x82\x02\x03"sv, R"({"a": 1, "b": [2, 3]})"sv},
    std::pair{"\xf9\x3e\x00"sv, "1.5"sv},
    std::pair{"\x64\x49\x45\x54\x46"sv, R"("IETF")"sv},
    std::pair{"\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"sv, "1.1"sv},
};

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

constexpr std::string_view sixteen_bytes =
    "\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff"sv;
constexpr std::string_view sixteen_digits = "00112233445566778899aabbccddeeff"sv;

std::vector<std::pair<std::string, std::string>> const byte_strings{
    {"\x41\xab"s, "h'ab'"s},
    {"\x44\x01\x02\x03\x04"s, "h'01020304'"s},
    {"\x50"s + std::string(sixteen_bytes), "h'"s + std::string(sixteen_digits) + "'"},
    {"\x58\x40"s + std::string(sixteen_bytes) + std::string(sixteen_bytes) + std::string(sixteen_bytes) +
         std::string(sixteen_bytes),
     "h'"s + std::string(sixteen_digits) + std::string(sixteen_digits) + std::string(sixteen_digits) +
         std::string(sixteen_digits) + "'"},
};

std::vector<std::pair<std::string, std::string>> const control_texts{
    {"\x6d"
     "a\x00\x01\x07\x08\x09\x0a\x0c\x0d\x1b\x1f\x7f"
     "b"s,
     R"("a\u0000\u0001\u0007\b\t\n\f\r\u001b\u001f)"
     "\x7f"
     R"(b")"s},
    {"\x68"
     "\x1e\x1dline\x0a\x03"s,
     R"("\u001e\u001dline\n\u0003")"s},
};

template <class Items>
std::vector<std::string> items_of(Items const &given, std::size_t const bytes)
{
    std::vector<std::string> items;
    std::size_t total = 0;
    while (total < bytes) {
        for (auto const &[encoded, text] : given) {
            items.emplace_back(encoded);
            total += encoded.size();
        }
    }
    return items;
}

template <class Items>
void write_all(benchmark::State &state, Items const &given)
{
    for (auto const &[encoded, text] : given) {
        auto const r = cbor::diagnostic_notation(encoded);
        if (!r || *r != text) {
            state.SkipWithError("diagnostic_notation gives a text that the row does not expect");
            return;
        }
    }
    std::vector<std::string> const items = items_of(given, cold_bytes());
    for (auto _ : state) {
        std::size_t written = 0;
        for (std::string const &item : items) {
            auto r = cbor::diagnostic_notation(item);
            benchmark::DoNotOptimize(r);
            written += r->size();
        }
        benchmark::DoNotOptimize(written);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(items.size()));
}

void diagnostic_notation(benchmark::State &state)
{
    write_all(state, appendix_a);
}

void diagnostic_notation_byte_string(benchmark::State &state)
{
    write_all(state, byte_strings);
}

void diagnostic_notation_control_text(benchmark::State &state)
{
    write_all(state, control_texts);
}
}

BENCHMARK(diagnostic_notation)->Unit(benchmark::kMillisecond);
BENCHMARK(diagnostic_notation_byte_string)->Unit(benchmark::kMillisecond);
BENCHMARK(diagnostic_notation_control_text)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
