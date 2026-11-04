#include <benchmark/benchmark.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

static void send_piece(benchmark::State &state)
{
    std::size_t const n = static_cast<std::size_t>(state.range(0));
    std::string const owner(n, 'x');
    int const fd = memfd_create("piece", 0);
    if (fd < 0)
        std::abort();
    for (auto _ : state) {
#if defined(ARM_COPY)
        std::vector<char> copied(owner.begin(), owner.end());
        iovec piece{};
        piece.iov_base = copied.data();
        piece.iov_len = copied.size();
#else
        iovec piece{};
        piece.iov_base = const_cast<char *>(owner.data());
        piece.iov_len = owner.size();
#endif
        if (writev(fd, &piece, 1) != static_cast<ssize_t>(n))
            std::abort();
        if (lseek(fd, 0, SEEK_SET) != 0)
            std::abort();
        benchmark::ClobberMemory();
    }
    close(fd);
}
BENCHMARK(send_piece)->Arg(32 << 10)->Arg(1 << 20)->Arg(33 << 20)->Arg(3 * ((32 << 10) + (1 << 20) + (33 << 20)));
BENCHMARK_MAIN();
