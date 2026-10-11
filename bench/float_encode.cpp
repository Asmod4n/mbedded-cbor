#include "value.hpp"

#include <benchmark/benchmark.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <stdfloat>
#include <string>
#include <vector>

#if defined(__STDCPP_FLOAT16_T__)
using half = std::float16_t;
#else
using half = _Float16;
#endif

static std::string read_file(char const *const path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static std::uint64_t be(std::string const &s, std::size_t const at, std::size_t const n)
{
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i)
        v = v << 8 | static_cast<std::uint8_t>(s[at + i]);
    return v;
}

static bench::value floats_tree(std::string const &doc)
{
    std::size_t at = 0;
    std::uint8_t const ib = static_cast<std::uint8_t>(doc[at++]);
    std::size_t const width = (ib & 31u) < 24 ? 0 : std::size_t{1} << ((ib & 31u) - 24);
    std::uint64_t const n = width == 0 ? (ib & 31u) : be(doc, at, width);
    at += width;
    bench::array a;
    for (std::uint64_t i = 0; i < n; ++i) {
        std::uint8_t const h = static_cast<std::uint8_t>(doc[at++]);
        double d;
        if (h == 0xf9) {
            d = static_cast<double>(std::bit_cast<half>(static_cast<std::uint16_t>(be(doc, at, 2))));
            at += 2;
        } else if (h == 0xfa) {
            d = std::bit_cast<float>(static_cast<std::uint32_t>(be(doc, at, 4)));
            at += 4;
        } else if (h == 0xfb) {
            d = std::bit_cast<double>(be(doc, at, 8));
            at += 8;
        } else {
            std::abort();
        }
        a.push_back(bench::value{d});
    }
    return bench::value{std::move(a)};
}

static std::string const doc = read_file(DOC_PATH);
static bench::value const tree = floats_tree(doc);
static std::vector<char> buf(2 * doc.size());

static void run(benchmark::State &state)
{
    for (auto _ : state) {
        bench::binding b;
#if defined(OLD_ENCODE)
        if (!cbor::encode<128, cbor::sharedrefs::off>(b, std::span<char>(buf), tree))
#else
        if (!cbor::encode(b, std::span<char>(buf), tree))
#endif
            std::abort();
        benchmark::ClobberMemory();
    }
}

int main(int argc, char **argv)
{
    benchmark::AddCustomContext("document", DOC_PATH);
    benchmark::RegisterBenchmark(ARM_NAME, run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
