#include <cbor/cbor.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

struct moment {
    std::uint32_t epoch;
};

struct row {
    std::string name;
    std::uint32_t amount;
    moment time;
};

struct table {
    std::vector<std::string> head;
    std::vector<row> rows;
};

struct field {
    std::string name;
    std::string label;
    std::string input;
};

struct form {
    std::string action;
    std::vector<field> fields;
};

struct screen {
    table list;
    form entry;
};

CBOR_DEFINE_TYPE_MEMBERS(moment, cbor::tag{40103}, epoch);
CBOR_DEFINE_TYPE_MEMBERS(row, cbor::tag{40104}, name, amount, time);
CBOR_DEFINE_TYPE_MEMBERS(table, cbor::tag{40100}, head, rows);
CBOR_DEFINE_TYPE_MEMBERS(field, cbor::tag{40102}, name, label, input);
CBOR_DEFINE_TYPE_MEMBERS(form, cbor::tag{40101}, action, fields);
CBOR_DEFINE_TYPE_MEMBERS(screen, cbor::tag{40105}, list, entry);

struct server_row {
    std::string name;
    std::uint32_t amount;
    std::chrono::sys_seconds time;
};

struct server_table {
    std::vector<std::string> head;
    std::vector<server_row> rows;
};

struct server_field {
    std::string name;
    std::string label;
    std::string input;
};

struct server_form {
    std::string action;
    std::vector<server_field> fields;
};

CBOR_DEFINE_TYPE_MEMBERS(server_row, cbor::untagged, name, amount, time);
CBOR_DEFINE_TYPE_MEMBERS(server_table, cbor::tag{40100}, head, rows);
CBOR_DEFINE_TYPE_MEMBERS(server_field, cbor::tag{40102}, name, label, input);
CBOR_DEFINE_TYPE_MEMBERS(server_form, cbor::tag{40101}, action, fields);

namespace
{

constexpr std::uint64_t ui_table = 40100;
constexpr std::uint64_t ui_form = 40101;
constexpr std::uint64_t ui_field = 40102;
constexpr std::uint64_t epoch_time = 1;
constexpr std::string_view names[] = {"Ada", "Grace", "Linus", "Barbara", "Ken", "Margaret", "Dennis"};
constexpr std::uint32_t form_fields = 5;

std::string wire;
std::string html;
std::optional<cbor::owning<cbor::item>> tree;
std::string schema_wire;
std::optional<cbor::schema<screen>::accessor<>> root;

void escaped_append(std::string &out, std::string_view const text)
{
    for (char const c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
}

std::string date_of(std::uint64_t const seconds)
{
    std::chrono::year_month_day const d{std::chrono::floor<std::chrono::days>(std::chrono::sys_seconds{std::chrono::seconds{seconds}})};
    char buffer[16];
    auto const n = std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02u", static_cast<int>(d.year()), static_cast<unsigned>(d.month()),
                                 static_cast<unsigned>(d.day()));
    return std::string(buffer, static_cast<std::size_t>(n));
}

void render(cbor::item const &i, std::string &out);

cbor::item const &value_of(cbor::item const &map, std::string_view const key)
{
    auto const pairs = *map.get<std::ranges::chunk_view<std::span<cbor::item const>>>();
    auto const at = std::ranges::find(pairs, key, [](auto const pair) { return *pair[0].template get<std::string_view>(); });
    return (*at)[1];
}

void render_children(std::span<cbor::item const> const children, std::string &out)
{
    for (cbor::item const &c : children)
        render(c, out);
}

void render(cbor::item const &i, std::string &out)
{
    switch (i.type()) {
    case cbor::type::text_string:
        escaped_append(out, *i.get<std::string_view>());
        return;
    case cbor::type::unsigned_integer:
        out += std::to_string(*i.get<std::uint64_t>());
        return;
    case cbor::type::array:
        render_children(*i.get<std::span<cbor::item const>>(), out);
        return;
    case cbor::type::tag:
        break;
    default:
        return;
    }
    auto const [number, content] = *i.get<std::pair<std::uint64_t, cbor::item const *>>();
    cbor::item const &m = *content;
    if (number == epoch_time) {
        std::string const d = date_of(*content->get<std::uint64_t>());
        out += "<time datetime=\"" + d + "\">" + d + "</time>";
    } else if (number == ui_table) {
        out += "<table><thead><tr>";
        for (cbor::item const &h : *value_of(m, "head").get<std::span<cbor::item const>>()) {
            out += "<th>";
            render(h, out);
            out += "</th>";
        }
        out += "</tr></thead><tbody>";
        for (cbor::item const &row : *value_of(m, "rows").get<std::span<cbor::item const>>()) {
            out += "<tr>";
            for (std::string_view const key : {"name", "amount", "time"}) {
                out += "<td>";
                render(value_of(row, key), out);
                out += "</td>";
            }
            out += "</tr>";
        }
        out += "</tbody></table>";
    } else if (number == ui_form) {
        out += "<form action=\"";
        render(value_of(m, "action"), out);
        out += "\">";
        render(value_of(m, "fields"), out);
        out += "<button>Send</button></form>";
    } else if (number == ui_field) {
        out += "<label>";
        render(value_of(m, "label"), out);
        out += " <input name=\"";
        render(value_of(m, "name"), out);
        out += "\" type=\"";
        render(value_of(m, "input"), out);
        out += "\"></label>";
    }
}

std::uint64_t packed(std::string_view const s)
{
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(s.data())) << 32) | s.size();
}

}

