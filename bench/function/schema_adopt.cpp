// The bench of the block cbor::schema<T>::adopt and unverified_adopt (schema.hpp). A build of a commit before the
// rename uses the earlier names path and unverified_path.
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

template <bool Verified, class S>
auto opened(std::string &&message)
{
    if constexpr (requires { S::adopt(std::move(message)); }) {
        if constexpr (Verified)
            return S::adopt(std::move(message));
        else
            return S::unverified_adopt(std::move(message));
    } else {
        if constexpr (Verified)
            return S::path(std::move(message));
        else
            return S::unverified_path(std::move(message));
    }
}

template <bool Verified>
void open(benchmark::State &state)
{
    auto const encoded = cbor::schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}});
    if (!encoded) {
        state.SkipWithError("cbor::schema<car>::encode refuses the car");
        return;
    }
    std::string const message = *encoded;
    auto const sample = opened<true, cbor::schema<car>>(std::string(message));
    if (!sample || sample->at_path<"$.make">() != "Volvo") {
        state.SkipWithError("cbor::schema<car>::adopt does not give the make of the car");
        return;
    }
    std::size_t const count = messages_of(message, cold_bytes()).size();
    for (auto _ : state) {
        state.PauseTiming();
        std::vector<std::string> messages = messages_of(message, cold_bytes());
        state.ResumeTiming();
        for (std::string &each : messages) {
            auto r = opened<Verified, cbor::schema<car>>(std::move(each));
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

}

BENCHMARK(open<true>)->Name("schema_adopt")->Unit(benchmark::kMillisecond);
BENCHMARK(open<false>)->Name("schema_unverified_adopt")->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
