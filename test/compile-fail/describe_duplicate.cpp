// Two entries of one description that give one map key, or that name one member twice, must not compile:
// RFC 8949 5.6 leaves a map with a repeated key to the decoder, and the owner decided that a description
// never writes one. CBOR_TEST_CASE selects the case: 1 two text keys, 2 two integer keys, 3 one member twice.
#include <cbor/cbor.hpp>

#include <expected>
#include <string>

struct twice {
    int a;
    int b;
};

#if CBOR_TEST_CASE == 1
CBOR_DESCRIBE_STRUCT(twice, (160001, 0), (a, "k"), (b, "k"))
#elif CBOR_TEST_CASE == 2
CBOR_DESCRIBE_STRUCT(twice, (160001, 0), (a, 1), (b, 1))
#elif CBOR_TEST_CASE == 3
CBOR_DESCRIBE_STRUCT(twice, (160001, 0), (a), (a, "x"))
#endif

int main()
{
    [[maybe_unused]] auto const encode =
        static_cast<std::expected<std::string, cbor::error> (*)(twice const &)>(&cbor::structs<twice>::encode);
}
