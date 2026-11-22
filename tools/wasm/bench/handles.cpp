#include <cbor/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct series {
    std::uint32_t id;
    std::vector<std::int32_t> values;
};

CBOR_DEFINE_TYPE_MEMBERS(series, cbor::tag{1990}, id, values);

struct plain_series {
    std::uint32_t id;
    std::vector<std::int32_t> values;
};

CBOR_DEFINE_TYPE_MEMBERS(plain_series, cbor::untagged, id, values);

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
    auto const viewed = cbor::lazy::view(documents[doc].bytes, std::string_view(*documents[doc].bytes).substr(offset));
    return *viewed;
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

}

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
    auto const elements = root->elements();
    auto const first = *elements->begin();
    return static_cast<std::uint32_t>(root->encoded().size() - first->encoded().size());
}

__attribute__((export_name("arr_next"))) std::uint32_t arr_next(std::uint32_t const doc, std::uint32_t const offset)
{
    auto const item_size_read = cbor::item_size(bytes_of(doc).substr(offset));
    return offset + static_cast<std::uint32_t>(*item_size_read);
}

__attribute__((export_name("get_int"))) std::int64_t get_int(std::uint32_t const doc, std::uint32_t const offset)
{
    auto const read = lazy_at(doc, offset).get<std::int64_t>();
    return *read;
}

__attribute__((export_name("get_text"))) std::uint64_t get_text(std::uint32_t const doc, std::uint32_t const offset)
{
    auto const t = lazy_at(doc, offset).get<std::string_view>();
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>((**t).data())) << 32) | (**t).size();
}

__attribute__((export_name("ints_fill"))) std::uint32_t ints_fill(std::uint32_t const doc, std::uint32_t offset,
                                                                 std::uint32_t const n, std::int32_t *const out)
{
    std::string_view const bytes = bytes_of(doc);
    for (std::uint32_t i = 0; i < n; ++i) {
        auto const read = lazy_at(doc, offset).get<std::int64_t>();
        out[i] = static_cast<std::int32_t>(*read);
        auto const item_size_read = cbor::item_size(bytes.substr(offset));
        offset += static_cast<std::uint32_t>(*item_size_read);
    }
    return offset;
}

__attribute__((export_name("ints_iterate"))) std::uint32_t ints_iterate(std::uint32_t const doc, std::int32_t *const out)
{
    auto const root = cbor::lazy::view(documents[doc].bytes);
    std::uint32_t i = 0;
    auto const elements = root->elements();
    for (auto const e : *elements) {
        auto const number = e->get<std::int64_t>();
        out[i++] = static_cast<std::int32_t>(*number);
    }
    return i;
}

__attribute__((export_name("series_make"))) std::uint32_t series_make(std::uint32_t const n, std::int32_t const packed)
{
    std::vector<std::int32_t> values(n);
    for (std::uint32_t i = 0; i < n; ++i)
        values[i] = static_cast<std::int32_t>(1000 + i % 1000);
    if (packed) {
        auto const bytes = cbor::schema<series>::encode(series{7, std::move(values)});
        return doc_load(bytes->data(), bytes->size());
    }
    auto const bytes = cbor::encode<plain_series>(plain_series{7, std::move(values)});
    return doc_load(bytes->data(), bytes->size());
}

__attribute__((export_name("schema_id"))) std::int32_t schema_id(std::uint32_t const doc)
{
    auto const schema_read = cbor::schema<series>::at_path<"$.id">(bytes_of(doc));
    return static_cast<std::int32_t>(*schema_read);
}

__attribute__((export_name("lazy_id"))) std::int32_t lazy_id(std::uint32_t const doc)
{
    auto const root = cbor::lazy::view(documents[doc].bytes);
    auto const id = root->at_path<"$.id", std::int64_t>();
    return static_cast<std::int32_t>(*id);
}

__attribute__((export_name("lazy_values"))) std::uint64_t lazy_values(std::uint32_t const doc)
{
    auto const root = cbor::lazy::view(documents[doc].bytes);
    auto const t = root->at_path<"$.values", cbor::typed_array>();
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>((**t).bytes.data())) << 32) | (**t).bytes.size();
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
