// Reads the pieces of an iov_writer back the way a caller sends them: writev
// into a temporary file, IOV_MAX pieces at a time, then read until the file
// has given every byte. Only the file descriptor reads and writes the file.
// https://pubs.opengroup.org/onlinepubs/9799919799/functions/writev.html
#ifndef CBOR_TEST_PIECES_HPP
#define CBOR_TEST_PIECES_HPP

// The iov forms exist where the platform has <sys/uio.h>, as in the library (encode.hpp); Windows has none.
#if __has_include(<sys/uio.h>)

#include <cbor/encode.hpp>

#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <limits>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace test {

template <class T>
std::expected<T, std::errc> count_of(std::size_t const n)
{
    if (!std::in_range<T>(n))
        return std::unexpected(std::errc::value_too_large);
    return static_cast<T>(n);
}

inline std::size_t sum_of(std::span<iovec const> const pieces)
{
    return std::ranges::fold_left(pieces | std::views::transform(&iovec::iov_len), std::size_t{0}, std::plus<>{});
}

inline std::errc errno_code()
{
    return static_cast<std::errc>(errno);
}

inline std::expected<std::string, std::errc> joined(std::span<iovec const> const pieces)
{
    long const iov_max = sysconf(_SC_IOV_MAX);
    if (iov_max <= 0)
        return std::unexpected(errno_code());
    std::unique_ptr<std::FILE, int (*)(std::FILE *)> const file(std::tmpfile(), &std::fclose);
    if (!file)
        return std::unexpected(errno_code());
    int const fd = fileno(file.get());
    std::size_t const step = static_cast<std::size_t>(iov_max);
    for (std::size_t at = 0; at < pieces.size(); at += step) {
        std::span<iovec const> const part = pieces.subspan(at).first(std::min(step, pieces.size() - at));
        auto const count = count_of<int>(part.size());
        if (!count)
            return std::unexpected(count.error());
        ssize_t const sent = writev(fd, part.data(), *count);
        if (sent < 0)
            return std::unexpected(errno_code());
        if (static_cast<std::size_t>(sent) != sum_of(part))
            return std::unexpected(std::errc::io_error);
    }
    if (lseek(fd, 0, SEEK_SET) != 0)
        return std::unexpected(errno_code());
    std::string out(sum_of(pieces), '\0');
    std::span<char> rest(out);
    while (!rest.empty()) {
        ssize_t const got = read(fd, rest.data(), rest.size());
        if (got < 0)
            return std::unexpected(errno_code());
        if (got == 0)
            return std::unexpected(std::errc::io_error);
        rest = rest.subspan(static_cast<std::size_t>(got));
    }
    return out;
}

}

#endif
#endif
