#include "../owned_copy.hpp"
#include "../small_stack.hpp"
#include <cbor/decode.hpp>
#include <cbor/diagnostic_notation.hpp>
#include <cbor/encode.hpp>
#include <cbor/error.hpp>
#include <cbor/item_size.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>
#include <cbor/schema.hpp>
#include <cbor/structs.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace std::string_literals;
using cbor::error;

namespace limits_stack
{
namespace
{

struct node {
    std::vector<node> c;
};

struct twig {
    std::uint8_t n;
};

struct leaf : twig {
};

struct holder;

struct limb : twig {
    std::vector<holder> next;
};

// A type that holds itself through a variant: cbor::schema writes and reads it by recursion.
struct holder {
    std::variant<leaf, limb> held;
};

} // namespace
} // namespace limits_stack

CBOR_DEFINE_DERIVED_TYPE_MEMBERS(limits_stack::leaf, limits_stack::twig, cbor::tag{1525}, n);
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(limits_stack::limb, limits_stack::twig, cbor::tag{1526}, n, next);
CBOR_DEFINE_TYPE_MEMBERS(limits_stack::holder, cbor::tag{1527}, held);

namespace limits_stack
{
namespace
{

// A chain of holders: each limb holds a list of one holder, and the last holder holds a leaf. A chain of k links is
// 2 k + 1 levels deep.
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

// The destructor of a holder recurses once for each link, and a chain deeper than the main thread holds would overflow
// there in a debug build with a sanitizer. The test takes the chain apart from its top in a loop, so that each link
// ends with no link below it.
void end_chain(holder chain)
{
    while (auto *const f = std::get_if<limb>(&chain.held)) {
        if (f->next.empty())
            break;
        holder next = std::move(f->next.front());
        chain = std::move(next);
    }
}

// Each link of a holder chain is two levels, each of at least one frame of the smallest size: a chain with half as many
// links as a stack has such frames is deeper than that stack. Its destructor recurses once for each link on the
// thread that ends it, the main thread.
constexpr std::size_t links_per_level = 2;

// A second small stack, four times as large, shows that the refusal follows the size of the stack. The deep input
// takes at least levels_beyond_small_stack frames of the smallest size, more than this stack holds too.
constexpr std::size_t larger_stack_bytes = 4 * test::small_stack_bytes;

std::string arrays_around_one(std::size_t const n)
{
    return std::string(n, '\x81') + "\x01"s;
}

// A struct that holds a std::vector of itself is a map that holds an array (RFC 8949 3.2): two levels for each step.
std::string nodes_around_empty(std::size_t const steps)
{
    std::string message;
    for (std::size_t i = 0; i < steps; ++i)
        message += "\xa1\x61" "c\x81"s;
    return message + "\xa1\x61" "c\x80"s;
}

std::expected<std::vector<cbor::lazy>, error> query(std::string const &message, std::string const &path)
{
    auto const l = test::lazy_of_copy(message);
    auto const q = cbor::jsonpath::compile<cbor::nodelist>(path);
    CHECK(l.has_value());
    CHECK(q.has_value());
    if (!l || !q)
        return std::unexpected(error::syntax_error);
    return l->at_path(*q);
}

// What a read of the given depth gives: its value, or nesting_depth_exceeded where the stack of the thread ends.
enum class outcome { value, refused };

template <class R>
void reads(R const &r, outcome const expected)
{
    if (expected == outcome::value) {
        CHECK(r.has_value());
    } else {
        CHECK_FALSE(r.has_value());
        if (!r)
            CHECK_EQ(r.error(), error::nesting_depth_exceeded);
    }
}

// The chain and every value that decode makes of it stay alive outside the reader: the destructor of a chain recurses
// through its links, and the test shrinks the stack of the reader, not of whoever ends the value.
struct kept_values {
    holder chain;
    std::optional<std::expected<cbor::ref<holder>, error>> decoded;
    std::optional<std::expected<cbor::ref<node>, error>> nodes;
};

void every_reader(std::size_t const levels, kept_values &kept, outcome const expected, outcome const chain_expected)
{
    std::string const deep = arrays_around_one(levels);
    reads(cbor::decode(std::string(deep)), expected);
    auto const l = test::lazy_of_copy(deep);
    CHECK(l.has_value());
    if (l)
        reads(l->clone(), expected);
    reads(cbor::diagnostic_notation(deep), expected);
    // item_size counts the items that are still to read and does not recurse: it reads every depth.
    reads(cbor::item_size(deep), outcome::value);
    reads(cbor::jsonpath::compile<cbor::lazy>("$[" + std::string(levels, '[') + "1" + std::string(levels, ']') + "]"),
          expected);
    reads(query(deep, "$..*"), expected);
    reads(query(deep, "$..[?@ == 1]"), expected);
    reads(query("\x82"s + arrays_around_one(levels - 1) + arrays_around_one(levels - 1), "$[?@ == $[1]]"), expected);
    kept.nodes = cbor::structs<node>::decode(nodes_around_empty((levels - 1) / links_per_level));
    reads(*kept.nodes, expected);
    if (*kept.nodes)
        reads(cbor::structs<node>::encode(***kept.nodes), expected);
    auto const written = cbor::schema<holder>::encode(kept.chain);
    reads(written, chain_expected);
    if (written) {
        kept.decoded = cbor::schema<holder>::decode(std::string(*written));
        reads(*kept.decoded, chain_expected);
    }
}

} // namespace
} // namespace limits_stack

