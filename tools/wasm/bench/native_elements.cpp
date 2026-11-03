#include <cbor/cbor.hpp>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
int main()
{
    constexpr std::size_t count = 1000000;
    std::string b = {char(0x9a), char(0x00), char(0x0f), char(0x42), char(0x40)};
    for (std::size_t i = 0; i < count; ++i) {
        unsigned v = 1000 + i % 1000;
        b += char(0x19); b += char(v >> 8); b += char(v & 255);
    }
    auto const doc = std::make_shared<std::string const>(b);
    double best = 1e9;
    long long sum = 0;
    for (int k = 0; k < 7; ++k) {
        auto t0 = std::chrono::steady_clock::now();
        auto const root = cbor::lazy::view(doc);
        for (auto const e : *root->elements()) sum += *e->get<std::int64_t>();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms < best) best = ms;
    }
    std::printf("native lazy elements get<int64>: %.2f ms (sum %lld)\n", best, sum);
}
