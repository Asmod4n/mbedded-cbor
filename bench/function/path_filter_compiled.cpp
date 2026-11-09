// The bench of the block that evaluates a filter of a path with numbers in the compile mode: lazy::at_path<Path,
// cbor::nodelist> (query.hpp). Each iteration applies the path to every message of a buffer of COLD_BYTES bytes.
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

void path_filter_compiled(benchmark::State &state)
{
    auto const sample = cbor::lazy::view(std::make_shared<std::string const>(message))
                            ->at_path<"$[?@ == 1.5 || @ < 1.2]", cbor::nodelist>();
    if (!sample || sample->size() != 4) {
        state.SkipWithError("the filter does not select 1.5, 1.5, 1 and 1.1 of [1.5, 1.5, 1, 1.1]");
        return;
    }
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(message)));
    for (auto _ : state) {
        for (cbor::lazy const &each : lazies) {
            auto r = each.at_path<"$[?@ == 1.5 || @ < 1.2]", cbor::nodelist>();
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * lazies.size()));
}

}

BENCHMARK(path_filter_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
