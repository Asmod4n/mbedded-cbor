#include "cbor/diagnostic_notation.hpp"
#include "cbor/encode.hpp"
#include "cbor/item.hpp"
#include "cbor/lazy.hpp"
#include "cbor/query.hpp"
#include "cbor/rfc8746.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
__attribute__((import_module("js"), import_name("value_null"))) void value_null();
__attribute__((import_module("js"), import_name("value_bool"))) void value_bool(std::int32_t);
__attribute__((import_module("js"), import_name("value_number"))) void value_number(double);
__attribute__((import_module("js"), import_name("value_string"))) void value_string(char const *, std::size_t);
__attribute__((import_module("js"), import_name("array_begin"))) void array_begin();
__attribute__((import_module("js"), import_name("object_begin"))) void object_begin();
__attribute__((import_module("js"), import_name("key_string"))) void key_string(char const *, std::size_t);
__attribute__((import_module("js"), import_name("key_number"))) void key_number(double);
__attribute__((import_module("js"), import_name("container_end"))) void container_end();
}

namespace {

struct reactor_state {
    std::optional<cbor::lazy> root;
    std::optional<cbor::jsonpath::query<cbor::nodelist>> query;
    std::string result;
    std::optional<cbor::ref<std::string_view>> text;
    std::optional<cbor::ref<std::span<std::byte const>>> bytes;
    std::optional<cbor::ref<cbor::typed_array>> array;
    std::vector<double> numbers;
    cbor::error last{};
    std::shared_ptr<std::string> buffer;
};

reactor_state state;

constexpr std::int64_t ids_summed_modulus = 1000000007;

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
    auto const e = cbor::jsonpath::compile<cbor::nodelist>(path);
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

void js_value_built(cbor::item const &i)
{
    switch (i.type) {
    case cbor::type::unsigned_integer:
        return value_number(static_cast<double>(i.argument().value()));
    case cbor::type::negative_integer:
        return value_number(-1.0 - static_cast<double>(i.argument().value()));
    case cbor::type::text_string:
        return value_string(i.text_string().value().data(), i.text_string().value().size());
    case cbor::type::array:
        array_begin();
        for (auto const &e : i.array().value())
            js_value_built(e);
        return container_end();
    case cbor::type::map:
        object_begin();
        for (std::size_t k = 0; k < i.map().value().size(); k += 2) {
            auto const &key = i.map().value()[k];
            if (key.type == cbor::type::text_string)
                key_string(key.text_string().value().data(), key.text_string().value().size());
            else
                key_number(static_cast<double>(key.argument().value()));
            js_value_built(i.map().value()[k + 1]);
        }
        return container_end();
    case cbor::type::tag:
        return js_value_built((*i.tag_content().value()));
    case cbor::type::false_value:
        return value_bool(0);
    case cbor::type::true_value:
        return value_bool(1);
    case cbor::type::half_float:
    case cbor::type::single_float:
    case cbor::type::double_float:
        return value_number(i.floating_point().value());
    default:
        return value_null();
    }
}

std::int32_t js_values_built(std::expected<cbor::nodelist, cbor::error> const &nodes)
{
    if (!nodes) [[unlikely]]
        return -static_cast<std::int32_t>(nodes.error());
    for (auto const &n : *nodes) {
        auto const d = n.decode();
        if (!d) [[unlikely]]
            return -static_cast<std::int32_t>(d.error());
        js_value_built(**d);
    }
    return static_cast<std::int32_t>(nodes->size());
}

template <class T>
std::expected<typename cbor::jsonpath::query<T>::result_type, cbor::error> leaf_found(char const *const ptr, std::size_t const len)
{
    auto const e = cbor::jsonpath::compile<T>(std::string_view{ptr, len});
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    return state.root->at_path(*e);
}

std::uint64_t view_packed(void const *const data, std::size_t const size)
{
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data)) << 32) | size;
}

std::uint64_t error_kept(cbor::error const e)
{
    state.last = e;
    return 0;
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
    auto l = cbor::lazy::take(std::string(ptr, len));
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
    auto e = cbor::jsonpath::compile<cbor::nodelist>(std::string_view{path_ptr, path_len});
    if (!e) [[unlikely]]
        return static_cast<std::int32_t>(e.error());
    state.query = std::move(*e);
    return 0;
}

__attribute__((export_name("user_id_literal"))) std::int64_t user_id_literal()
{
    auto const id = state.root->at_path<"$.statuses[0].user.id", std::int64_t>();
    return id ? *id : -1;
}

