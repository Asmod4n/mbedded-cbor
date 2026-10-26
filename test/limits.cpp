#include "reference.hpp"
#include "owned_copy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// A build that sets no limit has the nesting depth 128 and no size limit. The builds with limits set are in
// test/limits/, one binary for each set of flags, so each limit is tested by a build that the limit holds.
TEST_CASE("limits: the defaults")
{
    CHECK_EQ(cbor::limits::nesting_depth, 128u);
    CHECK_EQ(cbor::limits::string_length, std::numeric_limits<std::size_t>::max());
    CHECK_EQ(cbor::limits::container_elements, std::numeric_limits<std::size_t>::max());
}

// With no size limit, no check of a size exists. A head that claims 2^64 - 1 bytes or elements over no
// content is read and finds too little data, where test/limits/sizes_2.cpp refuses the same heads at the
// head.
TEST_CASE("limits: with no size limit a head of 2^64 - 1 finds too little data")
{
    std::string const text = "\x7b\xff\xff\xff\xff\xff\xff\xff\xff"s;
    std::string const array = "\x9b\xff\xff\xff\xff\xff\xff\xff\xff"s;
    CHECK_EQ(cbor::item_size(text).error(), error::too_little_data);
    CHECK_EQ(cbor::item_size(array).error(), error::too_little_data);
    CHECK_EQ(cbor::diagnostic_notation(text).error(), error::too_little_data);
    CHECK_EQ(cbor::diagnostic_notation(array).error(), error::too_little_data);
}

#ifdef __cpp_impl_reflection

namespace
{

struct[[= cbor::tag(1592)]] pmr_names {
    std::pmr::string text;
    std::pmr::vector<std::pmr::string> list;
};

// Each string is longer than any short string buffer, so each one allocates from the resource.
std::string const long_text(40, 'x');

std::string long_texts_encoded(std::size_t const n)
{
    std::string encoded(1, static_cast<char>(0x80 + n));
    for (std::size_t i = 0; i < n; ++i)
        encoded += "\x78\x28"s + long_text;
    return encoded;
}

} // namespace

// The memory of a decode is bounded by the caller with the standard: the target holds a std::pmr allocator
// over a resource with a budget, every element is made with the allocator of its container, and the refusal
// of the resource reaches the caller of structs as not_enough_memory. A monotonic_buffer_resource over a
// fixed buffer with null_memory_resource upstream refuses every byte beyond the buffer.
TEST_CASE("structs: a std::pmr target decodes with its allocator, and the refusal of its resource is "
          "not_enough_memory")
{
    using texts = std::pmr::vector<std::pmr::string>;
    std::string const encoded = long_texts_encoded(3);
    {
        std::array<std::byte, 4096> buffer{};
        std::pmr::monotonic_buffer_resource budget(buffer.data(), buffer.size(),
                                                   std::pmr::null_memory_resource());
        auto const decoded = cbor::structs<texts>::decode(std::make_shared<std::string const>(encoded),
                                                           texts(&budget));
        REQUIRE(decoded.has_value());
        CHECK_EQ((**decoded).size(), 3u);
        CHECK_EQ((**decoded).get_allocator().resource(), &budget);
        for (std::pmr::string const &text : (**decoded)) {
            CHECK_EQ(text, long_text);
            CHECK_EQ(text.get_allocator().resource(), &budget);
        }
    }
    // The buffer holds the vector of three strings but not the characters of all three.
    for (std::size_t const size :
         {0uz, 3 * sizeof(std::pmr::string), 3 * sizeof(std::pmr::string) + long_text.size()}) {
        CAPTURE(size);
        std::array<std::byte, 4096> buffer{};
        std::pmr::monotonic_buffer_resource budget(buffer.data(), size, std::pmr::null_memory_resource());
        auto const decoded = cbor::structs<texts>::decode(std::make_shared<std::string const>(encoded),
                                                           texts(&budget));
        REQUIRE_FALSE(decoded.has_value());
        CHECK_EQ(decoded.error(), error::not_enough_memory);
    }
}

// The schema reads into the members of the target that the caller made, so each member keeps its allocator.
TEST_CASE("schema: a target with std::pmr members decodes with their allocators")
{
    auto const encoded = cbor::schema<pmr_names>::encode(
        pmr_names{std::pmr::string(long_text), {std::pmr::string(long_text), std::pmr::string(long_text)}});
    REQUIRE(encoded.has_value());
    std::array<std::byte, 4096> buffer{};
    std::pmr::monotonic_buffer_resource budget(buffer.data(), buffer.size(),
                                               std::pmr::null_memory_resource());
    auto const owner = std::make_shared<std::string const>(*encoded);
    auto const decoded = cbor::schema<pmr_names>::decode(
        owner, pmr_names{std::pmr::string(&budget), std::pmr::vector<std::pmr::string>(&budget)});
    REQUIRE(decoded.has_value());
    CHECK_EQ((**decoded).text, long_text);
    CHECK_EQ((**decoded).text.get_allocator().resource(), &budget);
    CHECK_EQ((**decoded).list.size(), 2u);
    CHECK_EQ((**decoded).list.get_allocator().resource(), &budget);
    for (std::pmr::string const &text : (**decoded).list) {
        CHECK_EQ(text, long_text);
        CHECK_EQ(text.get_allocator().resource(), &budget);
    }
}


// schema reads into the members of the target as structs does, so the refusal of their resource reaches the caller
// of schema as not_enough_memory, and std::bad_alloc does not leave the call.
TEST_CASE("schema: the refusal of the resource of a std::pmr member is not_enough_memory")
{
    auto const encoded = cbor::schema<pmr_names>::encode(
        pmr_names{std::pmr::string(long_text), {std::pmr::string(long_text), std::pmr::string(long_text)}});
    REQUIRE(encoded.has_value());
    auto const owner = std::make_shared<std::string const>(*encoded);
    for (std::size_t const size : {0uz, long_text.size() + 1, 2 * long_text.size() + 2}) {
        CAPTURE(size);
        std::array<std::byte, 4096> buffer{};
        std::pmr::monotonic_buffer_resource budget(buffer.data(), size, std::pmr::null_memory_resource());
        auto const decoded = cbor::schema<pmr_names>::decode(
            owner, pmr_names{std::pmr::string(&budget), std::pmr::vector<std::pmr::string>(&budget)});
        REQUIRE_FALSE(decoded.has_value());
        CHECK_EQ(decoded.error(), error::not_enough_memory);
    }
}

#endif
