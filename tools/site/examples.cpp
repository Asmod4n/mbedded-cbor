#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>

namespace
{

enum class operation : std::int32_t { at_path = 0, size = 1, type = 2, get = 3 };

enum class result_type : std::int32_t { integer = 0, text = 1, number = 2, boolean = 3, nodes = 4, node = 5 };

std::string result;

constexpr std::string_view type_names[] = {
    "unsigned_integer", "negative_integer", "byte_string", "text_string", "array",          "map",
    "tag",              "false_value",      "true_value",  "null",        "undefined",      "simple_value",
    "half_float",       "single_float",     "double_float", "unsigned_bignum", "negative_bignum", "typed_array"};

std::string error_text(cbor::error const e)
{
    return "error: " + std::error_code{e}.message();
}

std::string notation_of(cbor::lazy const &node)
{
    auto const tree = node.clone();
    if (!tree)
        return error_text(tree.error());
    std::string bytes;
    if (auto const written = cbor::encode(**tree, bytes); !written)
        return error_text(written.error());
    auto const text = cbor::diagnostic_notation(bytes);
    return text ? *text : error_text(text.error());
}

template <class T, class F>
std::string read(cbor::lazy const &doc, std::string_view const path, F const show)
{
    auto const query = cbor::jsonpath::compile<T>(path);
    if (!query)
        return error_text(query.error());
    auto const value = doc.at_path(*query);
    if (!value)
        return error_text(value.error());
    return show(*value);
}

std::string example_run(operation const op, std::string_view const bytes, std::string_view const path, result_type const type)
{
    auto const doc = cbor::lazy::take_ownership(std::string(bytes));
    if (!doc)
        return error_text(doc.error());
    if (op == operation::at_path) {
        switch (type) {
        case result_type::integer:
            return read<std::int64_t>(*doc, path, [](std::int64_t const v) { return std::to_string(v); });
        case result_type::text:
            return read<std::string_view>(*doc, path, [](auto const &v) { return "\"" + std::string(*v) + "\""; });
        case result_type::number:
            return read<double>(*doc, path, [](double const v) { return std::to_string(v); });
        case result_type::boolean:
            return read<bool>(*doc, path, [](bool const v) { return std::string(v ? "true" : "false"); });
        case result_type::nodes:
            return read<cbor::nodelist>(*doc, path, [](cbor::nodelist const &v) {
                std::string out = std::to_string(v.size()) + " nodes:";
                for (cbor::lazy const &n : v)
                    out += " " + notation_of(n);
                return out;
            });
        case result_type::node:
            return read<cbor::lazy>(*doc, path, [](cbor::lazy const &v) { return "lazy of " + notation_of(v); });
        }
    }
    return read<cbor::lazy>(*doc, path, [op](cbor::lazy const &v) {
        if (op == operation::size) {
            auto const n = v.size();
            return n ? std::to_string(*n) : error_text(n.error());
        }
        if (op == operation::type) {
            auto const t = v.type();
            return t ? "cbor::type::" + std::string(type_names[static_cast<std::size_t>(*t)]) : error_text(t.error());
        }
        return notation_of(v);
    });
}

} // namespace

extern "C" {

__attribute__((export_name("buffer"))) char *buffer(std::size_t const size)
{
    return static_cast<char *>(std::malloc(size));
}

__attribute__((export_name("release"))) void release(char *const p)
{
    std::free(p);
}

__attribute__((export_name("run"))) std::size_t run(std::int32_t const op, char const *const bytes, std::size_t const bytes_size,
                                                    char const *const path, std::size_t const path_size,
                                                    std::int32_t const type)
{
    result = example_run(static_cast<operation>(op), {bytes, bytes_size}, {path, path_size}, static_cast<result_type>(type));
    return result.size();
}

__attribute__((export_name("result"))) char const *result_text()
{
    return result.data();
}
}
