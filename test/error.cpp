#include "host.hpp"

#include <initializer_list>
#include <system_error>

using cbor::condition;
using cbor::error;

// The extra parentheses keep doctest from printing std::error_code, which clang cannot do with
// libstdc++ 16.
// A binding that does not care which error it got checks one condition, as a rescue of a base
// class does. The groups are the kinds of error of RFC 8949: Appendix F and section 5.3.
TEST_CASE("error: every error belongs to one condition")
{
    for (error const e : {error::too_little_data, error::syntax_error})
        CHECK((std::error_code(e) == condition::not_well_formed));
    for (error const e : {error::invalid_utf8_string, error::inadmissible_type_for_tag_content,
                          error::sharedref_index_not_marked, error::sharedref_index_out_of_range,
                          error::sharedref_not_complete, error::reserved_simple_value})
        CHECK((std::error_code(e) == condition::not_valid));
    for (error const e : {error::indefinite_length, error::nesting_depth_exceeded, error::unsupported_value})
        CHECK((std::error_code(e) == condition::not_supported));
    for (error const e : {error::not_indexable, error::index_out_of_bounds, error::key_not_found,
                          error::incorrect_type, error::number_out_of_range})
        CHECK((std::error_code(e) == condition::not_found));
}

// An error is never success: the value 0 of std::error_code means no error.
TEST_CASE("error: no error converts to success")
{
    for (error const e :
         {error::too_little_data, error::syntax_error, error::indefinite_length, error::invalid_utf8_string,
          error::nesting_depth_exceeded, error::inadmissible_type_for_tag_content,
          error::sharedref_index_not_marked, error::sharedref_index_out_of_range,
          error::sharedref_not_complete, error::reserved_simple_value, error::unsupported_value})
        CHECK(static_cast<bool>(std::error_code(e)));
}
