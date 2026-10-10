#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
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

#include "binding.hpp"
#include "item_size.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "diagnostic_notation.hpp"
#include "lazy.hpp"
#include "owning_ref.hpp"
#include "rfc9535.hpp"
#include "shared.hpp"

namespace cbor
{

struct lazy;

template <binding Binding>
std::expected<typename Binding::value, error> at_path(Binding &binding, std::string_view path, lazy const &l);

template <binding Binding>
std::expected<typename Binding::value, error> query(Binding &binding, std::string_view path, lazy const &l);

template <fixed_string Path>
class is_valid_path;

template <fixed_string Path>
inline constexpr bool is_valid_path_v = is_valid_path<Path>::value;

template <fixed_string Path, binding Binding>
    requires is_valid_path_v<Path>
std::expected<typename Binding::value, error> at_path(Binding &binding, lazy const &l);

template <fixed_string Path, binding Binding>
    requires is_valid_path_v<Path>
std::expected<typename Binding::value, error> query(Binding &binding, lazy const &l);

template <fixed_string Path>
class is_singular_query;

template <fixed_string Path>
inline constexpr bool is_singular_query_v = is_singular_query<Path>::value;

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
              std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>))
std::expected<T, error> at_path(std::string_view encoded);

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<owning_ref<T>, error> at_path(std::shared_ptr<void const> owner, std::string_view encoded);

template <fixed_string Path, class T, class Encoded>
    requires std::same_as<std::remove_const_t<Encoded>, std::string>
