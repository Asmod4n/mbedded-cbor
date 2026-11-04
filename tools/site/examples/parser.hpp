#pragma once

#include "kinds/kinds.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <cctype>
#include <ranges>

namespace site
{

struct variable {
    std::string name;
    value v;
    bool constant;
};

struct session {
    std::span<std::span<entry const> const> tables;
    std::vector<variable> variables;
};

inline std::string shown(value const &v)
{
    std::string const text = v.text(v.held.get());
    if (v.expected && v.type == type_of<cbor::error>())
        return "unexpected " + text;
    return text;
}

struct completion {
    bool found = false;
    bool from_member = false;
    std::optional<value> object;
    bool constant = false;
    std::string partial;
    std::string argument_partial;
};

struct parser {
    std::string_view rest;
    session &s;
    completion *point = nullptr;
    bool runnable_only = false;

    result member_point(value v, bool const constant, std::string_view const text)
    {
        std::string inner = point->found && !point->object ? point->partial : std::string();
        point->found = true;
        point->from_member = true;
        point->object = std::move(v);
        point->constant = constant;
        point->partial = std::string(text);
        point->argument_partial = std::move(inner);
        return std::unexpected("completion");
    }

    bool member_point_open() const
    {
        return point != nullptr && !(point->found && point->from_member);
    }

    result completion_found(std::optional<value> object, std::string partial)
    {
        point->found = true;
        point->object = std::move(object);
        point->partial = std::move(partial);
        return std::unexpected("completion");
    }

