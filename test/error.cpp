#include "reference.hpp"

#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

using cbor::error;

namespace
{

// cbor::error is a finite set, so each test below checks each member. The list holds the 28 members in the order of
// include/cbor/error.hpp, whose values run from 1 to 28 with no gap.
constexpr std::array every_error{
    error::too_little_data,
    error::syntax_error,
    error::indefinite_length,
    error::nesting_depth_exceeded,
    error::inadmissible_type_for_tag_content,
    error::sharedref_index_not_marked,
    error::sharedref_not_complete,
    error::sharedref_not_defined,
    error::reserved_simple_value,
    error::unsupported_value,
    error::not_indexable,
    error::index_out_of_bounds,
    error::key_not_found,
    error::invalid_path,
    error::incorrect_type,
    error::number_out_of_range,
    error::cyclic_data_structure,
    error::unpopulated_table_index,
    error::nodelist_too_long,
    error::duplicate_key,
    error::no_buffer_space,
    error::value_too_large,
    error::not_enough_memory,
    error::io_error,
    error::string_length_exceeded,
    error::container_elements_exceeded,
    error::not_singular_query,
    error::amplification_limit_breach,
};

} // namespace

// A new member of cbor::error that the list above misses would go unchecked. The values run from 1 to the last
// member, so the size of the list equals the value of the last member.
TEST_CASE("error: the list of the tests holds every member of cbor::error")
{
    CHECK_EQ(every_error.size(), static_cast<std::size_t>(error::amplification_limit_breach));
}

// An error never equals the value-initialized enum: a result that holds error{} would read as no error.
TEST_CASE("error: no error is the value-initialized enum")
{
    for (error const e : every_error)
        CHECK_NE(e, error{});
}

// An error converts to std::error_code, so code that handles std::error_code handles a CBOR error too.
TEST_CASE("error: an error converts to std::error_code")
{
    CHECK_EQ(cbor::make_error_code(error::too_little_data).message(), "too little data");
    CHECK_EQ(std::error_code(error::index_out_of_bounds).message(), "index outside of array bounds");
    CHECK_EQ(std::string_view(cbor::category().name()), "cbor");
    CHECK(std::error_code(error::key_not_found) == error::key_not_found);
    CHECK(std::error_code(error::key_not_found) != std::errc::no_buffer_space);
}

// A log that shows the message of an error must tell each error apart, and no member may fall through to the text
// of an unknown value.
TEST_CASE("error: every error has its own message")
{
    std::set<std::string> messages;
    for (error const e : every_error) {
        CAPTURE(static_cast<int>(e));
        std::string const message = cbor::make_error_code(e).message();
        CHECK_NE(message, "unknown cbor error");
        CHECK(messages.insert(message).second);
    }
}

// The writer errors are copies of std::errc values (detail::validity::writer_error), but an error_code of
// them compared unequal to the std::errc they came from. Code that tests for std::errc::no_buffer_space
// missed a full buffer of this library. Each such error now has the std::errc as its default error condition;
// every other error keeps its own condition in the cbor category.
TEST_CASE("error: an error copied from std::errc compares equal to that std::errc")
{
    CHECK(std::error_code(error::no_buffer_space) == std::errc::no_buffer_space);
    CHECK(std::error_code(error::value_too_large) == std::errc::value_too_large);
    CHECK(std::error_code(error::not_enough_memory) == std::errc::not_enough_memory);
    CHECK(std::error_code(error::io_error) == std::errc::io_error);
    CHECK(std::error_code(error::no_buffer_space) == error::no_buffer_space);
    CHECK(std::error_code(error::no_buffer_space) != std::errc::value_too_large);
    CHECK(std::error_code(error::key_not_found) != std::errc::io_error);
    CHECK_EQ(cbor::category().default_error_condition(static_cast<int>(error::key_not_found)).category(),
             cbor::category());
    for (error const e : every_error)
        CHECK(std::error_code(e) == e);
}
