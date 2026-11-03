// Gives cbor::detail::bump, an arena that copies strings and byte
// strings into chunks on a memory resource and gives views of the
// copies. A chunk is reserved once and only appended to, so a view
// stays valid while the bump lives. The names follow the Rust crate
// bumpalo.
// https://docs.rs/bumpalo/latest/bumpalo/struct.Bump.html
#pragma once

#include <algorithm>
#include <cstddef>
#include <forward_list>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

namespace cbor::detail
{

class bump {
    std::pmr::forward_list<std::pmr::vector<char>> chars;
    std::pmr::forward_list<std::pmr::vector<std::byte>> bytes;

    template <class E>
    static std::span<E const> appended(std::pmr::forward_list<std::pmr::vector<E>> &chunks, std::span<E const> const part,
                                       std::size_t const chunk_size)
    {
        if (chunks.empty() || part.size() > chunks.front().capacity() - chunks.front().size()) [[unlikely]]
            chunks.emplace_front().reserve(std::max(part.size(), chunk_size));
        std::pmr::vector<E> &chunk = chunks.front();
        std::size_t const at = chunk.size();
        chunk.append_range(part);
        return std::span<E const>(chunk).subspan(at);
    }

public:
    explicit bump(std::pmr::memory_resource &memory) : chars(&memory), bytes(&memory)
    {
    }

    std::string_view alloc_str(std::string_view const text, std::size_t const chunk_size)
    {
        return std::string_view(appended(chars, std::span(text), chunk_size));
    }

    std::span<std::byte const> alloc_slice_copy(std::span<std::byte const> const from, std::size_t const chunk_size)
    {
        return appended(bytes, from, chunk_size);
    }
};

}
