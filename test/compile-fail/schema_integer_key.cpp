// cbor::schema writes the keys of a record as texts in its table, so a key must be a text. An integer key, which
// cbor::lazy and cbor::describe_members take, does not compile here, with a message that names the cause.
#include <cbor/cbor.hpp>

#include <cstdint>

struct [[=cbor::tag{1650}]] root {
    [[=cbor::key(7)]] std::int32_t m;
};

int main()
{
    root const value{};
    return cbor::schema<root>::encode(value).has_value() ? 0 : 1;
}
