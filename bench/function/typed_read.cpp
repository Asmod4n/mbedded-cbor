// The bench of the typed read at the end of a path and of lazy::get (query.hpp try_walk_bytes, lazy.hpp get).
// at_path_double_float, at_path_double_bignum: cbor::at_path over bytes with a compiled query $.a, the value a float
// and a bignum 3(h'05'). at_path_text: the same over bytes with their owner, the value a text, read as a view.
// lazy_get_double, lazy_get_text, lazy_get_integer: lazy::get of the value of the key "a".
// Each iteration reads every message of a buffer of COLD_BYTES bytes of messages.
#include <benchmark/benchmark.h>
#include <cbor/query.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::string_view float_message = "\xa1\x61\x61\xfb\x3f\xf8\x00\x00\x00\x00\x00\x00"sv;
constexpr std::string_view bignum_message = "\xa1\x61\x61\xc3\x41\x05"sv;
constexpr std::string_view text_message = "\xa1\x61\x61\x63\x61\x62\x63"sv;
constexpr std::string_view integer_message = "\xa1\x61\x61\x19\x03\xe8"sv;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

template <class T>
void at_path_bytes(benchmark::State &state, std::string_view const message, T const expected)
{
    auto const query = cbor::path::compile<T>("$.a");
    if (!query) {
        state.SkipWithError("cbor::path::compile refuses $.a");
        return;
    }
    auto const sample = cbor::at_path(message, *query);
    if (!sample || *sample != expected) {
        state.SkipWithError("the query $.a does not give the value of the message");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    std::vector<std::string> const messages(count, std::string(message));
    for (auto _ : state) {
        for (std::string const &each : messages) {
            auto r = cbor::at_path(std::string_view(each), *query);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

template <class T>
void lazy_get(benchmark::State &state, std::string_view const message, T const expected)
{
    auto const top = cbor::lazy::view(std::make_shared<std::string const>(message));
    auto const value = top ? top->at("a") : std::unexpected(top.error());
    auto const got = value ? value->template get<T>() : std::unexpected(value.error());
    bool const same = [&] {
        if (!got)
            return false;
        if constexpr (std::is_same_v<T, std::string_view>)
            return **got == expected;
        else
            return *got == expected;
    }();
    if (!same) {
        state.SkipWithError("lazy::get does not give the value of the key a");
        return;
    }
    std::vector<cbor::lazy> lazies;
    for (std::size_t total = 0; total < cold_bytes(); total += message.size())
        lazies.push_back(*cbor::lazy::view(std::make_shared<std::string const>(message))->at("a"));
    for (auto _ : state) {
        for (cbor::lazy const &each : lazies) {
            auto r = each.template get<T>();
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(lazies.size()));
}

void at_path_text(benchmark::State &state)
{
    auto const query = cbor::path::compile<std::string_view>("$.a");
    if (!query) {
        state.SkipWithError("cbor::path::compile refuses $.a");
        return;
    }
    std::size_t const count = cold_bytes() / text_message.size() + 1;
    std::vector<std::shared_ptr<std::string const>> messages;
    std::vector<std::shared_ptr<void const>> owners;
    for (std::size_t i = 0; i < count; ++i) {
        auto message = std::make_shared<std::string const>(text_message);
        owners.push_back(message);
        messages.push_back(std::move(message));
    }
    auto const sample = cbor::at_path(owners.front(), std::string_view(*messages.front()), *query);
    if (!sample || **sample != "abc"sv) {
        state.SkipWithError("the query $.a does not give the text abc");
        return;
    }
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            auto r = cbor::at_path(owners[i], std::string_view(*messages[i]), *query);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

void at_path_double_float(benchmark::State &state)
{
    at_path_bytes<double>(state, float_message, 1.5);
}

void at_path_double_bignum(benchmark::State &state)
{
    at_path_bytes<double>(state, bignum_message, -6.0);
}

void lazy_get_double(benchmark::State &state)
{
    lazy_get<double>(state, float_message, 1.5);
}

void lazy_get_text(benchmark::State &state)
{
    lazy_get<std::string_view>(state, text_message, "abc"sv);
}

void lazy_get_integer(benchmark::State &state)
{
    lazy_get<std::int64_t>(state, integer_message, 1000);
}

}

BENCHMARK(at_path_double_float)->Unit(benchmark::kMillisecond);
BENCHMARK(at_path_double_bignum)->Unit(benchmark::kMillisecond);
BENCHMARK(at_path_text)->Unit(benchmark::kMillisecond);
BENCHMARK(lazy_get_double)->Unit(benchmark::kMillisecond);
BENCHMARK(lazy_get_text)->Unit(benchmark::kMillisecond);
BENCHMARK(lazy_get_integer)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
