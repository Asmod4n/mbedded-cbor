#pragma once

#include "kinds/kinds.hpp"

#include <algorithm>
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

struct parser {
    std::string_view rest;
    session &s;

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
        char const *const last = text.data() + text.size();
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
            } else if (consumed("[")) {
                auto a = arguments_read("]");
                if (!a) [[unlikely]]
                    return std::unexpected(a.error());
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

}
