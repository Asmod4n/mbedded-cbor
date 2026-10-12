# Compares, for each decoder, the decoded value of the bytes that the library wrote with the decoded value of
# the expected bytes. Prints every finding with the bytes, the decoder and its version, and exits 1 when a
# finding exists. A finding is a written item that a decoder refuses for a reason that UNSUPPORTED does not
# list, a written item that reads as another value, or a value that a writer of the library refuses.
# usage: check.py <cases.tsv> <versions.tsv> <decoder>=<results.tsv>...
import collections
import re
import sys

# The library refuses these inputs on purpose: indefinite length is never written, and the nesting depth has a
# limit. A re-encode of such an input has nothing to write.
REFUSED = ("!indefinite length", "!nesting depth exceeded")

cases = {}
for line in open(sys.argv[1]):
    name, section, written, expected = line.rstrip("\n").split("\t")
    cases[name] = (section, written, expected)
versions = dict(line.rstrip("\n").split("\t", 1) for line in open(sys.argv[2]))

# What each decoder does not support. RFC 8949 5.2 lets a decoder refuse what it does not want to
# understand. Each entry comes from refusals that also happen on the original RFC or cbor-wg bytes, except the
# tag 6 of libcbor and the undefined of nlohmann/json, which only Packed CBOR output showed.
# Each entry: decoder, feature, RFC section, a pattern of the message of the decoder.
UNSUPPORTED = [
    ("libcbor", "tag numbers 6 to 20 (tag 6 of Packed CBOR, tag 18 of COSE), simple values other than 20 to 23",
     "RFC 8949 3.3, 3.4", r"^!cbor_load error 3 "),
    ("jsoncons", "simple values other than 20 to 23", "RFC 8949 3.3", r"unknown type was found"),
    ("nlohmann", "tags on any item but a byte string (handler store)", "RFC 8949 3.4",
     r"invalid byte: 0x(C[0-9A-F]|D[0-9A-B])$|CBOR binary: expected length"),
    ("nlohmann", "undefined and simple values other than 20 to 22", "RFC 8949 3.3",
     r"invalid byte: 0x(E[0-9A-F]|F[0-8])$"),
    ("nlohmann", "map keys that are not text", "RFC 8949 3.1, 5.6", r"CBOR string: expected length"),
    ("cbor_xs", "simple values other than 20 to 23", "RFC 8949 3.3", r"unexpected major 7 value"),
    ("cbor_xs", "undefined as a map key", "RFC 8949 3.3, 5.6", r"Types::Serialiser::error"),
]

findings = []
unsupported = collections.Counter()
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
        if got.startswith("!"):
            entry = next((u for u in UNSUPPORTED if u[0] == decoder and re.search(u[3], got)), None)
            if entry:
                unsupported[entry] += 1
            else:
                findings.append((decoder, name, section, written, expected, got, want))
        elif want.startswith("!"):
            limits[decoder].append((name, "expected only: " + want))
        elif want != "-" and got != want:
            findings.append((decoder, name, section, written, expected, got, want))
        else:
            ok += 1
    counts[decoder] = ok

for d, n in counts.items():
    print("%-10s %-28s agree %d" % (d, versions.get(d, "?"), n))
for (d, feature, section, _), n in unsupported.items():
    print("not supported by %s (%s): %s [%s], %d cases" % (d, versions.get(d, "?"), feature, section, n))
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
