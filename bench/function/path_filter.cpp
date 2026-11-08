// The bench of the block that evaluates a filter of a path with numbers: value_equal, value_less and the interning
// of values (query.hpp, lazy.hpp). Each iteration applies the query to every message of a buffer of COLD_BYTES bytes.
#include <benchmark/benchmark.h>
#include <cbor/query.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::string_view message = "\x84\xf9\x3e\x00\xfa\x3f\xc0\x00\x00\x01\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

void path_filter(benchmark::State &state)
{
    auto const query = cbor::path::compile<cbor::nodelist>("$[?@ == 1.5 || @ < 1.2]");
    if (!query) {
        state.SkipWithError("cbor::path::compile refuses the filter");
        return;
    }
    auto const sample = cbor::at_path(std::make_shared<std::string const>(message), *query);
    if (!sample || sample->size() != 4) {
        state.SkipWithError("the filter does not select 1.5, 1.5, 1 and 1.1 of [1.5, 1.5, 1, 1.1]");
        return;
    }
    std::vector<std::shared_ptr<std::string const>> owners;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size())
        owners.push_back(std::make_shared<std::string const>(message));
    for (auto _ : state) {
        for (auto const &each : owners) {
            auto r = cbor::at_path(each, *query);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * owners.size()));
}

}

BENCHMARK(path_filter)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
