// nlohmann/json: the canonical dump of one top-level item, read with from_cbor in strict mode. The tag
// handler "store" keeps the tag number of a byte string as its subtype; nlohmann/json refuses a tag on any other
// item with this handler. Map keys must be text in nlohmann/json.
#include "dump.hpp"

#include <nlohmann/json.hpp>

namespace
{

std::string value_dump(nlohmann::ordered_json const &j)
{
    switch (j.type()) {
    case nlohmann::json::value_t::null:
        return "s(22)";
    case nlohmann::json::value_t::boolean:
        return j.get<bool>() ? "s(21)" : "s(20)";
    case nlohmann::json::value_t::number_integer: {
        auto const v = j.get<std::int64_t>();
        return v < 0 ? interop::dump_negative_integer(static_cast<std::uint64_t>(-(v + 1)))
                     : interop::dump_unsigned_integer(static_cast<std::uint64_t>(v));
    }
    case nlohmann::json::value_t::number_unsigned:
        return interop::dump_unsigned_integer(j.get<std::uint64_t>());
    case nlohmann::json::value_t::number_float:
        return interop::dump_float(j.get<double>());
    case nlohmann::json::value_t::string:
        return "t'" + test::hexlify(j.get_ref<std::string const &>()) + "'";
    case nlohmann::json::value_t::binary: {
        auto const &b = j.get_binary();
        std::string const h = "h'" + test::hexlify({reinterpret_cast<char const *>(b.data()), b.size()}) + "'";
        return b.has_subtype() ? interop::dump_unsigned_integer(b.subtype()) + "(" + h + ")" : h;
    }
    case nlohmann::json::value_t::array: {
        std::vector<std::string> parts;
        for (auto const &e : j)
            parts.push_back(value_dump(e));
        return interop::dump_array(parts);
    }
    case nlohmann::json::value_t::object: {
        std::vector<std::string> parts;
        for (auto const &[k, v] : j.items())
            parts.push_back("t'" + test::hexlify(k) + "':" + value_dump(v));
        return interop::dump_sorted_map(parts);
    }
    default:
        return "?";
    }
}

}

int main()
{
    return interop::run([](std::string const &bytes) -> std::expected<std::string, std::string> {
        try {
            return value_dump(nlohmann::ordered_json::from_cbor(bytes, true, true,
                                                                nlohmann::json::cbor_tag_handler_t::store));
        } catch (std::exception const &e) {
            return std::unexpected(e.what());
        }
    });
}
