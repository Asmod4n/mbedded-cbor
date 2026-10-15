// The type is the first argument of the macro, so a comma in it splits the arguments. The documented form is
// a type alias; this file checks that the type itself with a comma does not compile.
#include <cbor/cbor.hpp>

#include <utility>

CBOR_DESCRIBE_STRUCT(std::pair<int, long>, (160001, 0), (first))

int main()
{
}
