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

std::shared_ptr<std::string const> loaded;
std::vector<std::uint32_t> row_offsets;

cbor::lazy row(std::uint32_t const i)
{
    return *cbor::lazy::view(loaded, std::string_view(*loaded).substr(row_offsets[i]));
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

__attribute__((export_name("rows_load"))) std::uint32_t rows_load(char const *const ptr, std::size_t const len)
{
    loaded = std::make_shared<std::string const>(ptr, len);
    auto const root = cbor::lazy::view(loaded);
    row_offsets.clear();
    auto const elements = root->elements();
    for (auto const r : *elements)
        row_offsets.push_back(static_cast<std::uint32_t>(r->offset));
    return static_cast<std::uint32_t>(row_offsets.size());
}

__attribute__((export_name("row_id"))) std::int32_t row_id(std::uint32_t const i)
{
    return static_cast<std::int32_t>(*row(i).at_key("id")->get<std::int64_t>());
}

__attribute__((export_name("row_label"))) std::uint64_t row_label(std::uint32_t const i)
{
    auto const label = row(i).at_key("label")->get<std::string_view>();
    return packed(**label);
}

__attribute__((export_name("rows_columns"))) void rows_columns(std::int32_t *const ids, std::uint32_t *const labels)
{
    for (std::uint32_t i = 0; i < row_offsets.size(); ++i) {
        cbor::lazy const r = row(i);
        ids[i] = static_cast<std::int32_t>(*r.at_key("id")->get<std::int64_t>());
        auto const label = r.at_key("label")->get<std::string_view>();
        std::string_view const s = **label;
        labels[2 * i] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(s.data()));
        labels[2 * i + 1] = static_cast<std::uint32_t>(s.size());
    }
}

__attribute__((export_name("row_find"))) std::int32_t row_find(std::int32_t const id)
{
    for (std::uint32_t i = 0; i < row_offsets.size(); ++i)
        if (*row(i).at_key("id")->get<std::int64_t>() == id)
            return static_cast<std::int32_t>(i);
    return -1;
}
}
