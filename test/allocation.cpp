#include "binding.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

std::size_t allocations = 0;

std::size_t allocated_bytes = 0;

} // namespace

// The replaced operator new counts each allocation of this test binary, so a test can show that a call allocates
// nothing.
[[gnu::noinline]] void *operator new(std::size_t const size)
{
    ++allocations;
    allocated_bytes += size;
    if (void *const p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

[[gnu::noinline]] void operator delete(void *const p) noexcept
{
    std::free(p);
}

[[gnu::noinline]] void operator delete(void *const p, std::size_t) noexcept
{
    std::free(p);
}

// The path that succeeds compares a key of the top-level item with a literal by value without an allocation: the
// top-level item holds no shared value, and the literal [1, "x"] is parsed at compile time.
TEST_CASE("path: a key is compared without an allocation")
{
    std::string const bytes = "\x82\x61x\xa1\x82\x01\x61x\x01"s;
    auto const root = *cbor::lazy::from(std::string(bytes));
    auto const map = *root.at(1);
    test_binding binding;
    REQUIRE(cbor::at_path<"$[[1, \"x\"]]">(binding, map).has_value());
    std::size_t const before = allocations;
    auto const found = cbor::at_path<"$[[1, \"x\"]]">(binding, map);
    std::size_t const after = allocations;
    REQUIRE(found.has_value());
    CHECK_EQ(after, before);
    CHECK_EQ(std::get<std::uint64_t>(found->kind), 1u);
}

// A top-level item with a shared value (tag 28) needs a table of the shared values to decode a reference to one, so the
// count of allocations is fixed here: the key [1, 29(0)] refers to the shared value "x".
TEST_CASE("path: a top-level item with shared values allocates only the table of shared values")
{
    std::string const bytes = "\x82\xd8\x1c\x61x\xa2\x82\x01\xd8\x1d\x00\x01\xa1\x61k\xd8\x1d\x00\x02"s;
    auto const root = *cbor::lazy::from(std::string(bytes));
    auto const map = *root.at(1);
    test_binding binding;
    REQUIRE(cbor::at_path<"$[[1, \"x\"]]">(binding, map).has_value());
    // The top-level item marks one shared value. The decoder keeps one flag and one entry for each mark, in two arrays,
    // and an array that holds at least one element is one allocation.
    constexpr std::size_t marks_allocations = 2;
    std::size_t const before = allocations;
    auto const found = cbor::at_path<"$[[1, \"x\"]]">(binding, map);
    std::size_t const after = allocations;
    REQUIRE(found.has_value());
    CHECK_EQ(after - before, marks_allocations);
    CHECK_EQ(std::get<std::uint64_t>(found->kind), 1u);
}

// The count in the head of an array is not trusted. The item of an array is its count and a view on its bytes, so a
// head that claims many elements makes decode take no room for them. Here the array claims 4096 elements and its
// first element is not well-formed, so decode fails before it builds anything but the array.
TEST_CASE("decode: the room kept for the elements of an array is bounded by the bytes left")
{
    std::string bytes = "\x99\x10\x00\xff"s;
    bytes.resize(4099);
    auto const root = *cbor::lazy::from(std::string(bytes));
    std::size_t const before = allocated_bytes;
    auto const r = root.decode();
    std::size_t const after = allocated_bytes;
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::syntax_error);
    CHECK_LE(after - before, 2 * bytes.size());
}

// The typed read of a path walks the bytes of the caller and builds no top-level item: a text with the owner of the
// caller, an integer from a std::string_view and a value under a mark of tag 28 are read with no allocation.
TEST_CASE("path: a typed read allocates nothing")
{
    auto const doc = std::make_shared<std::string const>(
        encoded(M("statuses"s, A(M("user"s, M("screen_name"s, "ann"s)), M("user"s, M("screen_name"s, "bob"s))))));
    std::string const numbers = "\x83\x01\x39\x03\xe7\x03"s;
    std::string const marked = "\xa1\x61\x61\xd8\x1c\x82\x01\x02"s;
    std::size_t const before = allocations;
    auto const name = cbor::at_path<"$.statuses[1].user.screen_name", std::string_view>(doc, *doc);
    auto const number = cbor::at_path<"$[1]", std::int64_t>(std::string_view(numbers));
    auto const shared = cbor::at_path<"$.a[1]", std::uint64_t>(std::string_view(marked));
    std::size_t const after = allocations;
    CHECK_EQ(after, before);
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "bob"sv);
    REQUIRE(number.has_value());
    CHECK_EQ(*number, -1000);
    REQUIRE(shared.has_value());
    CHECK_EQ(*shared, 2u);
}

// A tag 29 on the walk names a mark, and only a top-level item keeps the marks. The typed read then goes through lazy
// and allocates as lazy does. This test records that it allocates; the count depends on the C++ library.
TEST_CASE("path: a typed read through tag 29 allocates")
{
    std::string const doc = "\x82\xd8\x1c\x82\x07\x08\xd8\x1d\x00"s;
    std::size_t const before = allocations;
    auto const first = cbor::at_path<"$[1][0]", std::uint64_t>(std::string_view(doc));
    std::size_t const after = allocations;
    CHECK_GT(after, before);
    REQUIRE(first.has_value());
    CHECK_EQ(*first, 7u);
}
