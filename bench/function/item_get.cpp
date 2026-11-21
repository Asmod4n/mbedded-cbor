// The bench of item::get, item::elements and item::pairs (item.hpp) on a tree from cbor::decode.
// The message is an array of records in the form of bench/cars.json: text keys, text values, integers, negative
// integers, doubles, booleans, nested maps and arrays. The tree is decoded once, and it is as large as COLD_BYTES.
// item_get_scalars: each iteration reads every integer, double and text string of the tree through get.
// item_visit_tree: each iteration reads every node of the tree, by a switch on the kind and get<cbor::type> where the
// item has that get, and by get, elements and pairs where it does not.
// One file serves both forms of the item, so both arms build from one file with one set of flags.
#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

constexpr int unsigned_major = 0;
constexpr int negative_major = 1;
constexpr int text_major = 3;
constexpr int array_major = 4;
constexpr int map_major = 5;
constexpr char false_byte = '\xf4';
constexpr char true_byte = '\xf5';
constexpr char double_byte = '\xfb';
constexpr std::uint64_t one_byte_argument = 24;
constexpr std::uint64_t record_pairs = 9;
constexpr std::uint64_t wheels = 4;
constexpr std::uint64_t wheel_pairs = 3;
constexpr std::uint64_t engine_pairs = 3;

std::size_t cold_bytes()
{
    char const *const given = std::getenv("COLD_BYTES");
    if (given == nullptr)
        throw std::logic_error("bench: COLD_BYTES is not set");
    return std::stoull(given);
}

void append_head(std::string &out, int const major, std::uint64_t const argument)
{
    char const initial = static_cast<char>(major << 5);
    if (argument < one_byte_argument) {
        out.push_back(static_cast<char>(initial | static_cast<char>(argument)));
        return;
    }
    out.push_back(static_cast<char>(initial | 27));
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<char>((argument >> shift) & 0xff));
}

void append_text(std::string &out, std::string_view const text)
{
    append_head(out, text_major, text.size());
    out.append(text);
}

void append_double(std::string &out, double const value)
{
    out.push_back(double_byte);
    std::uint64_t const bits = std::bit_cast<std::uint64_t>(value);
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<char>((bits >> shift) & 0xff));
}

void append_record(std::string &out, std::uint64_t const i)
{
    append_head(out, map_major, record_pairs);
    append_text(out, "make");
    append_text(out, i % 2 == 0 ? "Tesla" : "Volvo");
    append_text(out, "model");
    append_text(out, i % 3 == 0 ? "Volt" : "Model S");
    append_text(out, "color");
    append_head(out, unsigned_major, i % 11);
    append_text(out, "weight");
    append_head(out, unsigned_major, 3000 + i % 700);
    append_text(out, "offset");
    append_head(out, negative_major, i % 300);
    append_text(out, "wheels");
    append_head(out, array_major, wheels);
    for (std::uint64_t w = 0; w < wheels; ++w) {
        append_head(out, map_major, wheel_pairs);
        append_text(out, "diameter");
        append_head(out, unsigned_major, 26 + (i + w) % 14);
        append_text(out, "airPressure");
        append_double(out, 30.0 + static_cast<double>((i + w) % 17) * 0.931);
        append_text(out, "snowTires");
        out.push_back((i + w) % 5 == 0 ? true_byte : false_byte);
    }
    append_text(out, "engine");
    append_head(out, map_major, engine_pairs);
    append_text(out, "horsepower");
    append_head(out, unsigned_major, 100 + i % 40000);
    append_text(out, "usesGas");
    out.push_back(i % 2 == 0 ? true_byte : false_byte);
    append_text(out, "cc");
    append_head(out, unsigned_major, 900 + i % 9000);
    append_text(out, "fuelLevel");
    append_double(out, static_cast<double>(i % 1000) * 0.0315);
    append_text(out, "hasNavSystem");
    out.push_back(i % 3 == 0 ? true_byte : false_byte);
}

