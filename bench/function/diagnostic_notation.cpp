// The bench of the block cbor::diagnostic_notation (diagnostic_notation.hpp).
// Each iteration writes the diagnostic notation of every item of a buffer of
// COLD_BYTES bytes. The items and their texts come from RFC 8949 Appendix A.
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

std::vector<std::string> items_of(std::size_t const bytes)
{
    std::vector<std::string> items;
    std::size_t total = 0;
    while (total < bytes) {
        for (auto const &[encoded, text] : appendix_a) {
            items.emplace_back(encoded);
            total += encoded.size();
        }
    }
    return items;
}

void diagnostic_notation(benchmark::State &state)
{
    for (auto const &[encoded, text] : appendix_a) {
        auto const r = cbor::diagnostic_notation(encoded);
        if (!r || *r != text) {
            state.SkipWithError("diagnostic_notation gives a text that RFC 8949 Appendix A does not give");
            return;
        }
    }
    std::vector<std::string> const items = items_of(cold_bytes());
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

}

BENCHMARK(diagnostic_notation)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