    static bool name_char(char const c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    void spaces_skip()
    {
        while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
            rest.remove_prefix(1);
    }

    bool consumed(std::string_view const token)
    {
        spaces_skip();
        if (!rest.starts_with(token))
            return false;
        if (name_char(token.back()) && rest.size() > token.size() && name_char(rest[token.size()]))
            return false;
        rest.remove_prefix(token.size());
        return true;
    }

    bool identifier_next()
    {
        spaces_skip();
        return !rest.empty() && (std::isalpha(static_cast<unsigned char>(rest.front())) || rest.front() == '_');
    }

    std::string identifier_read()
    {
        spaces_skip();
        std::string out;
        for (;;) {
            auto const n = static_cast<std::size_t>(std::ranges::find_if_not(rest, name_char) - rest.begin());
            out += rest.substr(0, n);
            rest.remove_prefix(n);
            if (!rest.starts_with("::"))
                return out;
            out += "::";
            rest.remove_prefix(2);
        }
    }

    std::expected<std::string, fault> string_read()
    {
        std::string out;
        while (!rest.empty() && rest.front() != '"') {
            if (rest.front() == '\\' && rest.size() > 1) {
                rest.remove_prefix(1);
                char const c = rest.front();
                out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
            } else {
                out += rest.front();
            }
            rest.remove_prefix(1);
        }
        if (rest.empty()) [[unlikely]]
            return std::unexpected("a string has no closing quote");
        rest.remove_prefix(1);
        return out;
    }

    result number_read()
    {
        auto const n = static_cast<std::size_t>(std::ranges::find_if(rest, [](char const c) {
                                                    return !std::isalnum(static_cast<unsigned char>(c)) && c != '.' &&
                                                           c != '-' && c != '+' && c != '\'';
                                                }) -
                                                rest.begin());
        std::string text(rest.substr(0, n));
        rest.remove_prefix(n);
        std::erase(text, '\'');
        bool const single = text.ends_with('f') && !text.starts_with("0x");
        bool const is_unsigned = text.ends_with('u') || text.ends_with('U');
        while (!text.empty() && (text.back() == 'f' || text.back() == 'u' || text.back() == 'U') && !text.starts_with("0x"))
            text.pop_back();
        char const *const first = text.data();
        char const *const last = std::to_address(text.end());
        bool const hex = text.starts_with("0x");
        if (!single && text.find_first_of(".eE") == std::string::npos) {
            if (hex) {
                unsigned long long u = 0;
                if (auto const r = std::from_chars(first + 2, last, u, 16); r.ec == std::errc{} && r.ptr == last)
                    return value_of(u);
            } else if (is_unsigned) {
                unsigned u = 0;
                if (auto const r = std::from_chars(first, last, u); r.ec == std::errc{} && r.ptr == last)
                    return value_of(u);
            } else {
                int i = 0;
                if (auto const r = std::from_chars(first, last, i); r.ec == std::errc{} && r.ptr == last)
                    return value_of(i);
                long long l = 0;
                if (auto const r = std::from_chars(first, last, l); r.ec == std::errc{} && r.ptr == last)
                    return value_of(l);
                unsigned long long u = 0;
                if (auto const r = std::from_chars(first, last, u); r.ec == std::errc{} && r.ptr == last)
                    return value_of(u);
            }
        } else if (single) {
            float f = 0;
            if (auto const r = std::from_chars(first, last, f); r.ec == std::errc{} && r.ptr == last)
                return value_of(f);
        } else {
            double d = 0;
            if (auto const r = std::from_chars(first, last, d); r.ec == std::errc{} && r.ptr == last)
                return value_of(d);
        }
        return std::unexpected("not a number: " + text);
    }

    std::expected<std::vector<std::string>, fault> template_arguments_read()
    {
        std::vector<std::string> out;
        if (!consumed("<"))
            return out;
        for (;;) {
            if (consumed("\"")) {
                auto t = string_read();
                if (!t) [[unlikely]]
                    return std::unexpected(t.error());
                out.push_back(std::move(*t));
            } else {
                std::size_t depth = 0;
                std::size_t i = 0;
                for (; i < rest.size(); ++i) {
                    char const c = rest[i];
                    if (c == '<')
                        ++depth;
                    else if ((c == '>' || c == ',') && depth == 0)
                        break;
                    else if (c == '>')
                        --depth;
                }
                std::string_view t = rest.substr(0, i);
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
                    t.remove_suffix(1);
                out.emplace_back(t);
                rest.remove_prefix(i);
            }
            if (consumed(">"))
                return out;
            if (!consumed(",")) [[unlikely]]
                return std::unexpected("a template argument list has no closing >");
        }
    }

    struct qualified {
        std::string name;
        std::vector<std::string> template_arguments;
    };

    static std::string joined(std::span<std::string const> const targs)
    {
        std::string out;
        for (std::string const &t : targs)
            out += (out.empty() ? "" : ", ") + t;
        return out;
    }

    std::expected<qualified, fault> qualified_read()
    {
        qualified q{identifier_read(), {}};
        for (;;) {
            auto targs = template_arguments_read();
            if (!targs) [[unlikely]]
                return std::unexpected(targs.error());
            if (targs->empty() || !rest.starts_with("::")) {
                q.template_arguments = std::move(*targs);
                return q;
            }
            rest.remove_prefix(2);
            q.name += "<" + joined(*targs) + ">::" + identifier_read();
        }
    }

    std::expected<std::vector<value>, fault> arguments_read(std::string_view const close)
    {
        std::vector<value> out;
        if (consumed(close))
            return out;
        for (;;) {
            auto v = expression();
            if (!v) [[unlikely]]
                return std::unexpected(v.error());
            out.push_back(std::move(*v));
            if (consumed(close))
                return out;
            if (!consumed(",")) [[unlikely]]
                return std::unexpected("an argument list has no closing " + std::string(close));
        }
    }

    entry const *entry_find(std::string_view const name) const
    {
        for (std::span<entry const> const table : s.tables)
            for (entry const &e : table)
                if (e.name == name)
                    return &e;
        return nullptr;
    }

    result called(std::string const &name, value const *self, std::span<std::string const> const targs,
                  std::span<value const> const args, bool const constant)
    {
        entry const *const e = entry_find(name);
        if (e == nullptr) [[unlikely]]
            return std::unexpected(name + " is not known here");
        if (e->changes && constant) [[unlikely]]
            return std::unexpected(name + " changes its object, and the object is const");
        if (runnable_only && !e->runnable && name != "std::move()" && name != "cbor::key{}") [[unlikely]]
            return std::unexpected(name + " is not run for a completion");
        return e->run(call{self, targs, args});
    }

    static result unwrapped(value v, std::string_view const how)
    {
        if (v.type == type_of<cbor::error>()) [[unlikely]]
            return std::unexpected(std::string(how) + " on a std::expected that holds " +
                                   kind<cbor::error>::text(*held_if<cbor::error>(v)));
        v.expected = false;
        return v;
    }

    result expected_member(value const &self, std::string const &name, std::span<value const> const args)
    {
        bool const holds_error = self.type == type_of<cbor::error>();
        if (name == "error") {
            if (!holds_error) [[unlikely]]
                return std::unexpected("error() on a std::expected that holds a value");
            value e = self;
            e.expected = false;
            return e;
        }
        if (name == "has_value")
            return value_of(!holds_error);
        if (name == "value_or") {
            if (args.size() != 1) [[unlikely]]
                return std::unexpected("value_or takes one argument");
            return holds_error ? args.front() : unwrapped(self, "value_or");
        }
        if (name == "value")
            return unwrapped(self, "value()");
        return std::unexpected(name + " is not a member of std::expected; -> reads the value");
    }

    result variable_read(std::string const &name, bool &constant) const
    {
        auto const v = std::ranges::find(s.variables, name, &variable::name);
        if (v == s.variables.end()) [[unlikely]]
            return std::unexpected(name + " is not known here");
        constant = v->constant;
        return v->v;
    }

    result enumerator_read(std::string_view const name, std::string_view const prefix,
                           std::span<std::string_view const> const names, auto const made)
    {
        auto const i = std::ranges::find(names, name.substr(prefix.size()));
        if (i == names.end() || i->empty()) [[unlikely]]
            return std::unexpected(std::string(name) + " is not known here");
        return made(static_cast<std::size_t>(i - names.begin()));
    }

    result primary(bool &constant)
    {
        constant = false;
        if (consumed("*")) {
            auto v = postfix_read(constant);
            if (!v) [[unlikely]]
                return v;
            if (v->expected)
                return unwrapped(*v, "*");
            return called(std::string(v->kind) + ".operator*()", &*v, {}, {}, false);
        }
        if (consumed("(")) {
            auto v = expression();
            if (v && !consumed(")")) [[unlikely]]
                return std::unexpected("a ( has no closing )");
            return v;
        }
        if (consumed("\"")) {
            auto t = string_read();
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            return value_of(std::move(*t));
        }
        spaces_skip();
        if (point && rest.empty())
            return completion_found(std::nullopt, "");
        if (!rest.empty() && (std::isdigit(static_cast<unsigned char>(rest.front())) || rest.front() == '-'))
            return number_read();
        if (!identifier_next()) [[unlikely]]
            return std::unexpected(rest.empty() ? "the line ends early"
                                                : "cannot read " + std::string(rest.substr(0, 16)));
        auto q = qualified_read();
        if (!q) [[unlikely]]
            return std::unexpected(q.error());
        std::string const &name = q->name;
        auto *const targs = &q->template_arguments;
        if (point && rest.empty() && targs->empty())
            return completion_found(std::nullopt, name);
        if (name == "true" || name == "false")
            return value_of(name == "true");
        if (name == "nullptr")
            return value_of(nullptr);
        if (name.starts_with("cbor::error::"))
            return enumerator_read(name, "cbor::error::", error_names,
                                   [](std::size_t const i) { return value_of(static_cast<cbor::error>(i)); });
        if (name.starts_with("cbor::type::"))
            return enumerator_read(name, "cbor::type::", type_names,
                                   [](std::size_t const i) { return value_of(static_cast<cbor::type>(i)); });
        for (auto const [open, close, suffix] :
             {std::tuple{std::string_view("("), std::string_view(")"), std::string_view("()")},
              std::tuple{std::string_view("{"), std::string_view("}"), std::string_view("{}")}}) {
            if (!consumed(open))
                continue;
            auto a = arguments_read(close);
            if (!a) [[unlikely]]
                return std::unexpected(a.error());
            return called(name + std::string(suffix), nullptr, *targs, *a, false);
        }
        if (targs->empty() && name.find("::") == std::string::npos)
            return variable_read(name, constant);
        return called(name, nullptr, *targs, {}, false);
    }

    result member_read(value v, bool const constant)
    {
        spaces_skip();
        std::string_view const start = rest;
        if (point && rest.empty())
            return member_point(std::move(v), constant, start);
        auto r = member_called(v, constant);
        if (r || !member_point_open())
            return r;
        return member_point(std::move(v), constant, start);
    }

    result member_called(value const &v, bool const constant)
    {
        if (!identifier_next()) [[unlikely]]
            return std::unexpected("a member needs a name");
        std::string const name = identifier_read();
        auto targs = template_arguments_read();
        if (!targs) [[unlikely]]
            return std::unexpected(targs.error());
        bool const parens = consumed("(");
        std::vector<value> args;
        if (parens) {
            auto a = arguments_read(")");
            if (!a) [[unlikely]]
                return std::unexpected(a.error());
            args = std::move(*a);
        }
        if (v.expected)
            return expected_member(v, name, args);
        return called(std::string(v.kind) + "." + name + (parens ? "()" : ""), &v, *targs, args, constant);
    }

    result postfix_read(bool &constant)
    {
        auto v = primary(constant);
        while (v) {
            if (consumed("->")) {
                if (v->expected) {
                    v = unwrapped(std::move(*v), "->");
                } else {
                    v = called(std::string(v->kind) + ".operator->()", &*v, {}, {}, constant);
                }
                if (v)
                    v = member_read(std::move(*v), constant);
            } else if (consumed(".")) {
                v = member_read(std::move(*v), constant);
            } else if (spaces_skip(), rest.starts_with("[")) {
                std::string_view const start = rest;
                rest.remove_prefix(1);
                auto a = arguments_read("]");
                if (!a) [[unlikely]] {
                    if (member_point_open())
                        return member_point(std::move(*v), constant, start);
                    return std::unexpected(a.error());
                }
                if (v->expected) [[unlikely]]
                    return std::unexpected("[] on a std::expected; *x or x-> reads the value");
                v = called(std::string(v->kind) + ".operator[]()", &*v, {}, *a, constant);
            } else {
                return v;
            }
            constant = false;
        }
        return v;
    }

    result expression()
    {
        bool constant = false;
        return postfix_read(constant);
    }

    result declared(std::string name, value v, bool const constant)
    {
        if (std::ranges::contains(s.variables, name, &variable::name)) [[unlikely]]
            return std::unexpected(name + " is declared already");
        s.variables.push_back({std::move(name), v, constant});
        return v;
    }

    result statement()
    {
        std::string_view const start = rest;
        if (identifier_next()) {
            auto q = qualified_read();
            if (q) {
                std::string type = q->name;
                if (!q->template_arguments.empty())
                    type += "<" + joined(q->template_arguments) + ">";
                bool const constant = consumed("const");
                consumed("&");
                if (identifier_next()) {
                    std::string const name = identifier_read();
                    if (consumed("=")) {
                        auto v = expression();
                        if (!v) [[unlikely]]
                            return v;
                        if (type != "auto" && v->kind != type) [[unlikely]]
                            return std::unexpected("the value is " + std::string(v->kind) + ", not " + type);
                        return declared(name, std::move(*v), constant);
                    }
                    if (type == "auto") [[unlikely]]
                        return std::unexpected("auto needs = and a value");
                    std::string_view const close = consumed("{") ? "}" : consumed("(") ? ")" : "";
                    std::vector<value> args;
                    if (!close.empty()) {
                        auto a = arguments_read(close);
                        if (!a) [[unlikely]]
                            return std::unexpected(a.error());
                        args = std::move(*a);
                    }
                    auto v = called(type + "{}", nullptr, {}, args, false);
                    if (!v) [[unlikely]]
                        return v;
                    return declared(name, std::move(*v), constant);
                }
            }
            rest = start;
        }
        return expression();
    }
};

inline std::string line_run(session &s, std::string_view const line)
{
    parser p{line, s};
    auto const v = p.statement();
    if (!v) [[unlikely]]
        return "fault: " + v.error();
    p.consumed(";");
    p.spaces_skip();
    if (!p.rest.empty()) [[unlikely]]
        return "fault: cannot read " + std::string(p.rest);
    return shown(*v);
}

inline std::vector<std::string> free_names(session const &s)
{
    std::vector<std::string> out;
    for (std::span<entry const> const table : s.tables)
        for (entry const &e : table) {
            std::string_view const name = e.name;
            if (name.find('.') != std::string_view::npos)
                continue;
            if (name.ends_with("()") || name.ends_with("{}"))
                out.emplace_back(std::string(name.substr(0, name.size() - 1)));
            else
                out.emplace_back(name);
        }
    for (variable const &v : s.variables)
        if (!v.name.empty())
            out.push_back(v.name);
    for (std::string_view const n : error_names)
        if (!n.empty())
            out.push_back("cbor::error::" + std::string(n));
    for (std::string_view const n : type_names)
        out.push_back("cbor::type::" + std::string(n));
    for (std::string_view const n : {"true", "false", "nullptr"})
        out.emplace_back(n);
    return out;
}

constexpr std::size_t completion_limit = 40;

struct completion_list {
    std::string head;
    std::vector<std::string> lines;
};

inline std::string spaces_removed(std::string_view const text)
{
    std::string out;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char const c = text[i];
        if (quoted && c == '\\' && i + 1 < text.size()) {
            out += c;
            out += text[++i];
            continue;
        }
        if (c == '"')
            quoted = !quoted;
        if (quoted || !std::isspace(static_cast<unsigned char>(c)))
            out += c;
    }
    return out;
}

inline std::string closers_of(std::string_view const text)
{
    std::string open;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char const c = text[i];
        if (quoted) {
            if (c == '\\')
                ++i;
            else if (c == '"')
                quoted = false;
            continue;
        }
        if (c == '-' && i + 1 < text.size() && text[i + 1] == '>')
            ++i;
        else if (c == '"')
            quoted = true;
        else if (c == '(')
            open += ')';
        else if (c == '[')
            open += ']';
        else if (c == '{')
            open += '}';
        else if (c == '<')
            open += '>';
        else if (!open.empty() && c == open.back())
            open.pop_back();
    }
    std::ranges::reverse(open);
    return (quoted ? "\"" : "") + open;
}

