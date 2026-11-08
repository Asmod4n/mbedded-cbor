// The bench of the block that makes a cbor::lazy from bytes: the rvalue form (adopt, before take_ownership) and
// view (lazy.hpp). Each iteration opens every message of a buffer of COLD_BYTES bytes of messages.
#include <benchmark/benchmark.h>
#include <cbor/lazy.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

template <class Lazy = cbor::lazy, class Encoded>
auto lazy_of(Encoded &&encoded)
{
    if constexpr (requires { Lazy::adopt(std::move(encoded)); })
        return Lazy::adopt(std::move(encoded));
    else
        return Lazy::take_ownership(std::move(encoded));
}

void lazy_adopt(benchmark::State &state)
{
    auto const sample = lazy_of(std::string(message));
    if (!sample || sample->at("b")->at(1)->get<std::int64_t>() != 3) {
        state.SkipWithError("the lazy of {\"a\": 1, \"b\": [2, 3]} does not give 3 at $.b[1]");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::string> messages(count, std::string(message));
        state.ResumeTiming();
        for (std::string &each : messages) {
            auto r = lazy_of(std::move(each));
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

void lazy_view(benchmark::State &state)
{
    std::size_t const count = cold_bytes() / message.size() + 1;
    std::vector<std::shared_ptr<std::string const>> owners;
    for (std::size_t i = 0; i < count; ++i)
        owners.push_back(std::make_shared<std::string const>(message));
    for (auto _ : state) {
        for (auto const &owner : owners) {
            auto r = cbor::lazy::view(owner);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

}

BENCHMARK(lazy_adopt)->Unit(benchmark::kMillisecond);
BENCHMARK(lazy_view)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
