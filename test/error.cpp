#include "binding.hpp"

#include <initializer_list>

using cbor::error;

// An error never equals the value-initialized enum: a result that holds error{} would read as no error.
TEST_CASE("error: no error is the value-initialized enum")
{
    for (error const e :
         {error::too_little_data, error::syntax_error, error::indefinite_length,
          error::nesting_depth_exceeded, error::inadmissible_type_for_tag_content,
          error::sharedref_index_not_marked, error::sharedref_index_out_of_range,
          error::sharedref_not_complete, error::reserved_simple_value, error::unsupported_value,
          error::not_indexable, error::index_out_of_bounds, error::key_not_found, error::invalid_path,
          error::incorrect_type, error::number_out_of_range, error::cyclic_data_structure,
          error::unpopulated_table_index, error::nodelist_too_long, error::duplicate_key})
        CHECK_NE(e, error{});
}

// The text of an error is a static string, read without an allocation.
TEST_CASE("error: every error has its own message")
{
    CHECK_EQ(cbor::message(error::too_little_data), "too little data");
    CHECK_EQ(cbor::message(error::index_out_of_bounds), "index outside of array bounds");
    for (error const e :
         {error::too_little_data, error::syntax_error, error::indefinite_length,
          error::nesting_depth_exceeded, error::inadmissible_type_for_tag_content,
          error::sharedref_index_not_marked, error::sharedref_index_out_of_range,
          error::sharedref_not_complete, error::reserved_simple_value, error::unsupported_value,
          error::not_indexable, error::index_out_of_bounds, error::key_not_found, error::invalid_path,
          error::incorrect_type, error::number_out_of_range, error::cyclic_data_structure,
          error::unpopulated_table_index, error::nodelist_too_long, error::duplicate_key})
        CHECK_NE(cbor::message(e), "unknown cbor error");
}
