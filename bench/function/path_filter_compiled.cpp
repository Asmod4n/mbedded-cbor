// The bench of the block that evaluates a filter of a path with numbers in the compile mode: lazy::at_path<Path,
// cbor::nodelist> (query.hpp). Each iteration applies the path to every message of a buffer of COLD_BYTES bytes.
// The rows with an embedded query read the nested message [[1, 2], [3], {"a": 1, "b": 2}, 4]: an existence test of a
// query that is not singular, a nested filter, count(), value() and an absolute query.
#include <benchmark/benchmark.h>
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
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * lazies.size()));
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
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * lazies.size()));
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
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * lazies.size()));
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

BENCHMARK(path_filter_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_descendant_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_existence_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_nested_filter_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_count_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_value_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK(path_absolute_compiled)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
