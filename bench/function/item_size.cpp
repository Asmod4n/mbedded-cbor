// The bench of cbor::item_size (item_size.hpp), which checks the count of pending items of each array and map
// against the bytes left (validity.hpp).
// item_size_arrays: item_size of a message of nested arrays and maps, 4096 times in one iteration.
// --output_file=PATH writes the size that item_size gives for each message and ends without timing; two builds that
// give the same file give the same sizes.
#include <benchmark/benchmark.h>
#include <cbor/item_size.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <string>
#include <string_view>

namespace {

using namespace std::string_view_literals;

constexpr std::size_t repeats = 4096;
// [1, 2, 3, [4, 5, [6, 7]], {"a": 1, "b": [2, 3], "c": {"d": 4}}, h'0102', "text", 1.5]
constexpr std::string_view message =
    "\x88\x01\x02\x03\x83\x04\x05\x82\x06\x07\xa3\x61\x61\x01\x61\x62\x82\x02\x03\x61\x63\xa1\x61\x64\x04\x42\x01\x02"
    "\x64text\xf9\x3e\x00"sv;

void item_size_arrays(benchmark::State &state)
{
    for (auto _ : state) {
        std::size_t sum = 0;
        for (std::size_t i = 0; i < repeats; ++i) {
            std::string_view bytes = message;
            benchmark::DoNotOptimize(bytes);
            auto const size = cbor::item_size(bytes);
            sum += *size;
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(repeats));
}

}

BENCHMARK(item_size_arrays);

int main(int argc, char **argv)
{
    constexpr std::string_view output_flag = "--output_file=";
    for (int i = 1; i < argc; ++i) {
        std::string_view const argument = argv[i];
        if (argument.starts_with(output_flag)) {
            std::ofstream file(std::string(argument.substr(output_flag.size())), std::ios::binary);
            for (std::size_t cut = 0; cut <= message.size(); ++cut) {
                auto const size = cbor::item_size(message.substr(0, cut));
                file << (size ? std::to_string(*size) : std::string("error")) << '\n';
            }
            return file ? 0 : 1;
        }
    }
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
