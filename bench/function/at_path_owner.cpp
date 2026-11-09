// The bench of the block of at_path forms that move the bytes into a new owner (query.hpp).
// Each iteration reads every message of a buffer of COLD_BYTES bytes of messages, each moved into its owner.
#include <benchmark/benchmark.h>
#include <cbor/query.hpp>

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

constexpr std::string_view message = "\xa2\x61\x61\x61\x78\x61\x62\x82\x02\x03"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

void at_path_fixed(benchmark::State &state)
{
    auto const sample = cbor::at_path<"$.a", std::string_view>(std::string(message));
    if (!sample || **sample != "x") {
        state.SkipWithError("at_path<\"$.a\"> of {\"a\": \"x\", \"b\": [2, 3]} does not give \"x\"");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::string> messages(count, std::string(message));
        state.ResumeTiming();
        for (std::string &each : messages) {
            auto r = cbor::at_path<"$.a", std::string_view>(std::move(each));
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

void at_path_query(benchmark::State &state)
{
    auto const query = cbor::path::compile<std::string_view>("$.a");
    if (!query) {
        state.SkipWithError("cbor::path::compile refuses $.a");
        return;
    }
    auto const sample = cbor::at_path(std::string(message), *query);
    if (!sample || **sample != "x") {
        state.SkipWithError("the query $.a of {\"a\": \"x\", \"b\": [2, 3]} does not give \"x\"");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::string> messages(count, std::string(message));
        state.ResumeTiming();
        for (std::string &each : messages) {
            auto r = cbor::at_path(std::move(each), *query);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

}

BENCHMARK(at_path_fixed)->Unit(benchmark::kMillisecond);
BENCHMARK(at_path_query)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
