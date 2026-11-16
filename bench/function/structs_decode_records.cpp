// The bench of cbor::structs<T>::decode into a new object, read in place through decode(owner, view) (structs.hpp).
// The bytes come from cbor::structs<T>::encode. structs_decode_records: a batch of records; the argument is the number
// of records (26 records are 2.3 KB of CBOR, 1662 records 150 KB). structs_decode_map, _set and _multimap: a container
// of distinct keys; the argument is the number of entries.
#include <benchmark/benchmark.h>
#include <cbor/structs.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct nested_t {
    double x;
    std::optional<double> y;
    bool operator==(nested_t const &) const = default;
};

struct record {
    std::uint64_t id;
    std::string name;
    double score;
    std::int64_t delta;
    bool active;
    std::vector<std::string> tags;
    nested_t nested;
    bool operator==(record const &) const = default;
};

struct batch {
    std::vector<record> records;
    bool operator==(batch const &) const = default;
};

batch make_batch(std::size_t const count)
{
    constexpr std::uint64_t seed = 88172645463325252u;
    constexpr int shift_a = 13;
    constexpr int shift_b = 7;
    constexpr int shift_c = 17;
    constexpr std::uint64_t id_range = 100000;
    constexpr std::uint64_t name_range = 1000;
    constexpr std::uint64_t escaped_name_every = 4;
    constexpr double hundredths = 100.0;
    constexpr std::uint64_t delta_range = 5000;
    constexpr double nested_x = 1.5e3;
    std::uint64_t state = seed;
    auto const next = [&state] {
        state ^= state << shift_a;
        state ^= state >> shift_b;
        state ^= state << shift_c;
        return state;
    };
    batch b;
    for (std::size_t i = 0; i < count; ++i) {
        record r;
        r.id = next() % id_range;
        r.name = "user " + std::to_string(next() % name_range) + (next() % escaped_name_every == 0 ? "\né" : "");
        r.score = static_cast<double>(next() % (id_range)) / hundredths;
        r.delta = -static_cast<std::int64_t>(next() % delta_range);
        r.active = next() % 2 == 1;
        r.tags = {"alpha", "beta", "gamma"};
        r.nested = {nested_x, std::nullopt};
        b.records.push_back(r);
    }
    return b;
}

void structs_decode_records(benchmark::State &state)
{
    batch const source = make_batch(static_cast<std::size_t>(state.range(0)));
    auto const encoded = cbor::structs<batch>::encode(source);
    if (!encoded) {
        state.SkipWithError("cbor::structs<batch>::encode refuses the batch");
        return;
    }
    auto const owner = std::make_shared<std::string const>(*encoded);
    auto const sample = cbor::structs<batch>::decode(owner, std::string_view(*owner));
    if (!sample || **sample != source) {
        state.SkipWithError("cbor::structs<batch>::decode does not give the batch back");
        return;
    }
    for (auto _ : state) {
        auto r = cbor::structs<batch>::decode(owner, std::string_view(*owner));
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}


template <class C>
C make_container(std::size_t const count)
{
    constexpr std::uint64_t key_step = 7919;
    C c;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint64_t const key = static_cast<std::uint64_t>(i) * key_step;
        if constexpr (requires { typename C::mapped_type; })
            c.emplace(key, key + 1);
        else
            c.emplace(key);
    }
    return c;
}

template <class C>
void structs_decode_container(benchmark::State &state)
{
    C const source = make_container<C>(static_cast<std::size_t>(state.range(0)));
    auto const encoded = cbor::structs<C>::encode(source);
    if (!encoded) {
        state.SkipWithError("cbor::structs encode refuses the container");
        return;
    }
    auto const owner = std::make_shared<std::string const>(*encoded);
    auto const sample = cbor::structs<C>::decode(owner, std::string_view(*owner));
    if (!sample || **sample != source) {
        state.SkipWithError("cbor::structs decode does not give the container back");
        return;
    }
    for (auto _ : state) {
        auto r = cbor::structs<C>::decode(owner, std::string_view(*owner));
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

}

BENCHMARK(structs_decode_records)->Arg(26)->Arg(1662);
BENCHMARK(structs_decode_container<std::map<std::uint64_t, std::uint64_t>>)->Name("structs_decode_map")->Arg(26)->Arg(1662);
BENCHMARK(structs_decode_container<std::set<std::uint64_t>>)->Name("structs_decode_set")->Arg(26)->Arg(1662);
BENCHMARK(structs_decode_container<std::multimap<std::uint64_t, std::uint64_t>>)->Name("structs_decode_multimap")->Arg(26)->Arg(1662);
BENCHMARK(structs_decode_container<std::unordered_multimap<std::uint64_t, std::uint64_t>>)->Name("structs_decode_unordered_multimap")->Arg(26)->Arg(1662);
BENCHMARK_MAIN();
