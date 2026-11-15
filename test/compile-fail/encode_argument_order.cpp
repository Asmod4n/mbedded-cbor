// Each encode that writes into a target takes only a target that it can write into: a std::span of char or std::byte,
// a container of bytes, or a writer. Any other type as the target does not compile at the call, and the compiler names
// the encode that refuses it. CBOR_TEST_CASE selects the case. Case 7: an item as the target. Case 0: each encode with
// each kind of target, which compiles.
#include <cbor/cbor.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

struct person {
    std::string name;
    std::int32_t age;
};

struct tire {
    std::uint32_t diameter;
    float pressure;
};

template <>
struct cbor::describe_members<tire> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1820};
    static constexpr std::tuple members{
        CBOR_MEMBER(tire, diameter),
        CBOR_MEMBER(tire, pressure),
    };
};

struct appending_writer {
    std::string *out;

    std::expected<void, std::errc> append(std::string_view const part)
    {
        out->append(part);
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

struct writer_target {
    std::string *out;

    appending_writer allocate(std::size_t)
    {
        return {out};
    }
};

int main()
{
    auto const tree = cbor::decode(std::string_view("\x82\x01\x61\x61", 4));
    if (!tree)
        return 1;
    cbor::item const &root = **tree;
#if CBOR_TEST_CASE == 7
    return cbor::encode(root, root).has_value() ? 0 : 1;
#else
    person const ann{"ann", 30};
    tire const front{17, 2.5f};
    std::string_view const view = "abc";
    std::string out;
    std::vector<std::byte> bytes;
    std::array<char, 256> chars{};
    std::array<std::byte, 256> raw{};
    std::string written;
    writer_target writer{&written};
    bool all = true;
    all = all && cbor::encode(root, out) && cbor::encode(root, bytes) && cbor::encode(root, std::span<char>(chars)) &&
          cbor::encode(root, std::span<std::byte>(raw)) && cbor::encode(root, writer);
    all = all && cbor::encode_cycles(root, out) && cbor::encode_cycles(root, bytes) &&
          cbor::encode_cycles(root, std::span<char>(chars)) && cbor::encode_cycles(root, std::span<std::byte>(raw)) &&
          cbor::encode_cycles(root, writer);
    all = all && cbor::structs<person>::encode(ann, out) && cbor::structs<person>::encode(ann, bytes) &&
          cbor::structs<person>::encode(ann, std::span<char>(chars)) &&
          cbor::structs<person>::encode(ann, std::span<std::byte>(raw)) && cbor::structs<person>::encode(ann, writer);
    all = all && cbor::schema<tire>::encode(front, out) && cbor::schema<tire>::encode(front, bytes) &&
          cbor::schema<tire>::encode(front, std::span<char>(chars)) &&
          cbor::schema<tire>::encode(front, std::span<std::byte>(raw)) && cbor::schema<tire>::encode(front, writer);
    all = all && cbor::static_dictionary<tire>::encode(front, out) &&
          cbor::static_dictionary<tire>::encode(front, bytes) &&
          cbor::static_dictionary<tire>::encode(front, std::span<char>(chars)) &&
          cbor::static_dictionary<tire>::encode(front, std::span<std::byte>(raw)) &&
          cbor::static_dictionary<tire>::encode(front, writer);
    all = all && cbor::structs<std::string_view>::encode(view, out) && cbor::structs<std::string>::encode("abc");
    return all ? 0 : 1;
#endif
}
