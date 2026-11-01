#include "types.hpp"

namespace schema_types
{


template <class T>
std::string golden_line(std::string_view const name)
{
    T value{};
    unsigned seed = 0;
    golden_fill(value, seed);
    auto const inside = form<T>::encode(value);
    auto const data = form<T>::template encode<cbor::description_place::apart>(value);
    if (!inside || !data)
        return std::string(name) + " no encoding\n";
    auto const description = form<T>::description();
    return std::string(name) + " " + golden_hex(*inside) + " " + golden_hex(*data) + " " +
           golden_hex(std::string_view(description.data(), description.size())) + "\n";
}

template <class = void>
std::string golden_report()
{
    std::string report;
    report += golden_line<wheel>("wheel");
    report += golden_line<engine>("engine");
    report += golden_line<car>("car");
    report += golden_line<empty>("empty");
    report += golden_line<thirty>("thirty");
    report += golden_line<long_name>("long_name");
    report += golden_line<größe>("größe");
    report += golden_line<login>("login");
    report += golden_line<measures>("measures");
    report += golden_line<named>("named");
    report += golden_line<people>("people");
    report += golden_line<pmr_named>("pmr_named");
    report += golden_line<pmr_people>("pmr_people");
    report += golden_line<unsorted>("unsorted");
    report += golden_line<unsorted_other_order>("unsorted_other_order");
    report += golden_line<renamed>("renamed");
    report += golden_line<listed_other_order>("listed_other_order");
    report += golden_line<sorted_inner>("sorted_inner");
    report += golden_line<sorted_outer>("sorted_outer");
    report += golden_line<tire>("tire");
    report += golden_line<vehicle>("vehicle");
    report += golden_line<badge>("badge");
#if defined(__STDCPP_FLOAT16_T__)
    report += golden_line<numbers>("numbers");
#endif
    report += golden_line<doubles>("doubles");
    report += golden_line<garage>("garage");
    report += golden_line<keyed>("keyed");
    report += golden_line<passkey_user>("passkey_user");
    report += golden_line<passkey_login>("passkey_login");
    report += golden_line<chain19>("chain19");
    report += golden_line<chain10>("chain10");
    report += golden_line<chain0>("chain0");
    report += golden_line<point_a>("point_a");
    report += golden_line<point_b>("point_b");
    report += golden_line<ticket>("ticket");
    report += golden_line<probe>("probe");
    report += golden_line<lot_wheel>("lot_wheel");
    report += golden_line<lot_engine>("lot_engine");
    report += golden_line<lot_car>("lot_car");
    report += golden_line<parking_lot>("parking_lot");
    report += golden_line<point>("point");
    report += golden_line<cloud>("cloud");
    report += golden_line<gauge>("gauge");
    report += golden_line<gauges>("gauges");
    report += golden_line<filler_type<3>>("filler_type<3>");
    report += golden_line<fifteen_entries>("fifteen_entries");
    report += golden_line<eighteen_entries>("eighteen_entries");
    report += golden_line<everything>("everything");
    return report;
}

template <class = void>
std::string golden_expected_line(std::string_view const name)
{
    std::string_view const all = golden_expected;
    std::string const key = std::string(name) + " ";
    std::size_t first = 0;
    if (!all.starts_with(key)) {
        std::size_t const at = all.find("\n" + key);
        if (at == std::string_view::npos)
            return {};
        first = at + 1;
    }
    std::size_t const last = all.find('\n', first);
    if (last == std::string_view::npos)
        return {};
    return std::string(all.substr(first, last + 1 - first));
}

} // namespace schema_types

// The bytes of every described type of this file, with values that differ in each leaf, as the commit before
// cbor::packed wrote them (dc787da): the message with the description inside, the data form apart, and the
// description. A change of the walk that changes one byte fails here.
TEST_CASE("golden bytes: every type of this file encodes as before")
{
    std::string expected(golden_expected);
#if !defined(__STDCPP_FLOAT16_T__)
    std::size_t const at = expected.find("\nnumbers ") + 1;
    expected.erase(at, expected.find('\n', at) + 1 - at);
#endif
    CHECK_EQ(golden_report(), expected);
}

