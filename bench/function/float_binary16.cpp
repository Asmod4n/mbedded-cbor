// The bench of the binary16 encoder (head.hpp, detail::heads::float_encode_binary16), reached through cbor::encoder.
// float_binary16_encode: cbor::encoder writes 4096 doubles that binary16 holds exactly (subnormal and normal) into a
// std::string, so each value takes the half-precision path.
// --output_file=PATH writes the encoding of every binary16 value (all 65536 bit patterns, read as float) and of the
// 4096 inputs, and ends without timing; two builds that give the same file encode the same bytes.
#include <benchmark/benchmark.h>
#include <cbor/encode.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <stdfloat>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t value_count = 4096;
constexpr std::uint16_t first_input_half = 1;
constexpr std::uint16_t input_step_half = 7;

std::array<double, value_count> inputs()
{
    std::array<double, value_count> values{};
    for (std::size_t i = 0; i < value_count; ++i)
        values[i] = static_cast<double>(
            std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(first_input_half + i * input_step_half)));
    return values;
}

std::string encoded_halves_and_inputs()
{
    std::string out;
    {
    cbor::encoder e(out);
    for (std::uint32_t half = 0; half <= std::numeric_limits<std::uint16_t>::max(); ++half) {
        if (!e.floating_point(static_cast<double>(std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(half)))))
            std::terminate();
    }
    for (double const value : inputs())
        if (!e.floating_point(value))
            std::terminate();
    }
    return out;
}

void float_binary16_encode(benchmark::State &state)
{
    std::array<double, value_count> const values = inputs();
    std::string out;
    for (auto _ : state) {
        out.clear();
        {
            cbor::encoder e(out);
            for (double const value : values)
                benchmark::DoNotOptimize(e.floating_point(value));
        }
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(value_count));
}

}

BENCHMARK(float_binary16_encode);

int main(int argc, char **argv)
{
    constexpr std::string_view output_flag = "--output_file=";
    for (int i = 1; i < argc; ++i) {
        std::string_view const argument = argv[i];
        if (argument.starts_with(output_flag)) {
            std::ofstream file(std::string(argument.substr(output_flag.size())), std::ios::binary);
            file << encoded_halves_and_inputs();
            return file ? 0 : 1;
        }
    }
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
