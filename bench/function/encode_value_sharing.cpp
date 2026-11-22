// The bench of the block cbor::encode and encode_iov (encode.hpp) of a tree with a cycle, which they write with
// tags 28 and 29 (the value-sharing registration, refs/value-sharing.html).
// Each iteration writes every tree of a buffer of COLD_BYTES bytes of messages.
#include <benchmark/benchmark.h>
#include <cbor/decode.hpp>
#include <cbor/encode.hpp>

#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::string_view message = "\xd8\x1c\x83\x01\x63\x61\x62\x63\xd8\x1d\x00"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

auto written(cbor::item const tree, std::string &out)
{
    return cbor::encode(out, tree);
}

auto pieces(cbor::item const tree)
{
    return cbor::encode_iov(tree);
}

void encode_value_sharing(benchmark::State &state)
{
    std::vector<cbor::owning<cbor::item>> trees;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size()) {
        auto const decoded = cbor::decode_cycles(message);
        trees.push_back(*decoded);
    }
    std::string out;
    if (!written(*trees.front(), out) || out != message) {
        state.SkipWithError("the encode does not give the message back");
        return;
    }
    for (auto _ : state) {
        for (auto const &tree : trees) {
            out.clear();
            auto r = written(*tree, out);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(trees.size()));
}

void encode_iov_value_sharing(benchmark::State &state)
{
    std::vector<cbor::owning<cbor::item>> trees;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size()) {
        auto const decoded = cbor::decode_cycles(message);
        trees.push_back(*decoded);
    }
    for (auto _ : state) {
        for (auto const &tree : trees) {
            auto r = pieces(*tree);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(trees.size()));
}

}

BENCHMARK(encode_value_sharing)->Unit(benchmark::kMillisecond);
BENCHMARK(encode_iov_value_sharing)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
