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
        if (link.empty())
            out += "<a id=\"L" + std::to_string(line) + "\"></a>";
        else
            out += "<a class=\"line\" data-line=\"" + std::to_string(line) + "\" href=\"" + std::string(link) + "#L" +
                   std::to_string(line) + "\"></a>";
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

struct declaration {
    std::string qualified;
    std::string display;
    bool definition;
    bool record;
    line_range range;
};

std::string display_of(CXCursor const cursor)
{
    CXString const s = clang_getCursorDisplayName(cursor);
    std::string out = clang_getCString(s);
    clang_disposeString(s);
    return out;
}

std::string qualified_of(CXCursor const cursor)
{
    std::string out = spelling_of(cursor);
    for (CXCursor p = clang_getCursorSemanticParent(cursor);
         !clang_Cursor_isNull(p) && clang_getCursorKind(p) != CXCursor_TranslationUnit; p = clang_getCursorSemanticParent(p))
        out = spelling_of(p) + "::" + out;
    return out;
}

bool kind_named(CXCursorKind const kind)
{
    switch (kind) {
    case CXCursor_FunctionDecl:
    case CXCursor_FunctionTemplate:
    case CXCursor_CXXMethod:
    case CXCursor_ClassDecl:
    case CXCursor_StructDecl:
    case CXCursor_ClassTemplate:
    case CXCursor_ClassTemplatePartialSpecialization:
    case CXCursor_VarDecl:
    case CXCursor_TypeAliasDecl:
    case CXCursor_TypeAliasTemplateDecl:
    case CXCursor_TypedefDecl:
    case CXCursor_EnumDecl:
    case CXCursor_ConceptDecl:
    case CXCursor_MacroDefinition:
    case CXCursor_UnexposedDecl:
        return true;
    default:
        return false;
    }
}

bool kind_record(CXCursorKind const kind)
{
    return kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl || kind == CXCursor_ClassTemplate ||
           kind == CXCursor_ClassTemplatePartialSpecialization || kind == CXCursor_EnumDecl;
}

struct declaration_walk {
    std::string include;
    std::vector<declaration> found;
};

CXChildVisitResult declaration_visit(CXCursor const cursor, CXCursor, CXClientData const data)
{
    auto &walk = *static_cast<declaration_walk *>(data);
    CXCursorKind const kind = clang_getCursorKind(cursor);
    if (kind_named(kind)) {
        CXSourceRange const extent = clang_getCursorExtent(cursor);
        CXFile file = nullptr;
        unsigned first = 0;
        unsigned last = 0;
        clang_getSpellingLocation(clang_getRangeStart(extent), &file, &first, nullptr, nullptr);
        clang_getSpellingLocation(clang_getRangeEnd(extent), nullptr, &last, nullptr, nullptr);
        CXString const name = clang_getFileName(file);
        std::string const path = file ? clang_getCString(name) : "";
        clang_disposeString(name);
        if (path.starts_with(walk.include))
            walk.found.push_back({kind == CXCursor_MacroDefinition ? spelling_of(cursor) : qualified_of(cursor),
                                  display_of(cursor), clang_isCursorDefinition(cursor) != 0, kind_record(kind),
                                  line_range{path, first, last}});
    }
    if (kind == CXCursor_FunctionDecl || kind == CXCursor_FunctionTemplate || kind == CXCursor_CXXMethod ||
        kind == CXCursor_VarDecl || kind == CXCursor_MacroDefinition)
        return CXChildVisit_Continue;
    return CXChildVisit_Recurse;
}

std::string template_arguments_stripped(std::string_view const name)
{
    std::string out;
    std::size_t depth = 0;
    for (char const c : name) {
        if (c == '<')
            ++depth;
        else if (c == '>')
            --depth;
        else if (depth == 0)
            out += c;
    }
    return out;
}