using namespace limits_stack;

// Every reader that walks nested items reads a moderate depth on the main thread and gives its value.
TEST_CASE("stack: every recursive reader reads a depth that every stack holds")
{
    kept_values kept{holder_chain((test::levels_every_stack_holds - 1) / links_per_level), std::nullopt, std::nullopt};
    every_reader(test::levels_every_stack_holds, kept, outcome::value, outcome::value);
}

// An input deeper than a small stack holds: every recursive reader stops with nesting_depth_exceeded where the stack
// of its thread ends, and does not run past the stack. On WebAssembly without threads the main thread runs it, and
// the stack of the program is small there.
TEST_CASE("stack: every recursive reader on a small stack refuses an input deeper than the stack")
{
    for (std::size_t const bytes : {test::small_stack_bytes, larger_stack_bytes}) {
        CAPTURE(bytes);
        // Without threads the main thread is the small stack. A chain deeper than that stack would overflow in its own
        // destructor, because a user type ends by its own recursion; there the chain stays shallow and gives values.
        std::size_t const links = test::threads_available ? bytes / test::smallest_frame_bytes / links_per_level
                                                          : (test::levels_every_stack_holds - 1) / links_per_level;
        constexpr outcome chain_expected = test::threads_available ? outcome::refused : outcome::value;
        kept_values kept{holder_chain(links), std::nullopt, std::nullopt};
        test::on_stack_of(bytes, [&kept] {
            every_reader(test::levels_beyond_small_stack, kept, outcome::refused, chain_expected);
        });
        end_chain(std::move(kept.chain));
    }
}

// A value that decode made on the main thread is ended and encoded on a thread with a small stack. The trees of the
// library are kept in zones and end without a recursion; encode recurses and stops with nesting_depth_exceeded where
// the small stack ends. Where the main thread does not hold the decode, it stops with nesting_depth_exceeded and
// there is no tree to end.
constexpr std::size_t tree_levels = test::levels_main_thread_holds;

TEST_CASE("stack: a deep tree of the library ends and encodes on a small stack")
{
    auto decoded = cbor::decode(arrays_around_one(tree_levels));
    if (!decoded) {
        CHECK_EQ(decoded.error(), error::nesting_depth_exceeded);
        return;
    }
    std::optional<cbor::owning<cbor::item>> tree(std::move(*decoded));
    auto const l = test::lazy_of_copy(arrays_around_one(tree_levels));
    REQUIRE(l.has_value());
    auto cloned = l->clone();
    REQUIRE(cloned.has_value());
    std::optional<cbor::owning<cbor::item>> clone(std::move(*cloned));
    test::on_small_stack([&tree, &clone] {
        std::string out;
        auto const written = cbor::encode(**tree, out);
        if (!written)
            CHECK_EQ(written.error(), error::nesting_depth_exceeded);
        tree.reset();
        clone.reset();
    });
    CHECK_FALSE(tree.has_value());
    CHECK_FALSE(clone.has_value());
}
