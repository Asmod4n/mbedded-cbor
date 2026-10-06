#pragma once

#include <expected>
#include <string_view>

namespace cbor
{

enum class error {
    too_little_data = 1,
    syntax_error,
    indefinite_length,
    nesting_depth_exceeded,
    inadmissible_type_for_tag_content,
    sharedref_index_not_marked,
    sharedref_index_out_of_range,
    sharedref_not_complete,
    reserved_simple_value,
    unsupported_value,
    not_indexable,
    index_out_of_bounds,
    key_not_found,
    invalid_path,
    incorrect_type,
    number_out_of_range,
    cyclic_data_structure,
    unpopulated_table_index,
    nodelist_too_long,
    duplicate_key,
    no_buffer_space,
    value_too_large,
    not_enough_memory,
    io_error
};

constexpr std::string_view message(error const e) noexcept
{
    switch (e) {
    case error::too_little_data:
        return "too little data";
    case error::syntax_error:
        return "syntax error";
    case error::indefinite_length:
        return "indefinite length";
    case error::nesting_depth_exceeded:
        return "nesting depth exceeded";
    case error::inadmissible_type_for_tag_content:
        return "inadmissible type for tag content";
    case error::sharedref_index_not_marked:
        return "sharedref index not marked";
    case error::sharedref_index_out_of_range:
        return "sharedref index out of range";
    case error::sharedref_not_complete:
        return "sharedref not complete";
    case error::reserved_simple_value:
        return "reserved simple value";
    case error::unsupported_value:
        return "unsupported value";
    case error::not_indexable:
        return "not indexable";
    case error::index_out_of_bounds:
        return "index outside of array bounds";
    case error::key_not_found:
        return "key not found";
    case error::invalid_path:
        return "invalid path";
    case error::incorrect_type:
        return "incorrect type";
    case error::number_out_of_range:
        return "number out of range";
    case error::cyclic_data_structure:
        return "cyclic data structure";
    case error::unpopulated_table_index:
        return "unpopulated table index";
    case error::nodelist_too_long:
        return "nodelist too long";
    case error::duplicate_key:
        return "duplicate key";
    case error::no_buffer_space:
        return "no buffer space";
    case error::value_too_large:
        return "value too large";
    case error::not_enough_memory:
        return "not enough memory";
    case error::io_error:
        return "io error";
    }
    return "unknown cbor error";
}

template <class T, class E = error>
struct result;

template <class T, class E>
struct result : std::expected<T, E> {
    using std::expected<T, E>::expected;
};

}
