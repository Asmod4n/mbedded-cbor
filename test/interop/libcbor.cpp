// libcbor: the canonical dump of one top-level item, read with cbor_load.
#include "dump.hpp"

#include <cbor.h>

namespace
{

std::string item_dump(cbor_item_t *const item)
{
    switch (cbor_typeof(item)) {
    case CBOR_TYPE_UINT:
        return interop::unsigned_dump(cbor_get_int(item));
    case CBOR_TYPE_NEGINT:
        return interop::negative_dump(cbor_get_int(item));
    case CBOR_TYPE_BYTESTRING:
        return "h'" + interop::hex_of({reinterpret_cast<char const *>(cbor_bytestring_handle(item)),
                                       cbor_bytestring_length(item)}) + "'";
    case CBOR_TYPE_STRING:
        return "t'" + interop::hex_of({reinterpret_cast<char const *>(cbor_string_handle(item)),
                                       cbor_string_length(item)}) + "'";
    case CBOR_TYPE_ARRAY: {
        std::vector<std::string> parts;
        for (std::size_t i = 0; i < cbor_array_size(item); ++i)
            parts.push_back(item_dump(cbor_array_handle(item)[i]));
        return interop::array_dump(parts);
    }
    case CBOR_TYPE_MAP: {
        std::vector<std::string> parts;
        for (std::size_t i = 0; i < cbor_map_size(item); ++i)
            parts.push_back(item_dump(cbor_map_handle(item)[i].key) + ":" + item_dump(cbor_map_handle(item)[i].value));
        return interop::map_dump(parts);
    }
    case CBOR_TYPE_TAG: {
        cbor_item_t *const content = cbor_tag_item(item);
        std::string const out = interop::unsigned_dump(cbor_tag_value(item)) + "(" + item_dump(content) + ")";
        cbor_decref(const_cast<cbor_item_t **>(&content));
        return out;
    }
    case CBOR_TYPE_FLOAT_CTRL:
        if (cbor_float_ctrl_is_ctrl(item))
            return "s(" + std::to_string(cbor_ctrl_value(item)) + ")";
        return interop::float_dump(cbor_float_get_float(item));
    }
    return "?";
}

}

int main()
{
    return interop::run([](std::string const &bytes) -> std::expected<std::string, std::string> {
        cbor_load_result r;
        cbor_item_t *item = cbor_load(reinterpret_cast<cbor_data>(bytes.data()), bytes.size(), &r);
        if (item == nullptr)
            return std::unexpected("cbor_load error " + std::to_string(r.error.code) + " at " +
                                   std::to_string(r.error.position));
        std::string const out = item_dump(item);
        cbor_decref(&item);
        if (r.read != bytes.size())
            return std::unexpected("bytes after the item");
        return out;
    });
}