__attribute__((export_name("compiled_first_int"))) std::int64_t compiled_first_int()
{
    auto const nodes = state.root->at_path(*state.query);
    if (!nodes || nodes->empty()) [[unlikely]]
        return -1;
    auto const v = nodes->front().get<std::int64_t>();
    return v ? *v : -1;
}

__attribute__((export_name("compiled_int_sum"))) std::int64_t compiled_int_sum()
{
    auto const nodes = state.root->at_path(*state.query);
    if (!nodes) [[unlikely]]
        return -1;
    std::int64_t sum = 0;
    for (auto const &n : *nodes)
        if (auto const v = n.get<std::int64_t>(); v)
            sum += *v % ids_summed_modulus;
    return sum;
}

__attribute__((export_name("literal_ids_sum"))) std::int64_t literal_ids_sum()
{
    auto const nodes = state.root->at_path<"$.statuses[*].id", cbor::nodelist>();
    if (!nodes) [[unlikely]]
        return -1;
    std::int64_t sum = 0;
    for (auto const &n : *nodes)
        if (auto const v = n.get<std::int64_t>(); v)
            sum += *v % ids_summed_modulus;
    return sum;
}

__attribute__((export_name("compiled_js_values"))) std::int32_t compiled_js_values()
{
    return js_values_built(state.root->at_path(*state.query));
}


__attribute__((export_name("last_error"))) std::int32_t last_error()
{
    return static_cast<std::int32_t>(state.last);
}

__attribute__((export_name("path_double"))) double path_double(char const *const ptr, std::size_t const len)
{
    auto const v = leaf_found<double>(ptr, len);
    if (!v) [[unlikely]] {
        error_kept(v.error());
        return 0;
    }
    state.last = {};
    return *v;
}

__attribute__((export_name("path_int"))) std::int64_t path_int(char const *const ptr, std::size_t const len)
{
    auto const v = leaf_found<std::int64_t>(ptr, len);
    if (!v) [[unlikely]] {
        error_kept(v.error());
        return 0;
    }
    state.last = {};
    return *v;
}

__attribute__((export_name("path_string"))) std::uint64_t path_string(char const *const ptr, std::size_t const len)
{
    auto v = leaf_found<std::string_view>(ptr, len);
    if (!v) [[unlikely]]
        return error_kept(v.error());
    state.last = {};
    state.text = std::move(*v);
    return view_packed((*state.text)->data(), (*state.text)->size());
}

__attribute__((export_name("path_bytes"))) std::uint64_t path_bytes(char const *const ptr, std::size_t const len)
{
    auto v = leaf_found<std::span<std::byte const>>(ptr, len);
    if (!v) [[unlikely]]
        return error_kept(v.error());
    state.last = {};
    state.bytes = std::move(*v);
    return view_packed((*state.bytes)->data(), (*state.bytes)->size());
}

__attribute__((export_name("path_typed_array"))) std::uint64_t path_typed_array(char const *const ptr,
                                                                                std::size_t const len)
{
    auto v = leaf_found<cbor::typed_array>(ptr, len);
    if (!v) [[unlikely]]
        return error_kept(v.error());
    state.last = {};
    state.array = std::move(*v);
    return view_packed((*state.array)->bytes.data(), (*state.array)->bytes.size());
}

__attribute__((export_name("typed_array_tag"))) std::uint32_t typed_array_tag()
{
    return static_cast<std::uint32_t>((*state.array)->tag);
}

__attribute__((export_name("path_doubles"))) std::uint64_t path_doubles(char const *const ptr, std::size_t const len)
{
    auto const nodes = leaf_found<cbor::nodelist>(ptr, len);
    if (!nodes) [[unlikely]]
        return error_kept(nodes.error());
    state.numbers.clear();
    for (auto const &n : *nodes) {
        auto const v = n.get<double>();
        if (!v) [[unlikely]]
            return error_kept(v.error());
        state.numbers.push_back(*v);
    }
    state.last = {};
    return view_packed(state.numbers.data(), state.numbers.size());
}

__attribute__((export_name("buffer_reserve"))) char *buffer_reserve(std::size_t const size)
{
    state.buffer = std::make_shared<std::string>(size, '\0');
    return state.buffer->data();
}

__attribute__((export_name("buffer_view"))) std::int32_t buffer_view()
{
    auto l = cbor::lazy::view(std::shared_ptr<std::string const>(std::move(state.buffer)));
    if (!l) [[unlikely]]
        return static_cast<std::int32_t>(l.error());
    state.root = std::move(*l);
    return 0;
}
}
