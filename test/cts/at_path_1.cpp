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
// path at compile time. Cases 0 to 75 of 706.
TEST_CASE("path: the JSONPath Compliance Test Suite in the compile mode, cases 0 to 75")
{
    auto const suite = test::suite_of(JSONPATH_CTS "/cts.cbor");
    test::check_valid_cases(suite,
        std::to_array<std::tuple<std::size_t, std::string_view, std::expected<value, cbor::error> (*)(cbor::lazy const &)>>({
            {0, "$"sv, &test::query<"$">},
            {3, "$.a"sv, &test::query<"$.a">},
            {10, "$.a.b.c"sv, &test::query<"$.a.b.c">},
            {11, "$.*"sv, &test::query<"$.*">},
            {13, "$[*]"sv, &test::query<"$[*]">},
            {14, "$.*.a"sv, &test::query<"$.*.a">},
            {15, "$[0,2]"sv, &test::query<"$[0,2]">},
            {19, "$['a',1]"sv, &test::query<"$['a',1]">},
            {21, "$[1,5:7]"sv, &test::query<"$[1,5:7]">},
            {22, "$[1,0:3]"sv, &test::query<"$[1,0:3]">},
            {23, "$[1,1]"sv, &test::query<"$[1,1]">},
            {24, "$[*,1]"sv, &test::query<"$[*,1]">},
            {25, "$[*,'a']"sv, &test::query<"$[*,'a']">},
            {26, "$[*,0:2]"sv, &test::query<"$[*,0:2]">},
            {27, "$[*,*]"sv, &test::query<"$[*,*]">},
            {29, "$..[1]"sv, &test::query<"$..[1]">},
            {30, "$..a"sv, &test::query<"$..a">},
            {31, "$.true"sv, &test::query<"$.true">},
            {32, "$.false"sv, &test::query<"$.false">},
            {33, "$.null"sv, &test::query<"$.null">},
            {34, "$..*"sv, &test::query<"$..*">},
            {35, "$..[*]"sv, &test::query<"$..[*]">},
            {40, "$..['a','d']"sv, &test::query<"$..['a','d']">},
            {45, "$[?@]"sv, &test::query<"$[?@]">},
            {46, "$[?@.a]"sv, &test::query<"$[?@.a]">},
            {48, "$[?$]"sv, &test::query<"$[?$]">},
            {49, "$[?$.*.a]"sv, &test::query<"$[?$.*.a]">},
            {50, "$[?@.a=='b']"sv, &test::query<"$[?@.a=='b']">},
            {54, "$[?@.a==1]"sv, &test::query<"$[?@.a==1]">},
            {55, "$[?@.a==null]"sv, &test::query<"$[?@.a==null]">},
            {57, "$[?@.a==true]"sv, &test::query<"$[?@.a==true]">},
            {58, "$[?@.a==false]"sv, &test::query<"$[?@.a==false]">},
            {59, "$[?@==@]"sv, &test::query<"$[?@==@]">},
            {60, "$[?$==$]"sv, &test::query<"$[?$==$]">},
            {61, "$[?@.absent==@.list[9]]"sv, &test::query<"$[?@.absent==@.list[9]]">},
            {62, "$[?@.a==@.b]"sv, &test::query<"$[?@.a==@.b]">},
            {64, "$[?@.a!='b']"sv, &test::query<"$[?@.a!='b']">},
            {70, "$[?@.a!=1]"sv, &test::query<"$[?@.a!=1]">},
            {72, "$[?@.a!=null]"sv, &test::query<"$[?@.a!=null]">},
            {74, "$[?@.a!=true]"sv, &test::query<"$[?@.a!=true]">},
            {75, "$[?@.a!=false]"sv, &test::query<"$[?@.a!=false]">},
        }));
    test::check_validity(suite, std::to_array<std::tuple<std::size_t, std::string_view, bool>>({
            {1, " $"sv, cbor::is_valid_path_v<" $">},
            {2, "$ "sv, cbor::is_valid_path_v<"$ ">},
            {6, "$.&"sv, cbor::is_valid_path_v<"$.&">},
            {7, "$.1"sv, cbor::is_valid_path_v<"$.1">},
            {16, "$[0 2]"sv, cbor::is_valid_path_v<"$[0 2]">},
            {17, "$[,0]"sv, cbor::is_valid_path_v<"$[,0]">},
            {18, "$[0,]"sv, cbor::is_valid_path_v<"$[0,]">},
            {28, "$[]"sv, cbor::is_valid_path_v<"$[]">},
            {42, "$.."sv, cbor::is_valid_path_v<"$..">},
            {43, "$[@.a]"sv, cbor::is_valid_path_v<"$[@.a]">},
            {44, "$[$.a]"sv, cbor::is_valid_path_v<"$[$.a]">},
    }));
}
