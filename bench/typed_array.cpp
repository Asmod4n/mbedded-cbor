#include <cbor/cbor.hpp>

#include <benchmark/benchmark.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdfloat>
#include <string>
#include <vector>

static_assert(std::endian::native == std::endian::little, "The arms write little-endian typed arrays from memory.");

#if defined(__STDCPP_FLOAT16_T__)
using half = std::float16_t;
#else
using half = _Float16;
#endif

#if defined(DOC_floats)
using element = double;
constexpr std::uint64_t tag = 86;
#else
using element = std::int64_t;
constexpr std::uint64_t tag = 79;
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

static std::vector<element> values(std::string const &doc)
{
    std::size_t at = 0;
    auto const argument = [&]() {
        std::uint8_t const ib = static_cast<std::uint8_t>(doc[at++]);
        std::size_t const width = (ib & 31u) < 24 ? 0 : std::size_t{1} << ((ib & 31u) - 24);
        std::uint64_t const a = width == 0 ? (ib & 31u) : be(doc, at, width);
        at += width;
        return std::pair{ib >> 5, a};
    };
    auto const [major, n] = argument();
    if (major != 4)
        std::abort();
    std::vector<element> v;
    for (std::uint64_t i = 0; i < n; ++i) {
#if defined(DOC_floats)
        std::uint8_t const h = static_cast<std::uint8_t>(doc[at++]);
        if (h == 0xf9) {
            v.push_back(static_cast<double>(std::bit_cast<half>(static_cast<std::uint16_t>(be(doc, at, 2)))));
            at += 2;
        } else if (h == 0xfa) {
            v.push_back(std::bit_cast<float>(static_cast<std::uint32_t>(be(doc, at, 4))));
            at += 4;
        } else if (h == 0xfb) {
            v.push_back(std::bit_cast<double>(be(doc, at, 8)));
            at += 8;
        } else {
            std::abort();
        }
#else
        auto const [m, a] = argument();
        v.push_back(m == 0 ? static_cast<std::int64_t>(a) : -1 - static_cast<std::int64_t>(a));
#endif
    }
    return v;
}

struct typed_binding {
    using value = std::span<element const>;
    cbor::kind kind_of(value const &) { return cbor::kind::typed_array; }
    cbor::typed_array typed_array_of(value const &v) { return {tag, std::as_bytes(v)}; }
};

static std::string const doc = read_file(DOC_PATH);
static std::vector<element> const items = values(doc);
static std::vector<char> buf(sizeof(element) * items.size() + 16);
static std::string encoded;

static std::uint64_t op()
{
#if defined(ARM_TA_WRITE)
    typed_binding b;
    if (!cbor::encode(b, std::span<char>(buf), std::span<element const>(items)))
        std::abort();
    benchmark::ClobberMemory();
    return buf.size();
#else
    auto const l = cbor::lazy::from(std::string_view(encoded));
    if (!l)
        std::abort();
    auto const a = l->get<cbor::typed_array>();
    if (!a || (**a).tag != tag)
        std::abort();
    std::span<std::byte const> const bytes = (**a).bytes;
    std::size_t const n = bytes.size() / sizeof(element);
    std::byte const *const p = bytes.data();
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        std::uint64_t e;
        std::memcpy(&e, p + i * sizeof(element), sizeof e);
        sum += e;
    }
    return sum;
#endif
}

static void run(benchmark::State &state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(op());
}

int main(int argc, char **argv)
{
    typed_binding b;
    if (!cbor::encode(b, encoded, std::span<element const>(items)))
        std::abort();
    benchmark::AddCustomContext("document", DOC_PATH);
    benchmark::AddCustomContext("check", std::to_string(op()));
    benchmark::AddCustomContext("encoded_bytes", std::to_string(encoded.size()));
    benchmark::RegisterBenchmark(ARM_NAME, run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
