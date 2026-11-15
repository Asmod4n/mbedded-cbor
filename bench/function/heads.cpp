// The bench of the block that writes and reads the heads of RFC 8949 section 3 (head.hpp, encode.hpp): every row
// reaches a head with an argument of 0, 1, 2, 4 and 8 bytes.
// heads_encode_iov: cbor::encode_iov of a decoded tree with arguments of each width.
// heads_structs_encode and heads_schema_encode: a struct with a string, small integers, a list of structs and a list
// of doubles, written by cbor::structs and by cbor::schema into a std::string; the rows with _span write the same
// struct one after the other into one span.
// heads_decode_wide: cbor::decode of a message with arguments of 2, 4 and 8 bytes and a text with a 2-byte length.
// heads_item_size_long: cbor::item_size of an array of 256 maps, so the fast read of a head has 9 bytes after it.
// heads_encode_large: cbor::encode of a tree of 100000 texts into one std::string, a message far larger than one
// block of cbor::encode.
// Each iteration handles every message or value of a buffer of COLD_BYTES bytes.
#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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

using namespace std::string_literals;
using namespace std::string_view_literals;

constexpr std::size_t long_array = 256;
constexpr std::size_t long_text = 300;
constexpr std::size_t large_texts = 100000;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::string wide_message()
{
    return "\x85\x19\x03\xe8\x1a\x00\x01\x86\xa0\x1b\x00\x00\x00\x02\x54\x0b\xe4\x00"
           "\x3a\x00\x01\x86\xa0\x79\x01\x2c"s +
           std::string(long_text, 'x');
}

void heads_encode_iov(benchmark::State &state)
{
    std::string const message = wide_message();
    auto const tree = cbor::decode(message);
    if (!tree || !cbor::encode_iov(**tree)) {
        state.SkipWithError("cbor::encode_iov refuses the tree of the wide message");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            auto r = cbor::encode_iov(**tree);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

car const sample_car{"Volvo", 5, {{69, 2.5f}, {69, 2.5f}, {70, 2.25f}, {70, 2.25f}}, {1.5, 1.25, 0.1}};

template <template <class> class Codec, bool Span = false>
void encode_car(benchmark::State &state)
{
    auto const sample = Codec<car>::encode(sample_car);
    if (!sample) {
        state.SkipWithError("the codec refuses the car");
        return;
    }
    std::size_t const count = cold_bytes() / sample->size() + 1;
    std::string out;
    std::string spans(count * sample->size(), '\0');
    for (auto _ : state) {
        if constexpr (Span) {
            std::span<char> rest(spans);
            for (std::size_t i = 0; i < count; ++i) {
                auto n = Codec<car>::encode(sample_car, rest);
                benchmark::DoNotOptimize(n);
                rest = rest.subspan(*n);
            }
            benchmark::DoNotOptimize(spans.data());
        } else {
            for (std::size_t i = 0; i < count; ++i) {
                out.clear();
                auto n = Codec<car>::encode(sample_car, out);
                benchmark::DoNotOptimize(n);
            }
            benchmark::DoNotOptimize(out.data());
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

void heads_structs_encode(benchmark::State &state)
{
    encode_car<cbor::structs>(state);
}

void heads_schema_encode(benchmark::State &state)
{
    encode_car<cbor::schema>(state);
}

void heads_structs_encode_span(benchmark::State &state)
{
    encode_car<cbor::structs, true>(state);
}

void heads_schema_encode_span(benchmark::State &state)
{
    encode_car<cbor::schema, true>(state);
}

void heads_decode_wide(benchmark::State &state)
{
    std::string const message = wide_message();
    if (!cbor::decode(message)) {
        state.SkipWithError("cbor::decode refuses the wide message");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    std::vector<std::string> messages(count, message);
    for (auto _ : state) {
        for (std::string const &each : messages) {
            auto r = cbor::decode(each);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

void heads_item_size_long(benchmark::State &state)
{
    std::string message = "\x99\x01\x00"s;
    for (std::size_t i = 0; i < long_array; ++i)
        message += "\xa1\x61" "a\x19\x03\xe8"sv;
    auto const size = cbor::item_size(message);
    if (!size || *size != message.size()) {
        state.SkipWithError("cbor::item_size does not give the size of the long message");
        return;
    }
    std::size_t const count = cold_bytes() / message.size() + 1;
    std::vector<std::string> messages(count, message);
    for (auto _ : state) {
        for (std::string const &each : messages) {
            auto r = cbor::item_size(each);
            benchmark::DoNotOptimize(r);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<benchmark::IterationCount>(count));
}

void heads_encode_large(benchmark::State &state)
{
    std::string message = "\x9a\x00\x01\x86\xa0"s;
    for (std::size_t i = 0; i < large_texts; ++i)
        message += "\x68" "abcdefgh"sv;
    auto const tree = cbor::decode(message);
    std::string out;
    if (!tree || !cbor::encode(**tree, out) || out != message) {
        state.SkipWithError("cbor::encode does not give the large message back");
        return;
    }
    for (auto _ : state) {
        out.clear();
        auto n = cbor::encode(**tree, out);
        benchmark::DoNotOptimize(n);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations());
}

}

BENCHMARK(heads_encode_iov)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_structs_encode)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_schema_encode)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_structs_encode_span)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_schema_encode_span)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_decode_wide)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_item_size_long)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_encode_large)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
