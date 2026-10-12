// jsoncons: the canonical dump of one top-level item, read with decode_cbor into an ojson. jsoncons maps a
// tag that it knows to a semantic_tag and drops the number of a tag that it does not know; the dump prints the
// semantic_tag. Map keys that are not text become text in jsoncons.
#include "dump.hpp"

#include <jsoncons/json.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>

namespace
{

std::string value_dump(jsoncons::ojson const &j)
{
    std::string const tag = j.tag() == jsoncons::semantic_tag::none ? "" : "<" + std::to_string(static_cast<int>(j.tag())) + ">";
    switch (j.type()) {
    case jsoncons::json_type::null_value:
        return tag + (j.tag() == jsoncons::semantic_tag::undefined ? "s(23)" : "s(22)");
    case jsoncons::json_type::bool_value:
        return tag + (j.as<bool>() ? "s(21)" : "s(20)");
    case jsoncons::json_type::int64_value: {
        auto const v = j.as<std::int64_t>();
        return tag + (v < 0 ? interop::negative_dump(static_cast<std::uint64_t>(-(v + 1)))
                            : interop::unsigned_dump(static_cast<std::uint64_t>(v)));
    }
    case jsoncons::json_type::uint64_value:
        return tag + interop::unsigned_dump(j.as<std::uint64_t>());
    case jsoncons::json_type::half_value:
    case jsoncons::json_type::double_value:
        return tag + interop::float_dump(j.as<double>());
    case jsoncons::json_type::string_value:
        return tag + "t'" + interop::hex_of(j.as_string_view()) + "'";
    case jsoncons::json_type::byte_string_value: {
        auto const b = j.as_byte_string_view();
        return tag + "h'" + interop::hex_of({reinterpret_cast<char const *>(b.data()), b.size()}) + "'";
    }
    case jsoncons::json_type::array_value: {
        std::vector<std::string> parts;
        for (auto const &e : j.array_range())
            parts.push_back(value_dump(e));
        return tag + interop::array_dump(parts);
    }
    case jsoncons::json_type::object_value: {
        std::vector<std::string> parts;
        for (auto const &m : j.object_range())
            parts.push_back("t'" + interop::hex_of(m.key()) + "':" + value_dump(m.value()));
        return tag + interop::map_dump(parts);
    }
    default:
        return tag + "?";
    }
}

}

int main()
{
    return interop::run([](std::string const &bytes) -> std::expected<std::string, std::string> {
        try {
            return value_dump(jsoncons::cbor::decode_cbor<jsoncons::ojson>(bytes));
        } catch (std::exception const &e) {
            return std::unexpected(e.what());
        }
    });
}