extern "C" {

__attribute__((export_name("alloc"))) void *alloc(std::size_t const size)
{
    return std::malloc(size);
}

__attribute__((export_name("release"))) void release(void *const ptr)
{
    std::free(ptr);
}

__attribute__((export_name("server_encode"))) std::uint64_t server_encode(std::uint32_t const rows)
{
    std::tuple<server_table, server_form> message{{{"Name <user>", "Amount", "Time"}, {}}, {"/send", {}}};
    for (std::uint32_t r = 0; r < rows; ++r)
        std::get<0>(message).rows.push_back(
            server_row{std::string(names[r % std::size(names)]) + " & co " + std::to_string(r), r * 37u % 10000u,
                       std::chrono::sys_seconds{std::chrono::seconds{1700000000u + r * 86400u}}});
    for (std::uint32_t f = 0; f < form_fields; ++f)
        std::get<1>(message).fields.push_back(
            server_field{"field" + std::to_string(f), "Field " + std::to_string(f), f % 2 ? "number" : "text"});
    wire = *cbor::structs<std::tuple<server_table, server_form>>::encode(message);
    return packed(wire);
}

__attribute__((export_name("ui_load"))) std::int32_t ui_load(char const *const ptr, std::size_t const len)
{
    auto t = cbor::decode(std::string_view(ptr, len));
    if (!t)
        return -static_cast<std::int32_t>(t.error());
    tree.emplace(std::move(*t));
    return 0;
}

__attribute__((export_name("ui_html"))) std::uint64_t ui_html()
{
    html.clear();
    render(**tree, html);
    return packed(html);
}

__attribute__((export_name("schema_encode"))) std::uint64_t schema_encode(std::uint32_t const rows)
{
    screen s{{{"Name <user>", "Amount", "Time"}, {}}, {"/send", {}}};
    for (std::uint32_t r = 0; r < rows; ++r)
        s.list.rows.push_back(row{std::string(names[r % std::size(names)]) + " & co " + std::to_string(r), r * 37u % 10000u,
                                  moment{1700000000u + r * 86400u}});
    for (std::uint32_t f = 0; f < form_fields; ++f)
        s.entry.fields.push_back(field{"field" + std::to_string(f), "Field " + std::to_string(f), f % 2 ? "number" : "text"});
    schema_wire = *cbor::schema<screen>::encode(s);
    return packed(schema_wire);
}

__attribute__((export_name("schema_apart_encode"))) std::uint64_t schema_apart_encode(std::uint32_t const rows)
{
    screen s{{{"Name <user>", "Amount", "Time"}, {}}, {"/send", {}}};
    for (std::uint32_t r = 0; r < rows; ++r)
        s.list.rows.push_back(row{std::string(names[r % std::size(names)]) + " & co " + std::to_string(r), r * 37u % 10000u,
                                  moment{1700000000u + r * 86400u}});
    for (std::uint32_t f = 0; f < form_fields; ++f)
        s.entry.fields.push_back(field{"field" + std::to_string(f), "Field " + std::to_string(f), f % 2 ? "number" : "text"});
    schema_wire = *cbor::static_dictionary<screen>::encode(s);
    return packed(schema_wire);
}

__attribute__((export_name("schema_description_size"))) std::uint32_t schema_description_size()
{
    return static_cast<std::uint32_t>(cbor::schema<screen>::description().size());
}

__attribute__((export_name("schema_apart_load"))) std::int32_t schema_apart_load(char const *const ptr, std::size_t const len)
{
    auto r = cbor::static_dictionary<screen>::adopt(std::string(ptr, len));
    if (!r)
        return -static_cast<std::int32_t>(r.error());
    root.emplace(std::move(*r));
    return 0;
}

__attribute__((export_name("schema_load"))) std::int32_t schema_load(char const *const ptr, std::size_t const len)
{
    auto r = cbor::schema<screen>::adopt(std::string(ptr, len));
    if (!r)
        return -static_cast<std::int32_t>(r.error());
    root.emplace(std::move(*r));
    return 0;
}

__attribute__((export_name("schema_html"))) std::uint64_t schema_html()
{
    html.clear();
    html += "<table><thead><tr>";
    std::size_t const heads = (*root->at_path<"$.list.head">()).size();
    for (std::size_t i = 0; i < heads; ++i) {
        html += "<th>";
        escaped_append(html, *root->at_path<"$.list.head[]">(i));
        html += "</th>";
    }
    html += "</tr></thead><tbody>";
    std::size_t const rows = (*root->at_path<"$.list.rows">()).size();
    for (std::size_t i = 0; i < rows; ++i) {
        html += "<tr><td>";
        escaped_append(html, *root->at_path<"$.list.rows[].name">(i));
        html += "</td><td>";
        html += std::to_string(*root->at_path<"$.list.rows[].amount">(i));
        std::string const d = date_of(*root->at_path<"$.list.rows[].time.epoch">(i));
        html += "</td><td><time datetime=\"" + d + "\">" + d + "</time></td></tr>";
    }
    html += "</tbody></table><form action=\"";
    escaped_append(html, *root->at_path<"$.entry.action">());
    html += "\">";
    std::size_t const fields = (*root->at_path<"$.entry.fields">()).size();
    for (std::size_t i = 0; i < fields; ++i) {
        html += "<label>";
        escaped_append(html, *root->at_path<"$.entry.fields[].label">(i));
        html += " <input name=\"";
        escaped_append(html, *root->at_path<"$.entry.fields[].name">(i));
        html += "\" type=\"";
        escaped_append(html, *root->at_path<"$.entry.fields[].input">(i));
        html += "\"></label>";
    }
    html += "<button>Send</button></form>";
    return packed(html);
}
}
