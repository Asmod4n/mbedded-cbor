// The bench of the block cbor::decode of a map and of an item under tag 24 (decode.hpp).
// Each iteration decodes every message of a buffer of COLD_BYTES bytes of messages.
#include <benchmark/benchmark.h>
#include <cbor/decode.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::string_view plain = "\xa2\x61\x61\x64\x49\x45\x54\x46\x61\x62\x82\x01\x02"sv;
constexpr std::string_view embedded = "\xd8\x18\x44\x82\x01\x61\x61"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

template <std::string_view const &Message>
void decode(benchmark::State &state)
{
    if (!cbor::decode(Message)) {
        state.SkipWithError("cbor::decode refuses the message of the bench");
        return;
    }
    std::vector<std::string> const messages(cold_bytes() / Message.size() + 1, std::string(Message));
    for (auto _ : state) {
        for (std::string const &each : messages) {
            auto r = cbor::decode(each);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(messages.size()));
}

}

BENCHMARK(decode<plain>)->Name("decode_map")->Unit(benchmark::kMillisecond);
BENCHMARK(decode<embedded>)->Name("decode_tag_24")->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
