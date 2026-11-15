// The bench of the copy limit of cbor::encode_iov(owning<item> const &) (encode.hpp). One binary per arm, set by
// CBOR_ENCODER_COPYBREAK: 0 gives every string as a piece that points into the tree, the largest std::size_t copies
// every string into the blocks. Each iteration writes one tree of a list with encode_iov and sends its pieces with
// writev into a socket pair that a second thread reads empty. The list holds more than COLD_BYTES bytes of strings of
// one payload size, so each tree is cold when its turn comes. The payload sizes run from the page size to twice the
// sum of the cache sizes of the machine, in steps of a factor of two; a cache that several cpus share counts once.
#include <benchmark/benchmark.h>
#include <cbor/decode.hpp>
#include <cbor/encode.hpp>

#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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

std::size_t read_cache_bytes()
{
    std::set<std::string> counted;
    std::size_t sum = 0;
    for (unsigned cpu = 0;; ++cpu) {
        std::string const base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cache/index";
        if (!std::ifstream(base + "0/size"))
            break;
        for (unsigned index = 0;; ++index) {
            std::ifstream size_file(base + std::to_string(index) + "/size");
            std::ifstream shared_file(base + std::to_string(index) + "/shared_cpu_list");
            std::ifstream level_file(base + std::to_string(index) + "/level");
            std::ifstream type_file(base + std::to_string(index) + "/type");
            if (!size_file)
                break;
            std::string size, shared, level, type;
            size_file >> size;
            shared_file >> shared;
            level_file >> level;
            type_file >> type;
            if (!counted.insert(level + type + shared).second)
                continue;
            constexpr std::size_t kibibyte = 1024;
            sum += std::stoull(size) * kibibyte;
        }
    }
    return sum;
}

std::vector<cbor::owning<cbor::item>> decode_cold_trees(std::size_t const payload, std::size_t const cold)
{
    std::size_t const count = cold / payload + 1;
    std::string const message = text_message(payload);
    std::vector<cbor::owning<cbor::item>> trees;
    for (std::size_t i = 0; i < count; ++i) {
        auto tree = cbor::decode(message);
        if (!tree)
            throw std::logic_error("bench: cbor::decode refuses the text");
        trees.push_back(std::move(*tree));
    }
    return trees;
}

void encode_iov_and_writev(benchmark::State &state, std::vector<cbor::owning<cbor::item>> const &trees)
{
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
    std::size_t sent = 0;
    for (auto _ : state) {
        auto const pieces = cbor::encode_iov(trees[next]);
        if (!pieces)
            throw std::logic_error("bench: cbor::encode_iov fails");
        send_pieces(ends[0], **pieces, iov_max);
        sent += sum_lengths(**pieces);
        next = next + 1 == trees.size() ? 0 : next + 1;
    }
    close(ends[0]);
    reader.join();
    close(ends[1]);
    state.SetBytesProcessed(static_cast<std::int64_t>(sent));
}

std::vector<std::size_t> payload_sizes()
{
    if (char const *const given = std::getenv("SIZES")) {
        std::vector<std::size_t> sizes;
        std::string const text(given);
        for (auto const part : std::views::split(text, ' '))
            if (!std::ranges::empty(part))
                sizes.push_back(std::stoull(std::string(std::string_view(part))));
        return sizes;
    }
    std::vector<std::size_t> sizes;
    std::size_t const top = 2 * read_cache_bytes();
    for (std::size_t size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE)); size <= top; size *= 2)
        sizes.push_back(size);
    return sizes;
}

} // namespace

int main(int argc, char **argv)
{
    benchmark::Initialize(&argc, argv);
    benchmark::JSONReporter reporter;
    std::size_t const cold = read_size("COLD_BYTES");
    for (std::size_t const size : payload_sizes()) {
        auto const trees = decode_cold_trees(size, cold);
        benchmark::RegisterBenchmark("encode_iov_and_writev/" + std::to_string(size),
                                     [&trees](benchmark::State &state) { encode_iov_and_writev(state, trees); })
            ->Unit(benchmark::kMicrosecond);
        benchmark::RunSpecifiedBenchmarks(&reporter);
        benchmark::ClearRegisteredBenchmarks();
    }
    benchmark::Shutdown();
}
