// The bench of the key lookup of cbor::lazy on a map (lazy.hpp), which reads the position of the lazy.
// lazy_at_key: the text key "a" on each lazy of a buffer of COLD_BYTES bytes of the message {"a": 1, "b": [2, 3]}.
// lazy_at_key_integer: the integer key 1 on each lazy of a buffer of COLD_BYTES bytes of the message {1: 1, 2: [2, 3]}.
// AT_KEY_FORM selects the names cbor::lazy::at_key(text) and cbor::lazy::at_key(integer). Without it the names of the
// earlier form are used: cbor::lazy::at(text) and cbor::lazy::at(cbor::key{integer}).
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

constexpr std::string_view text_message = "\xa2\x61\x61\x01\x61\x62\x82\x02\x03"sv;
constexpr std::string_view integer_message = "\xa2\x01\x01\x02\x82\x02\x03"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::vector<cbor::lazy> lazies_of(std::string_view const message, std::size_t const bytes)
{
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < bytes; total += message.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(message)));
    return lazies;
}

std::expected<cbor::lazy, cbor::error> value_of_text_key(cbor::lazy const &doc)
{
#ifdef AT_KEY_FORM
    return doc.at_key("a");
#else
    return doc.at("a");
#endif
}

std::expected<cbor::lazy, cbor::error> value_of_integer_key(cbor::lazy const &doc)
{
#ifdef AT_KEY_FORM
    return doc.at_key(1);
#else
    return doc.at(cbor::key{1});
#endif
}

void lazy_at_key(benchmark::State &state)
{
    std::vector<cbor::lazy> const lazies = lazies_of(text_message, cold_bytes());
    for (auto _ : state) {
        std::int64_t sum = 0;
        for (cbor::lazy const &doc : lazies)
            sum += *value_of_text_key(doc)->get<std::int64_t>();
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

void lazy_at_key_integer(benchmark::State &state)
{
    std::vector<cbor::lazy> const lazies = lazies_of(integer_message, cold_bytes());
    for (auto _ : state) {
        std::int64_t sum = 0;
        for (cbor::lazy const &doc : lazies)
            sum += *value_of_integer_key(doc)->get<std::int64_t>();
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

}

BENCHMARK(lazy_at_key)->Unit(benchmark::kMillisecond);
BENCHMARK(lazy_at_key_integer)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