std::string message_of(std::size_t const size)
{
    std::string records;
    std::uint64_t count = 0;
    while (records.size() < size)
        append_record(records, count++);
    std::string message;
    append_head(message, array_major, count);
    message.append(records);
    return message;
}

template <class Item>
constexpr bool has_kind_get = requires(Item const &it) { it.template get<cbor::type::unsigned_integer>(); };

std::uint64_t bytes_sum(std::string_view const text)
{
    std::uint64_t s = text.size();
    for (char const c : text)
        s += static_cast<std::uint8_t>(c);
    return s;
}

template <class Item>
std::uint64_t scalars_sum(Item const &it)
{
    if constexpr (has_kind_get<Item>) {
        switch (it.type) {
        case cbor::type::unsigned_integer:
            return static_cast<std::uint64_t>(static_cast<std::int64_t>(it.template get<cbor::type::unsigned_integer>().value()));
        case cbor::type::negative_integer:
            return static_cast<std::uint64_t>(-1 - static_cast<std::int64_t>(it.template get<cbor::type::negative_integer>().value()));
        case cbor::type::double_float:
            return std::bit_cast<std::uint64_t>(it.template get<cbor::type::double_float>().value());
        case cbor::type::text_string:
            return bytes_sum(it.template get<cbor::type::text_string>().value());
        case cbor::type::array: {
            std::uint64_t s = 0;
            for (Item const &child : it.template get<cbor::type::array>().value())
                s += scalars_sum(child);
            return s;
        }
        case cbor::type::map: {
            std::uint64_t s = 0;
            for (Item const &child : it.template get<cbor::type::map>().value())
                s += scalars_sum(child);
            return s;
        }
        default:
            return 0;
        }
    } else {
        switch (it.type()) {
        case cbor::type::unsigned_integer:
        case cbor::type::negative_integer:
            return static_cast<std::uint64_t>(it.template get<std::int64_t>().value());
        case cbor::type::double_float:
            return std::bit_cast<std::uint64_t>(it.template get<double>().value());
        case cbor::type::text_string:
            return bytes_sum(it.template get<std::string_view>().value());
        case cbor::type::array: {
            std::uint64_t s = 0;
            for (Item const &child : it.elements().value())
                s += scalars_sum(child);
            return s;
        }
        case cbor::type::map: {
            std::uint64_t s = 0;
            for (auto const pair : it.pairs().value())
                s += scalars_sum(std::get<0>(pair)) + scalars_sum(std::get<1>(pair));
            return s;
        }
        default:
            return 0;
        }
    }
}

template <class Item>
std::uint64_t tree_sum(Item const &it);

template <class Item, cbor::type K>
std::uint64_t children_sum(Item const &it)
{
    std::uint64_t s = 0;
    for (Item const &child : it.template get<K>().value())
        s += tree_sum(child);
    return s;
}

template <class Item, cbor::type K>
std::uint64_t tag_sum(Item const &it)
{
    auto const tag = it.template get<K>().value();
    return tag.first + tree_sum(*tag.second);
}

