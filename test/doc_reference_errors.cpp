#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

using namespace std::string_literals;

// Each test in this file is one example of a reference page of the errors group; the comment of the header gives the
// example through the snippet markers. The tests exist so that every example compiles and gives the result that its
// page states.

// error.hpp, snippet error: a missing key comes back as the value key_not_found, and its text comes from
// std::error_code.
TEST_CASE("doc reference errors: error")
{
    std::string const bytes = test::preferred(test::M("user"s, test::M("id"s, 42)));
    // [error]
    // bytes: {"user": {"id": 42}}
    auto const age = cbor::at_path<"$.user.age", std::int64_t>(bytes);
    auto const e = age.error();                              // key_not_found
    auto const text = std::error_code{e}.message();          // "key not found"
    // [error]
    CHECK_EQ(e, cbor::error::key_not_found);
    CHECK_EQ(text, "key not found"s);
}

// error.hpp, the comment of error: the first enumerator has the value 1, so no enumerator is equal to a
// std::error_code that holds 0.
TEST_CASE("doc reference errors: no error has the value 0")
{
    CHECK_EQ(static_cast<int>(cbor::error::too_little_data), 1);
    CHECK(static_cast<bool>(std::error_code{cbor::error::too_little_data}));
}

// error.hpp, snippet category: every cbor::error converts into a std::error_code of the one category, named "cbor".
TEST_CASE("doc reference errors: category")
{
    // [category]
    std::error_code const e = cbor::error::syntax_error;
    bool const same = e.category() == cbor::category();     // true
    std::string_view const name = cbor::category().name();  // "cbor"
    // [category]
    CHECK(same);
    CHECK_EQ(name, "cbor");
}

// error.hpp, snippet error_category: four errors compare equal to a std::errc, and the others do not.
TEST_CASE("doc reference errors: error_category")
{
    // [error_category]
    bool const space = std::error_code{cbor::error::no_buffer_space} == std::errc::no_buffer_space;  // true
    bool const other = std::error_code{cbor::error::too_little_data} == std::errc::no_message;       // false
    std::string const unknown = cbor::category().message(0);                                         // "unknown cbor error"
    // [error_category]
    CHECK(space);
    CHECK_FALSE(other);
    CHECK_EQ(unknown, "unknown cbor error"s);
    CHECK(std::error_code{cbor::error::not_enough_memory} == std::errc::not_enough_memory);
    CHECK_EQ(cbor::category().message(static_cast<int>(cbor::error::not_singular_query)), "not a singular query"s);
}

// error.hpp, snippet make_error_code: make_error_code gives the value and the category of the error.
TEST_CASE("doc reference errors: make_error_code")
{
    // [make_error_code]
    std::error_code const e = cbor::make_error_code(cbor::error::invalid_path);
    std::string const text = e.message();                               // "invalid path"
    bool const same = e == std::error_code{cbor::error::invalid_path};  // true
    // [make_error_code]
    CHECK_EQ(text, "invalid path"s);
    CHECK(same);
    CHECK_EQ(e.value(), static_cast<int>(cbor::error::invalid_path));
}

// error.hpp, snippet is_error_code_enum: the specialization lets a cbor::error convert to a std::error_code without
// a cast.
TEST_CASE("doc reference errors: is_error_code_enum")
{
    CHECK(std::is_error_code_enum_v<cbor::error>);
    // [is_error_code_enum]
    std::error_code const e = cbor::error::index_out_of_bounds;
    std::string const text = e.message();                     // "index outside of array bounds"
    bool const same = e == cbor::error::index_out_of_bounds;  // true
    // [is_error_code_enum]
    CHECK_EQ(text, "index outside of array bounds"s);
    CHECK(same);
}

// validity.hpp, snippet limits: a build that sets no macro has the default limits.
TEST_CASE("doc reference errors: limits")
{
    // [limits]
    // a build that sets no macro
    std::size_t const strings = cbor::limits::string_length;          // SIZE_MAX
    std::size_t const containers = cbor::limits::container_elements;  // SIZE_MAX
    // [limits]
    CHECK_EQ(strings, SIZE_MAX);
    CHECK_EQ(containers, SIZE_MAX);
}
