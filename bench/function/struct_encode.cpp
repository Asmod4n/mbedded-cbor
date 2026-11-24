// The bench of cbor::encode(T const &) against cbor::structs<T>::encode(T const &) for one described struct
// (structs.hpp). Each iteration writes every value of a buffer of values whose bytes fill COLD_BYTES, into a new
// std::string. Both functions start with the same bytes, which the bench checks once.
#include <benchmark/benchmark.h>
#include <cbor/describe.hpp>
#include <cbor/structs.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct person {
    std::string name;
    std::int64_t age;
    std::vector<double> scores;
};

}

CBOR_DEFINE_TYPE_MEMBERS(person, cbor::tag{1900}, name, age, scores);

namespace {

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::vector<person> values()
{
    person const sample{"Ann", 30, {1.5, 2.5, 3.5}};
    auto const encoded = cbor::encode(sample);
    if (!encoded)
        throw std::logic_error("bench: the sample does not encode");
    std::vector<person> all(cold_bytes() / encoded->size() + 1, sample);
    for (std::size_t i = 0; i < all.size(); ++i)
        all[i].age = static_cast<std::int64_t>(i);
    return all;
}

bool same_bytes(std::vector<person> const &all)
{
    for (person const &p : all) {
        auto const free_form = cbor::encode(p);
        auto const member_form = cbor::structs<person>::encode(p);
        if (!free_form || !member_form || *free_form != *member_form)
            return false;
    }
    return true;
}

template <bool Member>
void encode_person(benchmark::State &state)
{
    std::vector<person> const all = values();
    if (!same_bytes(all)) {
        state.SkipWithError("cbor::encode and cbor::structs<person>::encode give different bytes");
        return;
    }
    for (auto _ : state) {
        std::size_t total = 0;
        for (person const &p : all) {
            if constexpr (Member) {
                auto const bytes = cbor::structs<person>::encode(p);
                total += bytes->size();
            } else {
                auto const bytes = cbor::encode(p);
                total += bytes->size();
            }
        }
        benchmark::DoNotOptimize(total);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(all.size()));
}

}

BENCHMARK(encode_person<false>)->Name("encode_free")->Unit(benchmark::kMillisecond);
BENCHMARK(encode_person<true>)->Name("encode_structs")->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
