// The bench of the queue of pieces of cbor::sequence_reader: feed(shared_ptr) and next() (sequence_reader.hpp).
// Each iteration reads a CBOR sequence of COLD_BYTES bytes, fed as pieces that the reader holds without a copy.
// sequence_reader_whole: each piece holds whole items, and next() follows each feed.
// sequence_reader_cut: each piece is 13 bytes, so the end of a piece cuts items, and next() follows each feed.
// sequence_reader_queued: every piece is fed before the first next(), so the queue holds all pieces.
#include <benchmark/benchmark.h>
#include <cbor/sequence_reader.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::string_view_literals;

constexpr std::string_view message = "\xa2\x61\x61\x01\x61\x62\x82\x02\x03"sv;
constexpr std::size_t messages_in_whole_piece = 64;
constexpr std::size_t cut_piece_size = 13;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

std::string sequence_of(std::size_t const bytes)
{
    std::string s;
    while (s.size() < bytes)
        s.append(message);
    return s;
}

std::vector<std::shared_ptr<std::string const>> pieces_of(std::string_view const s, std::size_t const size)
{
    std::vector<std::shared_ptr<std::string const>> pieces;
    for (std::size_t at = 0; at < s.size(); at += size)
        pieces.push_back(std::make_shared<std::string const>(s.substr(at, size)));
    return pieces;
}

void run(benchmark::State &state, std::size_t const piece_size, bool const queued)
{
    std::string const s = sequence_of(cold_bytes());
    auto const pieces = pieces_of(s, piece_size);
    std::size_t const expected = s.size() / message.size();
    for (auto _ : state) {
        cbor::sequence_reader r;
        std::size_t n = 0;
        auto const drain = [&r, &n] {
            for (auto item = r.next(); item; item = r.next()) {
                benchmark::DoNotOptimize(item);
                ++n;
            }
        };
        for (auto const &p : pieces) {
            r.feed(p);
            if (!queued)
                drain();
        }
        drain();
        if (n != expected || !r.empty()) {
            state.SkipWithError("the reader does not give every item of the sequence");
            return;
        }
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * expected));
}

void sequence_reader_whole(benchmark::State &state)
{
    run(state, messages_in_whole_piece * message.size(), false);
}

void sequence_reader_cut(benchmark::State &state)
{
    run(state, cut_piece_size, false);
}

void sequence_reader_queued(benchmark::State &state)
{
    run(state, messages_in_whole_piece * message.size(), true);
}

}

BENCHMARK(sequence_reader_whole)->Unit(benchmark::kMillisecond);
BENCHMARK(sequence_reader_cut)->Unit(benchmark::kMillisecond);
BENCHMARK(sequence_reader_queued)->Unit(benchmark::kMillisecond);
BENCHMARK_MAIN();
