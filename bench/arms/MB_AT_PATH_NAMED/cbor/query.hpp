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
#include "item_end.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "inspect.hpp"
#include "lazy.hpp"
#include "owning_ref.hpp"
#include "shared.hpp"

namespace cbor
{

struct lazy;

template <std::size_t DepthMax = validity::nesting_depth_default, class Binding>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<typename Binding::value, error> at_path(Binding &binding, std::string_view path, lazy const &l);

template <fixed_string Path, std::size_t DepthMax>
class verify_path;

template <fixed_string Path, std::size_t DepthMax = validity::nesting_depth_default, class Binding>
    requires(verify_path<Path, DepthMax>::value)
std::expected<typename Binding::value, error> at_path(Binding &binding, lazy const &l);

template <fixed_string Path, std::size_t DepthMax>
class singular_query;

template <fixed_string Path, class T, std::size_t DepthMax = validity::nesting_depth_default>
    requires(singular_query<Path, DepthMax>::value &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
              std::is_same_v<T, std::nullptr_t>))
std::expected<T, error> at_path(std::string_view encoded);

template <fixed_string Path, class T, std::size_t DepthMax = validity::nesting_depth_default>
    requires(singular_query<Path, DepthMax>::value &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<owning_ref<T>, error> at_path(std::shared_ptr<void const> owner, std::string_view encoded);

template <fixed_string Path, class T, std::size_t DepthMax = validity::nesting_depth_default, class Encoded>
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
    };

    static constexpr bool name_first(char const c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
    }

    static constexpr bool function_name_char(char const c)
    {
        return (c >= 'a' && c <= 'z') || c == '_' || diagnostic_notation::digit(c);
    }

    static constexpr std::expected<std::optional<integer>, error> int_read(std::string_view const text, std::size_t const at)
    {
        if (at >= text.size())
            return std::nullopt;
        bool const negative = text[at] == '-';
        std::size_t const digits_at = at + (negative ? 1 : 0);
        std::size_t digits_end = digits_at;
        while (digits_end < text.size() && diagnostic_notation::digit(text[digits_end]))
            ++digits_end;
        if (digits_end == digits_at || digits_end - digits_at > 16 ||
            (text[digits_at] == '0' && (digits_end > digits_at + 1 || negative)))
            return std::nullopt;
        std::int64_t value = 0;
        for (char const d : std::span(text).subspan(digits_at, digits_end - digits_at))
            value = value * 10 + (d - '0');
        constexpr std::int64_t exact_max = (std::int64_t{1} << 53) - 1;
        if (value > exact_max) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return integer{negative ? -value : value, digits_end};
    }

    static constexpr std::optional<std::size_t> number_end(std::string_view const text, std::size_t at)
    {
        auto const digits = [&text](std::size_t i) {
            while (i < text.size() && diagnostic_notation::digit(text[i]))
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

    struct query {
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
            auto const next = diagnostic_notation::quoted_parse(text, at, name);
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
            if (auto const r = diagnostic_notation::head_append(keys, major_type::text_string, name.size(), diagnostic_notation::no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            keys += name;
            return next;
        }

        constexpr std::expected<std::size_t, error> literal_parse(std::string_view const text, std::size_t const at)
        {
            std::string literal;
            auto const next = diagnostic_notation::literal_parse({text, at}, literal, {0, depth_max});
            if (!next) [[unlikely]]
                return next;
            if (auto const r = diagnostic_notation::canonical_append(keys, {literal, 0}, {0, depth_max}); !r) [[unlikely]]
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
                auto const e = logical_or_parse(text, diagnostic_notation::blank_end(text, at + 1), depth + 1);
                if (!e) [[unlikely]]
                    return std::unexpected(e.error());
                s.kind = selector::kind::filter;
                s.expression = e->index;
                return std::pair{e->at, s};
            }
            auto const first = int_read(text, at);
            if (!first) [[unlikely]]
                return std::unexpected(first.error());
            std::size_t next = *first ? diagnostic_notation::blank_end(text, (*first)->at) : at;
            if (next < text.size() && text[next] == ':') {
                s.kind = selector::kind::slice;
                if (*first)
                    s.start = (*first)->value;
                next = diagnostic_notation::blank_end(text, next + 1);
                auto const end = int_read(text, next);
                if (!end) [[unlikely]]
                    return std::unexpected(end.error());
                if (*end) {
                    s.end = (*end)->value;
                    next = diagnostic_notation::blank_end(text, (*end)->at);
                }
                if (next < text.size() && text[next] == ':') {
                    next = diagnostic_notation::blank_end(text, next + 1);
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
                at = diagnostic_notation::blank_end(text, at + 1);
                if (at >= text.size()) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                auto const s = selector_parse(text, at, depth);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                chosen.push_back(s->second);
                at = diagnostic_notation::blank_end(text, s->first);
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
                std::size_t const next = diagnostic_notation::blank_end(text, at);
                if (next >= text.size() || (text[next] != '.' && text[next] != '['))
                    break;
                at = next;
                segment s{false, found_selectors.size(), 0};
                std::vector<selector> chosen;
                if (text[at] == '[' || std::ranges::starts_with(std::span(text).subspan(at), std::string_view("..["))) {
                    s.descendant = text[at] == '.';
                    auto const b = bracketed_parse(text, at + (s.descendant ? 2 : 0), depth);
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
                        while (end < text.size() && (name_first(text[end]) || (end != at && diagnostic_notation::digit(text[end]))))
                            ++end;
                        if (end == at) [[unlikely]]
                            return std::unexpected(error::invalid_path);
                        std::size_t const key_at = keys.size();
                        if (auto const r = diagnostic_notation::head_append(keys, major_type::text_string, end - at, diagnostic_notation::no_indicator); !r) [[unlikely]]
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
            std::size_t next = diagnostic_notation::blank_end(text, at + name.size() + 1);
            if (next < text.size() && text[next] == ')')
                ++next;
            else
                for (;;) {
                    auto const a = primary_parse(text, next, depth + 1);
                    if (!a) [[unlikely]]
                        return a;
                    arguments.push_back(a->index);
                    next = diagnostic_notation::blank_end(text, a->at);
                    if (next >= text.size() || (text[next] != ',' && text[next] != ')')) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    if (text[next++] == ')')
                        break;
                    next = diagnostic_notation::blank_end(text, next);
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
                    constexpr std::array<std::string_view, 3> names{"false", "true", "null"};
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
                auto const next = diagnostic_notation::number_parse(text, at, number);
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
            constexpr std::array<std::pair<std::string_view, comparison_op>, 6> ops{
                {{"==", comparison_op::equal},
                 {"!=", comparison_op::not_equal},
                 {"<=", comparison_op::less_equal},
                 {">=", comparison_op::greater_equal},
                 {"<", comparison_op::less},
                 {">", comparison_op::greater}}};
            for (auto const &[token, op] : ops)
                if (std::ranges::starts_with(std::span(text).subspan(at), token))
                    return std::pair{op, token.size()};
            return std::nullopt;
        }

        constexpr std::expected<parsed_expression, error> paren_parse(std::string_view const text, std::size_t const at, std::size_t const depth)
        {
            auto const inner = logical_or_parse(text, diagnostic_notation::blank_end(text, at + 1), depth + 1);
            if (!inner) [[unlikely]]
                return inner;
            std::size_t const close = diagnostic_notation::blank_end(text, inner->at);
            if (close >= text.size() || text[close] != ')') [[unlikely]]
                return std::unexpected(error::invalid_path);
            return parsed_expression{close + 1, inner->index};
        }

        constexpr std::expected<parsed_expression, error> basic_parse(std::string_view const text, std::size_t const at, std::size_t const depth)
        {
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (text[at] == '!') {
                std::size_t const next = diagnostic_notation::blank_end(text, at + 1);
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
            std::size_t const next = diagnostic_notation::blank_end(text, left->at);
            auto const op = comparison_op_read(text, next);
            if (!op) {
                if (expressions[left->index].kind != expression::kind::query) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                return left;
            }
            auto const right = primary_parse(text, diagnostic_notation::blank_end(text, next + op->second), depth);
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
            auto left = basic_parse(text, at, depth);
            for (;;) {
                if (!left) [[unlikely]]
                    return left;
                std::size_t const next = diagnostic_notation::blank_end(text, left->at);
                if (!std::ranges::starts_with(std::span(text).subspan(next), std::string_view("&&")))
                    return left;
                auto const right = basic_parse(text, diagnostic_notation::blank_end(text, next + 2), depth);
                if (!right) [[unlikely]]
                    return right;
                left = parsed_expression{right->at, expression_add({expression::kind::logical_and, left->index, right->index,
                                                                    comparison_op::equal, false, false})};
            }
        }

        constexpr std::expected<parsed_expression, error> logical_or_parse(std::string_view const text, std::size_t const at,
                                                                           std::size_t const depth)
        {
            if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto left = logical_and_parse(text, at, depth);
            for (;;) {
                if (!left) [[unlikely]]
                    return left;
                std::size_t const next = diagnostic_notation::blank_end(text, left->at);
                if (!std::ranges::starts_with(std::span(text).subspan(next), std::string_view("||")))
                    return left;
                auto const right = logical_and_parse(text, diagnostic_notation::blank_end(text, next + 2), depth);
                if (!right) [[unlikely]]
                    return right;
                left = parsed_expression{right->at, expression_add({expression::kind::logical_or, left->index, right->index,
                                                                    comparison_op::equal, false, false})};
            }
        }
    };

    static constexpr std::expected<query, error> query_parse(std::string_view const text, bool const literals,
                                                             std::size_t const depth_max)
    {
        query q{{}, {}, {}, {}, literals, depth_max, {}};
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

    template <std::size_t DepthMax>
    static std::expected<lazy, error> key_find(lazy const &node, std::string_view key);

    static constexpr std::expected<std::size_t, error> literal_end(diagnostic_notation::literal_cursor const cursor,
                                                                   diagnostic_notation::nesting const n)
    {
        if (auto const r = validity::check_nesting_depth(n.depth, n.depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::string_view const literal = cursor.text;
        auto const h = heads::raw_head_read(literal, cursor.at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        std::size_t next = h->at;
        switch (h->major) {
        case major_type::byte_string:
        case major_type::text_string: {
            heads::decoder d{std::string_view(std::span(literal).subspan(next))};
            auto const s = d.byte_string_decode(h->argument);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            return next + s->size();
        }
        case major_type::array:
        case major_type::map:
            for (std::uint64_t i = 0; i < (h->major == major_type::map ? 2 : 1) * h->argument; ++i) {
                auto const end = literal_end({literal, next}, {n.depth + 1, n.depth_max});
                if (!end) [[unlikely]]
                    return end;
                next = *end;
            }
            return next;
        case major_type::tag:
            return literal_end({literal, next}, {n.depth + 1, n.depth_max});
        default:
            return next;
        }
    }

    template <std::size_t DepthMax>
    static std::expected<std::size_t, error> top_level_item_end(value_sharing::top_level_item &top_level, std::size_t at, std::size_t depth);

    template <std::size_t DepthMax>
    static std::expected<bool, error> key_equal(value_sharing::top_level_item &top_level, std::size_t at,
                                                diagnostic_notation::literal_cursor literal, std::size_t depth);

    template <std::size_t DepthMax>
    static std::expected<bool, error> value_equal(lazy const &a, lazy const &b, std::size_t depth);

    template <std::size_t DepthMax>
    static std::expected<bool, error> value_less(lazy const &a, lazy const &b);

    template <std::size_t DepthMax>
    static std::expected<std::optional<lazy>, error> comparable_value(query_view const &v, std::size_t index, lazy const &current,
                                                                      lazy const &root);

    template <std::size_t DepthMax>
    static std::expected<bool, error> expression_test(query_view const &v, std::size_t index, lazy const &current, lazy const &root);

    template <std::size_t DepthMax>
    static std::expected<void, error> selector_apply(query_view const &v, selector const &s, lazy const &node, lazy const &root,
                                                     std::vector<lazy> &nodelist);

    template <std::size_t DepthMax>
    static std::expected<void, error> segment_apply(query_view const &v, segment const &s, lazy const &node, lazy const &root,
                                                        std::vector<lazy> &nodelist, std::size_t depth);

    template <std::size_t DepthMax>
    static std::expected<std::vector<lazy>, error> segments_apply(query_view const &v, std::size_t segment_at, std::size_t segment_count,
                                                                  lazy const &start, lazy const &root);

    template <std::size_t DepthMax, class Binding>
    static std::expected<typename Binding::value, error> query_walk(Binding &binding, query_view const &v, parsed_query const &top,
                                                             lazy const &root);

    static std::expected<std::optional<heads::decoder>, error> sharedref_find(heads::decoder d)
    {
        for (;;) {
            heads::decoder const before = d;
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                return before;
            if (h->argument == std::to_underlying(heads::tag_number::sharedref)) [[unlikely]]
                return std::nullopt;
            if (h->argument != std::to_underlying(heads::tag_number::shareable))
                return before;
        }
    }

    template <std::size_t DepthMax, class T>
    static std::optional<std::expected<T, error>> query_walk(query_view const &v, parsed_query const &top,
                                                             std::string_view const encoded)
    {
        heads::decoder d{encoded};
        heads::head h{};
        std::size_t step = 0;
        well_formedness::no_marks none;
        for (;;) {
            for (;;) {
                auto const at = sharedref_find(d);
                if (!at) [[unlikely]]
                    return std::unexpected(at.error());
                if (!*at) [[unlikely]]
                    return std::nullopt;
                d = **at;
                auto const c = d.head_decode();
                if (!c) [[unlikely]]
                    return std::unexpected(c.error());
                h = *c;
                if (h.major != major_type::tag || h.argument != std::to_underlying(heads::tag_number::encoded_cbor_data_item))
                    break;
                auto const r = d.head_decode();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (error const content = validity::check_tag_content(h.argument, r->major).error_or(error{});
                    content != error{}) [[unlikely]]
                    return std::unexpected(content);
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
                    if (auto const r = well_formedness::item_skip<DepthMax>(d, none, 1); !r) [[unlikely]]
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
                auto const key_at = sharedref_find(d);
                if (!key_at) [[unlikely]]
                    return std::unexpected(key_at.error());
                if (!*key_at) [[unlikely]]
                    return std::nullopt;
                heads::decoder probe = **key_at;
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
                if (auto const r = well_formedness::item_skip<DepthMax>(d, none, 1); !r) [[unlikely]]
                    return std::unexpected(r.error());
                if (!found)
                    if (auto const r = well_formedness::item_skip<DepthMax>(d, none, 1); !r) [[unlikely]]
                        return std::unexpected(r.error());
            }
            if (!found) [[unlikely]]
                return std::unexpected(error::key_not_found);
        }
        if constexpr (std::integral<T> && !std::is_same_v<T, bool>) {
            bool negative = h.major == major_type::negative_integer;
            std::uint64_t argument = h.argument;
            if (h.major == major_type::tag &&
                (h.argument == std::to_underlying(heads::tag_number::unsigned_bignum) ||
                 h.argument == std::to_underlying(heads::tag_number::negative_bignum))) {
                negative = h.argument == std::to_underlying(heads::tag_number::negative_bignum);
                auto const content = sharedref_find(d);
                if (!content) [[unlikely]]
                    return std::unexpected(content.error());
                if (!*content) [[unlikely]]
                    return std::nullopt;
                d = **content;
                auto const r = d.head_decode();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (error const c = validity::check_tag_content(h.argument, r->major).error_or(error{});
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
            switch (static_cast<heads::simple_float_information>(h.info)) {
            case heads::simple_float_information::half_precision_float:
            case heads::simple_float_information::single_precision_float:
            case heads::simple_float_information::double_precision_float:
                return heads::float_decode(h.info, h.argument);
            [[unlikely]] default:
                return std::unexpected(error::incorrect_type);
            }
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!heads::is_boolean(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return h.info == std::to_underlying(simple_value::true_value);
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
            auto const content = sharedref_find(d);
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            if (!*content) [[unlikely]]
                return std::nullopt;
            d = **content;
            auto const r = d.head_decode();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (error const c = validity::check_tag_content(h.argument, r->major).error_or(error{});
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

    template <std::size_t DepthMax, class T>
    static std::expected<T, error> query_walk(query_view const &v, parsed_query const &top, lazy const &root)
    {
        lazy node = root;
        for (segment const &s : v.segments.subspan(top.segment_at, top.segment_count)) {
            selector const &each = v.selectors[s.selector_at];
            auto const child = each.kind == selector::kind::index
                                   ? node.at<DepthMax>(each.index)
                                   : key_find<DepthMax>(node, std::string_view(std::span(v.keys).subspan(each.key_at, each.key_size)));
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

    template <fixed_string Path, std::size_t DepthMax, class T>
    static std::expected<T, error> query_walk(std::string_view const encoded)
    {
        constexpr auto q = [] {
            std::array const text = Path.value;
            return *query_parse(std::string_view(text.data(), text.size() - 1), true, DepthMax);
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
        query_view const v{std::get<0>(compiled), std::get<1>(compiled), {}, std::string_view(std::get<2>(compiled).data(), keys)};
        auto const walked = query_walk<DepthMax, T>(v, top, encoded);
        if (walked) [[likely]]
            return *walked;
        return query_walk<DepthMax, T>(
            v, top,
            lazy{std::make_shared<value_sharing::top_level_item>(std::shared_ptr<void const>{}, encoded, std::vector<lazy>{}, 0), 0});
    }

    template <fixed_string Path, std::size_t DepthMax, class T>
    static std::expected<owning_ref<T>, error> query_walk(std::shared_ptr<void const> owner,
                                                          std::string_view const encoded)
    {
        auto const r = query_walk<Path, DepthMax, T>(encoded);
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        return owning_ref<T>(std::move(owner), *r);
    }

    template <fixed_string, std::size_t>
    friend class singular_query;

    template <fixed_string Path, class T, std::size_t DepthMax>
        requires(singular_query<Path, DepthMax>::value &&
                 ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
                  std::is_same_v<T, std::nullptr_t>))
    friend std::expected<T, error> at_path(std::string_view encoded);

    template <fixed_string Path, class T, std::size_t DepthMax>
        requires(singular_query<Path, DepthMax>::value &&
                 (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                  std::is_same_v<T, typed_array>))
    friend std::expected<owning_ref<T>, error> at_path(std::shared_ptr<void const> owner,
                                                       std::string_view encoded);

    template <fixed_string, std::size_t>
    friend class verify_path;

    template <std::size_t DepthMax, class Binding>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    friend std::expected<typename Binding::value, error> at_path(Binding &binding, std::string_view path, lazy const &l);

    template <fixed_string Path, std::size_t DepthMax, class Binding>
        requires(verify_path<Path, DepthMax>::value)
    friend std::expected<typename Binding::value, error> at_path(Binding &binding, lazy const &l);
};

template <fixed_string Path, std::size_t DepthMax>
class verify_path
    : public std::bool_constant<
          validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value() &&
          jsonpath::query_parse(Path.view(), true, DepthMax).has_value()>
{
};

template <std::size_t DepthMax>
std::expected<std::size_t, error> jsonpath::top_level_item_end(value_sharing::top_level_item &top_level, std::size_t const at, std::size_t const depth)
{
    heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(at))};
    if (auto const r = well_formedness::item_skip<DepthMax>(d, top_level, depth); !r) [[unlikely]]
        return std::unexpected(r.error());
    return top_level.encoded.size() - d.encoded.size();
}

template <std::size_t DepthMax>
std::expected<bool, error> jsonpath::key_equal(value_sharing::top_level_item &top_level, std::size_t const start,
                                               diagnostic_notation::literal_cursor const cursor, std::size_t const depth)
{
    std::string_view const literal = cursor.text;
    if (auto const r = validity::check_nesting_depth(depth, DepthMax); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const at = value_sharing::shared_resolve(top_level, start);
    if (!at) [[unlikely]]
        return std::unexpected(at.error());
    auto const h = heads::raw_head_read(top_level.encoded, *at);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    auto const l = heads::raw_head_read(literal, cursor.at);
    if (!l) [[unlikely]]
        return std::unexpected(l.error());
    if (error const c = validity::check_definite_length(h->major, h->info).error_or(error{}); c != error{})
        [[unlikely]]
        return std::unexpected(c);
    if (h->major != l->major)
        return false;
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return h->argument == l->argument;
    case major_type::byte_string:
    case major_type::text_string: {
        heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(h->at))};
        auto const s = d.byte_string_decode(h->argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        return h->argument == l->argument && *s == std::string_view(std::span(literal).subspan(l->at, static_cast<std::size_t>(l->argument)));
    }
    case major_type::array: {
        if (h->argument != l->argument)
            return false;
        std::size_t d = h->at;
        std::size_t k = l->at;
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const equal = key_equal<DepthMax>(top_level, d, {literal, k}, depth + 1);
            if (!equal || !*equal)
                return equal;
            auto const d_end = top_level_item_end<DepthMax>(top_level, d, depth + 1);
            if (!d_end) [[unlikely]]
                return std::unexpected(d_end.error());
            auto const k_end = literal_end({literal, k}, {depth + 1, DepthMax});
            if (!k_end) [[unlikely]]
                return std::unexpected(k_end.error());
            d = *d_end;
            k = *k_end;
        }
        return true;
    }
    case major_type::map: {
        if (h->argument != l->argument)
            return false;
        std::size_t d = h->at;
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const value_at = top_level_item_end<DepthMax>(top_level, d, depth + 1);
            if (!value_at) [[unlikely]]
                return std::unexpected(value_at.error());
            auto const pair_end = top_level_item_end<DepthMax>(top_level, *value_at, depth + 1);
            if (!pair_end) [[unlikely]]
                return std::unexpected(pair_end.error());
            bool paired = false;
            std::size_t k = l->at;
            for (std::uint64_t j = 0; j < l->argument && !paired; ++j) {
                auto const k_value = literal_end({literal, k}, {depth + 1, DepthMax});
                if (!k_value) [[unlikely]]
                    return std::unexpected(k_value.error());
                auto const k_end = literal_end({literal, *k_value}, {depth + 1, DepthMax});
                if (!k_end) [[unlikely]]
                    return std::unexpected(k_end.error());
                auto const key_same = key_equal<DepthMax>(top_level, d, {literal, k}, depth + 1);
                if (!key_same) [[unlikely]]
                    return key_same;
                if (*key_same) {
                    for (std::size_t earlier = h->at; earlier < d;) {
                        auto const twin = key_equal<DepthMax>(top_level, earlier, {literal, k}, depth + 1);
                        if (!twin) [[unlikely]]
                            return twin;
                        if (*twin) [[unlikely]]
                            return std::unexpected(error::duplicate_key);
                        auto const earlier_value = top_level_item_end<DepthMax>(top_level, earlier, depth + 1);
                        if (!earlier_value) [[unlikely]]
                            return std::unexpected(earlier_value.error());
                        auto const earlier_end = top_level_item_end<DepthMax>(top_level, *earlier_value, depth + 1);
                        if (!earlier_end) [[unlikely]]
                            return std::unexpected(earlier_end.error());
                        earlier = *earlier_end;
                    }
                    auto const value_same = key_equal<DepthMax>(top_level, *value_at, {literal, *k_value}, depth + 1);
                    if (!value_same) [[unlikely]]
                        return value_same;
                    paired = *value_same;
                }
                k = *k_end;
            }
            if (!paired)
                return false;
            d = *pair_end;
        }
        return true;
    }
    case major_type::tag:
        if (h->argument != l->argument)
            return false;
        return key_equal<DepthMax>(top_level, h->at, {literal, l->at}, depth + 1);
    default:
        break;
    }
    constexpr std::uint8_t half = std::to_underlying(heads::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice = std::to_underlying(heads::simple_float_information::double_precision_float);
    bool const h_float = h->info >= half && h->info <= twice;
    bool const l_float = l->info >= half && l->info <= twice;
    if (h_float != l_float)
        return false;
    if (!h_float)
        return h->argument == l->argument;
    heads::float_key const a = heads::float_key_of(h->info, h->argument);
    heads::float_key const b = heads::float_key_of(l->info, l->argument);
    if (a.nan || b.nan)
        return a.nan && b.nan && a.widened == b.widened;
    return a.value == b.value;
}

template <std::size_t DepthMax>
std::expected<lazy, error> jsonpath::key_find(lazy const &node, std::string_view const key)
{
    heads::decoder text_key{key};
    auto const literal = text_key.head_decode();
    if (literal && literal->major == major_type::text_string)
        return node.at<DepthMax>(text_key.encoded);
    auto const found = value_sharing::container_resolve(node.top_level, node.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        std::size_t const start = source->encoded.size() - d.encoded.size();
        auto const key_at = value_sharing::shared_resolve(*source, start);
        if (!key_at) [[unlikely]]
            return std::unexpected(key_at.error());
        heads::decoder look{std::string_view(std::span(source->encoded).subspan(*key_at))};
        if (auto const k = look.head_decode(); !k) [[unlikely]]
            return std::unexpected(k.error());
        if (auto const r = well_formedness::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const match = key_equal<DepthMax>(*source, start, {key, 0}, 0);
        if (!match) [[unlikely]]
            return std::unexpected(match.error());
        if (*match)
            return lazy{source, source->encoded.size() - d.encoded.size()};
        if (auto const r = well_formedness::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}

template <std::size_t DepthMax>
std::expected<bool, error> jsonpath::value_equal(lazy const &a, lazy const &b, std::size_t const depth)
{
    if (auto const r = validity::check_nesting_depth(depth, DepthMax); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const x = value_sharing::container_resolve(a.top_level, a.offset);
    if (!x) [[unlikely]]
        return std::unexpected(x.error());
    auto const y = value_sharing::container_resolve(b.top_level, b.offset);
    if (!y) [[unlikely]]
        return std::unexpected(y.error());
    heads::head const &h = x->h;
    heads::head const &k = y->h;
    constexpr std::uint8_t half = std::to_underlying(heads::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice = std::to_underlying(heads::simple_float_information::double_precision_float);
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
        auto const left = a.elements<DepthMax>();
        if (!left) [[unlikely]]
            return std::unexpected(left.error());
        auto const right = b.elements<DepthMax>();
        if (!right) [[unlikely]]
            return std::unexpected(right.error());
        auto j = right->begin();
        for (auto const element : *left) {
            auto const other = *j;
            if (!element) [[unlikely]]
                return std::unexpected(element.error());
            if (!other) [[unlikely]]
                return std::unexpected(other.error());
            auto const same = value_equal<DepthMax>(*element, *other, depth + 1);
            if (!same || !*same)
                return same;
            ++j;
        }
        return true;
    }
    case major_type::map: {
        if (h.argument != k.argument)
            return false;
        auto const left = a.entries<DepthMax>();
        if (!left) [[unlikely]]
            return std::unexpected(left.error());
        auto const right = b.entries<DepthMax>();
        if (!right) [[unlikely]]
            return std::unexpected(right.error());
        for (auto const entry : *left) {
            if (!entry) [[unlikely]]
                return std::unexpected(entry.error());
            std::size_t twins = 0;
            for (auto const mine : *left) {
                if (!mine) [[unlikely]]
                    return std::unexpected(mine.error());
                auto const same = value_equal<DepthMax>(entry->first, mine->first, depth + 1);
                if (!same) [[unlikely]]
                    return same;
                twins += *same ? 1uz : 0uz;
            }
            std::size_t paired = 0;
            bool values_same = false;
            for (auto const other : *right) {
                if (!other) [[unlikely]]
                    return std::unexpected(other.error());
                auto const same = value_equal<DepthMax>(entry->first, other->first, depth + 1);
                if (!same) [[unlikely]]
                    return same;
                if (!*same)
                    continue;
                ++paired;
                auto const value_same = value_equal<DepthMax>(entry->second, other->second, depth + 1);
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
        return value_equal<DepthMax>(lazy{x->source, x_content}, lazy{y->source, y_content}, depth + 1);
    default:
        return h.info == k.info && h.argument == k.argument;
    }
}

template <std::size_t DepthMax>
std::expected<bool, error> jsonpath::value_less(lazy const &a, lazy const &b)
{
    auto const x = value_sharing::container_resolve(a.top_level, a.offset);
    if (!x) [[unlikely]]
        return std::unexpected(x.error());
    auto const y = value_sharing::container_resolve(b.top_level, b.offset);
    if (!y) [[unlikely]]
        return std::unexpected(y.error());
    heads::head const &h = x->h;
    heads::head const &k = y->h;
    constexpr std::uint8_t half = std::to_underlying(heads::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice = std::to_underlying(heads::simple_float_information::double_precision_float);
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

template <std::size_t DepthMax>
std::expected<std::optional<lazy>, error> jsonpath::comparable_value(query_view const &v, std::size_t const index,
                                                                     lazy const &current, lazy const &root)
{
    expression const &e = v.expressions[index];
    auto const unsigned_integer = [](std::uint64_t const n) -> std::expected<std::optional<lazy>, error> {
        std::string encoded;
        if (auto const r = diagnostic_notation::head_append(encoded, major_type::unsigned_integer, n, diagnostic_notation::no_indicator); !r) [[unlikely]]
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
        expression const &q = e.kind == expression::kind::query ? e : v.expressions[e.first];
        auto const nodes = segments_apply<DepthMax>(v, q.first, q.second, q.relative ? current : root, root);
        if (!nodes) [[unlikely]]
            return std::unexpected(nodes.error());
        if (e.kind == expression::kind::count)
            return unsigned_integer(nodes->size());
        if (nodes->size() != 1)
            return std::nullopt;
        return nodes->front();
    }
    case expression::kind::length: {
        auto const argument = comparable_value<DepthMax>(v, e.first, current, root);
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

template <std::size_t DepthMax>
std::expected<bool, error> jsonpath::expression_test(query_view const &v, std::size_t const index, lazy const &current,
                                                     lazy const &root)
{
    expression const &e = v.expressions[index];
    switch (e.kind) {
    case expression::kind::logical_or:
    case expression::kind::logical_and: {
        auto const left = expression_test<DepthMax>(v, e.first, current, root);
        if (!left || *left == (e.kind == expression::kind::logical_or))
            return left;
        return expression_test<DepthMax>(v, e.second, current, root);
    }
    case expression::kind::logical_not: {
        auto const operand = expression_test<DepthMax>(v, e.first, current, root);
        if (!operand) [[unlikely]]
            return operand;
        return !*operand;
    }
    case expression::kind::query: {
        auto const nodes = segments_apply<DepthMax>(v, e.first, e.second, e.relative ? current : root, root);
        if (!nodes) [[unlikely]]
            return std::unexpected(nodes.error());
        return !nodes->empty();
    }
    case expression::kind::comparison:
        break;
    [[unlikely]] default:
        return std::unexpected(error::invalid_path);
    }
    auto const a = comparable_value<DepthMax>(v, e.first, current, root);
    if (!a) [[unlikely]]
        return std::unexpected(a.error());
    auto const b = comparable_value<DepthMax>(v, e.second, current, root);
    if (!b) [[unlikely]]
        return std::unexpected(b.error());
    auto const equal = [&]() -> std::expected<bool, error> {
        if (!*a || !*b)
            return !*a && !*b;
        return value_equal<DepthMax>(**a, **b, 0);
    };
    auto const less = [](std::optional<lazy> const &x, std::optional<lazy> const &y) -> std::expected<bool, error> {
        if (!x || !y)
            return false;
        return value_less<DepthMax>(*x, *y);
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

template <std::size_t DepthMax>
std::expected<void, error> jsonpath::selector_apply(query_view const &v, selector const &s, lazy const &node, lazy const &root,
                                                    std::vector<lazy> &nodelist)
{
    std::size_t const limit = root.top_level->encoded.size();
    auto const append = [&nodelist, limit](lazy const &l) -> std::expected<void, error> {
        if (nodelist.size() >= limit) [[unlikely]]
            return std::unexpected(error::nodelist_too_long);
        nodelist.push_back(l);
        return {};
    };
    if (s.kind == selector::kind::key || s.kind == selector::kind::index) {
        auto const child = s.kind == selector::kind::index ? node.at<DepthMax>(s.index)
                                                           : key_find<DepthMax>(node, std::string_view(std::span(v.keys).subspan(s.key_at, s.key_size)));
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
        auto const elements = node.elements<DepthMax>();
        if (!elements) [[unlikely]]
            return std::unexpected(elements.error());
        for (auto const element : *elements) {
            if (!element) [[unlikely]]
                return std::unexpected(element.error());
            children.push_back(*element);
        }
    } else if (found->h.major == major_type::map && s.kind != selector::kind::slice) {
        auto const entries = node.entries<DepthMax>();
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
            auto const chosen = expression_test<DepthMax>(v, s.expression, child, root);
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

template <std::size_t DepthMax>
std::expected<void, error> jsonpath::segment_apply(query_view const &v, segment const &s, lazy const &node, lazy const &root,
                                                       std::vector<lazy> &nodelist, std::size_t const depth)
{
    if (auto const r = validity::check_nesting_depth(depth, DepthMax); !r) [[unlikely]]
        return std::unexpected(r.error());
    for (selector const &each : v.selectors.subspan(s.selector_at, s.selector_count))
        if (auto const r = selector_apply<DepthMax>(v, each, node, root, nodelist); !r) [[unlikely]]
            return r;
    if (!s.descendant)
        return {};
    std::vector<lazy> children;
    selector const wildcard{selector::kind::wildcard, 0, 0, 0, std::nullopt, std::nullopt, 1, 0};
    if (auto const r = selector_apply<DepthMax>(v, wildcard, node, root, children); !r) [[unlikely]]
        return r;
    for (lazy const &child : children)
        if (auto const r = segment_apply<DepthMax>(v, s, child, root, nodelist, depth + 1); !r) [[unlikely]]
            return r;
    return {};
}

template <std::size_t DepthMax>
std::expected<std::vector<lazy>, error> jsonpath::segments_apply(query_view const &v, std::size_t const segment_at,
                                                                 std::size_t const segment_count, lazy const &start, lazy const &root)
{
    std::vector<lazy> nodes{start};
    std::vector<lazy> next;
    for (segment const &s : v.segments.subspan(segment_at, segment_count)) {
        next.clear();
        for (lazy const &node : nodes)
            if (auto const r = segment_apply<DepthMax>(v, s, node, root, next, 0); !r) [[unlikely]]
                return std::unexpected(r.error());
        std::swap(nodes, next);
    }
    return nodes;
}

template <std::size_t DepthMax, class Binding>
std::expected<typename Binding::value, error> jsonpath::query_walk(Binding &binding, query_view const &v, parsed_query const &top,
                                                            lazy const &root)
{
    validity::throw_logic_error_if_null(root.top_level, "cbor::at_path: the lazy holds no top-level item");
    if (top.singular) {
        lazy node = root;
        for (segment const &s : v.segments.subspan(top.segment_at, top.segment_count)) {
            selector const &each = v.selectors[s.selector_at];
            auto const child = each.kind == selector::kind::index
                                   ? node.at<DepthMax>(each.index)
                                   : key_find<DepthMax>(node, std::string_view(std::span(v.keys).subspan(each.key_at, each.key_size)));
            if (!child) [[unlikely]]
                return std::unexpected(child.error());
            node = *child;
        }
        auto value = lazy_decode<DepthMax>(binding, node);
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        return std::move(*value);
    }
    auto const nodes = segments_apply<DepthMax>(v, top.segment_at, top.segment_count, root, root);
    if (!nodes) [[unlikely]]
        return std::unexpected(nodes.error());
    auto array = binding.array_decode(nodes->size());
    for (lazy const &node : *nodes) {
        auto value = lazy_decode<DepthMax>(binding, node);
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        array = binding.array_append(std::move(array), std::move(*value));
    }
    return array;
}

template <std::size_t DepthMax, class Binding>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<typename Binding::value, error> at_path(Binding &binding, std::string_view const path, lazy const &l)
{
    auto const q = jsonpath::query_parse(path, false, DepthMax);
    if (!q) [[unlikely]]
        return std::unexpected(q.error());
    return jsonpath::query_walk<DepthMax>(binding, {q->segments, q->selectors, q->expressions, q->keys}, q->top, l);
}

template <fixed_string Path, std::size_t DepthMax, class Binding>
    requires(verify_path<Path, DepthMax>::value)
std::expected<typename Binding::value, error> at_path(Binding &binding, lazy const &l)
{
    constexpr auto q = [] { return *jsonpath::query_parse(Path.view(), true, DepthMax); };
    constexpr std::size_t segments = q().segments.size();
    constexpr std::size_t selectors = q().selectors.size();
    constexpr std::size_t expressions = q().expressions.size();
    constexpr std::size_t keys = q().keys.size();
    constexpr auto top = q().top;
    constexpr auto compiled = [q] {
        auto const parsed = q();
        std::tuple<std::array<jsonpath::segment, segments>, std::array<jsonpath::selector, selectors>,
                   std::array<jsonpath::expression, expressions>, std::array<char, keys>>
            c{};
        std::ranges::copy(parsed.segments, std::get<0>(c).begin());
        std::ranges::copy(parsed.selectors, std::get<1>(c).begin());
        std::ranges::copy(parsed.expressions, std::get<2>(c).begin());
        std::ranges::copy(parsed.keys, std::get<3>(c).begin());
        return c;
    }();
    return jsonpath::query_walk<DepthMax>(binding,
                                          {std::get<0>(compiled), std::get<1>(compiled), std::get<2>(compiled),
                                           std::string_view(std::get<3>(compiled).data(), keys)},
                                          top, l);
}

template <fixed_string Path, std::size_t DepthMax>
class singular_query
    : public std::bool_constant<
          validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value() && [] {
    std::array const text = Path.value;
    auto const q = jsonpath::query_parse(std::string_view(text.data(), text.size() - 1), true, DepthMax);
    return q.has_value() && q->top.singular &&
           std::ranges::all_of(q->selectors, [&q](jsonpath::selector const &s) {
               return s.kind == jsonpath::selector::kind::index ||
                      (s.kind == jsonpath::selector::kind::key &&
                       static_cast<major_type>(static_cast<std::uint8_t>(q->keys[s.key_at]) >> 5) == major_type::text_string);
           });
}()>
{
};

template <fixed_string Path, class T, std::size_t DepthMax>
    requires(singular_query<Path, DepthMax>::value &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
              std::is_same_v<T, std::nullptr_t>))
std::expected<T, error> at_path(std::string_view const encoded)
{
    return jsonpath::query_walk<Path, DepthMax, T>(encoded);
}

template <fixed_string Path, class T, std::size_t DepthMax>
    requires(singular_query<Path, DepthMax>::value &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<owning_ref<T>, error> at_path(std::shared_ptr<void const> owner, std::string_view const encoded)
{
    validity::throw_logic_error_if_empty(owner, "cbor::at_path: the owner of the encoded data item is empty");
    return jsonpath::query_walk<Path, DepthMax, T>(std::move(owner), encoded);
}

}
