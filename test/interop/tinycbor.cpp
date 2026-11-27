// TinyCBOR: the canonical dump of one top-level item, read with the CborValue API. TinyCBOR has no data
// model, so its re-encode walks the CborValue and writes each item with the CborEncoder API: the same head
// type, the same float width and the same tag numbers, with each length in its shortest head and each string
// in one chunk.
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
        return negative ? interop::dump_negative_integer(raw) : interop::dump_unsigned_integer(raw);
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
        return (text ? "t'" : "h'") + test::hexlify(s) + "'";
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
        return map ? interop::dump_sorted_map(parts) : interop::dump_array(parts);
    }
    case CborTagType: {
        CborTag tag = 0;
        cbor_value_get_tag(&it, &tag);
        if ((e = cbor_value_advance_fixed(&it)))
            return fail(e);
        auto c = value_dump(it);
        if (!c)
            return c;
        return interop::dump_unsigned_integer(tag) + "(" + *c + ")";
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
        return interop::dump_float(d);
    }
    case CborInvalidType:
        break;
    }
    return std::unexpected("invalid type");
}

CborError value_copy(CborValue &it, CborEncoder &out)
{
    CborError e = CborNoError;
    switch (cbor_value_get_type(&it)) {
    case CborIntegerType: {
        std::uint64_t raw = 0;
        cbor_value_get_raw_integer(&it, &raw);
        e = cbor_value_is_negative_integer(&it) ? cbor_encode_negative_int(&out, raw + 1)
                                                : cbor_encode_uint(&out, raw);
        return e ? e : cbor_value_advance_fixed(&it);
    }
    case CborByteStringType:
    case CborTextStringType: {
        bool const text = cbor_value_is_text_string(&it);
        std::size_t n = 0;
        if ((e = cbor_value_calculate_string_length(&it, &n)))
            return e;
        std::string s(n, '\0');
        CborValue next;
        e = text ? cbor_value_copy_text_string(&it, s.data(), &n, &next)
                 : cbor_value_copy_byte_string(&it, reinterpret_cast<std::uint8_t *>(s.data()), &n, &next);
        if (e)
            return e;
        it = next;
        return text ? cbor_encode_text_string(&out, s.data(), n)
                    : cbor_encode_byte_string(&out, reinterpret_cast<std::uint8_t const *>(s.data()), n);
    }
    case CborArrayType:
    case CborMapType: {
        bool const map = cbor_value_is_map(&it);
        std::size_t n = CborIndefiniteLength;
        if (cbor_value_is_length_known(&it) &&
            (e = map ? cbor_value_get_map_length(&it, &n) : cbor_value_get_array_length(&it, &n)))
            return e;
        CborValue in;
        CborEncoder inner;
        if ((e = cbor_value_enter_container(&it, &in)))
            return e;
        if ((e = map ? cbor_encoder_create_map(&out, &inner, n) : cbor_encoder_create_array(&out, &inner, n)))
            return e;
        while (!cbor_value_at_end(&in))
            if ((e = value_copy(in, inner)))
                return e;
        if ((e = cbor_encoder_close_container(&out, &inner)))
            return e;
        return cbor_value_leave_container(&it, &in);
    }
    case CborTagType: {
        CborTag tag = 0;
        cbor_value_get_tag(&it, &tag);
        if ((e = cbor_encode_tag(&out, tag)) || (e = cbor_value_advance_fixed(&it)))
            return e;
        return value_copy(it, out);
    }
    case CborSimpleType:
    case CborBooleanType:
    case CborNullType:
    case CborUndefinedType: {
        std::uint8_t s = 0;
        if (cbor_value_is_boolean(&it)) {
            bool b = false;
            cbor_value_get_boolean(&it, &b);
            e = cbor_encode_boolean(&out, b);
        } else if (cbor_value_is_null(&it)) {
            e = cbor_encode_null(&out);
        } else if (cbor_value_is_undefined(&it)) {
            e = cbor_encode_undefined(&out);
        } else {
            cbor_value_get_simple_type(&it, &s);
            e = cbor_encode_simple_value(&out, s);
        }
        return e ? e : cbor_value_advance_fixed(&it);
    }
    case CborHalfFloatType: {
        std::uint16_t h = 0;
        cbor_value_get_half_float(&it, &h);
        e = cbor_encode_half_float(&out, &h);
        return e ? e : cbor_value_advance_fixed(&it);
    }
    case CborFloatType: {
        float f = 0;
        cbor_value_get_float(&it, &f);
        e = cbor_encode_float(&out, f);
        return e ? e : cbor_value_advance_fixed(&it);
    }
    case CborDoubleType: {
        double d = 0;
        cbor_value_get_double(&it, &d);
        e = cbor_encode_double(&out, d);
        return e ? e : cbor_value_advance_fixed(&it);
    }
    case CborInvalidType:
        break;
    }
    return CborErrorUnknownType;
}

// A re-encode writes each item once, with a head no longer than the head it read, so twice the input holds
// it. A buffer that is too small gives CborErrorOutOfMemory, and the report lists it.
inline constexpr std::size_t buffer_bytes_per_input_byte = 2;
inline constexpr std::size_t buffer_bytes_for_one_head = 9;

std::expected<void, std::string> parser_init_validated(std::string const &bytes, CborParser &parser,
                                                       CborValue &it)
{
    auto const *const data = reinterpret_cast<std::uint8_t const *>(bytes.data());
    if (CborError const e = cbor_parser_init(data, bytes.size(), 0, &parser, &it))
        return std::unexpected(cbor_error_string(e));
    CborValue check = it;
    if (CborError const e = cbor_value_validate(&check, CborValidateBasic))
        return std::unexpected(cbor_error_string(e));
    return {};
}

}

int main(int const argc, char const *const *const argv)
{
    return interop::run(
        argc, argv,
        [](std::string const &bytes) -> result {
            CborParser parser;
            CborValue it;
            if (auto const p = parser_init_validated(bytes, parser, it); !p)
                return std::unexpected(p.error());
            auto r = value_dump(it);
            if (r && cbor_value_get_next_byte(&it) !=
                         reinterpret_cast<std::uint8_t const *>(bytes.data()) + bytes.size())
                return std::unexpected("bytes after the item");
            return r;
        },
        [](std::string const &bytes) -> result {
            CborParser parser;
            CborValue it;
            if (auto const p = parser_init_validated(bytes, parser, it); !p)
                return std::unexpected(p.error());
            std::string out(bytes.size() * buffer_bytes_per_input_byte + buffer_bytes_for_one_head, '\0');
            CborEncoder encoder;
            cbor_encoder_init(&encoder, reinterpret_cast<std::uint8_t *>(out.data()), out.size(), 0);
            if (CborError const e = value_copy(it, encoder))
                return fail(e);
            out.resize(
                cbor_encoder_get_buffer_size(&encoder, reinterpret_cast<std::uint8_t const *>(out.data())));
            return out;
        });
}
