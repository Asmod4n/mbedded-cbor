// TinyCBOR: the canonical dump of one top-level item, read with the CborValue API.
#include "dump.hpp"

#include <tinycbor/cbor.h>

namespace
{

using result = std::expected<std::string, std::string>;

result fail(CborError const e)
{
    return std::unexpected(cbor_error_string(e));
}

result value_dump(CborValue &it)
{
    CborError e = CborNoError;
    switch (cbor_value_get_type(&it)) {
    case CborIntegerType: {
        std::uint64_t raw = 0;
        cbor_value_get_raw_integer(&it, &raw);
        bool const negative = cbor_value_is_negative_integer(&it);
        if ((e = cbor_value_advance_fixed(&it)))
            return fail(e);
        return negative ? interop::negative_dump(raw) : interop::unsigned_dump(raw);
    }
    case CborByteStringType:
    case CborTextStringType: {
        bool const text = cbor_value_is_text_string(&it);
        std::size_t n = 0;
        if ((e = cbor_value_calculate_string_length(&it, &n)))
            return fail(e);
        std::string s(n, '\0');
        CborValue next;
        e = text ? cbor_value_copy_text_string(&it, s.data(), &n, &next)
                 : cbor_value_copy_byte_string(&it, reinterpret_cast<std::uint8_t *>(s.data()), &n, &next);
        if (e)
            return fail(e);
        it = next;
        return (text ? "t'" : "h'") + interop::hex_of(s) + "'";
    }
    case CborArrayType:
    case CborMapType: {
        bool const map = cbor_value_is_map(&it);
        CborValue in;
        if ((e = cbor_value_enter_container(&it, &in)))
            return fail(e);
        std::vector<std::string> parts;
        while (!cbor_value_at_end(&in)) {
            auto k = value_dump(in);
            if (!k)
                return k;
            if (map) {
                auto v = value_dump(in);
                if (!v)
                    return v;
                parts.push_back(*k + ":" + *v);
            } else {
                parts.push_back(*k);
            }
        }
        if ((e = cbor_value_leave_container(&it, &in)))
            return fail(e);
        return map ? interop::map_dump(parts) : interop::array_dump(parts);
    }
    case CborTagType: {
        CborTag tag = 0;
        cbor_value_get_tag(&it, &tag);
        if ((e = cbor_value_advance_fixed(&it)))
            return fail(e);
        auto c = value_dump(it);
        if (!c)
            return c;
        return interop::unsigned_dump(tag) + "(" + *c + ")";
    }
    case CborSimpleType:
    case CborBooleanType:
    case CborNullType:
    case CborUndefinedType: {
        std::uint8_t s = 0;
        if (cbor_value_is_boolean(&it)) {
            bool b = false;
            cbor_value_get_boolean(&it, &b);
            s = b ? 21 : 20;
        } else if (cbor_value_is_null(&it)) {
            s = 22;
        } else if (cbor_value_is_undefined(&it)) {
            s = 23;
        } else {
            cbor_value_get_simple_type(&it, &s);
        }
        if ((e = cbor_value_advance_fixed(&it)))
            return fail(e);
        return "s(" + std::to_string(s) + ")";
    }
    case CborHalfFloatType:
    case CborFloatType:
    case CborDoubleType: {
        double d = 0;
        if (cbor_value_is_half_float(&it)) {
            float f = 0;
            cbor_value_get_half_float_as_float(&it, &f);
            d = f;
        } else if (cbor_value_is_float(&it)) {
            float f = 0;
            cbor_value_get_float(&it, &f);
            d = f;
        } else {
            cbor_value_get_double(&it, &d);
        }
        if ((e = cbor_value_advance_fixed(&it)))
            return fail(e);
        return interop::float_dump(d);
    }
    case CborInvalidType:
        break;
    }
    return std::unexpected("invalid type");
}

}

int main()
{
    return interop::run([](std::string const &bytes) -> result {
        CborParser parser;
        CborValue it;
        auto const *const data = reinterpret_cast<std::uint8_t const *>(bytes.data());
        if (CborError const e = cbor_parser_init(data, bytes.size(), 0, &parser, &it))
            return fail(e);
        CborValue check = it;
        if (CborError const e = cbor_value_validate(&check, CborValidateBasic))
            return fail(e);
        auto r = value_dump(it);
        if (r && cbor_value_get_next_byte(&it) != data + bytes.size())
            return std::unexpected("bytes after the item");
        return r;
    });
}
