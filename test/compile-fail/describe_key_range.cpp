// RFC 8949 3.1 gives integer keys down to -2^64, but no C++ literal holds a value below -2^63. The literal
// -9223372036854775808 is the negation of 2^63, which no signed 64-bit literal holds: g++ makes it __int128,
// which the key does not take, and clang makes it unsigned. Either way the build with -Werror stops here; the
// documented form of -2^63 is std::numeric_limits<std::int64_t>::min().
#include <cbor/cbor.hpp>

#include <expected>
#include <string>

struct low {
    int a;
};

CBOR_DESCRIBE_STRUCT(low, (160001, 0), (a, -9223372036854775808))

int main()
{
    [[maybe_unused]] auto const encode =
        static_cast<std::expected<std::string, cbor::error> (*)(low const &)>(&cbor::structs<low>::encode);
}
