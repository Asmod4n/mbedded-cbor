/**
 * @file
 * Reads values from CBOR with a JSONPath query. Parses a path at
 * compile time or with jsonpath::compile at run time. A key or a
 * literal in a path can be in diagnostic notation.
 * https://www.rfc-editor.org/rfc/rfc9535
 * https://datatracker.ietf.org/doc/html/draft-ietf-cbor-edn-literals-28
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "item_size.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "diagnostic_notation.hpp"
#include "lazy.hpp"
#include "ref.hpp"
#include "rfc9535.hpp"

namespace cbor
{

struct lazy;

/**
 * @brief Reads the value at a JSONPath (RFC 9535) in `encoded`, with no decode of the rest.
 *
 * The first five forms take the path as a string literal, and the compiler checks it.
 * The other forms take a cbor::jsonpath::query that cbor::jsonpath::compile() made at run time.
 * A number, `bool`, `std::nullptr_t` or `simple_value` comes back as a value from a `std::string_view`.
 * A text, a byte string or a typed array comes back as a view in a `ref<T>`, and the `ref` holds the owner of the
 * bytes.
 * `T` `std::string` or `std::vector<std::byte>` copies the value out of the bytes.
 * A `lazy` or a cbor::nodelist comes only from a query.
 * No form copies the input bytes.
 * The `Encoded &&` forms move the bytes into a new owner.
 * A path that is not valid or not singular does not compile.
 *
 * @code{.callseq}
 * cbor::at_path<"$.user.id", std::int64_t>(bytes)       → std::expected<std::int64_t, error>
 * cbor::at_path<"$.user.name", std::string_view>(owner) → std::expected<ref<std::string_view>, error>
 * cbor::at_path(bytes, q)                               → std::expected<T, error>
 * cbor::at_path(owner, q)                               → std::expected<query<T>::result_type, error>
 * @endcode
 * @code{.example}
 * message: {"user": {"id": 42, "name": "ann"}}
 * call: cbor::at_path<"$.user.id", std::int64_t>(bytes)
 * result: 42
 * call: auto const owner = std::make_shared<std::string const>(std::move(bytes))
 * result: shared_ptr to 21 bytes: a1 64 75 73 65 72 a2 62 69 64 18 2a 64 6e 61 6d 65 63 61 6e 6e
 * call: auto const name = cbor::at_path<"$.user.name", std::string_view>(owner)
 * result: ref to "ann"
 * call: **name
 * result: "ann"
 * call: auto const q = cbor::jsonpath::compile<std::int64_t>("$.user.id")
 * result: cbor::jsonpath::query<long long>
 * call: cbor::at_path(*owner, *q)
 * result: 42
 * @endcode
 *
 * @retval key_not_found, index_out_of_bounds, not_indexable a step finds no node
 * @retval incorrect_type, number_out_of_range the node does not read as `T`
 * @retval too_little_data, syntax_error, indefinite_length the bytes up to the node are broken
 * @retval string_length_exceeded, container_elements_exceeded a limit of cbor::limits is set and the item goes past
 * it
 * @throws std::logic_error a null or empty owner, or a moved-from query
 * @snippet doc_reference_path.cpp at_path
 * @see cbor::jsonpath::compile()
 * @see cbor::nodelist
 * @see cbor::lazy::at_path()
 *
 * @ingroup path
 */
template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
              std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
              || std::is_same_v<T, std::float128_t>
#endif
              ))
std::expected<T, error> at_path(std::string_view encoded);

/**
 * @brief Reads a view at a path in bytes that `owner` keeps alive.
 * @ingroup path
 */
template <fixed_string Path, class T, borrowed_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded);

/**
 * @brief Reads a view at a path in the bytes of `*owner`.
 * @ingroup path
 */
template <fixed_string Path, class T, owning_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(std::shared_ptr<Encoded const> owner);

/**
 * @brief Reads a view at a path, and moves the bytes into a new owner.
 * @ingroup path
 */
template <fixed_string Path, class T, owning_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(Encoded &&encoded);

/**
 * @brief Copies the text or the bytes at a path out of `encoded`.
 * @ingroup path
 */
template <fixed_string Path, copied_value T>
    requires is_singular_query_v<Path>
std::expected<T, error> at_path(std::basic_string_view<char> encoded);

/** @ingroup path */
class jsonpath
{
    struct selector {
        enum class kind { key, index, wildcard, slice, filter } kind;
        std::size_t key_at;
        std::size_t key_size;
        std::int64_t index;
        std::optional<std::int64_t> start;
        std::optional<std::int64_t> end;
        std::int64_t step;
        std::size_t expression;
    };

    struct segment {
        bool descendant;
        std::size_t selector_at;
        std::size_t selector_count;
    };

    enum class comparison_op { equal, not_equal, less, less_equal, greater, greater_equal };

    struct logical_expr {
        enum class kind {
            logical_or,
            logical_and,
            logical_not,
            comparison,
            filter_query,
            literal,
            length,
            count,
            value
        } kind;
        std::size_t first;
        std::size_t second;
        comparison_op op;
        bool relative;
        bool singular;
    };

    struct parsed_query {
        std::size_t at;
        std::size_t segment_at;
        std::size_t segment_count;
        bool singular;
    };

    struct parsed_expression {
        std::size_t at;
        std::size_t index;
    };

    struct integer {
        std::int64_t value;
        std::size_t at;
    };

    struct query_view {
        std::span<segment const> segments;
        std::span<selector const> selectors;
        std::span<logical_expr const> expressions;
        std::string_view keys;
        std::span<std::expected<std::vector<lazy>, error> const> absolute_nodelists;
    };

    static constexpr bool name_first(char const c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
    }

    static constexpr bool function_name_char(char const c)
    {
        return (c >= 'a' && c <= 'z') || c == '_' || extended_diagnostic_notation::digit(c);
    }

    static constexpr std::expected<std::optional<integer>, error> read_int(std::string_view const text, std::size_t const at)
    {
        if (at >= text.size())
            return std::nullopt;
        bool const negative = text[at] == '-';
        std::size_t const digits_at = at + (negative ? 1 : 0);
        std::size_t digits_end = digits_at;
        while (digits_end < text.size() && extended_diagnostic_notation::digit(text[digits_end]))
            ++digits_end;
        constexpr std::size_t exact_integer_digits = 16;
        if (digits_end == digits_at || digits_end - digits_at > exact_integer_digits ||
            (text[digits_at] == '0' && (digits_end > digits_at + 1 || negative)))
            return std::nullopt;
        std::int64_t value = 0;
        for (char const d : std::span(text).subspan(digits_at, digits_end - digits_at))
            value = value * extended_diagnostic_notation::decimal + (d - '0');
        if (value > rfc9535::exact_integer_max) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return integer{negative ? -value : value, digits_end};
    }

    struct syntax_tree {
        std::vector<segment> segments;
        std::vector<selector> selectors;
        std::vector<logical_expr> expressions;
        std::string keys;
        parsed_query top;

