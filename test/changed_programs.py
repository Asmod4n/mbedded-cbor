# Gives the test programs of the iter build whose code a change of the working tree touches: the map of each program
# to the lines of include/ and test/ that became its code (the debug line tables of its objects), and the selection
# of the programs that a diff against the base of that map reaches.
# usage: changed_programs.py map <build> <map>
#        changed_programs.py select <map> <base tree> <source tree> <changed path>...
import difflib
import json
import os
import re
import subprocess
import sys

SOURCE = "/src/"
CODE_DIRECTORIES = ("include/", "test/")
ALL_ON_CHANGE = ("CMakeLists.txt", "cmake/", "test/run.sh", "test/main.cpp", "test/changed_programs.py")
DATA_DIRECTORIES = ("cbor-test-vectors", "test-vectors", "test/jsonpath/", "bench/docs/")
CONTROL = ("if", "for", "while", "switch", "catch", "else", "do", "try", "return")


def program_lines(objects):
    lines = {}
    for o in objects:
        out = subprocess.run(["objdump", "--dwarf=decodedline", o], capture_output=True, text=True, check=True).stdout
        current = None
        for row in out.splitlines():
            if row.endswith(":") and row.startswith("/"):
                path = row[:-1]
                current = path[len(SOURCE):] if path.startswith(SOURCE) and path[len(SOURCE):].startswith(CODE_DIRECTORIES) else None
                continue
            if current is None:
                continue
            fields = row.split()
            if len(fields) >= 2 and fields[1].isdigit():
                lines.setdefault(current, set()).add(int(fields[1]))
    return {path: sorted(numbers) for path, numbers in lines.items()}


def make_map(build, target):
    programs = {}
    files = os.path.join(build, "CMakeFiles")
    for entry in sorted(os.listdir(files)):
        if not entry.endswith(".dir"):
            continue
        name = entry[:-len(".dir")]
        if not os.path.exists(os.path.join(build, name + ".passed")):
            continue
        objects = [os.path.join(d, f) for d, _, fs in os.walk(os.path.join(files, entry)) for f in fs if f.endswith(".o")]
        programs[name] = program_lines(objects)
    with open(os.path.join(target, "lines.json"), "w") as out:
        json.dump(programs, out)


def function_spans(text):
    spans = []
    stack = []
    head = []
    i = 0
    line = 1
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            head.append(c)
            i += 1
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            line += text.count("\n", i, end)
            i = end
            continue
        raw = re.match(r'R"([^(\s]*)\(', text[i:i + 20])
        if raw:
            close = ")" + raw.group(1) + '"'
            end = text.find(close, i + raw.end())
            end = n if end < 0 else end + len(close)
            line += text.count("\n", i, end)
            i = end
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            line += text.count("\n", i, j)
            i = j + 1
            continue
        inside = any(kind == "function" for kind, _, _ in stack)
        if c == "{":
            words = "".join(head).strip()
            first = words.split()[0] if words.split() else ""
            container = re.match(r"^(template\s*<.*?>\s*)?(class|struct|union|namespace|enum)\b", words, re.S)
            if not inside and not container and "(" in words and first not in CONTROL:
                stack.append(("function", line, words))
            else:
                stack.append(("other", line, words))
            head = []
        elif c == "}":
            if stack:
                kind, start, words = stack.pop()
                if kind == "function" and not any(k == "function" for k, _, _ in stack):
                    spans.append((start, line, words))
            head = []
        elif c == ";" and not inside:
            head = []
        else:
            head.append(c)
        i += 1
    return spans


def changed_old_lines(old, new):
    lines = []
    matcher = difflib.SequenceMatcher(a=old, b=new, autojunk=False)
    for tag, a0, a1, _, _ in matcher.get_opcodes():
        if tag == "equal":
            continue
        if a1 > a0:
            lines.append(list(range(a0 + 1, a1 + 1)))
        else:
            lines.append([a0, a0 + 1])
    return lines


def select(map_directory, base, source, changed):
    with open(os.path.join(map_directory, "lines.json")) as f:
        programs = {name: {path: set(lines) for path, lines in paths.items()} for name, paths in json.load(f).items()}
    everything = set(programs)
    chosen = set()
    reasons = []
    for path in changed:
        if path.startswith(ALL_ON_CHANGE) or path.startswith(DATA_DIRECTORIES):
            reasons.append(path + ": all programs")
            return everything, reasons
    for directory in ("include", "test"):
        old_root = os.path.join(base, directory)
        new_root = os.path.join(source, directory)
        names = set()
        for root in (old_root, new_root):
            for d, _, fs in os.walk(root):
                names.update(os.path.relpath(os.path.join(d, f), root) for f in fs)
        for name in sorted(names):
            path = directory + "/" + name
            old_file = os.path.join(old_root, name)
            new_file = os.path.join(new_root, name)
            old = open(old_file, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(old_file) else []
            new = open(new_file, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(new_file) else []
            if old == new:
                continue
            if path.startswith(ALL_ON_CHANGE) or path.startswith(DATA_DIRECTORIES) or not path.endswith((".hpp", ".cpp")):
                reasons.append(path + ": all programs")
                return everything, reasons
            spans = function_spans("\n".join(old) + "\n")
            for hunk in changed_old_lines(old, new):
                around = [s for s in spans if s[0] <= min(hunk) and max(hunk) <= s[1]]
                if not around:
                    reasons.append(f"{path}:{hunk[0]}: outside a function: all programs")
                    return everything, reasons
                start, end, words = min(around, key=lambda s: s[1] - s[0])
                if re.search(r"\b(constexpr|consteval)\b", words):
                    reasons.append(f"{path}:{hunk[0]}: in a constexpr or consteval function: all programs")
                    return everything, reasons
                hits = {p for p, paths in programs.items() if paths.get(path, set()) & set(range(start, end + 1))}
                if not hits:
                    reasons.append(f"{path}:{hunk[0]}: in a function that no program compiled: all programs")
                    return everything, reasons
                reasons.append(f"{path}:{hunk[0]}: {len(hits)} programs")
                chosen |= hits
    return chosen, reasons


def main():
    if sys.argv[1] == "map":
        make_map(sys.argv[2], sys.argv[3])
    else:
        chosen, reasons = select(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:])
        for r in reasons:
            print("select: " + r, file=sys.stderr)
        print(" ".join(sorted(chosen)))


main()
