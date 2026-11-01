#include <cbor/cbor.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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

void render_children(std::span<cbor::item const> const children, std::string &out)
{
    for (cbor::item const &c : children)
        render(c, out);
}

void render(cbor::item const &i, std::string &out)
{
    if (auto const s = i.get<cbor::type::text_string>()) {
        escaped_append(out, *s);
        return;
    }
    if (auto const u = i.get<cbor::type::unsigned_integer>()) {
        out += std::to_string(*u);
        return;
    }
    if (auto const a = i.get<cbor::type::array>()) {
        render_children(*a, out);
        return;
    }
    auto const t = i.get<cbor::type::tag>();
    if (!t)
        return;
    auto const [number, content] = *t;
    auto const parts = content->get<cbor::type::array>();
    if (number == epoch_time) {
        std::string const d = date_of(*content->get<cbor::type::unsigned_integer>());
        out += "<time datetime=\"" + d + "\">" + d + "</time>";
    } else if (number == ui_table) {
        out += "<table><thead><tr>";
        for (cbor::item const &h : *(*parts)[0].get<cbor::type::array>()) {
            out += "<th>";
            render(h, out);
            out += "</th>";
        }
        out += "</tr></thead><tbody>";
        for (cbor::item const &row : *(*parts)[1].get<cbor::type::array>()) {
            out += "<tr>";
            for (cbor::item const &cell : *row.get<cbor::type::array>()) {
                out += "<td>";
                render(cell, out);
                out += "</td>";
            }
            out += "</tr>";
        }
        out += "</tbody></table>";
    } else if (number == ui_form) {
        out += "<form action=\"";
        render((*parts)[0], out);
        out += "\">";
        render((*parts)[1], out);
        out += "<button>Send</button></form>";
    } else if (number == ui_field) {
        out += "<label>";
        render((*parts)[1], out);
        out += " <input name=\"";
        render((*parts)[0], out);
        out += "\" type=\"";
        render((*parts)[2], out);
        out += "\"></label>";
    }
}

std::uint64_t packed(std::string_view const s)
{
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(s.data())) << 32) | s.size();
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

__attribute__((export_name("server_encode"))) std::uint64_t server_encode(std::uint32_t const rows)
{
    wire.clear();
    cbor::encoder<std::string> e{wire};
    std::ignore = e.array(2);
    std::ignore = e.tag(ui_table);
    std::ignore = e.array(2);
    std::ignore = e.array(3);
    std::ignore = e.text_string("Name <user>");
    std::ignore = e.text_string("Amount");
    std::ignore = e.text_string("Time");
    std::ignore = e.array(rows);
    for (std::uint32_t r = 0; r < rows; ++r) {
        std::ignore = e.array(3);
        std::ignore = e.text_string(std::string(names[r % std::size(names)]) + " & co " + std::to_string(r));
        std::ignore = e.unsigned_integer(std::uint64_t{r * 37u % 10000u});
        std::ignore = e.tag(epoch_time);
        std::ignore = e.unsigned_integer(std::uint64_t{1700000000u + r * 86400u});
    }
    std::ignore = e.tag(ui_form);
    std::ignore = e.array(2);
    std::ignore = e.text_string("/send");
    std::ignore = e.array(form_fields);
    for (std::uint32_t f = 0; f < form_fields; ++f) {
        std::ignore = e.tag(ui_field);
        std::ignore = e.array(3);
        std::ignore = e.text_string("field" + std::to_string(f));
        std::ignore = e.text_string("Field " + std::to_string(f));
        std::ignore = e.text_string(f % 2 ? "number" : "text");
    }
    std::ignore = e.finish();
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
}
