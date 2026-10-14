#include "cbor/diagnostic_notation.hpp"
#include "cbor/encode.hpp"
#include "cbor/item.hpp"
#include "cbor/lazy.hpp"
#include "cbor/query.hpp"

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
    std::optional<cbor::jsonpath::expression<cbor::nodelist>> expression;
    std::string result;
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> tape;
    std::vector<double> numbers;
    std::vector<std::shared_ptr<cbor::item const>> decoded;
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

std::uint64_t offsets_packed(std::expected<cbor::nodelist, cbor::error> const &nodes)
{
    state.offsets.clear();
    if (!nodes) [[unlikely]]
        return 0;
    for (auto const &n : *nodes)
        state.offsets.push_back(static_cast<std::uint32_t>(n.offset));
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(state.offsets.data())) << 32) |
           state.offsets.size();
}

void js_value_built(cbor::item const &i)
{
    switch (i.type) {
    case cbor::type::unsigned_integer:
        return value_number(static_cast<double>(i.via.u64));
    case cbor::type::negative_integer:
        return value_number(-1.0 - static_cast<double>(i.via.u64));
    case cbor::type::text_string:
        return value_string(i.via.str.data(), i.via.str.size());
    case cbor::type::array:
        array_begin();
        for (auto const &e : i.via.array)
            js_value_built(e);
        return container_end();
    case cbor::type::map:
        object_begin();
        for (std::size_t k = 0; k < i.via.map.size(); k += 2) {
            auto const &key = i.via.map[k];
            if (key.type == cbor::type::text_string)
                key_string(key.via.str.data(), key.via.str.size());
            else
                key_number(static_cast<double>(key.via.u64));
            js_value_built(i.via.map[k + 1]);
        }
        return container_end();
    case cbor::type::tag:
        return js_value_built(*i.via.tag.content);
    case cbor::type::false_value:
        return value_bool(0);
    case cbor::type::true_value:
        return value_bool(1);
    case cbor::type::half_float:
    case cbor::type::single_float:
    case cbor::type::double_float:
        return value_number(i.via.f64);
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

enum class tape_op : std::uint32_t { null, false_value, true_value, number, string, key, array, object };

void tape_op_written(tape_op const op, std::uint32_t const a = 0, std::uint32_t const b = 0)
{
    state.tape.insert(state.tape.end(), {static_cast<std::uint32_t>(op), a, b});
}

void tape_string_written(tape_op const op, std::string_view const s)
{
    tape_op_written(op, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(s.data())),
                    static_cast<std::uint32_t>(s.size()));
}

void tape_number_written(double const v)
{
    tape_op_written(tape_op::number, static_cast<std::uint32_t>(state.numbers.size()));
    state.numbers.push_back(v);
}

void tape_value_written(cbor::item const &i)
{
    switch (i.type) {
    case cbor::type::unsigned_integer:
        return tape_number_written(static_cast<double>(i.via.u64));
    case cbor::type::negative_integer:
        return tape_number_written(-1.0 - static_cast<double>(i.via.u64));
    case cbor::type::text_string:
        return tape_string_written(tape_op::string, i.via.str);
    case cbor::type::array:
        tape_op_written(tape_op::array, static_cast<std::uint32_t>(i.via.array.size()));
        for (auto const &e : i.via.array)
            tape_value_written(e);
        return;
    case cbor::type::map:
        tape_op_written(tape_op::object, static_cast<std::uint32_t>(i.via.map.size() / 2));
        for (std::size_t k = 0; k < i.via.map.size(); k += 2) {
            auto const &key = i.via.map[k];
            if (key.type == cbor::type::text_string)
                tape_string_written(tape_op::key, key.via.str);
            else
                tape_string_written(tape_op::key, std::string_view{});
            tape_value_written(i.via.map[k + 1]);
        }
        return;
    case cbor::type::tag:
        return tape_value_written(*i.via.tag.content);
    case cbor::type::false_value:
        return tape_op_written(tape_op::false_value);
    case cbor::type::true_value:
        return tape_op_written(tape_op::true_value);
    case cbor::type::half_float:
    case cbor::type::single_float:
    case cbor::type::double_float:
        return tape_number_written(i.via.f64);
    default:
        return tape_op_written(tape_op::null);
    }
}

std::uint64_t tape_packed()
{
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(state.tape.data())) << 32) |
           (state.tape.size() / 3);
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

__attribute__((export_name("compiled_int_sum"))) std::int64_t compiled_int_sum()
{
    auto const nodes = state.root->at_path(*state.expression);
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

__attribute__((export_name("offsets"))) std::uint64_t offsets(char const *const path_ptr, std::size_t const path_len)
{
    auto const e = cbor::jsonpath::make_expression<cbor::nodelist>(std::string_view{path_ptr, path_len});
    if (!e) [[unlikely]]
        return 0;
    return offsets_packed(state.root->at_path(*e));
}

__attribute__((export_name("compiled_offsets"))) std::uint64_t compiled_offsets()
{
    return offsets_packed(state.root->at_path(*state.expression));
}

__attribute__((export_name("compiled_js_values"))) std::int32_t compiled_js_values()
{
    return js_values_built(state.root->at_path(*state.expression));
}

__attribute__((export_name("eager_js_value"))) std::int32_t eager_js_value()
{
    auto const d = state.root->decode();
    if (!d) [[unlikely]]
        return -static_cast<std::int32_t>(d.error());
    js_value_built(**d);
    return 1;
}

__attribute__((export_name("tape_numbers"))) double const *tape_numbers()
{
    return state.numbers.data();
}

__attribute__((export_name("eager_tape"))) std::uint64_t eager_tape()
{
    state.tape.clear();
    state.numbers.clear();
    state.decoded.clear();
    auto d = state.root->decode();
    if (!d) [[unlikely]]
        return 0;
    tape_value_written(**d);
    state.decoded.push_back(std::move(*d));
    return tape_packed();
}

__attribute__((export_name("compiled_tape"))) std::uint64_t compiled_tape()
{
    state.tape.clear();
    state.numbers.clear();
    auto const nodes = state.root->at_path(*state.expression);
    if (!nodes) [[unlikely]]
        return 0;
    state.decoded.clear();
    for (auto const &n : *nodes) {
        auto d = n.decode();
        if (!d) [[unlikely]]
            return 0;
        tape_value_written(**d);
        state.decoded.push_back(std::move(*d));
    }
    return tape_packed();
}
}