        constexpr std::expected<std::size_t, error> parse_string(std::string_view const text, std::size_t const at)
        {
            std::string name;
            auto const next = extended_diagnostic_notation::parse_quoted(text, at, name);
            if (!next) [[unlikely]]
                return next;
            for (char const c : std::span(text).subspan(at + 1, *next - at - 2))
                if (static_cast<unsigned char>(c) < extended_diagnostic_notation::unescaped_first) [[unlikely]]
                    return std::unexpected(error::invalid_path);
            if (auto const r = extended_diagnostic_notation::append_head(keys, major_type::text_string, name.size(), extended_diagnostic_notation::no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            keys += name;
            return next;
        }

        constexpr std::expected<std::size_t, error> parse_literal(std::string_view const text, std::size_t const at)
        {
            std::string literal;
            auto const next = extended_diagnostic_notation::parse_literal({text, at}, literal);
            if (!next) [[unlikely]]
                return next;
            if (auto const r = extended_diagnostic_notation::append_canonical(
                    keys, literal, detail::validity::offset{});
                !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }

        constexpr std::expected<std::pair<std::size_t, selector>, error> parse_selector(std::string_view const text, std::size_t const at)
        {
            selector s{selector::kind::key, keys.size(), 0, 0, std::nullopt, std::nullopt, 1, 0};
            char const c = text[at];
            if (c == '*') {
                s.kind = selector::kind::wildcard;
                return std::pair{at + 1, s};
            }
            if (c == '\'' || c == '"') {
                auto const next = parse_string(text, at);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                s.key_size = keys.size() - s.key_at;
                return std::pair{*next, s};
            }
            if (c == '?') {
                auto const e = parse_logical_or(text, extended_diagnostic_notation::blank_end(text, at + 1));
                if (!e) [[unlikely]]
                    return std::unexpected(e.error());
                s.kind = selector::kind::filter;
                s.expression = e->index;
                return std::pair{e->at, s};
            }
            auto const first = read_int(text, at);
            if (!first) [[unlikely]]
                return std::unexpected(first.error());
            std::size_t next = *first ? extended_diagnostic_notation::blank_end(text, (*first)->at) : at;
            if (next < text.size() && text[next] == ':') {
                s.kind = selector::kind::slice;
                if (*first)
                    s.start = (*first)->value;
                next = extended_diagnostic_notation::blank_end(text, next + 1);
                auto const end = read_int(text, next);
                if (!end) [[unlikely]]
                    return std::unexpected(end.error());
                if (*end) {
                    s.end = (*end)->value;
                    next = extended_diagnostic_notation::blank_end(text, (*end)->at);
                }
                if (next < text.size() && text[next] == ':') {
                    next = extended_diagnostic_notation::blank_end(text, next + 1);
                    auto const step = read_int(text, next);
                    if (!step) [[unlikely]]
                        return std::unexpected(step.error());
                    if (*step) {
                        s.step = (*step)->value;
                        next = (*step)->at;
                    }
                }
                return std::pair{next, s};
            }
            if (*first && next < text.size() && (text[next] == ']' || text[next] == ',')) {
                s.kind = selector::kind::index;
                s.index = (*first)->value;
                return std::pair{(*first)->at, s};
            }
            auto const literal_next = parse_literal(text, at);
            if (!literal_next) [[unlikely]]
                return std::unexpected(literal_next.error());
            s.key_size = keys.size() - s.key_at;
            return std::pair{*literal_next, s};
        }

        constexpr std::expected<std::pair<std::size_t, std::vector<selector>>, error> parse_bracketed(std::string_view const text, std::size_t at)
        {
            std::vector<selector> chosen;
            for (;;) {
                at = extended_diagnostic_notation::blank_end(text, at + 1);
                if (auto const left = detail::validity::check_text_left(text, at, 1); !left) [[unlikely]]
                    return std::unexpected(left.error());
                auto const s = parse_selector(text, at);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                chosen.push_back(s->second);
                at = extended_diagnostic_notation::blank_end(text, s->first);
                if (!detail::validity::check_text_left(text, at, 1) || (text[at] != ',' && text[at] != ']'))
                    [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (text[at] == ']')
                    return std::pair{at + 1, std::move(chosen)};
            }
        }

        constexpr std::expected<parsed_query, error> parse_segments(std::string_view const text, std::size_t at)
        {
            if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
                return std::unexpected(r.error());
            std::vector<segment> found;
            std::vector<selector> found_selectors;
            bool singular = true;
            for (;;) {
                std::size_t const next = extended_diagnostic_notation::blank_end(text, at);
                if (!detail::validity::check_text_left(text, next, 1) ||
                    (text[next] != '.' && text[next] != '['))
                    break;
                at = next;
                segment s{false, found_selectors.size(), 0};
                std::vector<selector> chosen;
                if (text[at] == '[' || std::string_view{std::span(text).subspan(at)}.starts_with(std::string_view("..["))) {
                    s.descendant = text[at] == '.';
                    auto const b =
                        parse_bracketed(text, at + (s.descendant ? std::string_view("..").size() : 0));
                    if (!b) [[unlikely]]
                        return std::unexpected(b.error());
                    chosen = b->second;
                    at = b->first;
                } else {
                    ++at;
                    if (at < text.size() && text[at] == '.') {
                        s.descendant = true;
                        ++at;
                    }
                    if (at < text.size() && text[at] == '*') {
                        chosen.push_back({selector::kind::wildcard, 0, 0, 0, std::nullopt, std::nullopt, 1, 0});
                        ++at;
                    } else {
                        std::size_t end = at;
                        while (end < text.size() && (name_first(text[end]) || (end != at && extended_diagnostic_notation::digit(text[end]))))
                            ++end;
                        if (end == at) [[unlikely]]
                            return std::unexpected(error::invalid_path);
                        std::size_t const key_at = keys.size();
                        if (auto const r = extended_diagnostic_notation::append_head(keys, major_type::text_string, end - at, extended_diagnostic_notation::no_indicator); !r) [[unlikely]]
                            return std::unexpected(r.error());
                        std::ranges::copy(std::span(text).subspan(at, end - at), std::back_inserter(keys));
                        chosen.push_back({selector::kind::key, key_at, keys.size() - key_at, 0, std::nullopt, std::nullopt, 1, 0});
                        at = end;
                    }
                }
                singular = singular && !s.descendant && chosen.size() == 1 &&
                           (chosen.front().kind == selector::kind::key || chosen.front().kind == selector::kind::index);
                s.selector_count = chosen.size();
                found_selectors.append_range(chosen);
                found.push_back(s);
            }
            for (segment &s : found)
                s.selector_at += selectors.size();
            selectors.append_range(found_selectors);
            parsed_query const p{at, segments.size(), found.size(), singular};
            segments.append_range(found);
            return p;
        }

        constexpr bool comparable(std::size_t const index) const
        {
            logical_expr const &e = expressions[index];
            return e.kind == logical_expr::kind::literal || e.kind == logical_expr::kind::length ||
                   e.kind == logical_expr::kind::count || e.kind == logical_expr::kind::value ||
                   (e.kind == logical_expr::kind::filter_query && e.singular);
        }

        constexpr std::size_t add_expression(logical_expr const e)
        {
            expressions.push_back(e);
            return expressions.size() - 1;
        }

        constexpr std::expected<parsed_expression, error> parse_function(std::string_view const text, std::size_t const at,
                                                                         std::string_view const name)
        {
            std::vector<std::size_t> arguments;
            std::size_t next = extended_diagnostic_notation::blank_end(text, at + name.size() + 1);
            if (next < text.size() && text[next] == ')')
                ++next;
            else
                for (;;) {
                    auto const a = parse_primary(text, next);
                    if (!a) [[unlikely]]
                        return a;
                    arguments.push_back(a->index);
                    next = extended_diagnostic_notation::blank_end(text, a->at);
                    if (!detail::validity::check_text_left(text, next, 1) ||
                        (text[next] != ',' && text[next] != ')')) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    if (text[next++] == ')')
                        break;
                    next = extended_diagnostic_notation::blank_end(text, next);
                }
            if (arguments.size() != 1) [[unlikely]]
                return std::unexpected(error::invalid_path);
            std::size_t const argument = arguments.front();
            auto const function = name == "length" ? logical_expr::kind::length
                                          : name == "count" ? logical_expr::kind::count
                                                            : logical_expr::kind::value;
            bool const nodes = expressions[argument].kind == logical_expr::kind::filter_query;
            if (function == logical_expr::kind::length ? !comparable(argument) : !nodes) [[unlikely]]
                return std::unexpected(error::invalid_path);
            return parsed_expression{next, add_expression({function, argument, 0, comparison_op::equal, false, false})};
        }

        constexpr std::expected<parsed_expression, error> parse_primary(std::string_view const text, std::size_t const at)
        {
            if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const left = detail::validity::check_text_left(text, at, 1); !left) [[unlikely]]
                return std::unexpected(left.error());
            char const c = text[at];
            if (c == '@' || c == '$') {
                auto const q = parse_segments(text, at + 1);
                if (!q) [[unlikely]]
                    return std::unexpected(q.error());
                return parsed_expression{
                    q->at, add_expression({logical_expr::kind::filter_query, q->segment_at, q->segment_count,
                                           comparison_op::equal, c == '@', q->singular})};
            }
            std::size_t const key_at = keys.size();
            auto const literal = [&](std::size_t const next) {
                return parsed_expression{next, add_expression({logical_expr::kind::literal, key_at, keys.size() - key_at,
                                                               comparison_op::equal, false, false})};
            };
            if (c >= 'a' && c <= 'z') {
                std::size_t end = at;
                while (end < text.size() && function_name_char(text[end]))
                    ++end;
                std::string_view const name{std::span(text).subspan(at, end - at)};
                bool const call = end < text.size() && text[end] == '(';
                if (call && (name == "length" || name == "count" || name == "value"))
                    return parse_function(text, at, name);
                if (call && (name == "match" || name == "search")) [[unlikely]]
                    return std::unexpected(error::invalid_path);
            }
            if (c == '\'' || c == '"') {
                auto const next = parse_string(text, at);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                return literal(*next);
            }
            auto const next = parse_literal(text, at);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            return literal(*next);
        }

        static constexpr std::optional<std::pair<comparison_op, std::size_t>> read_comparison_op(std::string_view const text, std::size_t const at)
        {
            constexpr auto ops = std::to_array<std::pair<std::string_view, comparison_op>>(
                {{"==", comparison_op::equal},
                 {"!=", comparison_op::not_equal},
                 {"<=", comparison_op::less_equal},
                 {">=", comparison_op::greater_equal},
                 {"<", comparison_op::less},
                 {">", comparison_op::greater}});
            for (auto const &[token, op] : ops)
                if (std::string_view{std::span(text).subspan(at)}.starts_with(token))
                    return std::pair{op, token.size()};
            return std::nullopt;
        }

        constexpr std::expected<parsed_expression, error> parse_paren(std::string_view const text, std::size_t const at)
        {
            auto const inner = parse_logical_or(text, extended_diagnostic_notation::blank_end(text, at + 1));
            if (!inner) [[unlikely]]
                return inner;
            std::size_t const close = extended_diagnostic_notation::blank_end(text, inner->at);
            if (!detail::validity::check_text_left(text, close, 1) || text[close] != ')') [[unlikely]]
                return std::unexpected(error::invalid_path);
            return parsed_expression{close + 1, inner->index};
        }

        constexpr std::expected<parsed_expression, error> parse_basic(std::string_view const text, std::size_t const at)
        {
            if (auto const left = detail::validity::check_text_left(text, at, 1); !left) [[unlikely]]
                return std::unexpected(left.error());
            if (text[at] == '!') {
                std::size_t const next = extended_diagnostic_notation::blank_end(text, at + 1);
                bool const paren = next < text.size() && text[next] == '(';
                auto const operand = paren ? parse_paren(text, next) : parse_primary(text, next);
                if (!operand) [[unlikely]]
                    return operand;
                if (!paren && expressions[operand->index].kind != logical_expr::kind::filter_query)
                    [[unlikely]]
                    return std::unexpected(error::invalid_path);
                return parsed_expression{operand->at, add_expression({logical_expr::kind::logical_not, operand->index, 0,
                                                                      comparison_op::equal, false, false})};
            }
            if (text[at] == '(')
                return parse_paren(text, at);
            auto const left = parse_primary(text, at);
            if (!left) [[unlikely]]
                return left;
            std::size_t const next = extended_diagnostic_notation::blank_end(text, left->at);
            auto const op = read_comparison_op(text, next);
            if (!op) {
                if (expressions[left->index].kind != logical_expr::kind::filter_query) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                return left;
            }
            auto const right = parse_primary(text, extended_diagnostic_notation::blank_end(text, next + op->second));
            if (!right) [[unlikely]]
                return right;
            if (!comparable(left->index) || !comparable(right->index)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            return parsed_expression{right->at, add_expression({logical_expr::kind::comparison, left->index, right->index,
                                                                op->first, false, false})};
        }

        constexpr std::expected<parsed_expression, error> parse_logical_and(std::string_view const text, std::size_t const at)
        {
            auto const first = parse_basic(text, at);
            if (!first) [[unlikely]]
                return first;
            parsed_expression chain = *first;
            std::optional<std::size_t> last;
            for (;;) {
                std::size_t const next = extended_diagnostic_notation::blank_end(text, chain.at);
                if (!std::string_view{std::span(text).subspan(next)}.starts_with(std::string_view("&&")))
                    return chain;
                auto const right = parse_basic(
                    text, extended_diagnostic_notation::blank_end(text, next + std::string_view("&&").size()));
                if (!right) [[unlikely]]
                    return right;
                std::size_t const left = last ? expressions[*last].second : chain.index;
                std::size_t const node = add_expression({logical_expr::kind::logical_and, left, right->index,
                                                         comparison_op::equal, false, false});
                if (last)
                    expressions[*last].second = node;
                else
                    chain.index = node;
                chain.at = right->at;
                last = node;
            }
        }

        constexpr std::expected<parsed_expression, error> parse_logical_or(std::string_view const text, std::size_t const at)
        {
            if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const first = parse_logical_and(text, at);
            if (!first) [[unlikely]]
                return first;
            parsed_expression chain = *first;
            std::optional<std::size_t> last;
            for (;;) {
                std::size_t const next = extended_diagnostic_notation::blank_end(text, chain.at);
                if (!std::string_view{std::span(text).subspan(next)}.starts_with(std::string_view("||")))
                    return chain;
                auto const right = parse_logical_and(
                    text, extended_diagnostic_notation::blank_end(text, next + std::string_view("||").size()));
                if (!right) [[unlikely]]
                    return right;
                std::size_t const left = last ? expressions[*last].second : chain.index;
                std::size_t const node = add_expression({logical_expr::kind::logical_or, left, right->index,
                                                         comparison_op::equal, false, false});
                if (last)
                    expressions[*last].second = node;
                else
                    chain.index = node;
                chain.at = right->at;
                last = node;
            }
        }
    };

    static constexpr std::expected<syntax_tree, error> parse_query(std::string_view const text)
    {
        syntax_tree q{{}, {}, {}, {}, {}};
        if (text.empty() || text.front() != '$') [[unlikely]]
            return std::unexpected(error::invalid_path);
        auto const top = q.parse_segments(text, 1);
        if (!top) [[unlikely]]
            return std::unexpected(top.error());
        if (top->at != text.size()) [[unlikely]]
            return std::unexpected(error::invalid_path);
        q.top = *top;
        return q;
    }

    static std::expected<lazy, error> find_key(lazy const &node, std::string_view key);

    static std::expected<lazy, error> select_index(lazy const &node, std::int64_t index);

    struct exact_number {
        bool nan;
        bool integral;
        bool negative;
        std::uint64_t magnitude;
        double value;
    };

    static constexpr double two_to_the_64 = 18446744073709551616.0;

    static exact_number number_of(heads::head const &n);

    static std::expected<std::pair<heads::head, heads::decoder<true>>, error>
    integer_of(heads::head const &n, heads::decoder<true> const content);

    static std::partial_ordering compare_numbers(exact_number const &a, exact_number const &b);

    static std::expected<std::pair<std::uint64_t, detail::validity::offset>, error>
    intern_value(detail::canonical_ids &ids, lazy::top_level_item const &source, detail::validity::offset at);

    static std::expected<bool, error> value_equal(lazy const &a, lazy const &b);

    static std::expected<bool, error> value_less(lazy const &a, lazy const &b);

    static std::expected<std::optional<lazy>, error> comparable_value(query_view const v, std::size_t index,
                                                                      lazy const &current, lazy const &root);

    static std::expected<bool, error> test_expression(query_view const v, std::size_t index,
                                                      lazy const &current, lazy const &root);

    static std::expected<void, error> apply_selector(query_view const v, selector const &s, lazy const &node,
                                                     lazy const &root, std::vector<lazy> &nodes);

    static std::expected<void, error> apply_segment(query_view const v, segment const &s, lazy const &node,
                                                    lazy const &root, std::vector<lazy> &nodes);

    static std::expected<std::vector<lazy>, error> apply_segments(query_view const v, std::size_t segment_at,
                                                                  std::size_t segment_count,
                                                                  lazy const &start, lazy const &root);

    static std::expected<lazy, error> walk_singular_query(query_view const v, parsed_query const &top,
                                                          lazy const &root);

    static std::expected<std::vector<lazy>, error> walk_query(query_view const v, parsed_query const &top,
                                                              lazy const &root);

    template <fixed_string Path, class Walk>
    static auto apply_compiled(Walk const &walk);

    template <class T, bool Checked>
    static std::optional<std::expected<T, error>> try_walk_bytes(query_view const v, parsed_query const &top,
                                                                 std::string_view const encoded)
    {
        heads::decoder<Checked> d{encoded};
        heads::head h{};
        std::size_t step = 0;
        auto const typed_array_elements =
            [&d](std::uint64_t const tag) -> std::optional<std::expected<std::string_view, error>> {
            auto const r = d.decode_head();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (error const c =
                    detail::validity::check_typed_array_content(tag, r->major, r->argument).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = d.decode_byte_string(r->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            return *bytes;
        };
        for (;;) {
            for (;;) {
                for (;;) {
                    heads::decoder const before = d;
                    auto const t = d.decode_head();
                    if (!t) [[unlikely]]
                        return std::unexpected(t.error());
                    if (t->major != major_type::tag || !heads::is_tag_passed(t->argument)) {
                        d = before;
                        break;
                    }
                }
                auto const c = d.decode_head();
                if (!c) [[unlikely]]
                    return std::unexpected(c.error());
                h = *c;
                if (h.major != major_type::tag ||
                    h.argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
                    break;
                auto const r = d.decode_head();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (!detail::validity::check_tag_content(h.argument, r->major).has_value()) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                auto const embedded = d.decode_byte_string(r->argument);
                if (!embedded) [[unlikely]]
                    return std::unexpected(embedded.error());
                d = heads::decoder<Checked>{*embedded};
            }
            if (auto const r =
                    detail::validity::check_pending_items(h.major, h.info, h.argument, d.encoded.size());
                !r) [[unlikely]]
                return std::unexpected(r.error());
            if (step == top.segment_count)
                break;
            selector const &each = v.selectors[v.segments[top.segment_at + step].selector_at];
            ++step;
            if (each.kind == selector::kind::index && h.major == major_type::array) {
                auto const position = detail::validity::check_index(each.index, h.argument);
                if (!position) [[unlikely]]
                    return std::unexpected(position.error());
                if (auto const r = well_formedness::skip_items(d, std::to_underlying(*position)); !r) [[unlikely]]
                    return std::unexpected(r.error());
                continue;
            }
            if (each.kind == selector::kind::index && heads::is_typed_array(h)) {
#ifdef __STDCPP_FLOAT128_T__
                if constexpr (std::is_same_v<T, std::float128_t>)
                    return std::nullopt;
#endif
                auto const elements = typed_array_elements(h.argument);
                if (!elements) [[unlikely]]
                    return std::nullopt;
                if (!*elements) [[unlikely]]
                    return std::unexpected(elements->error());
                auto const position = detail::validity::check_index(
                    each.index, (*elements)->size() / detail::validity::typed_array_element_size(h.argument));
                if (!position) [[unlikely]]
                    return std::unexpected(position.error());
                if constexpr (std::is_same_v<T, std::span<std::byte const>>)
                    if (step == top.segment_count)
                        return std::as_bytes(detail::validity::element_span(
                            **elements, *position, detail::validity::typed_array_element_size(h.argument)));
                auto const element = heads::decode_typed_array_element(h.argument, **elements, *position);
                if (!element) [[unlikely]]
                    return std::unexpected(element.error());
                if (step != top.segment_count) [[unlikely]]
                    return std::unexpected(error::not_indexable);
                h = *element;
                break;
            }
            if (heads::is_typed_array(h)) [[unlikely]]
                if (auto const elements = typed_array_elements(h.argument); elements && !*elements) [[unlikely]]
                    return std::unexpected(elements->error());
            if (h.major != major_type::map) [[unlikely]]
                return std::unexpected(error::not_indexable);
            std::string_view const key =
                each.kind == selector::kind::key ? std::string_view(std::span(v.keys).subspan(each.key_at, each.key_size)) : std::string_view{};
            heads::decoder<false> named{key};
            if (each.kind == selector::kind::key) {
                auto const n = named.decode_head();
                if (!n) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (n->major != major_type::text_string) [[unlikely]]
                    return std::nullopt;
            }
            bool found = false;
            for (std::uint64_t i = 0; i < h.argument && !found; ++i) {
                heads::decoder probe = d;
                probe.encoded = detail::validity::subspan(
                    probe.encoded,
                    heads::self_described_cbor_content_at(probe.encoded, detail::validity::offset{}));
                auto const k = probe.decode_head();
                if (!k) [[unlikely]]
                    return std::unexpected(k.error());
                if (each.kind == selector::kind::key) {
                    if (k->major == major_type::text_string) {
                        auto const content = probe.decode_byte_string(k->argument);
                        if (!content) [[unlikely]]
                            return std::unexpected(content.error());
                        found = *content == named.encoded;
                    }
                } else {
                    found = (k->major == major_type::unsigned_integer && each.index >= 0 &&
                             k->argument == static_cast<std::uint64_t>(each.index)) ||
                            (k->major == major_type::negative_integer && each.index < 0 &&
                             k->argument == static_cast<std::uint64_t>(-1 - each.index));
                    if (heads::is_bignum(k->major, k->argument)) [[unlikely]] {
                        heads::decoder content = probe;
                        auto const read = content.decode_bignum(k->argument);
                        if (!read) [[unlikely]]
                            return std::nullopt;
                        bool const negative = k->argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
                        found = *read && negative == (each.index < 0) &&
                                **read == (each.index < 0 ? static_cast<std::uint64_t>(-1 - each.index)
                                                          : static_cast<std::uint64_t>(each.index));
                    }
                }
                if (auto const r = well_formedness::skip_items(d, 1); !r) [[unlikely]]
                    return std::unexpected(r.error());
                if (!found)
                    if (auto const r = well_formedness::skip_items(d, 1); !r) [[unlikely]]
                        return std::unexpected(r.error());
            }
            if (!found) [[unlikely]]
                return std::unexpected(error::key_not_found);
        }
        if constexpr (std::integral<T> && !std::is_same_v<T, bool>) {
            bool negative = h.major == major_type::negative_integer;
            std::uint64_t argument = h.argument;
            if (heads::is_bignum(h.major, h.argument)) {
                negative = h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
                auto const read = d.decode_bignum(h.argument);
                if (!read) [[unlikely]]
                    return std::unexpected(read.error());
                if (!*read) [[unlikely]]
                    return std::unexpected(error::number_out_of_range);
                argument = **read;
            } else if (h.major != major_type::unsigned_integer && !negative) [[unlikely]] {
                return std::unexpected(error::incorrect_type);
            }
            if (error const c = detail::validity::check_number_range<T>(negative, argument).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            T const magnitude = static_cast<T>(argument);
            return static_cast<T>(negative ? ~magnitude : magnitude);
        } else if constexpr (std::is_same_v<T, double>) {
            if (heads::is_bignum(h.major, h.argument)) [[unlikely]]
                return std::nullopt;
            if (h.major == major_type::unsigned_integer || h.major == major_type::negative_integer) {
                bool const negative = h.major == major_type::negative_integer;
                if (error const c =
                        detail::validity::check_number_range<double>(negative, h.argument).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                if (!negative)
                    return static_cast<double>(h.argument);
                return -static_cast<double>(
                    h.argument == std::numeric_limits<std::uint64_t>::max() ? h.argument : h.argument + 1);
            }
            if (h.major != major_type::simple_float) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            switch (static_cast<rfc8949::simple_float_information>(h.info)) {
            case rfc8949::simple_float_information::half_precision_float:
            case rfc8949::simple_float_information::single_precision_float:
            case rfc8949::simple_float_information::double_precision_float:
                return heads::decode_float(h.info, h.argument);
            [[unlikely]] default:
                return std::unexpected(error::incorrect_type);
            }
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!heads::is_boolean(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return h.info == std::to_underlying(simple_value::true_value);
        } else if constexpr (std::is_same_v<T, simple_value>) {
            if (!heads::is_simple_value(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (error const c = detail::validity::check_simple_value(h.info, h.argument).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            return static_cast<simple_value>(h.argument);
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            if (!heads::is_null(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return nullptr;
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            if (h.major != major_type::text_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const text = d.decode_byte_string(h.argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
            return *text;
        } else if constexpr (std::is_same_v<T, typed_array>) {
            if (h.major != major_type::tag) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (error const r = detail::validity::check_typed_array(h.argument, 0).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            auto const bytes = typed_array_elements(h.argument);
            if (!bytes) [[unlikely]]
                return std::nullopt;
            if (!*bytes) [[unlikely]]
                return std::unexpected(bytes->error());
            return typed_array{h.argument, std::as_bytes(std::span(**bytes))};
#ifdef __STDCPP_FLOAT128_T__
        } else if constexpr (std::is_same_v<T, std::float128_t>) {
            return std::nullopt;
#endif
        } else {
            if (h.major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const bytes = d.decode_byte_string(h.argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            return std::as_bytes(std::span(*bytes));
        }
    }

    template <class T>
    static std::expected<T, error> walk_query(query_view const v, parsed_query const &top, lazy const &root)
    {
        lazy node = root;
        for (segment const &s : v.segments.subspan(top.segment_at, top.segment_count)) {
            selector const &each = v.selectors[s.selector_at];
            auto const child =
                each.kind == selector::kind::index
                    ? select_index(node, each.index)
                    : find_key(node, std::string_view(std::span(v.keys).subspan(each.key_at, each.key_size)));
            if (!child) [[unlikely]]
                return std::unexpected(child.error());
            node = *child;
        }
        auto const value = node.template get<T>();
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                      std::is_same_v<T, typed_array>)
            return **value;
        else
            return *value;
    }

    template <class T>
    static std::expected<T, error> walk_bytes(query_view const v, parsed_query const &top, std::string_view const encoded)
    {
        std::string_view const content = heads::self_described_cbor_content(encoded);
        auto const walked = try_walk_bytes<T, true>(v, top, content);
        if (walked) [[likely]]
            return *walked;
        return walk_query<T>(v, top,
                             lazy{lazy::top_level_item{std::shared_ptr<void const>{}, content},
                                  0,
                                  {}});
    }

    template <class T>
    static std::expected<T, error> walk_lazy(query_view const v, parsed_query const &top, lazy const &root)
    {
        detail::validity::throw_logic_error_if(root.top_level.encoded.data() == nullptr,
                                               "cbor::lazy::at_path: the lazy holds no top-level item");
        if (root.typed_array_tag != 0) [[unlikely]]
            return walk_query<T>(v, top, root);
        auto const walked =
            try_walk_bytes<T, true>(v, top, detail::validity::subspan(root.top_level.encoded, detail::validity::offset{root.offset}));
        if (walked) [[likely]]
            return *walked;
        return walk_query<T>(v, top, root);
    }

    template <fixed_string Path>
    static constexpr auto parse_literal()
    {
        return *parse_query(Path.view());
    }

    template <fixed_string Path, class T>
    static std::expected<T, error> walk_query(std::string_view const encoded)
    {
        constexpr std::size_t segments = parse_literal<Path>().segments.size();
        constexpr std::size_t selectors = parse_literal<Path>().selectors.size();
        constexpr std::size_t keys = parse_literal<Path>().keys.size();
        constexpr auto top = parse_literal<Path>().top;
        constexpr auto compiled = [] {
            auto const parsed = parse_literal<Path>();
            std::tuple<std::array<segment, segments>, std::array<selector, selectors>, std::array<char, keys>> c{};
            std::ranges::copy(parsed.segments, std::get<0>(c).begin());
            std::ranges::copy(parsed.selectors, std::get<1>(c).begin());
            std::ranges::copy(parsed.keys, std::get<2>(c).begin());
            return c;
        }();
        query_view const v{std::get<0>(compiled), std::get<1>(compiled), {}, std::string_view(std::get<2>(compiled)), {}};
        return walk_bytes<T>(v, top, encoded);
    }

    template <fixed_string Path, class T>
    static std::expected<T, error> walk_query(lazy const &root)
    {
        constexpr std::size_t segments = parse_literal<Path>().segments.size();
        constexpr std::size_t selectors = parse_literal<Path>().selectors.size();
        constexpr std::size_t keys = parse_literal<Path>().keys.size();
        constexpr auto top = parse_literal<Path>().top;
        constexpr auto compiled = [] {
            auto const parsed = parse_literal<Path>();
            std::tuple<std::array<segment, segments>, std::array<selector, selectors>, std::array<char, keys>> c{};
            std::ranges::copy(parsed.segments, std::get<0>(c).begin());
            std::ranges::copy(parsed.selectors, std::get<1>(c).begin());
            std::ranges::copy(parsed.keys, std::get<2>(c).begin());
            return c;
        }();
        query_view const v{std::get<0>(compiled), std::get<1>(compiled), {}, std::string_view(std::get<2>(compiled)), {}};
        return walk_lazy<T>(v, top, root);
    }
    template <fixed_string Path, class T>
    static std::expected<ref<T>, error> walk_owned_query(lazy const &root)
    {
        auto const r = walk_query<Path, T>(root);
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        return ref<T>(root.top_level.owner, *r);
    }

    template <fixed_string Path, class T>
    static std::expected<ref<T>, error> walk_query(std::shared_ptr<void const> owner,
                                                          std::string_view const encoded)
    {
        auto const r = walk_query<Path, T>(encoded);
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        return ref<T>(std::move(owner), *r);
    }

    /// @cond
    template <fixed_string Path, copied_value T>
        requires is_singular_query_v<Path>
    friend std::expected<T, error> at_path(std::string_view encoded);

    template <fixed_string>
    friend class is_singular_query;

    template <fixed_string Path, class T>
        requires(is_singular_query_v<Path> &&
                 ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
                  std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
              || std::is_same_v<T, std::float128_t>
#endif
              ))
    friend std::expected<T, error> at_path(std::string_view encoded);

    template <fixed_string Path, class T, borrowed_range_of_chars Encoded>
        requires(is_singular_query_v<Path> &&
                 (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                  std::is_same_v<T, typed_array>))
    friend std::expected<ref<T>, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded);

    template <fixed_string>
    friend class is_valid_path;

    friend struct lazy;
    /// @endcond

public:
    /**
     * @brief A parsed run-time path with the result type `T`.
     *
     * Only cbor::jsonpath::compile() makes one.
     * The query holds its own copy of the path data, and no view into the text of the path.
     * One query reads any number of items.
     * cbor::at_path() and cbor::lazy::at_path() apply it.
     * `result_type` is `ref<T>` for a view type `T`, and `T` otherwise.
     * A moved-from query throws `std::logic_error` when it is applied.
     *
     * @code{.callseq}
     * cbor::jsonpath::compile<std::int64_t>("$.id").value() → query<std::int64_t>
     * cbor::at_path(bytes, q)                               → std::expected<std::int64_t, error>
     * @endcode
     * @code{.example}
     * message: {"id": 1}
     * call: auto const q = cbor::jsonpath::compile<std::int64_t>("$.id").value()
     * result: cbor::jsonpath::query<long long>
     * call: cbor::at_path(bytes, q)
     * result: 1
     * @endcode
     * @code{.example}
     * message: {"id": 2, "x": 0}
     * call: auto const q = cbor::jsonpath::compile<std::int64_t>("$.id").value()
     * result: cbor::jsonpath::query<long long>
     * call: cbor::at_path(bytes, q)
     * result: 2
     * @endcode
     *
     * @snippet doc_reference_path.cpp query
     * @see cbor::jsonpath::compile()
     * @see cbor::at_path()
     *
     * @ingroup path
     */
    template <class T>
        requires path_result<T>
    class query
    {
        std::vector<segment> segments;
        std::vector<selector> selectors;
        std::vector<logical_expr> filters;
        std::string keys;
        parsed_query top{};

        query() = default;

    public:
        using value_type = T;
        using result_type = std::conditional_t<view_result<T>, ref<T>, T>;

        query(query const &) = default;

        query &operator=(query const &) = default;

        query(query &&other) noexcept
            : segments(std::move(other.segments)), selectors(std::move(other.selectors)),
              filters(std::move(other.filters)), keys(std::move(other.keys)),
              top(std::exchange(other.top, parsed_query{}))
        {
        }

        query &operator=(query &&other) noexcept
        {
            segments = std::move(other.segments);
            selectors = std::move(other.selectors);
            filters = std::move(other.filters);
            keys = std::move(other.keys);
            top = std::exchange(other.top, parsed_query{});
            return *this;
        }

        ~query() = default;

        friend class jsonpath;
    };

    /**
     * @brief Parses a JSONPath (RFC 9535) at run time and gives a cbor::jsonpath::query.
     *
     * The query owns its data, so `path` can end after the call.
     * An apply of the query does not parse again.
     * `T` is the result type: a scalar, a view type, `cbor::lazy` or cbor::nodelist.
     * The call reads no CBOR.
     * A path at run time reads the same grammar as a path at compile time, with EDN literals in brackets and in
     * filters.
     * `T` outside `path_result` does not compile.
     *
     * @code{.callseq}
     * cbor::jsonpath::compile<T>(path) → std::expected<query<T>, error>
     * @endcode
     * @code{.example}
     * message: {"a": [1, 2, 3]}
     * call: auto const owner = std::make_shared<std::string const>(bytes)
     * result: shared_ptr to 7 bytes: a1 61 61 83 01 02 03
     * call: auto const q = cbor::jsonpath::compile<cbor::nodelist>("$.a[?@ > 1]")
     * result: cbor::jsonpath::query<cbor::nodelist>
     * call: cbor::at_path(owner, *q)->size()
     * result: 2
     * call: cbor::at_path(owner, *cbor::jsonpath::compile<cbor::nodelist>("$.a[0]"))->size()
     * result: 1
     * call: cbor::jsonpath::compile<cbor::lazy>("$.a[1]").has_value()
     * result: true
     * @endcode
     *
     * @retval invalid_path the text is not valid, or it calls `match` or `search`
     * @retval not_singular_query the path selects many nodes and `T` is not `nodelist`
     * @retval nesting_depth_exceeded the path is nested deeper than the stack of the thread holds
     * @snippet doc_reference_path.cpp compile
     * @see cbor::jsonpath::query
     * @see cbor::at_path()
     */
    template <class T>
        requires path_result<T>
    static std::expected<query<T>, error> compile(std::string_view const path)
    {
        return query_compile<T>(path);
    }

private:
    template <class T>
    static std::expected<query<T>, error> query_compile(std::string_view const path)
    {
        auto q = parse_query(path);
        if (!q) [[unlikely]]
            return std::unexpected(q.error());
        if (!std::is_same_v<T, nodelist> && !q->top.singular) [[unlikely]]
            return std::unexpected(error::not_singular_query);
        query<T> e;
        e.segments = std::move(q->segments);
        e.selectors = std::move(q->selectors);
        e.filters = std::move(q->expressions);
        e.keys = std::move(q->keys);
        e.top = q->top;
        return e;
    }

    template <class T>
    static std::expected<typename query<T>::result_type, error> apply_query(query<T> const &e,
                                                                            lazy const &root)
    {
        throw_logic_error_if_query_empty(e);
        query_view const v{e.segments, e.selectors, e.filters, e.keys, {}};
        if constexpr (std::is_same_v<T, nodelist> || std::is_same_v<T, lazy>) {
            if constexpr (std::is_same_v<T, nodelist>)
                return walk_query(v, e.top, root);
            else
                return walk_singular_query(v, e.top, root);
        } else if constexpr (view_result<T>) {
            auto const r = walk_lazy<T>(v, e.top, root);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            return ref<T>(root.top_level.owner, *r);
        } else
            return walk_lazy<T>(v, e.top, root);
    }

    template <class T>
    static std::expected<T, error> apply_query(query<T> const &e, std::string_view const encoded)
    {
        throw_logic_error_if_query_empty(e);
        return walk_bytes<T>({e.segments, e.selectors, e.filters, e.keys, {}}, e.top,
                             encoded);
    }

    template <view_result T>
    static std::expected<ref<T>, error> apply_query(query<T> const &e, std::shared_ptr<void const> owner,
                                                    std::string_view const encoded)
    {
        auto const r = apply_query(e, encoded);
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        return ref<T>(std::move(owner), *r);
    }

    template <class T>
    static void throw_logic_error_if_query_empty(query<T> const &e)
    {
        detail::validity::throw_logic_error_if(e.top.at == 0,
                                               "cbor::jsonpath::query: the query is empty or moved from");
    }

    /// @cond
    template <scalar_result T>
    friend std::expected<T, error> at_path(std::string_view encoded, query<T> const &e);

    template <view_result T, borrowed_range_of_chars Encoded>
    friend std::expected<ref<T>, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded,
                                                       query<T> const &e);
    /// @endcond
};

template <class T>
inline constexpr bool is_path_query_v<jsonpath::query<T>> = true;

/**
 * @brief Reads a scalar at a query in `encoded`.
 * @ingroup path
 */
template <scalar_result T>
std::expected<T, error> at_path(std::string_view const encoded, jsonpath::query<T> const &e)
{
    return jsonpath::apply_query(e, encoded);
}

/**
 * @brief Reads a view at a query in bytes that `owner` keeps alive.
 * @ingroup path
 */
template <view_result T, borrowed_range_of_chars Encoded>
std::expected<ref<T>, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded,
                                            jsonpath::query<T> const &e)
{
    detail::validity::throw_logic_error_if_empty(
        owner, "cbor::at_path: the owner of the encoded data item is empty");
    return jsonpath::apply_query(e, std::move(owner),
                                 std::string_view(std::span<char const>(encoded)));
}

/**
 * @brief Gives the lazy or the nodelist at a query in bytes that `owner` keeps alive.
 * @ingroup path
 */
template <class T, borrowed_range_of_chars Encoded>
    requires(std::is_same_v<T, lazy> || std::is_same_v<T, nodelist>)
std::expected<T, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded,
                                jsonpath::query<T> const &e)
{
    return lazy::view(std::move(owner), encoded).and_then([&e](lazy const &root) { return root.at_path(e); });
}

/**
 * @brief Reads the result of a query in the bytes of `*owner`.
 * @ingroup path
 */
template <class T, owning_range_of_chars Encoded>
    requires(view_result<T> || std::is_same_v<T, lazy> || std::is_same_v<T, nodelist>)
std::expected<typename jsonpath::query<T>::result_type, error> at_path(std::shared_ptr<Encoded const> owner,
                                                                       jsonpath::query<T> const &e)
{
    detail::validity::throw_logic_error_if_null(owner, "cbor::at_path: the encoded data item is null");
    std::string_view const bytes{std::span<char const>(*owner)};
    return at_path(std::move(owner), bytes, e);
}

/**
 * @brief Reads the result of a query, and moves the bytes into a new owner.
 * @ingroup path
 */
template <class T, owning_range_of_chars Encoded>
    requires(view_result<T> || std::is_same_v<T, lazy> || std::is_same_v<T, nodelist>)
std::expected<typename jsonpath::query<T>::result_type, error> at_path(Encoded &&encoded,
                                                                       jsonpath::query<T> const &e)
{
    return at_path(std::make_shared<Encoded const>(std::move(encoded)), e);
}

template <fixed_string Path>
class is_valid_path
    : public std::bool_constant<jsonpath::parse_query(Path.view()).has_value()>
{
};

inline std::expected<lazy, error> jsonpath::select_index(lazy const &node, std::int64_t const index)
{
    node.throw_logic_error_if_no_top_level();
    auto found = lazy::resolve<true>(node);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (found->h.major != major_type::array && !heads::is_typed_array(found->h))
        return lazy::value_of(lazy::find_key(std::move(*found), index));
    return lazy::select_index(*found, index);
}

inline std::expected<lazy, error> jsonpath::find_key(lazy const &node, std::string_view const key)
{
    heads::decoder<false> text_key{key};
    auto const literal = text_key.decode_head();
    node.throw_logic_error_if_no_top_level();
    auto found = lazy::resolve<true>(node);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (literal && literal->major == major_type::text_string)
        return lazy::value_of(lazy::find_key(std::move(*found), text_key.encoded));
    auto [source, h, d, item_at] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        detail::validity::offset const start = detail::validity::distance(source.encoded, d.encoded);
        if (auto const r = well_formedness::skip_items(d, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const match = detail::validity::keys_equivalent<true>(
            source, start, key, detail::validity::offset{});
        if (!match) [[unlikely]]
            return std::unexpected(match.error());
        if (*match)
            return lazy{source, std::to_underlying(detail::validity::distance(source.encoded, d.encoded)), {}};
        if (auto const r = well_formedness::skip_items(d, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
            }
            return std::unexpected(error::key_not_found);
}

inline jsonpath::exact_number jsonpath::number_of(heads::head const &n)
{
    if (n.major == major_type::unsigned_integer)
        return {false, true, false, n.argument, 0.0};
    if (n.major == major_type::negative_integer)
        return {false, true, true, n.argument, 0.0};
    double const d = heads::decode_float(n.info, n.argument);
    if (std::isnan(d))
        return {true, false, false, 0, d};
    if (std::trunc(d) != d || d >= two_to_the_64 || d < -two_to_the_64)
        return {false, false, d < 0.0, 0, d};
    if (d >= 0.0)
        return {false, true, false, static_cast<std::uint64_t>(d), 0.0};
    if (d == -two_to_the_64)
        return {false, true, true, std::numeric_limits<std::uint64_t>::max(), 0.0};
    return {false, true, true, static_cast<std::uint64_t>(-d) - 1u, 0.0};
}

inline std::expected<std::pair<heads::head, heads::decoder<true>>, error>
jsonpath::integer_of(heads::head const &n, heads::decoder<true> const content)
{
    if (!heads::is_bignum(n.major, n.argument))
        return std::pair{n, content};
    auto const c = heads::read_raw_head<true>(content.encoded, detail::validity::offset{});
    if (!c || c->major != major_type::byte_string ||
        !detail::validity::check_definite_length(c->major, c->info).has_value())
        return std::pair{n, content};
    heads::decoder<true> d = content;
    auto const read = d.decode_bignum(n.argument);
    if (!read) [[unlikely]]
        return std::unexpected(read.error());
    if (!*read)
        return std::pair{n, content};
    return std::pair{heads::head{n.argument == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                     ? major_type::negative_integer
                                     : major_type::unsigned_integer,
                                 0, **read},
                     d};
}

inline std::partial_ordering jsonpath::compare_numbers(exact_number const &a, exact_number const &b)
{
    if (a.nan || b.nan)
        return std::partial_ordering::unordered;
    auto const integers = [](exact_number const &x, exact_number const &y) {
        if (x.negative != y.negative)
            return x.negative ? std::partial_ordering::less : std::partial_ordering::greater;
        auto const order = x.magnitude <=> y.magnitude;
        return x.negative ? 0 <=> order : std::partial_ordering(order);
    };
    if (a.integral && b.integral)
        return integers(a, b);
    if (!a.integral && !b.integral)
        return a.value <=> b.value;
    exact_number const &i = a.integral ? a : b;
    exact_number const &f = a.integral ? b : a;
    std::partial_ordering order = std::partial_ordering::equivalent;
    if (f.value >= two_to_the_64)
        order = std::partial_ordering::less;
    else if (f.value < -two_to_the_64)
        order = std::partial_ordering::greater;
    else {
        heads::head const whole =
            std::trunc(f.value) >= 0.0
                ? heads::head{major_type::unsigned_integer, 0, static_cast<std::uint64_t>(std::trunc(f.value))}
                : heads::head{major_type::negative_integer, 0, static_cast<std::uint64_t>(-std::trunc(f.value)) - 1u};
        order = integers(i, number_of(whole));
        if (order == 0)
            order = f.value > std::trunc(f.value) ? std::partial_ordering::less : std::partial_ordering::greater;
    }
    return a.integral ? order : 0 <=> order;
}

inline std::expected<std::pair<std::uint64_t, detail::validity::offset>, error>
jsonpath::intern_value(detail::canonical_ids &ids, lazy::top_level_item const &source, detail::validity::offset const at)
{
    if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const resolved = lazy::resolve_container<true>(source, at);
    if (!resolved) [[unlikely]]
        return std::unexpected(resolved.error());
    lazy::top_level_item const &inner = resolved->source;
    auto const folded = integer_of(resolved->h, resolved->d);
    if (!folded) [[unlikely]]
        return std::unexpected(folded.error());
    heads::head const h = folded->first;
    heads::decoder<true> d = folded->second;
    auto const end = [&](detail::validity::offset const inner_end) -> std::expected<detail::validity::offset, error> {
        if (inner.encoded.data() == source.encoded.data())
            return inner_end;
        heads::decoder<true> outer{detail::validity::subspan(source.encoded, at)};
        if (auto const r = well_formedness::skip_items(outer, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        return detail::validity::distance(source.encoded, outer.encoded);
    };
    auto const done = [&](std::uint64_t const id) -> std::expected<std::pair<std::uint64_t, detail::validity::offset>, error> {
        auto const e = end(detail::validity::distance(inner.encoded, d.encoded));
        if (!e) [[unlikely]]
            return std::unexpected(e.error());
        return std::pair{id, *e};
    };
    using kind = detail::canonical_ids::kind;
    constexpr std::uint8_t half = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice =
        std::to_underlying(rfc8949::simple_float_information::double_precision_float);
    bool const integral = h.major == major_type::unsigned_integer || h.major == major_type::negative_integer;
    bool const floating = h.major == major_type::simple_float && h.info >= half && h.info <= twice;
    if (integral || floating) {
        exact_number const n = number_of(h);
        if (n.nan) {
            ids.mark_unequal();
            return done(ids.id_of(kind::nan, 0, {}));
        }
        if (n.integral)
            return done(ids.id_of(n.negative ? kind::negative_integer : kind::positive_integer, n.magnitude, {}));
        return done(ids.id_of(kind::floating, std::bit_cast<std::uint64_t>(n.value), {}));
    }
    switch (h.major) {
    case major_type::byte_string:
    case major_type::text_string: {
        auto const s = d.decode_byte_string(h.argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        return done(ids.id_of(
            {h.major == major_type::byte_string ? kind::byte_string : kind::text_string, 0, *s, ids.children_end(), 0}));
    }
    case major_type::array:
    case major_type::map: {
        bool const map = h.major == major_type::map;
        if (auto const r = detail::validity::check_pending_items(h.major, h.info, h.argument, d.encoded.size()); !r)
            [[unlikely]]
            return std::unexpected(r.error());
        auto const child = [&]() -> std::expected<std::uint64_t, error> {
            auto const c = intern_value(ids, inner, detail::validity::distance(inner.encoded, d.encoded));
            if (!c) [[unlikely]]
                return std::unexpected(c.error());
            d.encoded = detail::validity::subspan(inner.encoded, c->second);
            return c->first;
        };
        std::vector<std::uint64_t> items;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> pairs;
        for (std::uint64_t i = 0; i < h.argument; ++i) {
            auto const key = child();
            if (!key) [[unlikely]]
                return std::unexpected(key.error());
            if (!map) {
                items.push_back(*key);
                continue;
            }
            auto const value = child();
            if (!value) [[unlikely]]
                return std::unexpected(value.error());
            pairs.emplace_back(*key, *value);
        }
        if (map) {
            std::ranges::sort(pairs);
            if (std::ranges::adjacent_find(pairs, {}, &std::pair<std::uint64_t, std::uint64_t>::first) != pairs.end())
                ids.mark_unequal();
            for (auto const &[key, value] : pairs) {
                items.push_back(key);
                items.push_back(value);
            }
        }
        return done(ids.id_of(map ? kind::map : kind::array, 0, items));
    }
    case major_type::tag: {
        auto const content = intern_value(ids, inner, detail::validity::distance(inner.encoded, d.encoded));
        if (!content) [[unlikely]]
            return std::unexpected(content.error());
        d.encoded = detail::validity::subspan(inner.encoded, content->second);
        std::array<std::uint64_t, 1> const item{content->first};
        return done(ids.id_of(kind::tag, h.argument, item));
    }
    default: {
        if (error const c = detail::validity::check_simple_value(h.info, h.argument).error_or(error{}); c != error{})
            [[unlikely]]
            return std::unexpected(c);
        std::array<std::uint64_t, 1> const info{h.info};
        return done(ids.id_of(kind::simple, h.argument, info));
    }
    }
}

inline std::expected<bool, error> jsonpath::value_equal(lazy const &a, lazy const &b)
{
    if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    a.throw_logic_error_if_no_top_level();
    auto const x = a.typed_array_tag != 0 ? lazy::resolve<true>(a)
                                          : lazy::resolve_container<true>(a.top_level, detail::validity::offset{a.offset});
    if (!x) [[unlikely]]
        return std::unexpected(x.error());
    auto const y = b.typed_array_tag != 0 ? lazy::resolve<true>(b)
                                          : lazy::resolve_container<true>(b.top_level, detail::validity::offset{b.offset});
    if (!y) [[unlikely]]
        return std::unexpected(y.error());
    auto const x_number = integer_of(x->h, x->d);
    if (!x_number) [[unlikely]]
        return std::unexpected(x_number.error());
    auto const y_number = integer_of(y->h, y->d);
    if (!y_number) [[unlikely]]
        return std::unexpected(y_number.error());
    heads::head const &h = x_number->first;
    heads::head const &k = y_number->first;
    constexpr std::uint8_t half = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice =
        std::to_underlying(rfc8949::simple_float_information::double_precision_float);
    auto const integral = [](heads::head const &n) {
        return n.major == major_type::unsigned_integer || n.major == major_type::negative_integer;
    };
    auto const floating = [](heads::head const &n) {
        return n.major == major_type::simple_float && n.info >= half && n.info <= twice;
    };
    if ((integral(h) || floating(h)) && (integral(k) || floating(k)))
        return compare_numbers(number_of(h), number_of(k)) == 0;
    if (h.major != k.major || floating(h) || floating(k))
        return false;
    detail::validity::offset const x_content = detail::validity::distance(x->source.encoded, x->d.encoded);
    detail::validity::offset const y_content = detail::validity::distance(y->source.encoded, y->d.encoded);
    switch (h.major) {
    case major_type::byte_string:
    case major_type::text_string: {
        heads::decoder d = x->d;
        heads::decoder e = y->d;
        auto const s = d.decode_byte_string(h.argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        auto const t = e.decode_byte_string(k.argument);
        if (!t) [[unlikely]]
            return std::unexpected(t.error());
        return *s == *t;
            }
            case major_type::array: {
                if (h.argument != k.argument)
                    return false;
                auto const left = a.elements();
                if (!left) [[unlikely]]
                    return std::unexpected(left.error());
                auto const right = b.elements();
                if (!right) [[unlikely]]
                    return std::unexpected(right.error());
                auto j = right->begin();
                for (auto const element : *left) {
                    auto const other = *j;
                    if (!element) [[unlikely]]
                        return std::unexpected(element.error());
                    if (!other) [[unlikely]]
                        return std::unexpected(other.error());
                    auto const same = value_equal(*element, *other);
                    if (!same || !*same)
                        return same;
                    ++j;
                }
                return true;
            }
            case major_type::map: {
                if (h.argument != k.argument)
                    return false;
                detail::canonical_ids ids;
                auto const p = intern_value(ids, a.top_level, detail::validity::offset{a.offset});
                if (!p) [[unlikely]]
                    return std::unexpected(p.error());
                auto const q = intern_value(ids, b.top_level, detail::validity::offset{b.offset});
                if (!q) [[unlikely]]
                    return std::unexpected(q.error());
                return !ids.unequal() && p->first == q->first;
            }
            case major_type::tag:
                if (h.argument != k.argument)
                    return false;
                return value_equal(lazy{x->source, std::to_underlying(x_content), {}},
                                   lazy{y->source, std::to_underlying(y_content), {}});
            default:
                return h.info == k.info && h.argument == k.argument;
            }
}

inline std::expected<bool, error> jsonpath::value_less(lazy const &a, lazy const &b)
{
    a.throw_logic_error_if_no_top_level();
    auto const x = a.typed_array_tag != 0 ? lazy::resolve<true>(a)
                                          : lazy::resolve_container<true>(a.top_level, detail::validity::offset{a.offset});
    if (!x) [[unlikely]]
        return std::unexpected(x.error());
    auto const y = b.typed_array_tag != 0 ? lazy::resolve<true>(b)
                                          : lazy::resolve_container<true>(b.top_level, detail::validity::offset{b.offset});
    if (!y) [[unlikely]]
        return std::unexpected(y.error());
    auto const x_number = integer_of(x->h, x->d);
    if (!x_number) [[unlikely]]
        return std::unexpected(x_number.error());
    auto const y_number = integer_of(y->h, y->d);
    if (!y_number) [[unlikely]]
        return std::unexpected(y_number.error());
    heads::head const &h = x_number->first;
    heads::head const &k = y_number->first;
    constexpr std::uint8_t half = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice =
        std::to_underlying(rfc8949::simple_float_information::double_precision_float);
    auto const integral = [](heads::head const &n) {
        return n.major == major_type::unsigned_integer || n.major == major_type::negative_integer;
    };
    auto const floating = [](heads::head const &n) {
        return n.major == major_type::simple_float && n.info >= half && n.info <= twice;
    };
            if ((integral(h) || floating(h)) && (integral(k) || floating(k)))
                return compare_numbers(number_of(h), number_of(k)) < 0;
            if (h.major != major_type::text_string || k.major != major_type::text_string)
                return false;
            heads::decoder d = x->d;
            heads::decoder e = y->d;
            auto const s = d.decode_byte_string(h.argument);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            auto const t = e.decode_byte_string(k.argument);
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            return std::ranges::lexicographical_compare(
                *s, *t, {}, [](char const c) { return static_cast<unsigned char>(c); },
                [](char const c) { return static_cast<unsigned char>(c); });
}

inline std::expected<std::optional<lazy>, error> jsonpath::comparable_value(query_view const v,
                                                                            std::size_t const index,
                                                                            lazy const &current,
                                                                            lazy const &root)
{
    if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    logical_expr const &e = v.expressions[index];
    auto const unsigned_integer = [](std::uint64_t const n) -> std::expected<std::optional<lazy>, error> {
        std::string encoded;
        if (auto const r = extended_diagnostic_notation::append_head(encoded, major_type::unsigned_integer, n, extended_diagnostic_notation::no_indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const owner = std::make_shared<std::string const>(std::move(encoded));
        auto const l = lazy::from(owner, *owner);
        if (!l) [[unlikely]]
            return std::unexpected(l.error());
        return *l;
    };
    switch (e.kind) {
    case logical_expr::kind::literal: {
        auto const owner = std::make_shared<std::string const>(
            std::string_view(std::span(v.keys).subspan(e.first, e.second)));
        auto const l = lazy::from(owner, *owner);
        if (!l) [[unlikely]]
            return std::unexpected(l.error());
        return *l;
    }
    case logical_expr::kind::filter_query:
    case logical_expr::kind::count:
    case logical_expr::kind::value: {
        std::size_t const at = e.kind == logical_expr::kind::filter_query ? index : e.first;
        logical_expr const &q = v.expressions[at];
        std::expected<std::vector<lazy>, error> relative;
        if (q.relative)
            relative = apply_segments(v, q.first, q.second, current, root);
        auto const &nodes = q.relative ? relative : v.absolute_nodelists[at];
        if (!nodes) [[unlikely]]
            return std::unexpected(nodes.error());
        if (e.kind == logical_expr::kind::count)
            return unsigned_integer(nodes->size());
        if (nodes->size() != 1)
            return std::nullopt;
        return nodes->front();
    }
    case logical_expr::kind::length: {
        auto const argument = comparable_value(v, e.first, current, root);
        if (!argument || !*argument) [[unlikely]]
            return argument;
        (*argument)->throw_logic_error_if_no_top_level();
        auto const found = (*argument)->typed_array_tag != 0
                               ? lazy::resolve<true>(**argument)
                               : lazy::resolve_container<true>((*argument)->top_level, detail::validity::offset{(*argument)->offset});
        if (!found) [[unlikely]]
            return std::unexpected(found.error());
        if (found->h.major == major_type::array || found->h.major == major_type::map)
            return unsigned_integer(found->h.argument);
        if (found->h.major != major_type::text_string)
            return std::nullopt;
        heads::decoder d = found->d;
        auto const s = d.decode_byte_string(found->h.argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        return unsigned_integer(static_cast<std::uint64_t>(std::ranges::count_if(
            *s, [](char const c) { return (static_cast<unsigned char>(c) & 0xc0) != 0x80; })));
    }
    [[unlikely]] default:
        return std::unexpected(error::invalid_path);
    }
}

inline std::expected<bool, error> jsonpath::test_expression(query_view const v, std::size_t const index,
                                                            lazy const &current, lazy const &root)
{
    if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    std::size_t at = index;
    while (v.expressions[at].kind == logical_expr::kind::logical_or || v.expressions[at].kind == logical_expr::kind::logical_and) {
        logical_expr const &chain = v.expressions[at];
        auto const left = test_expression(v, chain.first, current, root);
        if (!left || *left == (chain.kind == logical_expr::kind::logical_or))
            return left;
        at = chain.second;
    }
    logical_expr const &e = v.expressions[at];
    switch (e.kind) {
    case logical_expr::kind::logical_not: {
        auto const operand = test_expression(v, e.first, current, root);
        if (!operand) [[unlikely]]
            return operand;
        return !*operand;
    }
    case logical_expr::kind::filter_query: {
        std::expected<std::vector<lazy>, error> relative;
        if (e.relative)
            relative = apply_segments(v, e.first, e.second, current, root);
        auto const &nodes = e.relative ? relative : v.absolute_nodelists[at];
        if (!nodes) [[unlikely]]
            return std::unexpected(nodes.error());
        return !nodes->empty();
    }
    case logical_expr::kind::comparison:
        break;
    [[unlikely]] default:
        return std::unexpected(error::invalid_path);
    }
    auto const a = comparable_value(v, e.first, current, root);
    if (!a) [[unlikely]]
        return std::unexpected(a.error());
    auto const b = comparable_value(v, e.second, current, root);
    if (!b) [[unlikely]]
        return std::unexpected(b.error());
    auto const equal = [&]() -> std::expected<bool, error> {
        if (!*a || !*b)
            return !*a && !*b;
        return value_equal(**a, **b);
    };
    auto const less = [](std::optional<lazy> const &x,
                         std::optional<lazy> const &y) -> std::expected<bool, error> {
        if (!x || !y)
            return false;
        return value_less(*x, *y);
    };
    auto const either = [](std::expected<bool, error> const &x, auto const &y) -> std::expected<bool, error> {
        if (!x || *x)
            return x;
        return y();
    };
    switch (e.op) {
    case comparison_op::equal:
        return equal();
    case comparison_op::not_equal: {
        auto const r = equal();
        if (!r) [[unlikely]]
            return r;
        return !*r;
    }
    case comparison_op::less:
        return less(*a, *b);
    case comparison_op::less_equal:
        return either(less(*a, *b), equal);
    case comparison_op::greater:
        return less(*b, *a);
    case comparison_op::greater_equal:
        return either(less(*b, *a), equal);
    }
    return false;
}

inline std::expected<void, error> jsonpath::apply_selector(query_view const v, selector const &s,
                                                           lazy const &node, lazy const &root,
                                                           std::vector<lazy> &nodes)
{
    std::size_t const limit = root.top_level.encoded.size();
    auto const append = [&nodes, limit](lazy const &l) -> std::expected<void, error> {
        if (nodes.size() >= limit) [[unlikely]]
            return std::unexpected(error::nodelist_too_long);
        nodes.push_back(l);
        return {};
    };
    if (s.kind == selector::kind::key || s.kind == selector::kind::index) {
        auto const child =
            s.kind == selector::kind::index
                ? select_index(node, s.index)
                : find_key(node, std::string_view(std::span(v.keys).subspan(s.key_at, s.key_size)));
        if (child)
            return append(*child);
        if (child.error() != error::not_indexable && child.error() != error::index_out_of_bounds &&
            child.error() != error::key_not_found) [[unlikely]]
            return std::unexpected(child.error());
        return {};
    }
    node.throw_logic_error_if_no_top_level();
    auto const found = lazy::resolve<true>(node);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    lazy const content{found->source, std::to_underlying(found->item_at), {}};
    std::vector<lazy> children;
    if (found->h.major == major_type::array || heads::is_typed_array(found->h)) {
        auto const elements = content.elements();
        if (!elements) [[unlikely]]
            return std::unexpected(elements.error());
        for (auto const element : *elements) {
            if (!element) [[unlikely]]
                return std::unexpected(element.error());
            children.push_back(*element);
        }
            } else if (found->h.major == major_type::map && s.kind != selector::kind::slice) {
                auto const entries = content.entries();
                if (!entries) [[unlikely]]
                    return std::unexpected(entries.error());
                for (auto const entry : *entries) {
                    if (!entry) [[unlikely]]
                        return std::unexpected(entry.error());
                    children.push_back(entry->second);
                }
            }
            if (s.kind == selector::kind::slice) {
                auto const len = static_cast<std::int64_t>(children.size());
                if (s.step == 0)
                    return {};
                auto const normalize = [len](std::int64_t const i) { return i >= 0 ? i : len + i; };
                std::int64_t const start = normalize(s.start.value_or(s.step >= 0 ? 0 : len - 1));
                std::int64_t const end = s.end ? normalize(*s.end) : (s.step >= 0 ? len : -1);
                if (s.step > 0) {
                    std::int64_t const lower = std::min(std::max(start, std::int64_t{0}), len);
                    std::int64_t const upper = std::min(std::max(end, std::int64_t{0}), len);
                    for (std::int64_t i = lower; i < upper; i += s.step)
                        if (auto const r = append(children[static_cast<std::size_t>(i)]); !r) [[unlikely]]
                            return r;
                } else {
                    std::int64_t const upper = std::min(std::max(start, std::int64_t{-1}), len - 1);
                    std::int64_t const lower = std::min(std::max(end, std::int64_t{-1}), len - 1);
                    for (std::int64_t i = upper; lower < i; i += s.step)
                        if (auto const r = append(children[static_cast<std::size_t>(i)]); !r) [[unlikely]]
                            return r;
                }
                return {};
            }
            for (lazy const &child : children) {
                if (s.kind == selector::kind::filter) {
                    auto const chosen = test_expression(v, s.expression, child, root);
                    if (!chosen) [[unlikely]]
                        return std::unexpected(chosen.error());
                    if (!*chosen)
                        continue;
                }
                if (auto const r = append(child); !r) [[unlikely]]
                    return r;
            }
            return {};
}

inline std::expected<void, error> jsonpath::apply_segment(query_view const v, segment const &s,
                                                          lazy const &node, lazy const &root,
                                                          std::vector<lazy> &nodes)
{
    if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    for (selector const &each : v.selectors.subspan(s.selector_at, s.selector_count))
        if (auto const r = apply_selector(v, each, node, root, nodes); !r) [[unlikely]]
            return r;
    if (!s.descendant)
        return {};
    std::vector<lazy> children;
    selector const wildcard{selector::kind::wildcard, 0, 0, 0, std::nullopt, std::nullopt, 1, 0};
    if (auto const r = apply_selector(v, wildcard, node, root, children); !r) [[unlikely]]
        return r;
    for (lazy const &child : children)
        if (auto const r = apply_segment(v, s, child, root, nodes); !r) [[unlikely]]
            return r;
    return {};
}

inline std::expected<std::vector<lazy>, error> jsonpath::apply_segments(query_view const v,
                                                                        std::size_t const segment_at,
                                                                        std::size_t const segment_count,
                                                                        lazy const &start, lazy const &root)
{
    std::vector<lazy> nodes{start};
    std::vector<lazy> next;
    for (segment const &s : v.segments.subspan(segment_at, segment_count)) {
        next.clear();
        for (lazy const &node : nodes)
            if (auto const r = apply_segment(v, s, node, root, next); !r) [[unlikely]]
                return std::unexpected(r.error());
        std::swap(nodes, next);
    }
    return nodes;
}

inline std::expected<lazy, error> jsonpath::walk_singular_query(query_view const v, parsed_query const &top,
                                                                lazy const &root)
{
    detail::validity::throw_logic_error_if(root.top_level.encoded.data() == nullptr,
                                           "cbor::lazy::at_path: the lazy holds no top-level item");
    lazy node = root;
    for (segment const &s : v.segments.subspan(top.segment_at, top.segment_count)) {
        selector const &each = v.selectors[s.selector_at];
        auto const child =
            each.kind == selector::kind::index
                ? select_index(node, each.index)
                : find_key(node, std::string_view(std::span(v.keys).subspan(each.key_at, each.key_size)));
        if (!child) [[unlikely]]
            return std::unexpected(child.error());
        node = *child;
    }
    return node;
}

inline std::expected<std::vector<lazy>, error> jsonpath::walk_query(query_view const v,
                                                                    parsed_query const &top, lazy const &root)
{
    detail::validity::throw_logic_error_if(root.top_level.encoded.data() == nullptr,
                                           "cbor::lazy::query: the lazy holds no top-level item");
    std::vector<std::expected<std::vector<lazy>, error>> absolute_nodelists(v.expressions.size());
    query_view const with_nodelists{v.segments, v.selectors, v.expressions, v.keys, absolute_nodelists};
    for (std::size_t i = 0; i < v.expressions.size(); ++i)
        if (v.expressions[i].kind == logical_expr::kind::filter_query && !v.expressions[i].relative)
            absolute_nodelists[i] =
                apply_segments(with_nodelists, v.expressions[i].first, v.expressions[i].second, root, root);
    return apply_segments(with_nodelists, top.segment_at, top.segment_count, root, root);
}

template <fixed_string Path, class Walk>
auto jsonpath::apply_compiled(Walk const &walk)
{
    constexpr auto q = [] { return *parse_query(Path.view()); };
    constexpr std::size_t segments = q().segments.size();
    constexpr std::size_t selectors = q().selectors.size();
    constexpr std::size_t expressions = q().expressions.size();
    constexpr std::size_t keys = q().keys.size();
    constexpr auto top = q().top;
    constexpr auto compiled = [q] {
        auto const parsed = q();
        std::tuple<std::array<segment, segments>, std::array<selector, selectors>,
                   std::array<logical_expr, expressions>, std::array<char, keys>>
            c{};
        std::ranges::copy(parsed.segments, std::get<0>(c).begin());
        std::ranges::copy(parsed.selectors, std::get<1>(c).begin());
        std::ranges::copy(parsed.expressions, std::get<2>(c).begin());
        std::ranges::copy(parsed.keys, std::get<3>(c).begin());
        return c;
    }();
    return walk(query_view{std::get<0>(compiled),
                           std::get<1>(compiled),
                           std::get<2>(compiled),
                           std::string_view(std::get<3>(compiled)),
                           {}},
                top);
}

template <fixed_string Path>
class is_singular_query
    : public std::bool_constant<
          [] {
    std::array const text = Path.value;
    auto const q =
        jsonpath::parse_query(std::string_view(std::span(text).first(text.size() - 1)));
    return q.has_value() && q->top.singular;
}()>
{
};

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
              std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
              || std::is_same_v<T, std::float128_t>
#endif
              ))
std::expected<T, error> at_path(std::string_view const encoded)
{
    return jsonpath::walk_query<Path, T>(encoded);
}

template <fixed_string Path, class T, borrowed_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded)
{
    detail::validity::throw_logic_error_if_empty(
        owner, "cbor::at_path: the owner of the encoded data item is empty");
    return jsonpath::walk_query<Path, T>(std::move(owner),
                                         std::string_view(std::span<char const>(encoded)));
}

template <fixed_string Path, class T, owning_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(std::shared_ptr<Encoded const> owner)
{
    detail::validity::throw_logic_error_if_null(owner, "cbor::at_path: the encoded data item is null");
    std::string_view const bytes{std::span<char const>(*owner)};
    return at_path<Path, T>(std::move(owner), bytes);
}

template <fixed_string Path, class T, owning_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(Encoded &&encoded)
{
    return at_path<Path, T>(std::make_shared<Encoded const>(std::move(encoded)));
}

template <fixed_string Path, copied_value T>
    requires is_singular_query_v<Path>
std::expected<T, error> at_path(std::string_view const encoded)
{
    using view = std::conditional_t<std::same_as<T, std::string>, std::string_view, std::span<std::byte const>>;
    return jsonpath::walk_query<Path, view>(encoded).transform([](view const v) { return T(v.begin(), v.end()); });
}

inline std::expected<lazy, error> lazy::operator[](std::string_view const key) const
{
    return at(key);
}

inline std::expected<lazy, error> lazy::operator[](key const k) const
{
    return at(k);
}

inline std::expected<lazy, error> lazy::operator[](std::size_t const index) const
{
    return at(index);
}

template <fixed_string Path>
    requires is_valid_path_v<Path>
std::expected<lazy, error> lazy::at_path() const
{
    static_assert(jsonpath::parse_query(Path.view())->top.singular,
                  "cbor::lazy::at_path: the path is not a singular query (RFC 9535 2.3.5.1); cbor::nodelist "
                  "as the result type reads its nodelist");
    return jsonpath::apply_compiled<Path>(
        [this](jsonpath::query_view const v, jsonpath::parsed_query const &top) {
            return jsonpath::walk_singular_query(v, top, *this);
        });
}

template <fixed_string Path, std::same_as<nodelist> T>
    requires is_valid_path_v<Path>
std::expected<nodelist, error> lazy::at_path() const
{
    return jsonpath::apply_compiled<Path>(
        [this](jsonpath::query_view const v, jsonpath::parsed_query const &top) {
            return jsonpath::walk_query(v, top, *this);
        });
}

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
              std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
              || std::is_same_v<T, std::float128_t>
#endif
              ))
std::expected<T, error> lazy::at_path() const
{
    return jsonpath::walk_query<Path, T>(*this);
}

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> lazy::at_path() const
{
    return jsonpath::walk_owned_query<Path, T>(*this);
}

template <class E>
    requires is_path_query_v<E>
std::expected<typename E::result_type, error> lazy::at_path(E const &e) const
{
    return jsonpath::apply_query(e, *this);
}

}
