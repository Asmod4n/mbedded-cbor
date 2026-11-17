// Gives the fuzz target of cbor::decode.
#pragma once

#include "common.hpp"
#include "../test/owned_copy.hpp"
#include "../test/pieces.hpp"
#include "../test/unfolding.hpp"

#include <cbor/decode.hpp>
#include <cbor/encode.hpp>
#include <cbor/error.hpp>
#include <cbor/item_size.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fuzz
{

// Every target that encode takes writes the bytes of the std::string form: a container of char or of std::byte, a
// span of char or of std::byte of the exact size, and a writer with allocate, append and done. A span one byte short
// gives no_buffer_space.
inline void encode_targets(cbor::item const &root, std::string const &out)
{
    std::vector<char> chars;
    require(cbor::encode(chars, root) == out.size() && std::ranges::equal(chars, out));
    std::vector<std::byte> bytes;
    require(cbor::encode(bytes, root) == out.size() && std::ranges::equal(std::as_bytes(std::span(bytes)), std::as_bytes(std::span(out))));
    std::vector<char> exact(out.size());
    require(cbor::encode(std::span<char>(exact), root) == out.size() && std::ranges::equal(exact, out));
    std::vector<std::byte> exact_bytes(out.size());
    require(cbor::encode(std::span<std::byte>(exact_bytes), root) == out.size() &&
            std::ranges::equal(std::as_bytes(std::span(exact_bytes)), std::as_bytes(std::span(out))));
    if (!out.empty()) {
        std::vector<char> short_by_one(out.size() - 1);
        auto const refused = cbor::encode(std::span<char>(short_by_one), root);
        require(!refused && refused.error() == cbor::error::no_buffer_space);
        std::vector<std::byte> short_bytes(out.size() - 1);
        auto const refused_bytes = cbor::encode(std::span<std::byte>(short_bytes), root);
        require(!refused_bytes && refused_bytes.error() == cbor::error::no_buffer_space);
    }
    string_writer w;
    require(cbor::encode(w, root) == out.size() && w.encoded == out);
}

// encode of the decoded tree writes the library's own form (RFC 8949 2: the same value, not the same bytes). The
// written bytes decode to the same value, or the reference model refuses both alike (a cycle through tag 29).
// The own form is a fixed point of decode and encode, and the iov target gives the same bytes.
inline void tree_encode(std::string_view const input)
{
    auto const top_level = test::make_lazy_from_copy(input);
    if (!top_level)
        return;
    auto const tree = top_level->to_item();
    if (!tree)
        return;
    auto const end = cbor::item_size(input);
    require(end.has_value());
    std::string out;
    auto const written = cbor::encode(out, **tree);
    require(written.has_value() && *written == out.size());
    auto const value = test::decode_wire_into_value(input.substr(0, *end));
    auto const again = test::decode_wire_into_value(out);
    require(value.has_value() == again.has_value());
    if (value)
        require(values_are_the_same(*value, *again));
    else
        require(value.error() == again.error());
    auto const top_again = test::make_lazy_from_copy(out);
    require(top_again.has_value());
    auto const tree_again = top_again->to_item();
    require(tree_again.has_value());
    std::string twice;
    require(cbor::encode(twice, **tree_again).has_value() && twice == out);
    auto const pieces = cbor::encode_iov(*tree);
    require(pieces.has_value());
    auto const joined = test::join_pieces_into_bytes(**pieces);
    require(joined.has_value() && *joined == out);
    encode_targets(**tree, out);
}

// The nodes of a tree, reached through the elements, the pairs, the tag contents and the embedded item of each tag
// 24, not through a shared_reference and not into the byte string of a typed array. A shared_reference points into the tree, so this walk ends on a cycle.
inline std::vector<cbor::item const *> nodes_of(cbor::owning<cbor::item> const &tree)
{
    std::vector<cbor::item const *> nodes{&*tree};
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        cbor::item const &n = *nodes[i];
        if (n.type() == cbor::type::tag && n.get<std::pair<std::uint64_t, cbor::item const *>>()->first == 24)
            if (auto const embedded = cbor::encoded_cbor_data_item(tree, n))
                nodes.push_back(&embedded->get());
        n.visit([&nodes](auto const value) {
            using V = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<V, std::span<cbor::item const>>) {
                for (cbor::item const &c : value)
                    nodes.push_back(&c);
            } else if constexpr (std::same_as<V, cbor::pairs>) {
                for (auto const pair : value) {
                    nodes.push_back(&std::get<0>(pair));
                    nodes.push_back(&std::get<1>(pair));
                }
            } else if constexpr (std::same_as<V, std::pair<std::uint64_t, cbor::item const *>>) {
                nodes.push_back(value.second);
            }
        });
    }
    return nodes;
}

// cbor::decode resolves tags 28 and 29 (theories/Cycles.v). A decode that succeeds reads a stream that to_item
// also reads; each shared_reference names a node of the tree (theorem read_no_dangling); the tree has no more nodes
// than the tree of to_item, which keeps one node per tag (read_no_expansion). encode writes a tree
// with a shared_reference unless the amplification limit, the depth or the bounded span stops it, and decode reads back
// a graph with the same unfolding (theories/Writer.v write_read_back). A tree with as many nodes as the tree of
// clone holds no tag 28, so encode writes it as it writes the tree of clone.
inline void resolving_decode(std::string_view const input)
{
    auto const resolved = cbor::decode(input);
    if (!resolved)
        return;
    auto const top_level = test::make_lazy_from_copy(input);
    require(top_level.has_value());
    auto const plain = top_level->to_item();
    require(plain.has_value());
    std::vector<cbor::item const *> const nodes = nodes_of(*resolved);
    std::size_t const plain_nodes = nodes_of(*plain).size();
    require(nodes.size() <= plain_nodes);
    bool shared = false;
    for (cbor::item const *const n : nodes)
        if (n->type() == cbor::type::shared_reference) {
            shared = true;
            require(std::ranges::find(nodes, n->get<std::pair<std::uint64_t, cbor::item const *>>()->second) != nodes.end());
        }
    if (shared) {
        std::array<char, 1 << 16> bounded;
        auto const size = cbor::encode(std::span(bounded), **resolved);
        if (!size) {
            require(size.error() == cbor::error::amplification_limit_breach ||
                    size.error() == cbor::error::nesting_depth_exceeded ||
                    size.error() == cbor::error::no_buffer_space);
            return;
        }
        auto const back = cbor::decode(std::string_view(bounded.data(), *size));
        require(back.has_value() && test::unfoldings_equal(**resolved, **back));
        return;
    }
    std::string out;
    auto const written = cbor::encode(out, **resolved);
    require(written.has_value());
    if (nodes.size() != plain_nodes)
        return;
    std::string plain_out;
    require(cbor::encode(plain_out, **plain).has_value() && out == plain_out);
}

inline void decode_target(std::string_view const input)
{
    tree_encode(input);
    resolving_decode(input);
}

} // namespace fuzz
