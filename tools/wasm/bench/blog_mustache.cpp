#include <cbor/cbor.hpp>
#include <mustache/mustache.hpp>

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
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

struct post_view {
    std::uint32_t id;
    std::string_view title;
    std::string_view author;
    std::string_view summary;
    std::uint32_t published;
};

struct page_view {
    std::vector<post_view> posts;
};

namespace
{
std::optional<mustache::TypedTemplate<page_view>> tpl;
std::string html;
std::optional<cbor::schema<feed>::accessor<>> root;
page_view view;
}

extern "C" {
__attribute__((export_name("alloc"))) void *alloc(std::size_t n) { return std::malloc(n); }
__attribute__((export_name("release"))) void release(void *p) { std::free(p); }
__attribute__((export_name("setup"))) int setup()
{
    auto t = mustache::compile<page_view>("{{#posts}}<article data-id=\"{{id}}\"><h2>{{title}}</h2><p class=\"by\">{{author}} {{published}}</p><p>{{summary}}</p></article>{{/posts}}");
    if (!t)
        return -1;
    tpl.emplace(std::move(*t));
    return 0;
}
__attribute__((export_name("feed_load"))) int feed_load(char const *p, std::size_t n)
{
    auto r = cbor::schema<feed>::path(std::string(p, n));
    if (!r)
        return -1;
    root.emplace(std::move(*r));
    return 0;
}
__attribute__((export_name("page_html"))) std::uint64_t page_html(std::uint32_t page, std::uint32_t per)
{
    std::uint32_t const count = static_cast<std::uint32_t>((*root->at_path<"$.posts">()).size());
    view.posts.clear();
    for (std::uint32_t i = page * per; i < count && i < (page + 1) * per; ++i)
        view.posts.push_back({*root->at_path<"$.posts[].id">(i), *root->at_path<"$.posts[].title">(i),
                              *root->at_path<"$.posts[].author">(i), *root->at_path<"$.posts[].summary">(i),
                              *root->at_path<"$.posts[].published">(i)});
    auto r = mustache::render(*tpl, view);
    html = r ? std::move(*r) : std::string();
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(html.data())) << 32) | html.size();
}
}
