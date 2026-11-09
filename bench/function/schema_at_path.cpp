// The bench of the block of the reads of a schema accessor: at_path of a text and of a field in a list (schema.hpp).
// Each iteration reads every accessor of a buffer of COLD_BYTES bytes of messages.
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

void schema_at_path(benchmark::State &state)
{
    auto const encoded = cbor::schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}});
    if (!encoded) {
        state.SkipWithError("cbor::schema<car>::encode refuses the car");
        return;
    }
    auto const owner = std::make_shared<std::string const>(*encoded);
    auto const sample = cbor::schema<car>::path(owner);
    if (!sample || sample->at_path<"$.make">() != "Volvo" || sample->at_path<"$.tires[1].diameter">() != 18) {
        state.SkipWithError("the accessor of the car does not give Volvo and 18");
        return;
    }
    std::vector<decltype(cbor::schema<car>::path(owner))> accessors;
    for (std::size_t total = 0; total < cold_bytes(); total += encoded->size())
        accessors.push_back(cbor::schema<car>::path(std::make_shared<std::string const>(*encoded)));
    for (auto _ : state) {
        std::size_t sum = 0;
        for (auto const &each : accessors) {
            sum += each->at_path<"$.make">()->size();
            sum += *each->at_path<"$.tires[1].diameter">();
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(accessors.size()));
}

}

BENCHMARK(schema_at_path)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
