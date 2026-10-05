#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

struct cose_header {
    std::optional<std::span<std::byte const>> kid;
    static constexpr std::array<std::int64_t, 1> keys{4};
};

using cose_sign1 = cbor::tagged<18, std::tuple<std::span<std::byte const>, cose_header, std::span<std::byte const>,
                                               std::span<std::byte const>>>;

struct cwt_claims {
    std::optional<std::string_view> iss;
    std::optional<std::string_view> sub;
    std::optional<std::string_view> aud;
    std::optional<std::uint64_t> exp;
    std::optional<std::uint64_t> nbf;
    std::optional<std::uint64_t> iat;
    std::optional<std::span<std::byte const>> cti;
    static constexpr std::array<std::int64_t, 7> keys{1, 2, 3, 4, 5, 6, 7};
};

static std::uint64_t bytes_sum(std::span<std::byte const> const s)
{
    return std::transform_reduce(s.begin(), s.end(), std::uint64_t{0}, std::plus<>{},
                                 [](std::byte const b) { return std::uint64_t(b); });
}

static std::uint64_t bytes_sum(std::string_view const s)
{
    return bytes_sum(std::as_bytes(std::span(s)));
}

static std::shared_ptr<std::string const> doc;
static std::string msg;

static std::uint64_t claims_read(cwt_claims const &c)
{
    return bytes_sum(c.iss.value_or("")) + bytes_sum(c.sub.value_or("")) + bytes_sum(c.aud.value_or("")) +
           c.exp.value_or(0) + c.nbf.value_or(0) + c.iat.value_or(0) +
           bytes_sum(c.cti.value_or(std::span<std::byte const>{}));
}

static std::uint64_t op()
{
#if defined(OP_READ)
    auto const sign1 = cbor::databind<cose_sign1>::decode(doc, *doc);
    if (!sign1) [[unlikely]]
        std::abort();
    auto const &[protected_header, unprotected, payload, signature] = (*sign1)->content;
    auto const claims = cbor::databind<cwt_claims>::decode(
        doc, std::string_view(reinterpret_cast<char const *>(payload.data()), payload.size()));
    if (!claims) [[unlikely]]
        std::abort();
    return bytes_sum(protected_header) + bytes_sum(unprotected.kid.value_or(std::span<std::byte const>{})) +
           claims_read(**claims) + bytes_sum(signature);
#elif defined(OP_ENC)
    static auto const sign1 = cbor::databind<cose_sign1>::decode(doc, *doc);
    msg.clear();
    if (!cbor::databind<cose_sign1>::encode(**sign1, msg)) [[unlikely]]
        std::abort();
    benchmark::ClobberMemory();
    return msg.size();
#endif
}

static void run(benchmark::State &state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(op());
    state.SetBytesProcessed(std::int64_t(state.iterations()) * std::int64_t(doc->size()));
}

int main(int argc, char **argv)
{
    std::ifstream in(DOC_PATH, std::ios::binary);
    doc = std::make_shared<std::string const>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (doc->empty())
        std::abort();
    msg.reserve(2 * doc->size());
    benchmark::RegisterBenchmark("cwt", run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
}