TEST_CASE("golden bytes: wheel encodes as before")
{
    CHECK_FALSE(golden_expected_line("wheel").empty());
    CHECK_EQ(golden_line<wheel>("wheel"), golden_expected_line("wheel"));
}

TEST_CASE("golden bytes: engine encodes as before")
{
    CHECK_FALSE(golden_expected_line("engine").empty());
    CHECK_EQ(golden_line<engine>("engine"), golden_expected_line("engine"));
}

TEST_CASE("golden bytes: car encodes as before")
{
    CHECK_FALSE(golden_expected_line("car").empty());
    CHECK_EQ(golden_line<car>("car"), golden_expected_line("car"));
}

TEST_CASE("golden bytes: empty encodes as before")
{
    CHECK_FALSE(golden_expected_line("empty").empty());
    CHECK_EQ(golden_line<empty>("empty"), golden_expected_line("empty"));
}

TEST_CASE("golden bytes: thirty encodes as before")
{
    CHECK_FALSE(golden_expected_line("thirty").empty());
    CHECK_EQ(golden_line<thirty>("thirty"), golden_expected_line("thirty"));
}

TEST_CASE("golden bytes: long_name encodes as before")
{
    CHECK_FALSE(golden_expected_line("long_name").empty());
    CHECK_EQ(golden_line<long_name>("long_name"), golden_expected_line("long_name"));
}

TEST_CASE("golden bytes: größe encodes as before")
{
    CHECK_FALSE(golden_expected_line("größe").empty());
    CHECK_EQ(golden_line<größe>("größe"), golden_expected_line("größe"));
}

TEST_CASE("golden bytes: login encodes as before")
{
    CHECK_FALSE(golden_expected_line("login").empty());
    CHECK_EQ(golden_line<login>("login"), golden_expected_line("login"));
}

TEST_CASE("golden bytes: measures encodes as before")
{
    CHECK_FALSE(golden_expected_line("measures").empty());
    CHECK_EQ(golden_line<measures>("measures"), golden_expected_line("measures"));
}

TEST_CASE("golden bytes: named encodes as before")
{
    CHECK_FALSE(golden_expected_line("named").empty());
    CHECK_EQ(golden_line<named>("named"), golden_expected_line("named"));
}

TEST_CASE("golden bytes: people encodes as before")
{
    CHECK_FALSE(golden_expected_line("people").empty());
    CHECK_EQ(golden_line<people>("people"), golden_expected_line("people"));
}

TEST_CASE("golden bytes: pmr_named encodes as before")
{
    CHECK_FALSE(golden_expected_line("pmr_named").empty());
    CHECK_EQ(golden_line<pmr_named>("pmr_named"), golden_expected_line("pmr_named"));
}

TEST_CASE("golden bytes: pmr_people encodes as before")
{
    CHECK_FALSE(golden_expected_line("pmr_people").empty());
    CHECK_EQ(golden_line<pmr_people>("pmr_people"), golden_expected_line("pmr_people"));
}

TEST_CASE("golden bytes: unsorted encodes as before")
{
    CHECK_FALSE(golden_expected_line("unsorted").empty());
    CHECK_EQ(golden_line<unsorted>("unsorted"), golden_expected_line("unsorted"));
}

TEST_CASE("golden bytes: unsorted_other_order encodes as before")
{
    CHECK_FALSE(golden_expected_line("unsorted_other_order").empty());
    CHECK_EQ(golden_line<unsorted_other_order>("unsorted_other_order"),
             golden_expected_line("unsorted_other_order"));
}

TEST_CASE("golden bytes: renamed encodes as before")
{
    CHECK_FALSE(golden_expected_line("renamed").empty());
    CHECK_EQ(golden_line<renamed>("renamed"), golden_expected_line("renamed"));
}

