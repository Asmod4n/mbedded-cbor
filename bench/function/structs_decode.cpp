// The bench of the block cbor::structs<T>::decode with a member that the struct does not have, under tag 28
// (structs.hpp, item_size.hpp). Each iteration decodes every message of a buffer of COLD_BYTES bytes of messages.
#include <benchmark/benchmark.h>
#include <cbor/describe.hpp>
#include <cbor/structs.hpp>

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

struct person {
    std::string name;
    std::int64_t age;
};

}

CBOR_DEFINE_TYPE_MEMBERS(person, cbor::tag{1900}, name, age);

namespace {

constexpr std::string_view message =
    "\xd9\x07\x6c\xa3\x63"
    "age\x18\x1e\x64name\x63"
    "Ann\x61x\xd8\x1c\x82\x01\x02"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

void structs_decode(benchmark::State &state)
{
    auto const owner = std::make_shared<std::string const>(message);
    auto const sample = cbor::structs<person>::decode(owner);
    if (!sample || (*sample)->name != "Ann" || (*sample)->age != 30) {
        state.SkipWithError("cbor::structs<person>::decode does not give Ann, 30");
        return;
    }
    std::vector<std::shared_ptr<std::string const>> owners;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size())
        owners.push_back(std::make_shared<std::string const>(message));
    for (auto _ : state) {
        for (auto const &each : owners) {
            auto r = cbor::structs<person>::decode(each);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * owners.size()));
}

}

BENCHMARK(structs_decode)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
