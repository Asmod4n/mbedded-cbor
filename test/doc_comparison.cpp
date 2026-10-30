#include "reference.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one mbedded-cbor example of doc/comparison.md. The tests exist so that every example in
// the document compiles and gives the result that the document states. The text between "begin" and "end" is the
// text of the document.

// The section "Read one field" reads a number two maps deep without a decode of the whole item.
TEST_CASE("doc comparison: read one field")
{
    std::string const bytes = preferred(M("user"s, M("id"s, 42)));
    // begin path
    auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes); // *id == 42
    // end path
    CHECK_EQ(id, 42);
}

// The section "Broken input" gives the error of an array that the input cuts off.
TEST_CASE("doc comparison: broken input")
{
    std::string const whole = preferred(A(1, 2));
    std::string const bytes = whole.substr(0, whole.size() - 1);
    // begin errors
    auto const r = cbor::at_path<"$[1]", std::int64_t>(bytes);
    bool const broken = !r && r.error() == cbor::error::too_little_data;
    // end errors
    CHECK(broken);
}


namespace doc_comparison
{

// begin struct-decl
struct person {
    std::string name;
    int age;
};
// end struct-decl

// The section "Write a struct and read it back" gives the round trip of an aggregate through structs.
TEST_CASE("doc comparison: write a struct and read it back")
{
    // begin struct
    auto bytes = cbor::structs<person>::encode(person{"ann", 30});
    auto const back = cbor::structs<person>::decode(std::move(*bytes));
    // (*back)->name is "ann", (*back)->age is 30
    // end struct
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->name, "ann");
    CHECK_EQ((*back)->age, 30);
}

} // namespace doc_comparison

