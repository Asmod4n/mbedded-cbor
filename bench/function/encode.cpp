// The bench of the block cbor::encode of a tree into a std::string and into a std::span (encode.hpp), and of
// cbor::encode_iov of a tree.
// Each iteration writes every tree of a list whose messages hold COLD_BYTES bytes.
#include <benchmark/benchmark.h>
#include <cbor/decode.hpp>
#include <cbor/encode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::string_view message = "\xa2\x61\x61\x64\x49\x45\x54\x46\x61\x62\x82\x44\x01\x02\x03\x04\x02"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

template <bool Span>
void encode(benchmark::State &state)
{
    auto const sample = cbor::decode(message);
    if (!sample) {
        state.SkipWithError("cbor::decode refuses {\"a\": \"IETF\", \"b\": [h'01020304', 2]}");
        return;
    }
    std::string written;
    if (!cbor::encode(**sample, written) || written != message) {
        state.SkipWithError("cbor::encode does not give the bytes of the message back");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    std::string out(count * message.size(), '\0');
    for (auto _ : state) {
        if constexpr (Span) {
            std::span<char> rest(out);
            for (std::size_t i = 0; i < count; ++i) {
                auto n = cbor::encode(**sample, rest);
                benchmark::DoNotOptimize(n);
                rest = rest.subspan(*n);
            }
        } else {
            out.clear();
            for (std::size_t i = 0; i < count; ++i) {
                auto n = cbor::encode(**sample, out);
                benchmark::DoNotOptimize(n);
            }
        }
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

void encode_iov_item(benchmark::State &state)
{
    auto const sample = cbor::decode(message);
    if (!sample) {
        state.SkipWithError("cbor::decode refuses {\"a\": \"IETF\", \"b\": [h'01020304', 2]}");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            auto pieces = cbor::encode_iov(**sample);
            benchmark::DoNotOptimize(pieces);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}
}

BENCHMARK(encode<false>)->Name("encode_string")->Unit(benchmark::kMillisecond);
BENCHMARK(encode<true>)->Name("encode_span")->Unit(benchmark::kMillisecond);
BENCHMARK(encode_iov_item)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
