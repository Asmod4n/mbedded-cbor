// Two entries of one description that give one map key, or that name one member twice, must not compile:
// RFC 8949 5.6 leaves a map with a repeated key to the decoder, and the owner decided that a description
// never writes one. CBOR_TEST_CASE selects the case: 1 two text keys, 2 two integer keys, 3 one member twice, 4
// one member of a base twice, once through a pointer cast to the derived class, which names the same member.
#include <cbor/cbor.hpp>

#include <expected>
#include <string>

struct twice {
    int a;
    int b;
};

struct base_id {
    int id;
};

struct derived : base_id {
    int a;
};

#if CBOR_TEST_CASE == 1
template <>
struct cbor::describe_members<twice> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001};
    static constexpr std::tuple members{
        cbor::member{"k", &twice::a},
        cbor::member{"k", &twice::b},
    };
};
#elif CBOR_TEST_CASE == 2
template <>
struct cbor::describe_members<twice> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001};
    static constexpr std::tuple members{
        cbor::member{1, &twice::a},
        cbor::member{1, &twice::b},
    };
};
#elif CBOR_TEST_CASE == 3
template <>
struct cbor::describe_members<twice> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001};
    static constexpr std::tuple members{
        CBOR_MEMBER(twice, a),
        cbor::member{"x", &twice::a},
    };
};
#elif CBOR_TEST_CASE == 4
template <>
struct cbor::describe_members<derived> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001};
    static constexpr std::tuple members{
        CBOR_MEMBER(derived, id),
        cbor::member{"y", static_cast<int derived::*>(&derived::id)},
        CBOR_MEMBER(derived, a),
    };
};
#endif

#if CBOR_TEST_CASE == 4
using described = derived;
#else
using described = twice;
#endif

int main()
{
    [[maybe_unused]] auto const encode =
        static_cast<std::expected<std::string, cbor::error> (*)(described const &)>(&cbor::structs<described>::encode);
}
