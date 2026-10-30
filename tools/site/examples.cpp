#include "examples/errors.hpp"
#include "examples/item.hpp"
#include "examples/lazy.hpp"
#include "examples/parser.hpp"
#include "examples/path.hpp"
#include "examples/ref.hpp"
#include "examples/schema.hpp"
#include "examples/structs.hpp"
#include "examples/tools.hpp"

#include <cstdlib>
#include <cstring>

namespace
{

constexpr std::span<site::entry const> tables[] = {site::lazy_functions,   site::path_functions,  site::item_functions,
                                                   site::schema_functions, site::structs_functions, site::ref_functions,
                                                   site::tools_functions,  site::errors_functions};

std::string lines_run(std::string_view const encoded, std::string_view const lines)
{
    site::session s{tables, {}};
    s.variables.push_back({"bytes", site::value_of(std::string(encoded)), false});
    s.variables.push_back({"doc", site::value_of(cbor::lazy::take_ownership(std::string(encoded))), true});
    std::string out;
    for (auto const line : std::views::split(lines, '\n')) {
        std::string_view const text(line.begin(), line.end());
        if (text.find_first_not_of(" \t\r") == std::string_view::npos)
            continue;
        out += site::line_run(s, text);
        out += '\n';
    }
    return out;
}

}

extern "C" {

__attribute__((export_name("buffer"))) char *buffer(std::size_t const size)
{
    return static_cast<char *>(std::malloc(size));
}

__attribute__((export_name("release"))) void release(char *const p)
{
    std::free(p);
}

__attribute__((export_name("run"))) char *run(char const *const bytes, std::size_t const bytes_size,
                                              char const *const lines, std::size_t const lines_size)
{
    std::string const text = lines_run({bytes, bytes_size}, {lines, lines_size});
    auto const size = static_cast<std::uint32_t>(text.size());
    auto *const out = static_cast<char *>(std::malloc(sizeof size + text.size()));
    if (out == nullptr) [[unlikely]]
        std::abort();
    std::memcpy(out, &size, sizeof size);
    std::memcpy(out + sizeof size, text.data(), text.size());
    return out;
}
}
