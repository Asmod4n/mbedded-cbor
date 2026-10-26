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

// Each test in this file is one example of a page in doc/reference/errors/. The tests exist so that every
// example compiles and gives the result that its page states.

// error.md: a missing key comes back as the value key_not_found, and its text comes from std::error_code.
TEST_CASE("doc reference errors: error")
{
    std::string const bytes = test::preferred(test::M("user"s, test::M("id"s, 42)));
    auto const age = cbor::at_path<"$.user.age", std::int64_t>(bytes);
    CHECK_EQ(age.error(), cbor::error::key_not_found);
    CHECK_EQ(std::error_code{age.error()}.message(), "key not found"s);
}

// error.md: the first enumerator has the value 1, so no enumerator is equal to a std::error_code that holds 0.
TEST_CASE("doc reference errors: no error has the value 0")
{
    CHECK_EQ(static_cast<int>(cbor::error::too_little_data), 1);
    CHECK(static_cast<bool>(std::error_code{cbor::error::too_little_data}));
}

// category.md: every cbor::error converts into a std::error_code of the one category, named "cbor".
TEST_CASE("doc reference errors: category")
{
    std::error_code const e = cbor::error::syntax_error;
    CHECK_EQ(e.category(), cbor::category());
    CHECK_EQ(std::string_view(cbor::category().name()), "cbor");
}

// error_category.md: four errors compare equal to a std::errc, and the others do not.
TEST_CASE("doc reference errors: error_category")
{
    CHECK(std::error_code{cbor::error::no_buffer_space} == std::errc::no_buffer_space);
    CHECK(std::error_code{cbor::error::not_enough_memory} == std::errc::not_enough_memory);
    CHECK(std::error_code{cbor::error::too_little_data} != std::errc::no_message);
    CHECK_EQ(cbor::category().message(static_cast<int>(cbor::error::not_singular_query)), "not a singular query"s);
    CHECK_EQ(cbor::category().message(0), "unknown cbor error"s);
}

// make_error_code.md: make_error_code gives the value and the category of the error.
TEST_CASE("doc reference errors: make_error_code")
{
    std::error_code const e = cbor::make_error_code(cbor::error::invalid_path);
    CHECK_EQ(e.value(), static_cast<int>(cbor::error::invalid_path));
    CHECK_EQ(e.message(), "invalid path"s);
    CHECK_EQ(e, std::error_code{cbor::error::invalid_path});
}

// is_error_code_enum.md: the specialization lets a cbor::error convert to a std::error_code without a cast.
TEST_CASE("doc reference errors: is_error_code_enum")
{
    CHECK(std::is_error_code_enum_v<cbor::error>);
    std::error_code const e = cbor::error::index_out_of_bounds;
    CHECK_EQ(e.message(), "index outside of array bounds"s);
    CHECK(e == cbor::error::index_out_of_bounds);
}

// limits.md: a build that sets no macro has the default limits.
TEST_CASE("doc reference errors: limits")
{
    CHECK_EQ(cbor::limits::nesting_depth, std::size_t{128});
    CHECK_EQ(cbor::limits::string_length, SIZE_MAX);
    CHECK_EQ(cbor::limits::container_elements, SIZE_MAX);
    CHECK_EQ(CBOR_NESTING_DEPTH_DEFAULT, 128);
}