inline bool kept(result const &r)
{
    return r && !(r->expected && r->type == type_of<cbor::error>());
}

inline result candidate_run(session &s, value const &object, bool const constant, std::string_view const text)
{
    parser p{text, s};
    if (p.consumed("[")) {
        auto a = p.arguments_read("]");
        if (!a)
            return std::unexpected(a.error());
        p.spaces_skip();
        if (!p.rest.empty())
            return std::unexpected("cannot read " + std::string(p.rest));
        return p.called(std::string(object.kind) + ".operator[]()", &object, {}, *a, constant);
    }
    std::string const name = p.identifier_read();
    auto targs = p.template_arguments_read();
    if (!targs)
        return std::unexpected(targs.error());
    bool const parens = p.consumed("(");
    std::vector<value> args;
    if (parens) {
        auto a = p.arguments_read(")");
        if (!a)
            return std::unexpected(a.error());
        args = std::move(*a);
    }
    p.spaces_skip();
    if (!p.rest.empty())
        return std::unexpected("cannot read " + std::string(p.rest));
    if (object.expected)
        return p.expected_member(object, name, args);
    return p.called(std::string(object.kind) + "." + name + (parens ? "()" : ""), &object, *targs, args, constant);
}

inline entry const *entry_named(session const &s, std::string_view const name)
{
    for (std::span<entry const> const table : s.tables)
        for (entry const &e : table)
            if (e.name == name)
                return &e;
    return nullptr;
}

