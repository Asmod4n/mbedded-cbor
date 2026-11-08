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
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace fuzz
{

// The reference model copies a shared value to each reference, so a value marked near the root and referred to
// deep inside grows deeper than the input was; the decode of the written bytes then refuses it by its depth limit.
inline void decode_encode_decode(std::string_view const input)
{
    auto const value = test::decoded(input);
    if (!value)
        return;
    string_writer w;
    require(test::encode(w, *value).has_value());
    auto const again = test::decoded(w.encoded);
    if (!again) {
        require(again.error() == cbor::error::nesting_depth_exceeded);
        return;
    }
    require(same_number(*value, *again));
    string_writer twice;
    require(test::encode(twice, *again).has_value() && twice.encoded == w.encoded);
    auto const end = cbor::item_size(input);
    require(end.has_value() && *end <= input.size());
}

// Every target that encode takes writes the bytes of the std::string form: a container of char or of std::byte, a
// span of char or of std::byte of the exact size, and a writer with allocate, append and done. A span one byte short
// gives no_buffer_space.
inline void encode_targets(cbor::item const &root, std::string const &out)
{
    std::vector<char> chars;
    require(cbor::encode(root, chars) == out.size() && std::ranges::equal(chars, out));
    std::vector<std::byte> bytes;
    require(cbor::encode(root, bytes) == out.size() && std::ranges::equal(std::as_bytes(std::span(bytes)), std::as_bytes(std::span(out))));
    std::vector<char> exact(out.size());
    require(cbor::encode(root, std::span<char>(exact)) == out.size() && std::ranges::equal(exact, out));
    std::vector<std::byte> exact_bytes(out.size());
    require(cbor::encode(root, std::span<std::byte>(exact_bytes)) == out.size() &&
            std::ranges::equal(std::as_bytes(std::span(exact_bytes)), std::as_bytes(std::span(out))));
    if (!out.empty()) {
        std::vector<char> short_by_one(out.size() - 1);
        auto const refused = cbor::encode(root, std::span<char>(short_by_one));
        require(!refused && refused.error() == cbor::error::no_buffer_space);
        std::vector<std::byte> short_bytes(out.size() - 1);
        auto const refused_bytes = cbor::encode(root, std::span<std::byte>(short_bytes));
        require(!refused_bytes && refused_bytes.error() == cbor::error::no_buffer_space);
    }
    string_writer w;
    require(cbor::encode(root, w) == out.size() && w.encoded == out);
}

// encode of the decoded tree writes the library's own form (RFC 8949 2: the same value, not the same bytes). The
// written bytes decode to the same value, or the reference model refuses both alike (a cycle through tag 29).
// The own form is a fixed point of decode and encode, and the iov target gives the same bytes.
inline void tree_encode(std::string_view const input)
{
    auto const top_level = test::lazy_of_copy(input);
    if (!top_level)
        return;
    auto const tree = top_level->clone();
    if (!tree)
        return;
    auto const end = cbor::item_size(input);
    require(end.has_value());
    std::string out;
    auto const written = cbor::encode(**tree, out);
    require(written.has_value() && *written == out.size());
    auto const value = test::decoded(input.substr(0, *end));
    auto const again = test::decoded(out);
    require(value.has_value() == again.has_value());
    if (value)
        require(same(*value, *again));
    else
        require(value.error() == again.error());
    auto const top_again = test::lazy_of_copy(out);
    require(top_again.has_value());
    auto const tree_again = top_again->clone();
    require(tree_again.has_value());
    std::string twice;
    require(cbor::encode(**tree_again, twice).has_value() && twice == out);
    auto const pieces = cbor::encode_iov(**tree);
    require(pieces.has_value());
    auto const joined = test::joined(**pieces);
    require(joined.has_value() && *joined == out);
    encode_targets(**tree, out);
}

// The nodes of a tree, reached through the elements, the pairs, the tag contents and the embedded item of each tag
// 24, not through a shared_reference. A shared_reference points into the tree, so this walk ends on a cycle.
inline std::vector<cbor::item const *> nodes_of(cbor::owning<cbor::item> const &tree)
{
    std::vector<cbor::item const *> nodes{&*tree};
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        cbor::item const &n = *nodes[i];
        if (auto const t = n.get<cbor::type::tag>(); t && t->first == 24)
            if (auto const embedded = cbor::encoded_cbor_data_item(tree, n))
                nodes.push_back(&embedded->get());
        if (auto const a = n.get<cbor::type::array>())
            for (cbor::item const &c : *a)
                nodes.push_back(&c);
        else if (auto const m = n.get<cbor::type::map>())
            for (cbor::item const &c : *m)
                nodes.push_back(&c);
        else if (auto const t = n.get<cbor::type::tag>())
            nodes.push_back(t->second);
        else if (auto const u = n.get<cbor::type::unsigned_bignum>())
            nodes.push_back(u->second);
        else if (auto const v = n.get<cbor::type::negative_bignum>())
            nodes.push_back(v->second);
        else if (auto const w = n.get<cbor::type::typed_array>())
            nodes.push_back(w->second);
    }
    return nodes;
}

// cbor::decode resolves tags 28 and 29 (theories/Cycles.v). A decode that succeeds reads a stream that clone
// also reads; each shared_reference names a node of the tree (theorem read_no_dangling); the tree has no more nodes
// than the tree of clone, which keeps one node per tag (read_no_expansion). encode with cbor::cycles writes a tree
// with a shared_reference unless the amplification limit, the depth or the bounded span stops it, and decode reads back
// a graph with the same unfolding (theories/Writer.v write_read_back). A tree with as many nodes as the tree of
// clone holds no tag 28, so encode writes it as it writes the tree of clone.
inline void resolving_decode(std::string_view const input)
{
    auto const resolved = cbor::decode(input);
    if (!resolved)
        return;
    auto const top_level = test::lazy_of_copy(input);
    require(top_level.has_value());
    auto const plain = top_level->clone();
    require(plain.has_value());
    std::vector<cbor::item const *> const nodes = nodes_of(*resolved);
    std::size_t const plain_nodes = nodes_of(*plain).size();
    require(nodes.size() <= plain_nodes);
    bool shared = false;
    for (cbor::item const *const n : nodes)
        if (auto const r = n->get<cbor::type::shared_reference>()) {
            shared = true;
            require(std::ranges::find(nodes, r->second) != nodes.end());
        }
    if (shared) {
        std::array<char, 1 << 16> bounded;
        auto const size = cbor::encode(**resolved, std::span(bounded), cbor::cycles);
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
    auto const written = cbor::encode(**resolved, out);
    require(written.has_value());
    if (nodes.size() != plain_nodes)
        return;
    std::string plain_out;
    require(cbor::encode(**plain, plain_out).has_value() && out == plain_out);
}

inline void decode_target(std::string_view const input)
{
    decode_encode_decode(input);
    tree_encode(input);
    resolving_decode(input);
}

} // namespace fuzz