std::expected<owning_ref<T>, error> at_path(std::shared_ptr<void const> owner, Encoded &&encoded) = delete;

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

    struct expression {
        enum class kind { logical_or, logical_and, logical_not, comparison, query, literal, length, count, value } kind;
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
        std::span<expression const> expressions;
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

    static constexpr std::expected<std::optional<integer>, error> int_read(std::string_view const text, std::size_t const at)
    {
        if (at >= text.size())
            return std::nullopt;
        bool const negative = text[at] == '-';
        std::size_t const digits_at = at + (negative ? 1 : 0);
        std::size_t digits_end = digits_at;
        while (digits_end < text.size() && extended_diagnostic_notation::digit(text[digits_end]))
            ++digits_end;
        if (digits_end == digits_at || digits_end - digits_at > 16 ||
            (text[digits_at] == '0' && (digits_end > digits_at + 1 || negative)))
            return std::nullopt;
        std::int64_t value = 0;
        for (char const d : std::span(text).subspan(digits_at, digits_end - digits_at))
            value = value * extended_diagnostic_notation::decimal + (d - '0');
        if (value > rfc9535::exact_integer_max) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return integer{negative ? -value : value, digits_end};
    }

    static constexpr std::optional<std::size_t> number_end(std::string_view const text, std::size_t at)
    {
        auto const digits = [&text](std::size_t i) {
            while (i < text.size() && extended_diagnostic_notation::digit(text[i]))
                ++i;
            return i;
        };
        if (at < text.size() && text[at] == '-')
            ++at;
        std::size_t next = digits(at);
        if (next == at || (text[at] == '0' && next > at + 1))
            return std::nullopt;
        if (next < text.size() && text[next] == '.') {
            std::size_t const fraction = digits(next + 1);
            if (fraction == next + 1)
                return std::nullopt;
            next = fraction;
        }
        if (next < text.size() && (text[next] == 'e' || text[next] == 'E')) {
            std::size_t sign = next + 1;
            if (sign < text.size() && (text[sign] == '+' || text[sign] == '-'))
                ++sign;
            std::size_t const exponent = digits(sign);
            if (exponent == sign)
                return std::nullopt;
            next = exponent;
        }
        return next;
    }

    struct syntax_tree {
        std::vector<segment> segments;
        std::vector<selector> selectors;
        std::vector<expression> expressions;
        std::string keys;
        bool literals;
        std::size_t depth_max;
        parsed_query top;

        constexpr std::expected<std::size_t, error> string_parse(std::string_view const text, std::size_t const at)
        {
            std::string name;
            auto const next = extended_diagnostic_notation::quoted_parse(text, at, name);
            if (!next) [[unlikely]]
                return next;
            char const quote = text[at];
            for (std::size_t i = at + 1; !literals && i + 1 < *next; ++i) {
                if (text[i] != '\\')
                    continue;
                char const e = text[++i];
                if ((e == '\'' && quote == '"') || (e == '"' && quote == '\'') || (e == 'u' && text[i + 1] == '{'))
                    [[unlikely]]
                    return std::unexpected(error::invalid_path);
            }
            if (auto const r = extended_diagnostic_notation::head_append(keys, major_type::text_string, name.size(), extended_diagnostic_notation::no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            keys += name;
            return next;
        }

        constexpr std::expected<std::size_t, error> literal_parse(std::string_view const text, std::size_t const at)
        {
            std::string literal;
            auto const next = extended_diagnostic_notation::literal_parse({text, at}, literal, {0, depth_max});
            if (!next) [[unlikely]]
                return next;
            if (auto const r = extended_diagnostic_notation::canonical_append(keys, {literal, 0}, {0, depth_max}); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }

        constexpr std::expected<std::pair<std::size_t, selector>, error> selector_parse(std::string_view const text, std::size_t const at,
                                                                                       std::size_t const depth)
        {
            selector s{selector::kind::key, keys.size(), 0, 0, std::nullopt, std::nullopt, 1, 0};
            char const c = text[at];
            if (c == '*') {
                s.kind = selector::kind::wildcard;
                return std::pair{at + 1, s};
            }
            if (c == '\'' || c == '"') {
                auto const next = string_parse(text, at);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                s.key_size = keys.size() - s.key_at;
                return std::pair{*next, s};
            }
            if (c == '?') {
                auto const e = logical_or_parse(text, extended_diagnostic_notation::blank_end(text, at + 1), depth + 1);
                if (!e) [[unlikely]]
                    return std::unexpected(e.error());
                s.kind = selector::kind::filter;
                s.expression = e->index;
                return std::pair{e->at, s};
            }
            auto const first = int_read(text, at);
            if (!first) [[unlikely]]
                return std::unexpected(first.error());
            std::size_t next = *first ? extended_diagnostic_notation::blank_end(text, (*first)->at) : at;
            if (next < text.size() && text[next] == ':') {
                s.kind = selector::kind::slice;
                if (*first)
                    s.start = (*first)->value;
                next = extended_diagnostic_notation::blank_end(text, next + 1);
                auto const end = int_read(text, next);
                if (!end) [[unlikely]]
                    return std::unexpected(end.error());
                if (*end) {
                    s.end = (*end)->value;
                    next = extended_diagnostic_notation::blank_end(text, (*end)->at);
                }
                if (next < text.size() && text[next] == ':') {
                    next = extended_diagnostic_notation::blank_end(text, next + 1);
                    auto const step = int_read(text, next);
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
            if (!literals) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto const literal_next = literal_parse(text, at);
            if (!literal_next) [[unlikely]]
                return std::unexpected(literal_next.error());
            s.key_size = keys.size() - s.key_at;
            return std::pair{*literal_next, s};
        }

        constexpr std::expected<std::pair<std::size_t, std::vector<selector>>, error> bracketed_parse(std::string_view const text, std::size_t at,
                                                                                                     std::size_t const depth)
        {
            std::vector<selector> chosen;
            for (;;) {
                at = extended_diagnostic_notation::blank_end(text, at + 1);
                if (at >= text.size()) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                auto const s = selector_parse(text, at, depth);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                chosen.push_back(s->second);
                at = extended_diagnostic_notation::blank_end(text, s->first);
                if (at >= text.size() || (text[at] != ',' && text[at] != ']')) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (text[at] == ']')
                    return std::pair{at + 1, std::move(chosen)};
            }
        }

        constexpr std::expected<parsed_query, error> segments_parse(std::string_view const text, std::size_t at, std::size_t const depth)
        {
            if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
            std::vector<segment> found;
            std::vector<selector> found_selectors;
            bool singular = true;
            for (;;) {
                std::size_t const next = extended_diagnostic_notation::blank_end(text, at);
                if (next >= text.size() || (text[next] != '.' && text[next] != '['))
                    break;
                at = next;
                segment s{false, found_selectors.size(), 0};
                std::vector<selector> chosen;
                if (text[at] == '[' || std::ranges::starts_with(std::span(text).subspan(at), std::string_view("..["))) {
                    s.descendant = text[at] == '.';
                    auto const b =
                        bracketed_parse(text, at + (s.descendant ? std::string_view("..").size() : 0), depth);
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
                        if (auto const r = extended_diagnostic_notation::head_append(keys, major_type::text_string, end - at, extended_diagnostic_notation::no_indicator); !r) [[unlikely]]
                            return std::unexpected(r.error());
                        keys += std::string_view(std::span(text).subspan(at, end - at));
                        chosen.push_back({selector::kind::key, key_at, keys.size() - key_at, 0, std::nullopt, std::nullopt, 1, 0});
                        at = end;
                    }
                }
                singular = singular && !s.descendant && chosen.size() == 1 &&
                           (chosen.front().kind == selector::kind::key || chosen.front().kind == selector::kind::index);
                s.selector_count = chosen.size();
                found_selectors.insert(found_selectors.end(), chosen.begin(), chosen.end());
                found.push_back(s);
            }
            for (segment &s : found)
                s.selector_at += selectors.size();
            selectors.insert(selectors.end(), found_selectors.begin(), found_selectors.end());
            parsed_query const p{at, segments.size(), found.size(), singular};
            segments.insert(segments.end(), found.begin(), found.end());
            return p;
        }

        constexpr bool comparable(std::size_t const index) const
        {
            expression const &e = expressions[index];
            return e.kind == expression::kind::literal || e.kind == expression::kind::length ||
                   e.kind == expression::kind::count || e.kind == expression::kind::value ||
                   (e.kind == expression::kind::query && e.singular);
        }

        constexpr std::size_t expression_add(expression const e)
        {
            expressions.push_back(e);
            return expressions.size() - 1;
        }

        constexpr std::expected<parsed_expression, error> function_parse(std::string_view const text, std::size_t const at,
                                                                         std::string_view const name, std::size_t const depth)
        {
            std::vector<std::size_t> arguments;
            std::size_t next = extended_diagnostic_notation::blank_end(text, at + name.size() + 1);
            if (next < text.size() && text[next] == ')')
                ++next;
            else
                for (;;) {
                    auto const a = primary_parse(text, next, depth + 1);
                    if (!a) [[unlikely]]
                        return a;
                    arguments.push_back(a->index);
                    next = extended_diagnostic_notation::blank_end(text, a->at);
                    if (next >= text.size() || (text[next] != ',' && text[next] != ')')) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    if (text[next++] == ')')
                        break;
                    next = extended_diagnostic_notation::blank_end(text, next);
                }
            if (arguments.size() != 1) [[unlikely]]
                return std::unexpected(error::invalid_path);
            std::size_t const argument = arguments.front();
            auto const function = name == "length" ? expression::kind::length
                                          : name == "count" ? expression::kind::count
                                                            : expression::kind::value;
            bool const nodes = expressions[argument].kind == expression::kind::query;
            if (function == expression::kind::length ? !comparable(argument) : !nodes) [[unlikely]]
                return std::unexpected(error::invalid_path);
            return parsed_expression{next, expression_add({function, argument, 0, comparison_op::equal, false, false})};
        }

        constexpr std::expected<parsed_expression, error> primary_parse(std::string_view const text, std::size_t const at,
                                                                        std::size_t const depth)
        {
            if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text[at];
            if (c == '@' || c == '$') {
                auto const q = segments_parse(text, at + 1, depth + 1);
                if (!q) [[unlikely]]
                    return std::unexpected(q.error());
                return parsed_expression{q->at, expression_add({expression::kind::query, q->segment_at, q->segment_count,
                                                                comparison_op::equal, c == '@', q->singular})};
            }
            std::size_t const key_at = keys.size();
            auto const literal = [&](std::size_t const next) {
                return parsed_expression{next, expression_add({expression::kind::literal, key_at, keys.size() - key_at,
                                                               comparison_op::equal, false, false})};
            };
            if (c >= 'a' && c <= 'z') {
                std::size_t end = at;
                while (end < text.size() && function_name_char(text[end]))
                    ++end;
                std::string_view const name{std::span(text).subspan(at, end - at)};
                bool const call = end < text.size() && text[end] == '(';
                if (call && (name == "length" || name == "count" || name == "value"))
                    return function_parse(text, at, name, depth);
                if (call && (name == "match" || name == "search")) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (!literals) {
                    constexpr auto names = std::to_array<std::string_view>({"false", "true", "null"});
                    for (std::size_t i = 0; i < names.size(); ++i)
                        if (name == names[i]) {
                            keys.push_back(heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::false_value) + i));
                            return literal(end);
                        }
                    return std::unexpected(error::invalid_path);
                }
            }
            if (c == '\'' || c == '"') {
                auto const next = string_parse(text, at);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                return literal(*next);
            }
            if (!literals) {
                auto const end = number_end(text, at);
                if (!end) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                std::string number;
                auto const next = extended_diagnostic_notation::number_parse(text, at, number);
                if (!next || *next != *end) [[unlikely]]
                    return std::unexpected(error::invalid_path);
            }
            auto const next = literal_parse(text, at);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            return literal(*next);
        }

        static constexpr std::optional<std::pair<comparison_op, std::size_t>> comparison_op_read(std::string_view const text, std::size_t const at)
        {
            constexpr auto ops = std::to_array<std::pair<std::string_view, comparison_op>>(
                {{"==", comparison_op::equal},
                 {"!=", comparison_op::not_equal},
                 {"<=", comparison_op::less_equal},
                 {">=", comparison_op::greater_equal},
                 {"<", comparison_op::less},
                 {">", comparison_op::greater}});
            for (auto const &[token, op] : ops)
                if (std::ranges::starts_with(std::span(text).subspan(at), token))
                    return std::pair{op, token.size()};
            return std::nullopt;
        }

        constexpr std::expected<parsed_expression, error> paren_parse(std::string_view const text, std::size_t const at, std::size_t const depth)
        {
            auto const inner = logical_or_parse(text, extended_diagnostic_notation::blank_end(text, at + 1), depth + 1);
            if (!inner) [[unlikely]]
                return inner;
            std::size_t const close = extended_diagnostic_notation::blank_end(text, inner->at);
            if (close >= text.size() || text[close] != ')') [[unlikely]]
                return std::unexpected(error::invalid_path);
            return parsed_expression{close + 1, inner->index};
        }

        constexpr std::expected<parsed_expression, error> basic_parse(std::string_view const text, std::size_t const at, std::size_t const depth)
        {
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (text[at] == '!') {
                std::size_t const next = extended_diagnostic_notation::blank_end(text, at + 1);
                bool const paren = next < text.size() && text[next] == '(';
                auto const operand = paren ? paren_parse(text, next, depth) : primary_parse(text, next, depth);
                if (!operand) [[unlikely]]
                    return operand;
                if (!paren && expressions[operand->index].kind != expression::kind::query) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                return parsed_expression{operand->at, expression_add({expression::kind::logical_not, operand->index, 0,
                                                                      comparison_op::equal, false, false})};
            }
            if (text[at] == '(')
                return paren_parse(text, at, depth);
            auto const left = primary_parse(text, at, depth);
            if (!left) [[unlikely]]
                return left;
            std::size_t const next = extended_diagnostic_notation::blank_end(text, left->at);
            auto const op = comparison_op_read(text, next);
            if (!op) {
                if (expressions[left->index].kind != expression::kind::query) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                return left;
            }
            auto const right = primary_parse(text, extended_diagnostic_notation::blank_end(text, next + op->second), depth);
            if (!right) [[unlikely]]
                return right;
            if (!comparable(left->index) || !comparable(right->index)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            return parsed_expression{right->at, expression_add({expression::kind::comparison, left->index, right->index,
                                                                op->first, false, false})};
        }

        constexpr std::expected<parsed_expression, error> logical_and_parse(std::string_view const text, std::size_t const at,
                                                                            std::size_t const depth)
        {
            auto const first = basic_parse(text, at, depth);
            if (!first) [[unlikely]]
                return first;
            parsed_expression chain = *first;
            std::optional<std::size_t> last;
            for (;;) {
                std::size_t const next = extended_diagnostic_notation::blank_end(text, chain.at);
                if (!std::ranges::starts_with(std::span(text).subspan(next), std::string_view("&&")))
                    return chain;
                auto const right = basic_parse(
                    text, extended_diagnostic_notation::blank_end(text, next + std::string_view("&&").size()),
                    depth);
                if (!right) [[unlikely]]
                    return right;
                std::size_t const left = last ? expressions[*last].second : chain.index;
                std::size_t const node = expression_add({expression::kind::logical_and, left, right->index,
                                                         comparison_op::equal, false, false});
                if (last)
                    expressions[*last].second = node;
                else
                    chain.index = node;
                chain.at = right->at;
                last = node;
            }
        }

        constexpr std::expected<parsed_expression, error> logical_or_parse(std::string_view const text, std::size_t const at,
                                                                           std::size_t const depth)
        {
            if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const first = logical_and_parse(text, at, depth);
            if (!first) [[unlikely]]
                return first;
            parsed_expression chain = *first;
            std::optional<std::size_t> last;
            for (;;) {
                std::size_t const next = extended_diagnostic_notation::blank_end(text, chain.at);
                if (!std::ranges::starts_with(std::span(text).subspan(next), std::string_view("||")))
                    return chain;
                auto const right = logical_and_parse(
                    text, extended_diagnostic_notation::blank_end(text, next + std::string_view("||").size()),
                    depth);
                if (!right) [[unlikely]]
                    return right;
                std::size_t const left = last ? expressions[*last].second : chain.index;
                std::size_t const node = expression_add({expression::kind::logical_or, left, right->index,
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

    static constexpr std::expected<syntax_tree, error> query_parse(std::string_view const text, bool const literals,
                                                             std::size_t const depth_max)
    {
        syntax_tree q{{}, {}, {}, {}, literals, depth_max, {}};
        if (text.empty() || text.front() != '$') [[unlikely]]
            return std::unexpected(error::invalid_path);
        auto const top = q.segments_parse(text, 1, 0);
        if (!top) [[unlikely]]
            return std::unexpected(top.error());
        if (top->at != text.size()) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (auto const r = validity::check_nesting_depth(top->segment_count, depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        q.top = *top;
        return q;
    }

    static std::expected<lazy, error> key_find(lazy const &node, std::string_view key, std::size_t depth_max);

    static std::expected<lazy, error> index_select(lazy const &node, std::int64_t index);

    static std::expected<bool, error> value_equal(lazy const &a, lazy const &b, std::size_t depth, std::size_t depth_max);

    static std::expected<bool, error> value_less(lazy const &a, lazy const &b);

    static std::expected<std::optional<lazy>, error> comparable_value(query_view const &v, std::size_t index, lazy const &current,
                                                                      lazy const &root, std::size_t depth_max);

    static std::expected<bool, error> expression_test(query_view const &v, std::size_t index, lazy const &current, lazy const &root,
                                             std::size_t depth_max);

    static std::expected<void, error> selector_apply(query_view const &v, selector const &s, lazy const &node, lazy const &root,
                                                     std::vector<lazy> &nodelist, std::size_t depth_max);

    static std::expected<void, error> segment_apply(query_view const &v, segment const &s, lazy const &node, lazy const &root,
                                                        std::vector<lazy> &nodelist, std::size_t depth, std::size_t depth_max);

    static std::expected<std::vector<lazy>, error> segments_apply(query_view const &v, std::size_t segment_at, std::size_t segment_count,
                                                                  lazy const &start, lazy const &root, std::size_t depth_max);

    template <class Binding>
    static std::expected<typename Binding::value, error> singular_query_walk(Binding &binding, query_view const &v,
                                                                             parsed_query const &top, lazy const &root,
                                                                             std::size_t depth_max);

    template <class Binding>
    static std::expected<typename Binding::value, error> query_walk(Binding &binding, query_view const &v, parsed_query const &top,
                                                             lazy const &root, std::size_t depth_max);

    template <fixed_string Path, class Walk>
    static auto compiled_apply(Walk const &walk);

    template <class T>
    static std::optional<std::expected<T, error>> query_walk(query_view const &v, parsed_query const &top,
                                                             std::string_view const encoded)
    {
        heads::decoder d{encoded};
        heads::head h{};
        std::size_t step = 0;
        well_formedness::no_marks none;
        for (;;) {
            for (;;) {
                for (;;) {
                    heads::decoder const before = d;
                    auto const t = d.head_decode();
                    if (!t) [[unlikely]]
                        return std::unexpected(t.error());
                    if (t->major == major_type::tag &&
                        t->argument == std::to_underlying(rfc8949::tag_number::sharedref)) [[unlikely]]
                        return std::nullopt;
                    if (t->major != major_type::tag ||
                        (t->argument != std::to_underlying(rfc8949::tag_number::shareable) &&
                         t->argument != std::to_underlying(rfc8949::tag_number::self_described_cbor))) {
                        d = before;
                        break;
                    }
                }
                auto const c = d.head_decode();
                if (!c) [[unlikely]]
                    return std::unexpected(c.error());
                h = *c;
                if (h.major != major_type::tag ||
                    h.argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
                    break;
                auto const r = d.head_decode();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (!validity::check_tag_content(h.argument, r->major, r->info).has_value()) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                auto const embedded = d.byte_string_decode(r->argument);
                if (!embedded) [[unlikely]]
                    return std::unexpected(embedded.error());
                d = heads::decoder{*embedded};
            }
            if (step == top.segment_count)
                break;
            selector const &each = v.selectors[v.segments[top.segment_at + step].selector_at];
            ++step;
            if (each.kind == selector::kind::index && h.major == major_type::array) {
                auto const position = validity::check_index(each.index, h.argument);
                if (!position) [[unlikely]]
                    return std::unexpected(position.error());
                for (std::uint64_t i = 0; i < *position; ++i)
                    if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
                        return std::unexpected(r.error());
                continue;
            }
            if (h.major != major_type::map) [[unlikely]]
                return std::unexpected(error::not_indexable);
            std::string_view const key =
                each.kind == selector::kind::key ? std::string_view(std::span(v.keys).subspan(each.key_at, each.key_size)) : std::string_view{};
            heads::decoder named{key};
            if (each.kind == selector::kind::key && !named.head_decode()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            bool found = false;
            for (std::uint64_t i = 0; i < h.argument && !found; ++i) {
                heads::decoder probe = d;
                for (;;) {
                    heads::decoder const before = probe;
                    auto const t = probe.head_decode();
                    if (!t) [[unlikely]]
                        return std::unexpected(t.error());
                    if (t->major == major_type::tag &&
                        t->argument == std::to_underlying(rfc8949::tag_number::sharedref)) [[unlikely]]
                        return std::nullopt;
                    if (t->major != major_type::tag ||
                        (t->argument != std::to_underlying(rfc8949::tag_number::shareable) &&
                         t->argument != std::to_underlying(rfc8949::tag_number::self_described_cbor))) {
                        probe = before;
                        break;
                    }
                }
                auto const k = probe.head_decode();
                if (!k) [[unlikely]]
                    return std::unexpected(k.error());
                if (each.kind == selector::kind::key) {
                    if (k->major == major_type::text_string) {
                        auto const content = probe.byte_string_decode(k->argument);
                        if (!content) [[unlikely]]
                            return std::unexpected(content.error());
                        found = *content == named.encoded;
                    }
                } else {
                    found = (k->major == major_type::unsigned_integer && each.index >= 0 &&
                             k->argument == static_cast<std::uint64_t>(each.index)) ||
                            (k->major == major_type::negative_integer && each.index < 0 &&
                             k->argument == static_cast<std::uint64_t>(-1 - each.index));
                }
                if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
                    return std::unexpected(r.error());
                if (!found)
                    if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
                        return std::unexpected(r.error());
            }
            if (!found) [[unlikely]]
                return std::unexpected(error::key_not_found);
        }
        if constexpr (std::integral<T> && !std::is_same_v<T, bool>) {
            bool negative = h.major == major_type::negative_integer;
            std::uint64_t argument = h.argument;
            if (h.major == major_type::tag &&
                (h.argument == std::to_underlying(rfc8949::tag_number::unsigned_bignum) ||
                 h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum))) {
                negative = h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
                for (;;) {
                    heads::decoder const before = d;
                    auto const t = d.head_decode();
                    if (!t) [[unlikely]]
                        return std::unexpected(t.error());
                    if (t->major == major_type::tag &&
                        t->argument == std::to_underlying(rfc8949::tag_number::sharedref)) [[unlikely]]
                        return std::nullopt;
                    if (t->major != major_type::tag ||
                        (t->argument != std::to_underlying(rfc8949::tag_number::shareable) &&
                         t->argument != std::to_underlying(rfc8949::tag_number::self_described_cbor))) {
                        d = before;
                        break;
                    }
                }
                auto const r = d.head_decode();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (error const c = validity::check_tag_content(h.argument, r->major, r->info).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                auto const bytes = d.byte_string_decode(r->argument);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                std::string_view const magnitude = heads::magnitude_without_leading_zeros(*bytes);
                if (error const c = validity::check_magnitude_size(magnitude.size(), sizeof(std::uint64_t))
                                        .error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                argument = heads::magnitude_value(magnitude);
            } else if (h.major != major_type::unsigned_integer && !negative) [[unlikely]] {
                return std::unexpected(error::incorrect_type);
            }
            if (error const c = validity::check_number_range<T>(negative, argument).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            T const magnitude = static_cast<T>(argument);
            return static_cast<T>(negative ? ~magnitude : magnitude);
        } else if constexpr (std::is_same_v<T, double>) {
            if (h.major != major_type::simple_float) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            switch (static_cast<rfc8949::simple_float_information>(h.info)) {
            case rfc8949::simple_float_information::half_precision_float:
            case rfc8949::simple_float_information::single_precision_float:
            case rfc8949::simple_float_information::double_precision_float:
                return heads::float_decode(h.info, h.argument);
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
            if (error const c = validity::check_simple_value(h.info, h.argument).error_or(error{}); c != error{})
                [[unlikely]]
                return std::unexpected(c);
            return static_cast<simple_value>(h.argument);
        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            if (!heads::is_null(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return nullptr;
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            if (h.major != major_type::text_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const text = d.byte_string_decode(h.argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
            return *text;
        } else if constexpr (std::is_same_v<T, typed_array>) {
            if (h.major != major_type::tag) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (error const r = validity::typed_array_check(h.argument, 0).error_or(error{}); r != error{})
                [[unlikely]]
                return std::unexpected(r);
            for (;;) {
                heads::decoder const before = d;
                auto const t = d.head_decode();
                if (!t) [[unlikely]]
                    return std::unexpected(t.error());
                if (t->major == major_type::tag &&
                    t->argument == std::to_underlying(rfc8949::tag_number::sharedref)) [[unlikely]]
                    return std::nullopt;
                if (t->major != major_type::tag ||
                    (t->argument != std::to_underlying(rfc8949::tag_number::shareable) &&
                     t->argument != std::to_underlying(rfc8949::tag_number::self_described_cbor))) {
                    d = before;
                    break;
                }
            }
            auto const r = d.head_decode();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (error const c = validity::check_tag_content(h.argument, r->major, r->info).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = d.byte_string_decode(r->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            if (error const c = validity::typed_array_check(h.argument, bytes->size()).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            return typed_array{h.argument, std::as_bytes(std::span(*bytes))};
        } else {
            if (h.major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const bytes = d.byte_string_decode(h.argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            return std::as_bytes(std::span(*bytes));
        }
    }

    template <class T>
    static std::expected<T, error> query_walk(query_view const &v, parsed_query const &top, lazy const &root,
                                              std::size_t const depth_max)
    {
        lazy node = root;
        for (segment const &s : v.segments.subspan(top.segment_at, top.segment_count)) {
            selector const &each = v.selectors[s.selector_at];
            auto const child = each.kind == selector::kind::index
                                   ? index_select(node, each.index)
                                   : key_find(node, std::string_view(std::span(v.keys).subspan(each.key_at, each.key_size)), depth_max);
            if (!child) [[unlikely]]
                return std::unexpected(child.error());
            node = *child;
        }
        auto const value = node.get<T>();
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                      std::is_same_v<T, typed_array>)
            return **value;
        else
            return *value;
    }

    template <fixed_string Path, class T>
    static std::expected<T, error> query_walk(std::string_view const encoded)
    {
        constexpr auto q = [] {
            std::array const text = Path.value;
            return *query_parse(std::string_view(text.data(), text.size() - 1), true, validity::nesting_depth_default);
        };
        constexpr std::size_t segments = q().segments.size();
        constexpr std::size_t selectors = q().selectors.size();
        constexpr std::size_t keys = q().keys.size();
        constexpr auto top = q().top;
        constexpr auto compiled = [q] {
            auto const parsed = q();
            std::tuple<std::array<segment, segments>, std::array<selector, selectors>, std::array<char, keys>> c{};
            std::ranges::copy(parsed.segments, std::get<0>(c).begin());
            std::ranges::copy(parsed.selectors, std::get<1>(c).begin());
            std::ranges::copy(parsed.keys, std::get<2>(c).begin());
            return c;
        }();
        query_view const v{std::get<0>(compiled), std::get<1>(compiled), {}, std::string_view(std::get<2>(compiled).data(), keys), {}};
        if (auto const r = validity::check_input_bytes(encoded.size()); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::size_t const depth_max = limits.nesting_depth;
        if (auto const r = validity::check_nesting_depth(top.segment_count, depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::string_view const content = heads::self_described_cbor_content(encoded);
        auto const walked = query_walk<T>(v, top, content);
        if (walked) [[likely]]
            return *walked;
        return query_walk<T>(
            v, top,
            lazy{std::make_shared<value_sharing::top_level_item>(std::shared_ptr<void const>{}, content,
                                                                 std::vector<lazy>{}, 0),
                 0}, depth_max);
    }

    template <fixed_string Path, class T>
    static std::expected<owning_ref<T>, error> query_walk(std::shared_ptr<void const> owner,
                                                          std::string_view const encoded)
    {
        auto const r = query_walk<Path, T>(encoded);
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        return owning_ref<T>(std::move(owner), *r);
    }

    template <fixed_string>
    friend class is_singular_query;

    template <fixed_string Path, class T>
        requires(is_singular_query_v<Path> &&
                 ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
                  std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>))
    friend std::expected<T, error> at_path(std::string_view encoded);

    template <fixed_string Path, class T>
        requires(is_singular_query_v<Path> &&
                 (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                  std::is_same_v<T, typed_array>))
    friend std::expected<owning_ref<T>, error> at_path(std::shared_ptr<void const> owner,
                                                       std::string_view encoded);

    template <fixed_string>
    friend class is_valid_path;

    template <binding Binding>
    friend std::expected<typename Binding::value, error> at_path(Binding &binding, std::string_view path, lazy const &l);

    template <binding Binding>
    friend std::expected<typename Binding::value, error> query(Binding &binding, std::string_view path, lazy const &l);

    template <fixed_string Path, binding Binding>
        requires is_valid_path_v<Path>
    friend std::expected<typename Binding::value, error> at_path(Binding &binding, lazy const &l);

    template <fixed_string Path, binding Binding>
        requires is_valid_path_v<Path>
    friend std::expected<typename Binding::value, error> query(Binding &binding, lazy const &l);
};

template <fixed_string Path>
class is_valid_path
    : public std::bool_constant<
          jsonpath::query_parse(Path.view(), true, validity::nesting_depth_default).has_value()>
{
};

inline std::expected<lazy, error> jsonpath::index_select(lazy const &node, std::int64_t const index)
{
    auto found = value_sharing::container_resolve(node.top_level, node.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (found->h.major != major_type::array)
        return value_sharing::value_of(value_sharing::key_find(std::move(*found), index));
    auto &[source, h, d] = *found;
    auto const position = validity::check_index(index, h.argument);
    if (!position) [[unlikely]]
        return std::unexpected(position.error());
    well_formedness::no_marks none;
    for (std::uint64_t i = 0; i < *position; ++i)
        if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
            return std::unexpected(r.error());
    std::size_t const element = source->encoded.size() - d.encoded.size();
    return lazy{source, element};
}

inline std::expected<lazy, error> jsonpath::key_find(lazy const &node, std::string_view const key,
                                                   std::size_t const depth_max)
{
    heads::decoder text_key{key};
    auto const literal = text_key.head_decode();
    if (literal && literal->major == major_type::text_string)
        return node.at(text_key.encoded);
    auto const found = value_sharing::container_resolve(node.top_level, node.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    well_formedness::no_marks none;
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        std::size_t const start = source->encoded.size() - d.encoded.size();
        if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const match = validity::keys_equivalent(*source, start, key, 0, 0, depth_max);
        if (!match) [[unlikely]]
            return std::unexpected(match.error());
        if (*match)
            return lazy{source, source->encoded.size() - d.encoded.size()};
        if (auto const r = well_formedness::item_skip(d, none); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}

inline std::expected<bool, error> jsonpath::value_equal(lazy const &a, lazy const &b, std::size_t const depth,
                                                     std::size_t const depth_max)
{
    if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const x = value_sharing::container_resolve(a.top_level, a.offset);
    if (!x) [[unlikely]]
        return std::unexpected(x.error());
    auto const y = value_sharing::container_resolve(b.top_level, b.offset);
    if (!y) [[unlikely]]
        return std::unexpected(y.error());
    heads::head const &h = x->h;
    heads::head const &k = y->h;
    constexpr std::uint8_t half = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice =
        std::to_underlying(rfc8949::simple_float_information::double_precision_float);
    auto const integral = [](heads::head const &n) {
        return n.major == major_type::unsigned_integer || n.major == major_type::negative_integer;
    };
    auto const floating = [](heads::head const &n) {
        return n.major == major_type::simple_float && n.info >= half && n.info <= twice;
    };
    auto const number = [&](heads::head const &n) {
        if (floating(n))
            return heads::float_decode(n.info, n.argument);
        return n.major == major_type::unsigned_integer ? static_cast<double>(n.argument) : -1.0 - static_cast<double>(n.argument);
    };
    if (integral(h) && integral(k))
        return h.major == k.major && h.argument == k.argument;
    if ((integral(h) || floating(h)) && (integral(k) || floating(k)))
        return number(h) == number(k);
    if (h.major != k.major || floating(h) || floating(k))
        return false;
    std::size_t const x_content = x->source->encoded.size() - x->d.encoded.size();
    std::size_t const y_content = y->source->encoded.size() - y->d.encoded.size();
    switch (h.major) {
    case major_type::byte_string:
    case major_type::text_string: {
        heads::decoder d = x->d;
        heads::decoder e = y->d;
        auto const s = d.byte_string_decode(h.argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        auto const t = e.byte_string_decode(k.argument);
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
            auto const same = value_equal(*element, *other, depth + 1, depth_max);
            if (!same || !*same)
                return same;
            ++j;
        }
        return true;
    }
    case major_type::map: {
        if (h.argument != k.argument)
            return false;
        auto const left = a.entries();
        if (!left) [[unlikely]]
            return std::unexpected(left.error());
        auto const right = b.entries();
        if (!right) [[unlikely]]
            return std::unexpected(right.error());
        for (auto const entry : *left) {
            if (!entry) [[unlikely]]
                return std::unexpected(entry.error());
            std::size_t twins = 0;
            for (auto const mine : *left) {
                if (!mine) [[unlikely]]
                    return std::unexpected(mine.error());
                auto const same = value_equal(entry->first, mine->first, depth + 1, depth_max);
                if (!same) [[unlikely]]
                    return same;
                twins += *same ? 1uz : 0uz;
            }
            std::size_t paired = 0;
            bool values_same = false;
            for (auto const other : *right) {
                if (!other) [[unlikely]]
                    return std::unexpected(other.error());
                auto const same = value_equal(entry->first, other->first, depth + 1, depth_max);
                if (!same) [[unlikely]]
                    return same;
                if (!*same)
                    continue;
                ++paired;
                auto const value_same = value_equal(entry->second, other->second, depth + 1, depth_max);
                if (!value_same) [[unlikely]]
                    return value_same;
                values_same = *value_same;
            }
            if (twins != 1 || paired != 1 || !values_same)
                return false;
        }
        return true;
    }
    case major_type::tag:
        if (h.argument != k.argument)
            return false;
        return value_equal(lazy{x->source, x_content}, lazy{y->source, y_content}, depth + 1, depth_max);
    default:
        return h.info == k.info && h.argument == k.argument;
    }
}

inline std::expected<bool, error> jsonpath::value_less(lazy const &a, lazy const &b)
{
    auto const x = value_sharing::container_resolve(a.top_level, a.offset);
    if (!x) [[unlikely]]
        return std::unexpected(x.error());
    auto const y = value_sharing::container_resolve(b.top_level, b.offset);
    if (!y) [[unlikely]]
        return std::unexpected(y.error());
    heads::head const &h = x->h;
    heads::head const &k = y->h;
    constexpr std::uint8_t half = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice =
        std::to_underlying(rfc8949::simple_float_information::double_precision_float);
    auto const integral = [](heads::head const &n) {
        return n.major == major_type::unsigned_integer || n.major == major_type::negative_integer;
    };
    auto const floating = [](heads::head const &n) {
        return n.major == major_type::simple_float && n.info >= half && n.info <= twice;
    };
    auto const number = [&](heads::head const &n) {
        if (floating(n))
            return heads::float_decode(n.info, n.argument);
        return n.major == major_type::unsigned_integer ? static_cast<double>(n.argument) : -1.0 - static_cast<double>(n.argument);
    };
    if (integral(h) && integral(k)) {
        if (h.major != k.major)
            return h.major == major_type::negative_integer;
        return h.major == major_type::unsigned_integer ? h.argument < k.argument : h.argument > k.argument;
    }
    if ((integral(h) || floating(h)) && (integral(k) || floating(k)))
        return number(h) < number(k);
    if (h.major != major_type::text_string || k.major != major_type::text_string)
        return false;
    heads::decoder d = x->d;
    heads::decoder e = y->d;
    auto const s = d.byte_string_decode(h.argument);
    if (!s) [[unlikely]]
        return std::unexpected(s.error());
    auto const t = e.byte_string_decode(k.argument);
    if (!t) [[unlikely]]
        return std::unexpected(t.error());
    return std::ranges::lexicographical_compare(*s, *t, {}, [](char const c) { return static_cast<unsigned char>(c); },
                                                [](char const c) { return static_cast<unsigned char>(c); });
}

inline std::expected<std::optional<lazy>, error> jsonpath::comparable_value(query_view const &v, std::size_t const index,
                                                                     lazy const &current, lazy const &root,
                                                                     std::size_t const depth_max)
{
    expression const &e = v.expressions[index];
    auto const unsigned_integer = [](std::uint64_t const n) -> std::expected<std::optional<lazy>, error> {
        std::string encoded;
        if (auto const r = extended_diagnostic_notation::head_append(encoded, major_type::unsigned_integer, n, extended_diagnostic_notation::no_indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const l = lazy::from(std::move(encoded));
        if (!l) [[unlikely]]
            return std::unexpected(l.error());
        return *l;
    };
    switch (e.kind) {
    case expression::kind::literal: {
        auto const l = lazy::from(std::string_view(std::span(v.keys).subspan(e.first, e.second)));
        if (!l) [[unlikely]]
            return std::unexpected(l.error());
        return *l;
    }
    case expression::kind::query:
    case expression::kind::count:
    case expression::kind::value: {
        std::size_t const at = e.kind == expression::kind::query ? index : e.first;
        expression const &q = v.expressions[at];
        std::expected<std::vector<lazy>, error> relative;
        if (q.relative)
            relative = segments_apply(v, q.first, q.second, current, root, depth_max);
        auto const &nodes = q.relative ? relative : v.absolute_nodelists[at];
        if (!nodes) [[unlikely]]
            return std::unexpected(nodes.error());
        if (e.kind == expression::kind::count)
            return unsigned_integer(nodes->size());
        if (nodes->size() != 1)
            return std::nullopt;
        return nodes->front();
    }
    case expression::kind::length: {
        auto const argument = comparable_value(v, e.first, current, root, depth_max);
        if (!argument || !*argument) [[unlikely]]
            return argument;
        auto const found = value_sharing::container_resolve((*argument)->top_level, (*argument)->offset);
        if (!found) [[unlikely]]
            return std::unexpected(found.error());
        if (found->h.major == major_type::array || found->h.major == major_type::map)
            return unsigned_integer(found->h.argument);
        if (found->h.major != major_type::text_string)
            return std::nullopt;
        heads::decoder d = found->d;
        auto const s = d.byte_string_decode(found->h.argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        return unsigned_integer(static_cast<std::uint64_t>(
            std::ranges::count_if(*s, [](char const c) { return (static_cast<unsigned char>(c) & 0xc0) != 0x80; })));
    }
    [[unlikely]] default:
        return std::unexpected(error::invalid_path);
    }
}

inline std::expected<bool, error> jsonpath::expression_test(query_view const &v, std::size_t const index, lazy const &current,
                                                     lazy const &root, std::size_t const depth_max)
{
    std::size_t at = index;
    while (v.expressions[at].kind == expression::kind::logical_or || v.expressions[at].kind == expression::kind::logical_and) {
        expression const &chain = v.expressions[at];
        auto const left = expression_test(v, chain.first, current, root, depth_max);
        if (!left || *left == (chain.kind == expression::kind::logical_or))
            return left;
        at = chain.second;
    }
    expression const &e = v.expressions[at];
    switch (e.kind) {
    case expression::kind::logical_not: {
        auto const operand = expression_test(v, e.first, current, root, depth_max);
        if (!operand) [[unlikely]]
            return operand;
        return !*operand;
    }
    case expression::kind::query: {
        std::expected<std::vector<lazy>, error> relative;
        if (e.relative)
            relative = segments_apply(v, e.first, e.second, current, root, depth_max);
        auto const &nodes = e.relative ? relative : v.absolute_nodelists[at];
        if (!nodes) [[unlikely]]
            return std::unexpected(nodes.error());
        return !nodes->empty();
    }
    case expression::kind::comparison:
        break;
    [[unlikely]] default:
        return std::unexpected(error::invalid_path);
    }
    auto const a = comparable_value(v, e.first, current, root, depth_max);
    if (!a) [[unlikely]]
        return std::unexpected(a.error());
    auto const b = comparable_value(v, e.second, current, root, depth_max);
    if (!b) [[unlikely]]
        return std::unexpected(b.error());
    auto const equal = [&]() -> std::expected<bool, error> {
        if (!*a || !*b)
            return !*a && !*b;
        return value_equal(**a, **b, 0, depth_max);
    };
    auto const less = [](std::optional<lazy> const &x, std::optional<lazy> const &y) -> std::expected<bool, error> {
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

inline std::expected<void, error> jsonpath::selector_apply(query_view const &v, selector const &s, lazy const &node, lazy const &root,
                                                    std::vector<lazy> &nodelist, std::size_t const depth_max)
{
    std::size_t const limit = root.top_level->encoded.size();
    auto const append = [&nodelist, limit](lazy const &l) -> std::expected<void, error> {
        if (nodelist.size() >= limit) [[unlikely]]
            return std::unexpected(error::nodelist_too_long);
        nodelist.push_back(l);
        return {};
    };
    if (s.kind == selector::kind::key || s.kind == selector::kind::index) {
        auto const child = s.kind == selector::kind::index ? index_select(node, s.index)
                                                           : key_find(node, std::string_view(std::span(v.keys).subspan(s.key_at, s.key_size)), depth_max);
        if (child)
            return append(*child);
        if (child.error() != error::not_indexable && child.error() != error::index_out_of_bounds &&
            child.error() != error::key_not_found) [[unlikely]]
            return std::unexpected(child.error());
        return {};
    }
    auto const found = value_sharing::container_resolve(node.top_level, node.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    std::vector<lazy> children;
    if (found->h.major == major_type::array) {
        auto const elements = node.elements();
        if (!elements) [[unlikely]]
            return std::unexpected(elements.error());
        for (auto const element : *elements) {
            if (!element) [[unlikely]]
                return std::unexpected(element.error());
            children.push_back(*element);
        }
    } else if (found->h.major == major_type::map && s.kind != selector::kind::slice) {
        auto const entries = node.entries();
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
            auto const chosen = expression_test(v, s.expression, child, root, depth_max);
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

inline std::expected<void, error> jsonpath::segment_apply(query_view const &v, segment const &s, lazy const &node, lazy const &root,
                                                       std::vector<lazy> &nodelist, std::size_t const depth,
                                                       std::size_t const depth_max)
{
    if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    for (selector const &each : v.selectors.subspan(s.selector_at, s.selector_count))
        if (auto const r = selector_apply(v, each, node, root, nodelist, depth_max); !r) [[unlikely]]
            return r;
    if (!s.descendant)
        return {};
    std::vector<lazy> children;
    selector const wildcard{selector::kind::wildcard, 0, 0, 0, std::nullopt, std::nullopt, 1, 0};
    if (auto const r = selector_apply(v, wildcard, node, root, children, depth_max); !r) [[unlikely]]
        return r;
    for (lazy const &child : children)
        if (auto const r = segment_apply(v, s, child, root, nodelist, depth + 1, depth_max); !r) [[unlikely]]
            return r;
    return {};
}

inline std::expected<std::vector<lazy>, error> jsonpath::segments_apply(query_view const &v, std::size_t const segment_at,
                                                                 std::size_t const segment_count, lazy const &start, lazy const &root,
                                                                 std::size_t const depth_max)
{
    std::vector<lazy> nodes{start};
    std::vector<lazy> next;
    for (segment const &s : v.segments.subspan(segment_at, segment_count)) {
        next.clear();
        for (lazy const &node : nodes)
            if (auto const r = segment_apply(v, s, node, root, next, 0, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
        std::swap(nodes, next);
    }
    return nodes;
}

template <class Binding>
std::expected<typename Binding::value, error> jsonpath::singular_query_walk(Binding &binding, query_view const &v,
                                                                            parsed_query const &top, lazy const &root,
                                                                            std::size_t const depth_max)
{
    validity::throw_logic_error_if_null(root.top_level, "cbor::at_path: the lazy holds no top-level item");
    lazy node = root;
    for (segment const &s : v.segments.subspan(top.segment_at, top.segment_count)) {
        selector const &each = v.selectors[s.selector_at];
        auto const child = each.kind == selector::kind::index
                               ? index_select(node, each.index)
                               : key_find(node, std::string_view(std::span(v.keys).subspan(each.key_at, each.key_size)), depth_max);
        if (!child) [[unlikely]]
            return std::unexpected(child.error());
        node = *child;
    }
    auto value = lazy_decode(binding, node);
    if (!value) [[unlikely]]
        return std::unexpected(value.error());
    return std::move(*value);
}

template <class Binding>
std::expected<typename Binding::value, error> jsonpath::query_walk(Binding &binding, query_view const &v, parsed_query const &top,
                                                            lazy const &root, std::size_t const depth_max)
{
    validity::throw_logic_error_if_null(root.top_level, "cbor::query: the lazy holds no top-level item");
    std::vector<std::expected<std::vector<lazy>, error>> absolute_nodelists(v.expressions.size());
    query_view const with_nodelists{v.segments, v.selectors, v.expressions, v.keys, absolute_nodelists};
    for (std::size_t i = 0; i < v.expressions.size(); ++i)
        if (v.expressions[i].kind == expression::kind::query && !v.expressions[i].relative)
            absolute_nodelists[i] =
                segments_apply(with_nodelists, v.expressions[i].first, v.expressions[i].second, root, root, depth_max);
    auto const nodes = segments_apply(with_nodelists, top.segment_at, top.segment_count, root, root, depth_max);
    if (!nodes) [[unlikely]]
        return std::unexpected(nodes.error());
    auto array = binding.array_decode(nodes->size());
    for (lazy const &node : *nodes) {
        auto value = lazy_decode(binding, node);
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        array = binding.array_append(std::move(array), std::move(*value));
    }
    return array;
}

template <fixed_string Path, class Walk>
auto jsonpath::compiled_apply(Walk const &walk)
{
    constexpr auto q = [] { return *query_parse(Path.view(), true, validity::nesting_depth_default); };
    constexpr std::size_t segments = q().segments.size();
    constexpr std::size_t selectors = q().selectors.size();
    constexpr std::size_t expressions = q().expressions.size();
    constexpr std::size_t keys = q().keys.size();
    constexpr auto top = q().top;
    constexpr auto compiled = [q] {
        auto const parsed = q();
        std::tuple<std::array<segment, segments>, std::array<selector, selectors>,
                   std::array<expression, expressions>, std::array<char, keys>>
            c{};
        std::ranges::copy(parsed.segments, std::get<0>(c).begin());
        std::ranges::copy(parsed.selectors, std::get<1>(c).begin());
        std::ranges::copy(parsed.expressions, std::get<2>(c).begin());
        std::ranges::copy(parsed.keys, std::get<3>(c).begin());
        return c;
    }();
    using result = decltype(walk(query_view{}, top, std::size_t{}));
    std::size_t const depth_max = limits.nesting_depth;
    if (auto const r = validity::check_nesting_depth(top.segment_count, depth_max); !r) [[unlikely]]
        return result(std::unexpect, r.error());
    return walk(query_view{std::get<0>(compiled), std::get<1>(compiled), std::get<2>(compiled),
                           std::string_view(std::get<3>(compiled).data(), keys), {}},
                top, depth_max);
}

template <binding Binding>
std::expected<typename Binding::value, error> at_path(Binding &binding, std::string_view const path, lazy const &l)
{
    std::size_t const depth_max = limits.nesting_depth;
    auto const q = jsonpath::query_parse(path, false, depth_max);
    if (!q) [[unlikely]]
        return std::unexpected(q.error());
    if (!q->top.singular) [[unlikely]]
        return std::unexpected(error::invalid_path);
    return jsonpath::singular_query_walk(binding, {q->segments, q->selectors, q->expressions, q->keys, {}}, q->top, l,
                                         depth_max);
}

template <binding Binding>
std::expected<typename Binding::value, error> query(Binding &binding, std::string_view const path, lazy const &l)
{
    std::size_t const depth_max = limits.nesting_depth;
    auto const q = jsonpath::query_parse(path, false, depth_max);
    if (!q) [[unlikely]]
        return std::unexpected(q.error());
    return jsonpath::query_walk(binding, {q->segments, q->selectors, q->expressions, q->keys, {}}, q->top, l, depth_max);
}

template <fixed_string Path, binding Binding>
    requires is_valid_path_v<Path>
std::expected<typename Binding::value, error> at_path(Binding &binding, lazy const &l)
{
    static_assert(jsonpath::query_parse(Path.view(), true, validity::nesting_depth_default)->top.singular,
                  "cbor::at_path: the path is not a singular query (RFC 9535 2.3.5.1); cbor::query reads its nodelist");
    return jsonpath::compiled_apply<Path>(
        [&binding, &l](jsonpath::query_view const &v, jsonpath::parsed_query const &top, std::size_t const depth_max) {
            return jsonpath::singular_query_walk(binding, v, top, l, depth_max);
        });
}

template <fixed_string Path, binding Binding>
    requires is_valid_path_v<Path>
std::expected<typename Binding::value, error> query(Binding &binding, lazy const &l)
{
    return jsonpath::compiled_apply<Path>(
        [&binding, &l](jsonpath::query_view const &v, jsonpath::parsed_query const &top, std::size_t const depth_max) {
            return jsonpath::query_walk(binding, v, top, l, depth_max);
        });
}

template <fixed_string Path>
class is_singular_query
    : public std::bool_constant<
          [] {
    std::array const text = Path.value;
    auto const q = jsonpath::query_parse(std::string_view(text.data(), text.size() - 1), true, validity::nesting_depth_default);
    return q.has_value() && q->top.singular &&
           std::ranges::all_of(q->selectors, [&q](jsonpath::selector const &s) {
               return s.kind == jsonpath::selector::kind::index ||
                      (s.kind == jsonpath::selector::kind::key &&
                       static_cast<major_type>(static_cast<std::uint8_t>(q->keys[s.key_at]) >> rfc8949::additional_information_bits) == major_type::text_string);
           });
}()>
{
};

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
              std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>))
std::expected<T, error> at_path(std::string_view const encoded)
{
    return jsonpath::query_walk<Path, T>(encoded);
}

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<owning_ref<T>, error> at_path(std::shared_ptr<void const> owner, std::string_view const encoded)
{
    validity::throw_logic_error_if_empty(owner, "cbor::at_path: the owner of the encoded data item is empty");
    return jsonpath::query_walk<Path, T>(std::move(owner), encoded);
}

}
