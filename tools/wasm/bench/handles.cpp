#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct document {
    std::shared_ptr<std::string const> bytes;
    std::uint32_t generation = 0;
    std::uint32_t references = 0;
};

struct handle {
    std::uint32_t doc = 0;
    std::uint32_t offset = 0;
    std::uint32_t generation = 0;
    bool live = false;
};

std::vector<document> documents;
std::vector<std::uint32_t> free_documents;
std::vector<handle> handles;
std::vector<std::uint32_t> free_handles;

std::string_view bytes_of(std::uint32_t const doc)
{
    return *documents[doc].bytes;
}

cbor::lazy lazy_at(std::uint32_t const doc, std::uint32_t const offset)
{
    auto const root = cbor::lazy::view(documents[doc].bytes);
    return cbor::lazy{root->top_level, static_cast<decltype(cbor::lazy::offset)>(offset)};
}

std::uint32_t handle_new(std::uint32_t const doc, std::uint32_t const offset)
{
    std::uint32_t id;
    if (free_handles.empty()) {
        id = static_cast<std::uint32_t>(handles.size());
        handles.push_back({});
    } else {
        id = free_handles.back();
        free_handles.pop_back();
    }
    handle &h = handles[id];
    h.doc = doc;
    h.offset = offset;
    ++h.generation;
    h.live = true;
    return id;
}

} // namespace

extern "C" {

__attribute__((export_name("alloc"))) void *alloc(std::size_t const size)
{
    return std::malloc(size);
}

__attribute__((export_name("doc_load"))) std::uint32_t doc_load(char const *const ptr, std::size_t const len)
{
    std::uint32_t id;
    if (free_documents.empty()) {
        id = static_cast<std::uint32_t>(documents.size());
        documents.push_back({});
    } else {
        id = free_documents.back();
        free_documents.pop_back();
    }
    documents[id].bytes = std::make_shared<std::string const>(ptr, len);
    ++documents[id].generation;
    documents[id].references = 1;
    return id;
}

__attribute__((export_name("doc_release"))) void doc_release(std::uint32_t const doc)
{
    if (--documents[doc].references == 0) {
        documents[doc].bytes.reset();
        free_documents.push_back(doc);
    }
}

__attribute__((export_name("arr_first"))) std::uint32_t arr_first(std::uint32_t const doc)
{
    auto const root = cbor::lazy::view(documents[doc].bytes);
    return static_cast<std::uint32_t>((*root->elements()->begin())->offset);
}

__attribute__((export_name("arr_next"))) std::uint32_t arr_next(std::uint32_t const doc, std::uint32_t const offset)
{
    return offset + static_cast<std::uint32_t>(*cbor::item_size(bytes_of(doc).substr(offset)));
}

__attribute__((export_name("get_int"))) std::int64_t get_int(std::uint32_t const doc, std::uint32_t const offset)
{
    return *lazy_at(doc, offset).get<std::int64_t>();
}

__attribute__((export_name("get_text"))) std::uint64_t get_text(std::uint32_t const doc, std::uint32_t const offset)
{
    auto const t = lazy_at(doc, offset).get<std::string_view>();
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>((**t).data())) << 32) | (**t).size();
}

__attribute__((export_name("h_new"))) std::uint32_t h_new(std::uint32_t const doc, std::uint32_t const offset)
{
    ++documents[doc].references;
    return handle_new(doc, offset);
}

__attribute__((export_name("h_get_int"))) std::int64_t h_get_int(std::uint32_t const h)
{
    return get_int(handles[h].doc, handles[h].offset);
}

__attribute__((export_name("h_release"))) void h_release(std::uint32_t const h)
{
    handles[h].live = false;
    doc_release(handles[h].doc);
    free_handles.push_back(h);
}
}
