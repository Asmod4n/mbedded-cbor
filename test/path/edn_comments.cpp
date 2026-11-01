#include "edn.hpp"

#include <string_view>

// draft-ietf-cbor-edn-literals-28 3.1: comments are blank space in h'', and in b64'' only those that start with #,
// as "/" is a base64 character. The examples of the section stand for the bytes of the plain literal. The comment
// /world/ after the first example stands outside the literal, where a path is JSONPath and takes no comment.
TEST_CASE("path edn comments: the commented byte strings of 3.1 of the EDN draft")
{
    selects("h'68 65 6c /doubled l!/ 6c 6f # hello\n  20 /space/\n  77 6f 72 6c 64'", "4b68656c6c6f20776f726c64");
    selects("b64'/base64 not a comment/ but one follows # comment\n'", "5818FDB6AC7BAE27A2D69CA2699E9EDFDBBADA2779FA25968C2C");
    selects("h'/head/ 63 /contents/ 66 6f 6f'", "4463666f6f");
    selects("<<\"foo\">>", "4463666f6f");
}

// 5.2.1, app-string-h: blank space with comments stands around each hex digit, and the content may end in an
// eol-comment without its line feed. A "/" comment without its closing "/" is no h''. 2.3.2: the single-quoted
// string ends at its quote, so an escaped quote inside a comment is part of the comment.
TEST_CASE("path edn comments: comments inside h of the EDN draft")
{
    selects("h'01 /x/ // e'", "4101");
    selects("h'0/x/1'", "4101");
    selects("h'01 /it\\'s/ 02'", "420102");
    selects("h'01 # one\n02'", "420102");
    refused("h'01 /x'");
    refused("h'01 /x' /y/ '");
}

// 5.1, rule comment: a comment in slashes, a block comment that ends in */, and an eol-comment # or // that ends
// with its line feed. Each form is blank space between the items of an array.
TEST_CASE("path edn comments: every comment form of 5.1 of the EDN draft between items")
{
    for (std::string_view const l : {"[1, /c/ 2]"sv, "[1, /* c */ 2]"sv, "[1, # c\n 2]"sv, "[1, // c\n 2]"sv, "[1, /*a**/ 2]"sv,
                                     "[1, /a//b/ 2]"sv, "[1, /* a / b */ 2]"sv, "[1,/c/2]"sv, "[1/c/, 2]"sv})
        selects(l, "820102");
    for (std::string_view const l : {"[1, /*foo/ 2]"sv, "[1, /c 2]"sv, "[1 # x]"sv, "[1, /*/ 2]"sv})
        refused(l);
}

// 5.1: S stands between the parts of a map entry, inside simple( ), inside the parentheses of a tag, inside << >>
// and before the closing bracket; it does not stand between a tag number and its "(".
TEST_CASE("path edn comments: comments where rule S of the EDN draft stands")
{
    selects("{1: /c/ 2}", "a10102");
    selects("{1 /c/ : 2}", "a10102");
    selects("simple( /c/ 1 /c/ )", "e1");
    selects("1(/c/ 2 /c/)", "c102");
    selects("<</c/ 1 /c/>>", "4101");
    selects("[1, /c/]", "8101");
    selects("[1/c/]", "8101");
    selects("[1.5/c/]", "81f93e00");
    refused("1 /c/ (2)");
}

// 1.3.5 and 5.1, rule unescaped: a carriage return in a string literal is ignored on input, so a text with CR LF is
// the text with LF alone.
TEST_CASE("path edn comments: a carriage return in a string literal is not part of it")
{
    selects("[\"a\r\nb\"]", "8163610a62");
    selects("[\"a\nb\"]", "8163610a62");
}

// RFC 9535 has no comments and no raw control character in a name (2.3.1.1, unescaped starts at %x20). Comments
// are part of an EDN literal only, never of the JSONPath around it.
TEST_CASE("path edn comments: the JSONPath around a literal takes no comment and no raw control character")
{
    refused("1 /c/");
    refused("/c/ 1");
    for (std::string_view const path : {"$[?@.a == 1 /c/]"sv, "$[?/c/ @.a]"sv, "$['a\nb']"sv, "$['a\rb']"sv, "$[\"a\nb\"]"sv}) {
        CAPTURE(path);
        auto const q = cbor::jsonpath::compile<cbor::lazy>(path);
        REQUIRE_FALSE(q.has_value());
        CHECK_EQ(q.error(), error::invalid_path);
    }
}
