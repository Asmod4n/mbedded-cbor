#include "cbor/diagnostic_notation.hpp"
#include "cbor/encode.hpp"
#include "cbor/lazy.hpp"
#include "cbor/query.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct reactor_state {
    std::optional<cbor::lazy> root;
    std::optional<cbor::jsonpath::expression<cbor::nodelist>> expression;
    std::string result;
};

reactor_state state;

std::string error_named(cbor::error const e)
{
    return "error: " + cbor::make_error_code(e).message();
}

std::string nodes_written(cbor::nodelist const &nodes)
{
    std::string out = std::to_string(nodes.size()) + "\n[";
    for (auto const &node : nodes) {
        auto const decoded = node.decode();
        if (!decoded) [[unlikely]]
            return error_named(decoded.error());
        std::string bytes;
        if (auto const written = cbor::encode(**decoded, bytes); !written) [[unlikely]]
            return error_named(written.error());
        auto const edn = cbor::diagnostic_notation(bytes);
        if (!edn) [[unlikely]]
            return error_named(edn.error());
        if (&node != &nodes.front())
            out += ", ";
        out += *edn;
    }
    out += "]";
    return out;
}

std::string path_run(std::string_view const path)
{
    if (!state.root) [[unlikely]]
        return "error: no document loaded";
    auto const e = cbor::jsonpath::make_expression<cbor::nodelist>(path);
    if (!e) [[unlikely]]
        return error_named(e.error());
    auto const nodes = state.root->at_path(*e);
    if (!nodes) [[unlikely]]
        return error_named(nodes.error());
    return nodes_written(*nodes);
}

std::uint64_t result_packed(std::string text)
{
    state.result = std::move(text);
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(state.result.data())) << 32) |
           state.result.size();
}

} // namespace

extern "C" {

__attribute__((export_name("alloc"))) void *alloc(std::size_t const size)
{
    return std::malloc(size);
}

__attribute__((export_name("release"))) void release(void *const ptr)
{
    std::free(ptr);
}

__attribute__((export_name("load"))) std::int32_t load(char const *const ptr, std::size_t const len)
{
    auto l = cbor::lazy::copy(std::string_view{ptr, len});
    if (!l) [[unlikely]]
        return static_cast<std::int32_t>(l.error());
    state.root = std::move(*l);
    return 0;
}

__attribute__((export_name("run"))) std::uint64_t run(char const *const path_ptr, std::size_t const path_len)
{
    return result_packed(path_run(std::string_view{path_ptr, path_len}));
}

__attribute__((export_name("edn"))) std::uint64_t edn(char const *const ptr, std::size_t const len)
{
    auto const text = cbor::diagnostic_notation(std::string_view{ptr, len});
    return result_packed(text ? *text : error_named(text.error()));
}

__attribute__((export_name("compile"))) std::int32_t compile(char const *const path_ptr, std::size_t const path_len)
{
    auto e = cbor::jsonpath::make_expression<cbor::nodelist>(std::string_view{path_ptr, path_len});
    if (!e) [[unlikely]]
        return static_cast<std::int32_t>(e.error());
    state.expression = std::move(*e);
    return 0;
}

__attribute__((export_name("user_id_literal"))) std::int64_t user_id_literal()
{
    auto const id = state.root->at_path<"$.statuses[0].user.id", std::int64_t>();
    return id ? *id : -1;
}

__attribute__((export_name("compiled_first_int"))) std::int64_t compiled_first_int()
{
    auto const nodes = state.root->at_path(*state.expression);
    if (!nodes || nodes->empty()) [[unlikely]]
        return -1;
    auto const v = nodes->front().get<std::int64_t>();
    return v ? *v : -1;
}

__attribute__((export_name("compiled_int_sum"))) double compiled_int_sum()
{
    auto const nodes = state.root->at_path(*state.expression);
    if (!nodes) [[unlikely]]
        return -1;
    double sum = 0;
    for (auto const &n : *nodes)
        if (auto const v = n.get<double>(); v)
            sum += *v;
    return sum;
}

__attribute__((export_name("literal_ids_sum"))) double literal_ids_sum()
{
    auto const nodes = state.root->at_path<"$.statuses[*].id", cbor::nodelist>();
    if (!nodes) [[unlikely]]
        return -1;
    double sum = 0;
    for (auto const &n : *nodes)
        if (auto const v = n.get<double>(); v)
            sum += *v;
    return sum;
}
}
