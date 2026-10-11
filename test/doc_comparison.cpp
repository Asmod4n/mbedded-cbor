#include "binding.hpp"

#include <cstdint>
#include <expected>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is the mbedded-cbor column of doc/comparison.md. The tests exist so that every example in
// the document compiles and gives the result that the document states. The text between "begin" and "end" is the
// text of the document.

// The section "Read one field by path" reads a number two maps deep without a decode of the whole item.
TEST_CASE("doc comparison: read one field by path")
{
    // begin path
    std::string const bytes = "\xa1\x64user\xa1\x62id\x18\x2a"s; // {"user": {"id": 42}}
    std::expected<std::int64_t, cbor::error> const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
    // end path
    CHECK_EQ(id, 42);
}

// The section "Iterate an array" sums the elements of an array through a lazy.
TEST_CASE("doc comparison: iterate an array")
{
    // begin iterate
    auto const doc = cbor::lazy::from("\x83\x01\x02\x03"s); // [1, 2, 3]
    std::int64_t sum = 0;
    for (auto const e : *doc->elements())
        sum += e->get<std::int64_t>().value_or(0);
    // end iterate
    CHECK_EQ(sum, 6);
}

// The section "Shared values" reads tag 29 as the item that tag 28 marks, through a lazy and through databind.
TEST_CASE("doc comparison: shared values")
{
    // begin shared
    std::string const bytes = "\x82\xd8\x1c\x61x\xd8\x1d\x00"s; // [28("x"), 29(0)]
    auto const second = cbor::lazy::from(bytes)->at(1)->get<std::string_view>();
    // **second is "x"
    // end shared
    CHECK_EQ(**second, "x"sv);
}

// The section "Broken input and errors" gives the error of an array that the input cuts off.
TEST_CASE("doc comparison: broken input and errors")
{
    // begin errors
    auto const r = cbor::lazy::from("\x82\x01"s)->at(1); // [1, and the end of the input
    bool const broken = !r && r.error() == cbor::error::too_little_data;
    // end errors
    CHECK(broken);
}

#ifdef __cpp_impl_reflection

namespace doc_comparison
{

// begin struct-decl
struct person {
    std::string name;
    std::uint8_t age;
};
// end struct-decl

// begin numbers-decl
struct [[=cbor::tag(1900)]] series {
    std::vector<double> values;
};
// end numbers-decl

// The section "Read and write a struct" gives the round trip of an aggregate through databind.
TEST_CASE("doc comparison: read and write a struct")
{
    // begin struct
    auto const bytes = cbor::databind<person>::encode(person{"ann", 30});
    auto const back = cbor::databind<person>::decode(*bytes);
    // (*back)->name is "ann", (*back)->age is 30
    // end struct
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->name, "ann");
    CHECK_EQ((*back)->age, 30u);
}

// The section "Shared values" says that databind reads tag 29 as the item that tag 28 marks.
TEST_CASE("doc comparison: shared values through databind")
{
    // begin shared-databind
    auto const v = cbor::databind<std::vector<std::string>>::decode("\x82\xd8\x1c\x61x\xd8\x1d\x00"s);
    // **v is {"x", "x"}
    // end shared-databind
    CHECK_EQ(**v, std::vector<std::string>{"x", "x"});
}

// The section "A number array" reads a std::vector<double> member as an RFC 8746 typed array with no copy.
TEST_CASE("doc comparison: a number array")
{
    std::string const encoded_bytes = *cbor::schema<series>::encode(series{{1.5, 2.5, 3.5}});
    // begin numbers
    auto const root = cbor::schema<series>::path(encoded_bytes);
    auto const values = root->view<"$.values">();
    double const sum = std::accumulate(values->begin(), values->end(), 0.0);
    // end numbers
    CHECK_EQ(sum, 7.5);
}

} // namespace doc_comparison

#endif
