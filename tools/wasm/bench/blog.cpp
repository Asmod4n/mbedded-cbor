#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct post {
    std::uint32_t id;
    std::string title;
    std::string author;
    std::string summary;
    std::uint32_t published;
};

struct feed {
    std::vector<post> posts;
};

CBOR_DEFINE_TYPE_MEMBERS(post, cbor::tag{2001}, id, title, author, summary, published);
CBOR_DEFINE_TYPE_MEMBERS(feed, cbor::tag{2002}, posts);

namespace
{

constexpr std::string_view authors[] = {"Ada", "Grace", "Linus", "Barbara", "Ken", "Margaret", "Dennis"};

std::optional<cbor::schema<feed>::accessor<>> root;
std::string encoded;
std::string html;

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

__attribute__((export_name("server_encode"))) std::uint64_t server_encode(std::uint32_t const n)
{
    feed f;
    for (std::uint32_t i = 0; i < n; ++i)
        f.posts.push_back(post{i + 1, "Post number " + std::to_string(i + 1) + " <draft>",
                               std::string(authors[i % std::size(authors)]),
                               "Summary of post " + std::to_string(i + 1) + ": " + std::string(120, 'x'),
                               1700000000u + i * 86400u});
    auto const encoded_2 = cbor::schema<feed>::encode(f);
    encoded = *encoded_2;
    return packed(encoded);
}

__attribute__((export_name("feed_load"))) std::int32_t feed_load(char const *const ptr, std::size_t const len)
{
    auto r = cbor::schema<feed>::adopt(std::string(ptr, len));
    if (!r)
        return -static_cast<std::int32_t>(r.error());
    root.emplace(std::move(*r));
    return 0;
}

__attribute__((export_name("post_count"))) std::uint32_t post_count()
{
    auto const found = root->at_path<"$.posts">();
    return static_cast<std::uint32_t>((*found).size());
}

__attribute__((export_name("post_id"))) std::uint32_t post_id(std::uint32_t const i)
{
    auto const found = root->at_path<"$.posts[].id">(i);
    return *found;
}

__attribute__((export_name("post_published"))) std::uint32_t post_published(std::uint32_t const i)
{
    auto const found = root->at_path<"$.posts[].published">(i);
    return *found;
}

__attribute__((export_name("post_title"))) std::uint64_t post_title(std::uint32_t const i)
{
    auto const found = root->at_path<"$.posts[].title">(i);
    return packed(*found);
}

__attribute__((export_name("post_author"))) std::uint64_t post_author(std::uint32_t const i)
{
    auto const found = root->at_path<"$.posts[].author">(i);
    return packed(*found);
}

__attribute__((export_name("post_summary"))) std::uint64_t post_summary(std::uint32_t const i)
{
    auto const found = root->at_path<"$.posts[].summary">(i);
    return packed(*found);
}

__attribute__((export_name("page_html"))) std::uint64_t page_html(std::uint32_t const page, std::uint32_t const per_page)
{
    html.clear();
    std::uint32_t const count = post_count();
    for (std::uint32_t i = page * per_page; i < count && i < (page + 1) * per_page; ++i) {
        html += "<article data-id=\"";
        auto const found = root->at_path<"$.posts[].id">(i);
        html += std::to_string(*found);
        html += "\"><h2>";
        auto const found_2 = root->at_path<"$.posts[].title">(i);
        escaped_append(html, *found_2);
        html += "</h2><p class=\"by\">";
        auto const found_3 = root->at_path<"$.posts[].author">(i);
        escaped_append(html, *found_3);
        html += " ";
        auto const found_4 = root->at_path<"$.posts[].published">(i);
        html += std::to_string(*found_4);
        html += "</p><p>";
        auto const found_5 = root->at_path<"$.posts[].summary">(i);
        escaped_append(html, *found_5);
        html += "</p></article>";
    }
    return packed(html);
}
}
