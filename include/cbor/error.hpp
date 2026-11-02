// Gives cbor::error, the error codes of the library, and their
// std::error_category. Maps four codes to std::errc conditions.
// Stops the build with g++ older than 15.
#pragma once

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 15
#error "mbedded-cbor needs g++ 15 or later"
#endif

#include <expected>
#include <string>
#include <system_error>
#include <type_traits>

namespace cbor
{

/// Error values returned by library operations.
/// Each named value is nonzero and converts to std::error_code.
enum class error {
    /// The input ends before the requested data is complete.
    too_little_data = 1,
    /// The input is not valid for the operation.
    syntax_error,
    /// The operation does not accept an indefinite-length item.
    indefinite_length,
    /// The item or path exceeds the nesting limit.
    nesting_depth_exceeded,
    /// A tag contains a type its definition does not permit.
    inadmissible_type_for_tag_content,
    /// A shared reference names no preceding marked value.
    sharedref_index_not_marked,
    /// A shared reference index does not fit std::size_t.
    sharedref_index_out_of_range,
    /// A shared reference names a skipped value during struct decoding.
    sharedref_not_complete,
    /// An encoded simple value is reserved.
    reserved_simple_value,
    /// The operation does not support this value.
    unsupported_value,
    /// A key or index is applied to a non-indexable item.
    not_indexable,
    /// An index is outside the array.
    index_out_of_bounds,
    /// A map has no matching key.
    key_not_found,
    /// The path text is invalid.
    invalid_path,
    /// The item cannot be read as the requested type.
    incorrect_type,
    /// The number does not fit the requested type.
    number_out_of_range,
    /// A writer can return this for a cycle.
    cyclic_data_structure,
    /// A schema reference names an absent table entry.
    unpopulated_table_index,
    /// A path step selects more nodes than the item has bytes.
    nodelist_too_long,
    /// A uniqueness check found a duplicate key.
    duplicate_key,
    /// The writer has no buffer space.
    no_buffer_space,
    /// The value is too large for the writer.
    value_too_large,
    /// An allocation or writer operation has insufficient memory.
    not_enough_memory,
    /// The writer reports an input or output error.
    io_error,
    /// A string exceeds the configured limit.
    string_length_exceeded,
    /// A container exceeds the configured element limit.
    container_elements_exceeded,
    not_singular_query
};

/// Maps error values to messages and standard error conditions.
class error_category : public std::error_category
{
public:
    /// Returns the category name, "cbor".
    /// @par Complexity
    /// O(1); allocates nothing.
    char const *name() const noexcept override
    {
        return "cbor";
    }

    /// Returns the message for a code, or "unknown cbor error".
    /// @param condition The numeric error code.
    /// @par Complexity
    /// O(1) for the fixed set of messages; constructing the string may allocate.
    std::string message(int const condition) const override
    {
        switch (static_cast<error>(condition)) {
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
        case error::string_length_exceeded:
            return "string length exceeded";
        case error::container_elements_exceeded:
            return "container elements exceeded";
        case error::not_singular_query:
            return "not a singular query";
        }
        return "unknown cbor error";
    }

    /// Maps four error values to std::errc; other values keep this category.
    /// @param condition The numeric error code.
    /// @par Complexity
    /// O(1); allocates nothing.
    std::error_condition default_error_condition(int const condition) const noexcept override
    {
        switch (static_cast<error>(condition)) {
        case error::no_buffer_space:
            return std::make_error_condition(std::errc::no_buffer_space);
        case error::value_too_large:
            return std::make_error_condition(std::errc::value_too_large);
        case error::not_enough_memory:
            return std::make_error_condition(std::errc::not_enough_memory);
        case error::io_error:
            return std::make_error_condition(std::errc::io_error);
        default:
            return std::error_condition(condition, *this);
        }
    }
};

/// Returns the shared category for cbor::error.
/// @par Complexity
/// O(1); initializes the category on first use.
inline std::error_category const &category() noexcept
{
    static error_category const instance;
    return instance;
}

/// Converts an error to a std::error_code with the CBOR category.
/// @param e The error value.
/// @par Complexity
/// O(1); initializes the category on first use.
inline std::error_code make_error_code(error const e) noexcept
{
    return {static_cast<int>(e), category()};
}

} // namespace cbor

/// Enables implicit conversion of cbor::error to std::error_code.
template <>
struct std::is_error_code_enum<cbor::error> : std::true_type {};
