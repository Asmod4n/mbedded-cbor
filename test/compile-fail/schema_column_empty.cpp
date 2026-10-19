// cbor::schema writes a vector of structs as columns, and the count of elements stands in the head of the first
// column. A struct with no member of its own, directly in case 1 or through an inner struct with no member in case 2,
// gives no column, so a reader could not find the count. Such a vector does not compile.
#include <cbor/cbor.hpp>

#include <vector>

struct [[=cbor::tag{1600}]] empty {};

#if CBOR_TEST_CASE == 1
struct [[=cbor::tag{1601}]] root {
    std::vector<empty> items;
};
#else
struct [[=cbor::tag{1602}]] holder {
    empty e;
};

struct [[=cbor::tag{1601}]] root {
    std::vector<holder> items;
};
#endif

int main()
{
    root const value{};
    return cbor::schema<root>::encode(value).has_value() ? 0 : 1;
}
