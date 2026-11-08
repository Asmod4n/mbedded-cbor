// The bench of the block cbor::schema<T>::path and unverified_path in the rvalue form (schema.hpp).
// Each iteration opens every message of a buffer of COLD_BYTES bytes of messages, each moved into its owner.
#include <benchmark/benchmark.h>
#include <cbor/describe.hpp>
#include <cbor/schema.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct tire {
    std::uint16_t diameter;
    float pressure;
};

struct car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
};

}

CBOR_DEFINE_TYPE_MEMBERS(tire, cbor::tag{1800}, diameter, pressure);
CBOR_DEFINE_TYPE_MEMBERS(car, cbor::tag{1801}, make, seats, tires, track);

namespace {

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::vector<std::string> messages_of(std::string const &message, std::size_t const bytes)
{
    return std::vector<std::string>(bytes / message.size() + 1, message);
}

template <bool Verified>
void open(benchmark::State &state)
{
    std::string const message = cbor::schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}});
    auto const sample = cbor::schema<car>::path(std::string(message));
    if (!sample || sample->at_path<"$.make">() != "Volvo") {
        state.SkipWithError("cbor::schema<car>::path does not give the make of the car");
        return;
    }
    std::size_t const count = messages_of(message, cold_bytes()).size();
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::string> messages = messages_of(message, cold_bytes());
        state.ResumeTiming();
        for (std::string &each : messages) {
            if constexpr (Verified) {
                auto r = cbor::schema<car>::path(std::move(each));
                benchmark::DoNotOptimize(r);
            } else {
                auto r = cbor::schema<car>::unverified_path(std::move(each));
                benchmark::DoNotOptimize(r);
            }
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

}

BENCHMARK(open<true>)->Name("schema_path")->Unit(benchmark::kMillisecond);
BENCHMARK(open<false>)->Name("schema_unverified_path")->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