std::vector<line_range> ranges_of(std::vector<declaration> const &all, std::string_view const name)
{
    std::size_t cut = 0;
    std::size_t depth = 0;
    for (std::size_t i = 0; i + 1 < name.size(); ++i) {
        if (name[i] == '<')
            ++depth;
        else if (name[i] == '>')
            --depth;
        else if (depth == 0 && name[i] == ':' && name[i + 1] == ':')
            cut = i + 2;
    }
    std::string_view const last = name.substr(cut);
    std::string const qualified = template_arguments_stripped(name);
    bool const by_display = last.find('<') != std::string_view::npos;
    auto const named = [&](declaration const &d) {
        if (d.qualified != qualified)
            return false;
        return !by_display || d.display.starts_with(last.substr(0, last.size() - 1));
    };
    bool const defined = std::ranges::any_of(all, [&](declaration const &d) { return named(d) && d.record && d.definition; });
    std::vector<line_range> out;
    for (declaration const &d : all) {
        if (!named(d) || (d.record && defined && !d.definition))
            continue;
        if (std::ranges::contains(out, std::pair{d.range.path, d.range.first},
                                  [](line_range const &r) { return std::pair{r.path, r.first}; }))
            continue;
        out.push_back(d.range);
    }
    return out;
}

} // namespace site

int main(int const argc, char const *const *const argv)
{
    if (argc != 7) {
        std::fprintf(stderr, "usage: tokens <tree> <out> <url of include/cbor at the commit> <names> <types file> <url of the types file>\n");
        return 2;
    }
    std::filesystem::path const tree = argv[1];
    std::filesystem::path const out = argv[2];
    std::string const source_url = argv[3];
    std::filesystem::path const names = argv[4];
    std::filesystem::path const types = argv[5];
    std::string const types_url = argv[6];
    std::string const include = "-I" + (tree / "include").string();
    std::string const pfr = "-I" + (tree / "third_party/boost_pfr/include").string();
    char const *const arguments[] = {"-x", "c++", "-std=c++23", include.c_str(), pfr.c_str()};
    std::string const root = (tree / "include/cbor/cbor.hpp").string();
    CXIndex const index = clang_createIndex(0, 1);
    CXTranslationUnit unit = nullptr;
    if (clang_parseTranslationUnit2(index, root.c_str(), arguments, std::size(arguments), nullptr, 0,
                                    CXTranslationUnit_KeepGoing | CXTranslationUnit_DetailedPreprocessingRecord, &unit) != CXError_Success) {
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
    site::declaration_walk walk{(std::filesystem::canonical(tree / "include/cbor").string() + "/"), {}};
    clang_visitChildren(clang_getTranslationUnitCursor(unit), site::declaration_visit, &walk);
    std::ifstream pages(names);
    std::size_t written = 0;
    for (std::string line; std::getline(pages, line);) {
        std::size_t const tab = line.find('\t');
        std::string const page = line.substr(0, tab);
        std::vector<site::line_range> ranges;
        for (std::size_t at = tab; at != std::string::npos;) {
            std::size_t const next = line.find('\t', at + 1);
            auto const found = site::ranges_of(walk.found, line.substr(at + 1, next == std::string::npos ? next : next - at - 1));
            ranges.insert(ranges.end(), found.begin(), found.end());
            at = next;
        }
        if (ranges.empty()) {
            std::printf("missing %s\n", page.c_str());
            continue;
        }
        std::string impl;
        for (site::line_range const &range : ranges) {
            std::string const name = std::filesystem::path(range.path).filename().string();
            auto const file = std::ranges::find(files, "include/cbor/" + name, &site::token_file::path);
            impl += "<pre><code>" +
                    site::highlighted(file->text, file->tokens, range.first, range.last, source_url + "/" + name) +
                    "</code></pre>\n";
        }
        site::file_write(out / "impl" / (page + ".html"), impl);
        ++written;
    }
    clang_disposeTranslationUnit(unit);
    std::string const types_path = (tree / types).string();
    CXTranslationUnit types_unit = nullptr;
    if (clang_parseTranslationUnit2(index, types_path.c_str(), arguments, std::size(arguments), nullptr, 0,
                                    CXTranslationUnit_KeepGoing, &types_unit) != CXError_Success) {
        std::fprintf(stderr, "tokens: libclang cannot parse %s\n", types_path.c_str());
        return 1;
    }
    std::string const types_text = site::file_read(tree / types);
    auto const types_tokens = site::tokens_read(types_unit, clang_getFile(types_unit, types_path.c_str()),
                                                static_cast<std::uint32_t>(types_text.size()));
    site::file_write(out / "types" / (types.filename().string() + ".html"),
                     site::highlighted(types_text, types_tokens, 1, UINT32_MAX, types_url));
    clang_disposeTranslationUnit(types_unit);
    clang_disposeIndex(index);
    std::printf("tokens: %zu headers, %zu pages with an implementation\n", files.size(), written);
    return 0;
}
