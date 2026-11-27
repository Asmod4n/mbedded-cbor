// jsoncons: the canonical dump of one top-level item, read with decode_cbor into an ojson. jsoncons maps a
// tag that it knows to a semantic_tag. It drops the number of a tag that it does not know, except on a byte
// string, which keeps it as its ext_tag. Map keys that are not text become text in jsoncons. The dump writes
// each semantic_tag as the tag number and the content that the cbor_encoder of jsoncons writes for it, so the
// dump of a value equals the dump of the bytes that encode_cbor writes. A bigint is its decimal value, as
// check.py reads a bignum. A bigdec, a bigfloat and a float128 stay "<semantic_tag:text>", which check.py
// counts as not comparable. The re-encode writes the ojson of decode_cbor with encode_cbor.
#include "dump.hpp"

#include <jsoncons/json.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>

#include <cstdint>

namespace
{

inline constexpr std::uint64_t datetime_tag = 0;
inline constexpr std::uint64_t epoch_tag = 1;
inline constexpr std::uint64_t base64url_bytes_tag = 21;
inline constexpr std::uint64_t base64_bytes_tag = 22;
inline constexpr std::uint64_t base16_bytes_tag = 23;
inline constexpr std::uint64_t uri_tag = 32;
inline constexpr std::uint64_t base64url_text_tag = 33;
inline constexpr std::uint64_t base64_text_tag = 34;
inline constexpr double millis_in_second = 1e3;
inline constexpr double nanos_in_second = 1e9;

std::string epoch_dump(double const value, jsoncons::semantic_tag const tag)
{
    switch (tag) {
    case jsoncons::semantic_tag::epoch_milli:
        return interop::dump_tag(epoch_tag, interop::dump_float(value / millis_in_second));
    case jsoncons::semantic_tag::epoch_nano:
        return interop::dump_tag(epoch_tag, interop::dump_float(value / nanos_in_second));
    default:
        return interop::dump_tag(epoch_tag, interop::dump_float(value));
    }
}

std::string integer_dump(std::string const &text, jsoncons::semantic_tag const tag, double const value)
{
    switch (tag) {
    case jsoncons::semantic_tag::epoch_second:
        return interop::dump_tag(epoch_tag, text);
    case jsoncons::semantic_tag::epoch_milli:
    case jsoncons::semantic_tag::epoch_nano:
        return epoch_dump(value, tag);
    default:
        return text;
    }
}

std::string text_dump(std::string_view const s, jsoncons::semantic_tag const tag)
{
    std::string const text = "t'" + test::hexlify(s) + "'";
    switch (tag) {
    case jsoncons::semantic_tag::bigint:
        return std::string(s);
    case jsoncons::semantic_tag::bigdec:
        return "<bigdec:" + std::string(s) + ">";
    case jsoncons::semantic_tag::bigfloat:
        return "<bigfloat:" + std::string(s) + ">";
    case jsoncons::semantic_tag::float128:
        return "<float128:" + std::string(s) + ">";
    case jsoncons::semantic_tag::datetime:
        return interop::dump_tag(datetime_tag, text);
    case jsoncons::semantic_tag::uri:
        return interop::dump_tag(uri_tag, text);
    case jsoncons::semantic_tag::base64url:
        return interop::dump_tag(base64url_text_tag, text);
    case jsoncons::semantic_tag::base64:
        return interop::dump_tag(base64_text_tag, text);
    default:
        return text;
    }
}

std::string bytes_dump(jsoncons::ojson const &j)
{
    auto const b = j.as_byte_string_view();
    std::string const bytes =
        "h'" + test::hexlify({reinterpret_cast<char const *>(b.data()), b.size()}) + "'";
    switch (j.tag()) {
    case jsoncons::semantic_tag::base64url:
        return interop::dump_tag(base64url_bytes_tag, bytes);
    case jsoncons::semantic_tag::base64:
        return interop::dump_tag(base64_bytes_tag, bytes);
    case jsoncons::semantic_tag::base16:
        return interop::dump_tag(base16_bytes_tag, bytes);
    case jsoncons::semantic_tag::ext:
        return interop::dump_tag(j.ext_tag(), bytes);
    default:
        return bytes;
    }
}

std::string value_dump(jsoncons::ojson const &j)
{
    switch (j.type()) {
    case jsoncons::json_type::null_value:
        return j.tag() == jsoncons::semantic_tag::undefined ? "s(23)" : "s(22)";
    case jsoncons::json_type::bool_value:
        return j.as<bool>() ? "s(21)" : "s(20)";
    case jsoncons::json_type::int64_value: {
        auto const v = j.as<std::int64_t>();
        return integer_dump(v < 0 ? interop::dump_negative_integer(static_cast<std::uint64_t>(-(v + 1)))
                                  : interop::dump_unsigned_integer(static_cast<std::uint64_t>(v)),
                            j.tag(), static_cast<double>(v));
    }
    case jsoncons::json_type::uint64_value: {
        auto const v = j.as<std::uint64_t>();
        return integer_dump(interop::dump_unsigned_integer(v), j.tag(), static_cast<double>(v));
    }
    case jsoncons::json_type::half_value:
    case jsoncons::json_type::double_value:
        if (j.tag() == jsoncons::semantic_tag::epoch_second ||
            j.tag() == jsoncons::semantic_tag::epoch_milli || j.tag() == jsoncons::semantic_tag::epoch_nano)
            return epoch_dump(j.as<double>(), j.tag());
        return interop::dump_float(j.as<double>());
    case jsoncons::json_type::string_value:
        return text_dump(j.as_string_view(), j.tag());
    case jsoncons::json_type::byte_string_value:
        return bytes_dump(j);
    case jsoncons::json_type::array_value: {
        std::vector<std::string> parts;
        for (auto const &e : j.array_range())
            parts.push_back(value_dump(e));
        return interop::dump_array(parts);
    }
    case jsoncons::json_type::object_value: {
        std::vector<std::string> parts;
        for (auto const &m : j.object_range())
            parts.push_back("t'" + test::hexlify(m.key()) + "':" + value_dump(m.value()));
        return interop::dump_sorted_map(parts);
    }
    default:
        return "<json_type:" + std::to_string(static_cast<int>(j.type())) + ">";
    }
}
}

int main(int const argc, char const *const *const argv)
{
    return interop::run(
        argc, argv,
        [](std::string const &bytes) -> std::expected<std::string, std::string> {
            try {
                return value_dump(jsoncons::cbor::decode_cbor<jsoncons::ojson>(bytes));
            } catch (std::exception const &e) {
                return std::unexpected(e.what());
            }
        },
        [](std::string const &bytes) -> std::expected<std::string, std::string> {
            try {
                std::vector<std::uint8_t> out;
                jsoncons::cbor::encode_cbor(jsoncons::cbor::decode_cbor<jsoncons::ojson>(bytes), out);
                return std::string(out.begin(), out.end());
            } catch (std::exception const &e) {
                return std::unexpected(e.what());
            }
        });
}