inline bool runnable_text(session const &s, value const &object, std::string_view const text)
{
    if (object.expected)
        return !text.ends_with("(");
    std::string_view const kind = object.kind;
    if (text.starts_with("["))
        return entry_named(s, std::string(kind) + ".operator[]()") != nullptr &&
               entry_named(s, std::string(kind) + ".operator[]()")->runnable;
    auto const end = std::ranges::find_if(text, [](char const c) { return c == '<' || c == '('; });
    std::string const name = std::string(text.begin(), end);
    std::string const full = std::string(kind) + "." + name + (std::ranges::find(text, '(') != text.end() ? "()" : "");
    entry const *const e = entry_named(s, full);
    return e != nullptr && e->runnable && !text.ends_with("(");
}

inline std::vector<std::string> member_candidates(session &s, value const &object, bool const constant,
                                                  std::string_view const partial,
                                                  std::string_view const argument_partial)
{
    using member_writer = std::function<void(candidate_taken const &)>;
    std::vector<member_writer> writers;
    auto const fixed = [](std::vector<std::string> texts) {
        return member_writer([texts = std::move(texts)](candidate_taken const &taken) {
            for (std::string const &t : texts)
                if (!taken(t))
                    return;
        });
    };
    if (object.expected) {
        bool const holds_error = object.type == type_of<cbor::error>();
        writers.push_back(fixed({"has_value()", holds_error ? "error()" : "value()", "value_or("}));
    } else {
        std::string const prefix = std::string(object.kind) + ".";
        for (std::span<entry const> const table : s.tables)
            for (entry const &e : table) {
                if (!e.name.starts_with(prefix))
                    continue;
                std::string_view const member = e.name.substr(prefix.size());
                if (member == "operator*()" || member == "operator->()")
                    continue;
                if (member == "operator[]()" && !partial.starts_with("["))
                    continue;
                if (member != "operator[]()" && partial.starts_with("["))
                    continue;
                if (e.candidates != nullptr) {
                    writers.push_back([&object, &e, partial](candidate_taken const &taken) {
                        e.candidates(object, partial, taken);
                    });
                } else if (member.starts_with("operator")) {
                    continue;
                } else if (member.ends_with("()")) {
                    writers.push_back(fixed({std::string(member.substr(0, member.size() - 1)) + (e.runnable ? ")" : "")}));
                } else {
                    writers.push_back(fixed({std::string(member)}));
                }
            }
    }
    std::string const wanted = spaces_removed(partial);
    std::vector<std::string> out;
    std::set<std::string> tried;
    auto const survives = [&](std::string const &text) {
        if (!spaces_removed(text).starts_with(wanted) || !tried.insert(text).second)
            return false;
        if (runnable_text(s, object, text) && !kept(candidate_run(s, object, constant, text)))
            return false;
        out.push_back(text);
        return true;
    };
    for (member_writer const &w : writers) {
        if (out.size() >= completion_limit)
            break;
        w([&](std::string const &text) { return !survives(text) && out.size() < completion_limit; });
    }
    for (member_writer const &w : writers) {
        if (out.size() >= completion_limit)
            break;
        w([&](std::string const &text) {
            survives(text);
            return out.size() < completion_limit;
        });
    }
    if (out.empty() && !argument_partial.empty() && !object.expected) {
        auto const open = std::ranges::find_if(partial, [](char const c) { return c == '(' || c == '['; });
        if (open != partial.end()) {
            std::string const call(partial.begin(), std::ranges::next(open));
            std::string const close = *open == '(' ? ")" : "]";
            for (variable const &v : s.variables)
                if (v.name.starts_with(argument_partial))
                    survives(call + v.name + close);
        }
    }
    return out;
}

