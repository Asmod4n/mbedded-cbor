// The bench of the block that writes and reads the heads of RFC 8949 section 3 (head.hpp, encode.hpp): every row
// reaches a head with an argument of 0, 1, 2, 4 and 8 bytes.
// heads_encoder: cbor::encoder writes integers of each width, a negative integer, an array, a map, a tag, a text and
// floats of two widths into a std::string.
// heads_encode_iov: cbor::encode_iov of a decoded tree with arguments of each width.
// heads_structs_encode and heads_schema_encode: a struct with a string, small integers, a list of structs and a list
// of doubles, written by cbor::structs and by cbor::schema.
// heads_decode_wide: cbor::decode of a message with arguments of 2, 4 and 8 bytes and a text with a 2-byte length.
// heads_item_size_long: cbor::item_size of an array of 256 maps, so the fast read of a head has 9 bytes after it.
// Each iteration handles every message or value of a buffer of COLD_BYTES bytes.
#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

using namespace std::string_view_literals;

constexpr std::uint64_t one_byte = 200;
constexpr std::uint64_t two_bytes = 1000;
constexpr std::uint64_t four_bytes = 100000;
constexpr std::uint64_t eight_bytes = 10000000000;
constexpr std::size_t long_array = 256;
constexpr std::size_t long_text = 300;
constexpr std::size_t encoder_elements = 9;
constexpr std::size_t wide_elements = 5;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::expected<void, cbor::error> heads_written(cbor::encoder<std::string> &e)
{
    std::expected<void, cbor::error> r = e.array(encoder_elements);
    r = r.and_then([&] { return e.unsigned_integer(std::uint8_t{5}); });
    r = r.and_then([&] { return e.unsigned_integer(static_cast<std::uint8_t>(one_byte)); });
    r = r.and_then([&] { return e.unsigned_integer(static_cast<std::uint16_t>(two_bytes)); });
    r = r.and_then([&] { return e.unsigned_integer(static_cast<std::uint32_t>(four_bytes)); });
    r = r.and_then([&] { return e.unsigned_integer(eight_bytes); });
    r = r.and_then([&] { return e.negative_integer(static_cast<std::uint16_t>(two_bytes)); });
    r = r.and_then([&] { return e.map(1); });
    r = r.and_then([&] { return e.text_string("a"sv); });
    r = r.and_then([&] { return e.floating_point(1.5); });
    r = r.and_then([&] { return e.tag(1800); });
    r = r.and_then([&] { return e.floating_point(1.1f); });
    r = r.and_then([&] { return e.floating_point(0.1); });
    return r.and_then([&] { return e.finish().transform([](std::size_t) {}); });
}

void heads_encoder(benchmark::State &state)
{
    std::string sample;
    cbor::encoder<std::string> check(sample);
    if (!heads_written(check) || !cbor::decode(sample)) {
        state.SkipWithError("cbor::encoder does not write a message that cbor::decode reads");
        return;
    }
    std::size_t const count = cold_bytes() / sample.size() + 1;
    std::string out;
    out.reserve(count * sample.size());
    for (auto _ : state) {
        out.clear();
        for (std::size_t i = 0; i < count; ++i) {
            cbor::encoder<std::string> e(out);
            auto const r = heads_written(e);
            benchmark::DoNotOptimize(r);
        }
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

std::string wide_message()
{
    std::string m;
    cbor::encoder<std::string> e(m);
    (void)e.array(wide_elements);
    (void)e.unsigned_integer(static_cast<std::uint16_t>(two_bytes));
    (void)e.unsigned_integer(static_cast<std::uint32_t>(four_bytes));
    (void)e.unsigned_integer(eight_bytes);
    (void)e.negative_integer(static_cast<std::uint32_t>(four_bytes));
    (void)e.text_string(std::string(long_text, 'x'));
    (void)e.finish();
    return m;
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
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

car const sample_car{"Volvo", 5, {{69, 2.5f}, {69, 2.5f}, {70, 2.25f}, {70, 2.25f}}, {1.5, 1.25, 0.1}};

template <template <class> class Codec>
void encode_car(benchmark::State &state)
{
    auto const sample = Codec<car>::encode(sample_car);
    if (!sample) {
        state.SkipWithError("the codec refuses the car");
        return;
    }
    std::size_t const count = cold_bytes() / sample->size() + 1;
    std::string out;
    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            out.clear();
            auto const n = Codec<car>::encode(sample_car, out);
            benchmark::DoNotOptimize(n);
        }
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

void heads_structs_encode(benchmark::State &state)
{
    encode_car<cbor::structs>(state);
}

void heads_schema_encode(benchmark::State &state)
{
    encode_car<cbor::schema>(state);
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
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

void heads_item_size_long(benchmark::State &state)
{
    std::string message;
    cbor::encoder<std::string> e(message);
    (void)e.array(long_array);
    for (std::size_t i = 0; i < long_array; ++i) {
        (void)e.map(1);
        (void)e.text_string("a"sv);
        (void)e.unsigned_integer(static_cast<std::uint16_t>(two_bytes));
    }
    (void)e.finish();
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
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * count));
}

}

BENCHMARK(heads_encoder)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_encode_iov)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_structs_encode)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_schema_encode)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_decode_wide)->Unit(benchmark::kMillisecond);
BENCHMARK(heads_item_size_long)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
