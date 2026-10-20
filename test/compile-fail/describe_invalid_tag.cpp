// A description must not take a tag that the IANA registry calls always invalid (refs/cbor-tags.xml: 65535,
// 4294967295 and 18446744073709551615, the top of the two-, four- and eight-byte forms, and 21334, the
// programming aid for simple values of refs/draft-bormann-cbor-notable-tags.md), because a decoder may refuse
// that tag. CBOR_TEST_TAG is one tag of the set, or a neighbour of it, which compiles.
#include <cbor/cbor.hpp>

#include <expected>
#include <string>

struct t {
    int m;
};

template <>
struct cbor::describe_members<t> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{CBOR_TEST_TAG};
    static constexpr std::tuple members{
        CBOR_MEMBER(t, m),
    };
};

int main()
{
    [[maybe_unused]] auto const encode =
        static_cast<cbor::result<std::string> (*)(t const &)>(&cbor::structs<t>::encode);
}
