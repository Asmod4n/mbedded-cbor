// The bench of a CBOR sequence that lies whole in one buffer: cbor::sequence against cbor::sequence_reader fed once
// with the same buffer (sequence.hpp, sequence_reader.hpp). Each iteration reads every item of a sequence of
// COLD_BYTES bytes: *_count counts the items, *_read also reads the integer at key "a" of each item.
#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

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

std::shared_ptr<std::string const> sequence_of(std::size_t const bytes)
{
    std::string s;
    while (s.size() < bytes)
        s.append(message);
    return std::make_shared<std::string const>(std::move(s));
}

template <bool Read>
void with_sequence(benchmark::State &state)
{
    auto const owner = sequence_of(cold_bytes());
    std::size_t const expected = owner->size() / message.size();
    for (auto _ : state) {
        std::size_t n = 0;
        std::int64_t sum = 0;
        for (auto const item : cbor::sequence(owner)) {
            if (!item) [[unlikely]]
                break;
            if constexpr (Read)
                sum += *cbor::at_path<"$.a", std::int64_t>(*item);
            ++n;
        }
        if (n != expected) {
            state.SkipWithError("cbor::sequence gives a wrong count of items");
            return;
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(expected));
}

template <bool Read>
void with_reader(benchmark::State &state)
{
    auto const owner = sequence_of(cold_bytes());
    std::size_t const expected = owner->size() / message.size();
    for (auto _ : state) {
        cbor::sequence_reader r;
        r.feed(owner);
        std::size_t n = 0;
        std::int64_t sum = 0;
        for (auto item = r.next(); item; item = r.next()) {
            if constexpr (Read) {
                auto const a = item->at("a");
                sum += *a->get<std::int64_t>();
            }
            ++n;
        }
        if (n != expected) {
            state.SkipWithError("cbor::sequence_reader gives a wrong count of items");
            return;
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(expected));
}

}

BENCHMARK(with_sequence<false>)->Name("sequence_count")->Unit(benchmark::kMillisecond);
BENCHMARK(with_reader<false>)->Name("reader_count")->Unit(benchmark::kMillisecond);
BENCHMARK(with_sequence<true>)->Name("sequence_read")->Unit(benchmark::kMillisecond);
BENCHMARK(with_reader<true>)->Name("reader_read")->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
