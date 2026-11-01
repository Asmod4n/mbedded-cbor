#include "edn.hpp"
#include <cbor/diagnostic_notation.hpp>

#include <charconv>
#include <string>
#include <string_view>

// draft-ietf-cbor-edn-literals-28 4.3: (_ chunks) is an indefinite-length string of its chunks, all byte strings or
// all text strings; RFC 8949 3.2.3 gives it the value of the chunks joined. The examples of 4.3 select the key of
// that value.
TEST_CASE("path edn streamstring: the examples of 4.3 of the EDN draft")
{
    selects("(_ h'0123', h'4567')", "4401234567");
    selects("(_ \"foo\", \"bar\")", "66666f6f626172");
    selects("(_ '')", "40");
    selects("(_ \"\")", "60");
    selects("(_ 'a' 'b')", "426162");
    selects("(_ h'01', )", "4101");
    selects("(_/c/ h'01')", "4101");
    selects("(_ <<1>>, b64'Ag')", "420102");
}

// 4.3: "(_ )" is not used, as it does not tell a byte string from a text string; 5.1: a chunk is a string with no
// encoding indicator, and MS, mandatory space, follows "(_". RFC 8949 3.2.3: a chunk is a definite string of the
// type of the whole.
TEST_CASE("path edn streamstring: forms 4.3 and 5.1 of the EDN draft do not have")
{
    for (std::string_view const l : {"(_ )"sv, "(_ h'01', \"a\")"sv, "(_ 1)"sv, "(_h'01')"sv, "(_ (_ h'01'))"sv, "(_ \"a\"_0)"sv,
                                     "(_ \"a\"_i)"sv, "(_ ''_)"sv, "(_ h'01'"sv, "(_ h'01' h'02'"sv})
        refused(l);
}

// RFC 8949 Appendix A: the diagnostic notation that cbor::diagnostic_notation writes for an example is a literal
// of the same value. Every example that a selector reads as a key literal (not an integer, which is an index, and
// not a text, which is a name of RFC 9535) selects the key of its own bytes. Not here: the examples whose key is
// of indefinite length, as at_path does not read indefinite-length input (doc/basics.md). Three floats whose decimal
// form has more digits than an exact conversion takes read back only where std::from_chars converts them.
TEST_CASE("path edn streamstring: the diagnostic notation of each example of RFC 8949 Appendix A is a key")
{
    for (std::string_view const hex :
         {"c249010000000000000000"sv, "c349010000000000000000"sv, "f90000"sv, "f98000"sv, "f93c00"sv,
          "fb3ff199999999999a"sv, "f93e00"sv, "f97bff"sv, "fa47c35000"sv, "f90400"sv, "f9c400"sv,
          "fbc010666666666666"sv, "f97c00"sv, "f97e00"sv, "f9fc00"sv, "fa7f800000"sv, "fa7fc00000"sv, "faff800000"sv,
          "fb7ff0000000000000"sv, "fb7ff8000000000000"sv, "fbfff0000000000000"sv, "f4"sv, "f5"sv, "f6"sv, "f7"sv,
          "f0"sv, "f8ff"sv, "c074323031332d30332d32315432303a30343a30305a"sv, "c11a514b67b0"sv,
          "c1fb41d452d9ec200000"sv, "d74401020304"sv, "d818456449455446"sv,
          "d82076687474703a2f2f7777772e6578616d706c652e636f6d"sv, "40"sv, "4401020304"sv, "80"sv, "83010203"sv,
          "8301820203820405"sv, "98190102030405060708090a0b0c0d0e0f101112131415161718181819"sv, "a0"sv,
          "a201020304"sv, "a26161016162820203"sv, "826161a161626163"sv,
          "a56161614161626142616361436164614461656145"sv}) {
        auto const text = cbor::diagnostic_notation(bytes_of_hex(hex));
        REQUIRE(text.has_value());
        selects(*text, hex);
    }
#if defined(__cpp_lib_to_chars)
    for (std::string_view const hex : {"fa7f7fffff"sv, "fb7e37e43c8800759c"sv, "f90001"sv}) {
        auto const text = cbor::diagnostic_notation(bytes_of_hex(hex));
        REQUIRE(text.has_value());
        selects(*text, hex);
    }
#endif
}
