// The bench of the block that evaluates a filter of a path with numbers in the compile mode: lazy::at_path<Path,
// cbor::nodelist> (query.hpp). Each iteration applies the path to every message of a buffer of COLD_BYTES bytes.
// The rows with an embedded query read the nested message [[1, 2], [3], {"a": 1, "b": 2}, 4]: an existence test of a
// query that is not singular, a nested filter, count(), value() and an absolute query.
// path_map_equal_compiled compares maps by value: $[?@ == $[0]] over an array of 8 maps of 8 pairs, each pair
// value a map of 2 pairs, so the comparison interns maps (query.hpp intern_value).
// path_map_key_compiled finds the value of a key that is a map, {"a": 7, "b": 0}, in a map of 8 such keys, so the
// search interns the keys (lazy.hpp intern_key).
#include <benchmark/benchmark.h>
#include <cbor/encode.hpp>
#include <cbor/query.hpp>

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

constexpr std::string_view message = "\x84\xf9\x3e\x00\xfa\x3f\xc0\x00\x00\x01\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"sv;

constexpr std::string_view nested = "\x84\x82\x01\x02\x81\x03\xa2\x61\x61\x01\x61\x62\x02\x04"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

void path_filter_compiled(benchmark::State &state)
{
    auto const sample = cbor::lazy::view(std::make_shared<std::string const>(message))
                            ->at_path<"$[?@ == 1.5 || @ < 1.2]", cbor::nodelist>();
    if (!sample || sample->size() != 4) {
        state.SkipWithError("the filter does not select 1.5, 1.5, 1 and 1.1 of [1.5, 1.5, 1, 1.1]");
        return;
    }
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(message)));
    for (auto _ : state) {
        for (cbor::lazy const &each : lazies) {
            auto r = each.at_path<"$[?@ == 1.5 || @ < 1.2]", cbor::nodelist>();
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

}

void path_descendant_compiled(benchmark::State &state)
{
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(message)));
    for (auto _ : state) {
        for (cbor::lazy const &each : lazies) {
            auto r = each.at_path<"$..[1:3]", cbor::nodelist>();
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

template <cbor::fixed_string Path>
void path_nested_compiled(benchmark::State &state, std::size_t const selected)
{
    auto const sample = cbor::lazy::view(std::make_shared<std::string const>(nested))->at_path<Path, cbor::nodelist>();
    if (!sample || sample->size() != selected) {
        state.SkipWithError("the path does not select the nodes of [[1, 2], [3], {\"a\": 1, \"b\": 2}, 4] that RFC 9535 gives");
        return;
    }
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < cold_bytes(); total += nested.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(nested)));
    for (auto _ : state) {
        for (cbor::lazy const &each : lazies) {
            auto r = each.at_path<Path, cbor::nodelist>();
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

void path_existence_compiled(benchmark::State &state)
{
    path_nested_compiled<"$[?@.*]">(state, 3);
}

void path_nested_filter_compiled(benchmark::State &state)
{
    path_nested_compiled<"$[?@[?@ > 1]]">(state, 3);
}

void path_count_compiled(benchmark::State &state)
{
    path_nested_compiled<"$[?count(@.*) == 2]">(state, 2);
}

void path_value_compiled(benchmark::State &state)
{
    path_nested_compiled<"$[?value(@..a) == 1]">(state, 1);
}

void path_absolute_compiled(benchmark::State &state)
{
    path_nested_compiled<"$[?@ == $[3]]">(state, 1);
}

std::string maps_message()
{
    constexpr std::size_t maps = 8;
    constexpr std::size_t pairs = 8;
    constexpr std::size_t inner_pairs = 2;
    std::string m;
    cbor::encoder<std::string> e(m);
    (void)e.array(maps);
    for (std::size_t i = 0; i < maps; ++i) {
        (void)e.map(pairs);
        for (std::size_t k = pairs; k-- > 0;) {
            (void)e.unsigned_integer(static_cast<std::uint8_t>(k));
            (void)e.map(inner_pairs);
            (void)e.text_string("a"sv);
            (void)e.unsigned_integer(static_cast<std::uint8_t>(k));
            (void)e.text_string("b"sv);
            (void)e.unsigned_integer(static_cast<std::uint8_t>(i % 2));
        }
    }
    (void)e.finish();
    return m;
}

void path_map_equal_compiled(benchmark::State &state)
{
    std::string const maps = maps_message();
    auto const sample =
        cbor::lazy::view(std::make_shared<std::string const>(maps))->at_path<"$[?@ == $[0]]", cbor::nodelist>();
    if (!sample || sample->size() != 4) {
        state.SkipWithError("the filter does not select the 4 maps equal to the first");
        return;
    }
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < cold_bytes(); total += maps.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(maps)));
    for (auto _ : state) {
        for (cbor::lazy const &each : lazies) {
            auto r = each.at_path<"$[?@ == $[0]]", cbor::nodelist>();
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

std::string map_keys_message()
{
    constexpr std::size_t keys = 8;
    constexpr std::size_t key_pairs = 2;
    std::string m;
    cbor::encoder<std::string> e(m);
    (void)e.map(keys);
    for (std::size_t k = 0; k < keys; ++k) {
        (void)e.map(key_pairs);
        (void)e.text_string("a"sv);
        (void)e.unsigned_integer(static_cast<std::uint8_t>(k));
        (void)e.text_string("b"sv);
        (void)e.unsigned_integer(std::uint8_t{0});
        (void)e.unsigned_integer(static_cast<std::uint8_t>(k));
    }
    (void)e.finish();
    return m;
}

void path_map_key_compiled(benchmark::State &state)
{
    std::string const keyed = map_keys_message();
    auto const sample = cbor::lazy::view(std::make_shared<std::string const>(keyed))
                            ->at_path<"$[{\"a\": 7, \"b\": 0}]", std::uint64_t>();
    if (!sample || *sample != 7) {
        state.SkipWithError("the path does not find the value 7 of the key {\"a\": 7, \"b\": 0}");
        return;
    }
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < cold_bytes(); total += keyed.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(keyed)));
    for (auto _ : state) {
        for (cbor::lazy const &each : lazies) {
            auto r = each.at_path<"$[{\"a\": 7, \"b\": 0}]", std::uint64_t>();
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

BENCHMARK(path_filter_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_descendant_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_existence_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_nested_filter_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_count_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_value_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_absolute_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_map_equal_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_map_key_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
