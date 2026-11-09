// The bench of the block cbor::path::compile with EDN literals in filters (query.hpp, diagnostic_notation.hpp).
// Each iteration compiles every path of a buffer of COLD_BYTES bytes of path text.
#include <benchmark/benchmark.h>
#include <cbor/query.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::array paths{
    "$.a[?@ == [1, [2, 3], [4, 5]]]"sv,
    R"($.a[?@ == {"a": 1, "b": [2, 3]}])"sv,
    R"($.a[?@ == "IETF"])"sv,
    "$.a[?@ == h'01020304']"sv,
    "$.a[?@ == 1.5 && @ != 1.1]"sv,
    "$.a[1][2:9007199254740991]"sv,
};

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::vector<std::string> paths_of(std::size_t const bytes)
{
    std::vector<std::string> texts;
    std::size_t total = 0;
    while (total < bytes) {
        for (std::string_view const path : paths) {
            texts.emplace_back(path);
            total += path.size();
        }
    }
    return texts;
}

void path_compile(benchmark::State &state)
{
    for (std::string_view const path : paths) {
        if (!cbor::path::compile<cbor::nodelist>(path)) {
            state.SkipWithError("cbor::path::compile refuses a path of RFC 9535 with EDN literals");
            return;
        }
    }
    std::vector<std::string> const texts = paths_of(cold_bytes());
    for (auto _ : state) {
        for (std::string const &text : texts) {
            auto q = cbor::path::compile<cbor::nodelist>(text);
            benchmark::DoNotOptimize(q);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(texts.size()));
}

}

BENCHMARK(path_compile)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
