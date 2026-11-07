#include "reads_bound.hpp"
#include "reference.hpp"
#include <cbor/describe.hpp>
#include <cbor/schema.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using namespace std::string_literals;
using cbor::error;


// This file is built into the test binary with the default nesting depth and into the one with the nesting
// depth at its hard bound, so the stack of the thread holds the deepest chain that the library accepts in
// both.

namespace
{

struct node {
    std::vector<node> children;
};

} // namespace

template <>
struct cbor::describe_members<node> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1515};
    static constexpr std::tuple members{
        CBOR_MEMBER(node, children),
    };
};

namespace
{

struct twig {
    std::uint8_t n;
};

struct leaf : twig {
};

struct holder;

struct limb : twig {
    std::vector<holder> next;
};

struct holder {
    std::variant<leaf, limb> held;
};

} // namespace

CBOR_DEFINE_DERIVED_TYPE_MEMBERS(leaf, twig, cbor::tag{1525}, n);
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(limb, twig, cbor::tag{1526}, n, next);
CBOR_DEFINE_TYPE_MEMBERS(holder, cbor::tag{1527}, held);

namespace
{

// A chain of holders: each limb holds a list of one holder, and the last holder holds a leaf.
holder holder_chain(std::size_t const links)
{
    holder h{leaf{}};
    for (std::size_t i = 0; i < links; ++i) {
        limb f{};
        f.next.push_back(std::move(h));
        h = holder{std::move(f)};
    }
    return h;
}

void u32_append(std::string &out, std::uint32_t const value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out += static_cast<char>(value >> shift);
}

// draft-ietf-cbor-packed-19 2.2: item j sits at table index 16 + j. An even j is 6(j / 2), an odd j is
// 6(-(j + 1) / 2), so its argument is (j - 1) / 2 under major type 1.
void reference_append(std::string &out, std::uint32_t const item)
{
    out += '\xc6';
    out += item % 2 == 0 ? '\x1a' : '\x3a';
    u32_append(out, item / 2);
}

// A chain of nodes, each with one child, as an attacker writes it by hand. Item k is the list of children of
// the node at depth k: one node that refers to item k + 1, and the last item is empty. Each record carries
// the class tag 1515 = d9 05 eb of node. The encoder of this library cannot write it, because gcc does not
// inline a recursive encoder.
std::string node_chain(std::size_t const levels)
{
    auto const items = static_cast<std::uint32_t>(levels + 1);
    std::string out = "\xd8\x71\x82\x9a"s;
    u32_append(out, 16 + items);
    out += "\xd8\x72\x81\x68"
           "children"
           "\x5a"s;
    u32_append(out, 4 * items);
    std::size_t const directory = out.size();
    out.append(4 * items, '\0');
    out.append(14, '\xf7');
    for (std::uint32_t k = 0; k < items; ++k) {
        std::string offset;
        u32_append(offset, static_cast<std::uint32_t>(out.size()));
        out.replace(directory + 4 * k, 4, offset);
        bool const last = k + 1 == items;
        out += "\x9a"s;
        u32_append(out, last ? 0 : 1);
        if (!last) {
            out += "\xd9\x05\xeb\xd8\x80\x9a\x00\x00\x00\x01"s;
            reference_append(out, k + 1);
        }
    }
    out += "\xd9\x05\xeb\xd8\x80\x9a\x00\x00\x00\x01"s;
    reference_append(out, 0);
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
    REQUIRE_EQ(cbor::schema<node>::fixed_size(), 16u);

    constexpr std::size_t depth = cbor::limits::nesting_depth;
    auto const at_limit = cbor::schema<node>::decode(node_chain(depth));
    CHECK(test::reads_bound(at_limit));
    if (at_limit)
        CHECK_EQ(depth_of(**at_limit), depth);

    auto const over = cbor::schema<node>::decode(node_chain(depth + 1));
    REQUIRE_FALSE(over.has_value());
    CHECK_EQ(over.error(), error::nesting_depth_exceeded);

    auto const deep = cbor::schema<node>::decode(node_chain(100000));
    REQUIRE_FALSE(deep.has_value());
    CHECK_EQ(deep.error(), error::nesting_depth_exceeded);
}

// The record of a variant member is a part that a reference names, as a list is, so it counts one level. A chain
// of k links is 2 k + 1 levels deep: the variant of the root, then a list and a variant for each link. The test
// exists so that a type that holds itself through a variant stops at the nesting depth of the build in encode
// and in decode alike: encode counts the levels as decode does, so it writes no message that decode refuses, and
// a deeper chain does not run past the stack of the writer.
TEST_CASE("encode and decode: a variant record counts one level of nesting")
{
    constexpr std::size_t depth = cbor::limits::nesting_depth;
    constexpr std::size_t links = (depth - 1) / 2;
    auto const at_limit = cbor::schema<holder>::encode(holder_chain(links));
    CHECK(test::reads_bound(at_limit));
    if (at_limit)
        CHECK(test::reads_bound(cbor::schema<holder>::decode(std::string(*at_limit))));
    auto const over = cbor::schema<holder>::encode(holder_chain(links + 1));
    REQUIRE_FALSE(over.has_value());
    CHECK_EQ(over.error(), error::nesting_depth_exceeded);
}
