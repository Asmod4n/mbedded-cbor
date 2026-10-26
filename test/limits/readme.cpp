#include "../owned_copy.hpp"
#include "../reference.hpp"

#include <string>

using namespace std::string_literals;

// This binary is built with the flags of step 5 of README.md: -DCBOR_NESTING_DEPTH_DEFAULT=16,
// -DCBOR_STRING_LENGTH_DEFAULT=4096 and -DCBOR_CONTAINER_ELEMENTS_DEFAULT=100. The test exists so that the
// example of the README compiles with these flags and gives the result that the README states.

TEST_CASE("doc readme: set limits")
{
    CHECK_EQ(cbor::limits::nesting_depth, 16u);
    CHECK_EQ(cbor::limits::string_length, 4096u);
    CHECK_EQ(cbor::limits::container_elements, 100u);

    test::array a;
    for (int i = 0; i < 101; ++i)
        a.push_back(test::V(i));
    std::string const bytes = test::preferred(test::value{std::move(a)});
    auto const n = test::lazy_of_copy(bytes).value().size();
    CHECK_EQ(n.error(), cbor::error::container_elements_exceeded);
}

// The limits hold across the boundary of tag 24: an array embedded in a byte string counts against the
// container limit as an outer array does. The byte string of 103 bytes is within the string limit of this
// build.
TEST_CASE("limits readme: an array inside tag 24 counts")
{
    std::string const embedded = "\x98\x65"s + std::string(101, '\x00');
    auto const tagged = test::lazy_of_copy("\xd8\x18\x58\x67"s + embedded);
    REQUIRE(tagged.has_value());
    auto const refused = tagged->clone();
    REQUIRE_FALSE(refused.has_value());
    CHECK_EQ(refused.error(), cbor::error::container_elements_exceeded);
}
