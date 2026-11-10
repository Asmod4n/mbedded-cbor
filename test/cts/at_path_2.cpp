// Written by test/jsonpath/cts_to_cpp.py from test/jsonpath/cts.json; write it again with that script.
#include "../cts.hpp"

#include <array>
#include <expected>
#include <string_view>
#include <tuple>

using namespace std::string_view_literals;

// The JSONPath Compliance Test Suite checks RFC 9535 case by case. The compile mode makes each path program
// code, and selectors that differ only in names, strings, integer values or whitespace make the same form of
// code. So the first valid case of each form stands here as a template argument, and the generated walk must
// give the result that the suite allows and the result of the run-time mode; test/cts.cpp runs every case in
// the run-time mode. A selector that RFC 9535 refuses, or that holds match() or search(), must not be a valid
// path at compile time. Cases 76 to 157 of 706.
TEST_CASE("path: the JSONPath Compliance Test Suite in the compile mode, cases 76 to 157")
{
    auto const suite = test::suite_of(JSONPATH_CTS "/cts.cbor");
    test::check_valid_cases(suite,
        std::to_array<std::tuple<std::size_t, std::string_view, std::expected<value, cbor::error> (*)(cbor::lazy const &)>>({
            {76, "$[?@.a<'c']"sv, &test::query<"$[?@.a<'c']">},
            {78, "$[?@.a<10]"sv, &test::query<"$[?@.a<10]">},
            {79, "$[?@.a<null]"sv, &test::query<"$[?@.a<null]">},
            {80, "$[?@.a<true]"sv, &test::query<"$[?@.a<true]">},
            {81, "$[?@.a<false]"sv, &test::query<"$[?@.a<false]">},
            {82, "$[?@.a<='c']"sv, &test::query<"$[?@.a<='c']">},
            {84, "$[?@.a<=10]"sv, &test::query<"$[?@.a<=10]">},
            {85, "$[?@.a<=null]"sv, &test::query<"$[?@.a<=null]">},
            {86, "$[?@.a<=true]"sv, &test::query<"$[?@.a<=true]">},
            {87, "$[?@.a<=false]"sv, &test::query<"$[?@.a<=false]">},
            {88, "$[?@.a>'c']"sv, &test::query<"$[?@.a>'c']">},
            {90, "$[?@.a>10]"sv, &test::query<"$[?@.a>10]">},
            {91, "$[?@.a>null]"sv, &test::query<"$[?@.a>null]">},
            {92, "$[?@.a>true]"sv, &test::query<"$[?@.a>true]">},
            {93, "$[?@.a>false]"sv, &test::query<"$[?@.a>false]">},
            {94, "$[?@.a>='c']"sv, &test::query<"$[?@.a>='c']">},
            {96, "$[?@.a>=10]"sv, &test::query<"$[?@.a>=10]">},
            {97, "$[?@.a>=null]"sv, &test::query<"$[?@.a>=null]">},
            {98, "$[?@.a>=true]"sv, &test::query<"$[?@.a>=true]">},
            {99, "$[?@.a>=false]"sv, &test::query<"$[?@.a>=false]">},
            {100, "$[?@.a&&@.a!=null]"sv, &test::query<"$[?@.a&&@.a!=null]">},
            {101, "$[?@.a&&@.b]"sv, &test::query<"$[?@.a&&@.b]">},
            {102, "$[?@.a||@.b]"sv, &test::query<"$[?@.a||@.b]">},
            {103, "$[?@.a>0&&@.a<10]"sv, &test::query<"$[?@.a>0&&@.a<10]">},
            {104, "$[?@.a=='b'||@.a=='d']"sv, &test::query<"$[?@.a=='b'||@.a=='d']">},
            {105, "$[?!(@.a=='b')]"sv, &test::query<"$[?!(@.a=='b')]">},
            {106, "$[?!@.a]"sv, &test::query<"$[?!@.a]">},
            {108, "$[?@.*]"sv, &test::query<"$[?@.*]">},
            {109, "$[?@[0, 0, 'a']]"sv, &test::query<"$[?@[0, 0, 'a']]">},
            {110, "$[?@[0:2]]"sv, &test::query<"$[?@[0:2]]">},
            {111, "$[?!@.*]"sv, &test::query<"$[?!@.*]">},
            {116, "$[?@[?@>1]]"sv, &test::query<"$[?@[?@>1]]">},
            {118, "$[?@['0'] == 5]"sv, &test::query<"$[?@['0'] == 5]">},
            {119, "$[?@[0] == 5]"sv, &test::query<"$[?@[0] == 5]">},
            {120, "$[?@.a==1].b.x"sv, &test::query<"$[?@.a==1].b.x">},
            {121, "$[?@.z=='_']['x','y']"sv, &test::query<"$[?@.z=='_']['x','y']">},
            {152, "$[?@.a,?@.b]"sv, &test::query<"$[?@.a,?@.b]">},
            {153, "$[?@.a=='b',?@.b=='x']"sv, &test::query<"$[?@.a=='b',?@.b=='x']">},
            {155, "$[?@.a,1]"sv, &test::query<"$[?@.a,1]">},
            {156, "$[?@.a,*]"sv, &test::query<"$[?@.a,*]">},
            {157, "$[?@.a,1:]"sv, &test::query<"$[?@.a,1:]">},
        }));
    test::check_validity(suite, std::to_array<std::tuple<std::size_t, std::string_view, bool>>({
            {112, "$[?@[0:0]==0]"sv, cbor::is_valid_path_v<"$[?@[0:0]==0]">},
            {113, "$[?@[*]==0]"sv, cbor::is_valid_path_v<"$[?@[*]==0]">},
            {114, "$[?@..a==0]"sv, cbor::is_valid_path_v<"$[?@..a==0]">},
            {115, "$[?@.a[*].a==0]"sv, cbor::is_valid_path_v<"$[?@.a[*].a==0]">},
            {122, "$[?(@[0, 0]==42)]"sv, cbor::is_valid_path_v<"$[?(@[0, 0]==42)]">},
            {123, "$[?(@[0, 0]!=42)]"sv, cbor::is_valid_path_v<"$[?(@[0, 0]!=42)]">},
            {124, "$[?(@[0, 0]<=42)]"sv, cbor::is_valid_path_v<"$[?(@[0, 0]<=42)]">},
            {125, "$[?(@['a', 'a']==42)]"sv, cbor::is_valid_path_v<"$[?(@['a', 'a']==42)]">},
            {126, "$[?(@['a', 'a']!=42)]"sv, cbor::is_valid_path_v<"$[?(@['a', 'a']!=42)]">},
            {127, "$[?(@['a', 'a']<=42)]"sv, cbor::is_valid_path_v<"$[?(@['a', 'a']<=42)]">},
            {128, "$[?(@[0, '0']==42)]"sv, cbor::is_valid_path_v<"$[?(@[0, '0']==42)]">},
            {129, "$[?(@[0, '0']!=42)]"sv, cbor::is_valid_path_v<"$[?(@[0, '0']!=42)]">},
            {130, "$[?(@[0, '0']<=42)]"sv, cbor::is_valid_path_v<"$[?(@[0, '0']<=42)]">},
            {131, "$[?(@.*==42)]"sv, cbor::is_valid_path_v<"$[?(@.*==42)]">},
            {132, "$[?(@.*!=42)]"sv, cbor::is_valid_path_v<"$[?(@.*!=42)]">},
            {133, "$[?(@.*<=42)]"sv, cbor::is_valid_path_v<"$[?(@.*<=42)]">},
            {134, "$[?(@[0:0]==42)]"sv, cbor::is_valid_path_v<"$[?(@[0:0]==42)]">},
            {135, "$[?(@[0:0]!=42)]"sv, cbor::is_valid_path_v<"$[?(@[0:0]!=42)]">},
            {136, "$[?(@[0:0]<=42)]"sv, cbor::is_valid_path_v<"$[?(@[0:0]<=42)]">},
            {137, "$[?($[0, 0]==42)]"sv, cbor::is_valid_path_v<"$[?($[0, 0]==42)]">},
            {138, "$[?($[0, 0]!=42)]"sv, cbor::is_valid_path_v<"$[?($[0, 0]!=42)]">},
            {139, "$[?($[0, 0]<=42)]"sv, cbor::is_valid_path_v<"$[?($[0, 0]<=42)]">},
            {140, "$[?($['a', 'a']==42)]"sv, cbor::is_valid_path_v<"$[?($['a', 'a']==42)]">},
            {141, "$[?($['a', 'a']!=42)]"sv, cbor::is_valid_path_v<"$[?($['a', 'a']!=42)]">},
            {142, "$[?($['a', 'a']<=42)]"sv, cbor::is_valid_path_v<"$[?($['a', 'a']<=42)]">},
            {143, "$[?($[0, '0']==42)]"sv, cbor::is_valid_path_v<"$[?($[0, '0']==42)]">},
            {144, "$[?($[0, '0']!=42)]"sv, cbor::is_valid_path_v<"$[?($[0, '0']!=42)]">},
            {145, "$[?($[0, '0']<=42)]"sv, cbor::is_valid_path_v<"$[?($[0, '0']<=42)]">},
            {146, "$[?($.*==42)]"sv, cbor::is_valid_path_v<"$[?($.*==42)]">},
            {147, "$[?($.*!=42)]"sv, cbor::is_valid_path_v<"$[?($.*!=42)]">},
            {148, "$[?($.*<=42)]"sv, cbor::is_valid_path_v<"$[?($.*<=42)]">},
            {149, "$[?($[0:0]==42)]"sv, cbor::is_valid_path_v<"$[?($[0:0]==42)]">},
            {150, "$[?($[0:0]!=42)]"sv, cbor::is_valid_path_v<"$[?($[0:0]!=42)]">},
            {151, "$[?($[0:0]<=42)]"sv, cbor::is_valid_path_v<"$[?($[0:0]<=42)]">},
    }));
}
