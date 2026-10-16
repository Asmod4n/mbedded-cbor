// An entry of a description names a public non-static data member of the type or of a public base. Every
// other kind of name must not compile. CBOR_TEST_CASE selects the case: 1 a private member, 2 a member of a
// protected base, 3 a member function, 4 a static data member, 5 a bit-field, 6 a reference member.
#include <cbor/cbor.hpp>

#include <expected>
#include <string>

struct hidden_base {
    int inherited;
};

struct kinds : protected hidden_base {
    explicit kinds(int &r) : bits(0), reference(r)
    {
    }
    int bits : 3;
    int &reference;
    static int shared;
    int function() const
    {
        return own;
    }

private:
    int own = 0;
};

#if CBOR_TEST_CASE == 1
CBOR_DESCRIBE_STRUCT(kinds, (160001, 0), (own))
#elif CBOR_TEST_CASE == 2
CBOR_DESCRIBE_STRUCT(kinds, (160001, 0), (inherited))
#elif CBOR_TEST_CASE == 3
CBOR_DESCRIBE_STRUCT(kinds, (160001, 0), (function))
#elif CBOR_TEST_CASE == 4
CBOR_DESCRIBE_STRUCT(kinds, (160001, 0), (shared))
#elif CBOR_TEST_CASE == 5
CBOR_DESCRIBE_STRUCT(kinds, (160001, 0), (bits))
#elif CBOR_TEST_CASE == 6
CBOR_DESCRIBE_STRUCT(kinds, (160001, 0), (reference))
#endif

int main()
{
    [[maybe_unused]] auto const encode =
        static_cast<std::expected<std::string, cbor::error> (*)(kinds const &)>(&cbor::structs<kinds>::encode);
}
