# Writes test/cts/at_path_1.cpp to at_path_4.cpp from cts.json of the JSONPath Compliance Test Suite.
# A path of the compile mode is a template argument, and each distinct path is its own generated walk. Two selectors
# that differ only in a name, a string, the value of an integer of the same sign or in whitespace have the same kinds,
# counts and flags of their parts, so they generate the same form of code. The files hold the first valid case of each
# form as a call of lazy::at_path<Path, nodelist>; test/cts.cpp runs every case in the run-time mode. A number with a
# fraction or an exponent keeps its text in the form, because the compile mode parses it with its own code. Every
# other case stands as cbor::is_valid_path_v<Path>. The four files hold about the same number of valid cases. Each
# selector is an ordinary string literal in which every byte outside printable ASCII, the quote and the backslash are
# octal escapes, so the selector reaches the compiler byte for byte; a raw string turns a CR into a LF. The text beside
# it is a std::string_view literal, which keeps a NUL of the selector.
# Usage: python3 -I cts_to_cpp.py cts.json <directory of the four files>
import json
import os
import re
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


NUMBER = re.compile(r"-?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?")
NAME = re.compile(r"[A-Za-z_\u0080-\U0010FFFF][A-Za-z0-9_\u0080-\U0010FFFF]*")
KEPT_NAMES = ("true", "false", "null", "length", "count", "value", "match", "search")


def form(selector):
    out = []
    i = 0
    while i < len(selector):
        c = selector[i]
        if c in "'\"":
            i += 1
            while i < len(selector) and selector[i] != c:
                i += 2 if selector[i] == "\\" else 1
            i += 1
            out.append("S")
        elif c.isspace():
            i += 1
        elif NUMBER.match(selector, i) and (c.isdigit() or c == "-"):
            m = NUMBER.match(selector, i)
            if m.group(1) or m.group(2):
                out.append(m.group(0))
            else:
                out.append("Z" if int(m.group(0)) == 0 else "M" if m.group(0).startswith("-") else "P")
            i = m.end()
        elif NAME.match(selector, i):
            m = NAME.match(selector, i)
            out.append(m.group(0) if m.group(0) in KEPT_NAMES else "N")
            i = m.end()
        else:
            out.append(c)
            i += 1
    return " ".join(out)


def without_support(selector):
    return "match(" in selector or "search(" in selector


def part(cases, kept, first, end, number):
    valid = []
    other = []
    for i in range(first, end):
        c = cases[i]
        s = literal(c["selector"])
        if c.get("invalid_selector") or without_support(c["selector"]):
            other.append("            {%d, %ssv, cbor::is_valid_path_v<%s>}," % (i, s, s))
        elif i in kept:
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
        "// code, and selectors that differ only in names, strings, integer values or whitespace make the same form of",
        "// code. So the first valid case of each form stands here as a template argument, and the generated walk must",
        "// give the result that the suite allows and the result of the run-time mode; test/cts.cpp runs every case in",
        "// the run-time mode. A selector that RFC 9535 refuses, or that holds match() or search(), must not be a valid",
        "// path at compile time. Cases %d to %d of %d." % (first, end - 1, len(cases)),
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
    first_of_form = {}
    for i, c in enumerate(cases):
        if not c.get("invalid_selector") and not without_support(c["selector"]):
            first_of_form.setdefault(form(c["selector"]), i)
    kept = sorted(first_of_form.values())
    bounds = [0] + [kept[len(kept) * number // PARTS] for number in range(1, PARTS)] + [len(cases)]
    for number in range(1, PARTS + 1):
        part(cases, set(kept), bounds[number - 1], bounds[number], number)


main()
