#include "reference.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace std::string_literals;
using cbor::error;

#ifdef __cpp_impl_reflection

// This file is built into the test binary with the default nesting depth and into the one with the nesting
// depth at its hard bound, so the stack of the thread holds the deepest chain that the library accepts in
// both.

namespace
{

struct[[= cbor::tag(1515)]] node {
    std::vector<node> children;
};

void u32_append(std::string &out, std::uint32_t const value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out += static_cast<char>(value >> shift);
}

// draft-ietf-cbor-packed-19 2.2: 6(N) names shared index 16 + 2N, and 6(N) with a negative N names 16 - 2N
// - 1. So shared index 16 + j is 6(j / 2) for an even j and 6(-(j + 1) / 2) for an odd j, whose argument is
// (j - 1) / 2 under major type 1.
void reference_append(std::string &out, std::uint32_t const item)
{
    out += '\xc6';
    out += item % 2 == 0 ? '\x1a' : '\x3a';
    u32_append(out, item / 2);
}

// A chain of nodes, each with one child, as an attacker writes it by hand. Each node is a record with the
// class tag 1515 = d9 05 eb whose one field names the list of its children. List k holds one reference to the
// record of node k + 1, and that record names list k + 1; the last list is empty. Tag 1113 holds the
// directory at shared index 0, f7 at 1 to 16, list k at 17 + 2k and the record of node k + 1 at 18 + 2k, then
// the record function of node as the one argument, then the root, node 0. The directory holds the root start,
// the start of each item and the end of the items. The encoder of this library cannot write it, because gcc
// does not inline a recursive encoder.
std::string node_chain(std::size_t const levels)
{
    auto const items = static_cast<std::uint32_t>(2 * levels + 1);
    std::string out = "\xd9\x04\x59\x83\x9a"s;
    u32_append(out, 17 + items);
    out += "\x5a"s;
    u32_append(out, 4 * (items + 2));
    std::size_t const directory = out.size();
    out.append(4 * (items + 2), '\0');
    out.append(16, '\xf7');
    auto const entry_write = [&out, directory](std::uint32_t const j) {
        std::string offset;
        u32_append(offset, static_cast<std::uint32_t>(out.size()));
        out.replace(directory + 4 * j, 4, offset);
    };
    for (std::uint32_t k = 0; k <= levels; ++k) {
        entry_write(2 * k + 1);
        bool const last = k == levels;
        out += "\x9a"s;
        u32_append(out, last ? 0 : 1);
        if (last)
            break;
        reference_append(out, 2 + 2 * k);
        entry_write(2 * k + 2);
        out += "\xd9\x05\xeb\xd8\x80\x9a\x00\x00\x00\x01"s;
        reference_append(out, 3 + 2 * k);
    }
    entry_write(items + 1);
    out += "\x81\xd8\x72\x81\x68"
           "children"s;
    entry_write(0);
    out += "\xd9\x05\xeb\xd8\x80\x9a\x00\x00\x00\x01"s;
    reference_append(out, 1);
    return out;
}

std::size_t depth_of(node const &root)
{
    std::size_t depth = 0;
    for (node const *at = &root; !at->children.empty(); at = &at->children.front())
        ++depth;
    return depth;
}

} // namespace

// A struct that holds a list of itself is read by recursion, and the bytes decide how deep. Without a limit,
// a chain of 100000 nodes in 2.2 MB overflowed a stack of 8 MiB. Each reference that is followed counts one
// level, as each nested item counts one in the other decoders.
TEST_CASE("decode: a struct that holds itself stops at the nesting depth of the build")
{
    REQUIRE_EQ(cbor::packed<node>::fixed_size(), 16u);

    constexpr std::size_t depth = cbor::limits::nesting_depth;
    auto const at_limit = cbor::packed<node>::decode(node_chain(depth));
    REQUIRE(at_limit.has_value());
    CHECK_EQ(depth_of(**at_limit), depth);

    auto const over = cbor::packed<node>::decode(node_chain(depth + 1));
    REQUIRE_FALSE(over.has_value());
    CHECK_EQ(over.error(), error::nesting_depth_exceeded);

    auto const deep = cbor::packed<node>::decode(node_chain(100000));
    REQUIRE_FALSE(deep.has_value());
    CHECK_EQ(deep.error(), error::nesting_depth_exceeded);
}

#endif