template <class Item>
std::uint64_t tree_sum(Item const &it)
{
    if constexpr (has_kind_get<Item>) {
        switch (it.type) {
        case cbor::type::unsigned_integer:
            return it.template get<cbor::type::unsigned_integer>().value();
        case cbor::type::negative_integer:
            return ~it.template get<cbor::type::negative_integer>().value();
        case cbor::type::byte_string: {
            auto const b = it.template get<cbor::type::byte_string>().value();
            return b.size();
        }
        case cbor::type::text_string:
            return bytes_sum(it.template get<cbor::type::text_string>().value());
        case cbor::type::array:
            return children_sum<Item, cbor::type::array>(it);
        case cbor::type::map:
            return children_sum<Item, cbor::type::map>(it);
        case cbor::type::tag:
            return tag_sum<Item, cbor::type::tag>(it);
        case cbor::type::unsigned_bignum:
            return tag_sum<Item, cbor::type::unsigned_bignum>(it);
        case cbor::type::negative_bignum:
            return tag_sum<Item, cbor::type::negative_bignum>(it);
        case cbor::type::typed_array:
            return it.template get<cbor::type::typed_array>().value().first;
        case cbor::type::shared_reference:
            return it.template get<cbor::type::shared_reference>().value().first;
        case cbor::type::half_float:
            return std::bit_cast<std::uint64_t>(it.template get<cbor::type::half_float>().value());
        case cbor::type::single_float:
            return std::bit_cast<std::uint64_t>(it.template get<cbor::type::single_float>().value());
        case cbor::type::double_float:
            return std::bit_cast<std::uint64_t>(it.template get<cbor::type::double_float>().value());
        case cbor::type::false_value:
            return std::to_underlying(it.template get<cbor::type::false_value>().value());
        case cbor::type::true_value:
            return std::to_underlying(it.template get<cbor::type::true_value>().value());
        case cbor::type::null:
            return std::to_underlying(it.template get<cbor::type::null>().value());
        case cbor::type::undefined:
            return std::to_underlying(it.template get<cbor::type::undefined>().value());
        case cbor::type::simple_value:
            return std::to_underlying(it.template get<cbor::type::simple_value>().value());
        }
        std::unreachable();
    } else {
        switch (it.type()) {
        case cbor::type::unsigned_integer:
            return it.template get<std::uint64_t>().value();
        case cbor::type::negative_integer:
            return static_cast<std::uint64_t>(it.template get<cbor::int128>().value());
        case cbor::type::false_value:
        case cbor::type::true_value:
            return it.template get<bool>().value() ? 21 : 20;
        case cbor::type::null:
            return 22;
        case cbor::type::undefined:
        case cbor::type::simple_value:
            return std::to_underlying(it.template get<cbor::simple_value>().value());
        case cbor::type::half_float:
        case cbor::type::single_float:
        case cbor::type::double_float:
            return std::bit_cast<std::uint64_t>(it.template get<double>().value());
        case cbor::type::text_string:
            return bytes_sum(it.template get<std::string_view>().value());
        case cbor::type::byte_string:
            return it.template get<std::span<std::byte const>>().value().size();
        case cbor::type::array: {
            std::uint64_t s = 0;
            for (Item const &child : *it.elements())
                s += tree_sum(child);
            return s;
        }
        case cbor::type::map: {
            std::uint64_t s = 0;
            for (auto const [key, value] : *it.pairs())
                s += tree_sum(key) + tree_sum(value);
            return s;
        }
        case cbor::type::tag:
        case cbor::type::unsigned_bignum:
        case cbor::type::negative_bignum: {
            auto const tag = it.template get<std::pair<std::uint64_t, Item>>().value();
            return tag.first + tree_sum(tag.second);
        }
        case cbor::type::typed_array:
            return it.template get<cbor::typed_array>().value().tag;
        case cbor::type::shared_reference:
            return 0;
        }
        std::unreachable();
    }
}

template <class Sum>
void run(benchmark::State &state, Sum const sum)
{
    auto const tree = cbor::decode(message_of(cold_bytes()));
    if (!tree) {
        state.SkipWithError("cbor::decode refuses the message of the bench");
        return;
    }
    cbor::item const root = **tree;
    std::uint64_t total = 0;
    for (auto _ : state) {
        total = sum(root);
        benchmark::DoNotOptimize(total);
    }
    state.counters["sum_low_32_bits"] = static_cast<double>(total & std::numeric_limits<std::uint32_t>::max());
}

void item_get_scalars(benchmark::State &state)
{
    run(state, [](cbor::item const root) { return scalars_sum(root); });
}

void item_visit_tree(benchmark::State &state)
{
    run(state, [](cbor::item const root) { return tree_sum(root); });
}

}

BENCHMARK(item_get_scalars)->Unit(benchmark::kMicrosecond);
BENCHMARK(item_visit_tree)->Unit(benchmark::kMicrosecond);
BENCHMARK_MAIN();
