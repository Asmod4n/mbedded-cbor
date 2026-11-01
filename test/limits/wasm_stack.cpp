#include "../owned_copy.hpp"
#include <cbor/decode.hpp>
#include <cbor/diagnostic_notation.hpp>
#include <cbor/error.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>
#include <cbor/structs.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <expected>
#include <string>
#include <utility>
#include <vector>

using namespace std::string_literals;
using cbor::error;

namespace limits_wasm_stack
{
namespace
{

struct node {
    std::vector<node> c;
};

// The build sets the nesting depth to its hard bound, 512. A message of 511 nested arrays around 1 has its innermost
// item at depth 511, inside the bound (RFC 8949 3: each array is one level).
constexpr std::size_t levels = 511;

std::string arrays_around_one(std::size_t const n)
{
    return std::string(n, '\x81') + "\x01"s;
}

// The result of one reader: on a stack that holds the bound every reader gives its value; on a smaller stack a
// reader may stop with nesting_depth_exceeded where the stack ends, and gives no other error.
template <class R>
void reads(R const &r)
{
#if CBOR_TEST_STACK_HOLDS_BOUND
    CHECK(r.has_value());
#else
    if (!r)
        CHECK_EQ(r.error(), error::nesting_depth_exceeded);
#endif
}

std::expected<std::vector<cbor::lazy>, error> query(std::string const &message, std::string const &path)
{
    auto const l = test::lazy_of_copy(message);
    REQUIRE(l.has_value());
    auto const q = cbor::jsonpath::compile<cbor::nodelist>(path);
    REQUIRE(q.has_value());
    return l->at_path(*q);
}

} // namespace
} // namespace limits_wasm_stack

using namespace limits_wasm_stack;

// WebAssembly gives a program a stack of a fixed size, 64 KiB unless the link sets another. Every reader that walks
// nested items stops where the stack ends, with the error of the nesting depth, and does not run past the stack.
// The 64 KiB program checks that no reader traps; the 16 MiB program checks that every reader reads the bound.
TEST_CASE("wasm stack: every recursive reader at the nesting bound")
{
    std::string const deep = arrays_around_one(levels);
    reads(cbor::decode(std::string(deep)));
    auto const l = test::lazy_of_copy(deep);
    REQUIRE(l.has_value());
    reads(l->clone());
    reads(cbor::diagnostic_notation(deep));
    reads(cbor::jsonpath::compile<cbor::lazy>("$[" + std::string(levels, '[') + "1" + std::string(levels, ']') + "]"));
    reads(query(deep, "$..*"));
    reads(query(deep, "$..[?@ == 1]"));
    reads(query("\x82"s + arrays_around_one(levels - 1) + arrays_around_one(levels - 1), "$[?@ == $[1]]"));
}

// A struct that holds a std::vector of itself is a map that holds an array (RFC 8949 3.2): two levels for each step.
// 255 steps put the innermost empty array at depth 511.
TEST_CASE("wasm stack: structs at the nesting bound")
{
    constexpr std::size_t steps = (levels - 1) / 2;
    std::string message;
    for (std::size_t i = 0; i < steps; ++i)
        message += "\xa1\x61" "c\x81"s;
    message += "\xa1\x61" "c\x80"s;
    auto const decoded = cbor::structs<node>::decode(std::move(message));
    reads(decoded);
    if (decoded)
        reads(cbor::structs<node>::encode(**decoded));
}
