// The bench of the copy limit of cbor::encode_iov(owning<item> const &) (encode.hpp). One binary per arm, set by
// CBOR_ENCODER_COPYBREAK: 0 gives every string as a piece that points into the tree, the largest std::size_t copies
// every string into the blocks. Each iteration writes one tree of a list with encode_iov and sends its pieces with
// writev into a socket pair that a second thread reads empty. The list holds more than COLD_BYTES bytes of strings of
// PAYLOAD_BYTES bytes each, so each tree is cold when its turn comes.
#include <benchmark/benchmark.h>
#include <cbor/decode.hpp>
#include <cbor/encode.hpp>

#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::size_t read_size(char const *const name)
{
    char const *const given = std::getenv(name);
    if (given == nullptr)
        throw std::logic_error(std::string("bench: ") + name + " is not set");
    return std::stoull(given);
}

std::string text_message(std::size_t const payload)
{
    std::string out;
    cbor::encoder e{out};
    if (!e.text_string(std::string(payload, 't')) || !e.finish())
        throw std::logic_error("bench: cbor::encoder refuses the text");
    return out;
}

std::size_t sum_lengths(std::span<iovec const> const pieces)
{
    return std::ranges::fold_left(pieces | std::views::transform(&iovec::iov_len), std::size_t{0}, std::plus<>{});
}

void send_pieces(int const fd, std::span<iovec const> const pieces, std::size_t const iov_max)
{
    for (std::size_t at = 0; at < pieces.size(); at += iov_max) {
        std::span<iovec const> const part = pieces.subspan(at).first(std::min(iov_max, pieces.size() - at));
        ssize_t const sent = writev(fd, part.data(), static_cast<int>(part.size()));
        if (sent < 0 || static_cast<std::size_t>(sent) != sum_lengths(part))
            throw std::runtime_error("bench: writev wrote less than the pieces");
    }
}

void encode_iov_and_writev(benchmark::State &state)
{
    std::size_t const payload = read_size("PAYLOAD_BYTES");
    std::size_t const cold = read_size("COLD_BYTES");
    std::size_t const count = cold / payload + 1;
    std::string const message = text_message(payload);
    std::vector<cbor::owning<cbor::item>> trees;
    for (std::size_t i = 0; i < count; ++i) {
        auto tree = cbor::decode(message);
        if (!tree)
            throw std::logic_error("bench: cbor::decode refuses the text");
        trees.push_back(std::move(*tree));
    }
    std::array<int, 2> ends{};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends.data()) != 0)
        throw std::runtime_error("bench: socketpair fails");
    std::thread reader([fd = ends[1]] {
        std::vector<char> sink(std::size_t{1} << 20);
        while (read(fd, sink.data(), sink.size()) > 0) {
        }
    });
    std::size_t const iov_max = static_cast<std::size_t>(sysconf(_SC_IOV_MAX));
    std::size_t next = 0;
    for (auto _ : state) {
        auto const pieces = cbor::encode_iov(trees[next]);
        if (!pieces)
            throw std::logic_error("bench: cbor::encode_iov fails");
        send_pieces(ends[0], **pieces, iov_max);
        next = next + 1 == count ? 0 : next + 1;
    }
    close(ends[0]);
    reader.join();
    close(ends[1]);
    state.SetBytesProcessed(state.iterations() * static_cast<benchmark::IterationCount>(message.size()));
}

} // namespace

BENCHMARK(encode_iov_and_writev)->Unit(benchmark::kMicrosecond);
BENCHMARK_MAIN();
