#include "token.hpp"

#include <clang-c/Index.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace site
{

constexpr std::string_view kind_names[] = {"punctuation", "keyword", "identifier", "literal", "comment"};

std::string file_read(std::filesystem::path const &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void file_write(std::filesystem::path const &path, std::string_view const text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
}

std::string html_escaped(std::string_view const text)
{
    std::string out;
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
        default:
            out += c;
        }
    }
    return out;
}

std::uint32_t offset_of(CXSourceLocation const location)
{
    unsigned offset = 0;
    clang_getSpellingLocation(location, nullptr, nullptr, nullptr, &offset);
    return offset;
}

std::vector<token> tokens_read(CXTranslationUnit const unit, CXFile const file, std::uint32_t const size)
{
    CXSourceRange const whole =
        clang_getRange(clang_getLocationForOffset(unit, file, 0), clang_getLocationForOffset(unit, file, size));
    CXToken *found = nullptr;
    unsigned count = 0;
    clang_tokenize(unit, whole, &found, &count);
    std::vector<token> out;
    for (CXToken const &t : std::span(found, count)) {
        CXSourceRange const extent = clang_getTokenExtent(unit, t);
        std::uint32_t const begin = offset_of(clang_getRangeStart(extent));
        std::uint32_t const end = offset_of(clang_getRangeEnd(extent));
        out.push_back(token{static_cast<std::uint8_t>(clang_getTokenKind(t)), begin, end - begin});
    }
    clang_disposeTokens(unit, found, count);
    return out;
}

std::string highlighted(std::string_view const text, std::vector<token> const &tokens, std::uint32_t const first,
                        std::uint32_t const last, std::string_view const link)
{
    std::string out;
    std::uint32_t line = 1;
    std::uint32_t at = 0;
    std::size_t next = 0;
    auto const line_open = [&] {
        if (line < first || line > last)
            return;
        out += "<a id=\"L" + std::to_string(line) + "\"";
        if (!link.empty())
            out += " href=\"" + std::string(link) + "#L" + std::to_string(line) + "\"";
        out += "></a>";
    };
    auto const plain = [&](std::string_view const piece) {
        for (char const c : piece) {
            if (line >= first && line <= last)
                out += html_escaped(std::string_view(&c, 1));
            if (c == '\n') {
                ++line;
                line_open();
            }
        }
    };
    line_open();
    while (at < text.size()) {
        if (next < tokens.size() && tokens[next].offset == at) {
            token const &t = tokens[next++];
            bool const shown = line >= first && line <= last;
            if (shown)
                out += "<span class=\"" + std::string(kind_names[t.kind]) + "\">";
            plain(text.substr(t.offset, t.length));
            if (shown)
                out += "</span>";
            at = t.offset + t.length;
        } else {
            std::uint32_t const until = next < tokens.size() ? tokens[next].offset : static_cast<std::uint32_t>(text.size());
            plain(text.substr(at, until - at));
            at = until;
        }
    }
    return out;
}

std::string usr_of(CXCursor const cursor)
{
    CXString const s = clang_getCursorUSR(cursor);
    std::string out = clang_getCString(s);
    clang_disposeString(s);
    return out;
}

std::string spelling_of(CXCursor const cursor)
{
    CXString const s = clang_getCursorSpelling(cursor);
    std::string out = clang_getCString(s);
    clang_disposeString(s);
    return out;
}

struct member_query {
    std::string parent_usr;
    std::string name;
    std::vector<line_range> found;
};

CXChildVisitResult member_visit(CXCursor const cursor, CXCursor, CXClientData const data)
{
    auto &query = *static_cast<member_query *>(data);
    CXCursorKind const kind = clang_getCursorKind(cursor);
    if ((kind == CXCursor_FunctionTemplate || kind == CXCursor_CXXMethod) && spelling_of(cursor) == query.name &&
        usr_of(clang_getCursorSemanticParent(cursor)) == query.parent_usr) {
        CXSourceRange const extent = clang_getCursorExtent(cursor);
        CXFile file = nullptr;
        unsigned first = 0;
        unsigned last = 0;
        clang_getSpellingLocation(clang_getRangeStart(extent), &file, &first, nullptr, nullptr);
        clang_getSpellingLocation(clang_getRangeEnd(extent), nullptr, &last, nullptr, nullptr);
        CXString const name = clang_getFileName(file);
        query.found.push_back(line_range{clang_getCString(name), first, last});
        clang_disposeString(name);
        return CXChildVisit_Continue;
    }
    return CXChildVisit_Recurse;
}

} // namespace site

int main(int const argc, char const *const *const argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: tokens <tree> <out>\n");
        return 2;
    }
    std::filesystem::path const tree = argv[1];
    std::filesystem::path const out = argv[2];
    std::string const include = "-I" + (tree / "include").string();
    std::string const pfr = "-I" + (tree / "third_party/boost_pfr/include").string();
    char const *const arguments[] = {"-x", "c++", "-std=c++23", include.c_str(), pfr.c_str()};
    std::string const root = (tree / "include/cbor/cbor.hpp").string();
    CXIndex const index = clang_createIndex(0, 1);
    CXTranslationUnit unit = nullptr;
    if (clang_parseTranslationUnit2(index, root.c_str(), arguments, std::size(arguments), nullptr, 0,
                                    CXTranslationUnit_KeepGoing, &unit) != CXError_Success) {
        std::fprintf(stderr, "tokens: libclang cannot parse %s\n", root.c_str());
        return 1;
    }
    std::vector<std::filesystem::path> headers;
    for (auto const &entry : std::filesystem::directory_iterator(tree / "include/cbor"))
        if (entry.path().extension() == ".hpp")
            headers.push_back(entry.path());
    std::ranges::sort(headers);
    std::vector<site::token_file> files;
    for (auto const &header : headers) {
        std::string const path = std::filesystem::canonical(header).string();
        CXFile const file = clang_getFile(unit, path.c_str());
        if (file == nullptr) {
            std::fprintf(stderr, "tokens: %s is not part of %s\n", path.c_str(), root.c_str());
            return 1;
        }
        std::string text = site::file_read(header);
        auto tokens = site::tokens_read(unit, file, static_cast<std::uint32_t>(text.size()));
        std::string const name = header.filename().string();
        site::file_write(out / "source" / (name + ".html"),
                         "<pre><code>" + site::highlighted(text, tokens, 1, UINT32_MAX, {}) + "</code></pre>\n");
        site::token_file entry{"include/cbor/" + name, std::move(text), std::move(tokens)};
        auto const encoded = cbor::schema<site::token_file>::encode(entry);
        if (!encoded) {
            std::fprintf(stderr, "tokens: encode of %s failed\n", name.c_str());
            return 1;
        }
        site::file_write(out / "tokens" / (name + ".cbor"), *encoded);
        files.push_back(std::move(entry));
    }
    site::member_query query{"c:@N@cbor@S@lazy", "at_path", {}};
    clang_visitChildren(clang_getTranslationUnitCursor(unit), site::member_visit, &query);
    std::string impl;
    for (site::line_range const &range : query.found) {
        std::string const name = std::filesystem::path(range.path).filename().string();
        auto const file = std::ranges::find(files, "include/cbor/" + name, &site::token_file::path);
        impl += "<pre><code>" +
                site::highlighted(file->text, file->tokens, range.first, range.last, {}) +
                "</code></pre>\n";
    }
    site::file_write(out / "impl/lazy/at_path.html", impl);
    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
    std::printf("tokens: %zu headers, %zu ranges of lazy::at_path\n", files.size(), query.found.size());
    return 0;
}
