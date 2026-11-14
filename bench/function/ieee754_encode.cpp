// The bench of the float conversion (head.hpp before the move, ieee754.hpp after): preferred_float_info and encode_float,
// reached through cbor::encoder::floating_point.
// ieee754_encode: cbor::encoder writes 6144 doubles into a std::string: a third that binary16 holds, a third that only
// binary32 holds and a third that only binary64 holds, so each width of RFC 8949 section 3.3 is taken.
// --output_file=PATH writes the encoding of the 6144 values and ends without timing; two builds that give the same
// file encode the same bytes.
#include <benchmark/benchmark.h>
#include <cbor/encode.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <stdfloat>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t widths = 3;
constexpr std::size_t value_count = 2048 * widths;
constexpr std::uint16_t first_half = 1;
constexpr std::uint16_t half_step = 31;
constexpr std::uint32_t first_single_bits = 0x3f800001u;
constexpr std::uint32_t single_step = 0x2001u;
constexpr std::uint64_t first_double_bits = 0x3ff0000000000001ull;
constexpr std::uint64_t double_step = 0x20000000001ull;

std::array<double, value_count> inputs()
{
    std::array<double, value_count> values{};
    for (std::size_t i = 0; i < value_count; ++i) {
        std::size_t const k = i / widths;
        switch (i % widths) {
        case 0:
            values[i] = static_cast<double>(
                std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(first_half + k * half_step)));
            break;
        case 1:
            values[i] = static_cast<double>(
                std::bit_cast<float>(static_cast<std::uint32_t>(first_single_bits + k * single_step)));
            break;
        default:
            values[i] = std::bit_cast<double>(first_double_bits + k * double_step);
        }
    }
    return values;
}

std::string encoded_inputs()
{
    std::string out;
    {
        cbor::encoder e(out);
        for (double const value : inputs())
            if (!e.floating_point(value))
                std::terminate();
    }
    return out;
}

void ieee754_encode(benchmark::State &state)
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

BENCHMARK(ieee754_encode);

int main(int argc, char **argv)
{
    constexpr std::string_view output_flag = "--output_file=";
    for (int i = 1; i < argc; ++i) {
        std::string_view const argument = argv[i];
        if (argument.starts_with(output_flag)) {
            std::ofstream file(std::string(argument.substr(output_flag.size())), std::ios::binary);
            file << encoded_inputs();
            return file ? 0 : 1;
        }
    }
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
