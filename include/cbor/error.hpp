#pragma once

#include <expected>
#include <string>
#include <system_error>
#include <type_traits>

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
    duplicate_key
};

enum class condition { not_well_formed = 1, not_valid, not_supported, not_found };

class category final : public std::error_category
{
public:
    constexpr category() = default;

    char const *name() const noexcept override
    {
        return "cbor";
    }

    std::string message(int const value) const override
    {
        switch (static_cast<error>(value)) {
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
        }
        return "unknown cbor error";
    }

    std::error_condition default_error_condition(int const value) const noexcept override
    {
        switch (static_cast<error>(value)) {
        case error::too_little_data:
        case error::syntax_error:
            return {static_cast<int>(condition::not_well_formed), *this};
        case error::indefinite_length:
        case error::nesting_depth_exceeded:
        case error::unsupported_value:
        case error::cyclic_data_structure:
        case error::nodelist_too_long:
            return {static_cast<int>(condition::not_supported), *this};
        case error::not_indexable:
        case error::index_out_of_bounds:
        case error::key_not_found:
        case error::incorrect_type:
        case error::number_out_of_range:
            return {static_cast<int>(condition::not_found), *this};
        case error::inadmissible_type_for_tag_content:
        case error::sharedref_index_not_marked:
        case error::sharedref_index_out_of_range:
        case error::sharedref_not_complete:
        case error::reserved_simple_value:
        case error::invalid_path:
        case error::unpopulated_table_index:
        case error::duplicate_key:
            return {static_cast<int>(condition::not_valid), *this};
        }
        return {value, *this};
    }
};

inline constinit category const cbor_category;

inline std::error_code make_error_code(error const e) noexcept
{
    return {static_cast<int>(e), cbor_category};
}

inline std::error_condition make_error_condition(condition const c) noexcept
{
    return {static_cast<int>(c), cbor_category};
}

template <class T, class E = error>
struct result;

template <class T, class E>
struct result : std::expected<T, E> {
    using std::expected<T, E>::expected;
};

}

template <>
struct std::is_error_code_enum<cbor::error> : std::true_type {};

template <>
struct std::is_error_condition_enum<cbor::condition> : std::true_type {};
