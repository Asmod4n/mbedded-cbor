#include "reference.hpp"
#include "owned_copy.hpp"
#include <cbor/diagnostic_notation.hpp>
#include <cbor/encode.hpp>
#include <cbor/schema.hpp>
#include <cbor/structs.hpp>

#include <bit>
#include <cstdint>
#include <expected>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one mbedded-cbor example of bench/comparison.md that the section "Summary" of that page
// does not show. The tests exist so that every example in the document compiles and gives the
// result that the document states. The text between "begin" and "end" is the text of the document.

// The section "Read one field by path" names the type of the result, which the short form in
// the section "Summary" leaves to auto.
TEST_CASE("doc advanced comparison: read one field by path")
{
    std::string const bytes = encode_in_preferred_form(M("user"s, M("id"s, 42)));
    // begin path
    cbor::result<std::int64_t> const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
    // end path
    CHECK_EQ(id, 42);
}

// The section "Iterate an array" sums the elements of an array through a lazy.
TEST_CASE("doc advanced comparison: iterate an array")
{
    std::string bytes = encode_in_preferred_form(A(1, 2, 3));
    // begin iterate
    auto const doc = cbor::lazy::adopt(std::move(bytes)); // [1, 2, 3]
    std::int64_t sum = 0;
    auto const elements = doc->elements();
    for (auto const e : *elements)
        sum += e->get<std::int64_t>().value_or(0);
    // end iterate
    CHECK_EQ(sum, 6);
}

// The section "Shared values" shows that a lazy reads tag 29 as a plain tag over its index, and that structs
// refuses a value under tag 28 or tag 29.
TEST_CASE("doc advanced comparison: shared values")
{
    auto const edn_bytes = cbor::diag2cbor(R"([28("x"), 29(0)])");
    auto const owner =
        std::make_shared<std::string const>(edn_bytes.value());
    // begin shared
    auto const viewed = cbor::lazy::view(owner);
    auto const second = viewed->at(1);
    // second->type() is cbor::type::tag, and second->get<std::uint64_t>() is 0
    // end shared
    CHECK_EQ(second->type(), cbor::type::tag);
    CHECK_EQ(second->get<std::uint64_t>(), 0u);
    // begin shared-structs
    auto const v = cbor::structs<std::vector<std::string>>::decode(owner);
    // v.error() is cbor::error::invalid_tag
    // end shared-structs
    CHECK_EQ(v.error(), error::invalid_tag);
}

namespace doc_advanced_comparison
{

// begin numbers-decl
struct series {
    std::vector<double> values;
};
} // namespace doc_advanced_comparison

CBOR_DEFINE_TYPE_MEMBERS(doc_advanced_comparison::series, cbor::tag{1900}, values);

namespace doc_advanced_comparison
{

// end numbers-decl

// The section "A number array" reads a std::vector<double> member as an RFC 8746 typed array with no copy.
TEST_CASE("doc advanced comparison: a number array")
{
    std::string bytes = *cbor::schema<series>::encode(series{{1.5, 2.5, 3.5}});
    // begin numbers
    auto const root = cbor::schema<series>::adopt(std::move(bytes));
    auto const values = root->view<"$.values">();
    double const sum = std::accumulate(values->begin(), values->end(), 0.0);
    // end numbers
    CHECK_EQ(std::bit_cast<std::uint64_t>(sum), 0x401e000000000000u);
}

} // namespace doc_advanced_comparison
