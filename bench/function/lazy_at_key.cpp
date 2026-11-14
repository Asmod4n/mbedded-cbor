// The bench of cbor::lazy::at(std::string_view) on a map (lazy.hpp), which reads the position of the lazy.
// lazy_at_key: at("a") on each lazy of a buffer of COLD_BYTES bytes of the message {"a": 1, "b": [2, 3]}.
#include <benchmark/benchmark.h>
#include <cbor/lazy.hpp>

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

constexpr std::string_view message = "\xa2\x61\x61\x01\x61\x62\x82\x02\x03"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::vector<cbor::lazy> lazies_of(std::size_t const bytes)
{
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < bytes; total += message.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(message)));
    return lazies;
}

void lazy_at_key(benchmark::State &state)
{
    std::vector<cbor::lazy> const lazies = lazies_of(cold_bytes());
    for (auto _ : state) {
        std::int64_t sum = 0;
        for (cbor::lazy const &doc : lazies)
            sum += *doc.at("a")->get<std::int64_t>();
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

}

BENCHMARK(lazy_at_key)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