TEST_CASE("golden bytes: listed_other_order encodes as before")
{
    CHECK_FALSE(golden_expected_line("listed_other_order").empty());
    CHECK_EQ(golden_line<listed_other_order>("listed_other_order"),
             golden_expected_line("listed_other_order"));
}

TEST_CASE("golden bytes: sorted_inner encodes as before")
{
    CHECK_FALSE(golden_expected_line("sorted_inner").empty());
    CHECK_EQ(golden_line<sorted_inner>("sorted_inner"), golden_expected_line("sorted_inner"));
}

TEST_CASE("golden bytes: sorted_outer encodes as before")
{
    CHECK_FALSE(golden_expected_line("sorted_outer").empty());
    CHECK_EQ(golden_line<sorted_outer>("sorted_outer"), golden_expected_line("sorted_outer"));
}

TEST_CASE("golden bytes: tire encodes as before")
{
    CHECK_FALSE(golden_expected_line("tire").empty());
    CHECK_EQ(golden_line<tire>("tire"), golden_expected_line("tire"));
}

TEST_CASE("golden bytes: vehicle encodes as before")
{
    CHECK_FALSE(golden_expected_line("vehicle").empty());
    CHECK_EQ(golden_line<vehicle>("vehicle"), golden_expected_line("vehicle"));
}

TEST_CASE("golden bytes: badge encodes as before")
{
    CHECK_FALSE(golden_expected_line("badge").empty());
    CHECK_EQ(golden_line<badge>("badge"), golden_expected_line("badge"));
}

#if defined(__STDCPP_FLOAT16_T__)
TEST_CASE("golden bytes: numbers encodes as before")
{
    CHECK_FALSE(golden_expected_line("numbers").empty());
    CHECK_EQ(golden_line<numbers>("numbers"), golden_expected_line("numbers"));
}
#endif

TEST_CASE("golden bytes: doubles encodes as before")
{
    CHECK_FALSE(golden_expected_line("doubles").empty());
    CHECK_EQ(golden_line<doubles>("doubles"), golden_expected_line("doubles"));
}

TEST_CASE("golden bytes: garage encodes as before")
{
    CHECK_FALSE(golden_expected_line("garage").empty());
    CHECK_EQ(golden_line<garage>("garage"), golden_expected_line("garage"));
}

TEST_CASE("golden bytes: keyed encodes as before")
{
    CHECK_FALSE(golden_expected_line("keyed").empty());
    CHECK_EQ(golden_line<keyed>("keyed"), golden_expected_line("keyed"));
}

TEST_CASE("golden bytes: passkey_user encodes as before")
{
    CHECK_FALSE(golden_expected_line("passkey_user").empty());
    CHECK_EQ(golden_line<passkey_user>("passkey_user"), golden_expected_line("passkey_user"));
}

TEST_CASE("golden bytes: passkey_login encodes as before")
{
    CHECK_FALSE(golden_expected_line("passkey_login").empty());
    CHECK_EQ(golden_line<passkey_login>("passkey_login"), golden_expected_line("passkey_login"));
}

TEST_CASE("golden bytes: chain19 encodes as before")
{
    CHECK_FALSE(golden_expected_line("chain19").empty());
    CHECK_EQ(golden_line<chain19>("chain19"), golden_expected_line("chain19"));
}

TEST_CASE("golden bytes: chain10 encodes as before")
{
    CHECK_FALSE(golden_expected_line("chain10").empty());
    CHECK_EQ(golden_line<chain10>("chain10"), golden_expected_line("chain10"));
}

TEST_CASE("golden bytes: chain0 encodes as before")
{
    CHECK_FALSE(golden_expected_line("chain0").empty());
    CHECK_EQ(golden_line<chain0>("chain0"), golden_expected_line("chain0"));
}

