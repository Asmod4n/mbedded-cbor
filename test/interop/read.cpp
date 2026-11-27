// Reads with the public decoders of the library the bytes that other libraries wrote, and prints the
// canonical dump of dump.hpp for each. "read others" reads the lines of a re-encode ("name TAB input TAB
// written TAB dump") with cbor::decode and prints "name TAB dump-of-input TAB dump-of-written". "read cycles"
// reads the lines of cycles.tsv
// ("name TAB written TAB expected TAB kind") with cbor::decode_cycles and prints "name TAB graph". A graph is
// the dump in which a node that two paths reach is named: "&n" before its first visit and "*n" at each later
// visit. The nodes are numbered in depth-first order of first visit, and the pairs of a map are visited in
// the order of their dump text, in which each named node stands as "*". cbor2_dump.py and cbor_xs_dump.pl
// give the same text.
#include "dump.hpp"
#include <cbor/cbor.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using result = std::expected<std::string, std::string>;

std::string message_of(cbor::error const e)
{
    return std::error_code{e}.message();
}

template <class T>
T value_of(cbor::result<T> const &r)
{
    if (!r)
        throw std::logic_error("read: an item of a decoded tree does not give its value: " +
                               message_of(r.error()));
    return *r;
}

std::vector<cbor::item> targets_of(cbor::item const root)
{
    std::vector<cbor::item> targets;
    std::vector<cbor::item> open{root};
    while (!open.empty()) {
        cbor::item const n = open.back();
        open.pop_back();
        switch (n.type()) {
        case cbor::type::shared_reference: {
            cbor::item const target = value_of(n.get<std::pair<std::uint64_t, cbor::item>>()).second;
            if (std::ranges::find(targets, target) == targets.end())
                targets.push_back(target);
            break;
        }
        case cbor::type::tag:
        case cbor::type::unsigned_bignum:
        case cbor::type::negative_bignum:
            open.push_back(value_of(n.get<std::pair<std::uint64_t, cbor::item>>()).second);
            break;
        case cbor::type::array:
            for (cbor::item const e : value_of(n.elements()))
                open.push_back(e);
            break;
        case cbor::type::map:
            for (auto const &[k, v] : value_of(n.pairs())) {
                open.push_back(k);
                open.push_back(v);
            }
            break;
        default:
            break;
        }
    }
    return targets;
}

struct graph {
    std::vector<cbor::item> targets;
    std::vector<cbor::item> numbered;
};

std::string scalar_dump(cbor::item const n)
{
    switch (n.type()) {
    case cbor::type::unsigned_integer:
        return interop::dump_unsigned_integer(value_of(n.get<std::uint64_t>()));
    case cbor::type::negative_integer:
        return interop::dump_negative_integer(
            static_cast<std::uint64_t>(-(value_of(n.get<cbor::int128>()) + 1)));
    case cbor::type::byte_string: {
        auto const b = value_of(n.get<std::span<std::byte const>>());
        return "h'" + test::hexlify({reinterpret_cast<char const *>(b.data()), b.size()}) + "'";
    }
    case cbor::type::text_string:
        return "t'" + test::hexlify(value_of(n.get<std::string_view>())) + "'";
    case cbor::type::false_value:
    case cbor::type::true_value:
    case cbor::type::null:
    case cbor::type::undefined:
    case cbor::type::simple_value:
        return "s(" + std::to_string(std::to_underlying(value_of(n.get<cbor::simple_value>()))) + ")";
    case cbor::type::half_float:
    case cbor::type::single_float:
    case cbor::type::double_float:
        return interop::dump_float(value_of(n.get<double>()));
    case cbor::type::typed_array: {
        cbor::typed_array const a = value_of(n.get<cbor::typed_array>());
        return interop::dump_tag(
            a.tag,
            "h'" + test::hexlify({reinterpret_cast<char const *>(a.bytes.data()), a.bytes.size()}) + "'");
    }
    default:
        throw std::logic_error("read: a node that is no scalar");
    }
}

std::string graph_dump(graph &g, cbor::item n, bool const key);

std::string container_dump(graph &g, cbor::item const n, bool const key)
{
    switch (n.type()) {
    case cbor::type::tag:
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum: {
        auto const [number, content] = value_of(n.get<std::pair<std::uint64_t, cbor::item>>());
        return interop::dump_tag(number, graph_dump(g, content, key));
    }
    case cbor::type::array: {
        std::vector<std::string> parts;
        for (cbor::item const e : value_of(n.elements()))
            parts.push_back(graph_dump(g, e, key));
        return interop::dump_array(parts);
    }
    case cbor::type::map: {
        std::vector<std::pair<std::string, std::pair<cbor::item, cbor::item>>> pairs;
        for (auto const &[k, v] : value_of(n.pairs()))
            pairs.emplace_back(graph_dump(g, k, true) + ":" + graph_dump(g, v, true), std::pair{k, v});
        if (key || g.targets.empty()) {
            std::vector<std::string> parts;
            for (auto const &p : pairs)
                parts.push_back(p.first);
            return interop::dump_sorted_map(parts);
        }
        std::ranges::sort(pairs, {}, &decltype(pairs)::value_type::first);
        std::string out = "{";
        for (std::size_t i = 0; i < pairs.size(); ++i)
            out += (i ? "," : "") + graph_dump(g, pairs[i].second.first, false) + ":" +
                   graph_dump(g, pairs[i].second.second, false);
        return out + "}";
    }
    default:
        return scalar_dump(n);
    }
}

std::string graph_dump(graph &g, cbor::item n, bool const key)
{
    if (n.type() == cbor::type::shared_reference)
        n = value_of(n.get<std::pair<std::uint64_t, cbor::item>>()).second;
    if (std::ranges::find(g.targets, n) == g.targets.end())
        return container_dump(g, n, key);
    if (key)
        return "*";
    if (auto const at = std::ranges::find(g.numbered, n); at != g.numbered.end())
        return "*" + std::to_string(at - g.numbered.begin());
    g.numbered.push_back(n);
    std::string const name = "&" + std::to_string(g.numbered.size() - 1);
    return name + container_dump(g, n, false);
}

template <class Decode>
std::string result_of(Decode const decode, std::string_view const hex)
{
    if (hex == "-" || hex.starts_with('!'))
        return "-";
    auto const tree = decode(test::unhexlify(hex));
    if (!tree)
        return "!" + message_of(tree.error());
    graph g{targets_of(**tree), {}};
    return graph_dump(g, **tree, false);
}

}

int main(int const argc, char const *const *const argv)
{
    std::string_view const mode = argc == 2 ? argv[1] : "";
    if (mode != "others" && mode != "cycles") {
        std::cerr << "usage: read others|cycles <lines\n";
        return 2;
    }
    auto const decode = [](std::string const &bytes) { return cbor::decode(bytes); };
    auto const decode_cycles = [](std::string const &bytes) { return cbor::decode_cycles(bytes); };
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string_view> const fields = interop::fields_of(line);
        if (fields.size() != interop::case_fields)
            continue;
        if (mode == "others")
            std::cout << fields[0] << '\t' << result_of(decode, fields[1]) << '\t'
                      << result_of(decode, fields[2]) << '\n';
        else
            std::cout << fields[0] << '\t' << result_of(decode_cycles, fields[1]) << '\n';
    }
    return 0;
}
