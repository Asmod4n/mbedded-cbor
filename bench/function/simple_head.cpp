// The bench of the heads of major type 7 (head.hpp, encode.hpp), reached through cbor::encoder.
// simple_head_encode: cbor::encoder writes 4096 items into a std::string. Each item is a simple value (false, true,
// null, undefined, 16, 32) or a floating-point number (1.5, 0.1f as a float, 0.1 as a double), so each item takes a
// head of major type 7.
// --output_file=PATH writes the encoding of the 4096 items and ends without timing; two builds that give the same
// file encode the same bytes.
#include <benchmark/benchmark.h>
#include <cbor/encode.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t item_count = 4096;
constexpr std::size_t kinds = 9;
constexpr std::uint8_t simple_value_two_byte_form = 32;
constexpr std::uint8_t simple_value_one_byte_form = 16;
constexpr float single_precision_sample = 0.1f;
constexpr double double_precision_sample = 0.1;
constexpr double half_precision_sample = 1.5;

bool write_item(cbor::encoder<std::string> &e, std::size_t const i)
{
    switch (i % kinds) {
    case 0: return e.simple_value(cbor::simple_value::false_value).has_value();
    case 1: return e.simple_value(cbor::simple_value::true_value).has_value();
    case 2: return e.simple_value(cbor::simple_value::null).has_value();
    case 3: return e.simple_value(cbor::simple_value::undefined).has_value();
    case 4: return e.simple_value(cbor::simple_value{simple_value_one_byte_form}).has_value();
    case 5: return e.simple_value(cbor::simple_value{simple_value_two_byte_form}).has_value();
    case 6: return e.floating_point(half_precision_sample).has_value();
    case 7: return e.floating_point(single_precision_sample).has_value();
    default: return e.floating_point(double_precision_sample).has_value();
    }
}

std::string encoded_items()
{
    std::string out;
    {
        cbor::encoder e(out);
        for (std::size_t i = 0; i < item_count; ++i)
            if (!write_item(e, i))
                std::terminate();
    }
    return out;
}

void simple_head_encode(benchmark::State &state)
{
    std::string out;
    for (auto _ : state) {
        out.clear();
        {
            cbor::encoder e(out);
            for (std::size_t i = 0; i < item_count; ++i)
                benchmark::DoNotOptimize(write_item(e, i));
        }
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(item_count));
}

}

BENCHMARK(simple_head_encode);

int main(int argc, char **argv)
{
    constexpr std::string_view output_flag = "--output_file=";
    for (int i = 1; i < argc; ++i) {
        std::string_view const argument = argv[i];
        if (argument.starts_with(output_flag)) {
            std::ofstream file(std::string(argument.substr(output_flag.size())), std::ios::binary);
            file << encoded_items();
            return file ? 0 : 1;
        }
    }
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
