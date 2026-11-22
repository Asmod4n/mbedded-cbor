// The bench of the readers operator* of cbor::result (result.hpp).
// Each iteration reads the value of every result of a buffer of COLD_BYTES bytes of messages through auto const &:
// of a named result (read_*), and of a result that a copy of the buffer gives as a temporary (move_*), where the arm
// allows it. All results lie in one block that starts on a cache line, and every ref shares one owner.
#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::string_view message = "\xa2\x61\x61\x61\x78\x61\x62\x82\x02\x03"sv;
constexpr std::size_t cache_line = 64;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::size_t message_count()
{
    return cold_bytes() / message.size() + 1;
}

template <class R>
concept readable_temporary = requires(R &&r) { *std::move(r); };

template <class R, class Make, class Read>
void read_named(benchmark::State &state, Make const make, Read const read)
{
    std::size_t space = message_count() * sizeof(R) + cache_line;
    auto const block = std::make_unique_for_overwrite<std::byte[]>(space);
    void *start = block.get();
    if (std::align(cache_line, message_count() * sizeof(R), start, space) == nullptr)
        throw std::logic_error("bench: the block holds no aligned start");
    std::pmr::monotonic_buffer_resource arena(start, space, std::pmr::null_memory_resource());
    std::pmr::vector<R> results(&arena);
    results.reserve(message_count());
    for (std::size_t i = 0; i < message_count(); ++i)
        results.push_back(make());
    for (auto _ : state) {
        std::uint64_t sum = 0;
        for (R const &r : results) {
            auto const &v = *r;
            sum += read(v);
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(results.size()));
}

template <class R, class Make, class Read>
void read_moved(benchmark::State &state, Make const make, Read const read)
{
    std::vector<R> filled;
    filled.reserve(message_count());
    for (std::size_t i = 0; i < message_count(); ++i)
        filled.push_back(make());
    for (auto _ : state) {
        std::uint64_t sum = 0;
        std::vector<R> results = filled;
        for (R &r : results) {
            if constexpr (readable_temporary<R>) {
                auto const &v = *std::move(r);
                sum += read(v);
            } else {
                auto const &v = *r;
                sum += read(v);
            }
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(filled.size()));
}

auto const owner = std::make_shared<std::string const>(message);

auto make_ref()
{
    return cbor::at_path<"$.a", std::string_view>(owner);
}

auto make_scalar()
{
    return cbor::at_path<"$.b[0]", std::uint64_t>(message);
}

auto const nodes_query = cbor::path::compile<cbor::nodelist>("$.*");

auto make_nodelist()
{
    if (!nodes_query)
        throw std::logic_error("bench: the query $.* does not compile");
    return cbor::at_path(owner, *nodes_query);
}

auto const text_size = [](auto const &text) { return static_cast<std::uint64_t>((*text).size()); };
auto const number = [](std::uint64_t const n) { return n; };
auto const node_count = [](auto const &nodes) { return static_cast<std::uint64_t>(nodes.size()); };

void read_ref(benchmark::State &state)
{
    read_named<decltype(make_ref())>(state, make_ref, text_size);
}

void read_scalar(benchmark::State &state)
{
    read_named<decltype(make_scalar())>(state, make_scalar, number);
}

void read_nodelist(benchmark::State &state)
{
    read_named<decltype(make_nodelist())>(state, make_nodelist, node_count);
}

void move_ref(benchmark::State &state)
{
    read_moved<decltype(make_ref())>(state, make_ref, text_size);
}

void move_nodelist(benchmark::State &state)
{
    read_moved<decltype(make_nodelist())>(state, make_nodelist, node_count);
}

}

BENCHMARK(read_ref)->Unit(benchmark::kMillisecond);
BENCHMARK(read_scalar)->Unit(benchmark::kMillisecond);
BENCHMARK(read_nodelist)->Unit(benchmark::kMillisecond);
BENCHMARK(move_ref)->Unit(benchmark::kMillisecond);
BENCHMARK(move_nodelist)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
