// A description must not take a tag that a standard type has, because decode would read that tag as the
// standard type in one place and as the struct in another. CBOR_TEST_TAG is one tag of the set: 0, 1, 2, 3
// and 24 (RFC 8949 3.4), 64 to 87 (RFC 8746), 100 and 1004 (RFC 8943), 258 (CBOR_SETS.md).
#include <cbor/cbor.hpp>

struct t {
    int m;
};

CBOR_DESCRIBE_STRUCT(t, (CBOR_TEST_TAG, 0), (m))

int main()
{
}
