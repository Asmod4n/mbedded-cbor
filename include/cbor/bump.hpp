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
#include <string>
#include <string_view>
#include <vector>

#include "validity.hpp"

namespace cbor::detail
{

class bump {
    std::pmr::forward_list<std::pmr::string> chars;
    std::pmr::forward_list<std::pmr::vector<std::byte>> bytes;

    template <class Chunk>
    CBOR_NEVER_INLINE static void chunk_added(std::pmr::forward_list<Chunk> &chunks, std::size_t const size)
    {
        chunks.emplace_front().reserve(size);
    }

    template <class Chunk, class E>
    CBOR_ALWAYS_INLINE static std::span<E const> appended(std::pmr::forward_list<Chunk> &chunks,
                                                          std::span<E const> const part, std::size_t const chunk_size)
    {
        if (chunks.empty() || !validity::bytes_fit(part.size(), chunks.front().capacity() - chunks.front().size())) [[unlikely]]
            chunk_added(chunks, std::max(part.size(), chunk_size));
        Chunk &chunk = chunks.front();
        std::size_t const at = chunk.size();
        chunk.append_range(part);
        return std::span<E const>(chunk).subspan(at);
    }

public:
    explicit bump(std::pmr::memory_resource &memory) : chars(&memory), bytes(&memory)
    {
    }

    CBOR_ALWAYS_INLINE std::string_view alloc_str(std::string_view const text, std::size_t const chunk_size)
    {
        return std::string_view(appended(chars, std::span(text), chunk_size));
    }

    std::span<std::byte const> alloc_slice_copy(std::span<std::byte const> const from, std::size_t const chunk_size)
    {
        return appended(bytes, from, chunk_size);
    }
};

}
