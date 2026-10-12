# Compares, for each decoder, the decoded value of the bytes that the library wrote with the decoded value of
# the expected bytes. Prints every finding with the bytes, the decoder and its version, and exits 1 when a
# finding exists. A finding is a written item that a decoder refuses while it reads the expected item, a
# written item that reads as another value, or a value that a writer of the library refuses.
# usage: check.py <cases.tsv> <versions.tsv> <decoder>=<results.tsv>...
import collections
import sys

# The library refuses these inputs on purpose: indefinite length is never written, and the nesting depth has a
# limit. A re-encode of such an input has nothing to write.
REFUSED = ("!indefinite length", "!nesting depth exceeded")

cases = {}
for line in open(sys.argv[1]):
    name, section, written, expected = line.rstrip("\n").split("\t")
    cases[name] = (section, written, expected)
versions = dict(line.rstrip("\n").split("\t", 1) for line in open(sys.argv[2]))

findings = []
notes = collections.Counter()
limits = collections.defaultdict(list)
for name, (section, written, expected) in cases.items():
    if written.startswith("!"):
        if written in REFUSED:
            notes["input refused by the library: " + written[1:]] += 1
        else:
            findings.append(("library", name, section, written, expected, written, ""))
counts = {}
for arg in sys.argv[3:]:
    decoder, path = arg.split("=", 1)
    ok = 0
    for line in open(path):
        name, got, want = line.rstrip("\n").split("\t")
        section, written, expected = cases[name]
        if got.startswith("!") and (want == "-" or not want.startswith("!")):
            findings.append((decoder, name, section, written, expected, got, want))
        elif got.startswith("!"):
            limits[decoder].append((name, got))
        elif want.startswith("!"):
            limits[decoder].append((name, "expected only: " + want))
        elif want != "-" and got != want:
            findings.append((decoder, name, section, written, expected, got, want))
        else:
            ok += 1
    counts[decoder] = ok

for d, n in counts.items():
    print("%-10s %-28s agree %d, decoder limits %d" % (d, versions.get(d, "?"), n, len(limits[d])))
for k, n in notes.items():
    print("note: %d cases: %s" % (n, k))
for d, items in limits.items():
    for name, why in items:
        print("limit %s %s: %s" % (d, name, why[:160]))
for d, name, section, written, expected, got, want in findings:
    print("FINDING %s (%s) %s [%s]\n  written  %s\n  expected %s\n  decoded  %s\n  from exp %s"
          % (d, versions.get(d, "library"), name, section, written, expected, got[:300], want[:300]))
print("findings: %d" % len(findings))
sys.exit(1 if findings else 0)
