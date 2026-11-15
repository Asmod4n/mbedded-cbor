// The bench of the block cbor::schema<T>::decode and unverified_decode with a target (schema.hpp).
// Each iteration decodes every owner of a buffer of COLD_BYTES bytes of messages into a new target.
#include <benchmark/benchmark.h>
#include <cbor/describe.hpp>
#include <cbor/schema.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
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

template <bool Verified>
void decode(benchmark::State &state)
{
    auto const encoded = cbor::schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}});
    if (!encoded) {
        state.SkipWithError("cbor::schema<car>::encode refuses the car");
        return;
    }
    std::vector<std::shared_ptr<std::string const>> owners;
    for (std::size_t total = 0; total < cold_bytes(); total += encoded->size())
        owners.push_back(std::make_shared<std::string const>(*encoded));
    auto const sample = cbor::schema<car>::decode(owners.front(), car{});
    if (!sample || (*sample)->make != "Volvo") {
        state.SkipWithError("cbor::schema<car>::decode does not give the make of the car");
        return;
    }
    for (auto _ : state) {
        for (auto const &owner : owners) {
            if constexpr (Verified) {
                auto r = cbor::schema<car>::decode(owner, car{});
                benchmark::DoNotOptimize(r);
            } else {
                auto r = cbor::schema<car>::unverified_decode(owner, car{});
                benchmark::DoNotOptimize(r);
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(owners.size()));
}

}

BENCHMARK(decode<true>)->Name("schema_decode_target")->Unit(benchmark::kMillisecond);
BENCHMARK(decode<false>)->Name("schema_unverified_decode_target")->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
