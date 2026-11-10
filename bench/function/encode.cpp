// The bench of the block cbor::encode of a tree into a std::string and into a std::span (encode.hpp), and of
// cbor::encoder over a cbor::iov_writer with texts that the program hands over with their owner.
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

// A text one byte shorter than the size from which the iov writer passes a text by reference, and a text of
// that size. The first is copied into a block, the second becomes a piece of its own.
void encode_iov_owned(benchmark::State &state)
{
    auto const copied = std::make_shared<std::string const>(31, 'a');
    auto const referenced = std::make_shared<std::string const>(32, 'b');
    auto const write = [&] {
        cbor::iov_writer w;
        cbor::encoder e{w};
        if (!e.text_string(copied) || !e.text_string(referenced))
            throw std::logic_error("bench: cbor::encoder refuses a text");
        auto r = e.finish();
        if (!r)
            throw std::logic_error("bench: cbor::encoder::finish fails");
        return std::move(*r);
    };
    auto const sample = write();
    auto const points_into = [&sample](std::string const &text) {
        return std::ranges::any_of(*sample, [&text](iovec const &piece) {
            return static_cast<void const *>(piece.iov_base) == static_cast<void const *>(text.data());
        });
    };
    if (points_into(*copied) || !points_into(*referenced)) {
        state.SkipWithError(
            "the iov writer does not copy the 31-byte text and pass the 32-byte text by reference");
        return;
    }
    std::size_t const count = cold_bytes() / (copied->size() + referenced->size()) + 1;
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            auto pieces = write();
            benchmark::DoNotOptimize(pieces);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}
}

BENCHMARK(encode<false>)->Name("encode_string")->Unit(benchmark::kMillisecond);
BENCHMARK(encode<true>)->Name("encode_span")->Unit(benchmark::kMillisecond);
BENCHMARK(encode_iov_owned)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
