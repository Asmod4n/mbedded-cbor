#include "reference.hpp"

#include <cstdint>
#include <expected>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one mbedded-cbor example of doc/advanced/comparison.md that doc/comparison.md does not
// show. The tests exist so that every example in the document compiles and gives the result that the document
// states. The text between "begin" and "end" is the text of the document.

// The section "Read one field by path" names the type of the result, which the short form of doc/comparison.md
// leaves to auto.
TEST_CASE("doc advanced comparison: read one field by path")
{
    std::string const bytes = preferred(M("user"s, M("id"s, 42)));
    // begin path
    std::expected<std::int64_t, cbor::error> const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
    // end path
    CHECK_EQ(id, 42);
}

// The section "Iterate an array" sums the elements of an array through a lazy.
TEST_CASE("doc advanced comparison: iterate an array")
{
    std::string const bytes = preferred(A(1, 2, 3));
    // begin iterate
    auto const doc = cbor::lazy::from(bytes); // [1, 2, 3]
    std::int64_t sum = 0;
    for (auto const e : *doc->elements())
        sum += e->get<std::int64_t>().value_or(0);
    // end iterate
    CHECK_EQ(sum, 6);
}

// The section "Shared values" reads tag 29 as the item that tag 28 marks, through a lazy and through structs.
TEST_CASE("doc advanced comparison: shared values")
{
    std::string const bytes = "\x82\xd8\x1c\x61x\xd8\x1d\x00"s;
    // begin shared
    auto const second = cbor::lazy::from(bytes)->at(1)->get<std::string_view>();
    // **second is "x"
    // end shared
    CHECK_EQ(**second, "x"sv);
#ifdef __cpp_impl_reflection
    // begin shared-structs
    auto const v = cbor::structs<std::vector<std::string>>::decode(bytes);
    // **v is {"x", "x"}
    // end shared-structs
    CHECK_EQ(**v, std::vector<std::string>{"x", "x"});
#endif
}

#ifdef __cpp_impl_reflection

namespace doc_advanced_comparison
{

// begin numbers-decl
struct [[=cbor::tag(1900)]] series {
    std::vector<double> values;
};
// end numbers-decl

// The section "A number array" reads a std::vector<double> member as an RFC 8746 typed array with no copy.
TEST_CASE("doc advanced comparison: a number array")
{
    std::string const bytes = *cbor::packed<series>::encode(series{{1.5, 2.5, 3.5}});
    // begin numbers
    auto const root = cbor::packed<series>::path(bytes);
    auto const values = root->view<"$.values">();
    double const sum = std::accumulate(values->begin(), values->end(), 0.0);
    // end numbers
    CHECK_EQ(sum, 7.5);
}

} // namespace doc_advanced_comparison

#endif
