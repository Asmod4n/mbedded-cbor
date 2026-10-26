// cbor::tagged_item exists for content that is not a struct, such as the array of COSE_Sign1 (RFC 9052 4.2). A
// struct carries its tag in its description, so the wrapper over a struct must not compile.
#include <cbor/cbor.hpp>

struct person {
    int age;
};

int main()
{
    return cbor::tagged_item<1800, person>{}.number == 0;
}
