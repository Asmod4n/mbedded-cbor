/**
 * @file
 * Gives cbor::error, the error codes of the library, and their
 * std::error_category. Maps four codes to std::errc conditions.
 * Stops the build with g++ older than 15 and with the frontend of MSVC.
 */
#pragma once

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 15
#error "mbedded-cbor needs g++ 15 or later"
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#error "mbedded-cbor needs the clang frontend: on Windows build with clang-cl"
#endif

#include <expected>
#include <string>
#include <system_error>
#include <type_traits>

namespace cbor
{

/**
 * @brief Names the fault that a call returns in its `std::expected<T, cbor::error>`.
 *
 * Bad input never throws.
 * The first value is 1, so a `std::error_code` from a `cbor::error` is always true as a `bool`.
 * A wrong use of the library is not an error value: it does not compile, or it throws `std::logic_error`.
 *
 * @code{.callseq}
 * cbor::error::key_not_found   → cbor::error
 * std::error_code{e}.message() → std::string
 * @endcode
 * @code{.example}
 * message: {"user": {"id": 42}}
 * call: cbor::at_path<"$.user.id", std::int64_t>(bytes)
 * result: 42
 * call: cbor::error::key_not_found
 * result: cbor::error::key_not_found
 * call: std::error_code{cbor::error::key_not_found}.message()
 * result: "key not found"
 * @endcode
 *
 * @snippet doc_reference_errors.cpp error
 * @see cbor::make_error_code()
 * @see cbor::error_category
 *
 * @ingroup errors
 */
enum class error {
    /** The bytes end before the item ends. */
    too_little_data = 1,
    /**
     * A head is not well-formed, or a simple value is reserved in the two-byte form. Bytes after the item, where a
     * call reads one item.
     */
    syntax_error,
    /** A head uses indefinite length, and the call does not read it. */
    indefinite_length,
    /** A reader that recurses comes to the end of the stack of its thread (cbor::limits). */
    nesting_depth_exceeded,
    /** A tag has content that its definition does not allow. */
    inadmissible_type_for_tag_content,
    /** A tag 29 names no tag 28 before it. */
    sharedref_index_not_marked,
    /** `structs` decode meets a tag 29 that names a tag 28 in a skipped member. */
    sharedref_not_complete,
    /** `decode` meets a tag 29 that names a tag 28 whose content is only tags 28 and this tag 29. */
    sharedref_not_defined,
    /** An encode meets a simple value from 24 to 31. */
    reserved_simple_value,
    /** The encoder meets a typed array with a reserved tag or a tag outside RFC 8746. */
    unsupported_value,
    /** A key or an index meets an item that is not a map or an array. */
    not_indexable,
    /** The index is outside the array. */
    index_out_of_bounds,
    /** The key is not in the map. */
    key_not_found,
    /** The path text is not valid. */
    invalid_path,
    /** A typed read finds another kind of item than T takes. */
    incorrect_type,
    /** The number does not fit T. */
    number_out_of_range,
    /** The library does not return it. A writer of the program can return it for a cycle. */
    cyclic_data_structure,
    /** A reference in a `schema<T>` message names an entry that its table does not have. */
    unpopulated_table_index,
    /** One step of a path selects more nodes than the item has bytes. */
    nodelist_too_long,
    /** A deterministic check finds a key twice in one map. */
    duplicate_key,
    /** The writer of an encode gives this error. */
    no_buffer_space,
    /** The writer of an encode gives this error. */
    value_too_large,
    /**
     * The writer of an encode gives this error. It also comes from a refused allocation in `structs` and `schema`
     * decode.
     */
    not_enough_memory,
    /** The writer of an encode gives this error. */
    io_error,
    /** A string is longer than `cbor::limits::string_length`. */
    string_length_exceeded,
    /** A container has more items than `cbor::limits::container_elements`. */
    container_elements_exceeded,
    /** `path::compile<T>` gets a path that is not singular, and `T` is not `nodelist`. */
    not_singular_query,
    /**
     * cbor::encode() with cbor::cycles would write more than `cbor::limits::maximum_amplification` times the size of the
     * tree, past `cbor::limits::amplification_activation_threshold`.
     */
    amplification_limit_breach
};

/**
 * @brief The `std::error_category` of cbor::error.
 *
 * A program reaches it through cbor::category().
 * `name()` gives `"cbor"`.
 * `message` gives the text of the error, for example `"key not found"`, and `"unknown cbor error"` for a value that
 * is no `cbor::error`.
 *
 * `default_error_condition` maps four errors to a `std::errc`: `no_buffer_space`, `value_too_large`,
 * `not_enough_memory` and `io_error`, each to the `std::errc` of the same name.
 * Every other error maps to a condition of this category.
 * So only these four compare equal to a `std::errc`.
 *
 * @code{.callseq}
 * cbor::category().name()                             → char const *
 * cbor::category().message(condition)                 → std::string
 * cbor::category().default_error_condition(condition) → std::error_condition
 * @endcode
 * @code{.example}
 * message: {}
 * call: std::error_code const e{cbor::error::no_buffer_space}
 * result: std::error_code cbor:21
 * call: e.default_error_condition().category().name()
 * result: "generic"
 * call: std::error_code const t{cbor::error::too_little_data}
 * result: std::error_code cbor:1
 * call: t.default_error_condition().category().name()
 * result: "cbor"
 * call: cbor::category().message(13)
 * result: "key not found"
 * call: cbor::category().message(0)
 * result: "unknown cbor error"
 * @endcode
 *
 * @snippet doc_reference_errors.cpp error_category
 * @see cbor::category()
 * @see std::is_error_code_enum<cbor::error>
 *
 * @ingroup errors
 */
class error_category : public std::error_category
{
public:
    /**
     * @brief Gives the name of the category.
     * @return `"cbor"`
     */
    char const *name() const noexcept override
    {
        return "cbor";
    }

