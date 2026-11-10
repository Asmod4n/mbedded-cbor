// The bench of the block cbor::path::compile with EDN literals in filters (query.hpp, diagnostic_notation.hpp).
// Each iteration compiles every path of a buffer of COLD_BYTES bytes of path text. The rows nested_64, nested_1024,
// map_1000 and flat compile copies of one path with a literal nested 64 or 1024 levels, a map of 1000 keys, or
// [1, 2], as many copies as fill COLD_BYTES.
#include <benchmark/benchmark.h>
#include <cbor/query.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::array paths{
    "$.a[?@ == [1, [2, 3], [4, 5]]]"sv,
    R"($.a[?@ == {"a": 1, "b": [2, 3]}])"sv,
    R"($.a[?@ == "IETF"])"sv,
    "$.a[?@ == h'01020304']"sv,
    "$.a[?@ == 1.5 && @ != 1.1]"sv,
    "$.a[1][2:9007199254740991]"sv,
};

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::vector<std::string> paths_of(std::size_t const bytes)
{
    std::vector<std::string> texts;
    std::size_t total = 0;
    while (total < bytes) {
        for (std::string_view const path : paths) {
            texts.emplace_back(path);
            total += path.size();
        }
    }
    return texts;
}

void path_compile(benchmark::State &state)
{
    for (std::string_view const path : paths) {
        if (!cbor::path::compile<cbor::nodelist>(path)) {
            state.SkipWithError("cbor::path::compile refuses a path of RFC 9535 with EDN literals");
            return;
        }
    }
    std::vector<std::string> const texts = paths_of(cold_bytes());
    for (auto _ : state) {
        for (std::string const &text : texts) {
            auto q = cbor::path::compile<cbor::nodelist>(text);
            benchmark::DoNotOptimize(q);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(texts.size()));
}

std::string nested_literal_path(std::size_t const levels)
{
    return "$.a[?@ == " + std::string(levels, '[') + "1" + std::string(levels, ']') + "]";
}

std::string map_literal_path(std::size_t const keys)
{
    std::string path = "$.a[?@ == {";
    for (std::size_t i = keys; i > 0; --i)
        path += std::to_string(i) + ": 0" + (i > 1 ? ", " : "");
    return path + "}]";
}

void compile_copies(benchmark::State &state, std::string const &path)
{
    if (!cbor::path::compile<cbor::nodelist>(path)) {
        state.SkipWithError("cbor::path::compile refuses the path of the row");
        return;
    }
    std::size_t const copies = cold_bytes() / path.size() + 1;
    for (auto _ : state) {
        for (std::size_t i = 0; i < copies; ++i) {
            auto q = cbor::path::compile<cbor::nodelist>(path);
            benchmark::DoNotOptimize(q);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(copies));
}

constexpr std::size_t shallow_levels = 64;
constexpr std::size_t deep_levels = 1024;
constexpr std::size_t map_keys = 1000;

void path_compile_nested_64(benchmark::State &state)
{
    compile_copies(state, nested_literal_path(shallow_levels));
}

void path_compile_nested_1024(benchmark::State &state)
{
    compile_copies(state, nested_literal_path(deep_levels));
}

void path_compile_map_1000(benchmark::State &state)
{
    compile_copies(state, map_literal_path(map_keys));
}

void path_compile_flat(benchmark::State &state)
{
    compile_copies(state, "$.a[?@ == [1, 2]]");
}

}

BENCHMARK(path_compile)->Unit(benchmark::kMillisecond);
BENCHMARK(path_compile_nested_64)->Unit(benchmark::kMillisecond);
BENCHMARK(path_compile_nested_1024)->Unit(benchmark::kMillisecond);
BENCHMARK(path_compile_map_1000)->Unit(benchmark::kMillisecond);
BENCHMARK(path_compile_flat)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
