// The bench of lazy::clone (lazy.hpp), which decodes the item of a lazy into a tree of cbor::item.
// lazy_clone_element: each element of an array of 16 maps, an item that stands inside its message, so the clone
// reads only its part. lazy_clone_top: the whole array, the control row.
// Each iteration clones every lazy of a buffer of COLD_BYTES bytes of messages.
#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>

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

constexpr std::string_view element = "\xa2\x61\x61\x01\x61\x62\x82\x02\x03"sv;
constexpr std::size_t elements = 16;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::string message_of()
{
    std::string m(1, static_cast<char>(0x80 | elements));
    for (std::size_t i = 0; i < elements; ++i)
        m.append(element);
    return m;
}

void run(benchmark::State &state, bool const inside)
{
    std::string const message = message_of();
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size()) {
        auto const top = *cbor::lazy::view(std::make_shared<std::string const>(message));
        if (!inside) {
            lazies.push_back(top);
            continue;
        }
        for (std::size_t i = 0; i < elements; ++i)
            lazies.push_back(*top.at(i));
    }
    auto const sample = lazies.front().clone();
    if (!sample) {
        state.SkipWithError("clone of a lazy of the message fails");
        return;
    }
    for (auto _ : state) {
        for (cbor::lazy const &each : lazies) {
            auto r = each.clone();
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * lazies.size()));
}

void lazy_clone_element(benchmark::State &state)
{
    run(state, true);
}

void lazy_clone_top(benchmark::State &state)
{
    run(state, false);
}

}

BENCHMARK(lazy_clone_element)->Unit(benchmark::kMillisecond);
BENCHMARK(lazy_clone_top)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
