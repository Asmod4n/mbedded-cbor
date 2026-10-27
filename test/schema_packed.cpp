#ifdef __cpp_impl_reflection

#define CBOR_TEST_FORM packed
#define CBOR_TEST_FORM_NAME "packed: "
#include "schema.cpp"

namespace
{

struct branch {
    std::uint8_t v;
    std::vector<branch> kids;
};

} // namespace

template <>
struct cbor::describe_members<branch> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1690};
    static constexpr std::tuple members{
        CBOR_MEMBER(branch, v),
        CBOR_MEMBER(branch, kids),
    };
};

namespace
{

// cbor::schema reads the members with reflection at hand and copies a vector of fixed-size structs as a whole where
// the offsets of the class match the wire; cbor::packed reads only the description and writes member by member.
// Both must give the same bytes, the same description and the same sizes, and each must read what the other wrote.
template <class T>
void check_forms_agree(T const &value)
{
    auto const s = cbor::schema<T>::encode(value);
    auto const p = cbor::packed<T>::encode(value);
    REQUIRE(s.has_value());
    REQUIRE(p.has_value());
    CHECK_EQ(*s, *p);
    auto const s_data = cbor::schema<T>::template encode<cbor::description_place::apart>(value);
    auto const p_data = cbor::packed<T>::template encode<cbor::description_place::apart>(value);
    REQUIRE(s_data.has_value());
    REQUIRE(p_data.has_value());
    CHECK_EQ(*s_data, *p_data);
    CHECK(std::ranges::equal(cbor::schema<T>::description(), cbor::packed<T>::description()));
    CHECK_EQ(cbor::schema<T>::fixed_size(), cbor::packed<T>::fixed_size());
    auto const from_schema = cbor::packed<T>::decode(std::string(*s));
    REQUIRE(from_schema.has_value());
    CHECK_EQ(cbor::schema<T>::encode(**from_schema), *s);
    auto const from_packed = cbor::schema<T>::decode(std::string(*p));
    REQUIRE(from_packed.has_value());
    CHECK_EQ(cbor::packed<T>::encode(**from_packed), *p);
    auto const unverified = cbor::packed<T>::unverified_decode(std::string(*s));
    REQUIRE(unverified.has_value());
    CHECK_EQ(cbor::schema<T>::encode(**unverified), *s);
    auto const data = cbor::packed<T>::template decode<cbor::description_place::apart>(std::string(*s_data));
    REQUIRE(data.has_value());
    CHECK_EQ(cbor::schema<T>::encode(**data), *s);
    CHECK(cbor::packed<T>::path(std::string(*s)).has_value());
    CHECK(cbor::schema<T>::path(std::string(*p)).has_value());
}

template <class T>
void check_filled_forms_agree()
{
    T value{};
    unsigned seed = 0;
    golden_fill(value, seed);
    check_forms_agree(value);
}

} // namespace

// The types of test/schema.cpp, filled as for the golden bytes: typed arrays, fixed-size vectors (32873), a map,
// optionals, renamed keys, key orders that differ from the order of the description, 20 struct types in one root.
TEST_CASE("packed and schema: the same bytes, and each reads the other")
{
    check_filled_forms_agree<wheel>();
    check_filled_forms_agree<car>();
    check_filled_forms_agree<people>();
    check_filled_forms_agree<pmr_people>();
    check_filled_forms_agree<unsorted>();
    check_filled_forms_agree<renamed>();
    check_filled_forms_agree<sorted_outer>();
    check_filled_forms_agree<vehicle>();
    check_filled_forms_agree<numbers>();
    check_filled_forms_agree<garage>();
    check_filled_forms_agree<keyed>();
    check_filled_forms_agree<passkey_login>();
    check_filled_forms_agree<chain0>();
    check_filled_forms_agree<ticket>();
    check_filled_forms_agree<probe>();
    check_filled_forms_agree<parking_lot>();
    check_filled_forms_agree<cloud>();
    check_filled_forms_agree<gauges>();
    check_filled_forms_agree<eighteen_entries>();
    check_filled_forms_agree<everything>();
    check_forms_agree(sample_everything);
    check_forms_agree(station{{reading{1, "a"}, reading{2, "bb"}}, reading{3, "ccc"}, {{9, reading{4, "dddd"}}}});
}

// A recursive type is one record in the table however deep the value is.
TEST_CASE("packed and schema: a recursive type")
{
    check_forms_agree(branch{1, {{2, {}}, {3, {{4, {}}}}}});
    check_forms_agree(branch{});
}

// member_offset takes a reflection on schema and a member pointer on both; the offsets are the same. A path reads the
// same leaf through either form from the bytes of the other.
TEST_CASE("packed and schema: member offsets and paths agree")
{
    CHECK_EQ(cbor::schema<wheel>::member_offset<^^wheel::airPressure>(), cbor::packed<wheel>::member_offset<&wheel::airPressure>());
    CHECK_EQ(cbor::schema<car>::member_offset<^^car::motor>(), cbor::packed<car>::member_offset<&car::motor>());
    CHECK_EQ((cbor::schema<chain0>::member_offset<^^chain8::next, chain8>()),
             (cbor::packed<chain0>::member_offset<&chain8::next, chain8>()));
    std::string const bytes = *cbor::schema<everything>::encode(sample_everything);
    CHECK_EQ(cbor::packed<everything>::at_path<"$.maybe">(bytes), cbor::schema<everything>::at_path<"$.maybe">(bytes));
    CHECK_EQ(cbor::packed<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz),
             cbor::schema<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz));
    auto const root = cbor::packed<everything>::path(std::string(bytes));
    REQUIRE(root.has_value());
    auto const track = root->view<"$.track">();
    REQUIRE(track.has_value());
    CHECK_EQ(std::vector<double>(track->begin(), track->end()), sample_everything.track);
}

#endif
