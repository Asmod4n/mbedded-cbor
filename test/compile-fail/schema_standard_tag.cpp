// A packed struct must not take a tag that a standard type has (RFC 8949 3.4: 0, 1, 2, 3 and 24; RFC 8746: 64 to
// 87; RFC 8943: 100 and 1004; CBOR_SETS.md: 258), because a decoder reads that tag as the standard type and finds
// a record where it expects that content; tag 24 asks for a byte string. Nor may it take a tag that the IANA
// registry calls always invalid. The same holds for an inner struct, so case 1 tags the root and case 2 tags the
// inner struct. CBOR_TEST_TAG is one tag of a set, or a neighbour of it, which compiles.
#include <cbor/cbor.hpp>

#include <cstdint>

#if CBOR_TEST_CASE == 1
struct [[=cbor::tag{CBOR_TEST_TAG}]] root {
    std::int32_t m;
};
#else
struct [[=cbor::tag{CBOR_TEST_TAG}]] inner {
    std::int32_t m;
};

struct [[=cbor::tag{1500}]] root {
    inner i;
};
#endif

int main()
{
    root const value{};
    return cbor::schema<root>::encode(value).has_value() ? 0 : 1;
}
