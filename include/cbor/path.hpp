/**
 * @file
 * Gives cbor::key, a map key for a lookup, and cbor::fixed_string, a
 * path as a template argument. Declares the checks of a valid path and
 * of a singular query.
 * https://www.rfc-editor.org/rfc/rfc9535
 */
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#ifdef __cpp_impl_reflection
#include <meta>
#endif

namespace cbor
{

struct key {
    char const *text = nullptr;
    std::int64_t number = 0;

#ifdef __cpp_impl_reflection
    template <std::same_as<char> C>
    consteval explicit key(C const *const s) : text(std::define_static_string(std::string_view(s)))
    {
    }
#endif

    constexpr explicit key(std::int64_t const n) : number(n)
    {
    }
};

/**
 * @brief The type of a path at compile time.
 *
 * A string literal converts to it, so `at_path<"$.id", int>` names no `fixed_string`.
 * A program names it in its own template that passes a path on.
 * `value` holds the characters and the closing zero.
 * `view()` gives the characters without the zero.
 * The constructor is `consteval`: a `fixed_string` exists only at compile time.
 * It checks no grammar; cbor::is_valid_path_v does.
 * A text that is not a constant expression does not compile.
 *
 * @code{.example}
 * message: {"id": 42}
 * call: cbor::at_path<"$.id", std::int64_t>(bytes).value_or(0)
 * result: 42
 * call: cbor::at_path<"$.age", std::int64_t>(bytes).value_or(0)
 * result: 0
 * @endcode
 *
 * @snippet doc_reference_path.cpp fixed_string
 * @snippet doc_reference_path.cpp fixed_string use
 * @see cbor::at_path()
 * @see cbor::is_valid_path_v
 * @see cbor::is_singular_query_v
 *
 * @ingroup path
 */
template <std::size_t N>
struct fixed_string {
    std::array<char, N> value;

    consteval fixed_string(char const (&text)[N])
    {
        std::ranges::copy(text, value.begin());
    }

    consteval std::string_view view() const
    {
        return std::string_view(std::span(value).first(N - 1));
    }
};

template <fixed_string Path>
class is_valid_path;

/**
 * @brief True when the grammar of RFC 9535, with EDN literals, accepts `Path`.
 *
 * A path nested deeper than the compiler evaluates a constant expression does not compile.
 * `is_valid_path<Path>` is a `std::bool_constant` with the same value.
 * A path with `match` or `search` is not valid here.
 * `lazy.at_path<Path>()` and `lazy.at_path<Path, cbor::nodelist>()` require a valid path.
 *
 * @code{.example}
 * message: {"a": 1}
 * call: cbor::path::compile<cbor::nodelist>("$..a").has_value()
 * result: true
 * call: cbor::path::compile<cbor::nodelist>("$.a[").has_value()
 * result: false
 * call: cbor::path::compile<cbor::nodelist>("$[?match(@.t, 'a.c')]").has_value()
 * result: false
 * @endcode
 *
 * @snippet doc_reference_path.cpp is_valid_path_v
 * @see cbor::is_singular_query_v
 * @see cbor::path::compile()
 *
 * @ingroup path
 */
template <fixed_string Path>
inline constexpr bool is_valid_path_v = is_valid_path<Path>::value;

template <fixed_string Path>
class is_singular_query;

/**
 * @brief True when `Path` is valid and is a singular query (RFC 9535 2.3.5.1).
 *
 * A singular query has a name or an index in each segment, and no descendant segment.
 * It selects one node at most.
 * An EDN key in brackets is a name.
 * `is_singular_query<Path>` is a `std::bool_constant` with the same value.
 * Every form of cbor::at_path() with a path at compile time requires it.
 * A path that is not singular reads only into a cbor::nodelist.
 *
 * @code{.example}
 * message: {"a": [{"b": 1}]}
 * call: cbor::path::compile<cbor::lazy>("$.a[0]['b']").has_value()
 * result: true
 * call: cbor::path::compile<cbor::lazy>("$.a[*]").has_value()
 * result: false
 * call: cbor::path::compile<cbor::lazy>("$..a").has_value()
 * result: false
 * call: cbor::path::compile<cbor::lazy>("$.a[").has_value()
 * result: false
 * @endcode
 *
 * @snippet doc_reference_path.cpp is_singular_query_v
 * @see cbor::is_valid_path_v
 * @see cbor::nodelist
 *
 * @ingroup path
 */
template <fixed_string Path>
inline constexpr bool is_singular_query_v = is_singular_query<Path>::value;

}
