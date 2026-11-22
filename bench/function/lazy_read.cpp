// The bench of the block of reads of a cbor::lazy: at with a text key, an integer key and an index, size, type, get
// (lazy.hpp).
// Each iteration reads every lazy of a buffer of COLD_BYTES bytes of messages.
// lazy_read: text keys, the message {"a": 1, "b": [2, 3]}. lazy_read_integer_key: integer keys, {1: 1, 2: [2, 3]}.
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
constexpr std::string_view integer_key_message = "\xa2\x01\x01\x02\x82\x02\x03"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::vector<cbor::lazy> lazies_of(std::string_view const bytes_of_one, std::size_t const bytes)
{
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < bytes; total += bytes_of_one.size()) {
        auto const viewed = cbor::lazy::view(std::make_shared<std::string const>(bytes_of_one));
        lazies.push_back(*viewed);
    }
    return lazies;
}

template <class Key, class Index>
std::int64_t value_at(cbor::lazy const &doc, Key const key, Index const index)
{
    auto const outer = doc.at(key);
    auto const inner = outer->at(index);
    auto const value = inner->template get<std::int64_t>();
    return value ? *value : -1;
}

template <class Key>
std::int64_t value_at(cbor::lazy const &doc, Key const key)
{
    auto const outer = doc.at(key);
    auto const value = outer->template get<std::int64_t>();
    return value ? *value : -1;
}

std::int64_t size_and_type_of(cbor::lazy const &doc)
{
    auto const size = doc.size();
    auto const type = doc.type();
    return static_cast<std::int64_t>(*size) + static_cast<std::int64_t>(*type);
}

void lazy_read(benchmark::State &state)
{
    auto const sample = cbor::lazy::view(std::make_shared<std::string const>(message));
    if (!sample || value_at(*sample, "b", 1) != 3 || size_and_type_of(*sample) != 2 + static_cast<std::int64_t>(cbor::type::map)) {
        state.SkipWithError("the lazy of {\"a\": 1, \"b\": [2, 3]} does not give 3 at $.b[1] and size 2");
        return;
    }
    std::vector<cbor::lazy> const lazies = lazies_of(message, cold_bytes());
    for (auto _ : state) {
        std::int64_t sum = 0;
        for (cbor::lazy const &doc : lazies) {
            sum += value_at(doc, "a");
            sum += value_at(doc, "b", 1);
            sum += size_and_type_of(doc);
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

void lazy_read_integer_key(benchmark::State &state)
{
    auto const sample = cbor::lazy::view(std::make_shared<std::string const>(integer_key_message));
    if (!sample || value_at(*sample, 2, 1) != 3 || size_and_type_of(*sample) != 2 + static_cast<std::int64_t>(cbor::type::map)) {
        state.SkipWithError("the lazy of {1: 1, 2: [2, 3]} does not give 3 at key 2, index 1 and size 2");
        return;
    }
    std::vector<cbor::lazy> const lazies = lazies_of(integer_key_message, cold_bytes());
    for (auto _ : state) {
        std::int64_t sum = 0;
        for (cbor::lazy const &doc : lazies) {
            sum += value_at(doc, 1);
            sum += value_at(doc, 2, 1);
            sum += size_and_type_of(doc);
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

}

BENCHMARK(lazy_read)->Unit(benchmark::kMillisecond);
BENCHMARK(lazy_read_integer_key)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
