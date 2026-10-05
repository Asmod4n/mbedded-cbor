#include "binding.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

std::size_t allocations = 0;

} // namespace

// The replaced operator new counts each allocation of this test binary, so a test can show that a call allocates
// nothing.
void *operator new(std::size_t const size)
{
    ++allocations;
    if (void *const p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete(void *const p) noexcept
{
    std::free(p);
}

void operator delete(void *const p, std::size_t) noexcept
{
    std::free(p);
}

// The path that succeeds compares a key of the document with a literal by value without an allocation: the key
// [1, 29(0)] holds a shared reference inside an array, and the literal is [1, "x"] in preferred serialization.
TEST_CASE("path: a key is compared without an allocation")
{
    std::string const bytes = "\x82\xd8\x1c\x61x\xa2\x82\x01\xd8\x1d\x00\x01\xa1\x61k\xd8\x1d\x00\x02"s;
    auto const root = *cbor::lazy::from(std::string(bytes));
    auto const map = *root.at(1);
    REQUIRE(cbor::internal::key_find<16>(map, "\x82\x01\x61x"sv).has_value());
    std::size_t const before = allocations;
    auto const found = cbor::internal::key_find<16>(map, "\x82\x01\x61x"sv);
    std::size_t const after = allocations;
    REQUIRE(found.has_value());
    CHECK_EQ(after, before);
    CHECK_EQ(*found->get<std::uint64_t>(), 1u);
}