TEST_CASE("golden bytes: point_a encodes as before")
{
    CHECK_FALSE(golden_expected_line("point_a").empty());
    CHECK_EQ(golden_line<point_a>("point_a"), golden_expected_line("point_a"));
}

TEST_CASE("golden bytes: point_b encodes as before")
{
    CHECK_FALSE(golden_expected_line("point_b").empty());
    CHECK_EQ(golden_line<point_b>("point_b"), golden_expected_line("point_b"));
}

TEST_CASE("golden bytes: ticket encodes as before")
{
    CHECK_FALSE(golden_expected_line("ticket").empty());
    CHECK_EQ(golden_line<ticket>("ticket"), golden_expected_line("ticket"));
}

TEST_CASE("golden bytes: probe encodes as before")
{
    CHECK_FALSE(golden_expected_line("probe").empty());
    CHECK_EQ(golden_line<probe>("probe"), golden_expected_line("probe"));
}

TEST_CASE("golden bytes: lot_wheel encodes as before")
{
    CHECK_FALSE(golden_expected_line("lot_wheel").empty());
    CHECK_EQ(golden_line<lot_wheel>("lot_wheel"), golden_expected_line("lot_wheel"));
}

TEST_CASE("golden bytes: lot_engine encodes as before")
{
    CHECK_FALSE(golden_expected_line("lot_engine").empty());
    CHECK_EQ(golden_line<lot_engine>("lot_engine"), golden_expected_line("lot_engine"));
}

TEST_CASE("golden bytes: lot_car encodes as before")
{
    CHECK_FALSE(golden_expected_line("lot_car").empty());
    CHECK_EQ(golden_line<lot_car>("lot_car"), golden_expected_line("lot_car"));
}

TEST_CASE("golden bytes: parking_lot encodes as before")
{
    CHECK_FALSE(golden_expected_line("parking_lot").empty());
    CHECK_EQ(golden_line<parking_lot>("parking_lot"), golden_expected_line("parking_lot"));
}

TEST_CASE("golden bytes: point encodes as before")
{
    CHECK_FALSE(golden_expected_line("point").empty());
    CHECK_EQ(golden_line<point>("point"), golden_expected_line("point"));
}

TEST_CASE("golden bytes: cloud encodes as before")
{
    CHECK_FALSE(golden_expected_line("cloud").empty());
    CHECK_EQ(golden_line<cloud>("cloud"), golden_expected_line("cloud"));
}

TEST_CASE("golden bytes: gauge encodes as before")
{
    CHECK_FALSE(golden_expected_line("gauge").empty());
    CHECK_EQ(golden_line<gauge>("gauge"), golden_expected_line("gauge"));
}

TEST_CASE("golden bytes: gauges encodes as before")
{
    CHECK_FALSE(golden_expected_line("gauges").empty());
    CHECK_EQ(golden_line<gauges>("gauges"), golden_expected_line("gauges"));
}

TEST_CASE("golden bytes: filler_type<3> encodes as before")
{
    CHECK_FALSE(golden_expected_line("filler_type<3>").empty());
    CHECK_EQ(golden_line<filler_type<3>>("filler_type<3>"), golden_expected_line("filler_type<3>"));
}

TEST_CASE("golden bytes: fifteen_entries encodes as before")
{
    CHECK_FALSE(golden_expected_line("fifteen_entries").empty());
    CHECK_EQ(golden_line<fifteen_entries>("fifteen_entries"), golden_expected_line("fifteen_entries"));
}

TEST_CASE("golden bytes: eighteen_entries encodes as before")
{
    CHECK_FALSE(golden_expected_line("eighteen_entries").empty());
    CHECK_EQ(golden_line<eighteen_entries>("eighteen_entries"), golden_expected_line("eighteen_entries"));
}

TEST_CASE("golden bytes: everything encodes as before")
{
    CHECK_FALSE(golden_expected_line("everything").empty());
    CHECK_EQ(golden_line<everything>("everything"), golden_expected_line("everything"));
}
