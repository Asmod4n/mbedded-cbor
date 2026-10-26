// cbor::schema writes the keys of a record as texts in its table, so a key must be a text. An integer key, which
// cbor::structs takes, does not compile here, with a message that names the cause.
#include <cbor/cbor.hpp>

#include <cstdint>

struct root {
    std::int32_t m;
};

template <>
struct cbor::describe_members<root> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1650};
    static constexpr std::tuple members{
        cbor::member{7, &root::m},
    };
};

int main()
{
    root const value{};
    return cbor::schema<root>::encode(value).has_value() ? 0 : 1;
}
