// The bench of the heads of major types 0, 1, 4 and 6 (head.hpp, encode.hpp), reached through cbor::encoder.
// encoder_heads_encode: cbor::encoder writes 4096 items into a std::string: an unsigned integer of each argument
// width (RFC 8949 3), a negative integer, an array head and a tag head, so each item takes one head.
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
constexpr std::array<std::uint64_t, 9> arguments{
    0, 23, 24, 255, 256, 65535, 65536, 4294967295ull, 4294967296ull};

bool write_item(cbor::encoder<std::string> &e, std::size_t const i)
{
    std::uint64_t const argument = arguments[i % arguments.size()];
    switch (i / arguments.size() % 4) {
    case 0: return e.unsigned_integer(argument).has_value();
    case 1: return e.negative_integer(argument).has_value();
    case 2: return e.array(argument % 24).has_value();
    default: return e.tag(argument).has_value();
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

void encoder_heads_encode(benchmark::State &state)
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

BENCHMARK(encoder_heads_encode);

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