inline std::vector<std::string> continuations(session &s, value const &v)
{
    value object = v;
    std::string step = ".";
    if (v.expected) {
        if (v.type == type_of<cbor::error>())
            return {};
        object.expected = false;
        step = "->";
    }
    if (entry const *const arrow = entry_named(s, std::string(object.kind) + ".operator->()"); arrow && arrow->runnable) {
        auto const target = arrow->run(call{&object, {}, {}});
        if (!target)
            return {};
        step = v.expected ? ".value()->" : "->";
        object = *target;
    }
    std::vector<std::string> out;
    for (std::string const &c : member_candidates(s, object, false, "", ""))
        out.push_back(step + c);
    return out;
}

inline bool closed_line_runs(session const &s, std::string const &line)
{
    session copy = s;
    parser p{line, copy, nullptr, true};
    auto const v = p.statement();
    p.consumed(";");
    p.spaces_skip();
    return kept(v) && p.rest.empty();
}

inline std::string statement_trimmed(std::string_view line)
{
    while (!line.empty() && (std::isspace(static_cast<unsigned char>(line.back())) || line.back() == ';'))
        line.remove_suffix(1);
    return std::string(line);
}

inline completion_list completions(session &s, std::string_view const line)
{
    completion point;
    session const before = s;
    parser p{line, s, &point};
    auto const whole = p.statement();
    p.consumed(";");
    p.spaces_skip();
    completion_list out;
    std::set<std::string> seen;
    auto const add = [&](std::string text) {
        if (seen.insert(text).second)
            out.lines.push_back(std::move(text));
    };
    if (!point.found) {
        out.head = statement_trimmed(line);
        if (whole && p.rest.empty())
            for (std::string const &c : continuations(s, *whole))
                add(out.head + c);
        return out;
    }
    out.head = std::string(line.substr(0, line.size() - point.partial.size()));
    if (point.object) {
        for (std::string const &c : member_candidates(s, *point.object, point.constant, point.partial,
                                                      point.argument_partial)) {
            add(out.head + c);
            std::string const closers = closers_of(out.head + c);
            if (!closers.empty() && closed_line_runs(before, out.head + c + closers))
                add(out.head + c + closers);
        }
        return out;
    }
    for (std::string const &n : free_names(s))
        if (n.starts_with(point.partial))
            add(out.head + n);
    if (!point.partial.empty()) {
        session again = before;
        parser q{line, again};
        auto const v = q.statement();
        q.consumed(";");
        q.spaces_skip();
        if (v && q.rest.empty())
            for (std::string const &c : continuations(again, *v))
                add(statement_trimmed(line) + c);
    }
    return out;
}

}