    /**
     * @brief Gives the text of an error.
     * @param condition the value of a cbor::error
     * @return the text, or `"unknown cbor error"` for a value that is no cbor::error
     */
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
        case error::sharedref_not_complete:
            return "sharedref not complete";
        case error::sharedref_not_defined:
            return "sharedref not defined";
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
        case error::amplification_limit_breach:
            return "amplification limit breach";
        }
        return "unknown cbor error";
    }

    /**
     * @brief Gives the condition that an error compares equal to.
     * @param condition the value of a cbor::error
     * @return the `std::errc` of the same name for four errors, else a condition of this category
     */
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

/**
 * @brief Gives the one cbor::error_category of cbor::error.
 *
 * Each call gives the same object.
 * Its name is `"cbor"`.
 * A `std::error_code` made from a `cbor::error` holds this category.
 *
 * @code{.callseq}
 * cbor::category()        → std::error_category const &
 * cbor::category().name() → char const *
 * @endcode
 * @code{.example}
 * message: {}
 * call: std::error_code const e{cbor::error::syntax_error}
 * result: std::error_code cbor:2
 * call: e.category().name()
 * result: "cbor"
 * call: cbor::category()
 * result: std::error_category "cbor"
 * @endcode
 *
 * @snippet doc_reference_errors.cpp category
 * @see cbor::error_category
 * @see cbor::make_error_code()
 * @see cbor::error
 *
 * @ingroup errors
 */
inline std::error_category const &category() noexcept
{
    static error_category const instance;
    return instance;
}

/**
 * @brief Makes a `std::error_code` with the value of @p e and the cbor::category() of cbor::error.
 *
 * The constructor of `std::error_code` from a `cbor::error` calls it by argument-dependent lookup.
 * So a program seldom calls it by name.
 *
 * @code{.callseq}
 * cbor::make_error_code(e) → std::error_code
 * @endcode
 * @code{.example}
 * message: {}
 * call: std::error_code const e = cbor::make_error_code(cbor::error::invalid_path)
 * result: std::error_code cbor:14
 * call: e.message()
 * result: "invalid path"
 * call: e.value()
 * result: 14
 * @endcode
 *
 * @snippet doc_reference_errors.cpp make_error_code
 * @see std::is_error_code_enum<cbor::error>
 * @see cbor::category()
 * @see cbor::error
 *
 * @ingroup errors
 */
inline std::error_code make_error_code(error const e) noexcept
{
    return {static_cast<int>(e), category()};
}

} // namespace cbor

/**
 * @brief Marks cbor::error as an error code enum.
 *
 * So a `cbor::error` converts to a `std::error_code` with no cast, through cbor::make_error_code().
 * A `std::error_code` also compares equal to a `cbor::error` with `==`.
 * A `cbor::error` and a `std::errc` do not compare directly: that does not compile.
 * Put a `std::error_code` on the left.
 *
 * @code{.callseq}
 * std::is_error_code_enum_v<cbor::error> → bool
 * std::error_code e{cbor::error::x}      → std::error_code
 * @endcode
 * @code{.example}
 * message: {}
 * call: std::is_error_code_enum_v<cbor::error>
 * result: true
 * call: std::error_code const e{cbor::error::index_out_of_bounds}
 * result: std::error_code cbor:12
 * call: e.message()
 * result: "index outside of array bounds"
 * @endcode
 *
 * @snippet doc_reference_errors.cpp is_error_code_enum
 * @see cbor::make_error_code()
 * @see cbor::error_category
 *
 * @ingroup errors
 */
template <>
struct std::is_error_code_enum<cbor::error> : std::true_type {};
