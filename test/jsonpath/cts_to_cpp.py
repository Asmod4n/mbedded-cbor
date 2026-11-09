# Writes test/cts/at_path_1.cpp to at_path_4.cpp from cts.json of the JSONPath Compliance Test Suite.
# A path of the compile mode is a template argument, so each selector of the suite stands in the source. Each file
# holds the cases of one quarter of the suite by index: a valid case as a call of lazy::at_path<Path, nodelist>,
# every other case as cbor::is_valid_path_v<Path>. Each selector is an ordinary string literal in which every byte
# outside printable ASCII, the quote and the backslash are octal escapes, so the selector reaches the compiler
# byte for byte; a raw string turns a CR into a LF. The text beside it is a std::string_view literal, which keeps a
# NUL of the selector.
# Usage: python3 -I cts_to_cpp.py cts.json <directory of the four files>
import json
import os
import sys

PARTS = 4


def literal(text):
    out = []
    for b in text.encode("utf-8", "surrogatepass"):
        if 0x20 <= b < 0x7F and b not in (0x22, 0x5C):
            out.append(chr(b))
        else:
            out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


def without_support(selector):
    return "match(" in selector or "search(" in selector


def part(cases, first, end, number):
    valid = []
    other = []
    for i in range(first, end):
        c = cases[i]
        s = literal(c["selector"])
        if c.get("invalid_selector") or without_support(c["selector"]):
            other.append("            {%d, %ssv, cbor::is_valid_path_v<%s>}," % (i, s, s))
        else:
            valid.append("            {%d, %ssv, &test::query<%s>}," % (i, s, s))
    lines = [
        "// Written by test/jsonpath/cts_to_cpp.py from test/jsonpath/cts.json; write it again with that script.",
        "#include \"../cts.hpp\"",
        "",
        "#include <array>",
        "#include <expected>",
        "#include <string_view>",
        "#include <tuple>",
        "",
        "using namespace std::string_view_literals;",
        "",
        "// The JSONPath Compliance Test Suite checks RFC 9535 case by case. The compile mode makes each path program",
        "// code, so each selector of the suite stands here as a template argument, and the generated walk must give the",
        "// result that the suite allows and the result of the run-time mode. A selector that RFC 9535 refuses, or that",
        "// holds match() or search(), must not be a valid path at compile time. Cases %d to %d of %d." % (first, end - 1, len(cases)),
        "TEST_CASE(\"path: the JSONPath Compliance Test Suite in the compile mode, cases %d to %d\")" % (first, end - 1),
        "{",
        "    auto const suite = test::suite_of(JSONPATH_CTS \"/cts.cbor\");",
        "    test::check_valid_cases(suite,",
        "        std::to_array<std::tuple<std::size_t, std::string_view, std::expected<value, cbor::error> (*)(cbor::lazy const &)>>({",
    ]
    lines += valid
    lines += [
        "        }));",
        "    test::check_validity(suite, std::to_array<std::tuple<std::size_t, std::string_view, bool>>({",
    ]
    lines += other
    lines += ["    }));", "}", ""]
    with open(os.path.join(sys.argv[2], "at_path_%d.cpp" % number), "w") as f:
        f.write("\n".join(lines))


def main():
    cases = json.load(open(sys.argv[1]))["tests"]
    size = (len(cases) + PARTS - 1) // PARTS
    for number in range(1, PARTS + 1):
        part(cases, (number - 1) * size, min(number * size, len(cases)), number)


main()
