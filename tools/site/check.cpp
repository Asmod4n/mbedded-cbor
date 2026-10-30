#include "token.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{

std::string file_read(std::filesystem::path const &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string text_of_html(std::string_view const html)
{
    std::string out;
    bool tag = false;
    for (std::size_t i = 0; i < html.size(); ++i) {
        char const c = html[i];
        if (tag) {
            tag = c != '>';
        } else if (c == '<') {
            tag = true;
        } else if (c == '&') {
            std::size_t const end = html.find(';', i);
            std::string_view const entity = html.substr(i, end - i + 1);
            out += entity == "&amp;" ? '&' : entity == "&lt;" ? '<' : '>';
            i = end;
        } else {
            out += c;
        }
    }
    return out;
}

bool file_checked(std::filesystem::path const &tree, std::filesystem::path const &out, std::filesystem::path const &cbor)
{
    std::string const name = cbor.stem().string();
    std::string const header = file_read(tree / "include/cbor" / name);
    auto const decoded = cbor::schema<site::token_file>::decode(file_read(cbor));
    if (!decoded) {
        std::fprintf(stderr, "check: %s does not decode\n", name.c_str());
        return false;
    }
    site::token_file const &file = **decoded;
    bool good = file.text == header;
    std::uint32_t end = 0;
    for (site::token const &t : file.tokens) {
        good = good && t.offset >= end && t.offset + t.length <= file.text.size() && t.length > 0 && t.kind < 5;
        end = t.offset + t.length;
    }
    std::string const page = file_read(out / "source" / (name + ".html"));
    std::string_view const open = "<pre><code>";
    std::string_view const close = "</code></pre>\n";
    good = good && page.starts_with(open) && page.ends_with(close) &&
           text_of_html(std::string_view(page).substr(open.size(), page.size() - open.size() - close.size())) == header;
    if (!good)
        std::fprintf(stderr, "check: %s differs from include/cbor/%s\n", name.c_str(), name.c_str());
    return good;
}

} // namespace

int main(int const argc, char const *const *const argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: check <tree> <out>\n");
        return 2;
    }
    std::filesystem::path const tree = argv[1];
    std::filesystem::path const out = argv[2];
    int checked = 0;
    int bad = 0;
    for (auto const &entry : std::filesystem::directory_iterator(out / "tokens")) {
        ++checked;
        bad += file_checked(tree, out, entry.path()) ? 0 : 1;
    }
    std::printf("check: %d files, %d differ\n", checked, bad);
    return bad == 0 && checked > 0 ? 0 : 1;
}
