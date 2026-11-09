// Gives cbor::detail::string_saver, which copies strings and byte strings
// into chunks on a memory resource and gives views of the copies. A chunk
// is reserved once and only appended to, so a view stays valid while the
// string_saver lives. The name follows llvm::StringSaver, which does the
// same work for text.
// https://llvm.org/doxygen/classllvm_1_1StringSaver.html
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

class string_saver {
    std::pmr::forward_list<std::pmr::string> chars;
    std::pmr::forward_list<std::pmr::vector<std::byte>> bytes;

    template <auto Chunks>
    CBOR_NEVER_INLINE void chunk_added(std::size_t const size)
    {
        (this->*Chunks).emplace_front().reserve(size);
    }

    template <auto Chunks, class E>
    CBOR_ALWAYS_INLINE std::span<E const> appended(std::span<E const> const part, std::size_t const chunk_size)
    {
        auto &chunks = this->*Chunks;
        if (chunks.empty() ||
            !detail::validity::bytes_fit(part.size(), chunks.front().capacity() - chunks.front().size()))
            [[unlikely]]
            chunk_added<Chunks>(std::max(part.size(), chunk_size));
        auto &chunk = chunks.front();
        std::size_t const at = chunk.size();
        chunk.append_range(part);
        return std::span<E const>(chunk).subspan(at);
    }

public:
    explicit string_saver(std::pmr::memory_resource &memory) : chars(&memory), bytes(&memory)
    {
    }

    CBOR_ALWAYS_INLINE std::string_view save(std::string_view const text, std::size_t const chunk_size)
    {
        return std::string_view(appended<&string_saver::chars>(std::span(text), chunk_size));
    }

    std::span<std::byte const> save(std::span<std::byte const> const from, std::size_t const chunk_size)
    {
        return appended<&string_saver::bytes>(from, chunk_size);
    }
};

}
