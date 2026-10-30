#include "examples/errors.hpp"
#include "examples/item.hpp"
#include "examples/lazy.hpp"
#include "examples/parser.hpp"
#include "examples/path.hpp"
#include "examples/ref.hpp"
#include "examples/schema.hpp"
#include "examples/structs.hpp"
#include "examples/tools.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace
{

constexpr std::span<site::entry const> tables[] = {site::lazy_functions,   site::path_functions,  site::item_functions,
                                                   site::schema_functions, site::structs_functions, site::ref_functions,
                                                   site::tools_functions,  site::errors_functions};

constexpr std::string_view groups[] = {"lazy", "path", "item", "schema", "structs", "ref", "tools", "errors"};

static_assert(std::size(groups) == std::size(tables));

consteval bool names_unique()
{
    for (std::span<site::entry const> const table : tables) {
        std::vector<std::string_view> names;
        for (site::entry const &e : table)
            names.push_back(e.name);
        std::ranges::sort(names);
        if (std::ranges::adjacent_find(names) != names.end())
            return false;
    }
    return true;
}

static_assert(names_unique(), "a table holds two entries of the same name");

std::string lines_run(std::string_view const encoded, std::string_view const lines, std::string_view const group)
{
    std::vector<std::span<site::entry const>> order;
    for (std::size_t i = 0; i < std::size(tables); ++i)
        if (groups[i] == group)
            order.push_back(tables[i]);
    for (std::size_t i = 0; i < std::size(tables); ++i)
        if (groups[i] != group)
            order.push_back(tables[i]);
    site::session s{order, {}};
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
                                              char const *const lines, std::size_t const lines_size,
                                              char const *const group, std::size_t const group_size)
{
    std::string const text = lines_run({bytes, bytes_size}, {lines, lines_size}, {group, group_size});
    auto const size = static_cast<std::uint32_t>(text.size());
    auto *const out = static_cast<char *>(std::malloc(sizeof size + text.size()));
    if (out == nullptr) [[unlikely]]
        std::abort();
    std::memcpy(out, &size, sizeof size);
    std::memcpy(out + sizeof size, text.data(), text.size());
    return out;
}
}
